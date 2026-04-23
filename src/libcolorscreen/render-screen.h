#ifndef RENDER_SCREEN_H
#define RENDER_SCREEN_H
#include <assert.h>
#include "render-to-scr.h"
#include "screen.h"
namespace colorscreen
{
class render_screen : public render_to_scr
{
public:
  inline render_screen (scr_to_img_parameters &param, image_data &data,
                        render_parameters &rparam, int dst_maxval)
      : render_to_scr (param, data, rparam, dst_maxval), m_screen ()
  {
  }
  void
  set_render_type (render_type_parameters rtparam)
  {
  }
  bool
  precompute_all (progress_info *progress)
  {
    sharpen_parameters sharpen;
    m_screen = get_screen (m_scr_to_img.get_type (), false, 
			   false,
			   sharpen,
                           m_params.red_strip_width,
                           m_params.green_strip_width, progress);
    return render_to_scr::precompute_all (false, false, progress);
  }
  bool
  precompute_img_range (int_image_area area, progress_info *progress = NULL)
  {
    (void)area;
    return precompute_all (progress);
  }
  inline rgbdata sample_pixel_scr (point_t p) const
  {
    return m_screen->interpolated_mult ({ p.x + (coord_t)0.5, p.y + (coord_t)0.5 });
  }
  inline rgbdata sample_pixel_img (point_t p) const
  {
    return m_screen->interpolated_mult (m_scr_to_img.to_scr ({ p.x + (coord_t)0.5, p.y + (coord_t)0.5 }));
  }
  pure_attr inline rgbdata
  sample_pixel_final (coord_t x, coord_t y) const
  {
    point_t scr = m_scr_to_img.final_to_scr (
        { x - get_final_xshift (), y - get_final_yshift () });
    return sample_pixel_scr (scr);
  }
  pure_attr inline rgbdata fast_sample_pixel_img (int_point_t p) const
  {
    return sample_pixel_img ({(coord_t)p.x, (coord_t)p.y});
  }
  bool get_color_data (rgbdata *data, point_t p, int width,
                       int height, coord_t pixelsize, progress_info *progress) override
  {
    return downscale<render_screen, rgbdata, &render_screen::fast_sample_pixel_img> (
        data, p, width, height, pixelsize, progress);
  }

private:
  std::shared_ptr<screen> m_screen;
};
}
#endif
