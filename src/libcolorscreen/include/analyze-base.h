#ifndef ANALYZE_BASE_H
#define ANALYZE_BASE_H
#include "render-to-scr.h"
#include "scr-to-img.h"
#include "color.h"
#include "progress-info.h"
#include "bitmap.h"
#include "screen.h"
class DLL_PUBLIC analyze_base
{
protected:
  static constexpr const bool debug = false;
public:
  struct contrast_info
  {
    luminosity_t min;
    luminosity_t max;
  };
  enum mode
  {
    fast,
    precise,
    color,
    precise_rgb
  };
  void set_known_pixels (bitmap_2d *bitmap)
  {
    assert (!m_known_pixels && !m_n_known_pixels);
    m_known_pixels = bitmap;
    for (int y = 0; y < m_height; y++)
      for (int x = 0; x < m_width; x++)
	if (bitmap->test_bit (x,y))
	  m_n_known_pixels++;
  }
  luminosity_t &blue (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_height << m_bhscl) - 1);
      return m_blue [y * (m_width << m_bwscl) + x];
    }
  luminosity_t &red (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_height << m_rhscl) - 1);
      return m_red [y * (m_width << m_rwscl) + x];
    }
  luminosity_t &green (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_height << m_bhscl) - 1);
      return m_green [y * (m_width << m_gwscl) + x];
    }

  rgbdata &rgb_blue (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_height << m_bhscl) - 1);
      return m_rgb_blue [y * (m_width << m_bwscl) + x];
    }
  rgbdata &rgb_red (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_height << m_rhscl) - 1);
      return m_rgb_red [y * (m_width << m_rwscl) + x];
    }
  rgbdata &rgb_green (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_height << m_bhscl) - 1);
      return m_rgb_green [y * (m_width << m_gwscl) + x];
    }
  luminosity_t blue_avg (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width) - 1);
      y = std::min (std::max (y, 0), (m_height) - 1);
      if (!m_bhscl)
	{
	  if (!m_bwscl)
	    return m_blue [y * m_width + x];
	  else
	    return (m_blue [y * m_width * 2 + 2 * x] + m_blue [y * m_width * 2 + 2 * x + 1]) * 0.5;
	}
      else
	{
	  if (!m_bwscl)
	    return (m_blue [2 * y * m_width + x] + m_blue [2 * (y + 1) * m_width + x]) * 0.5;
	  else
	    return (m_blue [4 * y * m_width + 2 * x] + m_blue [4 * y * m_width + 2 * x + 1]
	            + m_blue [2 * (2 * y + 1) * m_width + 2 * x] + m_blue [2 * (2 * y + 1) * m_width + 2 * x + 1]) * 0.25;
	}
    }
  luminosity_t red_avg (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width) - 1);
      y = std::min (std::max (y, 0), (m_height) - 1);
      if (!m_rhscl)
	{
	  if (!m_rwscl)
	    return m_red [y * m_width + x];
	  else
	    return (m_red [y * m_width * 2 + 2 * x] + m_red [y * m_width * 2 + 2 * x + 1]) * 0.5;
	}
      else
	{
	  if (!m_rwscl)
	    return (m_red [2 * y * m_width + x] + m_red [2 * (y + 1) * m_width + x]) * 0.5;
	  else
	    return (m_red [4 * y * m_width + 2 * x] + m_red [4 * y * m_width + 2 * x + 1]
	            + m_red [2 * (2 * y + 1) * m_width + 2 * x] + m_red [2 * (2 * y + 1) * m_width + 2 * x + 1]) * 0.25;
	}
    }

  luminosity_t green_avg (int x, int y)
    {
      x = std::min (std::max (x, 0), (m_width << m_gwscl) - 1);
      y = std::min (std::max (y, 0), (m_height << m_ghscl) - 1);
      if (!m_ghscl)
	{
	  if (!m_gwscl)
	    return m_green [y * m_width + x];
	  else
	    return (m_green [y * m_width * 2 + 2 * x] + m_green [y * m_width * 2 + 2 * x + 1]) * 0.5;
	}
      else
	{
	  if (!m_gwscl)
	    return (m_green [2 * y * m_width + x] + m_green [2 * (y + 1) * m_width + x]) * 0.5;
	  else
	    return (m_green [4 * y * m_width + 2 * x] + m_green [4 * y * m_width + 2 * x + 1]
	            + m_green [2 * (2 * y + 1) * m_width + 2 * x] + m_green [2 * (2 * y + 1) * m_width + 2 * x + 1]) * 0.25;
	}
    }
  int get_xshift ()
  {
    return m_xshift;
  }
  int get_yshift ()
  {
    return m_yshift;
  }
  int get_width ()
  {
    return m_width;
  }
  int get_height ()
  {
    return m_height;
  }
  contrast_info &get_contrast (int x, int y)
  {
    return m_contrast[y * m_width + x];
  }

  DLL_PUBLIC virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
  DLL_PUBLIC void analyze_range (luminosity_t *rrmin, luminosity_t *rrmax, luminosity_t *rgmin, luminosity_t *rgmax, luminosity_t *rbmin, luminosity_t *rbmax);
  DLL_PUBLIC virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
  struct data_entry {
    int64_t x,y;
    pure_attr inline data_entry operator+(const data_entry other)
    {
      return {x + other.x, y + other.y};
    }
  };
