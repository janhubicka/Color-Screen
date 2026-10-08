/* Versioned Color-Screen parameter archive support.
   Copyright (C) 2026 Jan Hubicka
   This file is part of ColorScreen.  */

#include "parameter-archive.h"

#include <zip.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace colorscreen
{
namespace
{

constexpr size_t manifest_max_size = 1024 * 1024;
constexpr uint64_t legacy_csp_max_size = UINT64_C (512) * 1024 * 1024;
constexpr zip_int64_t archive_max_entries = 128;
constexpr int json_max_depth = 32;
constexpr size_t json_max_nodes = 8192;

/* Store MESSAGE in ERROR and return false. Defined below with the manifest
   helpers; declared here because path staging uses the same diagnostic path. */
bool archive_fail (std::string *error, const std::string &message);

/* Open UTF-8 host path NAME for binary reading.

   libzip's host-path API already uses UTF-8. Plain CSP dispatch must follow the
   same public filename contract; on Windows fopen() alone would instead use the
   active narrow code page.  */
FILE *
open_utf8_binary_read (const char *name)
{
  if (!name)
    return nullptr;
#ifdef _WIN32
  try
    {
      const std::filesystem::path path = std::filesystem::u8path (name);
      return _wfopen (path.c_str (), L"rb");
    }
  catch (...)
    {
      errno = EINVAL;
      return nullptr;
    }
#else
  return fopen (name, "rb");
#endif
}

/* Open UTF-8 host path NAME for binary replacement/truncation. */
FILE *
open_utf8_binary_write (const char *name)
{
  if (!name)
    return nullptr;
#ifdef _WIN32
  try
    {
      const std::filesystem::path path = std::filesystem::u8path (name);
      return _wfopen (path.c_str (), L"wb");
    }
  catch (...)
    {
      errno = EINVAL;
      return nullptr;
    }
#else
  return fopen (name, "wb");
#endif
}

/* Remove UTF-8 host path NAME without throwing while cleaning a failed write. */
void
remove_utf8_path (const char *name)
{
  if (!name)
    return;
  try
    {
      std::error_code ignored;
      std::filesystem::remove (std::filesystem::u8path (name), ignored);
    }
  catch (...)
    {
      /* Best-effort staging cleanup must never replace the real diagnostic. */
    }
}

static std::atomic<unsigned long long> parameter_temp_counter { 0 };

/* Create one empty sibling staging file exclusively and return its host path. */
bool
create_parameter_staging_path (const char *name, std::filesystem::path *result,
                               std::string *error)
{
  if (!name || !result)
    return archive_fail (error, "invalid parameter staging path");

  try
    {
      const std::filesystem::path target = std::filesystem::u8path (name);
      std::filesystem::path directory = target.parent_path ();
      if (directory.empty ())
        directory = std::filesystem::path (".");

#ifdef _WIN32
      const unsigned long long pid = (unsigned long long)_getpid ();
#else
      const unsigned long long pid = (unsigned long long)getpid ();
#endif
      const std::string base = target.filename ().u8string ();
      for (int attempt = 0; attempt < 128; ++attempt)
        {
          const unsigned long long serial
              = parameter_temp_counter.fetch_add (1, std::memory_order_relaxed);
          const std::string candidate_name
              = "." + base + ".tmp-" + std::to_string (pid) + "-"
                + std::to_string (serial);
          const std::filesystem::path candidate
              = directory / std::filesystem::u8path (candidate_name);

#ifdef _WIN32
          int fd = _wopen (candidate.c_str (),
                           _O_CREAT | _O_EXCL | _O_BINARY | _O_WRONLY,
                           _S_IREAD | _S_IWRITE);
          if (fd >= 0)
            {
              _close (fd);
              *result = candidate;
              return true;
            }
#else
          int fd = open (candidate.c_str (), O_CREAT | O_EXCL | O_WRONLY, 0600);
          if (fd >= 0)
            {
              close (fd);
              *result = candidate;
              return true;
            }
#endif
          if (errno != EEXIST)
            return archive_fail (
                error, std::string ("could not create parameter staging file: ")
                           + std::strerror (errno));
        }
    }
  catch (const std::exception &exception)
    {
      return archive_fail (error,
                           std::string ("invalid parameter output path: ")
                               + exception.what ());
    }
  catch (...)
    {
      return archive_fail (error, "invalid parameter output path");
    }

  return archive_fail (error, "could not allocate unique parameter staging file");
}

/* Atomically replace TARGET with completed STAGING in the same directory. */
bool
replace_parameter_staging_path (const std::filesystem::path &staging,
                                const std::filesystem::path &target,
                                std::string *error)
{
#ifdef _WIN32
  if (MoveFileExW (staging.c_str (), target.c_str (),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  const std::error_code ec ((int)GetLastError (), std::system_category ());
  return archive_fail (error,
                       "could not replace parameter target: " + ec.message ());
#else
  if (rename (staging.c_str (), target.c_str ()) == 0)
    return true;
  return archive_fail (
      error, std::string ("could not replace parameter target: ")
                 + std::strerror (errno));
#endif
}

/* Minimal JSON value used only by the parameter-archive manifest parser.  */
struct json_value
{
  enum class kind
  {
    null_value,
    boolean,
    number,
    string,
    array,
    object
  };

  kind type = kind::null_value;
  bool boolean_value = false;
  std::string text;
  std::vector<json_value> array_value;
  std::map<std::string, json_value> object_value;
};

/* Append Unicode code point CODEPOINT to OUTPUT as UTF-8.  */
bool
append_utf8 (std::string *output, uint32_t codepoint)
{
  if (!output || codepoint > 0x10ffff
      || (codepoint >= 0xd800 && codepoint <= 0xdfff))
    return false;
  if (codepoint <= 0x7f)
    output->push_back ((char)codepoint);
  else if (codepoint <= 0x7ff)
    {
      output->push_back ((char)(0xc0 | (codepoint >> 6)));
      output->push_back ((char)(0x80 | (codepoint & 0x3f)));
    }
  else if (codepoint <= 0xffff)
    {
      output->push_back ((char)(0xe0 | (codepoint >> 12)));
      output->push_back ((char)(0x80 | ((codepoint >> 6) & 0x3f)));
      output->push_back ((char)(0x80 | (codepoint & 0x3f)));
    }
  else
    {
      output->push_back ((char)(0xf0 | (codepoint >> 18)));
      output->push_back ((char)(0x80 | ((codepoint >> 12) & 0x3f)));
      output->push_back ((char)(0x80 | ((codepoint >> 6) & 0x3f)));
      output->push_back ((char)(0x80 | (codepoint & 0x3f)));
    }
  return true;
}

/* Return the numeric value of hexadecimal character C, or -1.  */
int
hex_value (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Strict, bounded JSON parser for manifest.json.

   Unknown manifest keys are represented normally so callers can ignore them
   without using a second permissive parser.  */
class json_parser
{
public:
  json_parser (const char *begin, const char *end)
      : m_cur (begin), m_end (end)
  {
  }

  /* Parse exactly one JSON value into VALUE.  */
  bool
  parse (json_value *value)
  {
    if (!value)
      return fail ("internal JSON destination is null");
    skip_space ();
    if (!parse_value (value, 0))
      return false;
    skip_space ();
    if (m_cur != m_end)
      return fail ("trailing data after JSON manifest");
    return true;
  }

  /* Return the parser diagnostic after failure.  */
  const std::string &
  error () const
  {
    return m_error;
  }

private:
  const char *m_cur;
  const char *m_end;
  size_t m_nodes = 0;
  std::string m_error;

  /* Store MESSAGE as the first parser failure.  */
  bool
  fail (const char *message)
  {
    if (m_error.empty ())
      m_error = message;
    return false;
  }

  /* Skip JSON whitespace.  */
  void
  skip_space ()
  {
    while (m_cur != m_end
           && (*m_cur == ' ' || *m_cur == '\n' || *m_cur == '\r'
               || *m_cur == '\t'))
      ++m_cur;
  }

  /* Parse one JSON value at nesting DEPTH.  */
  bool
  parse_value (json_value *value, int depth)
  {
    if (depth > json_max_depth)
      return fail ("JSON manifest nesting is too deep");
    if (++m_nodes > json_max_nodes)
      return fail ("JSON manifest contains too many values");
    if (m_cur == m_end)
      return fail ("unexpected end of JSON manifest");

    switch (*m_cur)
      {
      case '{':
        return parse_object (value, depth + 1);
      case '[':
        return parse_array (value, depth + 1);
      case '"':
        value->type = json_value::kind::string;
        return parse_string (&value->text);
      case 't':
        if (!consume_literal ("true"))
          return false;
        value->type = json_value::kind::boolean;
        value->boolean_value = true;
        return true;
      case 'f':
        if (!consume_literal ("false"))
          return false;
        value->type = json_value::kind::boolean;
        value->boolean_value = false;
        return true;
      case 'n':
        if (!consume_literal ("null"))
          return false;
        value->type = json_value::kind::null_value;
        return true;
      default:
        value->type = json_value::kind::number;
        return parse_number (&value->text);
      }
  }

  /* Consume exact JSON literal LITERAL.  */
  bool
  consume_literal (const char *literal)
  {
    for (const char *p = literal; *p; ++p)
      {
        if (m_cur == m_end || *m_cur != *p)
          return fail ("invalid JSON literal");
        ++m_cur;
      }
    return true;
  }

  /* Parse JSON object into VALUE and reject duplicate keys.  */
  bool
  parse_object (json_value *value, int depth)
  {
    value->type = json_value::kind::object;
    ++m_cur;
    skip_space ();
    if (m_cur != m_end && *m_cur == '}')
      {
        ++m_cur;
        return true;
      }

    while (m_cur != m_end)
      {
        if (*m_cur != '"')
          return fail ("JSON object key is not a string");
        std::string key;
        if (!parse_string (&key))
          return false;
        if (value->object_value.find (key) != value->object_value.end ())
          return fail ("JSON object contains a duplicate key");

        skip_space ();
        if (m_cur == m_end || *m_cur != ':')
          return fail ("JSON object key is missing ':'");
        ++m_cur;
        skip_space ();

        json_value child;
        if (!parse_value (&child, depth))
          return false;
        value->object_value.emplace (std::move (key), std::move (child));

        skip_space ();
        if (m_cur == m_end)
          return fail ("truncated JSON object");
        if (*m_cur == '}')
          {
            ++m_cur;
            return true;
          }
        if (*m_cur != ',')
          return fail ("JSON object members are not comma-separated");
        ++m_cur;
        skip_space ();
      }
    return fail ("truncated JSON object");
  }

  /* Parse JSON array into VALUE.  */
  bool
  parse_array (json_value *value, int depth)
  {
    value->type = json_value::kind::array;
    ++m_cur;
    skip_space ();
    if (m_cur != m_end && *m_cur == ']')
      {
        ++m_cur;
        return true;
      }

    while (m_cur != m_end)
      {
        json_value child;
        if (!parse_value (&child, depth))
          return false;
        value->array_value.push_back (std::move (child));
        skip_space ();
        if (m_cur == m_end)
          return fail ("truncated JSON array");
        if (*m_cur == ']')
          {
            ++m_cur;
            return true;
          }
        if (*m_cur != ',')
          return fail ("JSON array members are not comma-separated");
        ++m_cur;
        skip_space ();
      }
    return fail ("truncated JSON array");
  }

  /* Parse four hexadecimal digits and advance the input.  */
  bool
  parse_hex4 (uint32_t *value)
  {
    if (!value || m_end - m_cur < 4)
      return fail ("truncated JSON Unicode escape");
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i)
      {
        int digit = hex_value (*m_cur++);
        if (digit < 0)
          return fail ("invalid JSON Unicode escape");
        result = (result << 4) | (uint32_t)digit;
      }
    *value = result;
    return true;
  }

  /* Parse one JSON string including escape and surrogate handling.  */
  bool
  parse_string (std::string *output)
  {
    if (!output || m_cur == m_end || *m_cur != '"')
      return fail ("expected JSON string");
    ++m_cur;
    output->clear ();

    while (m_cur != m_end)
      {
        unsigned char c = (unsigned char)*m_cur++;
        if (c == '"')
          return true;
        if (c < 0x20)
          return fail ("control character in JSON string");
        if (c != '\\')
          {
            output->push_back ((char)c);
            continue;
          }

        if (m_cur == m_end)
          return fail ("truncated JSON string escape");
        char escaped = *m_cur++;
        switch (escaped)
          {
          case '"':
          case '\\':
          case '/':
            output->push_back (escaped);
            break;
          case 'b':
            output->push_back ('\b');
            break;
          case 'f':
            output->push_back ('\f');
            break;
          case 'n':
            output->push_back ('\n');
            break;
          case 'r':
            output->push_back ('\r');
            break;
          case 't':
            output->push_back ('\t');
            break;
          case 'u':
            {
              uint32_t first = 0;
              if (!parse_hex4 (&first))
                return false;
              uint32_t codepoint = first;
              if (first >= 0xd800 && first <= 0xdbff)
                {
                  if (m_end - m_cur < 6 || m_cur[0] != '\\'
                      || m_cur[1] != 'u')
                    return fail ("JSON high surrogate has no low surrogate");
                  m_cur += 2;
                  uint32_t second = 0;
                  if (!parse_hex4 (&second) || second < 0xdc00
                      || second > 0xdfff)
                    return fail ("invalid JSON low surrogate");
                  codepoint
                      = 0x10000 + ((first - 0xd800) << 10)
                        + (second - 0xdc00);
                }
              else if (first >= 0xdc00 && first <= 0xdfff)
                return fail ("unexpected JSON low surrogate");
              if (!append_utf8 (output, codepoint))
                return fail ("invalid JSON Unicode code point");
              break;
            }
          default:
            return fail ("unknown JSON string escape");
          }
      }
    return fail ("truncated JSON string");
  }

  /* Parse strict JSON number text into OUTPUT without floating conversion.  */
  bool
  parse_number (std::string *output)
  {
    const char *start = m_cur;
    if (m_cur != m_end && *m_cur == '-')
      ++m_cur;
    if (m_cur == m_end)
      return fail ("truncated JSON number");

    if (*m_cur == '0')
      {
        ++m_cur;
        if (m_cur != m_end && *m_cur >= '0' && *m_cur <= '9')
          return fail ("JSON number has a leading zero");
      }
    else
      {
        if (*m_cur < '1' || *m_cur > '9')
          return fail ("invalid JSON number");
        do
          ++m_cur;
        while (m_cur != m_end && *m_cur >= '0' && *m_cur <= '9');
      }

    if (m_cur != m_end && *m_cur == '.')
      {
        ++m_cur;
        const char *fraction = m_cur;
        while (m_cur != m_end && *m_cur >= '0' && *m_cur <= '9')
          ++m_cur;
        if (m_cur == fraction)
          return fail ("JSON fraction has no digits");
      }

    if (m_cur != m_end && (*m_cur == 'e' || *m_cur == 'E'))
      {
        ++m_cur;
        if (m_cur != m_end && (*m_cur == '+' || *m_cur == '-'))
          ++m_cur;
        const char *exponent = m_cur;
        while (m_cur != m_end && *m_cur >= '0' && *m_cur <= '9')
          ++m_cur;
        if (m_cur == exponent)
          return fail ("JSON exponent has no digits");
      }

    output->assign (start, m_cur);
    return true;
  }
};

/* Return true when DATA is structurally valid UTF-8.  */
bool
valid_utf8 (const std::string &data)
{
  size_t i = 0;
  while (i < data.size ())
    {
      unsigned char c = (unsigned char)data[i++];
      if (c <= 0x7f)
        continue;
      int continuation = 0;
      uint32_t codepoint = 0;
      if ((c & 0xe0) == 0xc0)
        {
          continuation = 1;
          codepoint = c & 0x1f;
          if (codepoint < 2)
            return false;
        }
      else if ((c & 0xf0) == 0xe0)
        {
          continuation = 2;
          codepoint = c & 0x0f;
        }
      else if ((c & 0xf8) == 0xf0)
        {
          continuation = 3;
          codepoint = c & 0x07;
        }
      else
        return false;

      if (i + (size_t)continuation > data.size ())
        return false;
      for (int j = 0; j < continuation; ++j)
        {
          unsigned char d = (unsigned char)data[i++];
          if ((d & 0xc0) != 0x80)
            return false;
          codepoint = (codepoint << 6) | (d & 0x3f);
        }

      if ((continuation == 2 && codepoint < 0x800)
          || (continuation == 3 && codepoint < 0x10000)
          || codepoint > 0x10ffff
          || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return false;
    }
  return true;
}

/* Store MESSAGE in ERROR and return false.  */
bool
archive_fail (std::string *error, const std::string &message)
{
  if (error)
    *error = message;
  return false;
}

/* Return object member KEY or null when OBJECT has no such member.  */
const json_value *
object_member (const json_value &object, const char *key)
{
  if (object.type != json_value::kind::object)
    return nullptr;
  auto it = object.object_value.find (key);
  return it == object.object_value.end () ? nullptr : &it->second;
}

/* Parse a non-negative integer JSON NUMBER into VALUE.  */
bool
json_uint64 (const json_value &number, uint64_t *value)
{
  if (!value || number.type != json_value::kind::number
      || number.text.empty ())
    return false;
  uint64_t result = 0;
  for (char c : number.text)
    {
      if (c < '0' || c > '9')
        return false;
      unsigned digit = (unsigned)(c - '0');
      if (result > (std::numeric_limits<uint64_t>::max () - digit) / 10)
        return false;
      result = result * 10 + digit;
    }
  *value = result;
  return true;
}

/* Convert strict JSON NUMBER to one finite double. */
bool
json_finite_double (const json_value &number, double *value)
{
  if (!value || number.type != json_value::kind::number
      || number.text.empty ())
    return false;
  char *end = nullptr;
  errno = 0;
  const double parsed = std::strtod (number.text.c_str (), &end);
  if (errno == ERANGE || !end || *end || !std::isfinite (parsed))
    return false;
  *value = parsed;
  return true;
}

/* Parse authoritative render fields not represented by the legacy CSP mirror. */
bool
parse_render_overrides (const json_value &object,
                        parameter_archive_render_overrides *result,
                        std::string *error)
{
  if (!result || object.type != json_value::kind::object)
    return archive_fail (error, "state.render_overrides must be an object");

  const json_value *ignore = object_member (object, "ignore_infrared");
  const json_value *scaling = object_member (object, "demosaiced_scaling");
  const json_value *whitepoint = object_member (object, "observer_whitepoint");
  const json_value *profile = object_member (object, "output_profile");
  const json_value *gamma = object_member (object, "output_gamma");
  const json_value *gamut = object_member (object, "gamut_warning");

  if (!ignore || ignore->type != json_value::kind::boolean
      || !scaling || scaling->type != json_value::kind::string
      || !whitepoint || whitepoint->type != json_value::kind::array
      || whitepoint->array_value.size () != 2
      || !profile || profile->type != json_value::kind::string
      || !gamma || gamma->type != json_value::kind::number
      || !gamut || gamut->type != json_value::kind::boolean)
    return archive_fail (
        error, "state.render_overrides is missing a required typed field");

  parameter_archive_render_overrides parsed;
  parsed.present = true;
  parsed.ignore_infrared = ignore->boolean_value;
  parsed.gamut_warning = gamut->boolean_value;

  bool scaling_found = false;
  for (int i = 0; i < (int)render_parameters::max_demosaiced_scaling; ++i)
    if (scaling->text
        == render_parameters::demosaiced_scaling_names[i].name)
      {
        parsed.demosaiced_scaling
            = (render_parameters::demosaiced_scaling_t)i;
        scaling_found = true;
        break;
      }
  if (!scaling_found)
    return archive_fail (error,
                         "unknown state.render_overrides demosaiced_scaling");

  bool profile_found = false;
  for (int i = 0; i < (int)render_parameters::output_profile_max; ++i)
    if (profile->text == render_parameters::output_profile_names[i])
      {
        parsed.output_profile = (render_parameters::output_profile_t)i;
        profile_found = true;
        break;
      }
  if (!profile_found)
    return archive_fail (error,
                         "unknown state.render_overrides output_profile");

  double white_x = 0;
  double white_y = 0;
  double output_gamma = 0;
  if (!json_finite_double (whitepoint->array_value[0], &white_x)
      || !json_finite_double (whitepoint->array_value[1], &white_y)
      || white_x < 0 || white_y <= 0 || white_x + white_y > 1
      || !json_finite_double (*gamma, &output_gamma)
      || (output_gamma != -1 && output_gamma <= 0))
    return archive_fail (error,
                         "invalid numeric state.render_overrides value");

  parsed.observer_whitepoint = xy_t (white_x, white_y);
  parsed.output_gamma = output_gamma;
  *result = parsed;
  return true;
}

/* Format VALUE as locale-independent finite JSON number text. */
std::string
json_number (double value)
{
  std::ostringstream stream;
  stream.imbue (std::locale::classic ());
  stream << std::setprecision (17) << value;
  return stream.str ();
}

/* Return true when PATH is a safe relative ZIP entry path.  */
bool
safe_archive_path (const std::string &path)
{
  if (path.empty () || path[0] == '/' || path[0] == '\\'
      || path.find ('\\') != std::string::npos
      || path.find (':') != std::string::npos || !valid_utf8 (path))
    return false;

  size_t start = 0;
  while (start < path.size ())
    {
      size_t slash = path.find ('/', start);
      size_t end = slash == std::string::npos ? path.size () : slash;
      if (end == start)
        return false;
      std::string component = path.substr (start, end - start);
      if (component == "." || component == "..")
        return false;
      if (slash == std::string::npos)
        return true;
      start = slash + 1;
    }
  return false;
}

/* Convert libzip OPEN_ERROR into a diagnostic.  */
std::string
zip_open_error (int open_error)
{
  char buffer[256];
  zip_error_to_str (buffer, sizeof (buffer), open_error, errno);
  return buffer;
}

/* Check compression/encryption methods advertised by STAT when available.  */
bool
validate_zip_methods (const zip_stat_t &stat, std::string *error)
{
#ifdef ZIP_STAT_ENCRYPTION_METHOD
  if ((stat.valid & ZIP_STAT_ENCRYPTION_METHOD)
      && stat.encryption_method != ZIP_EM_NONE)
    return archive_fail (error, "encrypted parameter archive entry is not supported");
#endif
#ifdef ZIP_STAT_COMP_METHOD
  if ((stat.valid & ZIP_STAT_COMP_METHOD)
      && stat.comp_method != ZIP_CM_STORE && stat.comp_method != ZIP_CM_DEFLATE)
    return archive_fail (error,
                         "unsupported parameter archive compression method");
#endif
  return true;
}

/* Read archive entry INDEX with an uncompressed SIZE_LIMIT.  */
bool
read_zip_entry (zip_t *archive, zip_uint64_t index, uint64_t size_limit,
                std::string *output, std::string *error)
{
  if (!archive || !output)
    return archive_fail (error, "internal archive read destination is null");

  zip_stat_t stat;
  zip_stat_init (&stat);
  if (zip_stat_index (archive, index, 0, &stat) != 0)
    return archive_fail (error, zip_strerror (archive));
  if (!validate_zip_methods (stat, error))
    return false;
  if (!(stat.valid & ZIP_STAT_SIZE) || stat.size > size_limit
      || stat.size > (zip_uint64_t)std::numeric_limits<size_t>::max ())
    return archive_fail (error, "parameter archive entry is too large");

  zip_file_t *file = zip_fopen_index (archive, index, 0);
  if (!file)
    return archive_fail (error, zip_strerror (archive));

  output->assign ((size_t)stat.size, '\0');
  size_t offset = 0;
  while (offset < output->size ())
    {
      zip_int64_t n = zip_fread (file, &(*output)[offset],
                                 output->size () - offset);
      if (n < 0)
        {
          std::string message = zip_file_strerror (file);
          zip_fclose (file);
          return archive_fail (error, message);
        }
      if (n == 0)
        break;
      offset += (size_t)n;
    }

  if (zip_fclose (file) != 0)
    return archive_fail (error, "could not close parameter archive entry");
  if (offset != output->size ())
    return archive_fail (error, "truncated parameter archive entry");
  return true;
}

/* Return byte size for a known typed payload element, or zero if unknown.  */
size_t
payload_element_size (const std::string &type)
{
  if (type == "uint8")
    return 1;
  if (type == "uint16-le")
    return 2;
  if (type == "uint32-le" || type == "int32-le" || type == "float32-le")
    return 4;
  if (type == "float64-le")
    return 8;
  return 0;
}

/* Validate optional PAYLOADS descriptors against archive ENTRIES.  */
bool
validate_payloads (
    const json_value *payloads,
    const std::map<std::string, std::pair<zip_uint64_t, uint64_t>> &entries,
    std::string *error)
{
  if (!payloads)
    return true;
  if (payloads->type != json_value::kind::array)
    return archive_fail (error, "manifest payloads must be an array");

  std::set<std::string> names;
  std::set<std::string> paths;
  for (const json_value &payload : payloads->array_value)
    {
      if (payload.type != json_value::kind::object)
        return archive_fail (error, "manifest payload descriptor is not an object");
      const json_value *name = object_member (payload, "name");
      const json_value *path = object_member (payload, "path");
      const json_value *element = object_member (payload, "element_type");
      const json_value *shape = object_member (payload, "shape");
      const json_value *required = object_member (payload, "required");
      if (!name || name->type != json_value::kind::string || name->text.empty ()
          || !path || path->type != json_value::kind::string
          || !safe_archive_path (path->text) || !element
          || element->type != json_value::kind::string || !shape
          || shape->type != json_value::kind::array)
        return archive_fail (error, "invalid manifest payload descriptor");
      if (!names.insert (name->text).second || !paths.insert (path->text).second)
        return archive_fail (error, "duplicate manifest payload name or path");

      bool is_required = false;
      if (required)
        {
          if (required->type != json_value::kind::boolean)
            return archive_fail (error,
                                 "manifest payload required flag is not boolean");
          is_required = required->boolean_value;
        }

      uint64_t elements = 1;
      for (const json_value &dimension : shape->array_value)
        {
          uint64_t value = 0;
          if (!json_uint64 (dimension, &value))
            return archive_fail (error, "invalid manifest payload dimension");
          if (value != 0
              && elements > std::numeric_limits<uint64_t>::max () / value)
            return archive_fail (error, "manifest payload shape overflows");
          elements *= value;
        }

      size_t element_size = payload_element_size (element->text);
      auto entry = entries.find (path->text);
      if (is_required && (element_size == 0 || entry == entries.end ()))
        return archive_fail (error, "unsupported required parameter payload");
      if (entry != entries.end () && element_size != 0)
        {
          if (elements > std::numeric_limits<uint64_t>::max () / element_size)
            return archive_fail (error, "manifest payload byte size overflows");
          if (entry->second.second != elements * element_size)
            return archive_fail (error,
                                 "manifest payload byte size does not match shape");
        }
    }
  return true;
}

/* Parse and validate schema-v1 MANIFEST_TEXT.  */
bool
parse_manifest (
    const std::string &manifest_text,
    const std::map<std::string, std::pair<zip_uint64_t, uint64_t>> &entries,
    parameter_archive_manifest *manifest, std::string *error)
{
  if (!valid_utf8 (manifest_text))
    return archive_fail (error, "parameter archive manifest is not valid UTF-8");

  json_parser parser (manifest_text.data (),
                      manifest_text.data () + manifest_text.size ());
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error,
                         "invalid parameter archive manifest: " + parser.error ());
  if (root.type != json_value::kind::object)
    return archive_fail (error, "parameter archive manifest root is not an object");

  const json_value *format = object_member (root, "format");
  if (!format || format->type != json_value::kind::string
      || format->text != "org.colorscreen.parameters")
    return archive_fail (error, "not a Color-Screen parameter archive manifest");

  uint64_t version = 0;
  const json_value *schema = object_member (root, "schema_version");
  if (!schema || !json_uint64 (*schema, &version) || version == 0
      || version > (uint64_t)std::numeric_limits<int>::max ())
    return archive_fail (error, "invalid parameter archive schema version");
  if (version != 1)
    return archive_fail (error, "unsupported parameter archive schema version");

  bool render_overrides_feature = false;
  std::set<std::string> required_feature_names;
  const json_value *features = object_member (root, "required_features");
  if (features)
    {
      if (features->type != json_value::kind::array)
        return archive_fail (error, "manifest required_features must be an array");
      for (const json_value &feature : features->array_value)
        {
          if (feature.type != json_value::kind::string || feature.text.empty ())
            return archive_fail (error, "invalid required parameter feature");
          if (!required_feature_names.insert (feature.text).second)
            return archive_fail (error,
                                 "duplicate required parameter archive feature");
          if (feature.text == "render-overrides-v1")
            render_overrides_feature = true;
          else
            return archive_fail (
                error, "unsupported required parameter archive feature: "
                           + feature.text);
        }
    }

  const json_value *state = object_member (root, "state");
  const json_value *legacy = state ? object_member (*state, "legacy_csp") : nullptr;
  if (!state || state->type != json_value::kind::object || !legacy
      || legacy->type != json_value::kind::string
      || !safe_archive_path (legacy->text))
    return archive_fail (error, "manifest has no valid state.legacy_csp path");
  if (entries.find (legacy->text) == entries.end ())
    return archive_fail (error, "parameter archive is missing its legacy CSP state");

  const json_value *render_overrides
      = object_member (*state, "render_overrides");
  if (render_overrides && !render_overrides_feature)
    return archive_fail (
        error, "state.render_overrides requires render-overrides-v1");
  if (render_overrides_feature && !render_overrides)
    return archive_fail (
        error, "render-overrides-v1 requires state.render_overrides");

  parameter_archive_render_overrides parsed_render_overrides;
  if (render_overrides_feature
      && !parse_render_overrides (*render_overrides, &parsed_render_overrides,
                                  error))
    return false;

  if (!validate_payloads (object_member (root, "payloads"), entries, error))
    return false;

  if (manifest)
    {
      manifest->schema_version = (int)version;
      manifest->legacy_csp_path = legacy->text;
      manifest->render_overrides = parsed_render_overrides;
    }
  return true;
}

/* Escape UTF-8 TEXT for inclusion as one JSON string.  */
std::string
json_escape (const std::string &text)
{
  static const char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve (text.size () + 8);
  for (unsigned char c : text)
    {
      switch (c)
        {
        case '"':
          result += "\\\"";
          break;
        case '\\':
          result += "\\\\";
          break;
        case '\b':
          result += "\\b";
          break;
        case '\f':
          result += "\\f";
          break;
        case '\n':
          result += "\\n";
          break;
        case '\r':
          result += "\\r";
          break;
        case '\t':
          result += "\\t";
          break;
        default:
          if (c < 0x20)
            {
              result += "\\u00";
              result.push_back (hex[(c >> 4) & 0xf]);
              result.push_back (hex[c & 0xf]);
            }
          else
            result.push_back ((char)c);
        }
    }
  return result;
}

/* Add CONTENT as archive entry NAME and select compression METHOD.  */
bool
add_archive_entry (zip_t *archive, const char *name, const std::string &content,
                   zip_int32_t method, std::string *error)
{
  zip_source_t *source
      = zip_source_buffer (archive, content.data (), content.size (), 0);
  if (!source)
    return archive_fail (error, zip_strerror (archive));

  zip_int64_t index = zip_file_add (
      archive, name, source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
  if (index < 0)
    {
      zip_source_free (source);
      return archive_fail (error, zip_strerror (archive));
    }
  if (zip_set_file_compression (archive, (zip_uint64_t)index, method,
                                method == ZIP_CM_DEFLATE ? 9 : 0)
      != 0)
    return archive_fail (error, zip_strerror (archive));
  return true;
}

}

/* Return authoritative structured archive values missing from legacy CSP. */
parameter_archive_render_overrides
parameter_archive_render_overrides_from (const render_parameters &rparam)
{
  parameter_archive_render_overrides result;
  result.present = true;
  result.ignore_infrared = rparam.ignore_infrared;
  result.demosaiced_scaling = rparam.demosaiced_scaling;
  result.observer_whitepoint = rparam.observer_whitepoint;
  /* Schema v1 requires these compatibility keys, but displayed output
     colourspace and gamut diagnostics now belong to individual views or
     explicit file-render requests. Do not serialize a transient display
     choice as if it were a reconstruction parameter. */
  result.output_profile = render_parameters::output_profile_sRGB;
  result.output_gamma = rparam.output_gamma;
  result.gamut_warning = false;
  return result;
}

/* Apply structured archive values after the legacy CSP mirror was parsed. */
void
apply_parameter_archive_render_overrides (
    const parameter_archive_render_overrides &overrides,
    render_parameters *rparam)
{
  if (!rparam || !overrides.present)
    return;
  rparam->ignore_infrared = overrides.ignore_infrared;
  rparam->demosaiced_scaling = overrides.demosaiced_scaling;
  rparam->observer_whitepoint = overrides.observer_whitepoint;
  /* Old v1 archives may contain nondefault view settings. Validate their
     typed representation during parsing, but do not restore them into
     document reconstruction state: current view/export configuration owns
     output colourspace and gamut warnings independently. */
  rparam->output_gamma = overrides.output_gamma;
}

/* Return true when NAME begins with one of the standard ZIP signatures.  */
bool
parameter_archive_signature_p (const char *name)
{
  if (!name)
    return false;
  FILE *file = open_utf8_binary_read (name);
  if (!file)
    return false;
  unsigned char signature[4];
  size_t count = fread (signature, 1, sizeof (signature), file);
  fclose (file);
  if (count != sizeof (signature) || signature[0] != 'P'
      || signature[1] != 'K')
    return false;
  return (signature[2] == 3 && signature[3] == 4)
         || (signature[2] == 5 && signature[3] == 6)
         || (signature[2] == 7 && signature[3] == 8);
}

/* Open plain CSP or one validated archive payload as a FILE*.  */
FILE *
open_parameter_payload (const char *name, bool *is_archive, std::string *error,
                        parameter_archive_manifest *manifest)
{
  if (error)
    error->clear ();
  if (is_archive)
    *is_archive = false;
  if (manifest)
    *manifest = {};
  if (!name)
    {
      archive_fail (error, "invalid parameter filename");
      return nullptr;
    }

  if (!parameter_archive_signature_p (name))
    {
      FILE *file = open_utf8_binary_read (name);
      if (!file)
        archive_fail (error, std::string ("could not open parameter file: ")
                                 + std::strerror (errno));
      return file;
    }

  std::string payload;
  if (!read_parameter_archive (name, &payload, manifest, error))
    return nullptr;

  FILE *file = std::tmpfile ();
  if (!file)
    {
      archive_fail (error,
                    std::string ("could not create temporary parameter stream: ")
                        + std::strerror (errno));
      return nullptr;
    }
  const bool written
      = fwrite (payload.data (), 1, payload.size (), file) == payload.size ();
  if (!written || fflush (file) != 0 || fseek (file, 0, SEEK_SET) != 0)
    {
      fclose (file);
      archive_fail (error, "could not stage archived parameter payload");
      return nullptr;
    }

  if (is_archive)
    *is_archive = true;
  return file;
}

/* Read and validate schema-v1 parameter archive NAME.  */
bool
read_parameter_archive (const char *name, std::string *legacy_csp,
                        parameter_archive_manifest *manifest,
                        std::string *error)
{
  if (error)
    error->clear ();
  if (!name || !legacy_csp)
    return archive_fail (error, "invalid parameter archive read arguments");

  int open_error = 0;
  zip_t *archive = zip_open (name, ZIP_RDONLY, &open_error);
  if (!archive)
    return archive_fail (error,
                         "could not open parameter archive: "
                             + zip_open_error (open_error));

  bool ok = false;
  do
    {
      zip_int64_t count = zip_get_num_entries (archive, 0);
      if (count < 0 || count > archive_max_entries)
        {
          archive_fail (error, "parameter archive contains too many entries");
          break;
        }

      std::map<std::string, std::pair<zip_uint64_t, uint64_t>> entries;
      bool entries_ok = true;
      for (zip_uint64_t i = 0; i < (zip_uint64_t)count; ++i)
        {
          const char *raw_name = zip_get_name (archive, i, ZIP_FL_ENC_GUESS);
          if (!raw_name)
            {
              archive_fail (error, zip_strerror (archive));
              entries_ok = false;
              break;
            }
          std::string entry_name = raw_name;
          if (!safe_archive_path (entry_name))
            {
              archive_fail (error, "unsafe path in parameter archive: "
                                       + entry_name);
              entries_ok = false;
              break;
            }

          zip_stat_t stat;
          zip_stat_init (&stat);
          if (zip_stat_index (archive, i, 0, &stat) != 0)
            {
              archive_fail (error, zip_strerror (archive));
              entries_ok = false;
              break;
            }
          if (!validate_zip_methods (stat, error)
              || !(stat.valid & ZIP_STAT_SIZE))
            {
              if (stat.valid && !(stat.valid & ZIP_STAT_SIZE))
                archive_fail (error,
                              "parameter archive entry has no declared size");
              entries_ok = false;
              break;
            }
          if (!entries.emplace (entry_name,
                                std::make_pair (i, (uint64_t)stat.size))
                   .second)
            {
              archive_fail (error,
                            "duplicate path in parameter archive: " + entry_name);
              entries_ok = false;
              break;
            }
        }
      if (!entries_ok)
        break;

      auto manifest_entry = entries.find ("manifest.json");
      if (manifest_entry == entries.end ())
        {
          archive_fail (error, "parameter archive has no manifest.json");
          break;
        }
      std::string manifest_text;
      if (!read_zip_entry (archive, manifest_entry->second.first,
                           manifest_max_size, &manifest_text, error))
        break;

      parameter_archive_manifest parsed;
      if (!parse_manifest (manifest_text, entries, &parsed, error))
        break;

      auto legacy_entry = entries.find (parsed.legacy_csp_path);
      if (legacy_entry == entries.end ()
          || !read_zip_entry (archive, legacy_entry->second.first,
                              legacy_csp_max_size, legacy_csp, error))
        break;

      if (manifest)
        *manifest = parsed;
      ok = true;
    }
  while (false);

  /* Read-only inspection never needs central-directory updates.  Discarding
     avoids turning an otherwise successful read into an irrelevant close-time
     write/finalization path.  */
  zip_discard (archive);
  return ok;
}

/* Write one schema-v1 parameter archive to staging path NAME.  */
bool
write_parameter_archive (
    const char *name, const std::string &legacy_csp,
    const char *generator_version, std::string *error,
    const parameter_archive_render_overrides *render_overrides)
{
  if (error)
    error->clear ();
  if (!name || legacy_csp.empty ())
    return archive_fail (error, "invalid parameter archive write arguments");

  std::string version = generator_version ? generator_version : "";
  if (!valid_utf8 (version))
    return archive_fail (error, "parameter archive generator version is not UTF-8");

  const bool structured_render
      = render_overrides && render_overrides->present;
  if (structured_render)
    {
      const auto scaling = render_overrides->demosaiced_scaling;
      const auto profile = render_overrides->output_profile;
      const double white_x = render_overrides->observer_whitepoint.x;
      const double white_y = render_overrides->observer_whitepoint.y;
      const double output_gamma = render_overrides->output_gamma;
      if ((int)scaling < 0
          || scaling >= render_parameters::max_demosaiced_scaling
          || (int)profile < 0 || profile >= render_parameters::output_profile_max
          || !std::isfinite (white_x) || !std::isfinite (white_y)
          || white_x < 0 || white_y <= 0 || white_x + white_y > 1
          || !std::isfinite (output_gamma)
          || (output_gamma != -1 && output_gamma <= 0))
        return archive_fail (error,
                             "invalid structured render override values");
    }

  std::string manifest
      = "{\n"
        "  \"format\": \"org.colorscreen.parameters\",\n"
        "  \"schema_version\": 1,\n";
  if (structured_render)
    manifest += "  \"required_features\": [\"render-overrides-v1\"],\n";
  manifest
      += "  \"generator\": {\n"
         "    \"application\": \"Color-Screen\",\n"
         "    \"version\": \""
         + json_escape (version)
         + "\"\n"
           "  },\n"
           "  \"state\": {\n"
           "    \"legacy_csp\": \"state/legacy.par\"";
  if (structured_render)
    {
      manifest
          += ",\n"
             "    \"render_overrides\": {\n"
             "      \"ignore_infrared\": "
             + std::string (render_overrides->ignore_infrared ? "true" : "false")
             + ",\n"
               "      \"demosaiced_scaling\": \""
             + json_escape (
                 render_parameters::demosaiced_scaling_names
                     [(int)render_overrides->demosaiced_scaling]
                         .name)
             + "\",\n"
               "      \"observer_whitepoint\": ["
             + json_number (render_overrides->observer_whitepoint.x) + ", "
             + json_number (render_overrides->observer_whitepoint.y)
             + "],\n"
               "      \"output_profile\": \""
             + json_escape (render_parameters::output_profile_names
                                [(int)render_overrides->output_profile])
             + "\",\n"
               "      \"output_gamma\": "
             + json_number (render_overrides->output_gamma)
             + ",\n"
               "      \"gamut_warning\": "
             + std::string (render_overrides->gamut_warning ? "true" : "false")
             + "\n"
               "    }";
    }
  manifest += "\n  },\n"
              "  \"payloads\": []\n"
              "}\n";

  int open_error = 0;
  zip_t *archive = zip_open (name, ZIP_CREATE | ZIP_TRUNCATE, &open_error);
  if (!archive)
    return archive_fail (error,
                         "could not create parameter archive: "
                             + zip_open_error (open_error));

  bool ok
      = add_archive_entry (archive, "manifest.json", manifest, ZIP_CM_STORE,
                           error)
        && add_archive_entry (archive, "state/legacy.par", legacy_csp,
                              ZIP_CM_DEFLATE, error);
  if (!ok)
    {
      zip_discard (archive);
      remove_utf8_path (name);
      return false;
    }

  if (zip_close (archive) != 0)
    {
      /* libzip retains ownership after a failed zip_close().  Release that
         handle before removing the caller's staging file.  */
      std::string message = zip_strerror (archive);
      zip_discard (archive);
      remove_utf8_path (name);
      return archive_fail (
          error, "could not finalize parameter archive: " + message);
    }
  return true;
}

/* Atomically replace one legacy or archive parameter payload. */
bool
write_parameter_payload_file (
    const char *name, const std::string &payload, bool archive,
    const char *generator_version, std::string *error,
    const parameter_archive_render_overrides *render_overrides)
{
  if (error)
    error->clear ();
  if (!name || payload.empty ())
    return archive_fail (error, "invalid parameter payload write arguments");

  std::filesystem::path staging_path;
  if (!create_parameter_staging_path (name, &staging_path, error))
    return false;

  bool written = false;
  std::string staging_utf8;
  try
    {
      staging_utf8 = staging_path.u8string ();
    }
  catch (...)
    {
      std::error_code ignored;
      std::filesystem::remove (staging_path, ignored);
      return archive_fail (error, "could not encode parameter staging path");
    }

  if (archive)
    written = write_parameter_archive (staging_utf8.c_str (), payload,
                                       generator_version, error,
                                       render_overrides);
  else
    {
      FILE *out = open_utf8_binary_write (staging_utf8.c_str ());
      if (!out)
        archive_fail (
            error, std::string ("could not open parameter staging output: ")
                       + std::strerror (errno));
      else
        {
          bool ok
              = fwrite (payload.data (), 1, payload.size (), out)
                == payload.size ();
          if (fflush (out) != 0)
            ok = false;
          if (fclose (out) != 0)
            ok = false;
          if (!ok)
            archive_fail (error, "could not write complete parameter payload");
          written = ok;
        }
    }

  if (!written)
    {
      remove_utf8_path (staging_utf8.c_str ());
      return false;
    }

  try
    {
      const std::filesystem::path target = std::filesystem::u8path (name);
      if (!replace_parameter_staging_path (staging_path, target, error))
        {
          remove_utf8_path (staging_utf8.c_str ());
          return false;
        }
    }
  catch (const std::exception &exception)
    {
      remove_utf8_path (staging_utf8.c_str ());
      return archive_fail (error,
                           std::string ("invalid parameter target path: ")
                               + exception.what ());
    }
  catch (...)
    {
      remove_utf8_path (staging_utf8.c_str ());
      return archive_fail (error, "invalid parameter target path");
    }

  if (error)
    error->clear ();
  return true;
}

}
