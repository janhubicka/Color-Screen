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
  bool precompute_img_range (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress)
  {
    return render_to_scr::precompute (true, true, xmin, ymin, xmax, ymax, progress);
  }
  rgbdata sample_pixel_scr (int x, int y)
  {
    point_t p = m_scr_to_img.to_img ({(coord_t)x, (coord_t)y});
    return sample_pixel (x, y, p.x, p.y);
  }
  rgbdata sample_pixel_img (int x, int y)
  {
    point_t p = m_scr_to_img.to_scr ({(coord_t)x, (coord_t)y});
    /* We can not call directly sample_pixel; 
       img coordinates it expects should be representing the center of screen coordinates.  */
    return sample_pixel_scr (p.x, p.y);
  }
  /* Unimplemented; just exists to make rendering templates happy. We never downscale.  */
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  {
    abort ();
  }
  void render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - get_final_xshift (), y - get_final_yshift (), &xx, &yy);
    int ix = xx + 0.5;
    int iy = yy + 0.5;
    point_t p = m_scr_to_img.to_img ({(coord_t)ix, (coord_t)iy});
    rgbdata d = sample_pixel (ix, iy, p.x, p.y);
    set_color (d.red, d.green, d.blue, r, g, b);
   }
private:
  pure_attr 
  rgbdata sample_pixel (int x, int y, coord_t zx, coord_t zy);
};
#endif