protected:
  /* Scales of R G and B tables as shifts.  I.e. 0 = one etry per screen period, 2 = two entries.  */
  DLL_PUBLIC_EXP analyze_base (int rwscl, int rhscl, int gwscl, int ghscl, int bwscl, int bhscl)
  : m_rwscl (rwscl), m_rhscl (rhscl), m_gwscl (gwscl), m_ghscl (ghscl), m_bwscl (bwscl), m_bhscl (bhscl),
    m_xshift (0), m_yshift (0), m_width (0), m_height (0), m_red (0), m_green (0), m_blue (0),  m_rgb_red (0), m_rgb_green (0), m_rgb_blue (0), m_known_pixels (NULL), m_n_known_pixels (0),
    m_contrast (NULL)
  {
  }
  DLL_PUBLIC_EXP virtual
  ~analyze_base()
  {
    free (m_red);
    free (m_green);
    free (m_blue);
    free (m_rgb_red);
    free (m_rgb_green);
    free (m_rgb_blue);
    free (m_contrast);
    if (m_known_pixels)
      delete m_known_pixels;
  }
  bool find_best_match_using_cpfind (analyze_base &other, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, int scale, FILE *report_file, progress_info *progress);
  int m_rwscl;
  int m_rhscl;
  int m_gwscl;
  int m_ghscl;
  int m_bwscl;
  int m_bhscl;
  int m_xshift, m_yshift, m_width, m_height;
  luminosity_t *m_red, *m_green, *m_blue;
  rgbdata *m_rgb_red, *m_rgb_green, *m_rgb_blue;
  bitmap_2d *m_known_pixels;
  int m_n_known_pixels;
  struct contrast_info *m_contrast;
};


template<typename GEOMETRY>
class DLL_PUBLIC analyze_base_worker : public analyze_base
{
public:
  DLL_PUBLIC_EXP analyze_base_worker (int rwscl, int rhscl, int gwscl, int ghscl, int bwscl, int bhscl)
  /* We store two reds per X coordinate.  */
  : analyze_base (rwscl, rhscl, gwscl, ghscl, bwscl, bhscl)
  {
  }
  DLL_PUBLIC inline pure_attr rgbdata bicubic_bw_interpolate (point_t scr);
  DLL_PUBLIC inline pure_attr rgbdata bicubic_rgb_interpolate (point_t scr, rgbdata patch_proportions);
  DLL_PUBLIC inline pure_attr rgbdata bicubic_interpolate (point_t scr, rgbdata patch_proportions);

  /* Accessors for color data; since width scales are compile time constants they will work faster then one from analyse_base.  */
  DLL_PUBLIC inline luminosity_t &
  red (int x, int y) const
  {
    x = std::min (std::max (x, 0), m_width * GEOMETRY::red_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::red_height_scale - 1);
    return m_red [y * m_width * GEOMETRY::red_width_scale + x];
  }

