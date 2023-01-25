#ifndef BASE_H
#define BASE_H
#include <cmath>
typedef double coord_t;

/* Like modf but always round down.  */
static inline float
my_modf (float x, int *ptr)
{
  float f = floorf (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}
static inline double
my_modf (double x, int *ptr)
{
  float f = floor (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}

static inline long long
nearest_int (float x)
{
  return roundf (x);
}
static inline long long
nearest_int (double x)
{
  return round (x);
}
#endif
