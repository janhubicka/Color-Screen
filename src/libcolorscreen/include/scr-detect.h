#ifndef SCR_DETECT_H
#define SCR_DETECT_H
#include "dllpublic.h"
#include "color.h"

struct scr_detect_parameters
{
  scr_detect_parameters ()
  : black (0, 0,0), red(1, 0, 0), green(0, 1, 0), blue(0, 0, 1), min_luminosity (0.001), min_ratio (4)
  { }

  color_t black, red, green, blue;
  luminosity_t min_luminosity;
  luminosity_t min_ratio;
  bool operator== (scr_detect_parameters &other) const
  {
    return black == other.black
	   && red == other.red
	   && green == other.green
	   && blue == other.blue;
  }
  bool operator!= (scr_detect_parameters &other) const
  {
    return !(*this == other);
  }
};

class scr_detect
{
public:
  void set_parameters (scr_detect_parameters param);
  enum color_class
  {
    red,
    green,
    blue,
    unknown
  };
  enum color_class classify_color (luminosity_t r, luminosity_t g, luminosity_t b)
  {
    m_color_adjust.apply_to_rgb (r, g, b, &r, &g, &b);
    if (r * r + b * b + g * g < m_param.min_luminosity * m_param.min_luminosity)
      return unknown;
    if (r > (g + b) * m_param.min_ratio)
      return red;
    if (g > (r + b) * m_param.min_ratio)
      return green;
    if (b > (r + g) * m_param.min_ratio)
      return blue;
    return unknown;
  }
private:
  scr_detect_parameters m_param;
  color_matrix m_color_adjust;
};

#endif
