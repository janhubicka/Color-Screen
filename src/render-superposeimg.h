#ifndef RENDERSUPERPOSEIMG_H
#define RENDERSUPERPOSEIMG_H
#include "render.h"
#include "screen.h"
class render_superpose_img : public render
{
public:
  inline render_superpose_img (scr_to_img_parameters param, image_data &data, int dst_maxval, screen *screen)
   : render (param, data, dst_maxval),
     m_screen (screen) { }
  void inline render_pixel (double x, double y, int *r, int *g, int *b);
private:
  screen *m_screen;
};

void
render_superpose_img::render_pixel (double x, double y, int *r, int *g, int *b)
{
  double gg, rr, bb;
  double xx, yy;
  int ix, iy;

  m_scr_to_img.to_scr (x+0.5, y+0.5, &xx, &yy);
  ix = (long long) ((xx * screen::size + 0.5)) & (screen::size - 1);
  iy = (long long) ((yy * screen::size + 0.5)) & (screen::size - 1);
  double graydata = get_img_pixel (x, y);
  set_color (graydata * m_screen->mult[ix][iy][1] + m_screen->add[ix][iy][1],
	     graydata * m_screen->mult[ix][iy][0] + m_screen->add[ix][iy][0],
	     graydata * m_screen->mult[ix][iy][2] + m_screen->add[ix][iy][2],
	     r,g,b);
}
#endif
