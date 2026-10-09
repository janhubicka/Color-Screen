/* Versioned Color-Screen parameter archive support.
   Copyright (C) 2026 Jan Hubicka
   This file is part of ColorScreen.  */

#include "parameter-archive.h"
#include "include/mesh.h"

#include <zip.h>

#include <atomic>
#include <charconv>
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
  json_parser (const char *begin, const char *end,
               size_t max_nodes = json_max_nodes)
      : m_cur (begin), m_end (end), m_max_nodes (max_nodes)
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
  size_t m_max_nodes;
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
    if (++m_nodes > m_max_nodes)
      return fail ("JSON document contains too many values");
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
  for (int i = 0; i < (int)render_output_parameters::output_profile_max; ++i)
    if (profile->text == render_output_parameters::output_profile_names[i])
      {
        parsed.output_profile = (render_output_parameters::output_profile_t)i;
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

/* Parse a final geometry frame that legacy CSP does not represent. */
bool
parse_geometry_final_frame (const json_value &object,
                            parameter_archive_geometry_final_frame *result,
                            std::string *error)
{
  if (!result || object.type != json_value::kind::object)
    return archive_fail (error, "state.geometry_final_frame must be an object");

  const json_value *angle = object_member (object, "final_angle");
  const json_value *ratio = object_member (object, "final_ratio");
  if (!angle || angle->type != json_value::kind::number
      || !ratio || ratio->type != json_value::kind::number)
    return archive_fail (
        error, "state.geometry_final_frame is missing a required typed field");

  double angle_value = 0, ratio_value = 0;
  if (!json_finite_double (*angle, &angle_value)
      || !json_finite_double (*ratio, &ratio_value))
    return archive_fail (error,
                         "invalid numeric state.geometry_final_frame value");
  const coord_t final_angle = (coord_t)angle_value;
  const coord_t final_ratio = (coord_t)ratio_value;
  if (!my_isfinite (final_angle) || !my_isfinite (final_ratio)
      || final_ratio <= 0)
    return archive_fail (error,
                         "invalid numeric state.geometry_final_frame value");

  result->present = true;
  result->final_angle = final_angle;
  result->final_ratio = final_ratio;
  return true;
}

/* Parse authoritative photographic bounds, distinct from the physical crop. */
bool
parse_image_area (const json_value &object,
                  parameter_archive_image_area *result, std::string *error)
{
  if (!result || object.type != json_value::kind::object)
    return archive_fail (error, "state.image_area must be an object");
  const json_value *enabled = object_member (object, "enabled");
  const json_value *rect = object_member (object, "rect");
  if (!enabled || enabled->type != json_value::kind::boolean
      || !rect || rect->type != json_value::kind::array
      || rect->array_value.size () != 4)
    return archive_fail (error, "state.image_area requires typed enabled and rect");

  uint64_t values[4] = {};
  const uint64_t limit = (uint64_t)std::numeric_limits<int>::max ();
  for (int i = 0; i < 4; ++i)
    if (!json_uint64 (rect->array_value[i], &values[i])
        || values[i] > limit)
      return archive_fail (error, "invalid integer state.image_area rectangle");

  if (enabled->boolean_value
      ? (values[2] == 0 || values[3] == 0
         || values[0] > limit - values[2]
         || values[1] > limit - values[3])
      : (values[0] != 0 || values[1] != 0
         || values[2] != 0 || values[3] != 0))
    return archive_fail (error, "invalid state.image_area bounds");

  parameter_archive_image_area parsed;
  parsed.present = true;
  if (enabled->boolean_value)
    parsed.area = int_optional_image_area (int_image_area (
        (int)values[0], (int)values[1], (int)values[2], (int)values[3]));
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
  bool geometry_final_frame_feature = false;
  bool image_area_feature = false;
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
          else if (feature.text == "geometry-final-frame-v1")
            geometry_final_frame_feature = true;
          else if (feature.text == "image-area-v1")
            image_area_feature = true;
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

  const json_value *geometry_final_frame
      = object_member (*state, "geometry_final_frame");
  if (geometry_final_frame && !geometry_final_frame_feature)
    return archive_fail (
        error, "state.geometry_final_frame requires geometry-final-frame-v1");
  if (geometry_final_frame_feature && !geometry_final_frame)
    return archive_fail (
        error, "geometry-final-frame-v1 requires state.geometry_final_frame");

  parameter_archive_geometry_final_frame parsed_geometry_final_frame;
  if (geometry_final_frame_feature
      && !parse_geometry_final_frame (*geometry_final_frame,
                                      &parsed_geometry_final_frame, error))
    return false;

  const json_value *image_area = object_member (*state, "image_area");
  if (image_area && !image_area_feature)
    return archive_fail (error, "state.image_area requires image-area-v1");
  if (image_area_feature && !image_area)
    return archive_fail (error, "image-area-v1 requires state.image_area");

  parameter_archive_image_area parsed_image_area;
  if (image_area_feature
      && !parse_image_area (*image_area, &parsed_image_area, error))
    return false;

  if (!validate_payloads (object_member (root, "payloads"), entries, error))
    return false;

  if (manifest)
    {
      manifest->schema_version = (int)version;
      manifest->legacy_csp_path = legacy->text;
      manifest->render_overrides = parsed_render_overrides;
      manifest->geometry_final_frame = parsed_geometry_final_frame;
      manifest->image_area = parsed_image_area;
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
  result.output_profile = render_output_parameters::output_profile_sRGB;
  result.output_gamma = -1;
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

}

/* Extract the final frame from PARAM without changing other geometry. */
parameter_archive_geometry_final_frame
parameter_archive_geometry_final_frame_from (const scr_to_img_parameters &param)
{
  parameter_archive_geometry_final_frame frame;
  frame.present = true;
  frame.final_angle = param.final_angle;
  frame.final_ratio = param.final_ratio;
  return frame;
}

/* Apply only the final-frame fields represented by FRAME. */
void
apply_parameter_archive_geometry_final_frame (
    const parameter_archive_geometry_final_frame &frame,
    scr_to_img_parameters *param)
{
  if (!param || !frame.present)
    return;
  param->final_angle = frame.final_angle;
  param->final_ratio = frame.final_ratio;
}

/* Extract saved image bounds independently from SCAN_CROP. */
parameter_archive_image_area
parameter_archive_image_area_from (const render_parameters &rparam)
{
  parameter_archive_image_area result;
  result.present = true;
  result.area = rparam.image_area;
  return result;
}

/* Restore the photographic bounds only when the reader supports the feature. */
void
apply_parameter_archive_image_area (const parameter_archive_image_area &bounds,
                                    render_parameters *rparam)
{
  if (!rparam || !bounds.present)
    return;
  rparam->image_area = bounds.area;
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
    const parameter_archive_render_overrides *render_overrides,
    const parameter_archive_geometry_final_frame *geometry_final_frame,
    const parameter_archive_image_area *image_area)
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
          || (int)profile < 0 || profile >= render_output_parameters::output_profile_max
          || !std::isfinite (white_x) || !std::isfinite (white_y)
          || white_x < 0 || white_y <= 0 || white_x + white_y > 1
          || !std::isfinite (output_gamma)
          || (output_gamma != -1 && output_gamma <= 0))
        return archive_fail (error,
                             "invalid structured render override values");
    }

  const bool structured_geometry
      = geometry_final_frame && geometry_final_frame->present;
  if (structured_geometry
      && (!my_isfinite (geometry_final_frame->final_angle)
          || !my_isfinite (geometry_final_frame->final_ratio)
          || geometry_final_frame->final_ratio <= 0))
    return archive_fail (error, "invalid structured geometry final frame");

  const bool structured_image_area = image_area && image_area->present;
  if (structured_image_area && image_area->area.set)
    {
      const int_optional_image_area &area = image_area->area;
      const int limit = std::numeric_limits<int>::max ();
      if (area.x < 0 || area.y < 0 || area.width <= 0 || area.height <= 0
          || area.x > limit - area.width || area.y > limit - area.height)
        return archive_fail (error, "invalid structured photographic image area");
    }

  std::string manifest
      = "{\n"
        "  \"format\": \"org.colorscreen.parameters\",\n"
        "  \"schema_version\": 1,\n";
  if (structured_render || structured_geometry || structured_image_area)
    {
      std::string features;
      if (structured_render)
        features += "\"render-overrides-v1\"";
      if (structured_geometry)
        {
          if (!features.empty ())
            features += ", ";
          features += "\"geometry-final-frame-v1\"";
        }
      if (structured_image_area)
        {
          if (!features.empty ())
            features += ", ";
          features += "\"image-area-v1\"";
        }
      manifest += "  \"required_features\": [" + features + "],\n";
    }
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
             + json_escape (render_output_parameters::output_profile_names
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
  if (structured_geometry)
    manifest += ",\n"
                "    \"geometry_final_frame\": {\n"
                "      \"final_angle\": "
                + json_number (geometry_final_frame->final_angle)
                + ",\n"
                  "      \"final_ratio\": "
                + json_number (geometry_final_frame->final_ratio)
                + "\n"
                  "    }";
  if (structured_image_area)
    {
      const int_optional_image_area &area = image_area->area;
      manifest += ",\n"
                  "    \"image_area\": {\n"
                  "      \"enabled\": "
                  + std::string (area.set ? "true" : "false")
                  + ",\n"
                    "      \"rect\": ["
                  + std::to_string (area.set ? area.x : 0) + ", "
                  + std::to_string (area.set ? area.y : 0) + ", "
                  + std::to_string (area.set ? area.width : 0) + ", "
                  + std::to_string (area.set ? area.height : 0)
                  + "]\n"
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
    const parameter_archive_render_overrides *render_overrides,
    const parameter_archive_geometry_final_frame *geometry_final_frame,
    const parameter_archive_image_area *image_area)
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
                                       render_overrides, geometry_final_frame,
                                       image_area);
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


/* Native schema-v2 registration component. This is intentionally kept
   separate from open_parameter_payload and write_parameter_payload_file:
   neither can produce a complete v2 document until all persistent processing
   domains have a native representation. These helpers NEVER parse CSP text. */
