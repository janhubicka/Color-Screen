#ifndef SOLVER_H
#define SOLVER_H
#include "scr-to-img.h"
struct solver_parameters
{
  solver_parameters ()
  : npoints (0), point (NULL)
  {
  }
  ~solver_parameters ()
  {
    free (point);
  }
  enum point_color {red,green,blue,max_point_color};
  static const char *point_color_names[(int)max_point_color];
  struct point_t
  {
    coord_t img_x, img_y;
    coord_t screen_x, screen_y;
    enum point_color color;
  };
  int npoints;
  struct point_t *point;
  int
  add_point (coord_t img_x, coord_t img_y, coord_t screen_x, coord_t screen_y, enum point_color color)
  {
    npoints++;
    point = (point_t *)realloc ((void *)point, npoints * sizeof (point_t));
    point[npoints - 1].img_x = img_x;
    point[npoints - 1].img_y = img_y;
    point[npoints - 1].screen_x = screen_x;
    point[npoints - 1].screen_y = screen_y;
    point[npoints - 1].color = color;
    return npoints;
  }
  void
  remove_point (int n)
  {
    if (n < 0 || n >= npoints)
      abort ();
    point[n] = point[npoints - 1];
    npoints--;
  }
  void
  dump (FILE *out)
  {
    for (int i =0; i < npoints; i++)
      {
	fprintf (out, "point %i img %f %f maps to scr %f %f\n", i, point[i].img_x, point[i].img_y, point[i].screen_x, point[i].screen_y);
      }
  }
};
coord_t solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam);
#endif
