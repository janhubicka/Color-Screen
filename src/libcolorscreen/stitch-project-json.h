/* Typed data transfer for the native stitching project JSON format.
   The image loader and CLI keep owning their stitch_project instances;
   the codec never depends on Qt or on live image/cache objects.  */

#ifndef COLORSCREEN_STITCH_PROJECT_JSON_H
#define COLORSCREEN_STITCH_PROJECT_JSON_H

#include "include/scr-to-img-parameters.h"
#include "include/dllpublic.h"
#include <string>
#include <vector>

namespace colorscreen
{

/* One tile in row-major order. JSON stores only the screen type and
   schema-3 geometric mapping rather than a full processing document.
   Other per-tile parameter domains are not persistent stitch-project state. */
struct stitch_project_json_tile
{
  std::string filename;
  coord_t angle = 0;
  coord_t ratio = 0;
  point_t position = {0, 0};
  int image_width = 0;
  int image_height = 0;
  int screen_width = 0;
  int screen_height = 0;
  int screen_shift_x = 0;
  int screen_shift_y = 0;
  scr_to_img_parameters geometry;
};

/* Persistent project values; neither source image pixels nor cache state
   belong to the project archive. */
struct stitch_project_json_data
{
  int columns = 0;
  int rows = 0;
  coord_t pixel_size = 0;
  std::vector<stitch_project_json_tile> tiles;
};

/* Encode the canonical native project JSON document (schema 2), leaving
   OUTPUT untouched on error. */
DLL_PUBLIC bool encode_stitch_project_json_document (
    const stitch_project_json_data &project, std::string *output,
    std::string *error);

/* Strict and bounded native project JSON reader. Unknown fields, malformed
   nested parameters and incorrect tile counts leave PROJECT unchanged. */
DLL_PUBLIC bool decode_stitch_project_json_document (
    const std::string &input, stitch_project_json_data *project,
    std::string *error);

}

#endif