  DLL_PUBLIC inline luminosity_t &
  green (int x, int y) const
  {
    x = std::min (std::max (x, 0), m_width * GEOMETRY::green_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::green_height_scale - 1);
    return m_green [y * m_width * GEOMETRY::green_width_scale + x];
  }

  DLL_PUBLIC inline luminosity_t &
  blue (int x, int y) const
  {
    x = std::min (std::max (x, 0), m_width * GEOMETRY::blue_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::blue_height_scale - 1);
    return m_blue [y * m_width * GEOMETRY::blue_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  rgb_red (int x, int y) const
  { 
    x = std::min (std::max (x, 0), m_width * GEOMETRY::red_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::red_height_scale - 1);
    return m_rgb_red [y * m_width * GEOMETRY::red_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  rgb_green (int x, int y) const
  { 
    x = std::min (std::max (x, 0), m_width * GEOMETRY::green_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::green_height_scale - 1);
    return m_rgb_green [y * m_width * GEOMETRY::green_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  rgb_blue (int x, int y) const
  { 
    x = std::min (std::max (x, 0), m_width * GEOMETRY::blue_width_scale - 1);
    y = std::min (std::max (y, 0), m_height * GEOMETRY::blue_height_scale - 1);
    return m_rgb_blue [y * m_width * GEOMETRY::blue_width_scale + x];
  }

  /* Same accessors bug w/o bounds checking.  */
  DLL_PUBLIC inline luminosity_t &
  fast_red (int x, int y) const
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::red_width_scale && y < m_height * GEOMETRY::red_height_scale));
    return m_red [y * m_width * GEOMETRY::red_width_scale + x];
  }

  DLL_PUBLIC inline luminosity_t &
  fast_green (int x, int y) const
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::green_width_scale && y < m_height * GEOMETRY::green_height_scale));
    return m_green [y * m_width * GEOMETRY::green_width_scale + x];
  }

  DLL_PUBLIC inline luminosity_t &
  fast_blue (int x, int y) const
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::blue_width_scale && y < m_height * GEOMETRY::blue_height_scale));
    return m_blue [y * m_width * GEOMETRY::blue_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  fast_rgb_red (int x, int y) const
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::red_width_scale && y < m_height * GEOMETRY::red_height_scale));
    return m_rgb_red [y * m_width * GEOMETRY::red_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  fast_rgb_green (int x, int y) const
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::green_width_scale && y < m_height * GEOMETRY::green_height_scale));
    return m_rgb_green [y * m_width * GEOMETRY::green_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata &
  fast_rgb_blue (int x, int y) const
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_width * GEOMETRY::blue_width_scale && y < m_height * GEOMETRY::blue_height_scale));
    return m_rgb_blue [y * m_width * GEOMETRY::blue_width_scale + x];
  }

  DLL_PUBLIC inline rgbdata
  screen_tile_color (int x, int y)
  {
    rgbdata ret = {0,0,0};
    for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::red_width_scale; xx++)
	ret.red += fast_red (x * GEOMETRY::red_width_scale + xx, y * GEOMETRY::red_height_scale + yy);
    ret.red *= (1.0 / (GEOMETRY::red_height_scale * GEOMETRY::red_width_scale));
    for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::green_width_scale; xx++)
	ret.green += fast_green (x * GEOMETRY::green_width_scale + xx, y * GEOMETRY::green_height_scale + yy);
    ret.green *= (1.0 / (GEOMETRY::green_height_scale * GEOMETRY::green_width_scale));
    for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::blue_width_scale; xx++)
	ret.blue += fast_blue (x * GEOMETRY::blue_width_scale + xx, y * GEOMETRY::blue_height_scale + yy);
    ret.blue *= (1.0 / (GEOMETRY::blue_height_scale * GEOMETRY::blue_width_scale));
    return ret;
  }

  DLL_PUBLIC inline void
  screen_tile_rgb_color (rgbdata &red, rgbdata &green, rgbdata &blue, int x, int y)
  {
    red = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::red_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::red_height_scale; xx++)
	red += fast_rgb_red (x * GEOMETRY::red_width_scale + xx, y * GEOMETRY::red_height_scale + yy);
    red *= (1.0 / (GEOMETRY::red_height_scale * GEOMETRY::red_width_scale));
    green = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::green_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::green_height_scale; xx++)
	green += fast_rgb_green (x * GEOMETRY::green_width_scale + xx, y * GEOMETRY::green_height_scale + yy);
    green *= (1.0 / (GEOMETRY::green_height_scale * GEOMETRY::green_width_scale));
    blue = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::blue_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::blue_height_scale; xx++)
	blue += fast_rgb_blue (x * GEOMETRY::blue_width_scale + xx, y * GEOMETRY::blue_height_scale + yy);
    blue *= (1.0 / (GEOMETRY::blue_height_scale * GEOMETRY::blue_width_scale));
  }
  DLL_PUBLIC bool analyze (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, const screen *screen, int width, int height, int xshift, int yshift, mode mode, luminosity_t collection_threshold, progress_info *progress);
