#ifndef SCR_DETECT_H
#define SCR_DETECT_H
#include "dllpublic.h"
#include "color.h"
/* For coord_t.  */
#include "scr-to-img.h"

struct scr_detect_parameters
{
  scr_detect_parameters ()
  : black (0, 0,0), red(1, 0, 0), green(0, 1, 0), blue(0, 0, 1), min_luminosity (0.001), min_ratio (1)
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
  void adjust_color (luminosity_t r, luminosity_t g, luminosity_t b,
		     luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
  {
    m_color_adjust.apply_to_rgb (r, g, b, rr, gg, bb);
  }
  enum color_class classify_color (luminosity_t r, luminosity_t g, luminosity_t b)
  {
    m_color_adjust.apply_to_rgb (r, g, b, &r, &g, &b);
    if (r * r + b * b + g * g < m_param.min_luminosity * m_param.min_luminosity)
      return unknown;
    if (r > (fabs (g) + fabs(b)) * m_param.min_ratio)
      return red;
    if (g > (fabs(r) + fabs(b)) * m_param.min_ratio)
      return green;
    if (b > (fabs(r) + fabs(g)) * m_param.min_ratio)
      return blue;
    return unknown;
  }
private:
  scr_detect_parameters m_param;
  color_matrix m_color_adjust;
};

class color_class_map
{
public:
  color_class_map ()
  : data (NULL)
  {
  }
  void
  allocate (int x, int y)
  {
    if (data)
      abort ();
    data = (unsigned char *)calloc (x * y, 1);
    width = x;
    height = y;
    if (!data)
      abort ();
  }
  ~color_class_map ()
  {
    if (data)
      free (data);
  }
  void
  set_class (int x, int y, scr_detect::color_class c)
  {
    data[x + y * width] = c;
  }
  scr_detect::color_class
  get_class (int x, int y)
  {
    if (x < 0 || y < 0 || x >= width || y>= height)
      return scr_detect::unknown;
    return (scr_detect::color_class)data[x + y * width];
  }
  void
  get_color (int x, int y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    static luminosity_t rgbtable[4][3] = {{1, 0, 0},
					  {0, 1, 0},
					  {0, 0, 1},
					  {1, 1, 1}};
    scr_detect::color_class t = get_class (x, y);
    *r = rgbtable[(int)t][0];
    *g = rgbtable[(int)t][1];
    *b = rgbtable[(int)t][2];
  }
  luminosity_t
  get_color_red (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::red || t == scr_detect::unknown;
  }
  luminosity_t
  get_color_green (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::green || t == scr_detect::unknown;
  }
  luminosity_t
  get_color_blue (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::blue || t == scr_detect::unknown;
  }
  unsigned char *data;
  int width, height;
};

#endif
