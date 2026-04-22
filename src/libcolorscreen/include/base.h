#ifndef BASE_H
#define BASE_H
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "colorscreen-config.h"
namespace colorscreen
{
/* Property specification.  */
struct property_t {
  const char *name;
  const char *pretty_name;
  const char *help;
};

#define flatten_attr __attribute__ ((__flatten__))
#define always_inline_attr __attribute__ ((__always_inline__))
#define nodiscard_attr [[nodiscard]]
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

/* Floor value X.  */
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

using coord_t = double;
struct int_point_t;

/* Coordinate point with real values.  */
struct point_t
{
  coord_t x, y;

  /* Return squared distance from this point to P.  XSCALE and YSCALE
     can scale the axes.  */
  pure_attr inline constexpr coord_t
  dist_sq2_from (point_t p, coord_t xscale = 1, coord_t yscale = 1) const
  {
    return (x - p.x) * (x - p.x) * (xscale * xscale) + (y - p.y) * (y - p.y) * (yscale * yscale);
  }

  /* Return distance from this point to P.  XSCALE and YSCALE
     can scale the axes.  */
  pure_attr inline coord_t
  dist_from (point_t p, coord_t xscale = 1, coord_t yscale = 1) const
  {
    return my_sqrt (dist_sq2_from (p, xscale, yscale));
  }

  /* Return length of the vector formed by this point.  */
  pure_attr inline coord_t
  length () const
  {
    return my_sqrt (x * x + y * y);
  }

  /* Add OTHER to this point.  */
  inline constexpr point_t &
  operator+=(const point_t other)
  {
    x += other.x;
    y += other.y;
    return *this;
  }

  /* Return sum of this point and OTHER.  */
  pure_attr inline constexpr point_t
  operator+(const point_t other) const
  {
    return {x + other.x, y + other.y};
  }

  /* Subtract OTHER from this point.  */
  inline constexpr point_t &
  operator-=(const point_t other)
  {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  /* Return difference between this point and OTHER.  */
  pure_attr inline constexpr point_t
  operator-(const point_t other) const
  {
    return {x - other.x, y - other.y};
  }

  /* Multiply this point by scalar OTHER.  */
  inline constexpr point_t &
  operator*=(const coord_t other)
  {
    x *= other;
    y *= other;
    return *this;
  }

  /* Return product of this point and scalar OTHER.  */
  pure_attr inline constexpr point_t
  operator*(const coord_t other) const
  {
    return {x * other, y * other};
  }

  /* Divide this point by scalar OTHER.  */
  inline constexpr point_t &
  operator/=(const coord_t other)
  {
    x /= other;
    y /= other;
    return *this;
  }

  /* Return quotient of this point and scalar OTHER.  */
  pure_attr inline constexpr point_t
  operator/(const coord_t other) const
  {
    return {x / other, y / other};
  }

  /* Return true if this point is almost equal to OTHER within EPSILON.  */
  const_attr inline bool
  almost_eq(point_t other, coord_t epsilon = 0.001) const
  {
    return (fabs (x - other.x) < epsilon && fabs (y - other.y) < epsilon);
  }

  /* Return true if this point is equal to OTHER.  */
  pure_attr inline bool
  operator== (const point_t &other) const
  {
    return x == other.x && y == other.y;
  }

  /* Return true if this point is not equal to OTHER.  */
  bool inline
  operator!=(const point_t &other) const
  {
    return !(*this == other);
  }
  inline int_point_t floor () const;
  inline int_point_t nearest () const;
  inline point_t modf (int_point_t *ret = nullptr) const;
};
/* Coordinate point with integer values.  */
struct int_point_t
{
  int64_t x, y;

  /* Add OTHER to this point.  */
  inline constexpr int_point_t &
  operator+=(const int_point_t other)
  {
    x += other.x;
    y += other.y;
    return *this;
  }

  /* Return sum of this point and OTHER.  */
  pure_attr inline constexpr int_point_t
  operator+(const int_point_t other) const
  {
    return {x + other.x, y + other.y};
  }

