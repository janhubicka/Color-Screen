#ifndef LANZCOS_H
#define LANZCOS_H
#include <cmath>
#include "include/precomputed-function.h"

namespace colorscreen
{
inline double
sinc (double x)
{
  if (x == 0)
    return 1.0;
  x *= M_PI;
  return std::sin (x) / x;
}

/* a = 1	Lanczos-1	Mathematically identical to a Sinc filter; very blurry.
   a = 2	Lanczos-2	Good balance; cleaner than Bicubic but less sharp than Lanczos-3.
   a = 3	Lanczos-3	The Gold Standard. High sharpness and excellent detail preservation.
   a = 4+	Lanczos-4+	Theoretically sharper, but often introduces
   				excessive "ringing" (ghost edges) that can ruin the image.  */

inline double
lanczos_kernel (double x, int a = 3)
{
  if (std::abs (x) >= a)
    return 0.0;
  return sinc (x) * sinc (x / a);
}

struct lanczos3_initializer
{
  precomputed_function<double> func;
  lanczos3_initializer ()
  {
    const int steps = 1024;
    double y[steps];
    for (int i = 0; i < steps; ++i)
      {
	double x = 3.0 * i / (steps - 1);
	y[i] = lanczos_kernel (x, 3);
      }
    func.set_range (0.0, 3.0);
    func.init_by_y_values (y, steps);
  }
};

inline const lanczos3_initializer precomputed_lanczos3;

inline double
lanczos3_kernel (double x)
{
  x = std::abs (x);
  if (x >= 3.0)
    return 0.0;
  return precomputed_lanczos3.func.apply (x);
}
}
#endif
