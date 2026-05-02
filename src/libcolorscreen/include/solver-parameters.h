#ifndef SOLVER_PARAMETERS_H
#define SOLVER_PARAMETERS_H
#include <vector>
#include "cow-vector.h"

#include "dllpublic.h"
#include "base.h"
#include "color.h"
#include "scr-to-img-parameters.h"
#include "matrix.h"
namespace colorscreen
{
struct solver_parameters
{
  DLL_PUBLIC_EXP
  solver_parameters ()
      : points (), optimize_lens (true), optimize_tilt (true), weighted (false), center ({0, 0})
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
    bool operator== (const solver_point_t &other) const
    {
      return img == other.img && scr == other.scr && color == other.color;
    }
    bool operator!= (const solver_point_t &other) const
    {
      return !(*this == other);
    }

    /* Translate point color to RGB.  */
    rgbdata
    get_rgb () const
    {
      static const rgbdata colors[]
          = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
      return colors[(int)color];
    }
  };

  /* Minimal number of points for nonlinear solver to work.  */
  static int min_mesh_points (enum scr_type type)
  {
    return screen_with_vertical_strips_p (type) ? 20 : 10;
  }
  static int min_lens_points (enum scr_type type)
  {
    return screen_with_vertical_strips_p (type) ? 200 : 100;
  }
  static int min_perspective_points (enum scr_type type)
  {
    return screen_with_vertical_strips_p (type) ? 20 : 10;
  }
  /* Minimal number of points for geometry solver to work.  */
  static int min_points (enum scr_type type)
  {
    return screen_with_vertical_strips_p (type) ? 4 : 3;
  }

  /* Vector holding points.  */
  cow_vector<solver_point_t> points;
  /* If true, lens parameters are auto-optimized.  */
  bool optimize_lens;
  /* If true, image tilt is auto-optimized.  */
  bool optimize_tilt;
  /* If true then weights of points are set according to the
     distance to center.x and center.y.   */
  bool weighted;
  point_t center;

  size_t
  n_points () const
  {
    return points.size ();
  }

  DLL_PUBLIC_EXP int
  find_img (point_t img) const
  {
    for (size_t n = 0; n < n_points (); n++)
      if (points[n].img.almost_eq (img, 0.1))
        return (int)n;
    return -1;
  }

  DLL_PUBLIC_EXP int
  add_point (point_t img, point_t screen, enum point_color color)
  {
    points.push_back ({ img, screen, color });
    return (int)n_points ();
  }
  DLL_PUBLIC_EXP int
  add_or_modify_point (point_t img, point_t screen, enum point_color color)
  {
    for (size_t n = 0; n < n_points (); n++)
      if (points[n].scr.almost_eq (screen, 0.5))
        {
	  points[n].img = img;
	  points[n].scr = screen;
	  points[n].color = color;
	  return (int)n;
        }
    points.push_back ({ img, screen, color });
    return (int)n_points ();
  }
  DLL_PUBLIC_EXP int
  find_point (point_t screen) const
  {
    for (size_t n = 0; n < n_points (); n++)
      if (points[n].scr.almost_eq (screen, 0.5))
	return n;
    return -1;
  }

  DLL_PUBLIC_EXP void
  remove_point (size_t n)
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
  DLL_PUBLIC void dump (FILE *out) const;
  bool operator== (const solver_parameters &other) const
  {
    return optimize_lens == other.optimize_lens &&
           optimize_tilt == other.optimize_tilt &&
           weighted == other.weighted &&
           center == other.center &&
           points == other.points;
  }
  bool operator!= (const solver_parameters &other) const
  {
    return !(*this == other);
  }
  /* Names of colors in enum point_color.  */
  DLL_PUBLIC static const char *const point_color_names[(int)max_point_color];
  /* Fit points to a line and return distance threshold for 90% of points.  */
  DLL_PUBLIC_EXP double fit_line (point_t &origin, point_t &dir) const;
  /* Return a const reference to the underlying points vector.  */
  const std::vector<solver_point_t>& read_points () const { return points.read (); }
  /* Apply matrix TRANS to screen coordinates of all solver points in-place.  */
  DLL_PUBLIC_EXP void transform_solution (matrix3x3<coord_t> trans);
};
}
#endif
