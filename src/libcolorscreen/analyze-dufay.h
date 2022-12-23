#ifndef ANALYZE_DUFAY_H
#include "include/render-to-scr.h"
#include "include/progress-info.h"
class analyze_dufay
{
public:
  analyze_dufay()
  : m_xshift (0), m_yshift (0), m_width (0), m_height (0), m_red (0), m_green (0), m_blue (0)
  {
  }
  ~analyze_dufay()
  {
    free (m_red);
    free (m_green);
    free (m_blue);
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
private:
  int m_xshift, m_yshift, m_width, m_height;
  luminosity_t *m_red, *m_green, *m_blue;
};
#endif