protected:
  DLL_PUBLIC bool analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  DLL_PUBLIC bool analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  DLL_PUBLIC bool analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  DLL_PUBLIC bool analyze_fast (render_to_scr *render,progress_info *progress);

};
template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::bicubic_rgb_interpolate (point_t scr, rgbdata patch_proportions)
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 2, green_maxx = 2, green_maxy = 2, blue_maxx = 2, blue_maxy = 2;

  scr.x += m_xshift;
  scr.y += m_yshift;
  point_t off;

  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  rgbdata intred = {0, 0, 0};
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_rgb_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      intred.red = cubic_interpolate (cubic_interpolate (get_red_p (-1, -1).red, get_red_p (-1, 0).red, get_red_p (-1, 1).red, get_red_p (-1, 2).red, off.y),
				      cubic_interpolate (get_red_p ( 0, -1).red, get_red_p ( 0, 0).red, get_red_p ( 0, 1).red, get_red_p ( 0, 2).red, off.y),
				      cubic_interpolate (get_red_p ( 1, -1).red, get_red_p ( 1, 0).red, get_red_p ( 1, 1).red, get_red_p ( 1, 2).red, off.y),
				      cubic_interpolate (get_red_p ( 2, -1).red, get_red_p ( 2, 0).red, get_red_p ( 2, 1).red, get_red_p ( 2, 2).red, off.y), off.x);
      intred.green = cubic_interpolate (cubic_interpolate (get_red_p (-1, -1).green, get_red_p (-1, 0).green, get_red_p (-1, 1).green, get_red_p (-1, 2).green, off.y),
				        cubic_interpolate (get_red_p ( 0, -1).green, get_red_p ( 0, 0).green, get_red_p ( 0, 1).green, get_red_p ( 0, 2).green, off.y),
				        cubic_interpolate (get_red_p ( 1, -1).green, get_red_p ( 1, 0).green, get_red_p ( 1, 1).green, get_red_p ( 1, 2).green, off.y),
				        cubic_interpolate (get_red_p ( 2, -1).green, get_red_p ( 2, 0).green, get_red_p ( 2, 1).green, get_red_p ( 2, 2).green, off.y), off.x);
      intred.blue = cubic_interpolate (cubic_interpolate (get_red_p (-1, -1).blue, get_red_p (-1, 0).blue, get_red_p (-1, 1).blue, get_red_p (-1, 2).blue, off.y),
				       cubic_interpolate (get_red_p ( 0, -1).blue, get_red_p ( 0, 0).blue, get_red_p ( 0, 1).blue, get_red_p ( 0, 2).blue, off.y),
				       cubic_interpolate (get_red_p ( 1, -1).blue, get_red_p ( 1, 0).blue, get_red_p ( 1, 1).blue, get_red_p ( 1, 2).blue, off.y),
				       cubic_interpolate (get_red_p ( 2, -1).blue, get_red_p ( 2, 0).blue, get_red_p ( 2, 1).blue, get_red_p ( 2, 2).blue, off.y), off.x);
