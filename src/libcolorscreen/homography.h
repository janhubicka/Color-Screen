/* Homography solvers.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef HOMOGRAPHY_H
#define HOMOGRAPHY_H
#include <vector>
#include "include/scr-to-img.h"

namespace colorscreen
{
namespace homography
{
/* Options for the homography solver.  */
enum solver_vars
{
  solve_rotation = 1,
  solve_free_rotation = 2,
  solve_screen_weights = 4,
  solve_image_weights = 8,
  solve_limit_ransac_iterations = 16,
  solve_vertical_strips = 32
};

/* Return homography matrix determined from 5 points.
   Applying M.perspective_transform on (0,0) will yield to ZERO.
   Applying M.perspective_transform on (1000,0) will yield to X.
   Applying M.perspective_transform on (0,1000) will yield to Y.
   Applying M.perspective_transform on (1000,1000) will yield to XPY (X+Y).
   Applying M.perspective_transform on (2000,3000) will yield to TXPY (2*X+3*Y).
   If INVERT is true then inverse of this transformation is computed.
   SCANNER_TYPE determines the scanner geometry.  */
trans_4d_matrix get_matrix_5points (bool invert, scanner_type type,
                                    point_t zero, point_t x, point_t y,
                                    point_t xpy, point_t txpy) noexcept;

/* Return homography matrix determined from POINTS using RANSAC method.
   If MAP is non-null apply early corrections (such as lens correction).
   If FLAGS contains solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER.
   If CHISQ_RET is non-NULL initialize it to square of errors.
   If FINAL_RUN is true then this is the final call to RANSAC.  */
trans_4d_matrix get_matrix_ransac (std::vector <solver_parameters::solver_point_t> &points,
                                   int flags, scanner_type type,
                                   scr_to_img *map, point_t wcenter,
                                   coord_t *chisq_ret = nullptr,
                                   bool final_run = false);

/* Return homography matrix determined from POINTS using least squares
   method.  If MAP is non-null apply early corrections (such as lens correction).
   If FLAGS contains solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER.
   If CHISQ_RET is non-NULL initialize it to square of errors.
   If TRANSFORMED is non-NULL it is initialized to the set of source points
   transformed by the resulting homography.  */
trans_4d_matrix get_matrix (std::vector <solver_parameters::solver_point_t> &points,
                            int flags, scanner_type type, scr_to_img *map,
                            point_t wcenter, coord_t *chisq_ret = nullptr,
			    std::vector <point_t> *transformed = nullptr);
};
}
#endif
