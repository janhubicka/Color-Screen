#ifndef FINETUNE_H
#define FINETUNE_H
#include "base.h"
#include "color.h"
#include <string>
namespace colorscreen
{
class screen;
struct render_parameters;
struct scr_to_img_parameters;
class image_data;
enum finetune_flags : uint64_t
{
  finetune_position = 1 << 0,
  finetune_screen_blur = 1 << 1,
  finetune_screen_channel_blurs = 1 << 2,
  finetune_screen_mtf_blur = 1 << 3,
  finetune_screen_ps_blur = 1 << 4,
  finetune_strips = 1 << 5,
  finetune_fog = 1 << 6,
  finetune_bw = 1 << 7,
  finetune_no_data_collection = 1 << 8,
  finetune_no_least_squares = 1 << 9,
  finetune_no_progress_report = 1 << 10,
  finetune_no_normalize = 1 << 11,
  finetune_emulsion_blur = 1 << 12,
  finetune_verbose = 1 << 13,
  finetune_use_dufay_srip_widths = 1 << 14,
  finetune_use_screen_blur = 1 << 15,
  finetune_simulate_infrared = 1 << 16,
  finetune_sharpening = ((int64_t)1) << 32,
};
struct finetune_parameters
{
  uint64_t flags;
  int range;
  int multitile;
  coord_t ignore_outliers;
  const char *simulated_file;
  const char *orig_file;
  const char *sharpened_file;
  const char *diff_file;
  const char *screen_file;
  const char *screen_blur_file;
  const char *emulsion_file;
  const char *merged_file;
  const char *collected_file;
  const char *dot_spread_file;
  finetune_parameters ()
      : flags (0), range (0), multitile (1), ignore_outliers (0.1),
        simulated_file (NULL), orig_file (NULL), sharpened_file (NULL), diff_file (NULL),
        screen_file (NULL), screen_blur_file (NULL), emulsion_file (NULL),
        merged_file (NULL), collected_file (NULL), dot_spread_file (NULL)
  {
  }
};
struct finetune_result
{
  bool success;
  point_t tile_pos;
  coord_t badness;
  coord_t uncertainity;
  coord_t screen_blur_radius;
  rgbdata screen_channel_blur_radius;
  luminosity_t screen_mtf_blur[4];
  coord_t emulsion_blur_radius;
  coord_t red_strip_width;
  coord_t green_strip_width;
  point_t screen_coord_adjust;
  point_t emulsion_coord_adjust;
  rgbdata color;
  rgbdata screen_red, screen_green, screen_blue;
  rgbdata fog;
  rgbdata mix_weights;
  rgbdata mix_dark;
  std::string err;

  /* Solver point data.  */
  point_t solver_point_img_location;
  point_t solver_point_screen_location;
  enum solver_parameters::point_color solver_point_color;

  finetune_result ()
  : success (false), tile_pos {-1, -1}, badness (12345), uncertainity (12345),
    screen_blur_radius (-1), screen_channel_blur_radius (-1, -1, -1), screen_mtf_blur {-1, -1, -1, -1},
    emulsion_blur_radius (-1), red_strip_width (-1), green_strip_width (-1),
    screen_coord_adjust {-1, -1}, emulsion_coord_adjust {-1, -1}, color (-1, -1, -1),
    screen_red (-1, -1, -1), screen_green (-1, -1, -1), screen_blue (-1, -1, -1),
    mix_weights (-1, -1, -1), mix_dark (-1, -1, -1), err (), solver_point_img_location {-1, -1},
    solver_point_screen_location {-1, -1}, solver_point_color (solver_parameters::max_point_color)
    {
    }
};
DLL_PUBLIC finetune_result
finetune (render_parameters &rparam, const scr_to_img_parameters &param,
          const image_data &img, const std::vector<point_t> &locs,
          const std::vector<finetune_result> *results,
          const finetune_parameters &fparams, progress_info *progress);
DLL_PUBLIC bool finetune_area (solver_parameters *sparam,
                               render_parameters &rparam,
                               const scr_to_img_parameters &param,
                               const image_data &img, int xmin, int ymin,
                               int xmax, int ymax, progress_info *progress);
bool determine_color_loss (rgbdata *ret_red, rgbdata *ret_green,
                           rgbdata *ret_blue, screen &scr, screen &collection_scr,
                           luminosity_t threshold, luminosity_t sharpen_radius, luminosity_t sharpen_amount,
			   scr_to_img &map, int xmin, int ymin, int xmax, int ymax);
DLL_PUBLIC void render_screen (image_data &img, scr_to_img_parameters &param,
                               render_parameters &rparam,
                               scr_detect_parameters &dparam, int width,
                               int height);
}
#endif
