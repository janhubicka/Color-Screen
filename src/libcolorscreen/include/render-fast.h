#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render.h"
class render_fast : public render_to_scr
{
public:
  render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval);
  void render_pixel (int x, int y, int *r, int *g, int *b);
  void render_pixel_img (double x, double y, int *r, int *g, int *b)
  {
    double xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    render_pixel (xx + m_scr_xshift, yy + m_scr_yshift, r, g, b);
  }
private:
};
#endif
