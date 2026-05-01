/* Geometry and lens warp solvers.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef SOLVER_H
#define SOLVER_H
#include "include/scr-to-img.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/scr-detect-parameters.h"
#include "include/imagedata.h"
#include "include/solver-parameters.h"

namespace colorscreen
{
class bitmap_2d;

/* Determine geometry using linear regression.
   PARAM is updated with results.
   IMG_DATA is the source image.
   SPARAM contains solver points.
   PROGRESS is used for progress reporting.  */
nodiscard_attr coord_t simple_solver (scr_to_img_parameters *param, const image_data &img_data,
                       const solver_parameters &sparam,
                       progress_info *progress = nullptr);

/* Optimize screen colors using detected color patches.
   PARAM determines the screen type and detection parameters.
   REDS, GREENS, BLUES are arrays of detected color patches.
   NREDS, NGREENS, NBLUES are number of patches in these arrays.
   PROGRESS is used for progress reporting.
   REPORT is file where report is written.  */
void optimize_screen_colors (scr_detect_parameters *param, color_t *reds,
                             int nreds, color_t *greens, int ngreens,
                             color_t *blues, int nblues,
                             progress_info *progress = nullptr,
                             FILE *report = nullptr);

/* Optimize screen colors using detected color patches in image IMG.
   PARAM determines the screen type and detection parameters.
   TYPE is the screen type.
   M is the mesh transformation.
   SHIFT is the shift of the screen coordinates.
   KNOWN_PATCHES is a bitmap of known color patches.
   GAMMA is the gamma of the image.
   PROGRESS is used for progress reporting.
   REPORT is file where report is written.  */
void optimize_screen_colors (scr_detect_parameters *param, scr_type type,
                             const image_data *img, const mesh *m, int_point_t shift,
                             const bitmap_2d *known_patches, luminosity_t gamma,
                             progress_info *progress = nullptr,
                             FILE *report = nullptr);

/* Optimize screen colors using detected color patches in image IMG.
   PARAM determines the screen type and detection parameters.
   IMG is the source image.
   GAMMA is the gamma of the image.
   AREA is the area to optimize.
   PROGRESS is used for progress reporting.
   REPORT is file where report is written.  */
nodiscard_attr bool optimize_screen_colors (scr_detect_parameters *param, const image_data *img,
                             luminosity_t gamma, int_image_area area,
                             progress_info *progress = nullptr,
                             FILE *report = nullptr);
}
#endif
