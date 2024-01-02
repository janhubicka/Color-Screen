#ifndef COLOR_H
#define COLOR_H
#include <cmath>
#include "matrix.h"

//typedef double luminosity_t;
typedef float luminosity_t;
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

inline luminosity_t
srgb_to_linear (luminosity_t c)
{
  /* sRGB values are undefined outside range 0..1 but give them reasonable meanings.  */
  if (c < 0)
    return -my_pow (-c, (luminosity_t)2.2);
  else if (c > 1)
    return my_pow (c, (luminosity_t)2.2);
  if (c < (luminosity_t)0.04045)
    return c / (luminosity_t)12.92;
  return my_pow ((c + (luminosity_t)0.055) / (luminosity_t)1.055, (luminosity_t)2.4);
}

/* We handle special gama of -1 for sRGB color space curves.  */
inline luminosity_t
apply_gamma (luminosity_t val, luminosity_t gamma)
{
  if (gamma == 1)
    return val;
  if (gamma == -1)
    return srgb_to_linear (val);
  if (val >= 0)
    return my_pow (val, gamma);
  else
    return -my_pow (-val, gamma);
}

/* sRGB->XYZ conversion matrix.  */
typedef matrix4x4<luminosity_t> color_matrix;
class srgb_xyz_matrix : public color_matrix
{
public:
  inline
  srgb_xyz_matrix ()
  : color_matrix (0.4124564,  0.3575761,  0.1804375, 0,
		  0.2126729,  0.7151522,  0.0721750, 0,
		  0.0193339,  0.1191920,  0.9503041, 0,
		  0,             0,              0,                  1)
  {}
};
/* XYZ->sRGB conversion matrix.  */
class xyz_srgb_matrix : public color_matrix
{
public:
  inline
  xyz_srgb_matrix ()
  : color_matrix (3.2404542, -1.5371385, -0.4985314, 0,
		 -0.9692660,  1.8760108,  0.0415560, 0,
		  0.0556434, -0.2040259,  1.0572252, 0,
		  0,             0,              0,                  1)
  {}
};

inline luminosity_t
linear_to_srgb (luminosity_t c)
{
  /* sRGB values are undefined outside range 0..1 but give them reasonable meanings.  */
  if (c < 0)
    return -my_pow (-c, 1/(luminosity_t)2.2);
  else if (c > 1)
    return my_pow (c, 1/(luminosity_t)2.2);
  if (c<(luminosity_t)0.0031308)
    return (luminosity_t)12.92 * c;
  return ((luminosity_t)1.055)*my_pow (c, 1/(luminosity_t)2.4)-(luminosity_t)0.055;
}

