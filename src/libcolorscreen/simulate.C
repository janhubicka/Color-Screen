/* Finite-image simulation of a periodic historical color screen.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "deconvolve.h"
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "render-to-scr.h"
#include "sharpen.h"
#include "simulate.h"

namespace colorscreen
{

/* Data passed to the pixel callback used by SHARPEN and DECONVOLVE_RGB.  SCR is
   the periodic filter after its pre-sampling transfer has been applied.  MAP
   converts capture-pixel coordinates to periodic screen coordinates.  */
struct get_pixel_data
{
  /* Periodic filter to sample.  */
  const screen *scr;
  /* Mapping from capture pixels to periodic filter coordinates.  */
  scr_to_img &map;
};

/* Return point-sampled capture pixel PT from P.  Use this callback only when
   the periodic screen already contains sensor-aperture attenuation.  */
inline rgbdata
get_point_pixel (get_pixel_data *p, int_point_t pt, int, int)
{
  return noantialias_screen (*p->scr, p->map, pt.x, pt.y);
}

/* Return pixel-integrated capture pixel PT from P.  Use this callback only
   when the periodic screen does not already contain sensor-aperture
   attenuation.  */
inline rgbdata
get_integrated_pixel (get_pixel_data *p, int_point_t pt, int, int)
{
  return antialias_screen (*p->scr, p->map, pt.x, pt.y);
}

/* Sample and digitally filter periodic screen P into IMG using callback
   GET_PIXEL.  PIXEL_DATA supplies the source screen and image-to-screen map.
   Update PROGRESS and return false if filtering or cancellation fails.  The
   callback is selected outside the pixel loop so point sampling does not pay
   for a per-pixel policy branch.  */
template <rgbdata (*GET_PIXEL) (get_pixel_data *, int_point_t, int, int)>
static bool
filter_simulated_screen (simulated_screen &img,
                         const simulated_screen_params &p,
                         get_pixel_data *pixel_data,
                         progress_info *progress)
{
  bool success;
  if (!p.sharpen.deconvolution_p ())
    success = sharpen<rgbdata, simulated_screen_pixel, get_pixel_data *, int,
                      GET_PIXEL> (
        img.data (), pixel_data, 0, p.width, p.height,
        p.sharpen.get_mode () == sharpen_parameters::none
            ? 0
            : p.sharpen.usm_radius,
        p.sharpen.usm_amount, progress, true);
  else
    success = deconvolve_rgb<rgbdata, simulated_screen_pixel,
                             get_pixel_data *, int, GET_PIXEL> (
        img.data (), pixel_data, 0, p.width, p.height, p.sharpen, progress,
        true);
  return success && !(progress && progress->cancelled ());
}

/* Render the periodic screen specified by P to finite image IMG.  First apply
   the explicit pixel-sampling operation in P.SAMPLING, then apply the digital
   filter in P.SHARPEN.  Update PROGRESS.  Return false if mapping, filtering,
   or cancellation fails.  */

static bool
render_simulated_screen (simulated_screen &img,
                         const simulated_screen_params &p,
                         progress_info *progress)
{
  scr_to_img map;
  if (!map.set_parameters (p.params, p.width, p.height))
    return false;
  struct get_pixel_data pd = { p.scr, map };
  if (progress)
    progress->set_task ("simulating scan of the screen filter", 1);
  sub_task task (progress);

  switch (p.sampling)
    {
    case screen_sampling::point_sample:
      if (!filter_simulated_screen<get_point_pixel> (img, p, &pd, progress))
        return false;
      break;
    case screen_sampling::integrate_pixel:
      if (!filter_simulated_screen<get_integrated_pixel> (img, p, &pd,
                                                           progress))
        return false;
      break;
    default:
      return false;
    }

  /* Disabled diagnostic output retained for local investigations.  */
  if (0)
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
              return false;
            }
        }
    }
  return true;
}

/* Construct a finite simulated screen for P.  Return null when the dimensions,
   source screen, mapping, filtering, or cancellation are invalid.  */
std::unique_ptr<simulated_screen>
get_new_simulated_screen (simulated_screen_params &p, progress_info *progress)
{
  if (!p.scr || p.width <= 0 || p.height <= 0)
    return nullptr;
  auto img = std::make_unique<simulated_screen> (p.width, p.height);
  if (!render_simulated_screen (*img, p, progress))
    return nullptr;
  return img;
}

/* Cache the most recent finite simulation to improve interactive response.  */
static simulated_screen_cache_t
    simulated_screen_cache ("simulated screens");

/* Return a cached finite simulation for PARAM and SCR.  SCREEN_ID identifies
   SCR, SAMPLING specifies pixel-aperture ownership, WIDTH and HEIGHT specify
   output dimensions, SHARPEN specifies the post-sampling digital filter, and
   ID receives the cache-entry identifier.  Update PROGRESS and return null on
   failure or cancellation.  */
std::shared_ptr<simulated_screen>
get_simulated_screen (const scr_to_img_parameters &param, const screen *scr,
                      uint64_t screen_id, screen_sampling sampling,
                      const sharpen_parameters sharpen, int width, int height,
                      progress_info *progress, uint64_t *id)
{
  simulated_screen_params p
      = { screen_id, param.mesh_trans ? param.mesh_trans->id : 0,
          width,     height, sampling,
          param,     sharpen,
          scr };
  return simulated_screen_cache.get (p, progress, id);
}

}
