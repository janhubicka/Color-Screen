#ifndef COLORSCREEN_H
#define COLORSCREEN_H
#include "render-fast.h"
#include "render-scr-detect.h"

/* Supported output modes.  */
enum output_mode
{
  none,
  realistic,
  preview_grid,
  color_preview_grid,
  interpolated,
  predictive,
  combined,
  detect_adjusted,
  detect_realistic,
  detect_nearest,
  detect_nearest_scaled,
  detect_relax,
};
DLL_PUBLIC bool render_to_file(enum output_mode mode, const char *outfname, image_data &scan, scr_to_img_parameters &param,
			       scr_detect_parameters &dparam, render_parameters &rparam, progress_info *progress, bool verbose, const char **error);
#endif
