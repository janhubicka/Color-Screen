#ifndef FINETUNE_H
#define FINETUNE_H
#include "solver.h"
enum finetune_flags
{
  finetune_position = 1,
  finetune_screen_blur = 2,
  finetune_dufay_strips = 4,
  finetune_bw = 8,
  finetune_no_least_squares = 16,
  finetune_no_progress_report = 32,
  finetune_no_normalize = 64,
  finetune_verbose = 128
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
};
DLL_PUBLIC finetune_result finetune (render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, int x, int y, int flags, progress_info *progress);
#endif