namespace
{
constexpr size_t v2_max_registration_points = 1000000;
constexpr size_t v2_max_mesh_points = 1000000;
constexpr size_t v2_max_json_bytes = 128 * 1024 * 1024;
constexpr size_t v2_max_json_nodes = 12000000;

/* Validate point P before rendering it as an unquoted JSON pair. */
bool
v2_finite_point (point_t p)
{
  return my_isfinite (p.x) && my_isfinite (p.y);
}

/* Validate all channels of COLOUR. */
bool
v2_finite_colour (color_t colour)
{
  return my_isfinite (colour.red) && my_isfinite (colour.green)
         && my_isfinite (colour.blue);
}

/* Return a pair of finite coordinates as JSON. Caller validated P. */
std::string
v2_point_text (point_t p)
{
  return "[" + json_number (p.x) + ", " + json_number (p.y) + "]";
}

/* Return a finite triplet as JSON. Caller validated C. */
std::string
v2_colour_text (color_t c)
{
  return "[" + json_number (c.red) + ", " + json_number (c.green)
         + ", " + json_number (c.blue) + "]";
}

/* Require KEY in OBJECT with TYPE; report the field name in ERROR. */
const json_value *
v2_required (const json_value &object, const char *key, json_value::kind type,
             std::string *error)
{
  const json_value *member = object_member (object, key);
  if (!member || member->type != type)
    {
      archive_fail (error, std::string ("invalid or missing v2 field: ") + key);
      return nullptr;
    }
  return member;
}

/* Read finite NUMBER into a storage type without non-finite overflow. */
template <class T>
bool
v2_real (const json_value &number, T *out)
{
  if (number.type != json_value::kind::number || number.text.empty ())
    return false;
  /* The JSON lexical parser has already validated the number spelling.
     Parse in the classic locale: legacy CSP's setlocale hack must not
     determine whether a native JSON decimal point is accepted. */
  std::istringstream stream (number.text);
  stream.imbue (std::locale::classic ());
  double value = 0;
  if (!(stream >> value) || !my_isfinite (value))
    return false;
  char extra = 0;
  if (stream.get (extra) || !stream.eof ())
    return false;
  if (value < (double)std::numeric_limits<T>::lowest ()
      || value > (double)std::numeric_limits<T>::max ())
    return false;
  T converted = (T)value;
  if (!my_isfinite (converted))
    return false;
  *out = converted;
  return true;
}

/* Decode [x, y] into POINT; no partial publication on failure. */
bool
v2_point (const json_value &value, point_t *point)
{
  if (value.type != json_value::kind::array
      || value.array_value.size () != 2)
    return false;
  point_t parsed;
  if (!v2_real (value.array_value[0], &parsed.x)
      || !v2_real (value.array_value[1], &parsed.y))
    return false;
  *point = parsed;
  return true;
}

/* Decode [r, g, b] into COLOUR in stored precision. */
bool
v2_colour (const json_value &value, color_t *colour)
{
  if (value.type != json_value::kind::array
      || value.array_value.size () != 3)
    return false;
  color_t parsed;
  if (!v2_real (value.array_value[0], &parsed.red)
      || !v2_real (value.array_value[1], &parsed.green)
      || !v2_real (value.array_value[2], &parsed.blue))
    return false;
  *colour = parsed;
  return true;
}

/* Decode a typed required finite scalar into OUT. */
template <class T>
bool
v2_field_real (const json_value &object, const char *key, T *out,
               std::string *error)
{
  const json_value *v
      = v2_required (object, key, json_value::kind::number, error);
  if (!v)
    return false;
  if (!v2_real (*v, out))
    return archive_fail (error, std::string ("invalid v2 number: ") + key);
  return true;
}

/* Decode a typed required boolean into OUT. */
bool
v2_field_bool (const json_value &object, const char *key, bool *out,
               std::string *error)
{
  const json_value *v
      = v2_required (object, key, json_value::kind::boolean, error);
  if (!v)
    return false;
  *out = v->boolean_value;
  return true;
}

/* Decode a required point-valued field into POINT. */
bool
v2_field_point (const json_value &object, const char *key, point_t *point,
                std::string *error)
{
  const json_value *v
      = v2_required (object, key, json_value::kind::array, error);
  return v && (v2_point (*v, point)
               || archive_fail (error,
                                std::string ("invalid v2 point: ") + key));
}

/* Decode an identifier from N stable choices; never serialize enum ordinals. */
bool
v2_enum (const json_value &object, const char *key,
         const std::vector<std::string> &names, int *out,
         std::string *error)
{
  const json_value *v
      = v2_required (object, key, json_value::kind::string, error);
  if (!v)
    return false;
  for (size_t i = 0; i < names.size (); ++i)
    if (v->text == names[i])
      {
        *out = (int)i;
        return true;
      }
  return archive_fail (error, std::string ("unknown v2 enum value for ") + key);
}

/* Build a list of stable ids from one property_t table. */
template <class T>
std::vector<std::string>
v2_property_names (const T *names, size_t count)
{
  std::vector<std::string> result;
  result.reserve (count);
  for (size_t i = 0; i < count; ++i)
    result.emplace_back (names[i].name);
  return result;
}

/* Validate the complete registration component before producing JSON.
   Particularly large meshes and point sets are bounded before traversal. */
bool
v2_validate_registration (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *error)
{
  if ((int)geometry.type < 0 || (int)geometry.type >= max_scr_type
      || (int)geometry.scanner_type < 0
      || (int)geometry.scanner_type >= max_scanner_type)
    return archive_fail (error, "invalid v2 screen/scanner type");

  if (!v2_finite_point (geometry.center)
      || !v2_finite_point (geometry.coordinate1)
      || !v2_finite_point (geometry.coordinate2)
      || !v2_finite_point (geometry.lens_correction.center)
      || !my_isfinite (geometry.projection_distance)
      || !my_isfinite (geometry.tilt_x)
      || !my_isfinite (geometry.tilt_y)
      || !my_isfinite (geometry.final_rotation)
      || !my_isfinite (geometry.final_angle)
      || !my_isfinite (geometry.final_ratio)
      || !(geometry.final_ratio > 0))
    return archive_fail (error, "invalid v2 geometry scalar");
  for (coord_t kr : geometry.lens_correction.kr)
    if (!my_isfinite (kr))
      return archive_fail (error, "invalid v2 radial lens coefficient");

  if (!v2_finite_colour (detection.red)
      || !v2_finite_colour (detection.green)
      || !v2_finite_colour (detection.blue)
      || !v2_finite_colour (detection.black)
      || !my_isfinite (detection.min_luminosity)
      || !my_isfinite (detection.min_ratio))
    return archive_fail (error, "invalid v2 screen detection control");

  if (!my_isfinite (solver.lens_center_distance)
      || solver.points.size () > v2_max_registration_points
      || profile_spots.size () > v2_max_registration_points)
    return archive_fail (error, "invalid v2 registration point count/policy");
  for (const auto &p : solver.points)
    if (!v2_finite_point (p.img) || !v2_finite_point (p.scr)
        || (int)p.color < 0
        || (int)p.color >= solver_parameters::max_point_color)
      return archive_fail (error, "invalid v2 registration control point");
  for (point_t point : profile_spots)
    if (!v2_finite_point (point))
      return archive_fail (error, "invalid v2 profile spot");

  if (geometry.mesh_trans)
    {
      const mesh &m = *geometry.mesh_trans;
      const int w = m.get_width (), h = m.get_height ();
      if (w < 2 || h < 2
          || (size_t)w > v2_max_mesh_points / (size_t)h
          || !my_isfinite (m.get_xshift ())
          || !my_isfinite (m.get_yshift ())
          || !my_isfinite (m.get_xstep ())
          || !my_isfinite (m.get_ystep ())
          || m.get_xstep () == 0 || m.get_ystep () == 0)
        return archive_fail (error, "invalid v2 mesh dimensions/geometry");
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          if (!v2_finite_point (m.get_point ({x, y})))
            return archive_fail (error, "nonfinite v2 mesh point");
    }
  return true;
}

/* Decode a native mesh object, checking shape and actual point count before
   allocation. An omitted mesh must be an explicit JSON null. */
bool
v2_decode_mesh (const json_value &value, scr_to_img_parameters *geometry,
                std::string *error)
{
  if (value.type == json_value::kind::null_value)
    {
      geometry->mesh_trans.reset ();
      geometry->mesh_trans_is_scr_to_img = false;
      return true;
    }
  if (value.type != json_value::kind::object)
    return archive_fail (error, "v2 geometry mesh must be an object or null");

  const json_value *direction
      = v2_required (value, "direction", json_value::kind::string, error);
  const json_value *dimensions
      = v2_required (value, "dimensions", json_value::kind::array, error);
  const json_value *points
      = v2_required (value, "points", json_value::kind::array, error);
  if (!direction || !dimensions || !points)
    return false;
  if (direction->text == "screen-to-image")
    geometry->mesh_trans_is_scr_to_img = true;
  else if (direction->text == "image-to-screen")
    geometry->mesh_trans_is_scr_to_img = false;
  else
    return archive_fail (error, "unknown v2 mesh direction");

  uint64_t w = 0, h = 0;
  if (dimensions->array_value.size () != 2
      || !json_uint64 (dimensions->array_value[0], &w)
      || !json_uint64 (dimensions->array_value[1], &h)
      || w < 2 || h < 2 || w > v2_max_mesh_points / h
      || w > (uint64_t)std::numeric_limits<int>::max ()
      || h > (uint64_t)std::numeric_limits<int>::max ()
      || points->array_value.size () != w * h)
    return archive_fail (error, "invalid v2 mesh point dimensions");

  point_t shift, step;
  if (!v2_field_point (value, "shift", &shift, error)
      || !v2_field_point (value, "step", &step, error)
      || step.x == 0 || step.y == 0)
    return archive_fail (error, "invalid v2 mesh step/shift");

  auto result = std::make_shared<mesh> (
      shift.x, shift.y, step.x, step.y, (int)w, (int)h);
  for (size_t i = 0; i < points->array_value.size (); ++i)
    {
      point_t p;
      if (!v2_point (points->array_value[i], &p)
          || !my_isfinite ((mesh::mesh_coord_t)p.x)
          || !my_isfinite ((mesh::mesh_coord_t)p.y))
        return archive_fail (error, "invalid v2 mesh coordinate");
      result->set_point ({(int)(i % w), (int)(i / w)}, p);
    }
  geometry->mesh_trans = std::move (result);
  return true;
}
} // anonymous namespace

/* Encode the native registration part of a future plain JSON v2 document.
   No CSP mirror or transitional v1 container fields are emitted. */
bool
encode_parameter_json_v2_registration (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 registration output");
  if (!v2_validate_registration (geometry, detection, solver, profile_spots,
                                 error))
    return false;

  std::string text;
  text.reserve (2048 + solver.points.size () * 110
                + profile_spots.size () * 48);
  text += "{\n  \"geometry\": {\n    \"screen_type\": \"";
  text += json_escape (scr_names[(int)geometry.type].name);
  text += "\",\n    \"scanner_type\": \"";
  text += json_escape (scanner_type_names[(int)geometry.scanner_type].name);
  text += "\",\n    \"center\": " + v2_point_text (geometry.center);
  text += ",\n    \"axis_x\": " + v2_point_text (geometry.coordinate1);
  text += ",\n    \"axis_y\": " + v2_point_text (geometry.coordinate2);
  text += ",\n    \"projection_distance\": "
          + json_number (geometry.projection_distance);
  text += ",\n    \"tilt\": "
          + v2_point_text ({geometry.tilt_x, geometry.tilt_y});
  text += ",\n    \"final_rotation\": "
          + json_number (geometry.final_rotation);
  text += ",\n    \"final_mirror\": ";
  text += geometry.final_mirror ? "true" : "false";
  text += ",\n    \"final_angle\": " + json_number (geometry.final_angle);
  text += ",\n    \"final_ratio\": " + json_number (geometry.final_ratio);
  text += ",\n    \"lens\": {\n      \"radial\": [";
  for (int i = 0; i < 4; ++i)
    {
      if (i)
        text += ", ";
      text += json_number (geometry.lens_correction.kr[i]);
    }
  text += "],\n      \"center\": "
          + v2_point_text (geometry.lens_correction.center) + "\n    },\n";
  text += "    \"mesh\": ";
  if (!geometry.mesh_trans)
    text += "null";
  else
    {
      const mesh &m = *geometry.mesh_trans;
      text += "{\n      \"direction\": \"";
      text += geometry.mesh_trans_is_scr_to_img
                  ? "screen-to-image" : "image-to-screen";
      text += "\",\n      \"shift\": "
              + v2_point_text ({m.get_xshift (), m.get_yshift ()});
      text += ",\n      \"step\": "
              + v2_point_text ({m.get_xstep (), m.get_ystep ()});
      text += ",\n      \"dimensions\": ["
              + std::to_string (m.get_width ()) + ", "
              + std::to_string (m.get_height ()) + "],\n      \"points\": [";
      for (int y = 0; y < m.get_height (); ++y)
        for (int x = 0; x < m.get_width (); ++x)
          {
            if (x || y)
              text += ", ";
            text += v2_point_text (m.get_point ({x, y}));
          }
      text += "]\n    }";
    }
  text += "\n  },\n  \"detection\": {\n    \"red\": "
          + v2_colour_text (detection.red);
  text += ",\n    \"green\": " + v2_colour_text (detection.green);
  text += ",\n    \"blue\": " + v2_colour_text (detection.blue);
  text += ",\n    \"black\": " + v2_colour_text (detection.black);
  text += ",\n    \"min_luminosity\": "
          + json_number (detection.min_luminosity);
  text += ",\n    \"min_ratio\": " + json_number (detection.min_ratio);
  text += "\n  },\n  \"registration\": {\n    \"optimize_lens\": ";
  text += solver.optimize_lens ? "true" : "false";
  text += ",\n    \"lens_center_distance\": "
          + json_number (solver.lens_center_distance);
  text += ",\n    \"optimize_tilt\": ";
  text += solver.optimize_tilt ? "true" : "false";
  text += ",\n    \"points\": [";
  bool first_point = true;
  for (const auto &point : solver.points)
    {
      if (!first_point)
        text += ",";
      first_point = false;
      text += "\n      {\"image\": " + v2_point_text (point.img)
              + ", \"screen\": " + v2_point_text (point.scr)
              + ", \"color\": \""
              + json_escape (solver_parameters::point_color_names
                                 [(int)point.color])
              + "\"}";
    }
  text += "\n    ]\n  },\n  \"profile\": {\n    \"spots\": [";
  for (size_t i = 0; i < profile_spots.size (); ++i)
    {
      if (i)
        text += ", ";
      text += v2_point_text (profile_spots[i]);
    }
  text += "]\n  }\n}\n";
  if (text.size () > v2_max_json_bytes)
    return archive_fail (error, "v2 registration JSON exceeds memory budget");
  *output = std::move (text);
  return true;
}

