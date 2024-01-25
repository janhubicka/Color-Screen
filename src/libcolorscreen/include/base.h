#ifndef BASE_H
#define BASE_H
#include <cmath>
#include <cstdint>
typedef double coord_t;
#define flatten_attr __attribute__ ((__flatten__))
#define pure_attr __attribute__ ((__pure__))
#define const_attr __attribute__ ((__const__))

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

static inline int64_t
nearest_int (float x)
{
  return roundf (x);
}
static inline int64_t
nearest_int (double x)
{
  return round (x);
}
#endif
