/* Core functionality for color screen analysis.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef ANALYZE_BASE_H
#define ANALYZE_BASE_H
#include <memory>
#include <cassert>
#include "include/color.h"
#include "include/progress-info.h"
#include "include/scr-to-img.h"
#include "render-to-scr.h"
#include "screen.h"
#include "bitmap.h"
#include "lanczos.h"

namespace colorscreen {

/* Base class for color screen analysis.
   Analyzes image data to determine properties of the color screen patches.  */
class analyze_base
{
protected:
  /* Enable debug checks if COLORSCREEN_CHECKING is defined.  */
  static constexpr bool debug = colorscreen_checking;
public:
  /* Information about min and max luminosity in a region.  */
  struct contrast_info
  {
    luminosity_t min;
    luminosity_t max;
  };
  /* Analysis modes.  */
  enum mode
  {
    /* Fast analysis from patch centers.  */
    fast,
    /* Precise analysis collecting all pixels.  */
    precise,
    /* Analysis of original scanner colors.  */
    color,
    /* Precise analysis in RGB scanner color space.  */
    precise_rgb
  };

  /* Set the bitmap of pixels with known values.
     BITMAP is the bitmap to use.  */
  void set_known_pixels (bitmap_2d *bitmap)
  {
    assert (!m_known_pixels && !m_n_known_pixels);
    m_known_pixels.reset (bitmap);
    for (int y = 0; y < m_area.height; y++)
      for (int x = 0; x < m_area.width; x++)
	if (bitmap->test_bit (x, y))
	  m_n_known_pixels++;
  }

  /* Access blue channel luminosity at (X, Y).  */
  pure_attr inline luminosity_t & blue (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_blue [y * (m_area.width << m_bwscl) + x];
    }

  /* Access blue channel luminosity at (X, Y) (const version).  */
  pure_attr inline luminosity_t blue (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_blue [y * (m_area.width << m_bwscl) + x];
    }