/* Decode a self-contained native v2 registration component, committing
   geometry, dye detection, solver settings and spots only on total success. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_registration_root (const json_value &root,
    scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, solver_parameters *solver,
    std::vector<point_t> *profile_spots, std::string *error)
{
  const json_value *g
      = v2_required (root, "geometry", json_value::kind::object, error);
  const json_value *d
      = v2_required (root, "detection", json_value::kind::object, error);
  const json_value *r
      = v2_required (root, "registration", json_value::kind::object, error);
  const json_value *profile
      = v2_required (root, "profile", json_value::kind::object, error);
  if (!g || !d || !r || !profile)
    return false;

  scr_to_img_parameters parsed_g;
  scr_detect_parameters parsed_d;
  solver_parameters parsed_r;
  std::vector<point_t> parsed_spots;

  int screen_index = 0, scanner_index = 0;
  if (!v2_enum (*g, "screen_type",
                v2_property_names (scr_names, max_scr_type),
                &screen_index, error)
      || !v2_enum (*g, "scanner_type",
                   v2_property_names (scanner_type_names, max_scanner_type),
                   &scanner_index, error))
    return false;
  parsed_g.type = (scr_type)screen_index;
  parsed_g.scanner_type = (enum scanner_type)scanner_index;
  point_t tilt;
  if (!v2_field_point (*g, "center", &parsed_g.center, error)
      || !v2_field_point (*g, "axis_x", &parsed_g.coordinate1, error)
      || !v2_field_point (*g, "axis_y", &parsed_g.coordinate2, error)
      || !v2_field_real (*g, "projection_distance",
                         &parsed_g.projection_distance, error)
      || !v2_field_point (*g, "tilt", &tilt, error)
      || !v2_field_real (*g, "final_rotation",
                         &parsed_g.final_rotation, error)
      || !v2_field_bool (*g, "final_mirror", &parsed_g.final_mirror, error)
      || !v2_field_real (*g, "final_angle", &parsed_g.final_angle, error)
      || !v2_field_real (*g, "final_ratio", &parsed_g.final_ratio, error)
      || !(parsed_g.final_ratio > 0))
    return archive_fail (error, "invalid v2 geometry fields");
  parsed_g.tilt_x = tilt.x;
  parsed_g.tilt_y = tilt.y;
  const json_value *lens
      = v2_required (*g, "lens", json_value::kind::object, error);
  if (!lens)
    return false;
  const json_value *radial
      = v2_required (*lens, "radial", json_value::kind::array, error);
  if (!radial || radial->array_value.size () != 4)
    return archive_fail (error, "invalid v2 lens polynomial");
  for (int i = 0; i < 4; ++i)
    if (!v2_real (radial->array_value[i],
                  &parsed_g.lens_correction.kr[i]))
      return archive_fail (error, "invalid v2 radial lens coefficient");
  if (!v2_field_point (*lens, "center", &parsed_g.lens_correction.center,
                       error))
    return false;
  const json_value *mesh_object = object_member (*g, "mesh");
  if (!mesh_object || !v2_decode_mesh (*mesh_object, &parsed_g, error))
    return archive_fail (error, "invalid or missing v2 mesh field");

  const json_value *red
      = v2_required (*d, "red", json_value::kind::array, error);
  const json_value *green
      = v2_required (*d, "green", json_value::kind::array, error);
  const json_value *blue
      = v2_required (*d, "blue", json_value::kind::array, error);
  const json_value *black
      = v2_required (*d, "black", json_value::kind::array, error);
  if (!red || !green || !blue || !black
      || !v2_colour (*red, &parsed_d.red)
      || !v2_colour (*green, &parsed_d.green)
      || !v2_colour (*blue, &parsed_d.blue)
      || !v2_colour (*black, &parsed_d.black)
      || !v2_field_real (*d, "min_luminosity",
                         &parsed_d.min_luminosity, error)
      || !v2_field_real (*d, "min_ratio", &parsed_d.min_ratio, error))
    return archive_fail (error, "invalid v2 screen dye controls");

  if (!v2_field_bool (*r, "optimize_lens", &parsed_r.optimize_lens, error)
      || !v2_field_real (*r, "lens_center_distance",
                         &parsed_r.lens_center_distance, error)
      || !v2_field_bool (*r, "optimize_tilt", &parsed_r.optimize_tilt, error))
    return false;
  const json_value *points
      = v2_required (*r, "points", json_value::kind::array, error);
  if (!points || points->array_value.size () > v2_max_registration_points)
    return archive_fail (error, "invalid v2 registration points");
  std::vector<std::string> colour_names;
  for (int i = 0; i < solver_parameters::max_point_color; ++i)
    colour_names.emplace_back (solver_parameters::point_color_names[i]);
  for (const json_value &point : points->array_value)
    {
      if (point.type != json_value::kind::object)
        return archive_fail (error, "v2 registration point must be object");
      solver_parameters::solver_point_t p;
      int colour = 0;
      if (!v2_field_point (point, "image", &p.img, error)
          || !v2_field_point (point, "screen", &p.scr, error)
          || !v2_enum (point, "color", colour_names, &colour, error))
        return false;
      p.color = (solver_parameters::point_color)colour;
      parsed_r.points.push_back (p);
    }

  const json_value *spots
      = v2_required (*profile, "spots", json_value::kind::array, error);
  if (!spots || spots->array_value.size () > v2_max_registration_points)
    return archive_fail (error, "invalid v2 profile spots");
  parsed_spots.reserve (spots->array_value.size ());
  for (const json_value &spot : spots->array_value)
    {
      point_t p;
      if (!v2_point (spot, &p))
        return archive_fail (error, "invalid v2 profile spot");
      parsed_spots.push_back (p);
    }
  if (!v2_validate_registration (parsed_g, parsed_d, parsed_r,
                                 parsed_spots, error))
    return false;
  *geometry = std::move (parsed_g);
  *detection = std::move (parsed_d);
  *solver = std::move (parsed_r);
  *profile_spots = std::move (parsed_spots);
  return true;
}

decode_parameter_json_v2_registration (
    const std::string &input, scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, solver_parameters *solver,
    std::vector<point_t> *profile_spots, std::string *error)
{
  if (error)
    error->clear ();
  if (!geometry || !detection || !solver || !profile_spots)
    return archive_fail (error, "missing v2 registration destination");
  if (input.size () > v2_max_json_bytes)
    return archive_fail (error, "v2 registration JSON exceeds memory budget");
  if (!valid_utf8 (input))
    return archive_fail (error, "v2 registration JSON is not valid UTF-8");

  json_parser parser (input.data (), input.data () + input.size (),
                      v2_max_json_nodes);
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 registration JSON: "
                         + parser.error ());

  return v2_decode_registration_root (root, geometry, detection, solver, profile_spots, error);
}


/* Capture-input scalar values are one independently testable component of
   the proposed full v2 JSON document. They intentionally exclude colour
   adjustments, scanner MTF and calibration grids; those need native codecs
   before a new .cspar writer may be exposed. */
namespace
{
constexpr size_t v2_max_capture_json_bytes = 128 * 1024;

/* Decode a JSON integer exactly, rejecting fractional/exponent spellings
   and any signed 32-bit overflow. std::from_chars does not use locale. */
bool
v2_json_int (const json_value &value, int *out)
{
  if (!out || value.type != json_value::kind::number)
    return false;
  int converted = 0;
  const char *start = value.text.data ();
  const char *end = start + value.text.size ();
  const auto result = std::from_chars (start, end, converted);
  if (result.ec != std::errc () || result.ptr != end)
    return false;
  *out = converted;
  return true;
}

/* Decode a required numeric integer property. */
bool
v2_field_int (const json_value &object, const char *name, int *out,
              std::string *error)
{
  const json_value *value
      = v2_required (object, name, json_value::kind::number, error);
  if (!value)
    return false;
  if (!v2_json_int (*value, out))
    return archive_fail (error, std::string ("invalid v2 integer: ") + name);
  return true;
}

/* Canonical JSON rectangle: the physical object crop and the separately
   selected photographic bounds are both image-pixel rectangles. */
std::string
v2_optional_area_text (const int_optional_image_area &area)
{
  return std::string ("{\"enabled\": ")
         + (area.set ? "true" : "false") + ", \"rect\": ["
         + std::to_string (area.set ? area.x : 0) + ", "
         + std::to_string (area.set ? area.y : 0) + ", "
         + std::to_string (area.set ? area.width : 0) + ", "
         + std::to_string (area.set ? area.height : 0) + "]}";
}

/* A selected area must have positive dimensions and a representable final
   corner. Nonnegative origins are required for photographic image bounds;
   physical-object crop origins may be signed for imported legacy projects. */
bool
v2_valid_area (const int_optional_image_area &area, bool photo)
{
  if (!area.set)
    return true;
  return area.width > 0 && area.height > 0
         && (!photo || (area.x >= 0 && area.y >= 0))
         && (int64_t)area.x + area.width
                <= std::numeric_limits<int>::max ()
         && (int64_t)area.y + area.height
                <= std::numeric_limits<int>::max ();
}

/* Parse enabled/rect data without normalizing an invalid input silently.
   Unselected areas use canonical [0,0,0,0] in JSON and the default sentinel
   representation internally. */
bool
v2_parse_area (const json_value &value, bool photo,
               int_optional_image_area *out, std::string *error)
{
  if (value.type != json_value::kind::object)
    return archive_fail (error, "v2 crop must be a JSON object");
  bool enabled = false;
  if (!v2_field_bool (value, "enabled", &enabled, error))
    return false;
  const json_value *rect
      = v2_required (value, "rect", json_value::kind::array, error);
  if (!rect || rect->array_value.size () != 4)
    return archive_fail (error, "v2 crop rect must have four integers");
  int components[4] {};
  for (int i = 0; i < 4; ++i)
    if (!v2_json_int (rect->array_value[i], &components[i]))
      return archive_fail (error, "invalid v2 crop coordinate");
  if (!enabled)
    {
      for (int component : components)
        if (component != 0)
          return archive_fail (error,
                               "disabled v2 crop must use zero coordinates");
      *out = int_optional_image_area ();
      return true;
    }
  int_optional_image_area area (
      int_image_area (components[0], components[1],
                      components[2], components[3]));
  if (!v2_valid_area (area, photo))
    return archive_fail (error, "invalid v2 selected crop bounds");
  *out = area;
  return true;
}

/* Validate the selected capture scalars before writing any output. */
bool
v2_valid_capture (const render_parameters &capture, std::string *error)
{
  if ((int)capture.capture_type < 0
      || (int)capture.capture_type >= render_parameters::capture_max
      || (int)capture.demosaic < 0
      || (int)capture.demosaic >= image_data::demosaic_max)
    return archive_fail (error, "unknown v2 capture or demosaic choice");
  if (!my_isfinite (capture.gamma)
      || !my_isfinite (capture.scan_exposure)
      || !my_isfinite (capture.dark_point)
      || !my_isfinite (capture.backlight_correction_black))
    return archive_fail (error, "nonfinite v2 capture scalar");
  if (!v2_valid_area (capture.scan_crop, false)
      || !v2_valid_area (capture.image_area, true))
    return archive_fail (error, "invalid v2 input crop/photographic area");
  return true;
}
} // anonymous namespace

/* Serialize selected capture inputs independently of old CSP keywords or
   schema-v1 ZIP supplements. OUTPUT is unchanged on failure. */
bool
encode_parameter_json_v2_capture (const render_parameters &capture,
                                  std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 capture output");
  if (!v2_valid_capture (capture, error))
    return false;
  std::string text;
  text.reserve (620);
  text += "{\n  \"capture\": {\n    \"capture_type\": \"";
  text += json_escape (render_parameters::capture_properties
                           [(int)capture.capture_type].name);
  text += "\",\n    \"demosaic\": \"";
  text += json_escape (image_data::demosaic_names
                           [(int)capture.demosaic].name);
  text += "\",\n    \"gamma\": " + json_number (capture.gamma);
  text += ",\n    \"scan_rotation_quarter_turns\": "
          + std::to_string (capture.scan_rotation);
  text += ",\n    \"scan_mirror\": ";
  text += capture.scan_mirror ? "true" : "false";
  text += ",\n    \"scan_crop\": "
          + v2_optional_area_text (capture.scan_crop);
  text += ",\n    \"image_area\": "
          + v2_optional_area_text (capture.image_area);
  text += ",\n    \"scan_exposure\": "
          + json_number (capture.scan_exposure);
  text += ",\n    \"dark_point\": " + json_number (capture.dark_point);
  text += ",\n    \"backlight_correction_black\": "
          + json_number (capture.backlight_correction_black);
  text += "\n  }\n}\n";
  if (text.size () > v2_max_capture_json_bytes)
    return archive_fail (error, "v2 capture JSON exceeds size limit");
  *output = std::move (text);
  return true;
}

/* Decode a native capture component while preserving unrelated colour,
   sharpness, reconstruction and grid controls in CAPTURE. No output changes
   until the complete document has passed strict validation. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_capture_root (const json_value &root,
    render_parameters *capture, std::string *error)
{
  const json_value *object
      = v2_required (root, "capture", json_value::kind::object, error);
  if (!object)
    return false;

  render_parameters parsed = *capture;
  int kind = 0, method = 0;
  if (!v2_enum (*object, "capture_type",
                v2_property_names (render_parameters::capture_properties,
                                   render_parameters::capture_max),
                &kind, error)
      || !v2_enum (*object, "demosaic",
                   v2_property_names (image_data::demosaic_names,
                                      image_data::demosaic_max),
                   &method, error)
      || !v2_field_real (*object, "gamma", &parsed.gamma, error)
      || !v2_field_int (*object, "scan_rotation_quarter_turns",
                        &parsed.scan_rotation, error)
      || !v2_field_bool (*object, "scan_mirror", &parsed.scan_mirror, error)
      || !v2_field_real (*object, "scan_exposure",
                         &parsed.scan_exposure, error)
      || !v2_field_real (*object, "dark_point", &parsed.dark_point, error)
      || !v2_field_real (*object, "backlight_correction_black",
                         &parsed.backlight_correction_black, error))
    return false;
  parsed.capture_type = (enum render_parameters::capture_type)kind;
  parsed.demosaic = (image_data::demosaicing_t)method;

  const json_value *crop
      = v2_required (*object, "scan_crop", json_value::kind::object, error);
  const json_value *image
      = v2_required (*object, "image_area", json_value::kind::object, error);
  if (!crop || !image || !v2_parse_area (*crop, false, &parsed.scan_crop, error)
      || !v2_parse_area (*image, true, &parsed.image_area, error)
      || !v2_valid_capture (parsed, error))
    return false;
  *capture = std::move (parsed);
  return true;
}

decode_parameter_json_v2_capture (const std::string &input,
                                  render_parameters *capture,
                                  std::string *error)
{
  if (error)
    error->clear ();
  if (!capture)
    return archive_fail (error, "missing v2 capture destination");
  if (input.size () > v2_max_capture_json_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 capture UTF-8/size");
  json_parser parser (input.data (), input.data () + input.size ());
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 capture JSON: " + parser.error ());
  return v2_decode_capture_root (root, capture, error);
}


/* Native JSON reconstruction state: the analysed scalar image layer and
   the material-screen demosaicing pipeline, including both independent noise
   removal stages. Parameters remain saved even when their mode is off. */
