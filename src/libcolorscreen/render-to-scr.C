#include <cassert>
#include "render-to-scr.h"
#include "screen.h"
#include "lru-cache.h"
#include "finetune-int.h"
#include "include/finetune.h"
namespace colorscreen
{
screen_table::screen_table (scanner_blur_correction_parameters *param,
                            scr_type type, luminosity_t red_strip_width,
                            luminosity_t green_strip_width,
			    const sharpen_parameters &sharpen,
                            progress_info *progress)
    : m_id (lru_caches::get ()), m_width (param->get_width ()),
      m_height (param->get_height ()), m_screen_table (m_width * m_height)
{
  screen s;
  s.initialize (type, red_strip_width, green_strip_width);
  if (progress)
    progress->set_task ("computing screen table", m_width * m_height);
#pragma omp parallel for default(none) shared(progress,sharpen) collapse(2)           \
    shared(param, s)
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        if (progress && progress->cancel_requested ())
          continue;
	switch (param->get_mode ())
	  {
	  case scanner_blur_correction_parameters::blur_radius:
	    m_screen_table[y * m_width + x].initialize_with_blur (
		s, param->get_correction (x, y)/* * (1 + (x & 1) + (y & 1))*/);
	    break;
	  case scanner_blur_correction_parameters::mtf_defocus:
	    {
	      sharpen_parameters sp = sharpen;
	      sp.scanner_mtf.defocus = param->get_correction (x, y);
	      sharpen_parameters *vv[3] = {&sp, &sp, &sp};
	      m_screen_table[y * m_width + x].initialize_with_sharpen_parameters (s, vv, false);
	      break;
	    }
	  case scanner_blur_correction_parameters::mtf_blur_diameter:
	    {
	      sharpen_parameters sp = sharpen;
	      sp.scanner_mtf.blur_diameter = param->get_correction (x, y);
	      sharpen_parameters *vv[3] = {&sp, &sp, &sp};
	      m_screen_table[y * m_width + x].initialize_with_sharpen_parameters (s, vv, false);
	      break;
	    }
	  case scanner_blur_correction_parameters::max_correction:
	    abort ();
	  }
        if (progress)
          progress->inc_progress ();
      }
}
saturation_loss_table::saturation_loss_table (
    screen_table *screen_table, screen *collection_screen, int img_width,
    int img_height, scr_to_img *map, luminosity_t collection_threshold,
    const sharpen_parameters &sharpen,
    progress_info *progress)
    : m_id (lru_caches::get ()), m_width (screen_table->get_width ()),
      m_height (screen_table->get_height ()), m_img_width (img_width),
      m_img_height (img_height),
      m_xstepinv (m_width / (coord_t)img_width),
      m_ystepinv (m_height / (coord_t)img_height),
      m_saturation_loss_table (m_width * m_height)
{
  if (progress)
    progress->set_task ("computing saturation loss table", m_width * m_height);
#pragma omp parallel for default(none) shared(progress) collapse(2)           \
    shared(screen_table, collection_screen, collection_threshold, map, sharpen)
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        int idx = y * m_width + x;
        // No progress here since we compute in parallel
        int xp = (x + 0.5) * m_img_width / m_width;
        int yp = (y + 0.5) * m_img_height / m_height;
        rgbdata cred, cgreen, cblue;
        if (determine_color_loss (
                &cred, &cgreen, &cblue, screen_table->get_screen (x, y),
		/* TODO: No support for adaptive sharpening/bluring of simulated
		   screens yet.  */
                *collection_screen, NULL,
	       	collection_threshold, sharpen, *map, xp - 100,
                yp - 100, xp + 100, yp + 100))
          {
            color_matrix sat (cred.red, cgreen.red, cblue.red, 0, //
			      cred.green, cgreen.green, cblue.green, 0, //
			      cred.blue, cgreen.blue, cblue.blue, 0, //
			      0, 0, 0, 1);
            m_saturation_loss_table[idx] = sat.invert ();
          }
        else
          {
            color_matrix id;
            m_saturation_loss_table[idx] = id;
          }
        if (progress)
          progress->inc_progress ();
      }
}