  /* Access red channel luminosity at (X, Y).  */
  pure_attr inline luminosity_t & red (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_rhscl) - 1);
      return m_red [y * (m_area.width << m_rwscl) + x];
    }

  /* Access red channel luminosity at (X, Y) (const version).  */
  pure_attr inline luminosity_t red (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_rhscl) - 1);
      return m_red [y * (m_area.width << m_rwscl) + x];
    }

  /* Access green channel luminosity at (X, Y).  */
  pure_attr inline luminosity_t & green (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_green [y * (m_area.width << m_gwscl) + x];
    }

  /* Access green channel luminosity at (X, Y) (const version).  */
  pure_attr inline luminosity_t green (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_green [y * (m_area.width << m_gwscl) + x];
    }

  /* Access blue channel RGB data at (X, Y).  */
  pure_attr inline rgbdata & rgb_blue (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_rgb_blue [y * (m_area.width << m_bwscl) + x];
    }

  /* Access blue channel RGB data at (X, Y) (const version).  */
  pure_attr inline rgbdata rgb_blue (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_bwscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_rgb_blue [y * (m_area.width << m_bwscl) + x];
    }

  /* Access red channel RGB data at (X, Y).  */
  pure_attr inline rgbdata & rgb_red (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_rhscl) - 1);
      return m_rgb_red [y * (m_area.width << m_rwscl) + x];
    }

  /* Access red channel RGB data at (X, Y) (const version).  */
  pure_attr inline rgbdata rgb_red (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_rwscl)  - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_rhscl) - 1);
      return m_rgb_red [y * (m_area.width << m_rwscl) + x];
    }

  /* Access green channel RGB data at (X, Y).  */
  pure_attr inline rgbdata & rgb_green (int x, int y) noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_rgb_green [y * (m_area.width << m_gwscl) + x];
    }

  /* Access green channel RGB data at (X, Y) (const version).  */
  pure_attr inline rgbdata rgb_green (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_ghscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_bhscl) - 1);
      return m_rgb_green [y * (m_area.width << m_gwscl) + x];
    }

  /* Return average blue channel luminosity at patch (X, Y).  */
  pure_attr inline luminosity_t blue_avg (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width) - 1);
      y = std::min (std::max (y, 0), (m_area.height) - 1);
      if (!m_bhscl)
	{
	  if (!m_bwscl)
	    return m_blue [y * m_area.width + x];
	  else
	    return (m_blue [y * m_area.width * 2 + 2 * x] + m_blue [y * m_area.width * 2 + 2 * x + 1]) * (luminosity_t) 0.5;
	}
      else
	{
	  if (!m_bwscl)
	    return (m_blue [2 * y * m_area.width + x] + m_blue [2 * (y + 1) * m_area.width + x]) * (luminosity_t) 0.5;
	  else
	    return (m_blue [4 * y * m_area.width + 2 * x] + m_blue [4 * y * m_area.width + 2 * x + 1]
	            + m_blue [2 * (2 * y + 1) * m_area.width + 2 * x] + m_blue [2 * (2 * y + 1) * m_area.width + 2 * x + 1]) * (luminosity_t) 0.25;
	}
    }

  /* Return average red channel luminosity at patch (X, Y).  */
  pure_attr inline luminosity_t red_avg (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width) - 1);
      y = std::min (std::max (y, 0), (m_area.height) - 1);
      if (!m_rhscl)
	{
	  if (!m_rwscl)
	    return m_red [y * m_area.width + x];
	  else
	    return (m_red [y * m_area.width * 2 + 2 * x] + m_red [y * m_area.width * 2 + 2 * x + 1]) * (luminosity_t) 0.5;
	}
      else
	{
	  if (!m_rwscl)
	    return (m_red [2 * y * m_area.width + x] + m_red [2 * (y + 1) * m_area.width + x]) * (luminosity_t) 0.5;
	  else
	    return (m_red [4 * y * m_area.width + 2 * x] + m_red [4 * y * m_area.width + 2 * x + 1]
	            + m_red [2 * (2 * y + 1) * m_area.width + 2 * x] + m_red [2 * (2 * y + 1) * m_area.width + 2 * x + 1]) * (luminosity_t) 0.25;
	}
    }

  /* Return average green channel luminosity at patch (X, Y).  */
  pure_attr inline luminosity_t green_avg (int x, int y) const noexcept
    {
      x = std::min (std::max (x, 0), (m_area.width << m_gwscl) - 1);
      y = std::min (std::max (y, 0), (m_area.height << m_ghscl) - 1);
      if (!m_ghscl)
	{
	  if (!m_gwscl)
	    return m_green [y * m_area.width + x];
	  else
	    return (m_green [y * m_area.width * 2 + 2 * x] + m_green [y * m_area.width * 2 + 2 * x + 1]) * (luminosity_t) 0.5;
	}
      else
	{
	  if (!m_gwscl)
	    return (m_green [2 * y * m_area.width + x] + m_green [2 * (y + 1) * m_area.width + x]) * (luminosity_t) 0.5;
	  else
	    return (m_green [4 * y * m_area.width + 2 * x] + m_green [4 * y * m_area.width + 2 * x + 1]
	            + m_green [2 * (2 * y + 1) * m_area.width + 2 * x] + m_green [2 * (2 * y + 1) * m_area.width + 2 * x + 1]) * (luminosity_t) 0.25;
	}
    }

  /* Return the analysis area.  */
  const_attr inline int_image_area get_area () const noexcept
  {
    return m_area;
  }

  /* Return contrast info for patch (X, Y).  */
  pure_attr inline contrast_info &get_contrast (int x, int y) noexcept
  {
    return m_contrast[y * m_area.width + x];
  }

  /* Return contrast info for patch (X, Y) (const version).  */
  pure_attr inline const contrast_info &get_contrast (int x, int y) const noexcept
  {
    return m_contrast[y * m_area.width + x];
  }

  /* Return area of the demosaiced image.  */
  virtual int_image_area demosaiced_area () const = 0;

  /* Find best match between this and OTHER scan.
     PERCENTAGE and MAX_PERCENTAGE define the required overlap.
     OTHER is the other scan to match against.
     CPFIND specifies whether to use cpfind tool.
     XSHIFT and YSHIFT are the resulting shifts.
     DIRECTION is the direction of matching.
     MAP and OTHER_MAP are the screen to image maps.
     REPORT_FILE is the file to write the report to.
     PROGRESS is the progress info object.  */
  virtual int find_best_match (int percentage, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = nullptr);

  /* Analyze the range of luminosity values.  */
  void analyze_range (luminosity_t *rrmin, luminosity_t *rrmax, luminosity_t *rgmin, luminosity_t *rgmax, luminosity_t *rbmin, luminosity_t *rbmax);

  /* Write the analyzed screen to a file.
     FILENAME is the name of the file to write to.
     KNOWN_PIXELS is the bitmap of known pixels.
     ERROR is the error message if any.
     PROGRESS is the progress info object.
     RMIN, RMAX, GMIN, GMAX, BMIN, BMAX define the luminosity range.  */
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = nullptr, luminosity_t rmin = (luminosity_t) 0, luminosity_t rmax = (luminosity_t) 1, luminosity_t gmin = (luminosity_t) 0, luminosity_t gmax = (luminosity_t) 1, luminosity_t bmin = (luminosity_t) 0, luminosity_t bmax = (luminosity_t) 1);

  typedef int_point_t data_entry;

  /* Destructor.  */
  virtual ~analyze_base() = default;

  /* Return screen width.  */
  int get_width () const { return m_area.width; }
  /* Return screen height.  */
  int get_height () const { return m_area.height; }
  /* Return X shift.  */
  int get_xshift () const { return m_area.xshift (); }
  /* Return Y shift.  */
  int get_yshift () const { return m_area.yshift (); }

