#ifndef ANALYZE_DUFAY_H
#define ANALYZE_DUFAY_H
#include "render-to-scr.h"
#include "analyze-base.h"
class analyze_dufay : public analyze_base
{
public:
  analyze_dufay()
  /* We store two reds per X coordinate.  */
  : analyze_base (1, 0, 0, 0, 0, 0)
  {
  }
  ~analyze_dufay()
  {
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

  bool analyze(render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, bool precise, luminosity_t collection_threshold, progress_info *progress = NULL);
  bool analyze_contrast (render_to_scr *render, image_data *img, scr_to_img *scr_to_img, progress_info *progress = NULL);
  luminosity_t compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress);
private:
};
#endif
