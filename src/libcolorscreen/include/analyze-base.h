#ifndef ANALYZE_BASE_H
#define ANALYZE_BASE_H
#include "scr-to-img.h"
#include "color.h"
#include "progress-info.h"
#include "bitmap.h"
#include "screen.h"
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
  struct data_entry {
    int64_t x,y;
    pure_attr inline data_entry operator+(const data_entry other)
    {
      return {x + other.x, y + other.y};
    }
  };
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

/* Collect luminosity of individual color patches.  */
  template<typename T>
  bool
  analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
  {
#pragma omp parallel shared(progress, render, scr_to_img, screen, collection_threshold, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
    {
#pragma omp for 
      for (int y = miny ; y < maxy; y++)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int x = minx; x < maxx; x++)
	      {
		point_t scr;
		scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr.x, &scr.y);
		scr.x += m_xshift;
		scr.y += m_yshift;
		/* Dufay analyzer shifts red strip and some pixels gets accounted to neighbouring screen tile;
		   add extra bffer of 1 screen tile to be sure we do not access uninitialized memory.  */
		if (!T::check_range && (scr.x <= 0 || scr.x >= m_width - 1 || scr.y <= 0 || scr.y >= m_height - 1))
		  continue;

		luminosity_t l = render->get_unadjusted_data (x, y);
		int ix = (uint64_t) nearest_int (scr.x * screen::size) & (unsigned)(screen::size - 1);
		int iy = (uint64_t) nearest_int (scr.y * screen::size) & (unsigned)(screen::size - 1);
		if (screen->mult[iy][ix][0] > collection_threshold)
		  {
		    data_entry e = T::red_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::red_width_scale && e.y >= 0 && e.y < m_height * T::red_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][0] - collection_threshold);
			int idx = e.y * m_width * T::red_width_scale + e.x;
			luminosity_t &c = m_red [idx];
			luminosity_t vall = val * l;
#pragma omp atomic
			c += vall;
			luminosity_t &w = w_red [idx];
#pragma omp atomic
			w += val;
		      }
		  }
		if (screen->mult[iy][ix][1] > collection_threshold)
		  {
		    data_entry e = T::green_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::green_width_scale && e.y >= 0 && e.y < m_height * T::green_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][1] - collection_threshold);
			int idx = e.y * m_width * T::green_width_scale + e.x;
			luminosity_t vall = val * l;
			luminosity_t &c = m_green [idx];
#pragma omp atomic
			c += vall;
			luminosity_t &w = w_green [idx];
#pragma omp atomic
			w += val;
		      }
		  }
		if (screen->mult[iy][ix][2] > collection_threshold)
		  {
		    data_entry e = T::blue_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::blue_width_scale && e.y >= 0 && e.y < m_height * T::blue_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][2] - collection_threshold);
			int idx = e.y * m_width * T::blue_width_scale + e.x;
			luminosity_t vall = val * l;
			luminosity_t &c = m_blue [idx];
#pragma omp atomic
			c += vall;
			luminosity_t &w = w_blue [idx];