protected:
  /* Minimum size for OpenMP parallelization.  */
  static constexpr size_t openmp_min_size = 128 * 1024;

  /* Initialize analyzer with scales.
     RWSCL, RHSCL, GWSCL, GHSCL, BWSCL, BHSCL are the scales for the channels.  */
  analyze_base (int rwscl, int rhscl, int gwscl, int ghscl, int bwscl, int bhscl)
  : m_rwscl (rwscl), m_rhscl (rhscl), m_gwscl (gwscl), m_ghscl (ghscl), m_bwscl (bwscl), m_bhscl (bhscl)
  {
  }

  /* Find best match using CPFIND tool.  */
  bool find_best_match_using_cpfind (analyze_base &other, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, int scale, FILE *report_file, progress_info *progress);

  int m_rwscl = 0;
  int m_rhscl = 0;
  int m_gwscl = 0;
  int m_ghscl = 0;
  int m_bwscl = 0;
  int m_bhscl = 0;
  int_image_area m_area;
  std::unique_ptr<luminosity_t[]> m_red, m_green, m_blue;
  std::unique_ptr<rgbdata[]> m_rgb_red, m_rgb_green, m_rgb_blue;
  std::unique_ptr<bitmap_2d> m_known_pixels;
  int m_n_known_pixels = 0;
  std::unique_ptr<contrast_info[]> m_contrast;
};


/* Worker class for analysis, templated by GEOMETRY.  */
template<typename GEOMETRY>
class analyze_base_worker : public analyze_base
{
public:
  /* Constructor.  */
  analyze_base_worker (int rwscl, int rhscl, int gwscl, int ghscl, int bwscl, int bhscl)
  : analyze_base (rwscl, rhscl, gwscl, ghscl, bwscl, bhscl)
  {
  }

  inline pure_attr rgbdata nearest_bw_interpolate (point_t scr) const;
  inline pure_attr rgbdata linear_bw_interpolate (point_t scr) const;
  inline pure_attr rgbdata bicubic_bw_interpolate (point_t scr) const;
  inline pure_attr rgbdata bicubic_rgb_interpolate (point_t scr, rgbdata patch_proportions) const;
  inline pure_attr rgbdata interpolate (point_t scr, rgbdata patch_proportions, render_parameters::screen_demosaic_t mode) const;

  /* Access red channel luminosity at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline luminosity_t & red (int x, int y) const noexcept
  {
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::red_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::red_height_scale - 1);
    return m_red [y * m_area.width * GEOMETRY::red_width_scale + x];
  }

  /* Access green channel luminosity at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline luminosity_t & green (int x, int y) const noexcept
  {
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::green_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::green_height_scale - 1);
    return m_green [y * m_area.width * GEOMETRY::green_width_scale + x];
  }

  /* Access blue channel luminosity at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline luminosity_t & blue (int x, int y) const noexcept
  {
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::blue_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::blue_height_scale - 1);
    return m_blue [y * m_area.width * GEOMETRY::blue_width_scale + x];
  }

  /* Access red channel RGB data at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline rgbdata & rgb_red (int x, int y) const noexcept
  { 
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::red_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::red_height_scale - 1);
    return m_rgb_red [y * m_area.width * GEOMETRY::red_width_scale + x];
  }

  /* Access green channel RGB data at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline rgbdata & rgb_green (int x, int y) const noexcept
  { 
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::green_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::green_height_scale - 1);
    return m_rgb_green [y * m_area.width * GEOMETRY::green_width_scale + x];
  }

  /* Access blue channel RGB data at (X, Y) with fixed scale from GEOMETRY.  */
  pure_attr inline rgbdata & rgb_blue (int x, int y) const noexcept
  { 
    x = std::min (std::max (x, 0), m_area.width * GEOMETRY::blue_width_scale - 1);
    y = std::min (std::max (y, 0), m_area.height * GEOMETRY::blue_height_scale - 1);
    return m_rgb_blue [y * m_area.width * GEOMETRY::blue_width_scale + x];
  }