screen *
get_new_screen (struct screen_params &p, progress_info *progress)
{
  screen *s = new screen;
  if (progress)
    progress->set_task ("initializing screen", 1);
  if (p.preview)
    s->initialize_preview (p.t, p.red_strip_width, p.green_strip_width);
  else
    s->initialize (p.t, p.red_strip_width, p.green_strip_width);
  if (p.sharpen.get_mode () == sharpen_parameters::none && !p.sharpen.usm_radius)
    return s;
  screen *blurred = new screen;
  if (progress)
    progress->set_task ("bluring screen", 1);
  //if (p.sharpen.deconvolution_p ())
  if (p.sharpen.scanner_mtf_scale)
    {
      /* No need to adjust by screen::size.  If p.screen_mtf_scale == screen::size
	 we should scale exactly by it.  */
      //mtf *vv[3] = {p.sharpen.scanner_mtf.get (), p.sharpen.scanner_mtf.get (), p.sharpen.scanner_mtf.get ()};
      sharpen_parameters *vv[3] = {&p.sharpen, &p.sharpen, &p.sharpen};
      blurred->empty ();
      blurred->initialize_with_sharpen_parameters (*s, vv, p.anticipate_sharpening);
      //blurred->initialize_with_2D_fft (*s, vv, { p.sharpen.scanner_mtf_scale, p.sharpen.scanner_mtf_scale, p.sharpen.scanner_mtf_scale }, p.anticipate_sharpening ? p.sharpen.scanner_snr : 0);
      //blurred->save_tiff ("/tmp/scr.tif", false, 3);
    }
  else
    blurred->initialize_with_blur (*s, p.sharpen.usm_radius);
  delete s;
  //blurred->clamp ();
  return blurred;
}
static render_to_scr::screen_cache_t
    screen_cache ("screen");

struct screen_table_params; // will use header definition



screen_table *
get_new_screen_table (struct screen_table_params &p, progress_info *progress)
{
  screen_table *s = new screen_table (p.param, p.type, p.red_strip_width,
                                      p.green_strip_width, p.sharpen, progress);
  if (progress && progress->cancelled ())
    {
      delete s;
      return NULL;
    }
  return s;
}
static render_to_scr::screen_table_cache_t
    screen_table_cache ("screen table");

saturation_loss_table *
get_new_saturation_loss_table (struct saturation_loss_params &p,
                               progress_info *progress)
{
  saturation_loss_table *s = new saturation_loss_table (
      p.scr_table, p.collection_screen, p.img_width, p.img_height, p.map,
      p.collection_threshold, p.sharpen, progress);
  if (progress && progress->cancelled ())
    {
      delete s;
      return NULL;
    }
  return s;
}
static render_to_scr::saturation_loss_table_cache_t
    saturation_loss_table_cache ("saturation loss table");

/* Return approximate size of an scan pixel in screen corrdinates.  */
coord_t
render_to_scr::pixel_size ()
{
  return m_scr_to_img.pixel_size (m_img.width, m_img.height);
}

bool
render_to_scr::precompute_all (bool grayscale_needed, bool normalized_patches,
                               progress_info *progress)
{
  return render::precompute_all (grayscale_needed, normalized_patches,
                                 normalized_patches ? m_scr_to_img.patch_proportions (&m_params) : (rgbdata){1.0/3, 1.0/3, 1.0/3},
                                 progress);
}
bool
render_to_scr::precompute (bool grayscale_needed, bool normalized_patches,
                           coord_t, coord_t, coord_t, coord_t,
                           progress_info *progress)
{
  return precompute_all (grayscale_needed, normalized_patches, progress);
}
bool
render_to_scr::precompute_img_range (bool grayscale_needed,
                                     bool normalized_patches, coord_t, coord_t,
                                     coord_t, coord_t, progress_info *progress)
{
  return precompute_all (grayscale_needed, normalized_patches, progress);
}

/* Compute screen of type T possibly in PREVIEW.
   Blur it according to SHARPEN parameters and if ANTICIPATE_SHARPENING
   is true, sharpen it back (so we get an estimate of what happens after
   sharpening step of scan).  */

