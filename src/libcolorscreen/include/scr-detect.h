#ifndef SCR_DETECT_H
#define SCR_DETECT_H
#include "dllpublic.h"
#include "color.h"
/* For coord_t.  */
#include "scr-to-img.h"

struct scr_detect_parameters
{
  scr_detect_parameters ()
  : gamma (2.2), black (0, 0,0), red(1, 0, 0), green(0, 1, 0), blue(0, 0, 1), min_luminosity (0.000), min_ratio (1)
  { }

  /* Gamma applied to image data before screen detection.  */
  luminosity_t gamma;
  /* Typical valus of red, green and blue dyes scaled to range (0,1) in the scan's gamma.  */
  color_t black, red, green, blue;
  /* Minimal luminosity for detection to be performed.  */
  luminosity_t min_luminosity;
  /* Determine dye as a given color if its luminosity is greater than ratio times the sum of luminosities of the other two colors.  */
  luminosity_t min_ratio;
  bool operator== (scr_detect_parameters &other) const
  {
    return gamma == gamma
	   && black == other.black
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
  void set_parameters (scr_detect_parameters param, int maxval);
  enum color_class
  {
    red,
    green,
    blue,
    unknown
  };
  void adjust_color (int r, int g, int b,
		     luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
  {
    m_color_adjust.apply_to_rgb (lookup_table[r], lookup_table[g], lookup_table[b], rr, gg, bb);
  }
  enum color_class classify_color (int ir, int ig, int ib)
  {
    luminosity_t r, g, b;

    m_color_adjust.apply_to_rgb (lookup_table[ir], lookup_table[ig], lookup_table[ib], &r, &g, &b);
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
  scr_detect_parameters m_param;
private:
  color_matrix m_color_adjust;
  luminosity_t *lookup_table;
};

class color_class_map
{
public:
  color_class_map ()
  : data (NULL), width (0), height (0), id(last_id++)
  {
  }
  void
  allocate (int x, int y)
  {
    if (data)
      abort ();
    data = (unsigned char *)calloc (x * y + 3 / 4, 1);
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
    unsigned int pos = x + y * width;
    unsigned char cc = ((unsigned char)c) << ((pos % 4u) * 2);
    unsigned char cm = ~(((unsigned char)3) << ((pos % 4u) * 2));
    data[pos / 4u] &= cm;
    data[pos / 4u] |= cc;
  }
  scr_detect::color_class
  get_class (int x, int y)
  {
    if (x < 0 || y < 0 || x >= width || y>= height)
      return scr_detect::unknown;
    unsigned int pos = x + y * width;
    return (scr_detect::color_class)((data[pos / 4u] >> ((pos % 4u) * 2)) & 3);
  }
  void
  get_color (int x, int y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    static luminosity_t rgbtable[4][3] = {{1, 0, 0},
					  {0, 1, 0},
					  {0, 0, 1},
					  {0, 0, 0}};
    scr_detect::color_class t = get_class (x, y);
    *r = rgbtable[(int)t][0];
    *g = rgbtable[(int)t][1];
    *b = rgbtable[(int)t][2];
  }
  luminosity_t
  get_color_red (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::red /*|| t == scr_detect::unknown*/;
  }
  luminosity_t
  get_color_green (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::green /*|| t == scr_detect::unknown*/;
  }
  luminosity_t
  get_color_blue (int x, int y)
  {
    scr_detect::color_class t = get_class (x, y);
    return t == scr_detect::blue /*|| t == scr_detect::unknown*/;
  }
  unsigned char *data;
  int width, height;
  int id;
private:
  static int last_id;
};

#endif
