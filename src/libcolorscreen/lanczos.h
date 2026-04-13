#ifndef LANZCOS_H
#define LANZCOS_H
#include <cmath>
#include "include/precomputed-function.h"

namespace colorscreen
{
inline luminosity_t
sinc (luminosity_t x)
{
  if (x == 0)
    return 1.0;
  x *= M_PI;
  return std::sinf (x) / x;
}

/* a = 1	Lanczos-1	Mathematically identical to a Sinc filter; very blurry.
   a = 2	Lanczos-2	Good balance; cleaner than Bicubic but less sharp than Lanczos-3.
   a = 3	Lanczos-3	The Gold Standard. High sharpness and excellent detail preservation.
   a = 4+	Lanczos-4+	Theoretically sharper, but often introduces
   				excessive "ringing" (ghost edges) that can ruin the image.  */

inline luminosity_t
lanczos_kernel (luminosity_t x, int a = 3)
{
  if (std::abs (x) >= a)
    return 0.0;
  return sinc (x) * sinc (x / a);
}

struct lanczos3_initializer
{
  static constexpr int steps = 1025;
  luminosity_t kernels[steps][6];
  lanczos3_initializer ()
  {
    for (int i = 0; i < steps; ++i)
      {
        luminosity_t off = i / (luminosity_t)(steps - 1);
        kernels[i][0] = lanczos_kernel (-2 - off);
        kernels[i][1] = lanczos_kernel (-1 - off);
        kernels[i][2] = lanczos_kernel ( 0 - off);
        kernels[i][3] = lanczos_kernel ( 1 - off);
        kernels[i][4] = lanczos_kernel ( 2 - off);
        kernels[i][5] = lanczos_kernel ( 3 - off);
      }
  }
#if 0
  precomputed_function<luminosity_t> func;
  lanczos3_initializer ()
  {
    const int steps = 1024;
    luminosity_t y[steps];
    for (int i = 0; i < steps; ++i)
      {
	luminosity_t x = 3.0 * i / (steps - 1);
	y[i] = lanczos_kernel (x, 3);
      }
    func.set_range (0.0, 3.0);
    func.init_by_y_values (y, steps);
  }
#endif
};

inline const lanczos3_initializer precomputed_lanczos3;

inline const luminosity_t *
lanczos3_kernel (luminosity_t x)
{
  return precomputed_lanczos3.kernels[nearest_int (x * (precomputed_cubic.steps - 1))];
}
}
#endif