render_to_scr::screen_cache_t::cached_ptr
render_to_scr::get_screen (enum scr_type t, bool preview, 
			   bool anticipate_sharpening,
			   const sharpen_parameters &sharpen,
                           coord_t red_strip_width, coord_t green_strip_width,
                           progress_info *progress, uint64_t *id)
{
  screen_params p = { t, preview, red_strip_width, green_strip_width, anticipate_sharpening, sharpen};
  return screen_cache.get_cached (p, progress, id);
}

screen *
render_to_scr::get_screen_raw (enum scr_type t, bool preview, 
			       bool anticipate_sharpening,
			       const sharpen_parameters &sharpen,
                               coord_t red_strip_width, coord_t green_strip_width,
                               progress_info *progress, uint64_t *id)
{
  screen_params p = { t, preview, red_strip_width, green_strip_width, anticipate_sharpening, sharpen};
  return screen_cache.get (p, progress, id);
}


void
render_to_scr::release_screen (screen *s)
{
  screen_cache.release (s);
}



bool
render_to_scr::compute_screen_table (progress_info *progress)
{
  assert (!m_screen_table);
  screen_table_params p
      = { m_params.scanner_blur_correction.get (),
          m_params.scanner_blur_correction->id, m_scr_to_img.get_type (),
          m_params.red_strip_width, m_params.green_strip_width };
  if (m_params.scanner_blur_correction->get_mode () != scanner_blur_correction_parameters::blur_radius)
    {
      p.sharpen = m_params.sharpen;
      p.sharpen.scanner_mtf_scale *= pixel_size ();
    }
  m_screen_table = screen_table_cache.get_cached (p, progress, &m_screen_table_uid);
  return (bool)m_screen_table;
}

bool
render_to_scr::compute_saturation_loss_table (
    screen *collection_screen, uint64_t collection_screen_uid,
    luminosity_t collection_threshold,
    const sharpen_parameters &sharpen,
    progress_info *progress)
{
  assert (!m_saturation_loss_table);
  if (!m_screen_table)
    compute_screen_table (progress);
  scr_to_img_parameters dummy;
  saturation_loss_params p
      = { m_screen_table.get (),
          m_screen_table_uid,
          collection_screen,
          collection_screen_uid,
          m_img.width,
          m_img.height,
          collection_threshold,
	  sharpen,
          m_scr_to_img_param.mesh_trans ? m_scr_to_img_param.mesh_trans->id
                                        : 0,
          m_scr_to_img_param.mesh_trans ? dummy : m_scr_to_img_param,
          &m_scr_to_img };
  m_saturation_loss_table = saturation_loss_table_cache.get_cached (p, progress);
  return (bool)m_saturation_loss_table;
}

void
render_to_scr::simulate_screen (progress_info *progress)
{
  if (m_simulated_screen)
    return;
  uint64_t screen_id;
  coord_t psize = pixel_size ();
  sharpen_parameters sharpen = m_params.sharpen;
  sharpen.usm_radius = m_params.screen_blur_radius * psize;
  sharpen.scanner_mtf_scale *= psize;
  screen_cache_t::cached_ptr scr = get_screen (m_scr_to_img.get_type (), false,
	       false,
	       sharpen,
	       m_params.red_strip_width,
	       m_params.green_strip_width, progress, &screen_id);
  m_simulated_screen =
    get_simulated_screen (m_scr_to_img.get_param (), scr.get (), screen_id, m_params.sharpen,
			  m_img.width, m_img.height, progress,
			  &m_simulated_screen_id);
}

render_to_scr::~render_to_scr ()
{
}

void
render_img::get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
                            int height, coord_t pixelsize,
                            progress_info *progress)
{
  if (m_profiled)
    downscale<render_img, rgbdata, &render_img::get_profiled_rgb_pixel,
              &account_rgb_pixel> (data, x, y, width, height, pixelsize,
                                   progress);
  else
    render::get_color_data (data, x, y, width, height, pixelsize, progress);
}

