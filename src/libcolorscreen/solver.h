#ifndef SOLVER_H
#define SOLVER_H
#include "include/scr-to-img.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/scr-detect-parameters.h"
#include "include/imagedata.h"
#include "include/detect-regular-screen-parameters.h"
#include "include/solver-parameters.h"

class bitmap_2d;

class homography
{
public:
  enum solver_vars
  {
    solve_rotation = 1,
    solve_free_rotation = 2,
    solve_screen_weights = 4,
    solve_image_weights = 8,
    solve_limit_ransac_iterations = 16
  };
  static trans_4d_matrix get_matrix_4points (bool invert, scanner_type type, point_t zero, point_t x, point_t y, point_t xpy);
  static trans_4d_matrix get_matrix_5points (bool invert, scanner_type type, point_t zero, point_t x, point_t y, point_t xpy, point_t txpy);
  static trans_4d_matrix get_matrix_ransac (solver_parameters::solver_point_t *points, int n, int flags,
					    scanner_type type,
					    scr_to_img *map,
					    coord_t wcenter_x, coord_t wcenter_y,
					    coord_t *chisq_ret = NULL, bool final = false);
  static trans_4d_matrix get_matrix (solver_parameters::solver_point_t *points, int n, int flags,
				     scanner_type type,
				     scr_to_img *map,
				     point_t wcenter,
				     coord_t *chisq_ret = NULL);
};
coord_t simple_solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress = NULL);
void optimize_screen_colors (scr_detect_parameters *param, color_t *reds, int nreds, color_t *greens, int ngreens, color_t *blues, int nblues, progress_info *progress = NULL, FILE *report = NULL);
void optimize_screen_colors (scr_detect_parameters *param, scr_type type, image_data *img, mesh *m, int xshift, int yshift, bitmap_2d *known_patches, luminosity_t gamma, progress_info *progress = NULL, FILE *report = NULL);
bool optimize_screen_colors (scr_detect_parameters *param, image_data *img, luminosity_t gamma, int x, int y, int width, int height, progress_info *progress = NULL, FILE *report = NULL);


#endif
