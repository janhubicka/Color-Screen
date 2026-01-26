#ifndef SCR_DETECT_H
#define SCR_DETECT_H
#include "include/scr-detect-parameters.h"
#include "include/imagedata.h"
#include "render.h"
class progress_info;
namespace colorscreen {

class scr_detect
{
public:
  scr_detect ()
  : lookup_table {}
  {
  }
  ~scr_detect ();
  bool set_parameters (scr_detect_parameters param, luminosity_t gamma, const image_data *img, progress_info *progress = NULL);
  enum color_class
  {
    red,
    green,
    blue,
    unknown
  };
  inline
  void adjust_linearized_color (luminosity_t r, luminosity_t g, luminosity_t b,
				luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
  {
    m_color_adjust.apply_to_rgb (r, g, b, rr, gg, bb);
  }
  inline
  void adjust_color (int r, int g, int b,
		     luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
  {
    m_color_adjust.apply_to_rgb (lookup_table[0][r], lookup_table[1][g], lookup_table[2][b], rr, gg, bb);
  }
  inline pure_attr
  enum color_class classify_adjusted_color (luminosity_t r, luminosity_t g, luminosity_t b) const
  {
    luminosity_t ma = std::max (std::max (r, g), b);
    if (ma < m_param.min_luminosity)
      return unknown;
    luminosity_t m = std::min (std::min (std::min (r, g), b), (luminosity_t)0);
    r -= m;
    g -= m;
    b -= m;
    if (r > (fabs (g) + fabs(b)) * m_param.min_ratio && r > g && r > b)
      return red;
    if (g > (fabs(r) + fabs(b)) * m_param.min_ratio && g > r && g > b)
      return green;
    if (b > (fabs(r) + fabs(g)) * m_param.min_ratio && b > r && b > g)
      return blue;

    return unknown;
  }
  inline pure_attr
  enum color_class classify_color (int ir, int ig, int ib) const
  {
    luminosity_t r, g, b;
    adjust_color (ir, ig, ib, &r, &g, &b);
    return classify_adjusted_color (r, g, b);
  }
  scr_detect_parameters m_param;
private:
  color_matrix m_color_adjust;
  render::lookup_table_cache_t::cached_ptr lookup_table[3];
};

class color_class_map
{
public:
  color_class_map ()
  : data (NULL), width (0), height (0)
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
private:
};
}
#endif
