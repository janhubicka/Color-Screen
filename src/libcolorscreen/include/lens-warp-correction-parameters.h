#ifndef LENS_WARP_CORRECTION_PARAMETERS_H
#define LENS_WARP_CORRECTION_PARAMETERS_H
/* Parameters for radial lens distortion correction.
   Implements the radial, single-coefficient-set subset of the DNG
   WarpRectilinear polynomial model.  Color-Screen currently has no
   tangential (kt0/kt1) terms or per-plane coefficient sets.  */
#include "base.h"
namespace colorscreen
{
/* Radial lens warp correction parameters.

   In DNG terminology this maps a point in the warped/output image to the
   source sample position.  With tangential terms fixed to zero,

     r_src = r_dst * (kr0 + kr1*r^2 + kr2*r^4 + kr3*r^6),

   where r is normalized by the distance from the optical center to the
   farthest output-image pixel.  DNG specifies the polynomial for 0 <= r <= 1.
   Color-Screen keeps the edge ratio constant when evaluating outside that
   interval so its numerical inverse has a defined extrapolation rule.  */
struct lens_warp_correction_parameters
{
  /* DNG WarpRectilinear radial coefficients kr0 ... kr3.  */
  coord_t kr[4];
  /* Optical center in normalized DNG coordinates.  For interoperability we
     follow Adobe DNG SDK dng_filter_warp: (0,0) is the top-left image bound
     and (1,1) is the exclusive bottom-right image bound.  Color-Screen may
     also use centers outside [0,1] when solving scanner-bed captures.  */
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

  /* Return derivative of source radius with respect to output radius for
     RSQ=r^2 in the DNG domain [0,1].  */
  pure_attr coord_t
  get_radial_derivative (coord_t rsq) const
  {
    return kr[0]
           + rsq * (3 * kr[1] + rsq * (5 * kr[2] + rsq * 7 * kr[3]));
  }

  /* Verify that the correction is monotone on a interval [0, 1].
     This is the DNG invertibility condition for the radial-only subset.  */
  pure_attr bool
  is_monotone () const
  {
    for (coord_t k : kr)
      if (!my_isfinite (k))
        return false;

    /* The source radius is r_src = r * (k0 + k1*r^2 + k2*r^4 + k3*r^6).
       For monotonicity we need d(r_src)/dr > 0 for r in [0, 1].
       d(r_src)/dr = k0 + 3*k1*r^2 + 5*k2*r^4 + 7*k3*r^6.
       Let x = r^2. We need f(x) = k0 + 3*k1*x + 5*k2*x^2 + 7*k3*x^3 > 0
       for x in [0, 1].  */
    auto f = [this] (coord_t x) { return get_radial_derivative (x); };

    /* Check boundaries.  */
    if (!(f (0) > 0) || !(f (1) > 0))
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

  /* Remove the overall radial scale by making get_ratio(1) == 1.

     This is a Color-Screen solver gauge, not a DNG requirement: an overall
     scale is otherwise almost completely degenerate with the fitted linear
     screen-to-image transform.  Imported DNG coefficients need not be
     normalized this way.  */
  bool
  normalize ()
  {
    const coord_t edge_ratio = get_ratio (1);
    if (!(edge_ratio > 0) || !my_isfinite (edge_ratio))
      return false;
    const coord_t c = 1 / edge_ratio;
    kr[0] *= c;
    kr[1] *= c;
    kr[2] *= c;
    kr[3] *= c;
    return my_isfinite (kr[0]) && my_isfinite (kr[1])
           && my_isfinite (kr[2]) && my_isfinite (kr[3])
           && my_fabs (1 - get_ratio (1)) <= 0.00001;
  }
};
}
#endif
