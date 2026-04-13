#ifndef CUBIC_INTERPOLATE_H
#define CUBIC_INTERPOLATE_H
namespace colorscreen
{
typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;
// Standard Cubic B-Spline Kernel function
inline luminosity_t
cubic_kernel (luminosity_t x)
{
  x = std::abs (x);
  if (x <= 1.0)
    return ((luminosity_t)1.5) * x * x * x - ((luminosity_t)2.5) * x * x + 1;
  if (x < 2.0)
    return ((luminosity_t)-0.5) * x * x * x + ((luminosity_t)2.5) * x * x - (4 * x) + 2;
  return 0.0;
# if 0
  if (x < (luminosity_t)1.0)
    return ((luminosity_t)(2.0 / 3.0)
            + (-(luminosity_t)1.0 + ((luminosity_t)0.5) * x) * x * x);
  else if (x < (luminosity_t)2.0)
    {
      x = 2 - x;
      return ((luminosity_t)(1.0 / 6.0)) * x * x * x;
    }
#endif
  return 0.0;
}

struct cubic_initializer
{
  static constexpr int steps = 1025;
  vec_luminosity_t kernels[steps];
  cubic_initializer ()
  {
    for (int i = 0; i < steps; ++i)
      {
        luminosity_t off = i / (luminosity_t)(steps - 1);
        kernels[i] = (vec_luminosity_t){ cubic_kernel (-1 - off),
                                         cubic_kernel (0 - off),
                                         cubic_kernel (1 - off),
                                         cubic_kernel (2 - off) };
      }
  }
};
inline const cubic_initializer precomputed_cubic;
/* Cubic interpolation helper.  */

static inline luminosity_t const_attr __attribute__ ((always_inline))
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
#if 1
  vec_luminosity_t k = precomputed_cubic.kernels[nearest_int (x * (precomputed_cubic.steps - 1))];
  return p0 * k[0] + p1 * k[1] + p2 * k[2] + p3 * k[3];
#else
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
#endif
}
static inline vec_luminosity_t const_attr __attribute__ ((always_inline))
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
#if 1
  vec_luminosity_t k = precomputed_cubic.kernels[nearest_int (x * (precomputed_cubic.steps - 1))];
  return p0 * k[0] + p1 * k[1] + p2 * k[2] + p3 * k[3];
#else
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
#endif
}
inline flatten_attr pure_attr luminosity_t always_inline_attr
do_bicubic_interpolate (vec_luminosity_t v1, vec_luminosity_t v2, vec_luminosity_t v3, vec_luminosity_t v4, point_t off)
{
  vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, off.y);
  return cubic_interpolate (v[0], v[1], v[2], v[3], off.x);
}
}
#endif
