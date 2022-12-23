#ifndef ANALYZE_PAGET_H
#include "include/render-to-scr.h"
#include "include/progress-info.h"
class analyze_paget
{
public:
  analyze_paget()
  : m_xshift (0), m_yshift (0), m_width (0), m_height (0), m_red (0), m_green (0), m_blue (0)
  {
  }
  luminosity_t &blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width * 2 - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_blue [y * m_width * 2 + x];
    }
  luminosity_t &red (int x, int y)
    { 
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_red [y * m_width + x];
    }
  luminosity_t &green (int x, int y) 
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_green [y * m_width + x];
    }
  /* Diagonal cooredinates have coordiate vectors (0.5,0.5) and (-0.5,0.5)  */
  inline static void to_diagonal_coordinates (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    *xx = x - y;
    *yy = x + y;
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t &diag_green (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return green (xx / 2, yy);
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t &diag_red (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return red (xx / 2, yy);
  }
  ~analyze_paget()
  {
    free (m_red);
    free (m_green);
    free (m_blue);
  }
  int get_xshift ()
  {
    return m_xshift;
  }
  int get_yshift ()
  {
    return m_yshift;
  }
  bool analyze(render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, bool precise, luminosity_t collection_threshold, progress_info *progress = NULL);
private:
  int m_xshift, m_yshift, m_width, m_height;
  luminosity_t *m_red, *m_green, *m_blue;
};
#endif