  /* Subtract OTHER from this point.  */
  inline constexpr int_point_t &
  operator-=(const int_point_t other)
  {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  /* Return difference between this point and OTHER.  */
  pure_attr inline constexpr int_point_t
  operator-(const int_point_t other) const
  {
    return {x - other.x, y - other.y};
  }

  /* Multiply this point by scalar OTHER.  */
  inline constexpr int_point_t &
  operator*=(const int64_t other)
  {
    x *= other;
    y *= other;
    return *this;
  }

  /* Return product of this point and scalar OTHER.  */
  pure_attr inline constexpr int_point_t
  operator*(const int64_t other) const
  {
    return {x * other, y * other};
  }

  /* Return true if this point is equal to OTHER.  */
  pure_attr inline bool
  operator== (const int_point_t &other) const
  {
    return x == other.x && y == other.y;
  }

  /* Return true if this point is not equal to OTHER.  */
  bool inline
  operator!=(const int_point_t &other) const
  {
    return !(*this == other);
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
  double f = floor (x);
  double ret = x - f;
  *ptr = f;
  return ret;
}

/* Round value X to nearest integer.  */
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

/* Return floor of this point.  */
int_point_t
point_t::floor () const
{
  return {(int64_t)my_floor (x), (int64_t)my_floor (y)};
}

/* Return nearest integer of this point.  */
int_point_t
point_t::nearest () const
{
  return {(int64_t)nearest_int (x), (int64_t)nearest_int (y)};
}

/* Decompose this point to integer and fractional part.  */
point_t
point_t::modf (int_point_t *val) const
{
  int xx, yy;
  point_t ret = {my_modf (x, &xx), my_modf (y, &yy)};
  if (val)
    *val = {(int64_t)xx, (int64_t)yy};
  return ret;
}

/* Hold coordinates of a rectangular tile within a bigger image.  */
template<typename T>
class image_area_base
{
public:
  
  /* Top left corner */
  T x, y;
  /* Size */
  T width, height;
  constexpr image_area_base ()
  : x (0), y (0), width (0), height (0)
  { }
  constexpr image_area_base (T nx, T ny, T nwidth, T nheight)
  : x (nx), y (ny), width (nwidth), height (nheight)
  { }

  /* Return true if the area is empty.  */
  pure_attr inline constexpr bool
  empty_p () const
  {
    return width <= 0 || height <= 0;
  }

  /* Return intersection of this area and OTHER.  */
  pure_attr inline constexpr image_area_base<T>
  intersect (image_area_base other) const
  {
    image_area_base<T> ret (x, y, width, height);
    if (x < other.x)
      {
	ret.width -= other.x - x;
	ret.x = other.x;
      }
    if (y < other.y)
      {
	ret.height -= other.y - y;
	ret.y = other.y;
      }
    if (ret.x + ret.width > other.x + other.width)
      ret.width = other.x + other.width - x;
    if (ret.y + ret.height > other.y + other.height)
      ret.height = other.y + other.height - y;
    ret.width = std::max (width, (T)0);
    ret.height = std::max (height, (T)0);
    return ret;
  }

  /* Return true if this area is equal to OTHER.  */
  pure_attr inline constexpr bool
  operator== (const image_area_base &other) const
  {
    return x == other.x && y == other.y && width == other.width && height == other.height;
  }
};
/* Optinally hold area of an image. */
template<typename T>
class optional_image_area_base : public image_area_base<T>
{
public:
  bool set;
  constexpr optional_image_area_base ()
  : set (false)
  { }
  bool
  operator== (const optional_image_area_base &other) const
  {
    return (image_area_base<T>::operator==(other) && set == other.set);
  }
};

typedef image_area_base<int> int_image_area;
typedef optional_image_area_base<int> int_optional_image_area;

/* Base class for geometry specifications for analyzers of regular screens.  */
struct base_geometry
{
  /* Convert demosaiced coordinates to screen coordinates.  */
  inline static point_t from_demosaiced_coordinates (point_t p)
  {
    return p;
  }
  /* Convert screen coordinates to demosaiced coordinates.  */
  inline static point_t to_demosaiced_coordinates (point_t p)
  {
    return p;
  }
  inline static int demosaic_period_x ()
  {
    return 2;
  }
  inline static int demosaic_period_y ()
  {
    return 2;
  }
  enum demosaic_entry_color
  {
    red,
    green,
    blue,
  };

  /* Default color pattern.  */
  inline static int demosaic_entry_color (int x, int y)
  {
    x &= 1;
    y &= 1;
    if (x == 0 && y == 0)
      return green;
    if (x == 1 && y == 1)
      return red;
    return blue;
  }
};

}
#endif
