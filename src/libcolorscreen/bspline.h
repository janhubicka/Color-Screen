#include <cmath>
#include "include/precomputed-function.h"
namespace colorscreen
{
// Standard Cubic B-Spline Kernel function
inline luminosity_t
bspline_kernel(luminosity_t x) {
    x = std::abs(x);
    if (x < (luminosity_t)1.0) {
        return ((luminosity_t)4.0 - (luminosity_t)6.0 * x * x + (luminosity_t)3.0 * x * x * x) / (luminosity_t)6.0;
    } else if (x < (luminosity_t)2.0) {
        return ((luminosity_t)8.0 - (luminosity_t)12.0 * x + (luminosity_t)6.0 * x * x - x * x * x) / (luminosity_t)6.0;
    }
    return 0.0;
}
static inline luminosity_t const_attr __attribute__ ((always_inline))
bspline_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p0 * bspline_kernel (-1-x)
         + p1 * bspline_kernel (-x)
         + p2 * bspline_kernel (1-x)
         + p3 * bspline_kernel (2-x);

}
static inline vec_luminosity_t const_attr __attribute__ ((always_inline))
vec_bspline_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p0 * bspline_kernel (-1-x)
         + p1 * bspline_kernel (-x)
         + p2 * bspline_kernel (1-x)
         + p3 * bspline_kernel (2-x);
}
inline flatten_attr pure_attr luminosity_t always_inline_attr
do_bspline_interpolate (vec_luminosity_t v1, vec_luminosity_t v2, vec_luminosity_t v3, vec_luminosity_t v4, point_t off)
{
  vec_luminosity_t v = vec_bspline_interpolate (v1, v2, v3, v4, off.y);
  return bspline_interpolate (v[0], v[1], v[2], v[3], off.x);
}
}
