/* High-level rendering to screen coordinates.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <cassert>
#include "render-to-scr.h"
#include "screen.h"
#include "lru-cache.h"
#include "finetune-int.h"
#include "include/finetune.h"

namespace colorscreen
{

namespace
{

/* Parameters for screen initialization.  */
struct screen_params
{
  enum scr_type t = Joly;
  bool preview = false;
  coord_t red_strip_width = (coord_t)0.0, green_strip_width = (coord_t)0.0;
  bool anticipate_sharpening = false;
  sharpen_parameters sharpen = {};

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const screen_params &o) const
  {
    return t == o.t && preview == o.preview 
	   && anticipate_sharpening == o.anticipate_sharpening
	   && sharpen == o.sharpen
	   /* We also blur, so we need to compare MTF if used.  */
	   && sharpen.scanner_mtf_scale == o.sharpen.scanner_mtf_scale
	   && (!sharpen.scanner_mtf_scale || sharpen.scanner_mtf == o.sharpen.scanner_mtf)
           && (!screen_with_varying_strips_p (t)
               || (red_strip_width == o.red_strip_width
                   && green_strip_width == o.green_strip_width));
  }
};

/* Return new screen for parameters P.  Update PROGRESS.  */
std::unique_ptr<screen>
get_new_screen (struct screen_params &p, progress_info *progress)
{
  auto s = std::make_unique<screen>();
  if (progress)
    progress->set_task ("initializing screen", 1);
  if (p.preview)
    s->initialize_preview (p.t, p.red_strip_width, p.green_strip_width);
  else
    s->initialize (p.t, p.red_strip_width, p.green_strip_width);
  if (p.sharpen.get_mode () == sharpen_parameters::none && !p.sharpen.usm_radius)
    return s;
  auto blurred = std::make_unique<screen>();
  if (progress)
    progress->set_task ("blurring screen", 1);
  if (p.sharpen.scanner_mtf_scale)
    {
      /* No need to adjust by screen::size.  If p.screen_mtf_scale == screen::size
	 we should scale exactly by it.  */
      sharpen_parameters *vv[3] = {&p.sharpen, &p.sharpen, &p.sharpen};
      blurred->empty ();
      blurred->initialize_with_sharpen_parameters (*s, vv, p.anticipate_sharpening);
    }
  else
    blurred->initialize_with_blur (*s, p.sharpen.usm_radius);
  return blurred;
}

typedef lru_cache<screen_params, screen, get_new_screen, 20> screen_cache_t;
static screen_cache_t screen_cache ("screen");

/* Parameters for screen table initialization.  */
struct screen_table_params
{
  scanner_blur_correction_parameters *param = nullptr;
  uint64_t param_id = 0;
  scr_type type = Joly;
  coord_t red_strip_width = (coord_t)0.0, green_strip_width = (coord_t)0.0;
  sharpen_parameters sharpen = {};

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const screen_table_params &o) const
  {
    return type == o.type && param_id == o.param_id
           && red_strip_width == o.red_strip_width
           && green_strip_width == o.green_strip_width
	   && sharpen.scanner_mtf_scale == o.sharpen.scanner_mtf_scale
	   && (!sharpen.scanner_mtf_scale || sharpen.scanner_mtf == o.sharpen.scanner_mtf)
           && sharpen == o.sharpen;
  }
};

/* Return new screen table for parameters P.  Update PROGRESS.  */
std::unique_ptr<screen_table>
get_new_screen_table (struct screen_table_params &p, progress_info *progress)
{
  auto s = std::make_unique<screen_table> (p.param, p.type, p.red_strip_width,
                                       p.green_strip_width, p.sharpen, progress);
  if (progress && progress->cancelled ())
    {
      return nullptr;
    }
  return s;
}

typedef lru_cache<screen_table_params, screen_table, get_new_screen_table, 4> screen_table_cache_t;
static screen_table_cache_t screen_table_cache ("screen table");

/* Parameters for saturation loss table initialization.  */
struct saturation_loss_params
{
  screen_table *scr_table = nullptr;
  uint64_t scr_table_id = 0;
  screen *collection_screen = nullptr;
  uint64_t collection_screen_id = 0;
  int img_width = 0, img_height = 0;
  luminosity_t collection_threshold = (luminosity_t)0.0;
  sharpen_parameters sharpen = {};
  uint64_t mesh_id = 0;
  scr_to_img_parameters scr_to_img_params = {};
  class scr_to_img *map = nullptr;

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const saturation_loss_params &o) const
  {
    return scr_table_id == o.scr_table_id
           && collection_threshold == o.collection_threshold
           && sharpen == o.sharpen
	   && sharpen.scanner_mtf_scale == o.sharpen.scanner_mtf_scale
	   && (!sharpen.scanner_mtf_scale || sharpen.scanner_mtf == o.sharpen.scanner_mtf)
           && img_width == o.img_width && img_height == o.img_height
           && mesh_id == o.mesh_id
           && (mesh_id || scr_to_img_params == o.scr_to_img_params);
  }
};