  /* Access red channel luminosity at (X, Y) (fast, no bounds checking).  */
  pure_attr inline luminosity_t & fast_red (int x, int y) const noexcept
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::red_width_scale && y < m_area.height * GEOMETRY::red_height_scale));
    return m_red [y * m_area.width * GEOMETRY::red_width_scale + x];
  }

  /* Access green channel luminosity at (X, Y) (fast, no bounds checking).  */
  pure_attr inline luminosity_t & fast_green (int x, int y) const noexcept
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::green_width_scale && y < m_area.height * GEOMETRY::green_height_scale));
    return m_green [y * m_area.width * GEOMETRY::green_width_scale + x];
  }

  /* Access blue channel luminosity at (X, Y) (fast, no bounds checking).  */
  pure_attr inline luminosity_t & fast_blue (int x, int y) const noexcept
  {
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::blue_width_scale && y < m_area.height * GEOMETRY::blue_height_scale));
    return m_blue [y * m_area.width * GEOMETRY::blue_width_scale + x];
  }

  /* Access red channel RGB data at (X, Y) (fast, no bounds checking).  */
  pure_attr inline rgbdata & fast_rgb_red (int x, int y) const noexcept
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::red_width_scale && y < m_area.height * GEOMETRY::red_height_scale));
    return m_rgb_red [y * m_area.width * GEOMETRY::red_width_scale + x];
  }

  /* Access green channel RGB data at (X, Y) (fast, no bounds checking).  */
  pure_attr inline rgbdata & fast_rgb_green (int x, int y) const noexcept
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::green_width_scale && y < m_area.height * GEOMETRY::green_height_scale));
    return m_rgb_green [y * m_area.width * GEOMETRY::green_width_scale + x];
  }

  /* Access blue channel RGB data at (X, Y) (fast, no bounds checking).  */
  pure_attr inline rgbdata & fast_rgb_blue (int x, int y) const noexcept
  { 
    assert (!debug || (x >= 0 && y >= 0 && x < m_area.width * GEOMETRY::blue_width_scale && y < m_area.height * GEOMETRY::blue_height_scale));
    return m_rgb_blue [y * m_area.width * GEOMETRY::blue_width_scale + x];
  }

  /* Return the average color of a screen tile at (X, Y).  */
  inline rgbdata
  screen_tile_color (int x, int y) const
  {
    rgbdata ret = {0,0,0};
    for (int yy = 0; yy < GEOMETRY::red_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::red_width_scale; xx++)
	ret.red += fast_red (x * GEOMETRY::red_width_scale + xx, y * GEOMETRY::red_height_scale + yy);
    ret.red *= ((luminosity_t) 1.0 / (GEOMETRY::red_height_scale * GEOMETRY::red_width_scale));
    for (int yy = 0; yy < GEOMETRY::green_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::green_width_scale; xx++)
	ret.green += fast_green (x * GEOMETRY::green_width_scale + xx, y * GEOMETRY::green_height_scale + yy);
    ret.green *= ((luminosity_t) 1.0 / (GEOMETRY::green_height_scale * GEOMETRY::green_width_scale));
    for (int yy = 0; yy < GEOMETRY::blue_height_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::blue_width_scale; xx++)
	ret.blue += fast_blue (x * GEOMETRY::blue_width_scale + xx, y * GEOMETRY::blue_height_scale + yy);
    ret.blue *= ((luminosity_t) 1.0 / (GEOMETRY::blue_height_scale * GEOMETRY::blue_width_scale));
    return ret;
  }

  /* Calculate average RGB colors for a screen tile at (X, Y).  */
  inline void
  screen_tile_rgb_color (rgbdata &red_ret, rgbdata &green_ret, rgbdata &blue_ret, int x, int y) const
  {
    red_ret = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::red_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::red_height_scale; xx++)
	red_ret += fast_rgb_red (x * GEOMETRY::red_width_scale + xx, y * GEOMETRY::red_height_scale + yy);
    red_ret *= ((luminosity_t) 1.0 / (GEOMETRY::red_height_scale * GEOMETRY::red_width_scale));
    green_ret = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::green_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::green_height_scale; xx++)
	green_ret += fast_rgb_green (x * GEOMETRY::green_width_scale + xx, y * GEOMETRY::green_height_scale + yy);
    green_ret *= ((luminosity_t) 1.0 / (GEOMETRY::green_height_scale * GEOMETRY::green_width_scale));
    blue_ret = {0, 0, 0};
    for (int yy = 0; yy < GEOMETRY::blue_width_scale; yy++)
      for (int xx = 0; xx < GEOMETRY::blue_height_scale; xx++)
	blue_ret += fast_rgb_blue (x * GEOMETRY::blue_width_scale + xx, y * GEOMETRY::blue_height_scale + yy);
    blue_ret *= ((luminosity_t) 1.0 / (GEOMETRY::blue_height_scale * GEOMETRY::blue_width_scale));
  }

  /* Populate demosaiced data vector.  */
  bool
  populate_demosaiced_data (std::vector<rgbdata> &demosaic, render *r, int_image_area area, progress_info *progress);

  /* Return the demosaiced area.  */
  virtual int_image_area demosaiced_area () const override;

  /* Analyze the image data.  */
  bool analyze (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, const screen *screen, const simulated_screen *simulated, int_image_area area, mode mode, luminosity_t collection_threshold, progress_info *progress);

protected:
  /* Precise analysis collecting all pixels.  */
  bool analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, const simulated_screen *simulated, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int_image_area area, progress_info *progress);
  /* Precise analysis in RGB scanner color space.  */
  bool analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, const simulated_screen *simulated, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int_image_area area, progress_info *progress);
  /* Analysis of original scanner colors.  */
  bool analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int_image_area area, progress_info *progress);
  /* Fast analysis from patch centers.
     RENDER is the renderer.
     PROGRESS is the progress info object.  */
  bool analyze_fast (render_to_scr *render, progress_info *progress);

};

/* Linear interpolation between A and B with offset OFF.  */
inline luminosity_t linear_interpolate (luminosity_t a, luminosity_t b, luminosity_t off)
{
  return a * ((luminosity_t) 1 - off) + b * off;
}

