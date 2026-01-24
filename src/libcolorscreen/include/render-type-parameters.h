#ifndef RENDER_TYPE_PARAMETERS_H
#define RENDER_TYPE_PARAMETERS_H
namespace colorscreen
{
enum render_type_t
{
  render_type_original,
  render_type_interpolated_original,
  render_type_profiled_original,
  render_type_interpolated_profiled_original,
  render_type_interpolated_diff,
  render_type_preview_grid,
  render_type_realistic,
  render_type_interpolated,
  render_type_predictive,
  render_type_combined,
  render_type_fast,
  render_type_extra,
  render_type_adjusted_color,
  render_type_first_scr_detect = render_type_adjusted_color,
  render_type_normalized_color,
  render_type_pixel_colors,
  render_type_realistic_scr,
  render_type_scr_nearest,
  render_type_scr_nearest_scaled,
  render_type_scr_relax,
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
    NEEDS_SCR_DETECT = 6, /* scr detect needs RGB.  */
    OUTPUTS_SCAN_PROFILE = 8,
    OUTPUTS_PROCESS_PROFILE = 16,
    OUTPUTS_SRGB_PROFILE = 32,
    SUPPORTS_IR_RGB_SWITCH = 64,
    SCAN_RESOLUTION = 128,
    SCREEN_RESOLUTION = 256,
    PATCH_RESOLUTION = 512,
    RESET_BRIGHTNESS_ETC = 1024,
    ANTIALIAS = 2048,
  };
};
DLL_PUBLIC extern const render_type_property render_type_properties[render_type_max];


//static const enum render_type_t first_scr_detect = render_type_adjusted_color;
class render_type_parameters
{
public:
  enum render_type_t type;
  bool color;
  bool antialias;
  render_type_parameters ()
      : type (render_type_original), color (true), antialias (true)
  {
  }
};
}
#endif
