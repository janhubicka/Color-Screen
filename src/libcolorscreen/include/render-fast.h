#ifndef RENDERFAST_H
#define RENDERFAST_H
#include "render-to-scr.h"
class render_fast : public render_to_scr
{
public:
  render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval);
  void
  set_render_type (render_type_parameters)
  {
  }
  bool precompute_all (progress_info *progress)
  {
    return render_to_scr::precompute_all (true, true, progress);
  }
  bool precompute_img_range (int, int, int, int, progress_info *progress = NULL)
  {
    return precompute_all (progress);
  }
  rgbdata sample_pixel_scr (int x, int y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    return sample_pixel (x, y, xx, yy);
  }
  rgbdata sample_pixel_img (int x, int y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    return sample_pixel (xx, yy, x, y);
  }
  /* Unimplemented; just exists to make rendering templates happy. We never downscale.  */
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  {
    abort ();
  }
  void render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    coord_t xx2, yy2;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    int ix = xx + 0.5;
    int iy = yy + 0.5;
    m_scr_to_img.to_img (ix, iy, &xx2, &yy2);
    rgbdata d = sample_pixel (ix, iy, xx2, yy2);
    set_color (d.red, d.green, d.blue, r, g, b);
   }
private:
  pure_attr 
  rgbdata sample_pixel (int x, int y, coord_t zx, coord_t zy);
};
#endif
