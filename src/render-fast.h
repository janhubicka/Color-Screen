#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render.h"
class render_fast : public render_to_scr
{
public:
  render_fast (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval, int dst_maxval);
  void render_pixel (int x, int y, int *r, int *g, int *b);
private:
  double m_scale;
};
#endif