/* Return new saturation loss table for parameters P.  Update PROGRESS.  */
std::unique_ptr<saturation_loss_table>
get_new_saturation_loss_table (struct saturation_loss_params &p,
                               progress_info *progress)
{
  auto s = std::make_unique<saturation_loss_table> (
      p.scr_table, p.collection_screen, p.img_width, p.img_height, p.map,
      p.collection_threshold, p.sharpen, progress);
  if (progress && progress->cancelled ())
    {
      return nullptr;
    }
  return s;
}

typedef lru_cache<saturation_loss_params, saturation_loss_table, get_new_saturation_loss_table, 4> saturation_loss_table_cache_t;
static saturation_loss_table_cache_t saturation_loss_table_cache ("saturation loss table");

}

/* Initialize screen table for PARAM, TYPE, RED_STRIP_WIDTH, GREEN_STRIP_WIDTH and SHARPEN.
   Update PROGRESS.  */
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
		s, param->get_correction (x, y));
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

/* Initialize saturation loss table for SCREEN_TABLE, COLLECTION_SCREEN, IMG_WIDTH, IMG_HEIGHT, MAP, COLLECTION_THRESHOLD and SHARPEN.
   Update PROGRESS.  */
saturation_loss_table::saturation_loss_table (
    screen_table *screen_table, screen *collection_screen, int img_width,
    int img_height, scr_to_img *map, luminosity_t collection_threshold,
    const sharpen_parameters &sharpen,
    progress_info *progress)
    : m_id (lru_caches::get ()), m_width (screen_table->get_width ()),
      m_height (screen_table->get_height ()), m_img_width (img_width),
      m_img_height (img_height),
      m_xstepinv ((coord_t)m_width / (coord_t)img_width),
      m_ystepinv ((coord_t)m_height / (coord_t)img_height),
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
        /* No progress here since we compute in parallel.  */
        int xp = (int)((x + (coord_t)0.5) * (coord_t)m_img_width / (coord_t)m_width);
        int yp = (int)((y + (coord_t)0.5) * (coord_t)m_img_height / (coord_t)m_height);
        rgbdata cred, cgreen, cblue;
        if (determine_color_loss (
                &cred, &cgreen, &cblue, screen_table->get_screen (x, y),
		/* TODO: No support for adaptive sharpening/blurring of simulated
		   screens yet.  */
                *collection_screen, NULL,
	       	collection_threshold, sharpen, *map,
		{xp - 100, yp - 100, 200, 200}))
          {
            color_matrix sat (cred.red, cgreen.red, cblue.red, (luminosity_t)0.0,
			      cred.green, cgreen.green, cblue.green, (luminosity_t)0.0,
			      cred.blue, cgreen.blue, cblue.blue, (luminosity_t)0.0,
			      (luminosity_t)0.0, (luminosity_t)0.0, (luminosity_t)0.0, (luminosity_t)1.0);
	    sat.transpose ();
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

/* Return approximate size of an scan pixel in screen coordinates.  */
coord_t
render_to_scr::pixel_size () const noexcept
{
  return m_pixel_size;
}

/* Precompute all data needed for rendering.  Update PROGRESS.
   GRAYSCALE_NEEDED specifies if grayscale only rendering is sufficient.
   NORMALIZED_PATCHES specifies if patches should be normalized.  */
bool
render_to_scr::precompute_all (bool grayscale_needed, bool normalized_patches,
			       progress_info *progress)
{
  if (!m_ok)
    return false;
  return render::precompute_all (grayscale_needed, normalized_patches,
				 normalized_patches ? m_scr_to_img.patch_proportions (&m_params) : (rgbdata){(luminosity_t)1.0/(luminosity_t)3.0, (luminosity_t)1.0/(luminosity_t)3.0, (luminosity_t)1.0/(luminosity_t)3.0},
				 progress);
}

/* Precompute all data needed for rendering in AREA.  Update PROGRESS.
   GRAYSCALE_NEEDED specifies if grayscale only rendering is sufficient.
   NORMALIZED_PATCHES specifies if patches should be normalized.  */
bool
render_to_scr::precompute_img_range (bool grayscale_needed, bool normalized_patches, int_image_area area, progress_info *progress)
{
  (void)area;
  return render_to_scr::precompute_all (grayscale_needed, normalized_patches, progress);
}

/* Return screen of type T in PREVIEW mode.  Sharpen it according to SHARPEN parameters
   if ANTICIPATE_SHARPENING is true.  RED_STRIP_WIDTH and GREEN_STRIP_WIDTH specify
   strip widths.  Update PROGRESS and return screen unique ID in ID.  */
std::shared_ptr<screen>
render_to_scr::get_screen (enum scr_type t, bool preview, 
			   bool anticipate_sharpening,
			   const sharpen_parameters &sharpen,
                           coord_t red_strip_width, coord_t green_strip_width,
                           progress_info *progress, uint64_t *id)
{
  screen_params p = { t, preview, red_strip_width, green_strip_width, anticipate_sharpening, sharpen};
  return screen_cache.get (p, progress, id);
}

/* Release screen S.  */
void
render_to_scr::release_screen (screen *s)
{
  (void)s;
}

/* Compute screen table for PROGRESS.  */
bool
render_to_scr::compute_screen_table (progress_info *progress)
{
  assert (!m_screen_table);
  screen_table_params p
      = { m_params.scanner_blur_correction.get (),
          m_params.scanner_blur_correction->id, m_scr_to_img.get_type (),
          m_params.red_strip_width, m_params.green_strip_width, {} };
  if (m_params.scanner_blur_correction->get_mode () != scanner_blur_correction_parameters::blur_radius)
    {
      p.sharpen = m_params.sharpen;
      p.sharpen.scanner_mtf_scale *= pixel_size ();
    }
  m_screen_table = screen_table_cache.get (p, progress, &m_screen_table_uid);
  return (bool)m_screen_table;
}

/* Compute saturation loss table for COLLECTION_SCREEN with ID COLLECTION_SCREEN_UID,
   COLLECTION_THRESHOLD and SHARPEN parameters.  Update PROGRESS.  */
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
  m_saturation_loss_table = saturation_loss_table_cache.get (p, progress);
  return (bool)m_saturation_loss_table;
}

