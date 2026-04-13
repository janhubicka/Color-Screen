#ifndef BSPLINE_H
#define BSPLINE_H
#include <cmath>
#include "include/precomputed-function.h"
namespace colorscreen
{
// Standard Cubic B-Spline Kernel function
inline luminosity_t
bspline_kernel (luminosity_t x)
{
  x = std::abs (x);
  if (x < (luminosity_t)1.0)
    return ((luminosity_t)(2.0 / 3.0)
            + (-(luminosity_t)1.0 + ((luminosity_t)0.5) * x) * x * x);
  else if (x < (luminosity_t)2.0)
    {
      x = 2 - x;
      return ((luminosity_t)(1.0 / 6.0)) * x * x * x;
    }
  return 0.0;
}

struct bspline_initializer
{
  static constexpr int steps = 1025;
  vec_luminosity_t kernels[steps];
  bspline_initializer ()
  {
    for (int i = 0; i < steps; ++i)
      {
        luminosity_t off = i / (luminosity_t)(steps - 1);
        kernels[i] = (vec_luminosity_t){ bspline_kernel (-1 - off),
                                         bspline_kernel (0 - off),
                                         bspline_kernel (1 - off),
                                         bspline_kernel (2 - off) };
      }
  }
};

inline const bspline_initializer precomputed_bspline;

static inline luminosity_t const_attr __attribute__ ((always_inline))
bspline_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2,
                     luminosity_t p3, coord_t x)
{
#if 1
  vec_luminosity_t k = precomputed_bspline.kernels[nearest_int (x * (precomputed_bspline.steps - 1))];
  return p0 * k[0] + p1 * k[1] + p2 * k[2] + p3 * k[3];
#else
  return p0 * bspline_kernel (-1-x)
         + p1 * bspline_kernel (-x)
         + p2 * bspline_kernel (1-x)
         + p3 * bspline_kernel (2-x);
#endif
}
static inline vec_luminosity_t const_attr __attribute__ ((always_inline))
vec_bspline_interpolate (vec_luminosity_t p0, vec_luminosity_t p1,
                         vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
#if 1
  vec_luminosity_t k = precomputed_bspline.kernels[nearest_int (x * (precomputed_bspline.steps - 1))];
  return p0 * k[0] + p1 * k[1] + p2 * k[2] + p3 * k[3];
#else
  return p0 * bspline_kernel (-1-x)
         + p1 * bspline_kernel (-x)
         + p2 * bspline_kernel (1-x)
         + p3 * bspline_kernel (2-x);
#endif
}
inline flatten_attr pure_attr luminosity_t always_inline_attr
do_bspline_interpolate (vec_luminosity_t v1, vec_luminosity_t v2,
                        vec_luminosity_t v3, vec_luminosity_t v4, point_t off)
{
  vec_luminosity_t v = vec_bspline_interpolate (v1, v2, v3, v4, off.y);
  return bspline_interpolate (v[0], v[1], v[2], v[3], off.x);
}
}
#endif