static void
init_to_color (rgbdata color, tile_parameters &tile)
{
  color = color.clamp ();
  int r = invert_gamma (color.red, -1) * 255 + 0.5;
  int g = invert_gamma (color.green, -1) * 255 + 0.5;
  int b = invert_gamma (color.blue, -1) * 255 + 0.5;

  for (int y = 0; y < tile.height; y++)
    for (int x = 0; x < tile.width; x++)
      {
	tile.pixels[x * 3 + y * tile.rowstride] = r;
	tile.pixels[x * 3 + y * tile.rowstride + 1] = g;
	tile.pixels[x * 3 + y * tile.rowstride + 2] = b;
      }
}

DLL_PUBLIC
bool
render_screen_tile (tile_parameters &tile, scr_type type,
                    const render_parameters &rparam, coord_t pixel_size,
                    enum render_screen_tile_type rst, progress_info *progress)
{
  color_matrix m;
  sharpen_parameters sp;
  bool avg = false;
  if (progress)
    progress.set_task ("rendering tile");
  if (rst >= (int)backlight_screen)
    {
      spectrum_dyes_to_xyz s;
      s.set_backlight (spectrum_dyes_to_xyz::il_D,
                       rparam.backlight_temperature);
      xyz backlight_white = s.whitepoint_xyz ();
      xyz_srgb_matrix a;
      rgbdata backlight;
      a.apply_to_rgb (backlight_white.x, backlight_white.y, backlight_white.z,
                      &backlight.red, &backlight.green, &backlight.blue);
      luminosity_t max = 1 / std::max (std::max (backlight.red, backlight.green), backlight.blue);
      if (type == Random)
        type = Joly;
      if (rst == backlight_screen || rst == corrected_backlight_screen)
	{
	  init_to_color (backlight * max, tile);
	  return true;
	}

      bool spectrum_based;
      bool optimized;
      render_parameters r = rparam;
      if (rst < (int)corrected_backlight_screen)
        m = r.get_dyes_matrix (&spectrum_based, &optimized, NULL) * max;
      else
        m = r.get_rgb_to_xyz_matrix (NULL, false, patch_proportions (type, &rparam), d65_white) * max;
      m = a * m;
      if (rst == full_screen || rst == corrected_full_screen)
        avg = true;
      rst = original_screen;
    }
  if (type == Random)
    return false;
  if (rst != original_screen)
    {
      sp = rparam.sharpen;
      sp.usm_radius *= pixel_size;
      sp.scanner_mtf_scale *= pixel_size;
      if (rst != sharpened_screen || sp.mode == sharpen_parameters::none)
        sp.mode = sharpen_parameters::blur_deconvolution;
    }
  render_to_scr::screen_cache_t::cached_ptr scr = render_to_scr::get_screen (
      type, false, rst == sharpened_screen, sp, rparam.red_strip_width,
      rparam.green_strip_width, progress);
  if (!scr)
    return false;
  /* For small renders do just one period of screen. For bigger do multiple.  */
  int mult = tile.width > 100 ? 3 : 1;
  if (!avg)
    for (int y = 0; y < tile.height; y++)
      for (int x = 0; x < tile.width; x++)
        {
          rgbdata wd
              = scr->interpolated_mult ({ x * (mult / ((coord_t)tile.width)),
                                          y * (mult / ((coord_t)tile.height)) });
          m.apply_to_rgb (wd.red, wd.green, wd.blue, &wd.red, &wd.green,
                          &wd.blue);
          // wd = (wd * 0.9) + (rgbdata){0.1,0.1,0.1};
          wd = wd.clamp ();
          tile.pixels[x * 3 + y * tile.rowstride]
              = invert_gamma (wd.red, -1) * 255 + 0.5;
          tile.pixels[x * 3 + y * tile.rowstride + 1]
              = invert_gamma (wd.green, -1) * 255 + 0.5;
          tile.pixels[x * 3 + y * tile.rowstride + 2]
              = invert_gamma (wd.blue, -1) * 255 + 0.5;
        }
  else
    {
      rgbdata sum = { 0, 0, 0 };
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          sum += rgbdata{ scr->mult[y][x][0], scr->mult[y][x][1],
                   scr->mult[y][x][2] };
      sum *= 1 / (luminosity_t)(screen::size * screen::size);
      m.apply_to_rgb (sum.red, sum.green, sum.blue, &sum.red, &sum.green,
                      &sum.blue);
      init_to_color (sum, tile);
    }

  return true;
}

}
