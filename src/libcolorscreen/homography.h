#ifndef HOMOGRAPHY_H
#define HOMOGRAPHY_H
#include "include/scr-to-img.h"

namespace colorscreen
{
namespace homography
{
enum solver_vars
{
  solve_rotation = 1,
  solve_free_rotation = 2,
  solve_screen_weights = 4,
  solve_image_weights = 8,
  solve_limit_ransac_iterations = 16
};
trans_4d_matrix get_matrix_5points (bool invert, scanner_type type,
                                    point_t zero, point_t x, point_t y,
                                    point_t xpy, point_t txpy);
trans_4d_matrix get_matrix_ransac (solver_parameters::solver_point_t *points,
                                   int n, int flags, scanner_type type,
                                   scr_to_img *map, coord_t wcenter_x,
                                   coord_t wcenter_y,
                                   coord_t *chisq_ret = NULL,
                                   bool final = false);
trans_4d_matrix get_matrix (solver_parameters::solver_point_t *points, int n,
                            int flags, scanner_type type, scr_to_img *map,
                            point_t wcenter, coord_t *chisq_ret = NULL);
};
}
#endif
