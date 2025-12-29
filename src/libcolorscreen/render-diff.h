#ifndef RENDER_DIFF_H
#define RENDER_DIFF_H
#include "render-interpolate.h"
namespace colorscreen
{
class render_diff : public render_to_scr
{
public:
  render_diff (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval)
   : render_to_scr (param, img, rparam, dst_maxval),
     r1 (param, img, rparam, dst_maxval), r2 (param, img, rparam, dst_maxval)
  {
    r2.original_color (true);
    m_brightness = m_params.brightness;
    m_params.brightness = 1;
    m_params.color_model = render_parameters::color_model_none;
    m_params.white_balance = {1, 1, 1};
    m_params.dark_point = 0;
    m_params.scan_exposure = 0;
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
  pure_attr inline rgbdata
  sample_pixel_scr (coord_t x, coord_t y) const
  {
    rgbdata c1 = r1.sample_pixel_scr (x, y);
    rgbdata c2 = r2.sample_pixel_scr (x, y);
    return {0.25-4 * (c1.red - c2.red) * m_brightness,
	    0.25-4 * (c1.green - c2.green) * m_brightness,
	    0.25-4 * (c1.blue - c2.blue) * m_brightness};
  }
  pure_attr inline rgbdata
  sample_pixel_img (coord_t x, coord_t y) const
  {
    point_t p = m_scr_to_img.to_scr ({x, y});
    return sample_pixel_scr (p.x, p.y);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int x, int y) const
  {
    point_t p = m_scr_to_img.to_scr ({x+(coord_t)0.5, y+(coord_t)0.5});
    return sample_pixel_scr (p.x, p.y);
  }
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  {
    downscale<render_diff, rgbdata, &render_diff::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
  }
private:
  luminosity_t m_brightness;
  render_interpolate r1;
  render_interpolate r2;
};
}
#endif