namespace
{
/* Validate all denoise fields, including inactive ones. Those are genuine
   undoable user settings and must survive a v2 round trip unchanged. */
bool
v2_valid_denoise (const denoise_parameters &p)
{
  return (int)p.mode >= 0 && (int)p.mode < denoise_parameters::denoise_mode_max
         && my_isfinite (p.strength)
         && my_isfinite (p.noise_variance_floor)
         && my_isfinite (p.noise_variance_slope)
         && my_isfinite (p.bilateral_sigma_s)
         && my_isfinite (p.bilateral_sigma_r);
}

/* Format one typed denoise stage. Caller has checked finite fields. */
std::string
v2_denoise_text (const denoise_parameters &p)
{
  std::string text = "{\n      \"mode\": \"";
  text += json_escape (denoise_parameters::denoise_mode_names[(int)p.mode].name);
  text += "\",\n      \"strength\": " + json_number (p.strength);
  text += ",\n      \"noise_variance_floor\": "
          + json_number (p.noise_variance_floor);
  text += ",\n      \"noise_variance_slope\": "
          + json_number (p.noise_variance_slope);
  text += ",\n      \"patch_radius\": " + std::to_string (p.patch_radius);
  text += ",\n      \"search_radius\": " + std::to_string (p.search_radius);
  text += ",\n      \"bilateral_sigma_s\": "
          + json_number (p.bilateral_sigma_s);
  text += ",\n      \"bilateral_sigma_r\": "
          + json_number (p.bilateral_sigma_r);
  text += "\n    }";
  return text;
}

/* Decode a complete denoise object into an independent value. */
bool
v2_decode_denoise (const json_value &object, denoise_parameters *out,
                   std::string *error)
{
  if (object.type != json_value::kind::object)
    return archive_fail (error, "v2 denoise stage must be an object");
  denoise_parameters p;
  int mode = 0;
  if (!v2_enum (object, "mode",
                v2_property_names (denoise_parameters::denoise_mode_names,
                                   denoise_parameters::denoise_mode_max),
                &mode, error)
      || !v2_field_real (object, "strength", &p.strength, error)
      || !v2_field_real (object, "noise_variance_floor",
                         &p.noise_variance_floor, error)
      || !v2_field_real (object, "noise_variance_slope",
                         &p.noise_variance_slope, error)
      || !v2_field_int (object, "patch_radius", &p.patch_radius, error)
      || !v2_field_int (object, "search_radius", &p.search_radius, error)
      || !v2_field_real (object, "bilateral_sigma_s",
                         &p.bilateral_sigma_s, error)
      || !v2_field_real (object, "bilateral_sigma_r",
                         &p.bilateral_sigma_r, error))
    return false;
  p.mode = (denoise_parameters::denoise_mode)mode;
  if (!v2_valid_denoise (p))
    return archive_fail (error, "invalid v2 denoising parameters");
  *out = p;
  return true;
}

/* Validate reconstruction enums and finite scalars before serialization. */
bool
v2_valid_reconstruction (const render_parameters &p, std::string *error)
{
  if ((int)p.collection_quality < 0
      || (int)p.collection_quality >= render_parameters::max_collection_quality
      || (int)p.screen_demosaic < 0
      || (int)p.screen_demosaic >= render_parameters::max_screen_demosaic
      || (int)p.demosaiced_scaling < 0
      || (int)p.demosaiced_scaling >= render_parameters::max_demosaiced_scaling)
    return archive_fail (error, "invalid v2 reconstruction algorithm");
  if (!my_isfinite (p.mix_red) || !my_isfinite (p.mix_green)
      || !my_isfinite (p.mix_blue) || !v2_finite_colour (p.mix_dark)
      || !my_isfinite (p.screen_blur_radius)
      || !my_isfinite (p.collection_threshold)
      || !v2_valid_denoise (p.screen_denoise)
      || !v2_valid_denoise (p.demosaiced_denoise))
    return archive_fail (error, "invalid v2 reconstruction scalar");
  return true;
}
} // anonymous namespace

/* Serialize a complete image-layer and screen-reconstruction component
   without invoking any legacy CSP writer. */
bool
encode_parameter_json_v2_reconstruction (const render_parameters &render,
                                         std::string *output,
                                         std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 reconstruction output");
  if (!v2_valid_reconstruction (render, error))
    return false;

  std::string text;
  text.reserve (1900);
  text += "{\n  \"reconstruction\": {\n    \"ignore_infrared\": ";
  text += render.ignore_infrared ? "true" : "false";
  text += ",\n    \"mix_weights\": [";
  text += json_number (render.mix_red) + ", "
          + json_number (render.mix_green) + ", "
          + json_number (render.mix_blue) + "]";
  text += ",\n    \"mix_dark\": " + v2_colour_text (render.mix_dark);
  text += ",\n    \"collection_quality\": \"";
  text += json_escape (render_parameters::collection_quality_names
                           [(int)render.collection_quality].name);
  text += "\",\n    \"screen_demosaic\": \"";
  text += json_escape (render_parameters::screen_demosaic_names
                           [(int)render.screen_demosaic].name);
  text += "\",\n    \"demosaiced_scaling\": \"";
  text += json_escape (render_parameters::demosaiced_scaling_names
                           [(int)render.demosaiced_scaling].name);
  text += "\",\n    \"screen_blur_radius\": "
          + json_number (render.screen_blur_radius);
  text += ",\n    \"collection_threshold\": "
          + json_number (render.collection_threshold);
  text += ",\n    \"screen_denoise\": "
          + v2_denoise_text (render.screen_denoise);
  text += ",\n    \"demosaiced_denoise\": "
          + v2_denoise_text (render.demosaiced_denoise);
  text += "\n  }\n}\n";
  if (text.size () > v2_max_capture_json_bytes)
    return archive_fail (error, "v2 reconstruction JSON exceeds size limit");
  *output = std::move (text);
  return true;
}

/* Parse native reconstruction controls into a copy. The independent input,
   output and MTF/correction settings already in RENDER are not changed. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_reconstruction_root (const json_value &root,
    render_parameters *render, std::string *error)
{
  const json_value *object
      = v2_required (root, "reconstruction", json_value::kind::object, error);
  if (!object)
    return false;

  render_parameters parsed = *render;
  int collection = 0, screen = 0, scaling = 0;
  const json_value *mix
      = v2_required (*object, "mix_weights", json_value::kind::array, error);
  const json_value *dark
      = v2_required (*object, "mix_dark", json_value::kind::array, error);
  if (!mix || !dark || mix->array_value.size () != 3)
    return archive_fail (error, "invalid v2 image-layer RGB mixer");
  if (!v2_real (mix->array_value[0], &parsed.mix_red)
      || !v2_real (mix->array_value[1], &parsed.mix_green)
      || !v2_real (mix->array_value[2], &parsed.mix_blue)
      || !v2_colour (*dark, &parsed.mix_dark))
    return archive_fail (error, "invalid v2 image-layer coefficients");

  if (!v2_field_bool (*object, "ignore_infrared",
                      &parsed.ignore_infrared, error)
      || !v2_enum (*object, "collection_quality",
                   v2_property_names (render_parameters::collection_quality_names,
                                      render_parameters::max_collection_quality),
                   &collection, error)
      || !v2_enum (*object, "screen_demosaic",
                   v2_property_names (render_parameters::screen_demosaic_names,
                                      render_parameters::max_screen_demosaic),
                   &screen, error)
      || !v2_enum (*object, "demosaiced_scaling",
                   v2_property_names (render_parameters::demosaiced_scaling_names,
                                      render_parameters::max_demosaiced_scaling),
                   &scaling, error)
      || !v2_field_real (*object, "screen_blur_radius",
                         &parsed.screen_blur_radius, error)
      || !v2_field_real (*object, "collection_threshold",
                         &parsed.collection_threshold, error))
    return false;
  parsed.collection_quality = (render_parameters::collection_quality_t)collection;
  parsed.screen_demosaic = (render_parameters::screen_demosaic_t)screen;
  parsed.demosaiced_scaling = (render_parameters::demosaiced_scaling_t)scaling;

  const json_value *pre
      = v2_required (*object, "screen_denoise", json_value::kind::object, error);
  const json_value *post
      = v2_required (*object, "demosaiced_denoise",
                     json_value::kind::object, error);
  if (!pre || !post
      || !v2_decode_denoise (*pre, &parsed.screen_denoise, error)
      || !v2_decode_denoise (*post, &parsed.demosaiced_denoise, error)
      || !v2_valid_reconstruction (parsed, error))
    return false;
  *render = std::move (parsed);
  return true;
}

decode_parameter_json_v2_reconstruction (const std::string &input,
                                         render_parameters *render,
                                         std::string *error)
{
  if (error)
    error->clear ();
  if (!render)
    return archive_fail (error, "missing v2 reconstruction destination");
  if (input.size () > v2_max_capture_json_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 reconstruction UTF-8/size");

  json_parser parser (input.data (), input.data () + input.size ());
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 reconstruction JSON: "
                         + parser.error ());
  return v2_decode_reconstruction_root (root, render, error);
}


/* Persisted historical-process controls: physical screen strip geometry,
   dye model/age/density, and the exact contact-copy H&D curve and exposure.
   View output colourspace and per-render gamut warnings are not part of this
   component. */
namespace
{
/* Preserve binary32 H&D coordinates without converting through a CSP file. */
std::string
v2_lum_pair_text (luminosity_t x, luminosity_t y)
{
  return "[" + json_number (x) + ", " + json_number (y) + "]";
}

/* Read an exact 2-tuple of H&D curve coordinates. */
bool
v2_lum_pair (const json_value &value, luminosity_t *x,
             luminosity_t *y)
{
  if (value.type != json_value::kind::array
      || value.array_value.size () != 2)
    return false;
  luminosity_t px, py;
  if (!v2_real (value.array_value[0], &px)
      || !v2_real (value.array_value[1], &py))
    return false;
  *x = px;
  *y = py;
  return true;
}

/* Decode named [x,y] H&D points with precise field diagnostics. */
bool
v2_named_lum_pair (const json_value &object, const char *name,
                   luminosity_t *x, luminosity_t *y, std::string *error)
{
  const json_value *value
      = v2_required (object, name, json_value::kind::array, error);
  if (!value)
    return false;
  if (!v2_lum_pair (*value, x, y))
    return archive_fail (error,
                         std::string ("invalid v2 contact-copy point: ") + name);
  return true;
}

/* All stored characteristic-curve coordinates are input settings,
   including those inactive while contact-copy simulation is off. */
bool
v2_finite_hd_curve (const hd_curve_parameters &curve)
{
  return my_isfinite (curve.minx) && my_isfinite (curve.miny)
         && my_isfinite (curve.linear1x) && my_isfinite (curve.linear1y)
         && my_isfinite (curve.linear2x) && my_isfinite (curve.linear2y)
         && my_isfinite (curve.maxx) && my_isfinite (curve.maxy);
}

/* Check every saved process control before writing partial JSON. */
bool
v2_valid_process (const render_parameters &p, std::string *error)
{
  if ((int)p.color_model < 0
      || (int)p.color_model >= render_parameters::color_model_max)
    return archive_fail (error, "unknown v2 historical dye model");
  if (!my_isfinite (p.red_strip_width)
      || !my_isfinite (p.green_strip_width)
      || !v2_finite_colour (p.age)
      || !v2_finite_colour (p.dye_density)
      || !my_isfinite (p.contact_copy.preflash)
      || !my_isfinite (p.contact_copy.exposure)
      || !my_isfinite (p.contact_copy.boost)
      || !v2_finite_hd_curve (p.contact_copy.emulsion_characteristic_curve))
    return archive_fail (error, "nonfinite v2 historical-process parameter");
  return true;
}
} // anonymous namespace

/* Encode historical physical material/process properties, including the
   characteristic-curve control points, as one native JSON component. */
bool
encode_parameter_json_v2_process (const render_parameters &render,
                                  std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 process output");
  if (!v2_valid_process (render, error))
    return false;

  const hd_curve_parameters &curve
      = render.contact_copy.emulsion_characteristic_curve;
  std::string text;
  text.reserve (1150);
  text += "{\n  \"process\": {\n    \"color_model\": \"";
  text += json_escape (render_parameters::color_model_properties
                           [(int)render.color_model].name);
  text += "\",\n    \"age\": " + v2_colour_text (render.age);
  text += ",\n    \"dye_density\": " + v2_colour_text (render.dye_density);
  text += ",\n    \"strip_widths\": {\n      \"red\": "
          + json_number (render.red_strip_width);
  text += ",\n      \"green\": "
          + json_number (render.green_strip_width);
  text += "\n    },\n    \"contact_copy\": {\n      \"simulate\": ";
  text += render.contact_copy.simulate ? "true" : "false";
  text += ",\n      \"preflash\": "
          + json_number (render.contact_copy.preflash);
  text += ",\n      \"exposure\": "
          + json_number (render.contact_copy.exposure);
  text += ",\n      \"boost\": "
          + json_number (render.contact_copy.boost);
  text += ",\n      \"emulsion_curve\": {\n        \"min\": "
          + v2_lum_pair_text (curve.minx, curve.miny);
  text += ",\n        \"linear1\": "
          + v2_lum_pair_text (curve.linear1x, curve.linear1y);
  text += ",\n        \"linear2\": "
          + v2_lum_pair_text (curve.linear2x, curve.linear2y);
  text += ",\n        \"max\": "
          + v2_lum_pair_text (curve.maxx, curve.maxy);
  text += "\n      }\n    }\n  }\n}\n";
  if (text.size () > v2_max_capture_json_bytes)
    return archive_fail (error, "v2 process JSON exceeds size limit");
  *output = std::move (text);
  return true;
}

