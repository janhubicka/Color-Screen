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
std::vector<float> *get_new_simulation (struct simulation_params &, progress_info *);
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
  bool precompute_all (progress_info *progress);
  bool
  precompute_img_range (int, int, int, int, progress_info *progress = NULL)
  {
    return precompute_all (progress);
  }
  inline luminosity_t
  simulate_pixel_img (int x, int y) const
  {
    if (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height)
      {
        rgbdata scr = m_screen->interpolated_mult (
            m_scr_to_img.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 }));
        luminosity_t rr = get_data_red (x, y), gg = get_data_green (x, y),
                     bb = get_data_blue (x, y);
        return rr * scr.red + gg * scr.green + bb * scr.blue;
      }
    return 0;
  }
  inline rgbdata
  sample_pixel_img (coord_t x, coord_t y) const
  {
    if (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height)
      {
	luminosity_t lum = (*m_simulated)[((int)y) * m_img.width + ((int)x)];
	return {lum, lum, lum};
      }
    else
      return {0, 0, 0};
  }
  inline rgbdata sample_pixel_scr (coord_t x, coord_t y) const
  {
    point_t p = m_scr_to_img.to_img ({x, y});
    return sample_pixel_img (p.x, p.y);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int x, int y) const
  {
    luminosity_t lum = (*m_simulated)[y * m_img.width + x];
    return {lum, lum, lum};
  }
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
                       int height, coord_t pixelsize, progress_info *progress);

  typedef lru_cache<simulation_params, std::vector<float>,
                    std::vector<float> *, &get_new_simulation, 1>
      simulation_cache_t;
private:
  render_to_scr::screen_cache_t::cached_ptr m_screen;
  simulation_cache_t::cached_ptr m_simulated;
};
}
#endif