/* 2D linear interpolation between V1, V2, V3, V4 with offset OFF.  */
inline flatten_attr pure_attr luminosity_t always_inline_attr
do_linear_interpolate (luminosity_t v1, luminosity_t v2, luminosity_t v3, luminosity_t v4, point_t off)
{
  luminosity_t a = linear_interpolate (v1,v2, (luminosity_t)off.x);
  luminosity_t b = linear_interpolate (v3,v4, (luminosity_t)off.x);
  return linear_interpolate (a,b, (luminosity_t)off.y);
}

template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::bicubic_rgb_interpolate (point_t scr, rgbdata patch_proportions) const
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 3, green_maxx = 2, green_maxy = 3, blue_maxx = 2, blue_maxy = 2;

  scr.x += (coord_t)m_area.xshift ();
  scr.y += (coord_t)m_area.yshift ();
  point_t off;

  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  rgbdata intred = {0, 0, 0};
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_area.width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_area.height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_rgb_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      intred.red = do_bicubic_interpolate ((vec_luminosity_t){get_red_p (-1, -1).red, get_red_p (0, -1).red, get_red_p (1, -1).red, get_red_p (2, -1).red},
				           (vec_luminosity_t){get_red_p (-1,  0).red, get_red_p (0,  0).red, get_red_p (1,  0).red, get_red_p (2,  0).red},
				           (vec_luminosity_t){get_red_p (-1,  1).red, get_red_p (0,  1).red, get_red_p (1,  1).red, get_red_p (2,  1).red},
				           (vec_luminosity_t){get_red_p (-1,  2).red, get_red_p (0,  2).red, get_red_p (1,  2).red, get_red_p (2,  2).red}, off);
      intred.green = do_bicubic_interpolate ((vec_luminosity_t){get_red_p (-1, -1).green, get_red_p (0, -1).green, get_red_p (1, -1).green, get_red_p (2, -1).green},
					     (vec_luminosity_t){get_red_p (-1,  0).green, get_red_p (0,  0).green, get_red_p (1,  0).green, get_red_p (2,  0).green},
					     (vec_luminosity_t){get_red_p (-1,  1).green, get_red_p (0,  1).green, get_red_p (1,  1).green, get_red_p (2,  1).green},
					     (vec_luminosity_t){get_red_p (-1,  2).green, get_red_p (0,  2).green, get_red_p (1,  2).green, get_red_p (2,  2).green}, off);
      intred.blue = do_bicubic_interpolate ((vec_luminosity_t){get_red_p (-1, -1).blue, get_red_p (0, -1).blue, get_red_p (1, -1).blue, get_red_p (2, -1).blue},
					    (vec_luminosity_t){get_red_p (-1,  0).blue, get_red_p (0,  0).blue, get_red_p (1,  0).blue, get_red_p (2,  0).blue},
					    (vec_luminosity_t){get_red_p (-1,  1).blue, get_red_p (0,  1).blue, get_red_p (1,  1).blue, get_red_p (2,  1).blue},
					    (vec_luminosity_t){get_red_p (-1,  2).blue, get_red_p (0,  2).blue, get_red_p (1,  2).blue, get_red_p (2,  2).blue}, off);
#undef get_red_p
    }
  rgbdata intgreen = {0, 0, 0};
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_area.width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_area.height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_rgb_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      intgreen.red = do_bicubic_interpolate ((vec_luminosity_t){get_green_p (-1, -1).red, get_green_p (0, -1).red, get_green_p (1, -1).red, get_green_p (2, -1).red},
					     (vec_luminosity_t){get_green_p (-1,  0).red, get_green_p (0,  0).red, get_green_p (1,  0).red, get_green_p (2,  0).red},
					     (vec_luminosity_t){get_green_p (-1,  1).red, get_green_p (0,  1).red, get_green_p (1,  1).red, get_green_p (2,  1).red},
					     (vec_luminosity_t){get_green_p (-1,  2).red, get_green_p (0,  2).red, get_green_p (1,  2).red, get_green_p (2,  2).red}, off);
      intgreen.green = do_bicubic_interpolate ((vec_luminosity_t){get_green_p (-1, -1).green, get_green_p (0, -1).green, get_green_p (1, -1).green, get_green_p (2, -1).green},
					       (vec_luminosity_t){get_green_p (-1,  0).green, get_green_p (0,  0).green, get_green_p (1,  0).green, get_green_p (2,  0).green},
					       (vec_luminosity_t){get_green_p (-1,  1).green, get_green_p (0,  1).green, get_green_p (1,  1).green, get_green_p (2,  1).green},
					       (vec_luminosity_t){get_green_p (-1,  2).green, get_green_p (0,  2).green, get_green_p (1,  2).green, get_green_p (2,  2).green}, off);
      intgreen.blue = do_bicubic_interpolate ((vec_luminosity_t){get_green_p (-1, -1).blue, get_green_p (0, -1).blue, get_green_p (1, -1).blue, get_green_p (2, -1).blue},
					      (vec_luminosity_t){get_green_p (-1,  0).blue, get_green_p (0,  0).blue, get_green_p (1,  0).blue, get_green_p (2,  0).blue},
					      (vec_luminosity_t){get_green_p (-1,  1).blue, get_green_p (0,  1).blue, get_green_p (1,  1).blue, get_green_p (2,  1).blue},
					      (vec_luminosity_t){get_green_p (-1,  2).blue, get_green_p (0,  2).blue, get_green_p (1,  2).blue, get_green_p (2,  2).blue}, off);
