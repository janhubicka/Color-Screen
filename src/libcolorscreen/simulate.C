#include "deconvolve.h"
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "render-to-scr.h"
#include "sharpen.h"
#include "simulate.h"

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
  const screen *scr;
  bool
  operator== (simulated_screen_params &o)
  {
    return screen_id == o.screen_id && mesh_trans_id == o.mesh_trans_id
           && (mesh_trans_id || params == o.params) && sharpen == o.sharpen;
  };
};

struct get_pixel_data
{
  const screen *scr;
  scr_to_img &map;
};

inline rgbdata
get_pixel (get_pixel_data *p, int x, int y, int, int)
{
  return antialias_screen (*p->scr, p->map, x, y);
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
  struct get_pixel_data pd = { p.scr, map };
  int stack = 0;
  //printf ("Simulating %f\n", p.sharpen.scanner_mtf_scale);
  if (progress)
    progress->set_task ("simulating scan of the screen filter", 1);
  if (progress)
    stack = progress->push ();

  if (!p.sharpen.deconvolution_p ())
    sharpen<rgbdata, simulated_screen_pixel, get_pixel_data *, int,
            get_pixel> (img.data (), &pd, 0, p.width, p.height,
                        p.sharpen.get_mode () == sharpen_parameters::none
                            ? 0
                            : p.sharpen.usm_radius,
                        p.sharpen.usm_amount, progress, true);
  else
    {
      deconvolve_rgb<rgbdata, simulated_screen_pixel, get_pixel_data *, int,
                    get_pixel> (img.data (), &pd, 0, p.width, p.height,
                                p.sharpen, progress, true);
    }
  //for (size_t y = 0; y < (size_t)p.height; y++)
    //for (size_t x = 0; x < (size_t)p.width; x++)
      //img.put_pixel (x, y, img.get_pixel (x, y).clamp (0, 1));

  if (1)
    {
      p.scr->save_tiff ("/tmp/simulation-scr.tif", false, 3);
      tiff_writer_params pp;
      int width = std::min (1024, p.width);
      int height = std::min (1024, p.height);
      //printf ("Saving %i %i\n", width, height);
      pp.width = width;
      pp.height = height;
      pp.depth = 16;
      const char *error;
      pp.filename = "/tmp/simulation.tif";
      tiff_writer renderedu (pp, &error);
      for (int y = 0; y < height; y++)
        {
          for (int x = 0; x < width; x++)
            {
              rgbdata m = (img.get_pixel (x, y) * (luminosity_t)65535).clamp (0, 65535);
              renderedu.put_pixel (x, m.red, m.green, m.blue);
            }
          if (!renderedu.write_row ())
            {
              printf ("Write error line %i\n", y);
              break;
            }
        }
    }
  if (progress)
    progress->pop (stack);
}

simulated_screen *
get_new_simulated_screen (simulated_screen_params &p, progress_info *progress)
{
  simulated_screen *img = new simulated_screen (p.width, p.height);
  render_simulated_screen (*img, p, progress);
  return img;
}
/* To improve interactive response we cache conversion tables.  */
static lru_cache<simulated_screen_params, simulated_screen, simulated_screen *,
                 get_new_simulated_screen, 1>
    simulated_screen_cache ("simulated screens");

}

simulated_screen *
get_simulated_screen (const scr_to_img_parameters &param, const screen *scr,
                      uint64_t screen_id, const sharpen_parameters sharpen,
                      int width, int height, progress_info *progress,
                      uint64_t *id)
{
  simulated_screen_params p
      = { screen_id, param.mesh_trans ? param.mesh_trans->id : 0,
          width,     height,
          param,     sharpen,
          scr };
  return simulated_screen_cache.get (p, progress, id);
}
void
release_simulated_screen (simulated_screen *s)
{
  simulated_screen_cache.release (s);
}

}
