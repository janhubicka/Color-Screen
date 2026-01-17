#ifndef SOLVER_PARAMETERS_H
#define SOLVER_PARAMETERS_H
#include <vector>
#include "dllpublic.h"
#include "base.h"
#include "color.h"
#include "scr-to-img-parameters.h"
namespace colorscreen
{
struct solver_parameters
{
  DLL_PUBLIC_EXP
  solver_parameters ()
      : points (), optimize_lens (true), optimize_tilt (true), weighted (false)
  {
  }
  DLL_PUBLIC_EXP void
  copy_without_points (const solver_parameters &other)
  {
    optimize_lens = other.optimize_lens;
    optimize_tilt = other.optimize_tilt;
    weighted = other.weighted;
  }
  DLL_PUBLIC_EXP ~solver_parameters () {}

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
    point_t img, scr;
    enum point_color color;

    /* Translate point color to RGB.  */
    rgbdata
    get_rgb () const
    {
      static const rgbdata colors[]
          = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
      return colors[(int)color];
    }
  };

  /* Vector holding points.  */
  std::vector<solver_point_t> points;
  /* If true, lens parameters are auto-optimized.  */
  bool optimize_lens;
  /* If true, image tilt is auto-optimized.  */
  bool optimize_tilt;
  /* If true then weights of points are set according to the
     distance to center.x and center.y.   */
  bool weighted;
  point_t center;

  int
  n_points ()
  {
    return points.size ();
  }

  DLL_PUBLIC_EXP int
  find_img (point_t img)
  {
    for (int n = 0; n < n_points (); n++)
      if (points[n].img.almost_eq (img, 0.1))
        return n;
    return -1;
  }

  DLL_PUBLIC_EXP int
  add_point (point_t img, point_t screen, enum point_color color)
  {
    points.push_back ({ img, screen, color });
    return n_points ();
  }

  DLL_PUBLIC_EXP void
  remove_point (int n)
  {
    /* Just for fun keep the order as points were added.  */
    points.erase (points.begin () + n);
  }
  DLL_PUBLIC_EXP void
  remove_points ()
  {
    points.clear ();
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
  DLL_PUBLIC void dump (FILE *out);
  /* Names of colors in enum point_color.  */
  DLL_PUBLIC static const char *const point_color_names[(int)max_point_color];
};
}
#endif