#undef get_green_p
    }
  rgbdata intblue = {0, 0, 0};
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_area.width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_area.height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_rgb_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      intblue.red = do_bicubic_interpolate ((vec_luminosity_t){get_blue_p (-1, -1).red, get_blue_p (0, -1).red, get_blue_p (1, -1).red, get_blue_p (2, -1).red},
					    (vec_luminosity_t){get_blue_p (-1,  0).red, get_blue_p (0,  0).red, get_blue_p (1,  0).red, get_blue_p (2,  0).red},
					    (vec_luminosity_t){get_blue_p (-1,  1).red, get_blue_p (0,  1).red, get_blue_p (1,  1).red, get_blue_p (2,  1).red},
					    (vec_luminosity_t){get_blue_p (-1,  2).red, get_blue_p (0,  2).red, get_blue_p (1,  2).red, get_blue_p (2,  2).red}, off);
      intblue.green = do_bicubic_interpolate ((vec_luminosity_t){get_blue_p (-1, -1).green, get_blue_p (0, -1).green, get_blue_p (1, -1).green, get_blue_p (2, -1).green},
					      (vec_luminosity_t){get_blue_p (-1,  0).green, get_blue_p (0,  0).green, get_blue_p (1,  0).green, get_blue_p (2,  0).green},
					      (vec_luminosity_t){get_blue_p (-1,  1).green, get_blue_p (0,  1).green, get_blue_p (1,  1).green, get_blue_p (2,  1).green},
					      (vec_luminosity_t){get_blue_p (-1,  2).green, get_blue_p (0,  2).green, get_blue_p (1,  2).green, get_blue_p (2,  2).green}, off);
      intblue.blue = do_bicubic_interpolate ((vec_luminosity_t){get_blue_p (-1, -1).blue, get_blue_p (0, -1).blue, get_blue_p (1, -1).blue, get_blue_p (2, -1).blue},
					     (vec_luminosity_t){get_blue_p (-1,  0).blue, get_blue_p (0,  0).blue, get_blue_p (1,  0).blue, get_blue_p (2,  0).blue},
					     (vec_luminosity_t){get_blue_p (-1,  1).blue, get_blue_p (0,  1).blue, get_blue_p (1,  1).blue, get_blue_p (2,  1).blue},
					     (vec_luminosity_t){get_blue_p (-1,  2).blue, get_blue_p (0,  2).blue, get_blue_p (1,  2).blue, get_blue_p (2,  2).blue}, off);
#undef get_blue_p
    }

  return intred * patch_proportions.red + intgreen * patch_proportions.green + intblue * patch_proportions.blue;
}
template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::nearest_bw_interpolate (point_t scr) const
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 3, green_maxx = 2, green_maxy = 3, blue_maxx = 2, blue_maxy = 2;
  rgbdata ret = {(luminosity_t) 1, (luminosity_t) 0, (luminosity_t) 0};

  scr.x += (coord_t)m_area.xshift ();
  scr.y += (coord_t)m_area.yshift ();
  point_t off;
  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_area.width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_area.height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      ret.red = get_red_p (off.x > (coord_t) 0.5, off.y > (coord_t) 0.5);
#undef get_red_p
    }
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_area.width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_area.height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      ret.green = get_green_p (off.x > (coord_t) 0.5, off.y > (coord_t) 0.5);
#undef get_green_p
    }
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_area.width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_area.height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      ret.blue = get_blue_p (off.x > (coord_t) 0.5, off.y > (coord_t) 0.5);
#undef get_blue_p
    }
  return ret;
}
template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::linear_bw_interpolate (point_t scr) const
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 3, green_maxx = 2, green_maxy = 3, blue_maxx = 2, blue_maxy = 2;
  rgbdata ret = {(luminosity_t) 1, (luminosity_t) 0, (luminosity_t) 0};

  scr.x += (coord_t)m_area.xshift ();
  scr.y += (coord_t)m_area.yshift ();
  point_t off;
  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_area.width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_area.height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      ret.red = do_linear_interpolate (get_red_p (0, 0), get_red_p (1, 0), get_red_p (0, 1), get_red_p (1, 1), off);
#undef get_red_p
    }
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_area.width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_area.height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      ret.green = do_linear_interpolate (get_green_p (0, 0), get_green_p (1, 0), get_green_p (0, 1), get_green_p (1, 1), off);
#undef get_green_p
    }
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_area.width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_area.height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      ret.blue = do_linear_interpolate (get_blue_p (0, 0), get_blue_p (1, 0), get_blue_p (0, 1), get_blue_p (1, 1), off);
#undef get_blue_p
    }
  return ret;
}

