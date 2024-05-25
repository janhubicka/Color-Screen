#ifndef FINETUNE_H
#define FINETUNE_H
#include "solver.h"
enum finetune_flags
{
  finetune_position = 1,
  finetune_screen_blur = 2,
  finetune_dufay_strips = 4,
  finetune_fog = 8,
  finetune_bw = 16,
  finetune_no_least_squares = 32,
  finetune_no_progress_report = 64,
  finetune_no_normalize = 128,
  finetune_verbose = 256
};
struct finetune_parameters
{
  int flags;
  int range;
  coord_t ignore_outliers;
  const char *simulated_file;
  const char *orig_file;
  const char *diff_file;
  finetune_parameters ()
  : flags (0), range (0), ignore_outliers (0.1), simulated_file (NULL), orig_file (NULL), diff_file (NULL)
  { }
};
struct finetune_result
{
  bool success;
  coord_t badness;
  coord_t screen_blur_radius;
  coord_t dufay_red_strip_width;
  coord_t dufay_green_strip_width;
  point_t screen_coord_adjust;
  rgbdata color;
  rgbdata screen_red, screen_green, screen_blue;
  rgbdata fog;
};
DLL_PUBLIC finetune_result finetune (render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, int x, int y, const finetune_parameters &fparams, progress_info *progress);
#endif
