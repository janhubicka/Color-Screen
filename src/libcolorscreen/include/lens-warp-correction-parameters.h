#ifndef LENS_WARP_CORRECTION_PARAMETERS_H
#define LENS_WARP_CORRECTION_PARAMETERS_H
#include "base.h"
struct lens_warp_correction_parameters
{
  /* Radial correction coefficients, same as in the DNG specs.  */
  coord_t kr[4];
  /* Center in relative coordinates 0...1  */
  point_t center;

  constexpr
  lens_warp_correction_parameters ()
      : kr{ 1, 0, 0, 0 }, center ({ 0.5, 0.5 })
  {
  }

  bool
  operator== (lens_warp_correction_parameters &other) const
  {
    return center == other.center && kr[0] == other.kr[0]
           && kr[1] == other.kr[1] && kr[2] == other.kr[2]
           && kr[3] == other.kr[3];
  }

  /* Return true if lens need no correcting.  */
  bool
  is_noop ()
  {
    return kr[0] == 1 && kr[1] == 0 && kr[2] == 0 && kr[3] == 0;
  }

  /* Apply the correction: kr0 + (kr1 * r^2) + (kr2 * r^4) + (kr3 * r^6)  */
  coord_t
  get_ratio (coord_t rsq)
  {
    if (rsq > 1)
      rsq = 1;
    return kr[0] + rsq * (kr[1] + rsq * (kr[2] + rsq * kr[3]));
  }

  /* Verify that the correction is  monotone on a interval 0..1.
     This should be true for all sane parameters.  */
  pure_attr bool
  is_monotone ()
  {
    coord_t l = 0;
    for (int i = 1; true; i++)
      {
        if (i > 1024 * 1024)
          return false;
        coord_t p = i * (((coord_t)1) / 1024);
        coord_t nl = p * get_ratio (p);
        if (!(nl > l))
          return false;
        if (nl > 1)
          break;
        l = nl;
      }
    return true;
  }

  /* Adjust parameters so get_ration (1) == 1.  Again this should be true
     for sane parameters.  */
  void
  normalize ()
  {
    coord_t c = 1 / get_ratio (1);
    kr[0] *= c;
    kr[1] *= c;
    kr[2] *= c;
    kr[3] *= c;
    if (fabs (1 - get_ratio (1)) > 0.00001)
      abort ();
  }
};
#endif
