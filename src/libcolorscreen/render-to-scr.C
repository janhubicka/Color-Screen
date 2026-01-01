#include <cassert>
#include "render-to-scr.h"
#include "screen.h"
#include "lru-cache.h"
#include "include/finetune.h"
namespace colorscreen
{
screen_table::screen_table (scanner_blur_correction_parameters *param,
                            scr_type type, luminosity_t red_strip_width,
                            luminosity_t green_strip_width,
                            progress_info *progress)
    : m_id (lru_caches::get ()), m_width (param->get_width ()),
      m_height (param->get_height ()), m_screen_table (m_width * m_height)
{
  screen s;
  s.initialize (type, red_strip_width, green_strip_width);
  if (progress)
    progress->set_task ("computing screen table", m_width * m_height);
#pragma omp parallel for default(none) shared(progress) collapse(2)           \
    shared(param, s)
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        m_screen_table[y * m_width + x].initialize_with_blur (
            s, param->get_gaussian_blur_radius (x, y)/* * (1 + (x & 1) + (y & 1))*/);
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
                *collection_screen, collection_threshold, sharpen, *map, xp - 100,
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

namespace
{
struct screen_params
{
  enum scr_type t;
  bool preview;
  coord_t red_strip_width, green_strip_width;
  bool anticipate_sharpening;
  sharpen_parameters sharpen;

  bool
  operator== (screen_params &o)
  {
    return t == o.t && preview == o.preview 
	   && anticipate_sharpening == o.anticipate_sharpening
	   && sharpen == o.sharpen
           && (!screen_with_varying_strips_p (t)
               || (red_strip_width == o.red_strip_width
                   && green_strip_width == o.green_strip_width));
  }
};

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
  if (p.sharpen.scanner_mtf)
    {
      /* No need to adjust by screen::size.  If p.screen_mtf_scale == screen::size
	 we should scale exactly by it.  */
      //mtf *vv[3] = {p.sharpen.scanner_mtf.get (), p.sharpen.scanner_mtf.get (), p.sharpen.scanner_mtf.get ()};
      sharpen_parameters *vv[3] = {&p.sharpen, &p.sharpen, &p.sharpen};
      blurred->empty ();
      blurred->initialize_with_sharpen_parameters (*s, vv, p.anticipate_sharpening);
      //blurred->initialize_with_2D_fft (*s, vv, { p.sharpen.scanner_mtf_scale, p.sharpen.scanner_mtf_scale, p.sharpen.scanner_mtf_scale }, p.anticipate_sharpening ? p.sharpen.scanner_snr : 0);
      blurred->save_tiff ("/tmp/scr.tif", false, 3);
    }
  else
    blurred->initialize_with_blur (*s, p.sharpen.usm_radius);
  delete s;
  blurred->clamp ();
  return blurred;
}
static lru_cache<screen_params, screen, screen *, get_new_screen, 4>
    screen_cache ("screen");

struct screen_table_params
{
  scanner_blur_correction_parameters *param;
  uint64_t param_id;
  scr_type type;
  luminosity_t red_strip_width, green_strip_width;
  bool
  operator== (screen_table_params &o)
  {
    return type == o.type && param_id == o.param_id
           && red_strip_width == o.red_strip_width
           && green_strip_width == o.green_strip_width;
  }
};

screen_table *
get_new_screen_table (struct screen_table_params &p, progress_info *progress)
{
  screen_table *s = new screen_table (p.param, p.type, p.red_strip_width,
                                      p.green_strip_width, progress);
  if (progress && progress->cancelled ())
    {
      delete s;
      return NULL;
    }
  return s;
}
static lru_cache<screen_table_params, screen_table, screen_table *,
                 get_new_screen_table, 4>
    screen_table_cache ("screen table");

}
struct saturation_loss_params
{
  screen_table *scr_table;
  uint64_t scr_table_id;
  screen *collection_screen;
  uint64_t collection_screen_id;
  int img_width, img_height;
  luminosity_t collection_threshold;
  sharpen_parameters sharpen;
  uint64_t mesh_id;
  scr_to_img_parameters scr_to_img_params;
  scr_to_img *map;

  bool
  operator== (saturation_loss_params &o)
  {
    return scr_table_id == o.scr_table_id
           && collection_threshold == o.collection_threshold
           && sharpen == o.sharpen
           && img_width == o.img_width && img_height == o.img_height
           && mesh_id == o.mesh_id
           && (mesh_id || scr_to_img_params == o.scr_to_img_params);
  }
};
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
static lru_cache<saturation_loss_params, saturation_loss_table,
                 saturation_loss_table *, get_new_saturation_loss_table, 4>
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

screen *
render_to_scr::get_screen (enum scr_type t, bool preview, 
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
  m_screen_table = screen_table_cache.get (p, progress, &m_screen_table_uid);
  return m_screen_table != NULL;
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
      = { m_screen_table,
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
  return m_saturation_loss_table != NULL;
}

render_to_scr::~render_to_scr ()
{
  if (m_screen_table)
    screen_table_cache.release (m_screen_table);
  if (m_saturation_loss_table)
    saturation_loss_table_cache.release (m_saturation_loss_table);
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

}