struct xyz {
  luminosity_t x, y, z;
  constexpr xyz (luminosity_t xx, luminosity_t yy, luminosity_t zz)
  : x (xx), y (yy), z (zz)
  { }
  inline constexpr xyz (struct xyY);
  xyz &operator+=(const luminosity_t other)
  {
    x += other;
    y += other;
    z += other;
    return *this;
  }
  xyz &operator-=(const luminosity_t other)
  {
    x -= other;
    y -= other;
    z -= other;
    return *this;
  }
  xyz &operator*=(const luminosity_t other)
  {
    x *= other;
    y *= other;
    z *= other;
    return *this;
  }
  xyz &operator+=(const xyz other)
  {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }
  xyz operator+(const xyz b)
  {
    return {x + b.x, y + b.y, z + b.z};
  }
  xyz operator*(const luminosity_t b)
  {
    return {x * b, y * b, z * b};
  }
  xyz &operator-=(const xyz other)
  {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
  }
  xyz &operator*=(const xyz other)
  {
    x *= other.x;
    y *= other.y;
    z *= other.z;
    return *this;
  }
  luminosity_t &operator[](const int index)
  {
    switch (index)
    {
      case 0: return x;
      case 1: return y;
      case 2: return z;
      default: __builtin_unreachable ();
    }
  }
  inline void
  to_srgb (luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    xyz_srgb_matrix m;
    m.apply_to_rgb (x, y, z, r, g, b);
    *r = linear_to_srgb (*r);
    *g = linear_to_srgb (*g);
    *b = linear_to_srgb (*b);
  }
  static inline xyz
  from_srgb (luminosity_t r, luminosity_t g, luminosity_t b)
  {
    srgb_xyz_matrix m;
    luminosity_t x,y,z;
    r = srgb_to_linear (r);
    g = srgb_to_linear (g);
    b = srgb_to_linear (b);
    m.apply_to_rgb (r, g, b, &x, &y, &z);
    return xyz (x, y, z);
  }
  static inline xyz
  from_linear_srgb (luminosity_t r, luminosity_t g, luminosity_t b)
  {
    srgb_xyz_matrix m;
    luminosity_t x,y,z;
    m.apply_to_rgb (r, g, b, &x, &y, &z);
    return xyz (x, y, z);
  }
  void
  print_sRGB (FILE *f, bool verbose)
  {
    luminosity_t r, g, b;
    to_srgb (&r,&g,&b);
    r *= 255;
    g *= 255;
    b *= 255;
    if (verbose)
      fprintf (f, "sRGB r:%f g:%f b:%f " , r, g, b);
    r = std::min ((luminosity_t)255, std::max ((luminosity_t)0, r));
    g = std::min ((luminosity_t)255, std::max ((luminosity_t)0, g));
    b = std::min ((luminosity_t)255, std::max ((luminosity_t)0, b));
    fprintf (f, "#%02x%02x%02x", (int)(r + 0.5), (int)(g + 0.5), (int)(b + 0.5));
  }
  void
  print (FILE *f)
  {
    fprintf (f, "x:%f y:%f z:%f ", x, y, z);
    print_sRGB (f, true);
    fprintf (f, "\n");
  }
  bool almost_equal_p (xyz other, luminosity_t epsilon = 0.001)
  {
    return (fabs (x - other.x) < epsilon 
	    || fabs (y - other.y) > epsilon
	    || fabs (z - other.z) > epsilon);
  }
};
struct xy_t
{
  luminosity_t x, y;
  constexpr xy_t (luminosity_t xx, luminosity_t yy)
  : x (xx), y (yy)
  { }
  inline constexpr xy_t (struct xyY c);
  constexpr xy_t (xyz c)
  : x (c.x / (c.x + c.y + c.z)), y (c.y / (c.x + c.y + c.z))
  { }
};
struct xyY
{
  luminosity_t x, y, Y;
  constexpr xyY (luminosity_t xx, luminosity_t yy, luminosity_t YY)
  : x (xx), y (yy), Y (YY)
  {}
  constexpr xyY (xyz c)
  : x (c.x + c.y + c.z ? c.x / (c.x + c.y + c.z) : 0), y (c.x + c.y + c.z ? c.y / (c.x + c.y + c.z) : 0), Y (c.y)
  {}
};

inline constexpr
xy_t::xy_t (struct xyY c)
: x (c.x), y (c.y)
{ }

class bradford_xyz_to_rgb_matrix : public color_matrix
{
public:
  inline
  bradford_xyz_to_rgb_matrix ()
  : color_matrix ( 0.8951,  0.2664, -0.1614, 0,
		  -0.7502,  1.7135,  0.0367, 0,
		   0.0389, -0.0685,  1.0296, 0,
		  0,        0,       0,      1)
  {}
};
/* Inverse of the above.  */
class bradford_rgb_to_xyz_matrix : public color_matrix
{
public:
  inline
  bradford_rgb_to_xyz_matrix ()
  : color_matrix (0.986993,   -0.147054,  0.159963,  0,
		  0.432305,    0.51836,   0.0492912, 0,
		  -0.00852866, 0.0400428, 0.968487,  0,
		  0, 0, 0, 1)
  {}
};

/* The Bradford transform, so named since it resulted from
   work carried outat the University of Leeds under the sponsorship of the UK
   Society of Dyers and Colouristsbased in the nearby city of Bradford, has
   become accepted as a sound basis for predicting theeffects of adaptation to
   a reasonable degree of accuracy.  */
