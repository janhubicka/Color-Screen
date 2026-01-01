#include "simulate.h"
#include "render-to-scr.h"
#include "lru-cache.h"
#include "sharpen.h"
#include "deconvolute.h"

namespace colorscreen
{


namespace
{
struct simulated_screen_params
{
  uint64_t screen_id;
  uint64_t mesh_trans_id;
  int width, height;
  scr_to_img_parameters params;
  sharpen_parameters sharpen;
  screen *scr;
  bool
  operator== (simulated_screen_params &o)
  {
    return screen_id == o.screen_id
	   && mesh_trans_id == o.mesh_trans_id
	   && (mesh_trans_id || params == o.params)
	   && sharpen == o.sharpen;
  };
};


struct get_pixel_data
{
  screen *scr;
  scr_to_img &map;
};

inline rgbdata
get_pixel (get_pixel_data *p, int x, int y, int, int)
{
  const int steps = 5;
  rgbdata d = {0, 0, 0};
  for (int xx = 0; xx < steps; xx++)
    for (int yy = 0; yy < steps; yy++)
      d += p->scr->interpolated_mult (
	p->map.to_scr ({ x + (xx + 1) / (coord_t)(steps + 1),
			y + (yy + 1) / (coord_t)(steps + 1) }));
  return d * 1 / (coord_t)(steps * steps);
}

/* Render screen to IMG.  This is used for unit-testing of the screen
   discovery.  */

void
render_simulated_screen (simulated_screen &img,
			 const simulated_screen_params &p,
			 progress_info *progress)
{
  scr_to_img map;
  map.set_parameters (p.params, p.width, p.height);
  struct get_pixel_data pd = {p.scr, map};
  int stack = 0;
  if (progress)
    progress->set_task ("simulating scan of the screen filter",1);
  if (progress)
    stack = progress->push ();

#if 0
  //coord_t pixel_size = map.pixel_size (width, height);
  /* TODO handle screen blur radius.  */
  //sharpen.usm_radius = .screen_blur_radius * pixel_size;
  //sharpen.scanner_mtf_scale *= pixel_size;
  for (int y = 0; y < p.height; y++)
    for (int x = 0; x < p.width; x++)
      {
        const int steps = 5;
        rgbdata d = { 0, 0, 0 };
        for (int xx = 0; xx < steps; xx++)
          for (int yy = 0; yy < steps; yy++)
            d += p.scr->interpolated_mult (
                map.to_scr ({ x + (xx + 1) / (coord_t)(steps + 1),
                              y + (yy + 1) / (coord_t)(steps + 1) }));
        d *= 1 / (coord_t)(steps * steps);
        img[y * p.width + x] = d;
      }
#endif
      if (!p.sharpen.deconvolution_p ())
	sharpen<rgbdata, simulated_screen_pixel, get_pixel_data *, int, get_pixel> (
	    img.data (), &pd, p.width, p.height, p.height,
	    p.sharpen.get_mode () == sharpen_parameters::none ? 0 : p.sharpen.usm_radius, p.sharpen.usm_amount, progress, true);
      else
	{
	  deconvolute_rgb<rgbdata, simulated_screen_pixel, get_pixel_data *, int, get_pixel> (
	      img.data (), &pd, p.width, p.height, p.height,
	      p.sharpen, progress, true);
	}
  if (progress)
    progress->pop (stack);
}

simulated_screen *
get_new_simulated_screen (simulated_screen_params &p, progress_info *progress)
{
  simulated_screen *img = new simulated_screen (p.width * p.height);
  render_simulated_screen (*img, p, progress);
  return img;
}
/* To improve interactive response we cache conversion tables.  */
static lru_cache<simulated_screen_params, simulated_screen, simulated_screen *,
                 get_new_simulated_screen, 1>
    simulated_screen_cache ("simulated screens");

}

simulated_screen *
get_simulated_screen (scr_to_img_parameters &param,
		      screen *scr, uint64_t screen_id,
		      sharpen_parameters sharpen,
		      int width, int height, progress_info *progress,
		      uint64_t *id)
{
  simulated_screen_params p = {screen_id, param.mesh_trans ? param.mesh_trans->id : 0,
			       width, height, param, sharpen, scr};
  return simulated_screen_cache.get (p, progress, id);
}
void
release_simulated_screen (simulated_screen *s)
{
  simulated_screen_cache.release (s);
}

}
