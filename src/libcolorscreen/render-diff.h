#ifndef RENDER_DIFF_H
#define RENDER_DIFF_H
#include "render-interpolate.h"
class render_diff : public render_to_scr
{
public:
  render_diff (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval)
   : render_to_scr (param, img, rparam, dst_maxval),
     r1 (param, img, rparam, dst_maxval), r2 (param, img, rparam, dst_maxval)
  {
    r2.original_color (true);
  }
  ~render_diff ()
  {
  }
  void
  set_render_type (render_type_parameters)
  {
  }
  bool
  precompute_img_range (int x1, int x2, int y1, int y2, progress_info *progress)
  {
    if (!render_to_scr::precompute (false, true, 0, 1, 0, 1, progress))
      return false;
    if (!r1.precompute_img_range (x1, x2, y1, y1, progress))
      return false;
    return r2.precompute_img_range (x1, x2, y1, y1, progress);
  }
  bool
  precompute_all (progress_info *progress)
  {
    if (!render_to_scr::precompute (false, true, 0, 1, 0, 1, progress))
      return false;
    if (!r1.precompute_all (progress))
      return false;
    return r2.precompute_all (progress);
  }
  rgbdata
  sample_pixel_scr (coord_t x, coord_t y)
  {
    rgbdata c1 = r1.sample_pixel_scr (x, y);
    rgbdata c2 = r2.sample_pixel_scr (x, y);
    return {0.25-4 * (c1.red - c2.red),
	    0.25-4 * (c1.green - c2.green),
	    0.25-4 * (c1.blue - c2.blue)};
  }
  rgbdata
  sample_pixel_img (int x, int y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    return sample_pixel_scr (xx, yy);
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    rgbdata d = sample_pixel_scr (xx, yy);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  {
    downscale<render_diff, rgbdata, &render_diff::sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
  }
private:
  render_interpolate r1;
  render_interpolate r2;
};
#endif