#pragma omp atomic
			w += val;
		      }
		  }
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::red_height_scale; yy++)
	        for (int x = 0; x < m_width * T::red_width_scale; x++)
		  {
		    data_entry e = {x, y * T::red_height_scale + yy};
		    int idx = e.y * m_width * T::red_width_scale + e.x;
		    if (w_red [idx] != 0)
		      m_red [idx] /= w_red [idx];
		    else
		      {
			point_t scr = T::red_entry_to_scr (e);
			m_red [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::green_height_scale; yy++)
	        for (int x = 0; x < m_width * T::green_width_scale; x++)
		  {
		    data_entry e = {x, y * T::green_height_scale + yy};
		    int idx = e.y * m_width * T::green_width_scale + e.x;
		    if (w_green [idx] != 0)
		      m_green [idx] /= w_green [idx];
		    else
		      {
			point_t scr = T::green_entry_to_scr (e);
			m_green [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::blue_height_scale; yy++)
	        for (int x = 0; x < m_width * T::blue_width_scale; x++)
		  {
		    data_entry e = {x, y * T::blue_height_scale + yy};
		    int idx = e.y * m_width * T::blue_width_scale + e.x;
		    if (w_blue [idx] != 0)
		      m_blue [idx] /= w_blue [idx];
		    else
		      {
			point_t scr = T::blue_entry_to_scr (e);
			m_blue [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
	}
      }
    return !progress || !progress->cancelled ();
  }
  /* Collect RGB colors of individual color patches (in scanner color space).
     This is useful i.e. to determine scanner response to the dye colors and set mixing weights.  */
  template<typename T>
  bool
  analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
  {
#pragma omp parallel shared(progress, render, scr_to_img, screen, collection_threshold, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
    {
#pragma omp for 
      for (int y = miny ; y < maxy; y++)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int x = minx; x < maxx; x++)
	      {
		point_t scr;
		scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr.x, &scr.y);
		scr.x += m_xshift;
		scr.y += m_yshift;
		if (!T::check_range && (scr.x < 0 || scr.x > m_width - 1 || scr.y < 0 || scr.y > m_height - 1))
		  continue;

		rgbdata l = render->get_unadjusted_rgb_pixel (x, y);
		int ix = (uint64_t) nearest_int (scr.x * screen::size) & (unsigned)(screen::size - 1);
		int iy = (uint64_t) nearest_int (scr.y * screen::size) & (unsigned)(screen::size - 1);
		if (screen->mult[iy][ix][0] > collection_threshold)
		  {
		    data_entry e = T::red_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::red_width_scale && e.y >= 0 && e.y < m_height * T::red_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][0] - collection_threshold);
			int idx = e.y * m_width * T::red_width_scale + e.x;
			rgbdata vall = l * val;
			rgbdata &c = m_rgb_red [idx];
#pragma omp atomic
			c.red += vall.red;
#pragma omp atomic
			c.green += vall.green;
#pragma omp atomic
			c.blue += vall.blue;
			luminosity_t &w = w_red [idx];
#pragma omp atomic
			w += val;
		      }
		  }
		if (screen->mult[iy][ix][1] > collection_threshold)
		  {
		    data_entry e = T::green_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::green_width_scale && e.y >= 0 && e.y < m_height * T::green_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][1] - collection_threshold);
			int idx = e.y * m_width * T::green_width_scale + e.x;
			rgbdata vall = l * val;
			rgbdata &c = m_rgb_green [idx];
#pragma omp atomic
			c.red += vall.red;
#pragma omp atomic
			c.green += vall.green;
#pragma omp atomic
			c.blue += vall.blue;
			luminosity_t &w = w_green [idx];
#pragma omp atomic
			w += val;
		      }
		  }
		if (screen->mult[iy][ix][2] > collection_threshold)
		  {
		    data_entry e = T::blue_scr_to_entry (scr);
		    if (!T::check_range || (e.x >= 0 && e.x < m_width * T::blue_width_scale && e.y >= 0 && e.y < m_height * T::blue_height_scale))
		      {
			luminosity_t val = (screen->mult[iy][ix][2] - collection_threshold);
			int idx = e.y * m_width * T::blue_width_scale + e.x;
			rgbdata vall = l * val;
			rgbdata &c = m_rgb_blue [idx];
#pragma omp atomic
			c.red += vall.red;
#pragma omp atomic
			c.green += vall.green;
#pragma omp atomic
			c.blue += vall.blue;
			luminosity_t &w = w_blue [idx];
#pragma omp atomic
			w += val;
		      }
		  }
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::red_height_scale; yy++)
	        for (int x = 0; x < m_width * T::red_width_scale; x++)
		  {
		    data_entry e = {x, y * T::red_height_scale + yy};
		    int idx = e.y * m_width * T::red_width_scale + e.x;
		    if (w_red [idx] != 0)
		      m_rgb_red [idx] /= w_red [idx];
		    else
		      {
			point_t scr = T::red_entry_to_scr (e);
			m_rgb_red [idx] = render->get_unadjusted_rgb_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::green_height_scale; yy++)
	        for (int x = 0; x < m_width * T::green_width_scale; x++)
		  {
		    data_entry e = {x, y * T::green_height_scale + yy};
		    int idx = e.y * m_width * T::green_width_scale + e.x;
		    if (w_green [idx] != 0)
		      m_rgb_green [idx] /= w_green [idx];
		    else
		      {
			point_t scr = T::green_entry_to_scr (e);
			m_rgb_green [idx] = render->get_unadjusted_rgb_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::blue_height_scale; yy++)
	        for (int x = 0; x < m_width * T::blue_width_scale; x++)
		  {
		    data_entry e = {x, y * T::blue_height_scale + yy};
		    int idx = e.y * m_width * T::blue_width_scale + e.x;
		    if (w_blue [idx] != 0)
		      m_rgb_blue [idx] /= w_blue [idx];
		    else
		      {
			point_t scr = T::blue_entry_to_scr (e);
			m_rgb_blue [idx] = render->get_unadjusted_rgb_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		      }
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
	}
      }
    return !progress || !progress->cancelled ();
  }
  /* Collect data for deinterlaced rendering in original or profiled colors.
     TODO: This does not make much sense.  We should apply profile first or collect in RGB colors.  */
  template<typename T>
  bool
  analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
  {
    luminosity_t weights[256];
    luminosity_t half_weights[256];

    /* FIXME: technically not right for paget where diagonal coordinates are not
       of same size as normal ones.  */
    coord_t pixel_size = render->pixel_size ();
    coord_t left = 128 - (pixel_size / 2) * 256;
    coord_t right = 128 + (pixel_size / 2) * 256;
    coord_t half_left = 128 - (pixel_size / 4) * 256;
    coord_t half_right = 128 + (pixel_size / 4) * 256;

    for (int i = 0; i < 256; i++)
      {
	if (i <= left)
	  weights[i] = 0;
	else if (i >= right)
	  weights[i] = 1;
	else
	  weights[i] = (i - left) / (right - left);

	if (i <= half_left)
	  half_weights[i] = 0;
	else if (i >= half_right)
	  half_weights[i] = 1;
	else
	  half_weights[i] = (i - half_left) / (half_right - half_left);
      }
#pragma omp parallel shared(progress, render, scr_to_img, w_blue, w_red, w_green, minx, miny, maxx, maxy, half_weights, weights) default(none)
    {
#pragma omp for 
      for (int y = miny ; y < maxy; y++)
	{
	  /* This will optimize to constants; be sure that those are not hidden by openmp runtime.  */
	  int64_t red_minx = 0, red_miny = 0, red_maxx = 0, red_maxy = 0;
	  data_entry o = T::offset_for_interpolation_red ({0, 1});
	  red_minx = std::min (o.x, red_minx);
	  red_maxx = std::max (o.x, red_maxx);
	  red_miny = std::min (o.y, red_miny);
	  red_maxy = std::max (o.y, red_maxy);

	  o = T::offset_for_interpolation_red ({1, 0});
	  red_minx = std::min (o.x, red_minx);
	  red_maxx = std::max (o.x, red_maxx);
	  red_miny = std::min (o.y, red_miny);
	  red_maxy = std::max (o.y, red_maxy);

	  o = T::offset_for_interpolation_red ({1, 1});
	  red_minx = std::min (o.x, red_minx);
	  red_maxx = std::max (o.x, red_maxx);
	  red_miny = std::min (o.y, red_miny);
	  red_maxy = std::max (o.y, red_maxy);

	  int64_t green_minx = 0, green_miny = 0, green_maxx = 0, green_maxy = 0;
	  o = T::offset_for_interpolation_green ({0, 1});
	  green_minx = std::min (o.x, green_minx);
	  green_maxx = std::max (o.x, green_maxx);
	  green_miny = std::min (o.y, green_miny);
	  green_maxy = std::max (o.y, green_maxy);

	  o = T::offset_for_interpolation_green ({1, 0});
	  green_minx = std::min (o.x, green_minx);
	  green_maxx = std::max (o.x, green_maxx);
	  green_miny = std::min (o.y, green_miny);
	  green_maxy = std::max (o.y, green_maxy);

	  o = T::offset_for_interpolation_green ({1, 1});
	  green_minx = std::min (o.x, green_minx);
	  green_maxx = std::max (o.x, green_maxx);
	  green_miny = std::min (o.y, green_miny);
	  green_maxy = std::max (o.y, green_maxy);

	  int64_t blue_minx = 0, blue_miny = 0, blue_maxx = 0, blue_maxy = 0;
	  o = T::offset_for_interpolation_blue ({0, 1});
	  blue_minx = std::min (o.x, blue_minx);
	  blue_maxx = std::max (o.x, blue_maxx);
	  blue_miny = std::min (o.y, blue_miny);
	  blue_maxy = std::max (o.y, blue_maxy);

	  o = T::offset_for_interpolation_blue ({1, 0});
	  blue_minx = std::min (o.x, blue_minx);
	  blue_maxx = std::max (o.x, blue_maxx);
	  blue_miny = std::min (o.y, blue_miny);
	  blue_maxy = std::max (o.y, blue_maxy);

	  o = T::offset_for_interpolation_blue ({1, 1});
	  blue_minx = std::min (o.x, blue_minx);
	  blue_maxx = std::max (o.x, blue_maxx);
	  blue_miny = std::min (o.y, blue_miny);
	  blue_maxy = std::max (o.y, blue_maxy);
	  if (!progress || !progress->cancel_requested ())
	    for (int x = minx; x < maxx; x++)
	      {
		point_t scr;
		scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr.x, &scr.y);
		scr.x += m_xshift;
		scr.y += m_yshift;
		if (!T::check_range && (scr.x < 0 || scr.x > m_width - 1 || scr.y < 0 || scr.y > m_height - 1))
		  continue;
		rgbdata d = render->get_unadjusted_rgb_pixel (x, y);

		point_t off;
		analyze_base::data_entry e = T::red_scr_to_entry (scr, &off);
		if (e.x + red_minx >= 0 && e.x + red_maxx < m_width * T::red_width_scale
	       	    && e.y + red_miny >= 0 && e.y + red_maxy < m_height * T::red_height_scale)
		  {
		    off.x = half_weights[(int)(off.x * 255.5)];
		    off.y = weights[(int)(off.y * 255.5)];
		    luminosity_t &l1 = w_red [(e.y) * m_width * T::red_width_scale + e.x];
		    luminosity_t &v1 = m_red [(e.y) * m_width * T::red_width_scale + e.x];
		    luminosity_t val1 = (1 - off.x) * (1 - off.y);
#pragma omp atomic
		    l1 += val1;
#pragma omp atomic
		    v1 += d.red * val1;
		    data_entry o = T::offset_for_interpolation_red ({1, 0});
		    luminosity_t &l2 = w_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t &v2 = m_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t val2 = (off.x) * (1 - off.y);
#pragma omp atomic
		    l2 += val2;
#pragma omp atomic
		    v2 += d.red * val2;
		    o = T::offset_for_interpolation_red ({0, 1});
		    luminosity_t &l3 = w_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t &v3 = m_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t val3 = (1 - off.x) * (off.y);
#pragma omp atomic
		    l3 += val3;
#pragma omp atomic
		    v3 += d.red * val3;
		    o = T::offset_for_interpolation_red ({1, 1});
		    luminosity_t &l4 = w_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t &v4 = m_red [(e.y + o.y) * m_width * T::red_width_scale + e.x + o.x];
		    luminosity_t val4 = (off.x) * (off.y);
#pragma omp atomic
		    l4 += val4;
#pragma omp atomic
		    v4 += d.red * val4;
		  }
		e = T::green_scr_to_entry (scr, &off);
		if (e.x + green_minx >= 0 && e.x + green_maxx < m_width * T::green_width_scale
	       	    && e.y + green_miny >= 0 && e.y + green_maxy < m_height * T::green_height_scale)
		  {
		    off.x = half_weights[(int)(off.x * 255.5)];
		    off.y = weights[(int)(off.y * 255.5)];
		    luminosity_t &l1 = w_green [(e.y) * m_width * T::green_width_scale + e.x];
		    luminosity_t &v1 = m_green [(e.y) * m_width * T::green_width_scale + e.x];
		    luminosity_t val1 = (1 - off.x) * (1 - off.y);
#pragma omp atomic
		    l1 += val1;
#pragma omp atomic
		    v1 += d.green * val1;
		    data_entry o = T::offset_for_interpolation_green ({1, 0});
		    luminosity_t &l2 = w_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t &v2 = m_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t val2 = (off.x) * (1 - off.y);
#pragma omp atomic
		    l2 += val2;
#pragma omp atomic
		    v2 += d.green * val2;
		    o = T::offset_for_interpolation_green ({0, 1});
		    luminosity_t &l3 = w_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t &v3 = m_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t val3 = (1 - off.x) * (off.y);
#pragma omp atomic
		    l3 += val3;
#pragma omp atomic
		    v3 += d.green * val3;
		    o = T::offset_for_interpolation_green ({1, 1});
		    luminosity_t &l4 = w_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t &v4 = m_green [(e.y + o.y) * m_width * T::green_width_scale + e.x + o.x];
		    luminosity_t val4 = (off.x) * (off.y);
#pragma omp atomic
		    l4 += val4;
#pragma omp atomic
		    v4 += d.green * val4;
		  }
		e = T::blue_scr_to_entry (scr, &off);
		if (e.x + blue_minx >= 0 && e.x + blue_maxx < m_width * T::blue_width_scale
	       	    && e.y + blue_miny >= 0 && e.y + blue_maxy < m_height * T::blue_height_scale)
		  {
		    off.x = half_weights[(int)(off.x * 255.5)];
		    off.y = weights[(int)(off.y * 255.5)];
		    luminosity_t &l1 = w_blue [(e.y) * m_width * T::blue_width_scale + e.x];
		    luminosity_t &v1 = m_blue [(e.y) * m_width * T::blue_width_scale + e.x];
		    luminosity_t val1 = (1 - off.x) * (1 - off.y);
#pragma omp atomic
		    l1 += val1;
#pragma omp atomic
		    v1 += d.blue * val1;
		    data_entry o = T::offset_for_interpolation_blue ({1, 0});
		    luminosity_t &l2 = w_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t &v2 = m_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t val2 = (off.x) * (1 - off.y);
#pragma omp atomic
		    l2 += val2;
#pragma omp atomic
		    v2 += d.blue * val2;
		    o = T::offset_for_interpolation_blue ({0, 1});
		    luminosity_t &l3 = w_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t &v3 = m_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t val3 = (1 - off.x) * (off.y);
#pragma omp atomic
		    l3 += val3;
#pragma omp atomic
		    v3 += d.blue * val3;
		    o = T::offset_for_interpolation_blue ({1, 1});
		    luminosity_t &l4 = w_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t &v4 = m_blue [(e.y + o.y) * m_width * T::blue_width_scale + e.x + o.x];
		    luminosity_t val4 = (off.x) * (off.y);
#pragma omp atomic
		    l4 += val4;
#pragma omp atomic
		    v4 += d.blue * val4;
		  }
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    if (!progress || !progress->cancel_requested ())
      {
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::red_height_scale; yy++)
	        for (int x = 0; x < m_width * T::red_width_scale; x++)
		  {
		    data_entry e = {x, y * T::red_height_scale + yy};
		    int idx = e.y * m_width * T::red_width_scale + e.x;
		    if (w_red [idx] != 0)
		      m_red [idx] /= w_red [idx];
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::green_height_scale; yy++)
	        for (int x = 0; x < m_width * T::green_width_scale; x++)
		  {
		    data_entry e = {x, y * T::green_height_scale + yy};
		    int idx = e.y * m_width * T::green_width_scale + e.x;
		    if (w_green [idx] != 0)
		      m_green [idx] /= w_green [idx];
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
#pragma omp for nowait
	for (int y = 0; y < m_height; y++)
	  {
	    if (!progress || !progress->cancel_requested ())
	      for (int yy = 0 ; yy < T::blue_height_scale; yy++)
	        for (int x = 0; x < m_width * T::blue_width_scale; x++)
		  {
		    data_entry e = {x, y * T::blue_height_scale + yy};
		    int idx = e.y * m_width * T::blue_width_scale + e.x;
		    if (w_blue [idx] != 0)
		      m_blue [idx] /= w_blue [idx];
		  }
	    if (progress)
	      progress->inc_progress ();
	  }
	}
      }
    return !progress || !progress->cancelled ();
  }

  /* Fast data collection from centers of individual patches.  */
  template<typename T>
  bool
  analyze_fast (render_to_scr *render,progress_info *progress)
  {
#pragma omp parallel for default (none) shared (progress, render)
    for (int x = 0; x < m_width; x++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int y = 0 ; y < m_height; y++)
	    {
	      for (int yy = 0; yy < T::red_height_scale; yy++)
	        for (int xx = 0; xx < T::red_width_scale; xx++)
		  {
		    data_entry e = {x * T::red_width_scale + xx, y * T::red_height_scale + yy};
		    int idx = e.y * m_width * T::red_width_scale + e.x;
		    point_t scr = T::red_entry_to_scr (e);
		    m_red [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		  }
	      for (int yy = 0; yy < T::green_height_scale; yy++)
	        for (int xx = 0; xx < T::green_width_scale; xx++)
		  {
		    data_entry e = {x * T::green_width_scale + xx, y * T::green_height_scale + yy};
		    int idx = e.y * m_width * T::green_width_scale + e.x;
		    point_t scr = T::green_entry_to_scr (e);
		    m_green [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		  }
	      for (int yy = 0; yy < T::blue_height_scale; yy++)
	        for (int xx = 0; xx < T::blue_width_scale; xx++)
		  {
		    data_entry e = {x * T::blue_width_scale + xx, y * T::blue_height_scale + yy};
		    int idx = e.y * m_width * T::blue_width_scale + e.x;
		    point_t scr = T::blue_entry_to_scr (e);
		    m_blue [idx] = render->get_unadjusted_img_pixel_scr (scr.x - m_xshift, scr.y - m_yshift);
		  }
	    }
	if (progress)
	  progress->inc_progress ();
      }
#undef pixel
    return !progress || !progress->cancelled ();
  }
};
#endif
