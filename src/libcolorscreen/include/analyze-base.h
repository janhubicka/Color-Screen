#ifndef ANALYZE_BASE_H
#define ANALYZE_BASE_H
#include "scr-to-img.h"
#include "color.h"
#include "progress-info.h"
#include "bitmap.h"
class analyze_base
{
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

  virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
  void analyze_range (luminosity_t *rrmin, luminosity_t *rrmax, luminosity_t *rgmin, luminosity_t *rgmax, luminosity_t *rbmin, luminosity_t *rbmax);
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
protected:
  /* Scales of R G and B tables as shifts.  I.e. 0 = one etry per screen period, 2 = two entries.  */
  analyze_base (int rwscl, int rhscl, int gwscl, int ghscl, int bwscl, int bhscl)
  : m_rwscl (rwscl), m_rhscl (rhscl), m_gwscl (gwscl), m_ghscl (ghscl), m_bwscl (bwscl), m_bhscl (bhscl),
    m_xshift (0), m_yshift (0), m_width (0), m_height (0), m_red (0), m_green (0), m_blue (0),  m_rgb_red (0), m_rgb_green (0), m_rgb_blue (0), m_known_pixels (NULL), m_n_known_pixels (0),
    m_contrast (NULL)
  {
  }
  virtual
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
#endif
