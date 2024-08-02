#ifndef BASE_H
#define BASE_H
#include <cmath>
#include <cstdint>
#include "colorscreen-config.h"
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

typedef double coord_t;
struct point_t
{
  coord_t x, y;
  pure_attr inline coord_t
  dist_sq2_from (point_t p) const
  {
    return (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y);
  }
  pure_attr inline coord_t
  dist_from (point_t p) const
  {
    return my_sqrt (dist_sq2_from (p));
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
#endif
