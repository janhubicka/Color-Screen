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
DLL_PUBLIC bool finetune (render_parameters *rparam, solver_parameters::point_t *point, coord_t *badness, const scr_to_img_parameters &param, const image_data &img, int x, int y, int flags, progress_info *progress);
#endif
