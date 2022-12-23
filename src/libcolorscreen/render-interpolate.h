#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include "include/render-to-scr.h"
#include "include/screen.h"
#include "analyze-dufay.h"
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval, bool screen_compensation, bool adjust_luminosity);
  ~render_interpolate ();
  bool precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress);
  void render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
  void render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
  bool precompute_all (progress_info *progress)
  {
    int xshift, yshift, width, height;
    m_scr_to_img.get_range (0, 0, m_img.width, m_img.height, &xshift, &yshift, &width, &height);
    return precompute (-xshift, -yshift, -xshift + width, -yshift + height, progress);
  }
  bool precompute_img_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2, progress_info *progress)
  {
    int xshift, yshift, width, height;
    m_scr_to_img.get_range (x1, y1, x2, y2, &xshift, &yshift, &width, &height);
    return precompute (-xshift, -yshift, -xshift + width, -yshift + height, progress);
  }
private:
  int m_prec_xshift, m_prec_yshift, m_prec_width, m_prec_height;
  analyze_dufay m_dufay;
  luminosity_t *m_prec_red;
  luminosity_t *m_prec_green;
  luminosity_t *m_prec_blue;
  screen *m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;

  luminosity_t &prec_blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_prec_width * 2 - 1);
      y = std::min (std::max (y, 0), m_prec_height * 2 - 1);
      return m_prec_blue [y * m_prec_width * 2 + x];
    }
  luminosity_t &prec_red (int x, int y)
    { 
      x = std::min (std::max (x, 0), m_prec_width - 1);
      y = std::min (std::max (y, 0), m_prec_height * 2 - 1);
      return m_prec_red [y * m_prec_width + x];
    }
  luminosity_t &prec_green (int x, int y) 
    {
      x = std::min (std::max (x, 0), m_prec_width - 1);
      y = std::min (std::max (y, 0), m_prec_height * 2 - 1);
      return m_prec_green [y * m_prec_width + x];
    }
  luminosity_t &dufay_prec_blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_prec_width - 1);
      y = std::min (std::max (y, 0), m_prec_height - 1);
      return m_prec_blue [y * m_prec_width + x];
    }
  luminosity_t &dufay_prec_red (int x, int y)
    {
      x = std::min (std::max (x, 0), 2 * m_prec_width - 1);
      y = std::min (std::max (y, 0), m_prec_height - 1);
      return m_prec_red [y * m_prec_width * 2 + x];
    }

  luminosity_t &dufay_prec_green (int x, int y)
    {
      x = std::min (std::max (x, 0), m_prec_width - 1);
      y = std::min (std::max (y, 0), m_prec_height - 1);
      return m_prec_green [y * m_prec_width + x];
    }
  void render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b);

  /* Diagonal cooredinates have coordiate vectors (0.5,0.5) and (-0.5,0.5)  */
  inline void to_diagonal_coordinates (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    *xx = x - y;
    *yy = x + y;
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t prec_diag_green (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return prec_green (xx / 2, yy);
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t prec_diag_red (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return prec_red (xx / 2, yy);
  }
};
#endif
