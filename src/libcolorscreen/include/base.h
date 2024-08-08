#ifndef BASE_H
#define BASE_H
#include <cmath>
#include <cstdint>
#include "colorscreen-config.h"
namespace colorscreen
{
#define flatten_attr __attribute__ ((__flatten__))
#define always_inline_attr __attribute__ ((__always_inline__))
#ifdef COLORSCREEN_CHECKING
#define pure_attr
#define const_attr
static constexpr const bool colorscreen_checking = true;
#else
#define pure_attr __attribute__ ((__pure__))
#define const_attr __attribute__ ((__const__))
static constexpr const bool colorscreen_checking = false;
#endif

/* Windows does not seem to define this by default.  */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Prevent conversion to wrong data type when doing math.  */
static inline float
my_pow (float x, float y)
{
  return powf (x, y);
}

static inline double
my_pow (double x, double y)
{
  return pow (x, y);
}

/* Prevent conversion to wrong data type when doing math.  */
static inline float
my_sqrt (float x)
{
  return sqrtf (x);
}
static inline double
my_sqrt (double x)
{
  return sqrt (x);
}

static inline double
my_floor (double x)
{
  return floor (x);
}
static inline float
my_floor (float x)
{
  return floorf (x);
}

typedef double coord_t;
struct int_point_t;
struct point_t
{
  coord_t x, y;
  pure_attr inline coord_t
  dist_sq2_from (point_t p, coord_t xscale = 1, coord_t yscale = 1) const
  {
    return (x - p.x) * (x - p.x) * (xscale * xscale) + (y - p.y) * (y - p.y) * (yscale * yscale);
  }
  pure_attr inline coord_t
  dist_from (point_t p, coord_t xscale = 1, coord_t yscale = 1) const
  {
    return my_sqrt (dist_sq2_from (p, xscale, yscale));
  }
  pure_attr inline coord_t
  length () const
  {
    return my_sqrt (x * x + y * y);
  }
  point_t inline &operator+=(const point_t other)
  {
    x += other.x;
    y += other.y;
    return *this;
  }
  pure_attr inline point_t operator+(const point_t other) const
  {
    return {x + other.x, y + other.y};
  }
  point_t inline &operator-=(const point_t other)
  {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  pure_attr inline point_t operator-(const point_t other) const
  {
    return {x - other.x, y - other.y};
  }
  point_t inline &operator*=(const coord_t other)
  {
    x *= other;
    y *= other;
    return *this;
  }
  pure_attr inline point_t operator*(const coord_t other) const
  {
    return {x * other, y * other};
  }
  const_attr inline bool almost_eq(point_t other, coord_t epsilon = 0.001) const
  {
    return (fabs (x-other.x) < epsilon && fabs (y - other.y) < epsilon);
  }
  pure_attr inline bool operator== (point_t &other) const
  {
    return x == other.x && y == other.y;
  }
  bool inline operator==(const point_t &other) const
  {
    return other.x == x && other.y == y;
  }
  bool inline operator!=(const point_t &other) const
  {
    return other.x != x || other.y != y;
  }
  inline int_point_t floor ();
  inline int_point_t nearest ();
  inline point_t modf (int_point_t *ret = NULL);
};
struct int_point_t
{
  int64_t x, y;
  int_point_t inline &operator+=(const int_point_t other)
  {
    x += other.x;
    y += other.y;
    return *this;
  }
  pure_attr inline int_point_t operator+(const int_point_t other) const
  {
    return {x + other.x, y + other.y};
  }
  int_point_t inline &operator-=(const int_point_t other)
  {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  pure_attr inline int_point_t operator-(const int_point_t other) const
  {
    return {x - other.x, y - other.y};
  }
  int_point_t inline &operator*=(const int64_t other)
  {
    x *= other;
    y *= other;
    return *this;
  }
  pure_attr inline int_point_t operator*(const int64_t other) const
  {
    return {x * other, y * other};
  }
  pure_attr inline bool operator== (int_point_t &other) const
  {
    return x == other.x && y == other.y;
  }
  bool inline operator==(const int_point_t &other) const
  {
    return other.x == x && other.y == y;
  }
  bool inline operator!=(const int_point_t &other) const
  {
    return other.x != x || other.y != y;
  }
};

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

int_point_t
point_t::floor ()
{
  return {(int64_t)my_floor (x), (int64_t)my_floor (y)};
}
int_point_t
point_t::nearest ()
{
  return {(int64_t)nearest_int (x), (int64_t)nearest_int (y)};
}
point_t
point_t::modf (int_point_t *val)
{
  int xx, yy;
  point_t ret = {my_modf (x, &xx), my_modf (y, &yy)};
  if (val)
    *val = {(int64_t)xx, (int64_t)yy};
  return ret;
}
}
#endif