/* Parse a complete process group privately, commit only on success, and
   retain unrelated capture/sharpness/colour parameters untouched. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_process_root (const json_value &root,
    render_parameters *render, std::string *error)
{
  const json_value *object
      = v2_required (root, "process", json_value::kind::object, error);
  if (!object)
    return false;
  render_parameters parsed = *render;
  int color_model = 0;
  if (!v2_enum (*object, "color_model",
                v2_property_names (render_parameters::color_model_properties,
                                   render_parameters::color_model_max),
                &color_model, error))
    return false;
  parsed.color_model = (render_parameters::color_model_t)color_model;

  const json_value *age
      = v2_required (*object, "age", json_value::kind::array, error);
  const json_value *density
      = v2_required (*object, "dye_density",
                     json_value::kind::array, error);
  const json_value *strips
      = v2_required (*object, "strip_widths", json_value::kind::object, error);
  const json_value *copy
      = v2_required (*object, "contact_copy", json_value::kind::object, error);
  if (!age || !density || !strips || !copy)
    return false;
  if (!v2_colour (*age, &parsed.age)
      || !v2_colour (*density, &parsed.dye_density)
      || !v2_field_real (*strips, "red", &parsed.red_strip_width, error)
      || !v2_field_real (*strips, "green", &parsed.green_strip_width, error)
      || !v2_field_bool (*copy, "simulate", &parsed.contact_copy.simulate,
                         error)
      || !v2_field_real (*copy, "preflash",
                         &parsed.contact_copy.preflash, error)
      || !v2_field_real (*copy, "exposure",
                         &parsed.contact_copy.exposure, error)
      || !v2_field_real (*copy, "boost", &parsed.contact_copy.boost, error))
    return archive_fail (error, "invalid v2 historical-process fields");

  const json_value *hd
      = v2_required (*copy, "emulsion_curve", json_value::kind::object,
                     error);
  if (!hd)
    return false;
  hd_curve_parameters &curve
      = parsed.contact_copy.emulsion_characteristic_curve;
  if (!v2_named_lum_pair (*hd, "min", &curve.minx, &curve.miny, error)
      || !v2_named_lum_pair (*hd, "linear1",
                             &curve.linear1x, &curve.linear1y, error)
      || !v2_named_lum_pair (*hd, "linear2",
                             &curve.linear2x, &curve.linear2y, error)
      || !v2_named_lum_pair (*hd, "max", &curve.maxx, &curve.maxy, error)
      || !v2_valid_process (parsed, error))
    return false;
  *render = std::move (parsed);
  return true;
}

decode_parameter_json_v2_process (const std::string &input,
                                  render_parameters *render,
                                  std::string *error)
{
  if (error)
    error->clear ();
  if (!render)
    return archive_fail (error, "missing v2 process destination");
  if (input.size () > v2_max_capture_json_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 process UTF-8/size");
  json_parser parser (input.data (), input.data () + input.size ());
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 process JSON: "
                         + parser.error ());

  return v2_decode_process_root (root, render, error);
}


/* Stored colour calibration and appearance. The profile describes the
   scanned object and its historical dyes; output ICC/gamma/gamut belong
   to the independent runtime render request, not this JSON component. */
namespace
{
constexpr size_t v2_max_tone_points = 100000;
constexpr size_t v2_max_color_json_bytes = 16 * 1024 * 1024;
constexpr size_t v2_max_color_json_nodes = 500000;

/* Return true only for finite XYZ camera/scanner primaries. */
bool
v2_finite_xyz (xyz c)
{
  return my_isfinite (c.x) && my_isfinite (c.y)
         && my_isfinite (c.z);
}

/* Serialize a scanner XYZ triplet in its original binary32 precision. */
std::string
v2_xyz_text (xyz c)
{
  return "[" + json_number (c.x) + ", " + json_number (c.y)
         + ", " + json_number (c.z) + "]";
}

/* Decode one RGB/XYZ triplet, preserving the stored luminosity_t type. */
bool
v2_xyz (const json_value &value, xyz *result)
{
  if (value.type != json_value::kind::array
      || value.array_value.size () != 3)
    return false;
  xyz parsed;
  if (!v2_real (value.array_value[0], &parsed.x)
      || !v2_real (value.array_value[1], &parsed.y)
      || !v2_real (value.array_value[2], &parsed.z))
    return false;
  *result = parsed;
  return true;
}

/* Check all saved calibration/tone inputs, even if an appearance adjustment
   is currently disabled. A value outside a UI slider's range need not be
   discarded when importing old parameter files. */
bool
v2_valid_color (const render_parameters &p, std::string *error)
{
  if ((int)p.dye_balance < 0
      || (int)p.dye_balance >= render_parameters::dye_balance_max
      || (int)p.output_tone_curve < 0
      || (int)p.output_tone_curve >= tone_curve::tone_curve_max)
    return archive_fail (error, "unknown v2 color/curve algorithm");
  if (!v2_finite_xyz (p.scanner_red)
      || !v2_finite_xyz (p.scanner_green)
      || !v2_finite_xyz (p.scanner_blue)
      || !v2_finite_colour (p.profiled_dark)
      || !v2_finite_colour (p.profiled_red)
      || !v2_finite_colour (p.profiled_green)
      || !v2_finite_colour (p.profiled_blue)
      || !v2_finite_colour (p.white_balance)
      || !my_isfinite (p.presaturation)
      || !my_isfinite (p.temperature)
      || !my_isfinite (p.backlight_temperature)
      || !my_isfinite (p.observer_whitepoint.x)
      || !my_isfinite (p.observer_whitepoint.y)
      || !my_isfinite (p.saturation)
      || !my_isfinite (p.brightness))
    return archive_fail (error, "nonfinite v2 color/appearance value");
  if (p.output_tone_curve_control_points.size () > v2_max_tone_points)
    return archive_fail (error, "v2 tone curve has too many points");
  for (point_t point : p.output_tone_curve_control_points)
    if (!v2_finite_point (point))
      return archive_fail (error, "nonfinite v2 tone-curve control point");
  return true;
}

/* Decode one XYZ member from a three-component JSON array. */
bool
v2_field_xyz (const json_value &object, const char *key,
              xyz *out, std::string *error)
{
  const json_value *value
      = v2_required (object, key, json_value::kind::array, error);
  if (!value)
    return false;
  if (!v2_xyz (*value, out))
    return archive_fail (error, std::string ("invalid v2 XYZ: ") + key);
  return true;
}

/* Decode one RGB member from a three-component JSON array. */
bool
v2_field_rgb (const json_value &object, const char *key,
              rgbdata *out, std::string *error)
{
  const json_value *value
      = v2_required (object, key, json_value::kind::array, error);
  if (!value)
    return false;
  if (!v2_colour (*value, out))
    return archive_fail (error, std::string ("invalid v2 RGB: ") + key);
  return true;
}
} // anonymous namespace

/* Encode the native colour section directly from saved render parameters.
   The precision and order are deterministic; no CSP output is generated. */
bool
encode_parameter_json_v2_color (const render_parameters &render,
                                std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 colour output");
  if (!v2_valid_color (render, error))
    return false;
  std::string text;
  text.reserve (1400 + render.output_tone_curve_control_points.size () * 48);
  text += "{\n  \"color\": {\n    \"scanner_primaries\": {\n";
  text += "      \"red\": " + v2_xyz_text (render.scanner_red);
  text += ",\n      \"green\": " + v2_xyz_text (render.scanner_green);
  text += ",\n      \"blue\": " + v2_xyz_text (render.scanner_blue);
  text += "\n    },\n    \"process_profile\": {\n";
  text += "      \"dark\": " + v2_colour_text (render.profiled_dark);
  text += ",\n      \"red\": " + v2_colour_text (render.profiled_red);
  text += ",\n      \"green\": " + v2_colour_text (render.profiled_green);
  text += ",\n      \"blue\": " + v2_colour_text (render.profiled_blue);
  text += "\n    },\n    \"white_balance\": "
          + v2_colour_text (render.white_balance);
  text += ",\n    \"presaturation\": " + json_number (render.presaturation);
  text += ",\n    \"temperature\": " + json_number (render.temperature);
  text += ",\n    \"backlight_temperature\": "
          + json_number (render.backlight_temperature);
  text += ",\n    \"observer_whitepoint\": "
          + v2_lum_pair_text (render.observer_whitepoint.x,
                              render.observer_whitepoint.y);
  text += ",\n    \"dye_balance\": \"";
  text += json_escape (render_parameters::dye_balance_names
                           [(int)render.dye_balance].name);
  text += "\",\n    \"saturation\": " + json_number (render.saturation);
  text += ",\n    \"brightness\": " + json_number (render.brightness);
  text += ",\n    \"tone_curve\": {\n      \"type\": \"";
  text += json_escape (tone_curve::tone_curve_names
                           [(int)render.output_tone_curve].name);
  text += "\",\n      \"control_points\": [";
  for (size_t i = 0; i < render.output_tone_curve_control_points.size (); ++i)
    {
      if (i)
        text += ", ";
      text += v2_point_text (render.output_tone_curve_control_points[i]);
    }
  text += "]\n    }\n  }\n}\n";
  if (text.size () > v2_max_color_json_bytes)
    return archive_fail (error, "v2 colour JSON exceeds size limit");
  *output = std::move (text);
  return true;
}

/* Parse saved colour calibration and appearance transactionally. No renderer
   output profile/transfer or view-specific gamut warning is touched. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_color_root (const json_value &root,
    render_parameters *render, std::string *error)
{
  const json_value *object
      = v2_required (root, "color", json_value::kind::object, error);
  if (!object)
    return false;
  const json_value *primaries
      = v2_required (*object, "scanner_primaries",
                     json_value::kind::object, error);
  const json_value *profile
      = v2_required (*object, "process_profile",
                     json_value::kind::object, error);
  const json_value *tone
      = v2_required (*object, "tone_curve", json_value::kind::object, error);
  if (!primaries || !profile || !tone)
    return false;

  render_parameters parsed = *render;
  int balance = 0, curve_type = 0;
  if (!v2_field_xyz (*primaries, "red", &parsed.scanner_red, error)
      || !v2_field_xyz (*primaries, "green", &parsed.scanner_green, error)
      || !v2_field_xyz (*primaries, "blue", &parsed.scanner_blue, error)
      || !v2_field_rgb (*profile, "dark", &parsed.profiled_dark, error)
      || !v2_field_rgb (*profile, "red", &parsed.profiled_red, error)
      || !v2_field_rgb (*profile, "green", &parsed.profiled_green, error)
      || !v2_field_rgb (*profile, "blue", &parsed.profiled_blue, error)
      || !v2_field_rgb (*object, "white_balance",
                        &parsed.white_balance, error)
      || !v2_field_real (*object, "presaturation",
                         &parsed.presaturation, error)
      || !v2_field_real (*object, "temperature",
                         &parsed.temperature, error)
      || !v2_field_real (*object, "backlight_temperature",
                         &parsed.backlight_temperature, error)
      || !v2_enum (*object, "dye_balance",
                   v2_property_names (render_parameters::dye_balance_names,
                                      render_parameters::dye_balance_max),
                   &balance, error)
      || !v2_field_real (*object, "saturation", &parsed.saturation, error)
      || !v2_field_real (*object, "brightness", &parsed.brightness, error)
      || !v2_enum (*tone, "type",
                   v2_property_names (tone_curve::tone_curve_names,
                                      tone_curve::tone_curve_max),
                   &curve_type, error))
    return false;
  parsed.dye_balance = (render_parameters::dye_balance_t)balance;
  parsed.output_tone_curve = (tone_curve::tone_curves)curve_type;

  const json_value *white
      = v2_required (*object, "observer_whitepoint",
                     json_value::kind::array, error);
  if (!white || !v2_lum_pair (*white, &parsed.observer_whitepoint.x,
                              &parsed.observer_whitepoint.y))
    return archive_fail (error, "invalid v2 observer whitepoint");
  const json_value *points
      = v2_required (*tone, "control_points", json_value::kind::array, error);
  if (!points || points->array_value.size () > v2_max_tone_points)
    return archive_fail (error, "invalid v2 output tone-curve point count");
  parsed.output_tone_curve_control_points.clear ();
  parsed.output_tone_curve_control_points.reserve (points->array_value.size ());
  for (const json_value &sample : points->array_value)
    {
      point_t point;
      if (!v2_point (sample, &point))
        return archive_fail (error, "invalid v2 tone-curve point");
      parsed.output_tone_curve_control_points.push_back (point);
    }
  if (!v2_valid_color (parsed, error))
    return false;
  *render = std::move (parsed);
  return true;
}

decode_parameter_json_v2_color (const std::string &input,
                                render_parameters *render, std::string *error)
{
  if (error)
    error->clear ();
  if (!render)
    return archive_fail (error, "missing v2 colour destination");
  if (input.size () > v2_max_color_json_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 colour UTF-8/size");
  json_parser parser (input.data (), input.data () + input.size (),
                      v2_max_color_json_nodes);
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 colour JSON: " + parser.error ());
  return v2_decode_color_root (root, render, error);
}


/* Schema-v2 capture sharpness/MTF component. An MTF curve contains not only
   numerical frequencies and contrasts but the accepted image ROI, edge
   quality, channel/wavelength and independently editable provenance. None of
   that persistent evidence may disappear because a model is currently off. */
