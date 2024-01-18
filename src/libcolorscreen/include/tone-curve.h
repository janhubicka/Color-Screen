#ifndef TONE_CURVE_H
#define TONE_CURVE_H
#include <cassert>
#include "precomputed-function.h"
#include "color.h"

/* Implement DNG-style tone curve.  */
class tone_curve:public precomputed_function <luminosity_t>
{
  const bool debug=true;
public:
  enum tone_curves
  {
    tone_curve_linear,
    tone_curve_dng,
    tone_curve_max
  };
  constexpr static const char *tone_curve_names[tone_curve_max] =
  {
    "linear",
    "dng"
  };
  tone_curve (enum tone_curves type);
  /* This does the same as dng reference implementation.  */
  rgbdata
  apply_to_rgb (rgbdata c)
  {
    const int r = 0, g = 1, b = 2;
    if (c.red >= c.green)
      {
	if (c.green > c.blue)
	  return apply_to_rgb_1 (c, r, g, b); // r > g > b
	else if (c.blue > c.red)
	  return apply_to_rgb_1 (c, b, r, g); // b > r > g
	else if (c.blue > c.green)
	  return apply_to_rgb_1 (c, r, b, g); // r > b > g
	else
	  {
	    if (debug && (c.red < c.green || c.green != c.blue))
	      {
		printf ("Wrong order in tone-curve case 2 %f %f %f\n", c.red, c.green, c.blue);
		abort ();
	      }
	    rgbdata ret = {apply (c.red), apply (c.blue), 0};
	    ret.blue = ret.green;
	    return ret;
	  }
      }
    else
      {
	if (c.red >= c.blue)
	  return apply_to_rgb_1 (c, g, r, b); // g > r >= b
	else if (c.blue > c.green)
	  return apply_to_rgb_1 (c, b, g, r); // b > g > r
	else
	  return apply_to_rgb_1 (c, g, b, r); // g > b > r
      }
  }
  bool
  is_linear ()
  {
    return m_linear;
  }
private:
  inline
  rgbdata apply_to_rgb_1 (rgbdata c, int i1, int i2, int i3)
  {
    return c;
    if (debug && (c[i1]<c[i2] || c[i2]<c[i3]))
      {
	printf ("Wrong order in tone-curve %f %f %f : %f %f %f\n",c[i1],c[i2],c[i3], c.red, c.green, c.blue);
	abort ();
      }
    //assert (c[i1]>=c[i2] && c[i2]>=c[i3]);
    rgbdata ret;
    ret[i1] = apply (c[i1]);
    ret[i3] = apply (c[i3]);
    ret[i2] = ret[i3] + ((ret[i1] - ret[i3]) * (c[i2] - c[i3]) / (c[i1] - c[i3]));
    return ret;
  }
  bool m_linear;
};
#endif