#undef get_red_p
    }
  rgbdata intgreen = {0, 0, 0};
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_rgb_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      intgreen.red = cubic_interpolate (cubic_interpolate (get_green_p (-1, -1).red, get_green_p (-1, 0).red, get_green_p (-1, 1).red, get_green_p (-1, 2).red, off.y),
					cubic_interpolate (get_green_p ( 0, -1).red, get_green_p ( 0, 0).red, get_green_p ( 0, 1).red, get_green_p ( 0, 2).red, off.y),
					cubic_interpolate (get_green_p ( 1, -1).red, get_green_p ( 1, 0).red, get_green_p ( 1, 1).red, get_green_p ( 1, 2).red, off.y),
					cubic_interpolate (get_green_p ( 2, -1).red, get_green_p ( 2, 0).red, get_green_p ( 2, 1).red, get_green_p ( 2, 2).red, off.y), off.x);
      intgreen.green = cubic_interpolate (cubic_interpolate (get_green_p (-1, -1).green, get_green_p (-1, 0).green, get_green_p (-1, 1).green, get_green_p (-1, 2).green, off.y),
					  cubic_interpolate (get_green_p ( 0, -1).green, get_green_p ( 0, 0).green, get_green_p ( 0, 1).green, get_green_p ( 0, 2).green, off.y),
					  cubic_interpolate (get_green_p ( 1, -1).green, get_green_p ( 1, 0).green, get_green_p ( 1, 1).green, get_green_p ( 1, 2).green, off.y),
					  cubic_interpolate (get_green_p ( 2, -1).green, get_green_p ( 2, 0).green, get_green_p ( 2, 1).green, get_green_p ( 2, 2).green, off.y), off.x);
      intgreen.blue = cubic_interpolate (cubic_interpolate (get_green_p (-1, -1).blue, get_green_p (-1, 0).blue, get_green_p (-1, 1).blue, get_green_p (-1, 2).blue, off.y),
					 cubic_interpolate (get_green_p ( 0, -1).blue, get_green_p ( 0, 0).blue, get_green_p ( 0, 1).blue, get_green_p ( 0, 2).blue, off.y),
					 cubic_interpolate (get_green_p ( 1, -1).blue, get_green_p ( 1, 0).blue, get_green_p ( 1, 1).blue, get_green_p ( 1, 2).blue, off.y),
					 cubic_interpolate (get_green_p ( 2, -1).blue, get_green_p ( 2, 0).blue, get_green_p ( 2, 1).blue, get_green_p ( 2, 2).blue, off.y), off.x);
#undef get_green_p
    }
  rgbdata intblue = {0, 0, 0};
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_rgb_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      intblue.red = cubic_interpolate (cubic_interpolate (get_blue_p (-1, -1).red, get_blue_p (-1, 0).red, get_blue_p (-1, 1).red, get_blue_p (-1, 2).red, off.y),
				       cubic_interpolate (get_blue_p ( 0, -1).red, get_blue_p ( 0, 0).red, get_blue_p ( 0, 1).red, get_blue_p ( 0, 2).red, off.y),
				       cubic_interpolate (get_blue_p ( 1, -1).red, get_blue_p ( 1, 0).red, get_blue_p ( 1, 1).red, get_blue_p ( 1, 2).red, off.y),
				       cubic_interpolate (get_blue_p ( 2, -1).red, get_blue_p ( 2, 0).red, get_blue_p ( 2, 1).red, get_blue_p ( 2, 2).red, off.y), off.x);
      intblue.green = cubic_interpolate (cubic_interpolate (get_blue_p (-1, -1).green, get_blue_p (-1, 0).green, get_blue_p (-1, 1).green, get_blue_p (-1, 2).green, off.y),
					 cubic_interpolate (get_blue_p ( 0, -1).green, get_blue_p ( 0, 0).green, get_blue_p ( 0, 1).green, get_blue_p ( 0, 2).green, off.y),
					 cubic_interpolate (get_blue_p ( 1, -1).green, get_blue_p ( 1, 0).green, get_blue_p ( 1, 1).green, get_blue_p ( 1, 2).green, off.y),
					 cubic_interpolate (get_blue_p ( 2, -1).green, get_blue_p ( 2, 0).green, get_blue_p ( 2, 1).green, get_blue_p ( 2, 2).green, off.y), off.x);
      intblue.blue = cubic_interpolate (cubic_interpolate (get_blue_p (-1, -1).blue, get_blue_p (-1, 0).blue, get_blue_p (-1, 1).blue, get_blue_p (-1, 2).blue, off.y),
					cubic_interpolate (get_blue_p ( 0, -1).blue, get_blue_p ( 0, 0).blue, get_blue_p ( 0, 1).blue, get_blue_p ( 0, 2).blue, off.y),
					cubic_interpolate (get_blue_p ( 1, -1).blue, get_blue_p ( 1, 0).blue, get_blue_p ( 1, 1).blue, get_blue_p ( 1, 2).blue, off.y),
					cubic_interpolate (get_blue_p ( 2, -1).blue, get_blue_p ( 2, 0).blue, get_blue_p ( 2, 1).blue, get_blue_p ( 2, 2).blue, off.y), off.x);
