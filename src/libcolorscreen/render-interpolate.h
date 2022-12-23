#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include "include/render-to-scr.h"
#include "include/screen.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
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
  analyze_dufay m_dufay;
  analyze_paget m_paget;
  screen *m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;

  void render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b);

};
#endif
