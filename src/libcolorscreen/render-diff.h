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
  nodiscard_attr bool
  precompute_img_range (int_image_area area, progress_info *progress)
  {
    if (!render_to_scr::precompute_img_range (false, true, {{0, 0}, {1, 1}}, progress))
      return false;
    if (!r1.precompute_img_range (area, progress))
      return false;
    return r2.precompute_img_range (area, progress);
  }
  nodiscard_attr bool
  precompute_all (progress_info *progress)
  {
    if (!render_to_scr::precompute_img_range (false, true, {{0, 0}, {1, 1}}, progress))
      return false;
    if (!r1.precompute_all (progress))
      return false;
    return r2.precompute_all (progress);
  }
  pure_attr inline rgbdata
  sample_pixel_scr (point_t p) const
  {
    rgbdata c1 = r1.sample_pixel_scr (p);
    rgbdata c2 = r2.sample_pixel_scr (p);
    return {(luminosity_t)0.25-4 * (c1.red - c2.red) * m_brightness,
	    (luminosity_t)0.25-4 * (c1.green - c2.green) * m_brightness,
	    (luminosity_t)0.25-4 * (c1.blue - c2.blue) * m_brightness};
  }
  pure_attr inline rgbdata
  sample_pixel_img (point_t pi) const
  {
    point_t p = m_scr_to_img.to_scr (pi);
    return sample_pixel_scr (p);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t pi) const
  {
    point_t p = m_scr_to_img.to_scr ({pi.x+(coord_t)0.5, pi.y+(coord_t)0.5});
    return sample_pixel_scr (p);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress) override
  {
    return downscale<render_diff, rgbdata, &render_diff::fast_sample_pixel_img> (
        data, p, width, height, pixelsize, progress);
  }
private:
  luminosity_t m_brightness;
  render_interpolate r1;
  render_interpolate r2;
};
}
#endif
