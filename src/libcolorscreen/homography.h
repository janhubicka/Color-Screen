#ifndef HOMOGRAPHY_H
#define HOMOGRAPHY_H
#include <vector>
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
  solve_limit_ransac_iterations = 16,
  solve_vertical_strips = 32
};
trans_4d_matrix get_matrix_5points (bool invert, scanner_type type,
                                    point_t zero, point_t x, point_t y,
                                    point_t xpy, point_t txpy);
trans_4d_matrix get_matrix_ransac (std::vector <solver_parameters::solver_point_t> &points,
                                   int flags, scanner_type type,
                                   scr_to_img *map, point_t wcenter,
                                   coord_t *chisq_ret = NULL,
                                   bool final_run = false);
trans_4d_matrix get_matrix (std::vector <solver_parameters::solver_point_t> &points,
                            int flags, scanner_type type, scr_to_img *map,
                            point_t wcenter, coord_t *chisq_ret = NULL);
};
}
#endif
