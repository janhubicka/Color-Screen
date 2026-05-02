#ifndef RENDER_SIMULATE_H
#define RENDER_SIMULATE_H
#include "render-to-scr.h"
#include "screen.h"
#include <assert.h>
namespace colorscreen
{
class render_simulate_process;
struct simulation_params
{
  uint64_t image_id;
  const class image_data *img;
  render_simulate_process *r;
  luminosity_t gamma;
  scr_to_img_parameters scr_to_img;
  sharpen_parameters sharpen;

  bool
  operator== (simulation_params & o)
  {
    return image_id == o.image_id && gamma == o.gamma
	   && scr_to_img == o.scr_to_img && sharpen == o.sharpen;
  }
};
std::unique_ptr<std::vector<float>> get_new_simulation (struct simulation_params &, progress_info *);
class render_simulate_process : public render_to_scr
{
public:
  inline render_simulate_process (scr_to_img_parameters &param,
                                  image_data &data, render_parameters &rparam,
                                  int dst_maxval)
      : render_to_scr (param, data, rparam, dst_maxval), m_screen ()
  {
  }
  void
  set_render_type (render_type_parameters rtparam)
  {
  }
  nodiscard_attr bool precompute_all (progress_info *progress);
  nodiscard_attr bool precompute_img_range (int_image_area area, progress_info *progress = NULL)
  {
    (void)area;
    return precompute_all (progress);
  }
  inline luminosity_t
  simulate_pixel_img (int_point_t p) const
  {
    if (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height)
      {
        rgbdata scr = m_screen->interpolated_mult (
            m_scr_to_img.to_scr ({ p.x + (coord_t)0.5, p.y + (coord_t)0.5 }));
        luminosity_t rr = get_data_red (p), gg = get_data_green (p),
                     bb = get_data_blue (p);
        return rr * scr.red * m_proportions_cor.red + gg * scr.green * m_proportions_cor.green + bb * scr.blue * m_proportions_cor.blue;
      }
    return 0;
  }
  inline rgbdata
  sample_pixel_img (point_t p) const
  {
    if (p.x >= 0 && p.x < m_img.width && p.y >= 0 && p.y < m_img.height)
      {
	luminosity_t lum = (*m_simulated)[((int)p.y) * m_img.width + ((int)p.x)];
	return {lum, lum, lum};
      }
    else
      return {0, 0, 0};
  }
  inline rgbdata sample_pixel_scr (point_t p) const
  {
    point_t pi = m_scr_to_img.to_img (p);
    return sample_pixel_img (pi);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    luminosity_t lum = (*m_simulated)[p.y * m_img.width + p.x];
    return {lum, lum, lum};
  }
  bool get_color_data (rgbdata *data, point_t p, int width,
                       int height, coord_t pixelsize, progress_info *progress);

  typedef lru_cache<simulation_params, std::vector<float>,
                    &get_new_simulation, 1>
      simulation_cache_t;
private:
  std::shared_ptr<screen> m_screen;
  std::shared_ptr<std::vector<float>> m_simulated;
  rgbdata m_proportions_cor;
};
}
#endif
