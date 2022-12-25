#ifndef ANALYZE_DUFAY_H
#define ANALYZE_DUFAY_H
#include "render-to-scr.h"
#include "progress-info.h"
#include "bitmap.h"
class analyze_dufay
{
public:
  analyze_dufay()
  : m_xshift (0), m_yshift (0), m_width (0), m_height (0), m_red (0), m_green (0), m_blue (0), m_known_pixels (NULL), m_n_known_pixels (0)
  {
  }
  ~analyze_dufay()
  {
    free (m_red);
    free (m_green);
    free (m_blue);
    if (m_known_pixels)
      delete m_known_pixels;
  }
  luminosity_t &blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_blue [y * m_width + x];
    }
  luminosity_t &red (int x, int y)
    {
      x = std::min (std::max (x, 0), 2 * m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_red [y * m_width * 2 + x];
    }

  luminosity_t &green (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_green [y * m_width + x];
    }
  int get_xshift ()
  {
    return m_xshift;
  }
  int get_yshift ()
  {
    return m_yshift;
  }
  bool analyze(render_to_scr *render, int width, int height, int xshift, int yshift, bool precise, progress_info *progress = NULL);
  bool find_best_match (int percentake, analyze_dufay &other, int skiptop, int skipbottom, int skipleft, int skipright, int *xshift, int *yshift, progress_info *progress = NULL);
  void set_known_pixels (bitmap_2d *bitmap)
  {
    assert (!m_known_pixels && !m_n_known_pixels);
    m_known_pixels = bitmap;
    for (int y = 0; y < m_height; y++)
      for (int x = 0; x < m_width; x++)
	if (bitmap->test_bit (x,y))
	  m_n_known_pixels++;
  }
private:
  int m_xshift, m_yshift, m_width, m_height;
  luminosity_t *m_red, *m_green, *m_blue;
  bitmap_2d *m_known_pixels;
  int m_n_known_pixels;
};
#endif