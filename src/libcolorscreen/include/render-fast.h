#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render-to-scr.h"
class render_fast : public render_to_scr
{
public:
  render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval);
  bool precompute_all (progress_info *progress)
  {
    return render_to_scr::precompute_all (true, progress);
  }
  void render_pixel_scr (int x, int y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    render_pixel (x, y, xx, yy, r, g, b);
  }
  rgbdata sample_pixel_scr (int x, int y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    return sample_pixel (x, y, xx, yy);
  }
  void render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
  void render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
private:
  void render_pixel (int x, int y, coord_t zx, coord_t zy, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel (x, y, zx, zy);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
  pure_attr flatten_attr
  rgbdata sample_pixel (int x, int y, coord_t zx, coord_t zy);
};
#endif