namespace
{
constexpr size_t v2_max_mtf_measurements = 256;
constexpr size_t v2_max_mtf_samples = 1000000;
constexpr size_t v2_max_sharpness_bytes = 128 * 1024 * 1024;
constexpr size_t v2_max_sharpness_nodes = 5000000;

/* Stable project-file spellings already used by loadsave.C, without
   exporting that legacy serializer's private tables. */
const std::vector<std::string> &
v2_mtf_models ()
{
  static const std::vector<std::string> names
      = {"automatic", "physical-diffraction", "empirical-fallback"};
  return names;
}

/* Stable native sensor/measurement channel names, with -1 as unknown. */
const std::vector<std::string> &
v2_mtf_channels ()
{
  static const std::vector<std::string> names
      = {"unknown", "red", "green", "blue", "ir"};
  return names;
}

/* Preserve even the sentinel-valued ROI coordinates from old imported MTF
   records. The ROI is unavailable when WIDTH or HEIGHT is nonpositive. */
std::string
v2_rectangle_text (int_image_area area)
{
  return "[" + std::to_string (area.x) + ", "
         + std::to_string (area.y) + ", "
         + std::to_string (area.width) + ", "
         + std::to_string (area.height) + "]";
}

/* Decode one exact four-integer rectangle without silently normalizing
   unavailable legacy-provenance ROI values. */
bool
v2_rectangle (const json_value &value, int_image_area *area)
{
  if (!area || value.type != json_value::kind::array
      || value.array_value.size () != 4)
    return false;
  int values[4];
  for (int i = 0; i < 4; ++i)
    if (!v2_json_int (value.array_value[i], &values[i]))
      return false;
  *area = {values[0], values[1], values[2], values[3]};
  return true;
}

/* Preserve finite, individually editable sample/provenance data and bound
   cumulative complexity before producing an MTF JSON document. */
bool
v2_valid_mtf (const mtf_parameters &mtf, std::string *error)
{
  const int mode = (int)mtf.model;
  if (mode < 0 || (size_t)mode >= v2_mtf_models ().size ())
    return archive_fail (error, "unknown v2 scanner MTF model");
  if (!my_isfinite (mtf.sigma)
      || !my_isfinite (mtf.halo_fraction)
      || !my_isfinite (mtf.halo_sigma)
      || !my_isfinite (mtf.blur_diameter)
      || !my_isfinite (mtf.defocus) || !my_isfinite (mtf.f_stop)
      || !my_isfinite (mtf.pixel_pitch)
      || !my_isfinite (mtf.sensor_fill_factor)
      || !my_isfinite (mtf.scan_dpi))
    return archive_fail (error, "nonfinite v2 scanner MTF parameter");
  for (double wavelength : mtf.wavelengths)
    if (!my_isfinite (wavelength))
      return archive_fail (error, "nonfinite v2 channel wavelength");
  if (mtf.measurements.size () > v2_max_mtf_measurements
      || mtf.measured_mtf_idx < -1
      || (mtf.measured_mtf_idx >= 0
          && (size_t)mtf.measured_mtf_idx >= mtf.measurements.size ()))
    return archive_fail (error, "invalid v2 scanner MTF measurement selection");

  size_t total_samples = 0;
  for (const mtf_measurement &measurement : mtf.measurements)
    {
      if (measurement.channel < -1 || measurement.channel > 3
          || !my_isfinite (measurement.wavelength)
          || !my_isfinite (measurement.edge_angle)
          || !my_isfinite (measurement.edge_fit_rms)
          || !my_isfinite (measurement.edge_contrast)
          || !my_isfinite (measurement.edge_snr)
          || !my_isfinite (measurement.phase_coverage)
          || !v2_finite_point (measurement.edge_p1)
          || !v2_finite_point (measurement.edge_p2)
          || !valid_utf8 (measurement.name)
          || !valid_utf8 (measurement.source_filename))
        return archive_fail (error, "invalid v2 measured MTF metadata");
      if (measurement.size () > v2_max_mtf_samples - total_samples)
        return archive_fail (error, "v2 measured MTF sample budget exceeded");
      total_samples += measurement.size ();
      for (size_t i = 0; i < measurement.size (); ++i)
        if (!my_isfinite (measurement.get_freq ((int)i))
            || !my_isfinite (measurement.get_contrast ((int)i))
            || !my_isfinite (measurement.get_uncertainty ((int)i)))
          return archive_fail (error, "nonfinite v2 measured MTF sample");
      /* A selected ROI must not overflow pixel-coordinate arithmetic. Old
         measurement records may retain an unavailable negative-size sentinel. */
      if (measurement.has_spatial_metadata ()
          && ((int64_t)measurement.roi.x + measurement.roi.width
                  > std::numeric_limits<int>::max ()
              || (int64_t)measurement.roi.y + measurement.roi.height
                  > std::numeric_limits<int>::max ()))
        return archive_fail (error, "invalid v2 measured MTF ROI bounds");
    }
  return true;
}

/* Check all persisted capture-sharpening values, even values ignored by the
   currently selected algorithm, without enforcing GUI slider ranges. */
bool
v2_valid_sharpness (const render_parameters &rparam, std::string *error)
{
  const sharpen_parameters &p = rparam.sharpen;
  if ((int)p.mode < 0 || (int)p.mode >= sharpen_parameters::sharpen_mode_max
      || (int)p.resampling < 0
      || (int)p.resampling >= sharpen_parameters::resampling_kernel_max)
    return archive_fail (error, "unknown v2 sharpening/resampling mode");
  if (!my_isfinite (p.usm_radius) || !my_isfinite (p.usm_amount)
      || !my_isfinite (p.scanner_snr)
      || !my_isfinite (p.scanner_mtf_scale)
      || !my_isfinite (p.richardson_lucy_sigma))
    return archive_fail (error, "nonfinite v2 sharpening parameter");
  return v2_valid_mtf (p.scanner_mtf, error);
}

/* Decode a finite JSON numeric tuple with exactly N values. */
bool
v2_number_tuple (const json_value &value, size_t n, double *out)
{
  if (value.type != json_value::kind::array
      || value.array_value.size () != n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (!v2_real (value.array_value[i], &out[i]))
      return false;
  return true;
}
} // anonymous namespace

/* Encode all persistent sharpening, scanner model and measured-MTF evidence
   without using CSP text, while retaining deterministic array order. */
bool
encode_parameter_json_v2_sharpness (const render_parameters &render,
                                    std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 sharpness output");
  if (!v2_valid_sharpness (render, error))
    return false;

  const sharpen_parameters &p = render.sharpen;
  const mtf_parameters &mtf = p.scanner_mtf;
  std::string text;
  text.reserve (2048);
  text += "{\n  \"sharpness\": {\n    \"mode\": \"";
  text += json_escape (sharpen_parameters::sharpen_mode_names[(int)p.mode].name);
  text += "\",\n    \"unsharp_radius\": " + json_number (p.usm_radius);
  text += ",\n    \"unsharp_amount\": " + json_number (p.usm_amount);
  text += ",\n    \"scanner_snr\": " + json_number (p.scanner_snr);
  text += ",\n    \"scanner_mtf_scale\": " + json_number (p.scanner_mtf_scale);
  text += ",\n    \"richardson_lucy_iterations\": "
          + std::to_string (p.richardson_lucy_iterations);
  text += ",\n    \"richardson_lucy_sigma\": "
          + json_number (p.richardson_lucy_sigma);
  text += ",\n    \"deconvolution_supersample\": "
          + std::to_string (p.supersample);
  text += ",\n    \"resampling_kernel\": \"";
  text += json_escape (sharpen_parameters::resampling_kernel_names
                           [(int)p.resampling].name);
  text += "\",\n    \"mtf\": {\n      \"model\": \""
          + json_escape (v2_mtf_models ()[(int)mtf.model]) + "\"";
  text += ",\n      \"sigma_px\": " + json_number (mtf.sigma);
  text += ",\n      \"halo_fraction\": " + json_number (mtf.halo_fraction);
  text += ",\n      \"halo_sigma_px\": " + json_number (mtf.halo_sigma);
  text += ",\n      \"blur_diameter_px\": "
          + json_number (mtf.blur_diameter);
  text += ",\n      \"defocus_mm\": " + json_number (mtf.defocus);
  text += ",\n      \"f_stop\": " + json_number (mtf.f_stop);
  text += ",\n      \"pixel_pitch_um\": " + json_number (mtf.pixel_pitch);
  text += ",\n      \"sensor_fill_factor\": "
          + json_number (mtf.sensor_fill_factor);
  text += ",\n      \"scan_dpi\": " + json_number (mtf.scan_dpi);
  text += ",\n      \"wavelengths_nm\": [";
  for (int i = 0; i < 4; ++i)
    {
      if (i)
        text += ", ";
      text += json_number (mtf.wavelengths[i]);
    }
  text += "]";
  text += ",\n      \"selected_measurement\": "
          + std::to_string (mtf.measured_mtf_idx);
  text += ",\n      \"measurements\": [";

  for (size_t j = 0; j < mtf.measurements.size (); ++j)
    {
      const mtf_measurement &m = mtf.measurements[j];
      if (j)
        text += ",";
      text += "\n        {\n          \"channel\": \""
              + json_escape (v2_mtf_channels ()[(size_t)(m.channel + 1)])
              + "\",\n          \"image_layer\": ";
      text += m.image_layer ? "true" : "false";
      text += ",\n          \"wavelength_nm\": " + json_number (m.wavelength);
      text += ",\n          \"same_capture\": ";
      text += m.same_capture ? "true" : "false";
      text += ",\n          \"name\": \"" + json_escape (m.name) + "\"";
      text += ",\n          \"source_filename\": \""
              + json_escape (m.source_filename) + "\"";
      text += ",\n          \"source_dimensions\": ["
              + std::to_string (m.source_width) + ", "
              + std::to_string (m.source_height) + "]";
      text += ",\n          \"roi\": " + v2_rectangle_text (m.roi);
      text += ",\n          \"edge\": ["
              + v2_point_text (m.edge_p1) + ", "
              + v2_point_text (m.edge_p2) + "]";
      text += ",\n          \"edge_quality\": ["
              + json_number (m.edge_angle) + ", "
              + json_number (m.edge_fit_rms) + ", "
              + json_number (m.edge_contrast) + ", "
              + json_number (m.edge_snr) + ", "
              + json_number (m.phase_coverage) + "]";
      text += ",\n          \"samples\": [";
      for (size_t i = 0; i < m.size (); ++i)
        {
          if (i)
            text += ", ";
          text += "[" + json_number (m.get_freq ((int)i)) + ", "
                  + json_number (m.get_contrast ((int)i)) + ", "
                  + json_number (m.get_uncertainty ((int)i)) + "]";
        }
      text += "]\n        }";
      if (text.size () > v2_max_sharpness_bytes)
        return archive_fail (error, "v2 sharpness JSON exceeds size limit");
    }

  text += "\n      ]\n    }\n  }\n}\n";
  if (text.size () > v2_max_sharpness_bytes || !valid_utf8 (text))
    return archive_fail (error, "v2 sharpness JSON exceeds size/UTF-8 limit");
  *output = std::move (text);
  return true;
}

/* Read all MTF and sharpening inputs as one transaction. A malformed
   measurement, nested array, ROI, or unknown channel cannot modify the
   caller's accepted parameters. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_sharpness_root (const json_value &root,
    render_parameters *render, std::string *error)
{
  const json_value *object
      = v2_required (root, "sharpness", json_value::kind::object, error);
  if (!object)
    return false;
  const json_value *mtf_obj
      = v2_required (*object, "mtf", json_value::kind::object, error);
  if (!mtf_obj)
    return false;

  render_parameters parsed = *render;
  sharpen_parameters &p = parsed.sharpen;
  mtf_parameters &mtf = p.scanner_mtf;
  int mode = 0, kernel = 0, model = 0;
  if (!v2_enum (*object, "mode",
                v2_property_names (sharpen_parameters::sharpen_mode_names,
                                   sharpen_parameters::sharpen_mode_max),
                &mode, error)
      || !v2_enum (*object, "resampling_kernel",
                   v2_property_names (sharpen_parameters::resampling_kernel_names,
                                      sharpen_parameters::resampling_kernel_max),
                   &kernel, error)
      || !v2_field_real (*object, "unsharp_radius", &p.usm_radius, error)
      || !v2_field_real (*object, "unsharp_amount", &p.usm_amount, error)
      || !v2_field_real (*object, "scanner_snr", &p.scanner_snr, error)
      || !v2_field_real (*object, "scanner_mtf_scale",
                         &p.scanner_mtf_scale, error)
      || !v2_field_int (*object, "richardson_lucy_iterations",
                        &p.richardson_lucy_iterations, error)
      || !v2_field_real (*object, "richardson_lucy_sigma",
                         &p.richardson_lucy_sigma, error)
      || !v2_field_int (*object, "deconvolution_supersample",
                        &p.supersample, error)
      || !v2_enum (*mtf_obj, "model", v2_mtf_models (), &model, error))
    return false;
  p.mode = (sharpen_parameters::sharpen_mode)mode;
  p.resampling = (sharpen_parameters::resampling_kernel)kernel;
  mtf.model = (mtf_model)model;

  if (!v2_field_real (*mtf_obj, "sigma_px", &mtf.sigma, error)
      || !v2_field_real (*mtf_obj, "halo_fraction",
                         &mtf.halo_fraction, error)
      || !v2_field_real (*mtf_obj, "halo_sigma_px",
                         &mtf.halo_sigma, error)
      || !v2_field_real (*mtf_obj, "blur_diameter_px",
                         &mtf.blur_diameter, error)
      || !v2_field_real (*mtf_obj, "defocus_mm", &mtf.defocus, error)
      || !v2_field_real (*mtf_obj, "f_stop", &mtf.f_stop, error)
      || !v2_field_real (*mtf_obj, "pixel_pitch_um",
                         &mtf.pixel_pitch, error)
      || !v2_field_real (*mtf_obj, "sensor_fill_factor",
                         &mtf.sensor_fill_factor, error)
      || !v2_field_real (*mtf_obj, "scan_dpi", &mtf.scan_dpi, error)
      || !v2_field_int (*mtf_obj, "selected_measurement",
                        &mtf.measured_mtf_idx, error))
    return false;

  const json_value *wavelengths
      = v2_required (*mtf_obj, "wavelengths_nm",
                     json_value::kind::array, error);
  if (!wavelengths || !v2_number_tuple (*wavelengths, 4,
                                        mtf.wavelengths.data ()))
    return archive_fail (error, "invalid v2 channel wavelengths");

  const json_value *measurements
      = v2_required (*mtf_obj, "measurements", json_value::kind::array, error);
  if (!measurements || measurements->array_value.size () > v2_max_mtf_measurements)
    return archive_fail (error, "too many v2 MTF measurements");
  mtf.measurements.clear ();
  mtf.measurements.reserve (measurements->array_value.size ());
  size_t total_samples = 0;
  for (const json_value &element : measurements->array_value)
    {
      if (element.type != json_value::kind::object)
        return archive_fail (error, "v2 MTF measurement must be an object");
      mtf_measurement m;
      int channel = 0;
      const json_value *name
          = v2_required (element, "name", json_value::kind::string, error);
      const json_value *source
          = v2_required (element, "source_filename",
                         json_value::kind::string, error);
      if (!name || !source
          || !v2_enum (element, "channel", v2_mtf_channels (), &channel,
                       error)
          || !v2_field_bool (element, "image_layer", &m.image_layer, error)
          || !v2_field_bool (element, "same_capture",
                             &m.same_capture, error)
          || !v2_field_real (element, "wavelength_nm",
                             &m.wavelength, error))
        return false;
      m.channel = channel - 1;
      m.name = name->text;
      m.source_filename = source->text;

      const json_value *dimensions
          = v2_required (element, "source_dimensions",
                         json_value::kind::array, error);
      const json_value *roi
          = v2_required (element, "roi", json_value::kind::array, error);
      const json_value *edge
          = v2_required (element, "edge", json_value::kind::array, error);
      const json_value *quality
          = v2_required (element, "edge_quality",
                         json_value::kind::array, error);
      const json_value *samples
          = v2_required (element, "samples",
                         json_value::kind::array, error);
      if (!dimensions || !roi || !edge || !quality || !samples)
        return false;
      if (dimensions->array_value.size () != 2
          || !v2_json_int (dimensions->array_value[0], &m.source_width)
          || !v2_json_int (dimensions->array_value[1], &m.source_height)
          || !v2_rectangle (*roi, &m.roi)
          || edge->array_value.size () != 2
          || !v2_point (edge->array_value[0], &m.edge_p1)
          || !v2_point (edge->array_value[1], &m.edge_p2))
        return archive_fail (error, "invalid v2 MTF spatial provenance");
      double diagnostics[5];
      if (!v2_number_tuple (*quality, 5, diagnostics))
        return archive_fail (error, "invalid v2 MTF edge quality tuple");
      m.edge_angle = diagnostics[0];
      m.edge_fit_rms = diagnostics[1];
      m.edge_contrast = diagnostics[2];
      m.edge_snr = diagnostics[3];
      m.phase_coverage = diagnostics[4];
      if (samples->array_value.size () > v2_max_mtf_samples - total_samples)
        return archive_fail (error, "v2 MTF samples exceed memory budget");
      total_samples += samples->array_value.size ();
      for (const json_value &sample : samples->array_value)
        {
          double values[3];
          if (!v2_number_tuple (sample, 3, values))
            return archive_fail (error, "invalid v2 measured MTF sample");
          m.add_value (values[0], values[1], values[2]);
        }
      mtf.measurements.push_back (std::move (m));
    }
  if (!v2_valid_sharpness (parsed, error))
    return false;
  *render = std::move (parsed);
  return true;
}

decode_parameter_json_v2_sharpness (const std::string &input,
                                    render_parameters *render,
                                    std::string *error)
{
  if (error)
    error->clear ();
  if (!render)
    return archive_fail (error, "missing v2 sharpness destination");
  if (input.size () > v2_max_sharpness_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 sharpness JSON UTF-8/size");

  json_parser parser (input.data (), input.data () + input.size (),
                      v2_max_sharpness_nodes);
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 sharpness JSON: "
                         + parser.error ());
  return v2_decode_sharpness_root (root, render, error);
}


/* Native v2 spatial calibration grids, including stitched per-tile scanner
   optics. The legacy CSP writer serializes these through nested FILE* saves;
   this codec reads the actual calibration resources instead. */
namespace
{
constexpr size_t v2_max_backlight_cells = 262144;
constexpr size_t v2_max_blur_cells = 1000000;
constexpr size_t v2_max_tile_cells = 65536;
constexpr size_t v2_max_grids_bytes = 128 * 1024 * 1024;
constexpr size_t v2_max_grids_nodes = 8000000;

/* Reject overflow and unreasonable allocation before multiplying dimensions.
   An empty 0x0 tile grid is the only permitted empty grid. */
bool
v2_grid_size (int w, int h, size_t limit, bool allow_empty,
              size_t *count)
{
  if (allow_empty && w == 0 && h == 0)
    {
      *count = 0;
      return true;
    }
  if (w <= 0 || h <= 0 || (size_t)w > limit / (size_t)h)
    return false;
  *count = (size_t)w * (size_t)h;
  return true;
}

/* Parse the exact grid shape as signed integers, with allocation limits. */
bool
v2_parse_grid_dimensions (const json_value &object,
                          size_t limit, bool allow_empty,
                          int *width, int *height, size_t *count,
                          std::string *error)
{
  const json_value *dims
      = v2_required (object, "dimensions", json_value::kind::array, error);
  if (!dims || dims->array_value.size () != 2
      || !v2_json_int (dims->array_value[0], width)
      || !v2_json_int (dims->array_value[1], height)
      || !v2_grid_size (*width, *height, limit, allow_empty, count))
    return archive_fail (error, "invalid v2 calibration-grid dimensions");
  return true;
}

/* Validate one scanner-blur correction grid and all saved cell values.
   Per-cell reduction diagnostics are derived analytics, separately exported
   as CSV, and are not part of original CSP persistence. */
bool
v2_valid_blur_grid (
    const std::shared_ptr<scanner_blur_correction_parameters> &blur,
    std::string *error)
{
  if (!blur)
    return true;
  size_t count = 0;
  if (!v2_grid_size (blur->get_width (), blur->get_height (),
                     v2_max_blur_cells, false, &count)
      || (int)blur->get_mode () < 0
      || (int)blur->get_mode ()
             >= scanner_blur_correction_parameters::max_correction)
    return archive_fail (error, "invalid v2 scanner blur correction shape/mode");
  for (size_t i = 0; i < count; ++i)
    if (!my_isfinite (blur->get_correction (
            (int)(i % blur->get_width ()),
            (int)(i / blur->get_width ()))))
      return archive_fail (error, "nonfinite v2 scanner blur correction");
  return true;
}

/* Append a nullable scanner-blur grid, including the explicit physical mode.
   Multiple tile grids may have distinct modes and dimensions. */
bool
v2_append_blur_grid (
    const std::shared_ptr<scanner_blur_correction_parameters> &blur,
    std::string *text, std::string *error)
{
  if (!v2_valid_blur_grid (blur, error))
    return false;
  if (!blur)
    {
      *text += "null";
      return true;
    }
  const int w = blur->get_width (), h = blur->get_height ();
  *text += "{\"mode\": \"";
  *text += json_escape (scanner_blur_correction_parameters::correction_names
                            [(int)blur->get_mode ()]);
  *text += "\", \"dimensions\": [" + std::to_string (w)
           + ", " + std::to_string (h) + "], \"values\": [";
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      {
        if (x || y)
          *text += ", ";
        *text += json_number (blur->get_correction (x, y));
      }
  *text += "]}";
  if (text->size () > v2_max_grids_bytes)
    return archive_fail (error, "v2 spatial corrections exceed size limit");
  return true;
}

/* Validate all directly stored per-tile, black-reference and blur data.
   Do not inadvertently serialize cache-only blur diagnostics or view masks. */
bool
v2_valid_grids (const render_parameters &render, std::string *error)
{
  size_t cells = 0;
  if (!v2_grid_size (render.tile_adjustments_width,
                     render.tile_adjustments_height,
                     v2_max_tile_cells, true, &cells)
      || cells != render.tile_adjustments.size ())
    return archive_fail (error, "invalid v2 stitched-tile correction grid");
  if (!v2_valid_blur_grid (render.scanner_blur_correction, error))
    return false;
  for (const render_parameters::tile_adjustment &tile :
       render.tile_adjustments)
    if (!my_isfinite (tile.exposure)
        || !my_isfinite (tile.dark_point)
        || !v2_valid_blur_grid (tile.scanner_blur_correction, error))
      return archive_fail (error, "invalid v2 stitched-tile calibration");

  if (render.backlight_correction)
    {
      const backlight_correction_parameters &light
          = *render.backlight_correction;
      if (!v2_grid_size (light.get_width (), light.get_height (),
                         v2_max_backlight_cells, false, &cells))
        return archive_fail (error, "invalid v2 backlight dimensions");
      for (size_t i = 0; i < cells; ++i)
        for (int channel = 0; channel < 4; ++channel)
          {
            const int x = (int)(i % light.get_width ());
            const int y = (int)(i / light.get_width ());
            auto c = (backlight_correction_parameters::channel)channel;
            if (!my_isfinite (light.get_luminosity (x, y, c))
                || !my_isfinite (light.get_sub (x, y, c)))
              return archive_fail (error, "nonfinite v2 backlight calibration");
          }
    }
  return true;
}

/* Parse nullable blur corrections into a new resource. Caller owns no
   half-decoded data on failure. */
bool
v2_parse_blur_grid (
    const json_value &value,
    std::shared_ptr<scanner_blur_correction_parameters> *output,
    std::string *error)
{
  if (value.type == json_value::kind::null_value)
    {
      output->reset ();
      return true;
    }
  if (value.type != json_value::kind::object)
    return archive_fail (error, "v2 scanner blur grid must be null/object");
  const json_value *mode
      = v2_required (value, "mode", json_value::kind::string, error);
  const json_value *values
      = v2_required (value, "values", json_value::kind::array, error);
  if (!mode || !values)
    return false;

  int selected_mode = -1;
  for (int i = 0;
       i < scanner_blur_correction_parameters::max_correction; ++i)
    if (mode->text
        == scanner_blur_correction_parameters::correction_names[i])
      selected_mode = i;
  if (selected_mode < 0)
    return archive_fail (error, "unknown v2 scanner blur correction mode");
  int w = 0, h = 0;
  size_t count = 0;
  if (!v2_parse_grid_dimensions (value, v2_max_blur_cells, false,
                                 &w, &h, &count, error)
      || values->array_value.size () != count)
    return archive_fail (error, "invalid v2 scanner blur data dimensions");

  auto result = std::make_shared<scanner_blur_correction_parameters> ();
  if (!result->alloc (
          w, h, (scanner_blur_correction_parameters::correction_mode)selected_mode))
    return archive_fail (error, "could not allocate v2 scanner blur grid");
  for (size_t i = 0; i < count; ++i)
    {
      luminosity_t sample;
      if (!v2_real (values->array_value[i], &sample))
        return archive_fail (error, "invalid v2 scanner blur cell");
      result->set_correction ((int)(i % w), (int)(i / w), sample);
    }
  *output = std::move (result);
  return true;
}

/* Parse nullable backlight data with exact channel and subtraction samples.
   Non-enabled channels are retained as numeric data so a later calibration
   mode switch can reproduce the accepted original table. */
bool
v2_parse_backlight (
    const json_value &value,
    std::shared_ptr<backlight_correction_parameters> *output,
    std::string *error)
{
  if (value.type == json_value::kind::null_value)
    {
      output->reset ();
      return true;
    }
  if (value.type != json_value::kind::object)
    return archive_fail (error, "v2 backlight grid must be null/object");
  const json_value *channels
      = v2_required (value, "channels", json_value::kind::array, error);
  const json_value *lum
      = v2_required (value, "luminosities", json_value::kind::array, error);
  const json_value *sub
      = v2_required (value, "subtractions", json_value::kind::array, error);
  bool black = false;
  if (!channels || !lum || !sub || channels->array_value.size () != 4
      || !v2_field_bool (value, "black_correction", &black, error))
    return archive_fail (error, "invalid v2 backlight channel metadata");
  bool enabled[4];
  for (int c = 0; c < 4; ++c)
    {
      if (channels->array_value[c].type != json_value::kind::boolean)
        return archive_fail (error, "invalid v2 backlight channel flag");
      enabled[c] = channels->array_value[c].boolean_value;
    }
  int w = 0, h = 0;
  size_t count = 0;
  if (!v2_parse_grid_dimensions (value, v2_max_backlight_cells, false,
                                 &w, &h, &count, error)
      || lum->array_value.size () != count
      || sub->array_value.size () != count)
    return archive_fail (error, "invalid v2 backlight sample dimensions");
  auto result = std::make_shared<backlight_correction_parameters> ();
  if (!result->alloc (w, h, enabled))
    return archive_fail (error, "could not allocate v2 backlight correction");
  result->black_correction = black;
  for (size_t i = 0; i < count; ++i)
    {
      const json_value &lum_row = lum->array_value[i];
      const json_value &sub_row = sub->array_value[i];
      if (lum_row.type != json_value::kind::array
          || sub_row.type != json_value::kind::array
          || lum_row.array_value.size () != 4
          || sub_row.array_value.size () != 4)
        return archive_fail (error, "invalid v2 backlight sample tuple");
      for (int c = 0; c < 4; ++c)
        {
          luminosity_t brightness, offset;
          if (!v2_real (lum_row.array_value[c], &brightness)
              || !v2_real (sub_row.array_value[c], &offset))
            return archive_fail (error, "invalid v2 backlight sample");
          auto channel = (backlight_correction_parameters::channel)c;
          const int x = (int)(i % w), y = (int)(i / w);
          result->set_luminosity (x, y, brightness, channel);
          result->set_sub (x, y, offset, channel);
        }
    }
  *output = std::move (result);
  return true;
}
} // anonymous namespace

/* Encode the original calibration tables directly as stable, typed numeric
   JSON arrays; do not use backlight/save() or scanner_blur/save() streams. */
bool
encode_parameter_json_v2_correction_grids (
    const render_parameters &render, std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 corrections output");
  if (!v2_valid_grids (render, error))
    return false;

  std::string text;
  text.reserve (2048);
  text += "{\n  \"correction_grids\": {\n    \"backlight\": ";
  if (!render.backlight_correction)
    text += "null";
  else
    {
      const backlight_correction_parameters &light
          = *render.backlight_correction;
      const int w = light.get_width (), h = light.get_height ();
      text += "{\"dimensions\": [" + std::to_string (w) + ", "
              + std::to_string (h) + "], \"channels\": [";
      for (int c = 0; c < 4; ++c)
        {
          if (c)
            text += ", ";
          text += light.channel_enabled (
              (backlight_correction_parameters::channel)c) ? "true" : "false";
        }
      text += "], \"black_correction\": ";
      text += light.black_correction ? "true" : "false";
      text += ", \"luminosities\": [";
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          {
            if (x || y)
              text += ", ";
            text += "[";
            for (int c = 0; c < 4; ++c)
              {
                if (c)
                  text += ", ";
                text += json_number (light.get_luminosity (
                    x, y, (backlight_correction_parameters::channel)c));
              }
            text += "]";
          }
      text += "], \"subtractions\": [";
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          {
            if (x || y)
              text += ", ";
            text += "[";
            for (int c = 0; c < 4; ++c)
              {
                if (c)
                  text += ", ";
                text += json_number (light.get_sub (
                    x, y, (backlight_correction_parameters::channel)c));
              }
            text += "]";
          }
      text += "]}";
      if (text.size () > v2_max_grids_bytes)
        return archive_fail (error, "v2 backlight data exceeds size limit");
    }
  text += ",\n    \"scanner_blur\": ";
  if (!v2_append_blur_grid (render.scanner_blur_correction, &text, error))
    return false;
  text += ",\n    \"tiles\": {\"dimensions\": ["
          + std::to_string (render.tile_adjustments_width) + ", "
          + std::to_string (render.tile_adjustments_height)
          + "], \"adjustments\": [";
  for (size_t i = 0; i < render.tile_adjustments.size (); ++i)
    {
      if (i)
        text += ", ";
      const render_parameters::tile_adjustment &tile
          = render.tile_adjustments[i];
      text += "{\"exposure\": " + json_number (tile.exposure)
              + ", \"dark_point\": " + json_number (tile.dark_point)
              + ", \"scanner_blur\": ";
      if (!v2_append_blur_grid (tile.scanner_blur_correction, &text, error))
        return false;
      text += "}";
    }
  text += "]}\n  }\n}\n";
  if (text.size () > v2_max_grids_bytes || !valid_utf8 (text))
    return archive_fail (error, "v2 calibration JSON exceeds size/UTF-8 limit");
  *output = std::move (text);
  return true;
}

/* Decode all correction resources privately and commit only when every
   sample and every nested stitched-tile grid is present and valid. */
bool
/* Decode a validated JSON syntax tree into the named native component.
   The public component reader and future full document reader share this
   implementation rather than parsing or serializing nested data twice. */
static bool
v2_decode_correction_grids_root (const json_value &root,
    render_parameters *render, std::string *error)
{
  const json_value *object
      = v2_required (root, "correction_grids",
                     json_value::kind::object, error);
  if (!object)
    return false;
  const json_value *light = object_member (*object, "backlight");
  const json_value *blur = object_member (*object, "scanner_blur");
  const json_value *tile_grid
      = v2_required (*object, "tiles", json_value::kind::object, error);
  if (!light || !blur || !tile_grid)
    return archive_fail (error, "missing required v2 calibration grid");

  render_parameters parsed = *render;
  if (!v2_parse_backlight (*light, &parsed.backlight_correction, error)
      || !v2_parse_blur_grid (*blur, &parsed.scanner_blur_correction, error))
    return false;

  int w = 0, h = 0;
  size_t count = 0;
  const json_value *tiles
      = v2_required (*tile_grid, "adjustments",
                     json_value::kind::array, error);
  if (!tiles || !v2_parse_grid_dimensions (
          *tile_grid, v2_max_tile_cells, true, &w, &h, &count, error)
      || tiles->array_value.size () != count)
    return archive_fail (error, "invalid v2 stitched-tile grid shape");

  parsed.tile_adjustments.clear ();
  parsed.tile_adjustments_width = 0;
  parsed.tile_adjustments_height = 0;
  if (count)
    parsed.set_tile_adjustments_dimensions (w, h);
  for (size_t i = 0; i < count; ++i)
    {
      const json_value &entry = tiles->array_value[i];
      if (entry.type != json_value::kind::object)
        return archive_fail (error, "invalid v2 stitched-tile entry");
      render_parameters::tile_adjustment &tile
          = parsed.tile_adjustments[i];
      if (!v2_field_real (entry, "exposure", &tile.exposure, error)
          || !v2_field_real (entry, "dark_point", &tile.dark_point, error))
        return false;
      const json_value *tile_blur = object_member (entry, "scanner_blur");
      if (!tile_blur
          || !v2_parse_blur_grid (*tile_blur,
                                  &tile.scanner_blur_correction, error))
        return archive_fail (error, "invalid or absent v2 tile blur grid");
    }
  if (!v2_valid_grids (parsed, error))
    return false;
  *render = std::move (parsed);
  return true;
}

decode_parameter_json_v2_correction_grids (
    const std::string &input, render_parameters *render, std::string *error)
{
  if (error)
    error->clear ();
  if (!render)
    return archive_fail (error, "missing v2 corrections destination");
  if (input.size () > v2_max_grids_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 calibration JSON UTF-8/size");
  json_parser parser (input.data (), input.data () + input.size (),
                      v2_max_grids_nodes);
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 calibration JSON: "
                         + parser.error ());
  return v2_decode_correction_grids_root (root, render, error);
}


/* Native schema-v2 document envelope. Unlike the v1 ZIP writer, this has a
   single authoritative typed JSON root. Component encoders create validated
   root objects; append only their members, never a legacy CSP mirror. */
namespace
{
constexpr size_t v2_max_document_bytes = 256 * 1024 * 1024;
constexpr size_t v2_max_document_nodes = 12000000;

/* Append the members of a trusted, validated component root to DOC.
   Each component emits a complete JSON object and knows its stable names.
   Strip only its outermost braces, preserving every nested numeric value. */
bool
v2_append_document_component (const std::string &fragment,
                              std::string *doc, std::string *error)
{
  const size_t first_brace = fragment.find_first_not_of (" \t\r\n");
  const size_t last_brace = fragment.find_last_not_of (" \t\r\n");
  if (first_brace == std::string::npos || last_brace <= first_brace
      || fragment[first_brace] != '{' || fragment[last_brace] != '}')
    return archive_fail (error, "invalid internal v2 component object");
  const size_t first_key
      = fragment.find_first_not_of (" \t\r\n", first_brace + 1);
  const size_t last_key
      = fragment.find_last_not_of (" \t\r\n", last_brace - 1);
  if (first_key == std::string::npos || first_key > last_key
      || last_key >= last_brace)
    return archive_fail (error, "empty internal v2 component");
  if (doc->size () > v2_max_document_bytes - (last_key - first_key + 3))
    return archive_fail (error, "v2 document exceeds size limit");
  *doc += ",\n";
  doc->append (fragment, first_key, last_key - first_key + 1);
  return true;
}
} // anonymous namespace

/* Encode all authoritative persistent processing/calibration domains into
   one complete plain-JSON document. The in-memory result does not change
   Save As, CLI, recovery or on-disk content format until their integration
   and compatibility tests have been completed. */
bool
encode_parameter_json_v2_document (
    const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render,
    const solver_parameters &solver,
    const std::vector<point_t> &profile_spots,
    std::string *output, std::string *error)
{
  if (error)
    error->clear ();
  if (!output)
    return archive_fail (error, "missing v2 document output");
  std::string text
      = "{\n  \"format\": \"org.colorscreen.parameters\",\n"
        "  \"schema_version\": 2";
  std::string component;

  /* Maintain stable ordering for reproducible files and useful diffs. */
  if (!encode_parameter_json_v2_capture (render, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_process (render, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_registration (
             geometry, detection, solver, profile_spots, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_reconstruction (render, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_sharpness (render, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_color (render, &component, error)
      || !v2_append_document_component (component, &text, error)
      || !encode_parameter_json_v2_correction_grids (render, &component, error)
      || !v2_append_document_component (component, &text, error))
    return false;
  text += "\n}\n";
  if (text.size () > v2_max_document_bytes || !valid_utf8 (text))
    return archive_fail (error, "v2 document exceeds UTF-8/size limit");
  *output = std::move (text);
  return true;
}

/* Parse one full JSON syntax tree with strict v2 identity, then use the same
   typed component decoders as focused regression tests. No component may
   publish into the original document before all sections have validated. */
bool
decode_parameter_json_v2_document (
    const std::string &input,
    scr_to_img_parameters *geometry,
    scr_detect_parameters *detection,
    render_parameters *render,
    solver_parameters *solver,
    std::vector<point_t> *profile_spots,
    std::string *error)
{
  if (error)
    error->clear ();
  if (!geometry || !detection || !render || !solver || !profile_spots)
    return archive_fail (error, "missing v2 document destination");
  if (input.size () > v2_max_document_bytes || !valid_utf8 (input))
    return archive_fail (error, "invalid v2 document UTF-8/size");
  json_parser parser (input.data (), input.data () + input.size (),
                      v2_max_document_nodes);
  json_value root;
  if (!parser.parse (&root))
    return archive_fail (error, "invalid v2 document JSON: "
                         + parser.error ());
  if (root.type != json_value::kind::object)
    return archive_fail (error, "v2 document root must be an object");

  const json_value *format
      = v2_required (root, "format", json_value::kind::string, error);
  const json_value *version
      = v2_required (root, "schema_version",
                     json_value::kind::number, error);
  uint64_t schema = 0;
  if (!format || format->text != "org.colorscreen.parameters"
      || !version || !json_uint64 (*version, &schema) || schema != 2)
    return archive_fail (error, "unsupported v2 parameter document identity");

  /* Do not reinterpret old ZIP/CSP mirrors as optional v2 payloads, and do
     not ignore required features that an older reader cannot preserve. */
  if (object_member (root, "state") || object_member (root, "legacy_csp")
      || object_member (root, "payloads"))
    return archive_fail (error, "legacy container members are not v2 state");
  const json_value *features = object_member (root, "required_features");
  if (features && (features->type != json_value::kind::array
                   || !features->array_value.empty ()))
    return archive_fail (error, "unsupported required v2 parameter feature");

  scr_to_img_parameters parsed_geometry;
  scr_detect_parameters parsed_detection;
  render_parameters parsed_render;
  solver_parameters parsed_solver;
  std::vector<point_t> parsed_spots;
  if (!v2_decode_capture_root (root, &parsed_render, error)
      || !v2_decode_process_root (root, &parsed_render, error)
      || !v2_decode_registration_root (
             root, &parsed_geometry, &parsed_detection, &parsed_solver,
             &parsed_spots, error)
      || !v2_decode_reconstruction_root (root, &parsed_render, error)
      || !v2_decode_sharpness_root (root, &parsed_render, error)
      || !v2_decode_color_root (root, &parsed_render, error)
      || !v2_decode_correction_grids_root (root, &parsed_render, error))
    return false;

  *geometry = std::move (parsed_geometry);
  *detection = std::move (parsed_detection);
  *render = std::move (parsed_render);
  *solver = std::move (parsed_solver);
  *profile_spots = std::move (parsed_spots);
  return true;
}


/* Check whether UTF-8 host path NAME looks like a native JSON text file.
   This does not claim full schema-v2 validity: use the complete reader for
   format-marker, version, required fields and hostile input validation. */
bool
parameter_json_v2_signature_p (const char *name)
{
  FILE *file = open_utf8_binary_read (name);
  if (!file)
    return false;
  int first = EOF;
  do
    first = fgetc (file);
  while (first == ' ' || first == '\t' || first == '\r'
         || first == '\n');
  fclose (file);
  return first == '{';
}

/* Stream a whole native JSON document from UTF-8 PATH while enforcing the
   codec's input limit before allocating untrusted arrays. No FILE* returned
   to frontends is disguised as a legacy CSP parameter stream. */
bool
read_parameter_json_v2_file (
    const char *name, scr_to_img_parameters *geometry,
    scr_detect_parameters *detection, render_parameters *render,
    solver_parameters *solver, std::vector<point_t> *profile_spots,
    std::string *error)
{
  if (error)
    error->clear ();
  if (!name || !geometry || !detection || !render || !solver
      || !profile_spots)
    return archive_fail (error, "invalid v2 document file arguments");

  FILE *file = open_utf8_binary_read (name);
  if (!file)
    return archive_fail (error, "could not open native JSON v2 file");
  std::string contents;
  char buffer[64 * 1024];
  bool ok = true;
  while (true)
    {
      const size_t count = fread (buffer, 1, sizeof (buffer), file);
      if (count)
        {
          if (count > v2_max_document_bytes - contents.size ())
            {
              archive_fail (error, "v2 parameter file exceeds size limit");
              ok = false;
              break;
            }
          contents.append (buffer, count);
        }
      if (count < sizeof (buffer))
        {
          if (ferror (file))
            {
              archive_fail (error, "error reading native JSON v2 file");
              ok = false;
            }
          break;
        }
    }
  if (fclose (file) != 0)
    {
      archive_fail (error, "error closing native JSON v2 file");
      ok = false;
    }
  if (!ok)
    return false;
  return decode_parameter_json_v2_document (
      contents, geometry, detection, render, solver, profile_spots, error);
}

/* Write the complete direct native JSON document atomically using the same
   Unicode-path, sibling-staging and replace-on-success writer as schema-v1
   archives. ARCHIVE=false here means raw bytes, never legacy CSP conversion. */
bool
write_parameter_json_v2_file (
    const char *name, const scr_to_img_parameters &geometry,
    const scr_detect_parameters &detection,
    const render_parameters &render, const solver_parameters &solver,
    const std::vector<point_t> &profile_spots, std::string *error)
{
  if (error)
    error->clear ();
  if (!name)
    return archive_fail (error, "missing native JSON v2 output pathname");
  std::string document;
  if (!encode_parameter_json_v2_document (
          geometry, detection, render, solver, profile_spots,
          &document, error))
    return false;
  return write_parameter_payload_file (name, document,
                                       /*archive=*/false, nullptr, error);
}

}
