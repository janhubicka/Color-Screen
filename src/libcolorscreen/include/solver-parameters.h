#ifndef SOLVER_PARAMETERS_H
#define SOLVER_PARAMETERS_H
#include "dllpublic.h"
#include "base.h"
#include "color.h"
#include "scr-to-img-parameters.h"
struct solver_parameters
{
  DLL_PUBLIC_EXP
  solver_parameters ()
      : npoints (0), point (NULL), optimize_lens (true), optimize_tilt (true),
        weighted (false)
  {
  }
  DLL_PUBLIC_EXP ~solver_parameters () { free (point); }

  /* Solver takes as input set of points.  Every point records
      - image coordinates (img_x, img_y)
      - screen coordinats (scr_x, scr_y)
      - color (only used for visualization.  */

  enum point_color
  {
    red,
    green,
    blue,
    max_point_color
  };
  struct solver_point_t
  {
    coord_t img_x, img_y;
    coord_t screen_x, screen_y;
    enum point_color color;

    /* Translate point color to RGB.  */
    void
    get_rgb (luminosity_t *r, luminosity_t *g, luminosity_t *b)
    {
      const luminosity_t colors[][3]
          = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
      *r = colors[(int)color][0];
      *g = colors[(int)color][1];
      *b = colors[(int)color][2];
    }
  };

  /* Vector holding points.  */
  int npoints;
  struct solver_point_t *point;
  /* If true, lens parameters are auto-optimized.  */
  bool optimize_lens;
  /* If true, image tilt is auto-optimized.  */
  bool optimize_tilt;
  /* If true then weights of points are set according to the
     distance to center.x and center.y.   */
  bool weighted;
  point_t center;

  DLL_PUBLIC_EXP int
  find_img (point_t img)
  {
    for (int n = 0; n < npoints; n++)
      {
	if (fabs (point[n].img_x - img.x) < 0.1 && fabs (point[n].img_y - img.y) < 0.1)
          return n;
      }
    return -1;
  }

  DLL_PUBLIC_EXP int
  add_point (coord_t img_x, coord_t img_y, coord_t screen_x, coord_t screen_y,
             enum point_color color)
  {
    npoints++;
    point = (solver_point_t *)realloc ((void *)point, npoints * sizeof (solver_point_t));
    point[npoints - 1].img_x = img_x;
    point[npoints - 1].img_y = img_y;
    point[npoints - 1].screen_x = screen_x;
    point[npoints - 1].screen_y = screen_y;
    point[npoints - 1].color = color;
    return npoints;
  }
  DLL_PUBLIC_EXP int
  add_point (point_t img, point_t screen, enum point_color color)
  {
    return add_point (img.x, img.y, screen.x, screen.y, color);
  }

  DLL_PUBLIC_EXP void
  remove_point (int n)
  {
    /* Just for fun keep the order as points were added.  */
    for (; n < npoints - 1; n++)
      point[n] = point[n + 1];
#if 0
    if (n < 0 || n >= npoints)
      abort ();
    point[n] = point[npoints - 1];
#endif
    npoints--;
  }
  DLL_PUBLIC_EXP void
  remove_points ()
  {
    npoints = 0;
  }
  DLL_PUBLIC_EXP void
  dump (FILE *out)
  {
    for (int i = 0; i < npoints; i++)
      {
        fprintf (out, "point %i img %f %f maps to scr %f %f color %i\n", i,
                 point[i].img_x, point[i].img_y, point[i].screen_x,
                 point[i].screen_y, (int)point[i].color);
      }
  }
  /* get_point_location returns description of invividual color patches in
     single period of the screen.  */
  struct point_location
  {
    coord_t x, y;
    solver_parameters::point_color color;
  };
  DLL_PUBLIC static point_location *get_point_locations (enum scr_type type,
                                                         int *n);
  /* Names of colors in enum point_color.  */
  static const char *point_color_names[(int)max_point_color];
};
#endif
