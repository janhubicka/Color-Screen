#include "render-simulate.h"
#include "deconvolve.h"
#include "sharpen.h"
namespace colorscreen
{
namespace {
render_simulate_process::simulation_cache_t simulation_cache ("Simulated image layers");

luminosity_t
getdata (int, int x, int y, int width, render_simulate_process *r)
{
  return r->simulate_pixel_img (x, y);
}
}

std::vector<float> *
get_new_simulation (struct simulation_params &param, progress_info *progress)
{
  std::vector <float> *img = new std::vector <float> (param.img->width * param.img->height);
  bool ok = false;
  if (param.sharpen.deconvolution_p ())
    ok = deconvolve<luminosity_t, float,
		    int, render_simulate_process *, getdata> (
	img->data (), 0, param.r, param.img->width, param.img->height,
	param.sharpen, progress, true);
  else
    ok = sharpen<luminosity_t, float, int,
		 render_simulate_process *, getdata> (
	img->data (), 0, param.r, param.img->width, param.img->height,
	param.sharpen.get_mode () == sharpen_parameters::none ? 0 : param.sharpen.usm_radius,
	/* Blur intead of sharpen.  */
	-param.sharpen.usm_amount, progress);
  if (!ok)
    {
      delete img;
      return NULL;
    }
  return img;
}

void
render_simulate_process::get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
					 int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render_simulate_process, rgbdata,
	    &render_simulate_process::fast_sample_pixel_img, &account_rgb_pixel> (
      data, x, y, width, height, pixelsize, progress);
}
bool
render_simulate_process::precompute_all (progress_info *progress)
{
  sharpen_parameters sharpen;
  m_screen = get_screen (m_scr_to_img.get_type (), false, false, sharpen,
			 m_params.red_strip_width,
			 m_params.green_strip_width, progress);
  if (!render_to_scr::precompute_all (false, false, progress))
    return false;
  struct simulation_params p = {m_img.id, &m_img, this, m_params.gamma, m_scr_to_img.get_param (), m_params.sharpen};
  p.sharpen.mode = sharpen_parameters::blur_deconvolution;
  m_simulated = simulation_cache.get_cached (p, progress);
  return m_simulated.get() != NULL;
}
}