class bradford_whitepoint_adaptation_matrix : public color_matrix
{
public:
  bradford_whitepoint_adaptation_matrix (xyz from, xyz to)
  {
    color_matrix Mbfdinv = bradford_rgb_to_xyz_matrix ();
    color_matrix Mbfd    = bradford_xyz_to_rgb_matrix ();
    luminosity_t tor, tog, tob, fromr, fromg, fromb;
    Mbfd.apply_to_rgb (to.x, to.y, to.z, &tor, &tog, &tob);
    Mbfd.apply_to_rgb (from.x, from.y, from.z, &fromr, &fromg, &fromb);
    color_matrix correction (tor/fromr, 0, 0, 0,
			     0, tog/fromg, 0, 0,
			     0, 0, tob/fromb, 0,
			     0, 0, 0, 1);
    matrix<luminosity_t,4> ret = Mbfdinv * correction * Mbfd;
    memcpy (m_elements, ret.m_elements, sizeof (m_elements));
  }
};


constexpr xyz::xyz (xyY c)
 : x (!c.Y ? 0 : c.x * c.Y / c.y), y (c.Y), z (!c.Y ? 0 : (1 - c.x - c.y) * c.Y / c.y)
{
}

struct cie_lab
{
   luminosity_t l, a, b;

   cie_lab (xyz c);
};

/* Datastructure used to store information about dye luminosities.  */
struct rgbdata
{
  luminosity_t red, green, blue;
  bool operator== (rgbdata &other) const
  {
    return red == other.red
	   && green == other.green
	   && blue == other.blue;
  }
  bool operator!= (rgbdata &other) const
  {
    return !(*this == other);
  }
  rgbdata &operator+=(const luminosity_t other)
  {
    red += other;
    green += other;
    blue += other;
    return *this;
  }
  rgbdata &operator-=(const luminosity_t other)
  {
    red -= other;
    green -= other;
    blue -= other;
    return *this;
  }
  rgbdata &operator*=(const luminosity_t other)
  {
    red *= other;
    green *= other;
    blue *= other;
    return *this;
  }
  rgbdata &operator/=(const luminosity_t other)
  {
    luminosity_t rother = 1 / other;
    red *= rother;
    green *= rother;
    blue *= rother;
    return *this;
  }
  rgbdata &operator+=(const rgbdata other)
  {
    red += other.red;
    green += other.green;
    blue += other.blue;
    return *this;
  }
  rgbdata &operator-=(const rgbdata other)
  {
    red -= other.red;
    green -= other.green;
    blue -= other.blue;
    return *this;
  }
  rgbdata &operator*=(const rgbdata other)
  {
    red *= other.red;
    green *= other.green;
    blue *= other.blue;
    return *this;
  }
  luminosity_t &operator[](const int index)
  {
    switch (index)
    {
      case 0: return red;
      case 1: return green;
      case 2: return blue;
      default: __builtin_unreachable ();
    }
  }
  inline rgbdata
  gamma (luminosity_t g)
  {
    rgbdata ret = {apply_gamma (red, g), apply_gamma (green, g), apply_gamma (blue, g)};
    return ret;
  }

  /* Sign preserving gamma.  */
  inline rgbdata
  sgngamma (luminosity_t g)
  {
    rgbdata ret = {apply_gamma (fabs (red), g), apply_gamma (fabs (green), g), apply_gamma (fabs (blue), g)};
    if (red < 0)
      ret.red = -ret.red;
    if (green < 0)
      ret.green = -ret.green;
    if (blue < 0)
      ret.blue = -ret.blue;
    return ret;
  }
  inline rgbdata
  normalize ()
  {
    luminosity_t coef = 1 / my_sqrt (red * red + blue * blue + green * green);
    rgbdata ret = {red * coef, green * coef, blue * coef};
    return ret;
  }
};
inline rgbdata operator+(rgbdata lhs, luminosity_t rhs)
{
  lhs += rhs;
  return lhs;
}
inline rgbdata operator-(rgbdata lhs, luminosity_t rhs)
{
  lhs -= rhs;
  return lhs;
}
inline rgbdata operator*(rgbdata lhs, luminosity_t rhs)
{
  lhs *= rhs;
  return lhs;
}
inline rgbdata operator/(rgbdata lhs, luminosity_t rhs)
{
  lhs /= rhs;
  return lhs;
}
inline rgbdata operator+(rgbdata lhs, rgbdata rhs)
{
  lhs += rhs;
  return lhs;
}
inline rgbdata operator-(rgbdata lhs, rgbdata rhs)
{
  lhs -= rhs;
  return lhs;
}
inline rgbdata operator*(rgbdata lhs, rgbdata rhs)
{
  lhs *= rhs;
  return lhs;
}

