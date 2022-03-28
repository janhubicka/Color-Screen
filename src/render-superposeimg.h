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
  void inline render_pixel_img (double x, double y, int *r, int *g, int *b);
  void inline render_pixel_img_antialias (double x, double y, double pixelsize, int steps, int *r, int *g, int *b);
private:
  void inline sample_pixel_img (double x, double y, double *r, double *g, double *b);
  screen *m_screen;
};

void
render_superpose_img::sample_pixel_img (double x, double y, double *r, double *g, double *b)
{
  double gg, rr, bb;
  double xx, yy;
  int ix, iy;

  m_scr_to_img.to_scr (x+0.5, y+0.5, &xx, &yy);
  ix = (long long) ((xx * screen::size + 0.5)) & (screen::size - 1);
  iy = (long long) ((yy * screen::size + 0.5)) & (screen::size - 1);
  double graydata = get_img_pixel (x, y);
  *r = graydata * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
  *g = graydata * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
  *b = graydata * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
}
void
render_superpose_img::render_pixel_img (double x, double y, int *r, int *g, int *b)
{
  double rr, gg, bb;
  render_superpose_img::sample_pixel_img (x, y, &rr, &gg, &bb);
  set_color (rr, gg, bb, r,g,b);
}
void
render_superpose_img::render_pixel_img_antialias (double x, double y, double pixelsize, int steps, int *r, int *g, int *b)
{
  double rr = 0, gg = 0, bb = 0;
  for (int xx = 0; xx < steps; xx ++)
    for (int yy = 0; yy < steps; yy ++)
      {
        double rrr, ggg, bbb;
        render_superpose_img::sample_pixel_img (x + xx * pixelsize / steps, y + yy * pixelsize / steps, &rrr, &ggg, &bbb);
	rr += rrr;
	bb += bbb;
	gg += ggg;
      }
  set_color (3 * rr / (steps * steps), 3 * gg / (steps * steps), 3 * bb / (steps * steps), r,g,b);
}
#endif