template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::bicubic_bw_interpolate (point_t scr) const
{
  /* Paget needs -3 for miny because of diagonal coordinates.  */
  int64_t red_minx = -2, red_miny = -3, green_minx = -2, green_miny = -3, blue_minx = -2, blue_miny = -2;
  int64_t red_maxx = 2, red_maxy = 3, green_maxx = 2, green_maxy = 3, blue_maxx = 2, blue_maxy = 2;
  rgbdata ret = {(luminosity_t) 1, (luminosity_t) 0, (luminosity_t) 0};

  scr.x += (coord_t)m_area.xshift ();
  scr.y += (coord_t)m_area.yshift ();
  point_t off;
  data_entry e = GEOMETRY::red_scr_to_entry (scr, &off);
  if (e.x + red_minx >= 0 && e.x + red_maxx < m_area.width * GEOMETRY::red_width_scale
      && e.y + red_miny >= 0 && e.y + red_maxy < m_area.height * GEOMETRY::red_height_scale)
    {
#define get_red_p(xx, yy) fast_red (GEOMETRY::offset_for_interpolation_red (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_red (e, {xx, yy}).y)
      ret.red = do_bicubic_interpolate ((vec_luminosity_t){get_red_p (-1, -1), get_red_p (0, -1), get_red_p (1, -1), get_red_p (2, -1)},
					(vec_luminosity_t){get_red_p (-1,  0), get_red_p (0,  0), get_red_p (1,  0), get_red_p (2,  0)},
					(vec_luminosity_t){get_red_p (-1,  1), get_red_p (0,  1), get_red_p (1,  1), get_red_p (2,  1)},
					(vec_luminosity_t){get_red_p (-1,  2), get_red_p (0,  2), get_red_p (1,  2), get_red_p (2,  2)}, off);
#undef get_red_p
    }
  e = GEOMETRY::green_scr_to_entry (scr, &off);
  if (e.x + green_minx >= 0 && e.x + green_maxx < m_area.width * GEOMETRY::green_width_scale
      && e.y + green_miny >= 0 && e.y + green_maxy < m_area.height * GEOMETRY::green_height_scale)
    {
#define get_green_p(xx, yy) fast_green (GEOMETRY::offset_for_interpolation_green (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_green (e, {xx, yy}).y)
      ret.green = do_bicubic_interpolate ((vec_luminosity_t){get_green_p (-1, -1), get_green_p (0, -1), get_green_p (1, -1), get_green_p (2, -1)},
					(vec_luminosity_t){get_green_p (-1,  0), get_green_p (0,  0), get_green_p (1,  0), get_green_p (2,  0)},
					(vec_luminosity_t){get_green_p (-1,  1), get_green_p (0,  1), get_green_p (1,  1), get_green_p (2,  1)},
					(vec_luminosity_t){get_green_p (-1,  2), get_green_p (0,  2), get_green_p (1,  2), get_green_p (2,  2)}, off);
#undef get_green_p
    }
  e = GEOMETRY::blue_scr_to_entry (scr, &off);
  if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_area.width * GEOMETRY::blue_width_scale
      && e.y + blue_miny >= 0 && e.y + blue_maxy < m_area.height * GEOMETRY::blue_height_scale)
    {
#define get_blue_p(xx, yy) fast_blue (GEOMETRY::offset_for_interpolation_blue (e,{xx, yy}).x, GEOMETRY::offset_for_interpolation_blue (e, {xx, yy}).y)
      ret.blue = do_bicubic_interpolate ((vec_luminosity_t){get_blue_p (-1, -1), get_blue_p (0, -1), get_blue_p (1, -1), get_blue_p (2, -1)},
					(vec_luminosity_t){get_blue_p (-1,  0), get_blue_p (0,  0), get_blue_p (1,  0), get_blue_p (2,  0)},
					(vec_luminosity_t){get_blue_p (-1,  1), get_blue_p (0,  1), get_blue_p (1,  1), get_blue_p (2,  1)},
					(vec_luminosity_t){get_blue_p (-1,  2), get_blue_p (0,  2), get_blue_p (1,  2), get_blue_p (2,  2)}, off);
#undef get_blue_p
    }
  return ret;
}