#undef get_blue_p
    }

  return intred * patch_proportions.red + intgreen * patch_proportions.green + intblue * patch_proportions.blue;
}

template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::bicubic_bw_interpolate (point_t scr)
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 2, green_maxx = 2, green_maxy = 2, blue_maxx = 2, blue_maxy = 2;
  rgbdata ret = {1,0,0};

  scr.x += m_xshift;
  scr.y += m_yshift;
  point_t off;
  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      ret.red = cubic_interpolate (cubic_interpolate (get_red_p (-1, -1), get_red_p (-1, 0), get_red_p (-1, 1), get_red_p (-1, 2), off.y),
				   cubic_interpolate (get_red_p ( 0, -1), get_red_p ( 0, 0), get_red_p ( 0, 1), get_red_p ( 0, 2), off.y),
				   cubic_interpolate (get_red_p ( 1, -1), get_red_p ( 1, 0), get_red_p ( 1, 1), get_red_p ( 1, 2), off.y),
				   cubic_interpolate (get_red_p ( 2, -1), get_red_p ( 2, 0), get_red_p ( 2, 1), get_red_p ( 2, 2), off.y), off.x);
#undef get_red_p
    }
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      ret.green = cubic_interpolate (cubic_interpolate (get_green_p (-1, -1), get_green_p (-1, 0), get_green_p (-1, 1), get_green_p (-1, 2), off.y),
				     cubic_interpolate (get_green_p ( 0, -1), get_green_p ( 0, 0), get_green_p ( 0, 1), get_green_p ( 0, 2), off.y),
				     cubic_interpolate (get_green_p ( 1, -1), get_green_p ( 1, 0), get_green_p ( 1, 1), get_green_p ( 1, 2), off.y),
				     cubic_interpolate (get_green_p ( 2, -1), get_green_p ( 2, 0), get_green_p ( 2, 1), get_green_p ( 2, 2), off.y), off.x);
#undef get_green_p
    }
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      ret.blue = cubic_interpolate (cubic_interpolate (get_blue_p (-1, -1), get_blue_p (-1, 0), get_blue_p (-1, 1), get_blue_p (-1, 2), off.y),
				    cubic_interpolate (get_blue_p ( 0, -1), get_blue_p ( 0, 0), get_blue_p ( 0, 1), get_blue_p ( 0, 2), off.y),
				    cubic_interpolate (get_blue_p ( 1, -1), get_blue_p ( 1, 0), get_blue_p ( 1, 1), get_blue_p ( 1, 2), off.y),
				    cubic_interpolate (get_blue_p ( 2, -1), get_blue_p ( 2, 0), get_blue_p ( 2, 1), get_blue_p ( 2, 2), off.y), off.x);
#undef get_blue_p
    }
  return ret;
}
template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::bicubic_interpolate (point_t scr, rgbdata patch_proportions)
{
  if (m_red)
    return bicubic_bw_interpolate (scr);
  else
    return bicubic_rgb_interpolate (scr, patch_proportions);
}
#endif