inline luminosity_t
invert_gamma (luminosity_t val, luminosity_t gamma)
{
  if (gamma == 1)
    return val;
  if (gamma == -1)
    return linear_to_srgb (val);
  if (val >= 0)
    return my_pow (val, 1 / gamma);
  else
    return my_pow (-val, 1 / gamma);
}

typedef rgbdata color_t;
color_matrix matrix_by_dye_xy (luminosity_t rx, luminosity_t ry,
			       luminosity_t gx, luminosity_t gy,
			       luminosity_t bx, luminosity_t by);
color_matrix matrix_by_dye_xyY (xyY red, xyY green, xyY blue);
color_matrix matrix_by_dye_xyz (xyz red, xyz green, xyz blue);
// http://www.graficaobscura.com/matrix/index.html
static const luminosity_t rwght = 0.3086, gwght = 0.6094, bwght = 0.0820;

// http://www.graficaobscura.com/matrix/index.html
class saturation_matrix : public color_matrix
{
public:
  inline
  saturation_matrix (luminosity_t s)
  : color_matrix ((1-s)*rwght + s, (1-s)*gwght    , (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght + s, (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght    , (1-s)*bwght + s, 0,
	       0,             0,              0,                  1)
  {}
};

/* Same as saturation matrix but have all weights 1/3.  */
class presaturation_matrix : public color_matrix
{
public:
  inline
  presaturation_matrix (luminosity_t s)
  : color_matrix ((1-s)*(1.0/3.0) + s, (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0)    , 0,
	       (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0) + s, (1-s)*(1.0/3.0)    , 0,
	       (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0) + s, 0,
	       0,             0,              0,                  1)
  {}
};

/* Autocrhome dyes srgb based on Eversmart scan of Smirous singer
   54.4 9.1 5.2
   8.9 36.5 8.1
   19.6 9.8 33.3
 */
class autochrome_matrix : public color_matrix
{
public:
  inline
  autochrome_matrix ()
  : color_matrix (0.544,0.089,0.052, 0,
	       0.089,0.365,0.081, 0,
	       0.196,0.098,0.333, 0,
	       0,             0,              0,                  1)
  { }
};
/* Matrix profile of Finlay taking screen
   Based on XYZ measurements of Finlay filter scan on eversmart dimmed to 50%.   */
#if 0
class finlay_matrix : public color_matrix
{
public:
  inline
  finlay_matrix ()
  : color_matrix (0.116325,0.148173,0.060772, 0,
	          0.059402,0.201094,0.028883, 0,
	          0.005753,0.030250,0.136011, 0,
	          0,             0,              0,                  1)
  { }
};
#else
#if 0
/* Based on second measurement. */
class finlay_matrix : public color_matrix
{
public:
  inline
  finlay_matrix ()
  : color_matrix (0.127466,0.147393,0.060898, 0,
	       0.064056,0.200520,0.028144, 0,
	       0.053229,0.028117,0.138672, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
#if 0
/* Based on second measurement. */
class finlay_matrix : public color_matrix
{
public:
  inline
  finlay_matrix ()
  : color_matrix (0.212141,0.276332,0.102475, 0,
	       0.104568,0.378063,0.050871, 0,
	       0.102475,0.057676,0.267136, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
/* Based on third measurement. */
class finlay_matrix : public color_matrix
{
public:
  inline
  finlay_matrix ()
  : color_matrix (0.158378,0.191719,0.078963, 0,
	       0.079810,0.258469,0.036660, 0,
	       0.072299,0.038142,0.179542, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
class adjusted_finlay_matrix : public color_matrix
{
public:
  inline
  adjusted_finlay_matrix ()
  : color_matrix (0.116325,0.148173-0.015,0.060772 - 0.03, 0,
		  0.059402+0.02,0.201094+0.04,0.028883 /*- 0.02*/+0.01, 0,
		  0.005753,0.030250,0.136011 /*+ 0.1*/, 0,
		  0,             0,              0,                  1)
  { }
};
/* Matrix profile of dufay taken from Nikon steamroler.
   In XYZ.  */
class dufay_matrix : public color_matrix
{
public:
  inline
  dufay_matrix ()
  : color_matrix (0.321001,0.205657,0.072222, 0,
		  0.178050,0.406124,0.071736, 0,
		  0.006007,0.040292,0.240037, 0,
		  0,             0,              0,                  1)
  { }
};
/* Matrix I decided works well for kimono picture (sRGB).  */
class grading_matrix : public color_matrix
{
public:
  inline
  grading_matrix ()
  : color_matrix (1,-0.4,-0.1, 0,
		  0.25,1,-0.1, 0,
		  +0.05,-0.55,1.05, 0,
		  0,             0,              0,                  1)
  { normalize_grayscale (); }
};
/* XYZ->wide gammut RGB conversion matrix.  */
class xyz_wide_gammut_rgb_matrix : public color_matrix
{
public:
  inline
  xyz_wide_gammut_rgb_matrix ()
  : color_matrix ( 1.4628067, -0.1840623, -0.2743606, 0,
		  -0.5217933,  1.4472381,  0.0677227, 0,
		   0.0349342, -0.0968930,  1.2884099, 0,
		  0,             0,              0,                  1)
  {}
};
/* XYZ->wide gammut RGB conversion matrix.  */
class xyz_pro_photo_rgb_matrix : public color_matrix
{
public:
  inline
  xyz_pro_photo_rgb_matrix ()
  : color_matrix (1.3459433, -0.2556075, -0.0511118, 0,
		  -0.5445989,  1.5081673,  0.0205351, 0,
		   0.0000000,  0.0000000,  1.2118128, 0,
		  0,             0,              0,                  1)
  {}
};
inline void
xyz_to_srgb (luminosity_t x, luminosity_t y, luminosity_t z,  luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  xyz_srgb_matrix m;
  m.apply_to_rgb (x, y, z, r, g, b);
  *r = linear_to_srgb (*r);
  *g = linear_to_srgb (*g);
  *b = linear_to_srgb (*b);
}
inline void
xyz_to_wide_gammut_rgb (luminosity_t x, luminosity_t y, luminosity_t z,  luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  xyz_wide_gammut_rgb_matrix m;
  m.apply_to_rgb (x, y, z, r, g, b);
  *r = invert_gamma (*r, 2.2);
  *g = invert_gamma (*g, 2.2);
  *b = invert_gamma (*b, 2.2);
}
/* Add gamma function.  */
inline void
xyz_to_pro_photo_rgb (luminosity_t x, luminosity_t y, luminosity_t z,  luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  xyz_pro_photo_rgb_matrix m;
  m.apply_to_rgb (x, y, z, r, g, b);
  /*TODO: Fix also in spectrum_dyes_to_xyz::tiff_with_overlapping_filters_response.  */
  /**r = invert_gamma (*r, 1.8);
  *g = invert_gamma (*g, 1.8);
  *b = invert_gamma (*b, 1.8);*/
}

inline xyz
xyY_to_xyz (luminosity_t x, luminosity_t y, luminosity_t Y)
{
  if (!Y)
    return (xyz){0,0,0};
  xyz ret = {x * Y / y, Y, (1 - x - y) * Y / y};
  return ret;
}
inline void
xyz_to_xyY (luminosity_t x, luminosity_t y, luminosity_t z,  luminosity_t *rx, luminosity_t *ry, luminosity_t *rY)
{
	*rx = x / (x + y + z);
	*ry = y / (x + y + z);
	*rY = y;
}

luminosity_t deltaE(cie_lab c1, cie_lab c2);
luminosity_t deltaE2000(cie_lab c1, cie_lab c2);
luminosity_t deltaE(xyz c1, xyz c2);
luminosity_t deltaE2000(xyz c1, xyz c2);
luminosity_t dominant_wavelength (xy_t color, xy_t whitepoint = xy_t(0.33,0.33));
xy_t find_best_whitepoint (xyz red, xyz green, xyz blue,
			   luminosity_t red_dominating_wavelength,
			   luminosity_t green_dominating_wavelength,
			   luminosity_t blue_dominating_wavelength);

static const xyz srgb_white (0.9505, 1, 1.0888);	 // sRGB whitepoint
static const xyz netural_white = xyY (0.33,0.33, 1);     // neutral white
static const xyz il_A_white = xyY (0.44757, 0.40745, 1); // Illuminant B white
static const xyz il_B_white = xyY (0.34842, 0.35161, 1); // Illuminant B white
static const xyz il_C_white = xyY (0.31006, 0.31616, 1); // Illuminant C white
#endif