template<typename GEOMETRY>
inline pure_attr rgbdata
analyze_base_worker<GEOMETRY>::interpolate (point_t scr, rgbdata patch_proportions, render_parameters::screen_demosaic_t mode) const
{
  if (m_red)
  {
    switch (mode)
      {
      case render_parameters::nearest_demosaic:
	return nearest_bw_interpolate (scr);
      case render_parameters::linear_demosaic:
	return linear_bw_interpolate (scr);
      default:
      case render_parameters::bicubic_demosaic:
	return bicubic_bw_interpolate (scr);
      }
  }
  else
    return bicubic_rgb_interpolate (scr, patch_proportions);
}
template<typename GEOMETRY>
bool
analyze_base_worker<GEOMETRY>::populate_demosaiced_data (std::vector<rgbdata> &demosaic, render *r, int_image_area area, progress_info *progress)
{
  if (progress)
    progress->set_task ("populating demosaiced data", area.height);

  /* Step 1: Populate demosaic with the mosaiced data.
     Each pixel gets only its known channel value; others remain 0.  */
#pragma omp parallel shared(progress,area,r,demosaic) default(none)
  for (int y = 0; y < area.height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < area.width; x++)
	  {
	    point_t p = GEOMETRY::from_demosaiced_coordinates (point_t {(coord_t)(x + area.x), (coord_t)(y + area.y)});
	    p.x += (coord_t)m_area.xshift ();
	    p.y += (coord_t)m_area.yshift ();
	    point_t off;
	    data_entry e = GEOMETRY::red_scr_to_entry (p, &off);
	    if (my_fabs (off.x) < (coord_t) 0.01 && my_fabs (off.y) < (coord_t) 0.01)
	      {
		demosaic [y * area.width + x].red =  /*std::max (red (e.x, e.y), (luminosity_t) 0)*/ r->adjust_luminosity_ir (red (e.x, e.y));
		assert (!debug
			|| GEOMETRY::demosaic_entry_color (x + area.x, y + area.y)
			   == base_geometry::red);
		continue;
	      }
	    e = GEOMETRY::green_scr_to_entry (p, &off);
	    if (my_fabs (off.x) < (coord_t) 0.01 && my_fabs (off.y) < (coord_t) 0.01)
	      {
		demosaic [y * area.width + x].green = /*std::max (green (e.x, e.y), (luminosity_t) 0)*/ r->adjust_luminosity_ir (green (e.x, e.y));
		assert (!debug
			|| GEOMETRY::demosaic_entry_color (x + area.x, y + area.y)
			   == base_geometry::green);
		continue;
	      }
	    e = GEOMETRY::blue_scr_to_entry (p, &off);
	    demosaic [y * area.width + x].blue = /*std::max (blue (e.x, e.y), (luminosity_t) 0)*/ r->adjust_luminosity_ir (blue (e.x, e.y));
	    assert (!debug
		    || GEOMETRY::demosaic_entry_color (x + area.x, y + area.y)
		       == base_geometry::blue);
	    assert (my_fabs (off.x) < (coord_t) 0.01 && my_fabs (off.y) < (coord_t) 0.01);
	  }
      if (progress)
	progress->inc_progress ();
    }
  return !progress || !progress->cancelled ();
}
template<typename GEOMETRY>
int_image_area
analyze_base_worker<GEOMETRY>::demosaiced_area () const
{
  point_t corners[4] = {
    GEOMETRY::to_demosaiced_coordinates (point_t {(coord_t)m_area.top_left ().x, (coord_t)m_area.top_left ().y}),
    GEOMETRY::to_demosaiced_coordinates (point_t {(coord_t)m_area.top_right ().x, (coord_t)m_area.top_right ().y}),
    GEOMETRY::to_demosaiced_coordinates (point_t {(coord_t)m_area.bottom_left ().x, (coord_t)m_area.bottom_left ().y}),
    GEOMETRY::to_demosaiced_coordinates (point_t {(coord_t)m_area.bottom_right ().x, (coord_t)m_area.bottom_right ().y})
  };

  int_image_area area (int_point_t {(int64_t)my_floor (corners[0].x), (int64_t)my_floor (corners[0].y)});
  for (int i = 0; i < 4; i++)
    {
      area.extend (int_point_t {(int64_t)my_floor (corners[i].x), (int64_t)my_floor (corners[i].y)});
      area.extend (int_point_t {(int64_t)my_ceil (corners[i].x), (int64_t)my_ceil (corners[i].y)});
    }

  int x = area.x;
  int y = area.y;
  int max_x = area.x + area.width;
  int max_y = area.y + area.height;

  /* Round to the demosaicing period.  */
  if (x > 0)
    x -= x % GEOMETRY::demosaic_period_x ();
  else
    x = -(-x + (x % GEOMETRY::demosaic_period_x ()) - GEOMETRY::demosaic_period_x ());
  if (y > 0)
    y -= y % GEOMETRY::demosaic_period_y ();
  else
    y = -(-y + (y % GEOMETRY::demosaic_period_y ()) - GEOMETRY::demosaic_period_y ());

  /* Identify width and height.  */
  int width = max_x - x;
  int height = max_y - y;
  /* Also round up to demosaicing period.  */
  width = (width + GEOMETRY::demosaic_period_x () - 1)/ GEOMETRY::demosaic_period_x () * GEOMETRY::demosaic_period_x ();
  height = (height + GEOMETRY::demosaic_period_y () - 1)/ GEOMETRY::demosaic_period_y () * GEOMETRY::demosaic_period_y ();
  return {x, y, width, height};
}

}
#endif

