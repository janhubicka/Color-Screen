#ifndef FINETUNE_H
#define FINETUNE_H
#include "base.h"
#include "color.h"
#include "colorscreen.h"
#include <memory>
#include <string>
namespace colorscreen
{
class screen;
struct render_parameters;
struct scr_to_img_parameters;
struct scr_detect_parameters;
class image_data;
enum finetune_flags : uint64_t
{
  finetune_position = 1 << 0,
  finetune_screen_blur = 1 << 1,
  finetune_screen_channel_blurs = 1 << 2,
  finetune_scanner_mtf_sigma = 1 << 3,
  finetune_scanner_mtf_defocus = 1 << 4,
  finetune_strips = 1 << 5,
  finetune_fog = 1 << 6,
  finetune_bw = 1 << 7,
  finetune_no_data_collection = 1 << 8,
  finetune_no_least_squares = 1 << 9,
  finetune_no_progress_report = 1 << 10,
  finetune_no_normalize = 1 << 11,
  finetune_emulsion_blur = 1 << 12,
  finetune_verbose = 1 << 13,
  finetune_use_strip_widths = 1 << 14,
  finetune_use_screen_blur = 1 << 15,
  finetune_simulate_infrared = 1 << 16,
  finetune_sharpening = 1 << 17,
  finetune_scanner_mtf_channel_defocus = 1 << 18,
  finetune_coordinates = 1 << 19,
  finetune_produce_images = 1 << 20
};
struct finetune_parameters
{
  uint64_t flags = 0;
  int range = 0;
  int multitile = 1;
  coord_t ignore_outliers = 0.1;
  const char *simulated_file = nullptr;
  const char *orig_file = nullptr;
  const char *sharpened_file = nullptr;
  const char *diff_file = nullptr;
  const char *screen_file = nullptr;
  const char *screen_blur_file = nullptr;
  const char *emulsion_file = nullptr;
  const char *merged_file = nullptr;
  const char *collected_file = nullptr;
  const char *dot_spread_file = nullptr;
  finetune_parameters () {}
};
struct finetune_result
{
  bool success = false;
  point_t tile_pos = { -1, -1 };
  coord_t badness = 12345;
  coord_t uncertainty = 12345;
  coord_t screen_blur_radius = -1;
  rgbdata screen_channel_blur_radius = { -1, -1, -1 };
  luminosity_t scanner_mtf_sigma = -1;
  luminosity_t scanner_mtf_blur_diameter = -1;
  luminosity_t scanner_mtf_defocus = -1;
  rgbdata scanner_mtf_channel_defocus_or_blur = { -1, -1, -1 };
  coord_t emulsion_blur_radius = -1;
  coord_t red_strip_width = -1;
  coord_t green_strip_width = -1;
  point_t screen_coord_adjust = { -1, -1 };
  point_t emulsion_coord_adjust = { -1, -1 };
  rgbdata color = { -1, -1, -1 };
  rgbdata screen_red = { -1, -1, -1 }, screen_green = { -1, -1, -1 },
          screen_blue = { -1, -1, -1 };
  rgbdata fog = { 0, 0, 0 };
  rgbdata mix_weights = { -1, -1, -1 };
  rgbdata mix_dark = { -1, -1, -1 };
  std::string err;

  /* Solver point data.  */
  point_t solver_point_img_location = { -1, -1 };
  point_t solver_point_screen_location = { -1, -1 };
  enum solver_parameters::point_color solver_point_color
      = solver_parameters::max_point_color;

  point_t center = { 0, 0 }, coordinate1 = { 0, 0 }, coordinate2 = { 0, 0 };

  /* Solver images  */
  std::shared_ptr<simple_image> diff;
  std::shared_ptr<simple_image> simulated;
  std::shared_ptr<simple_image> sharpened;
  std::shared_ptr<simple_image> orig;
  std::shared_ptr<simple_image> screen;
  std::shared_ptr<simple_image> blurred_screen;
  std::shared_ptr<simple_image> emulsion_screen;
  std::shared_ptr<simple_image> merged_screen;
  std::shared_ptr<simple_image> collected_screen;
  std::shared_ptr<simple_image> dot_spread;

  finetune_result () {}

  // finetune_result(finetune_result&&) = default;
  // finetune_result& operator=(finetune_result&&) = default;
};

DLL_PUBLIC finetune_result
finetune (render_parameters &rparam, const scr_to_img_parameters &param,
          const image_data &img, const std::vector<point_t> &locs,
          const std::vector<finetune_result> *results,
          const finetune_parameters &fparams, progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
finetune_area (solver_parameters *sparam, render_parameters &rparam,
               const scr_to_img_parameters &param, const image_data &img,
               int_image_area area, progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
finetune_misregistered_area (solver_parameters *solver, render_parameters &rparam,
			     const scr_to_img_parameters &param, const image_data &img,
			     int_image_area area, progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
render_screen (image_data &img, const scr_to_img_parameters &param,
               const render_parameters &rparam, const scr_detect_parameters &dparam,
	       int width, int height);
} // namespace colorscreen
#endif
