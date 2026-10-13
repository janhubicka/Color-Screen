#ifndef COLORSCREEN_PARAMETER_JSON_BUDGET_H
#define COLORSCREEN_PARAMETER_JSON_BUDGET_H

#include <array>
#include <cstddef>
#include <string_view>

namespace colorscreen
{
/* Reader and writer share aggregate limits; component limits alone do not
   bound a document containing several individually large collections. */
constexpr int parameter_json_max_depth = 32;
constexpr size_t parameter_json_max_nodes = 12000000;
constexpr size_t parameter_json_max_points = 1000000;

/* Check the value count and nesting of already valid, generated JSON TEXT
   against NODE_LIMIT and the reader's depth limit, without constructing
   another object tree. Object keys are not values; string escapes and number
   spelling cannot add nodes. This is a resource preflight, NOT an input syntax
   validator: untrusted documents must still use the strict JSON parser. */
inline bool
parameter_json_resources_fit (std::string_view text, size_t node_limit)
{
  std::array<bool, parameter_json_max_depth + 1> arrays {};
  size_t depth = 0, nodes = 0;
  bool in_string = false, escaped = false, value_expected = true;
  for (char c : text)
    {
      if (in_string)
        {
          if (escaped)
            escaped = false;
          else if (c == '\\')
            escaped = true;
          else if (c == '"')
            in_string = false;
          continue;
        }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        continue;
      if (c == '}' || c == ']')
        {
          if (!depth || arrays[depth - 1] != (c == ']'))
            return false;
          --depth;
          value_expected = false;
          continue;
        }
      if (c == ':' || c == ',')
        {
          value_expected = c == ':' || (depth && arrays[depth - 1]);
          continue;
        }
      if (value_expected)
        {
          if (depth > parameter_json_max_depth || nodes == node_limit)
            return false;
          ++nodes;
          value_expected = false;
        }
      if (c == '"')
        in_string = true;
      else if (c == '{' || c == '[')
        {
          if (depth == arrays.size ())
            return false;
          arrays[depth++] = c == '[';
          value_expected = c == '[';
        }
    }
  return nodes && !depth && !in_string;
}
}
#endif
