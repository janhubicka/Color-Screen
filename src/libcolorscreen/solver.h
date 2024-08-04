#ifndef SOLVER_H
#define SOLVER_H
#include "include/scr-to-img.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/scr-detect-parameters.h"
#include "include/imagedata.h"
#include "include/detect-regular-screen-parameters.h"
#include "include/solver-parameters.h"

namespace colorscreen
{
class bitmap_2d;
coord_t simple_solver (scr_to_img_parameters *param, image_data &img_data,
                       solver_parameters &sparam,
                       progress_info *progress = NULL);
void optimize_screen_colors (scr_detect_parameters *param, color_t *reds,
                             int nreds, color_t *greens, int ngreens,
                             color_t *blues, int nblues,
                             progress_info *progress = NULL,
                             FILE *report = NULL);
void optimize_screen_colors (scr_detect_parameters *param, scr_type type,
                             image_data *img, mesh *m, int xshift, int yshift,
                             bitmap_2d *known_patches, luminosity_t gamma,
                             progress_info *progress = NULL,
                             FILE *report = NULL);
bool optimize_screen_colors (scr_detect_parameters *param, image_data *img,
                             luminosity_t gamma, int x, int y, int width,
                             int height, progress_info *progress = NULL,
                             FILE *report = NULL);
}
#endif
