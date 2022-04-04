#ifndef COLOR_H
#define COLOR_H
#include "matrix.h"

// http://www.graficaobscura.com/matrix/index.html
static const double rwght = 0.3086, gwght = 0.6094, bwght = 0.0820;

// http://www.graficaobscura.com/matrix/index.html
class saturation_matrix : public matrix4x4
{
public:
  inline
  saturation_matrix (double s)
  : matrix4x4 ((1-s)*rwght + s, (1-s)*gwght    , (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght + s, (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght    , (1-s)*bwght + s, 0,
	       0,             0,              0,                  1)
  {}
};

/* Same as saturation matrix but have all weights 1/3.  */
class presaturation_matrix : public matrix4x4
{
public:
  inline
  presaturation_matrix (double s)
  : matrix4x4 ((1-s)*(1.0/3.0) + s, (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0)    , 0,
	       (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0) + s, (1-s)*(1.0/3.0)    , 0,
	       (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0)    , (1-s)*(1.0/3.0) + s, 0,
	       0,             0,              0,                  1)
  {}
};
/* Matrix profile of Finlay taking screen
   Based on XYZ measurements of Finlay filter scan on eversmart dimmed to 50%.   */
#if 1
class finlay_matrix : public matrix4x4
{
public:
  inline
  finlay_matrix ()
  : matrix4x4 (0.116325,0.148173,0.060772, 0,
	       0.059402,0.201094,0.028883, 0,
	       0.005753,0.030250,0.136011, 0,
	       0,             0,              0,                  1)
  { }
};
#else
#if 0
/* Based on second measurement. */
class finlay_matrix : public matrix4x4
{
public:
  inline
  finlay_matrix ()
  : matrix4x4 (0.127466,0.147393,0.060898, 0,
	       0.064056,0.200520,0.028144, 0,
	       0.053229,0.028117,0.138672, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
#if 0
/* Based on second measurement. */
class finlay_matrix : public matrix4x4
{
public:
  inline
  finlay_matrix ()
  : matrix4x4 (0.212141,0.276332,0.102475, 0,
	       0.104568,0.378063,0.050871, 0,
	       0.102475,0.057676,0.267136, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
/* Based on third measurement. */
class finlay_matrix : public matrix4x4
{
public:
  inline
  finlay_matrix ()
  : matrix4x4 (0.158378,0.191719,0.078963, 0,
	       0.079810,0.258469,0.036660, 0,
	       0.072299,0.038142,0.179542, 0,
	       0,             0,              0,                  1)
  { }
};
#endif
class adjusted_finlay_matrix : public matrix4x4
{
public:
  inline
  adjusted_finlay_matrix ()
  : matrix4x4 (0.116325,0.148173-0.015,0.060772 - 0.03, 0,
	       0.059402+0.02,0.201094+0.04,0.028883 /*- 0.02*/+0.01, 0,
	       0.005753,0.030250,0.136011 /*+ 0.1*/, 0,
	       0,             0,              0,                  1)
  { }
};
/* Matrix profile of dufay taken from Nikon steamroler.
   In XYZ.  */
class dufay_matrix : public matrix4x4
{
public:
  inline
  dufay_matrix ()
  : matrix4x4 (0.321001,0.205657,0.072222, 0,
	       0.178050,0.406124,0.071736, 0,
	       0.006007,0.040292,0.240037, 0,
	       0,             0,              0,                  1)
  { }
};
/* Matrix I decided works well for kimono picture (sRGB).  */
class grading_matrix : public matrix4x4
{
public:
  inline
  grading_matrix ()
  : matrix4x4 (1,-0.4,-0.1, 0,
	       0.25,1,-0.1, 0,
	       +0.05,-0.55,1.05, 0,
	       0,             0,              0,                  1)
  { normalize_grayscale (); }
};
/* sRGB->XYZ conversion matrix.  */
class srgb_xyz_matrix : public matrix4x4
{
public:
  inline
  srgb_xyz_matrix ()
  : matrix4x4 (0.4124564,  0.3575761,  0.1804375, 0,
 	       0.2126729,  0.7151522,  0.0721750, 0,
 	       0.0193339,  0.1191920,  0.9503041, 0,
	       0,             0,              0,                  1)
  {}
};
/* XYZ->sRGB conversion matrix.  */
class xyz_srgb_matrix : public matrix4x4
{
public:
  inline
  xyz_srgb_matrix ()
  : matrix4x4 (3.2404542, -1.5371385, -0.4985314, 0,
	      -0.9692660,  1.8760108,  0.0415560, 0,
	       0.0556434, -0.2040259,  1.0572252, 0,
	       0,             0,              0,                  1)
  {}
};
inline double
srgb_to_linear (double c)
{
  if (c < 0.04045)
    return c / 12.92;
  return pow ((c + 0.055) / 1.055, 2.4);
}
inline double
linear_to_srgb (double c)
{
  if (c<0.0031308)
    return 12.92 * c;
  return 1.055*pow (c, 1/2.4)-0.055;
}
inline void
xyz_to_srgb (double x, double y, double z,  double *r, double *g, double *b)
{
  xyz_srgb_matrix m;
  m.apply_to_rgb (x, y, z, r, g, b);
  *r = linear_to_srgb (*r);
  *g = linear_to_srgb (*g);
  *b = linear_to_srgb (*b);
}

inline void
srgb_to_xyz (double r, double g, double b,  double *x, double *y, double *z)
{
  srgb_xyz_matrix m;
  r = srgb_to_linear (r);
  g = srgb_to_linear (g);
  b = srgb_to_linear (b);
  m.apply_to_rgb (r, g, b, x, y, z);
}
#endif