/* Simulate screen rendering for PROGRESS.  */
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
  std::shared_ptr<screen> scr = get_screen (m_scr_to_img.get_type (), false,
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

/* Compute RGB data of downscaled image.  Update PROGRESS.  */
bool
render_img::get_color_data (rgbdata *data, point_t p, int width,
                            int height, coord_t pixelsize,
                            progress_info *progress)
{
  if (m_profiled)
    return downscale<render_img, rgbdata, &render_img::get_profiled_rgb_pixel> (
        data, p, width, height, pixelsize, progress);
  else
    return render::get_color_data (data, p, width, height, pixelsize, progress);
}

/* Initialize TILE to COLOR.  */
static void
init_to_color (rgbdata color, tile_parameters &tile)
{
  color = color.clamp ();
  int r = invert_gamma (color.red, -1) * 255 + (coord_t)0.5;
  int g = invert_gamma (color.green, -1) * 255 + (coord_t)0.5;
  int b = invert_gamma (color.blue, -1) * 255 + (coord_t)0.5;

  for (int y = 0; y < tile.height; y++)
    for (int x = 0; x < tile.width; x++)
      {
	tile.pixels[x * 3 + y * tile.rowstride] = r;
	tile.pixels[x * 3 + y * tile.rowstride + 1] = g;
	tile.pixels[x * 3 + y * tile.rowstride + 2] = b;
      }
}

/* Render screen TILE of TYPE for RPARAM, PIXEL_SIZE and RST.
   Update PROGRESS.  */
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
    progress->set_task ("rendering tile", 1);
  if (rst == dot_spread)
    {
      std::shared_ptr<mtf> cur_mtf = mtf::get_mtf (rparam.sharpen.scanner_mtf, NULL);
      return cur_mtf->render_dot_spread_tile (tile, progress);
    }

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
      luminosity_t max = (luminosity_t)1.0 / std::max (std::max (backlight.red, backlight.green), backlight.blue);
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
  std::shared_ptr<screen> scr = render_to_scr::get_screen (
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
              = scr->interpolated_mult ({ (coord_t)x * ((coord_t)mult / ((coord_t)tile.width)),
                                          (coord_t)y * ((coord_t)mult / ((coord_t)tile.height)) });
          m.apply_to_rgb (wd.red, wd.green, wd.blue, &wd.red, &wd.green,
                          &wd.blue);
          wd = wd.clamp ();
          tile.pixels[x * 3 + y * tile.rowstride]
              = invert_gamma (wd.red, -1) * 255 + (coord_t)0.5;
          tile.pixels[x * 3 + y * tile.rowstride + 1]
              = invert_gamma (wd.green, -1) * 255 + (coord_t)0.5;
          tile.pixels[x * 3 + y * tile.rowstride + 2]
              = invert_gamma (wd.blue, -1) * 255 + (coord_t)0.5;
        }
  else
    {
      rgbdata sum = { (luminosity_t)0.0, (luminosity_t)0.0, (luminosity_t)0.0 };
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          sum += rgbdata{ scr->mult[y][x][0], scr->mult[y][x][1],
                   scr->mult[y][x][2] };
      sum *= (luminosity_t)1.0 / (luminosity_t)(screen::size * screen::size);
      m.apply_to_rgb (sum.red, sum.green, sum.blue, &sum.red, &sum.green,
                      &sum.blue);
      init_to_color (sum, tile);
    }

  return true;
}

}
