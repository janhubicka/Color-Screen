#ifndef RENDER_TYPE_PARAMETERS_H
#define RENDER_TYPE_PARAMETERS_H
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
  };
};

namespace {
static const constexpr render_type_property render_type_properties[render_type_max] =
{
   {"original", render_type_property::OUTPUTS_SCAN_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::SCAN_RESOLUTION},
   {"interpolated-original", render_type_property::OUTPUTS_SCAN_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"profiled-original", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_RGB | render_type_property::SCAN_RESOLUTION},
   {"interpolated-profiled-original", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"interpolated-diff", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"preview-grid", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"realistic", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"interpolated", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::PATCH_RESOLUTION},
   {"interpolated-predictive", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"interpolated-combined", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"fast", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCREEN_RESOLUTION},
   {"extra", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::PATCH_RESOLUTION},
   {"detected-adjusted-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
   {"detected-normalized-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION | render_type_property::RESET_BRIGHTNESS_ETC},
   {"detected-screen-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION | render_type_property::RESET_BRIGHTNESS_ETC},
   {"detected-realistic", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
   {"detected-interpolated", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
   {"detected-interpolated-scaled", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
   {"detected-relaxation-scaled", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
};
}


//static const enum render_type_t first_scr_detect = render_type_adjusted_color;
class render_type_parameters
{
public:
  enum render_type_t type;
  bool color;
  bool antialias;
  render_type_parameters ()
  : type (render_type_original), color (true), antialias (true)
  { }
};

#endif
