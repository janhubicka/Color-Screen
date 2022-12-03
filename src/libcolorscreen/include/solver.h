#ifndef SOLVER_H
#define SOLVER_H
#include "scr-to-img.h"
struct solver_point
{
  coord_t img_x, img_y;
  coord_t screen_x, screen_y;
};
coord_t solver (scr_to_img_parameters *param, image_data &img_data, int n, solver_point *points);
#endif
