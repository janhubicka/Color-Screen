#include <cmath>
#include "include/precomputed-function.h"
namespace colorscreen
{
// Standard Cubic B-Spline Kernel function
inline double
bspline_kernel(double x) {
    x = std::abs(x);
    if (x < 1.0) {
        return (4.0 - 6.0 * x * x + 3.0 * x * x * x) / 6.0;
    } else if (x < 2.0) {
        return (8.0 - 12.0 * x + 6.0 * x * x - x * x * x) / 6.0;
    }
    return 0.0;
}
}
