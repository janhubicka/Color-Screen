#ifndef RENDER_TYPE_PARAMETERS_H
#define RENDER_TYPE_PARAMETERS_H
#include "base.h"
#include "dllpublic.h"
#include <cstdint>
#include <vector>
namespace colorscreen
{
enum render_type_t
{
  render_type_original,
  render_type_interpolated,
  render_type_predictive,
  render_type_image_layer,
  render_type_screen,
  render_type_realistic,
  render_type_combined,
  render_type_interpolated_original,
  render_type_preview_grid,
  render_type_simulate_process,
  render_type_fast,
  render_type_extra,
  render_type_adjusted_color,
  /* Legacy GTK keyboard grouping only.  Renderer dispatch must never
     infer ownership from enum ordering.  */
  render_type_first_scr_detect = render_type_adjusted_color,
  render_type_normalized_color,
  render_type_pixel_colors,
  render_type_realistic_scr,
  render_type_scr_nearest,
  render_type_scr_nearest_scaled,
  render_type_scr_relax,
  render_type_profiled_original,
  render_type_interpolated_profiled_original,
  render_type_interpolated_diff,
  render_type_max
};

class render_type_property
{
public:
  const char *name;
  const char *pretty_name;
  int flags;
  enum flag
  {
    NEEDS_SCR_TO_IMG = 1,
    NEEDS_RGB = 2,
    USES_SCR_DETECT = 4,
    NEEDS_SCR_DETECT = USES_SCR_DETECT | NEEDS_RGB,
    /* Screen-colour detection always requires RGB input.  */
    NEEDS_CORRECTION_PROFILE = 8,
    OUTPUTS_SCAN_PROFILE = 16,
    OUTPUTS_PROCESS_PROFILE = 32,
    OUTPUTS_SRGB_PROFILE = 64,
    SUPPORTS_IR_RGB_SWITCH = 128,
    SCAN_RESOLUTION = 256,
    SCREEN_RESOLUTION = 512,
    PATCH_RESOLUTION = 1024,
    RESET_BRIGHTNESS_ETC = 2048,
    ANTIALIAS = 4096,
    HIDE_IN_GUI = 8192
  };
  const char *help;
};
DLL_PUBLIC extern const render_type_property render_type_properties[render_type_max];

/* Settings of one rendering request, never parameters of the source image.
   An onscreen view or export chooses its own output transform and tile
   visibility. The operation-local encoded transfer is independent of the
   document's colour/tone calibration. Do not serialize this as CSP state. */
struct render_output_parameters
{
  enum output_profile_t
  {
    output_profile_sRGB,
    output_profile_xyz,
    output_profile_original,
    output_profile_max
  };

  inline static constexpr const char *output_profile_names[(int)output_profile_max] = {
      "sRGB", "XYZ", "original"
  };

  output_profile_t output_profile = output_profile_sRGB;
  /* -1 selects the sRGB transfer function; 1 selects linear output. */
  luminosity_t output_gamma = -1;
  bool gamut_warning = false;

  /* Optional view/export-local visibility of stitch tiles.
     An absent/mismatching table means all tiles are visible. The actual
     image-load readiness is checked separately by stitch_project. */
  int tile_columns = 0, tile_rows = 0;
  std::vector<uint8_t> tile_enabled;

  bool
  tile_enabled_p (int x, int y, int width, int height) const
  {
    if (x < 0 || y < 0 || x >= width || y >= height)
      return false;
    return tile_columns != width || tile_rows != height
           || tile_enabled.size () != (size_t)width * height
           || tile_enabled[y * (size_t)width + x] != 0;
  }

  void
  set_tile_enabled (int width, int height, int x, int y, bool enabled)
  {
    if (width <= 0 || height <= 0 || width > 256 || height > 256
        || x < 0 || y < 0 || x >= width || y >= height)
      return;
    if (tile_columns != width || tile_rows != height
        || tile_enabled.size () != (size_t)width * height)
      {
        tile_columns = width;
        tile_rows = height;
        tile_enabled.assign ((size_t)width * height, 1);
      }
    tile_enabled[y * (size_t)width + x] = enabled ? 1 : 0;
  }
};

class render_type_parameters
{
public:
  enum render_type_t type;
  render_output_parameters output;
  bool color;
  bool antialias;
  render_type_parameters ()
      : type (render_type_original), color (true), antialias (true)
  {
  }
};
}
#endif
