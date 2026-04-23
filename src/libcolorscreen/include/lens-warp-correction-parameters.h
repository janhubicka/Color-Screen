#ifndef LENS_WARP_CORRECTION_PARAMETERS_H
#define LENS_WARP_CORRECTION_PARAMETERS_H
/* Parameters for radial lens distortion correction.
   Follows the DNG WarpRectilinear polynomial model.  */
#include "base.h"
namespace colorscreen
{
/* Radial lens warp correction parameters.  Follows DNG WarpRectilinear model.
   Correction is defined as:
   r_src = (kr0 + kr1 * r^2 + kr2 * r^4 + kr3 * r^6) * r_dst
   where r is distance from lens center.  */
struct lens_warp_correction_parameters
{
  /* Radial correction coefficients, same as in the DNG specs.  */
  coord_t kr[4];
  /* Center in relative coordinates 0...1  */
  point_t center;

  /* Initialize with identity correction and center at (0.5, 0.5).  */
  constexpr
  lens_warp_correction_parameters ()
      : kr{ 1, 0, 0, 0 }, center ({ 0.5, 0.5 })
  {
  }

  /* Return true if parameters are equal to OTHER.  */
  pure_attr bool
  operator== (const lens_warp_correction_parameters &other) const
  {
    return center == other.center && kr[0] == other.kr[0]
           && kr[1] == other.kr[1] && kr[2] == other.kr[2]
           && kr[3] == other.kr[3];
  }

  /* Return true if lens need no correcting.  */
  pure_attr bool
  is_noop () const
  {
    return kr[0] == 1 && kr[1] == 0 && kr[2] == 0 && kr[3] == 0;
  }

  /* Compute the radial correction ratio for given radius squared RSQ.
     The ratio is: kr0 + (kr1 * RSQ) + (kr2 * RSQ^2) + (kr3 * RSQ^3).  */
  pure_attr coord_t
  get_ratio (coord_t rsq) const
  {
    if (rsq > 1)
      rsq = 1;
    return kr[0] + rsq * (kr[1] + rsq * (kr[2] + rsq * kr[3]));
  }

  /* Verify that the correction is monotone on a interval [0, 1].
     Monotonicity is required for the inverse mapping to be well-defined.  */
  pure_attr bool
  is_monotone () const
  {
    /* The source radius is r_src = r * (k0 + k1*r^2 + k2*r^4 + k3*r^6).
       For monotonicity we need d(r_src)/dr > 0 for r in [0, 1].
       d(r_src)/dr = k0 + 3*k1*r^2 + 5*k2*r^4 + 7*k3*r^6.
       Let x = r^2. We need f(x) = k0 + 3*k1*x + 5*k2*x^2 + 7*k3*x^3 > 0
       for x in [0, 1].  */
    auto f = [this] (coord_t x) {
      return kr[0] + x * (3 * kr[1] + x * (5 * kr[2] + x * 7 * kr[3]));
    };

    /* Check boundaries.  */
    if (f (0) <= 0 || f (1) <= 0)
      return false;

    /* Check critical points of f(x) where f'(x) = 0.
       f'(x) = 3*k1 + 10*k2*x + 21*k3*x^2.  */
    coord_t a = 21 * kr[3];
    coord_t b = 10 * kr[2];
    coord_t c = 3 * kr[1];

    if (my_fabs (a) < 1e-12)
      {
	/* Linear or constant derivative.  */
	if (my_fabs (b) > 1e-12)
	  {
	    coord_t x = -c / b;
	    if (x > 0 && x < 1 && f (x) <= 0)
	      return false;
	  }
      }
    else
      {
	/* Quadratic derivative.  */
	coord_t disc = b * b - 4 * a * c;
	if (disc >= 0)
	  {
	    coord_t sq = my_sqrt (disc);
	    coord_t x1 = (-b + sq) / (2 * a);
	    coord_t x2 = (-b - sq) / (2 * a);
	    if (x1 > 0 && x1 < 1 && f (x1) <= 0)
	      return false;
	    if (x2 > 0 && x2 < 1 && f (x2) <= 0)
	      return false;
	  }
      }
    return true;
  }

  /* Adjust parameters so get_ratio (1) == 1.  This is used to normalize
     the correction so that it is defined relative to the maximum distance.
     Returns false if the normalization fails (e.g., degenerate coefficients).  */
  bool
  normalize ()
  {
    coord_t c = 1 / get_ratio (1);
    kr[0] *= c;
    kr[1] *= c;
    kr[2] *= c;
    kr[3] *= c;
    if (my_fabs (1 - get_ratio (1)) > 0.00001)
      return false;
    return true;
  }
};
}
#endif
