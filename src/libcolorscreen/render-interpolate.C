/* Render tools for color screen reconstruction using interpolation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <assert.h>
#include <memory>
#include <limits>
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "include/dufaycolor.h"
#include "nmsimplex.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "finetune-int.h"
#include "demosaic.h"

namespace colorscreen
{

namespace
{

/* Parameters for tile-based analysis caching.  */
struct analyzer_params
{
  uint64_t img_id;
  uint64_t graydata_id;
  uint64_t simulated_screen_id;
  uint64_t screen_id;
  luminosity_t gamma;
  enum analyze_base::mode mode;
  luminosity_t collection_threshold;
  uint64_t mesh_trans_id;
  scr_to_img_parameters params;

  const image_data *img;
  const screen *scr;
  class render_to_scr *render;
  class scr_to_img *scr_to_img_map;
  struct simulated_screen *simulated_screen_ptr;

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const analyzer_params &o) const
  {
    if (mode != o.mode || mesh_trans_id != o.mesh_trans_id
	|| simulated_screen_id != o.simulated_screen_id
        || (!mesh_trans_id && params != o.params)
        || params.type != o.params.type)
      return false;
    if (mode == analyze_base::color || mode == analyze_base::precise_rgb)
      {
        if (img_id != o.img_id || gamma != o.gamma)
          return false;
      }
    else if (graydata_id != o.graydata_id)
      return false;
    if (mode == analyze_base::fast || mode == analyze_base::color)
      return true;
    return screen_id == o.screen_id
           && collection_threshold == o.collection_threshold;
  }
};

/* Parameters for demosaicing caching.  */
template <typename ANALYZER>
struct demosaiced_params
{
  uint64_t analyzer_id;

  luminosity_t dark_point;
  luminosity_t scan_exposure;
  contact_copy_parameters contact_copy;
  render_parameters::screen_demosaic_t alg;
  denoise_parameters screen_denoise;

  ANALYZER *analyzer;
  class render_interpolate *r;

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const demosaiced_params &o) const
  {
    return analyzer_id == o.analyzer_id
	   && dark_point == o.dark_point
	   && scan_exposure == o.scan_exposure
	   && contact_copy == o.contact_copy
	   && alg == o.alg
	   && screen_denoise.equal_p (o.screen_denoise);
  }
};

/* Factory function for Dufay color analysis.  */
std::unique_ptr<analyze_dufay>
get_new_dufay_analysis (struct analyzer_params &p, int_image_area area,
                        progress_info *progress)
{
  auto ret = std::make_unique<analyze_dufay> ();
  {
    const screen *s = p.scr;
    screen adapted;
    if (!p.scr)
      ;
    else if (p.params.type == DioptichromeB)
      {
        s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
            {
              adapted.mult[y][x][0] = p.scr->mult[y][x][1];
              adapted.mult[y][x][1] = p.scr->mult[y][x][0];
              adapted.mult[y][x][2] = p.scr->mult[y][x][2];
            }
      }
    else if (p.params.type == ImprovedDioptichromeB
             || p.params.type == Omnicolore)
      {
        s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
            {
              adapted.mult[y][x][0] = p.scr->mult[y][x][2];
              adapted.mult[y][x][1] = p.scr->mult[y][x][1];
              adapted.mult[y][x][2] = p.scr->mult[y][x][0];
            }
      }
    if (ret->analyze (p.render, p.img, p.scr_to_img_map, s, p.simulated_screen_ptr,
                      area, p.mode,
                      p.collection_threshold, progress))
      return ret;
  }
  return nullptr;
}

/* Factory function for Paget color analysis.  */
std::unique_ptr<analyze_paget>
get_new_paget_analysis (struct analyzer_params &p, int_image_area area,
                        progress_info *progress)
{
  auto ret = std::make_unique<analyze_paget> ();
  if (ret->analyze (p.render, p.img, p.scr_to_img_map, p.scr, p.simulated_screen_ptr,
                    area, p.mode,
                    p.collection_threshold, progress))
    return ret;
  return nullptr;
}

/* Factory function for Paget demosaicing.  */
std::unique_ptr<demosaic_paget>
get_new_demosaic_paget (demosaiced_params<analyze_paget> &p, progress_info *progress)
{
  auto ret = std::make_unique<demosaic_paget> ();
  if (!ret->demosaic (p.analyzer, (render_to_scr *)p.r, p.alg, p.screen_denoise, progress))
    {
      return nullptr;
    }
  return ret;
}

/* Factory function for Dufay demosaicing.  */
std::unique_ptr<demosaic_dufay>
get_new_demosaic_dufay (demosaiced_params<analyze_dufay> &p, progress_info *progress)
{
  auto ret = std::make_unique<demosaic_dufay> ();
  if (!ret->demosaic (p.analyzer, (render_to_scr *)p.r, p.alg, p.screen_denoise, progress))
    {
      return nullptr;
    }
  return ret;
}

/* Factory function for strips color analysis.  */
std::unique_ptr<analyze_strips>
get_new_strips_analysis (struct analyzer_params &p, int_image_area area,
                         progress_info *progress)
{
  auto ret = std::make_unique<analyze_strips> ();
  {
    const screen *s = p.scr;
    screen adapted;
    if (!p.scr)
      ;
    else if (p.params.type == Joly)
      {
        s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
            {
              adapted.mult[y][x][0] = p.scr->mult[y][x][2];
              adapted.mult[y][x][1] = p.scr->mult[y][x][1];
              adapted.mult[y][x][2] = p.scr->mult[y][x][0];
            }
      }
    if (ret->analyze (p.render, p.img, p.scr_to_img_map, s, p.simulated_screen_ptr,
                      area, p.mode,
                      p.collection_threshold, progress))
      return ret;
  }
  return nullptr;
}

typedef lru_tile_cache<analyzer_params, analyze_dufay, get_new_dufay_analysis, 2> dufay_analyzer_cache_t;
typedef lru_tile_cache<analyzer_params, analyze_paget, get_new_paget_analysis, 2> paget_analyzer_cache_t;
typedef lru_tile_cache<analyzer_params, analyze_strips, get_new_strips_analysis, 2> strips_analyzer_cache_t;
typedef lru_cache<demosaiced_params<analyze_paget>, demosaic_paget, get_new_demosaic_paget, 2> demosaic_paget_cache_t;
typedef lru_cache<demosaiced_params<analyze_dufay>, demosaic_dufay, get_new_demosaic_dufay, 2> demosaic_dufay_cache_t;

static dufay_analyzer_cache_t dufay_analyzer_cache ("dufay analyzer");
static paget_analyzer_cache_t paget_analyzer_cache ("paget analyzer");
static strips_analyzer_cache_t strips_analyzer_cache ("strips analyzer");
static demosaic_paget_cache_t demosaic_paget_cache ("paget demosaic");
static demosaic_dufay_cache_t demosaic_dufay_cache ("dufay demosaic");

}

render_interpolate::render_interpolate (const scr_to_img_parameters &param,
                                        const image_data &img,
                                        render_parameters &rparam,
                                        int dst_maxval)
    : render_to_scr (param, img, rparam, dst_maxval), m_screen (),
      m_screen_compensation (false), m_adjust_luminosity (false),
      m_original_color (false), m_unadjusted (false), m_profiled (false),
      m_precise_rgb (false), m_dufay (), m_paget (), m_strips ()
{
}
void
render_interpolate::set_render_type (render_type_parameters rtparam)
{
  m_adjust_luminosity = (rtparam.type == render_type_combined);
  m_screen_compensation = (rtparam.type == render_type_predictive);
  if (rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original)
    original_color (rtparam.type
                    == render_type_interpolated_profiled_original);
}

/* Sample pixel at screen coordinate P and return its color. Apply saturation
   loss compensation if necessary.  */

rgbdata
render_interpolate::compensate_saturation_loss_scr (point_t p, rgbdata c) const
{
  if (!m_saturation_loss_table)
    return m_saturation_matrix.apply_to_rgb (c);
  return m_saturation_loss_table->compensate_saturation_loss_img (
      m_scr_to_img.to_img (p), c);
}

/* Sample pixel at image coordinate P and return its color. Apply saturation
   loss compensation if necessary.  */

rgbdata
render_interpolate::compensate_saturation_loss_img (point_t p, rgbdata c) const
{
  if (!m_saturation_loss_table)
    {
      m_saturation_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red,
                                         &c.green, &c.blue);
      return c;
    }
  return m_saturation_loss_table->compensate_saturation_loss_img (p, c);
}

/* Precompute internal data structures for AREA. PROGRESS is used for progress
   reporting.  */

bool
render_interpolate::precompute (int_image_area area, progress_info *progress)
{
  uint64_t screen_id = 0;
  if (m_scr_to_img_param.type == Random)
    return false;
  /* When doing profiled matrix, we need to pre-scale the profile so black
     point correction goes right. Without doing so, for example black from red
     pixels would be subtracted too aggressively, since we account for every
     pixel in image, not only red patch portion.  */
  if (!render_to_scr::precompute_img_range (!m_original_color && !m_precise_rgb,
					    !m_original_color || m_profiled, area, progress))
    return false;
  if (m_screen_compensation
      || m_params.collection_quality != render_parameters::fast_collection
      || m_precise_rgb)
    {
      coord_t psize = pixel_size ();
      sharpen_parameters sharpen = m_params.sharpen;
      sharpen.usm_radius = m_params.screen_blur_radius * psize;
      sharpen.scanner_mtf_scale *= psize;

      if (sharpen.get_mode () != sharpen_parameters::none
          && m_params.collection_quality
                 == render_parameters::simulated_screen_collection)
        simulate_screen (progress);

      m_screen = get_screen (m_scr_to_img.get_type (), false,
                             sharpen.deconvolution_p (), sharpen,
                             m_params.red_strip_width,
                             m_params.green_strip_width, progress, &screen_id);
      if (!m_screen)
        return false;
      if (!m_original_color && !m_precise_rgb)
        {
          if (m_params.scanner_blur_correction)
            compute_saturation_loss_table (m_screen.get (), screen_id,
                                           m_params.collection_threshold,
                                           m_params.sharpen, progress);
          else
            {
              rgbdata cred, cgreen, cblue;
              sharpen_parameters sharpen = m_params.sharpen;
              sharpen.usm_radius = m_params.screen_blur_radius * psize;
              sharpen.scanner_mtf_scale *= psize;
              std::shared_ptr<screen> scr = get_screen (m_scr_to_img.get_type (), false, false,
                                        sharpen, m_params.red_strip_width,
                                        m_params.green_strip_width, progress,
                                        &screen_id);
              if (determine_color_loss (
                      &cred, &cgreen, &cblue, *scr, *m_screen,
                      m_simulated_screen.get (), m_params.collection_threshold,
                      m_params.sharpen, m_scr_to_img,
		      {m_img.width / 2 - 100, m_img.height / 2 - 100, 200, 200}))
                {
		  color_matrix sat (cred.red, cgreen.red, cblue.red,
				    (luminosity_t)0,
				    cred.green, cgreen.green, cblue.green,
				    (luminosity_t)0,
				    cred.blue, cgreen.blue, cblue.blue,
				    (luminosity_t)0,
				    (luminosity_t)0, (luminosity_t)0, (luminosity_t)0, (luminosity_t)1);
		  /* cred is result of data collection of red element; this means cred.blue is amount
		     of blue collected to red.  So when screen is fully red, we will account
		     cred.red, cgreen.red, cblue.red.   This is why we need to transpose.  */
		  sat.transpose ();
		  m_saturation_matrix = sat.invert ();
#if 0
		  m_saturation_matrix.apply_to_rgb (rgbdata(1,0,0)).print (stdout);
		  m_saturation_matrix.print (stdout);
		  m_saturation_matrix.apply_to_rgb (cred).print (stdout);
		  m_saturation_matrix.apply_to_rgb (cgreen).print (stdout);
		  m_saturation_matrix.apply_to_rgb (cblue).print (stdout);
#endif
                }
            }
        }
    }
  int_image_area full_range (m_scr_to_img.get_range (m_img.width, m_img.height));
  int_image_area analysis_area = area;
  //printf ("full %i %i %i %i\n", full_range.x, full_range.y, full_range.width, full_range.height);
  //printf ("area %i %i %i %i\n", area.x, area.y, area.width, area.height);

  /* If area is significantly larger then half of image, just compute whole image.  */
  if (analysis_area.width * analysis_area.height > full_range.width * full_range.height / 2)
    analysis_area = full_range;
  
  /* For UI response,
     it is better to compute whole image then significant portion of it.  */
  /* We need to compute bit more to get interpolation right.
     TODO: figure out how much.  */
  analysis_area.x -= 5;
  analysis_area.y -= 5;
  analysis_area.width += 9;
  analysis_area.height += 9;

  struct analyzer_params p{
    m_img.id,
    m_gray_data_id,
    m_simulated_screen_id,
    screen_id,
    m_params.gamma,
    m_original_color
        ? analyze_base::precise_rgb
        : (m_precise_rgb ? analyze_base::precise_rgb
                         : (m_params.collection_quality
                                     == render_parameters::fast_collection
                                 ? analyze_base::fast
                                 : analyze_base::precise)),
    m_params.collection_threshold,
    m_scr_to_img.get_param ().mesh_trans
        ? m_scr_to_img.get_param ().mesh_trans->id
        : 0,
    m_scr_to_img.get_param (),
    &m_img,
    m_screen.get (),
    this,
    &m_scr_to_img,
    m_simulated_screen.get ()
  };
  if (paget_like_screen_p (m_scr_to_img.get_type ()))
    {
      uint64_t id;
      m_paget = paget_analyzer_cache.get (p, analysis_area, progress, &id);
      if (!m_paget)
        return false;
      if (((int)m_params.screen_demosaic >= (int)render_parameters::hamilton_adams_demosaic
	  || m_params.screen_demosaic == render_parameters::default_demosaic)
	  && !m_original_color)
        {
	  struct demosaiced_params<analyze_paget> pp = {
	    id, m_params.dark_point, m_params.scan_exposure, m_params.contact_copy,
	    m_params.screen_demosaic == render_parameters::default_demosaic
	    ? (m_screen_compensation ? render_parameters::rcd_demosaic : render_parameters::amaze_demosaic)
	    : m_params.screen_demosaic,
	    m_params.screen_denoise,
	    m_paget.get (), this
	  };
	  m_demosaic_paget = demosaic_paget_cache.get (pp, progress);
	  if (!m_demosaic_paget)
	    return false;
        }
    }
  else if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      uint64_t id;
      m_dufay = dufay_analyzer_cache.get (p, analysis_area, progress, &id);
      if (!m_dufay)
        return false;
      if (((int)m_params.screen_demosaic >= (int)render_parameters::hamilton_adams_demosaic
	  || m_params.screen_demosaic == render_parameters::default_demosaic)
	  && !m_original_color)
        {
	  struct demosaiced_params<analyze_dufay> pp = {
	    id, m_params.dark_point, m_params.scan_exposure, m_params.contact_copy,
	    m_params.screen_demosaic == render_parameters::default_demosaic
	    ? (m_screen_compensation ? render_parameters::rcd_demosaic : render_parameters::amaze_demosaic)
	    : m_params.screen_demosaic,
	    m_params.screen_denoise,
	    m_dufay.get (), this
	  };
	  m_demosaic_dufay = demosaic_dufay_cache.get (pp, progress);
	  if (!m_demosaic_dufay)
	    return false;
        }
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      m_strips = strips_analyzer_cache.get (p, analysis_area, progress);
      if (!m_strips)
        return false;
    }
  else
    return false;
  m_interpolation_proportions = m_scr_to_img.patch_proportions (&m_params);
  if (!m_original_color)
    {
      if (m_scr_to_img.get_type () == DioptichromeB)
        std::swap (m_interpolation_proportions.red,
                   m_interpolation_proportions.green);
      else if (m_scr_to_img.get_type () == ImprovedDioptichromeB
               || m_scr_to_img.get_type () == Omnicolore)
        std::swap (m_interpolation_proportions.red,
                   m_interpolation_proportions.blue);
      else if (m_scr_to_img.get_type () == Joly)
        std::swap (m_interpolation_proportions.red,
                   m_interpolation_proportions.blue);
    }
  return !progress || !progress->cancelled ();
}

/* Sample pixel at screen coordinate P and return its color.  */

pure_attr rgbdata
render_interpolate::sample_pixel_scr (point_t p) const
{
  coord_t x = p.x, y = p.y;
  rgbdata c;
  bool adjusted = false;

  if (paget_like_screen_p (m_scr_to_img.get_type ()))
    {
      if (m_demosaic_paget)
	{
	  c = m_demosaic_paget->interpolate ({x, y}, m_interpolation_proportions, 
			  m_params.demosaiced_scaling == render_parameters::default_scaling
			  ? (m_screen_compensation ? render_parameters::bspline_scaling : render_parameters::lanczos3_scaling)
			  : m_params.demosaiced_scaling);
	  adjusted = true;
	}
      else
        c = m_paget->interpolate ({ x, y }, m_interpolation_proportions, m_params.screen_demosaic);
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      c = m_strips->interpolate ({ x, y },
                                  m_interpolation_proportions, m_params.screen_demosaic);
      if (m_original_color)
        ;
      else if (m_scr_to_img.get_type () == Joly)
        std::swap (c.red, c.blue);
    }
  else
    {
      if (m_demosaic_dufay)
	{
	  c = m_demosaic_dufay->interpolate ({x, y}, m_interpolation_proportions, 
			  m_params.demosaiced_scaling == render_parameters::default_scaling
			  ? (m_screen_compensation ? render_parameters::bspline_scaling : render_parameters::lanczos3_scaling)
			  : m_params.demosaiced_scaling);
	  adjusted = true;
	}
      else
        c = m_dufay->interpolate ({ x, y }, m_interpolation_proportions, m_params.screen_demosaic);
      if (m_original_color)
        ;
      else if (m_scr_to_img.get_type () == DioptichromeB)
        std::swap (c.red, c.green);
      else if (m_scr_to_img.get_type () == ImprovedDioptichromeB
               || m_scr_to_img.get_type () == Omnicolore)
        std::swap (c.red, c.blue);
    }
  /* TODO: With demosaicing we incorrectly first apply adjust_luminosity_ir
     (when constructing demosaiced data) and only later compensate saturation loss.
     This seems unavoidable, since we can only compensate after demosaicing.
     It seems that in this case we may need to build more complex profile?  */
  if (!m_original_color)
    c = compensate_saturation_loss_scr (p, c);
  if (m_unadjusted || adjusted)
    ;
  else if (!m_original_color)
    {
      c.red = adjust_luminosity_ir (c.red);
      c.green = adjust_luminosity_ir (c.green);
      c.blue = adjust_luminosity_ir (c.blue);
    }
  else if (m_profiled)
    {
      profile_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green,
                                   &c.blue);
      c.red = adjust_luminosity_ir (c.red);
      c.green = adjust_luminosity_ir (c.green);
      c.blue = adjust_luminosity_ir (c.blue);
    }
  else
    c = adjust_rgb (c);
  if (m_screen_compensation)
    {
      point_t pi = m_scr_to_img.to_img (p);
      coord_t lum = get_img_pixel (pi);
      rgbdata s;
      if (!m_simulated_screen)
        s = m_screen->interpolated_mult (p);
      else
	s = get_simulated_screen_pixel (pi);

      /* This is clamping logic trying to avoid too colorful artifacts
         alongs the edges.  */
#if 0
      c.red = std::max (c.red, (luminosity_t)0);
      c.green = std::max (c.green, (luminosity_t)0);
      c.blue = std::max (c.blue, (luminosity_t)0);
#endif

      luminosity_t llum = c.red * s.red + c.green * s.green + c.blue * s.blue;
      luminosity_t correction = llum ? lum / llum : lum * (luminosity_t)100;

#if 1
      luminosity_t redmin = lum - (1 - std::min (s.red, (luminosity_t)1));
      luminosity_t redmax = lum + (1 - std::min (s.red, (luminosity_t)1));
      if (c.red * correction < redmin)
        correction = redmin / c.red;
      else if (c.red * correction > redmax)
        correction = redmax / c.red;

      luminosity_t greenmin = lum - (1 - std::min (s.green, (luminosity_t)1));
      luminosity_t greenmax = lum + (1 - std::min (s.green, (luminosity_t)1));
      if (c.green * correction < greenmin)
        correction = greenmin / c.green;
      else if (c.green * correction > greenmax)
        correction = greenmax / c.green;

      luminosity_t bluemin = lum - (1 - std::min (s.blue, (luminosity_t)1));
      luminosity_t bluemax = lum + (1 - std::min (s.blue, (luminosity_t)1));
      if (c.blue * correction < bluemin)
        correction = bluemin / c.blue;
      else if (c.blue * correction > bluemax)
        correction = bluemax / c.blue;
#endif
      correction = std::clamp (correction, (luminosity_t)0.0, (luminosity_t)5.0);

      return c * correction;
    }
  else if (m_adjust_luminosity)
    {
      luminosity_t l = get_img_pixel_scr (p);
      luminosity_t red2, green2, blue2;
      out_color.m_color_matrix.apply_to_rgb (c.red, c.green, c.blue, &red2, &green2,
                                   &blue2);
      // TODO: We really should convert to XYZ and determine just Y.
      luminosity_t gr = (red2 * rwght + green2 * gwght + blue2 * bwght);
      if (gr <= (luminosity_t)1e-5 || l <= (luminosity_t)1e-5)
        red2 = green2 = blue2 = l;
      else
        {
          gr = l / gr;
          red2 *= gr;
          green2 *= gr;
          blue2 *= gr;
        }
      // TODO: Inverse color matrix can be stored.
      out_color.m_color_matrix.invert ().apply_to_rgb (red2, green2, blue2, &c.red,
                                             &c.green, &c.blue);
      return c;
    }
  else
    return c;
}

/* Increase LRU cache sizes for stitch projects by N.  */

void
render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  /* Triple size, since we have 3 modes.  */
  dufay_analyzer_cache.increase_capacity (3 * n);
  paget_analyzer_cache.increase_capacity (3 * n);
  strips_analyzer_cache.increase_capacity (3 * n);
  demosaic_paget_cache.increase_capacity (3 * n);
}

/* Store downscaled image data starting at P of size WIDTH x HEIGHT to DATA.  */

bool
render_interpolate::get_color_data (rgbdata *data, point_t p,
                                    int width, int height, coord_t pixelsize,
                                    progress_info *progress)
{
  return downscale<render_interpolate, rgbdata, &render_interpolate::fast_sample_pixel_img> (
      data, p, width, height, pixelsize, progress);
}

/* Run ANALYZE on every screen point in the given AREA. If SCREEN is true,
   AREA is in screen coordinates, otherwise it is in image coordinates.
   PROGRESS is used for progress reporting.  */

bool
render_interpolate::analyze_patches (analyzer analyze, const char *task,
                                     bool screen, int_image_area area,
                                     progress_info *progress)
{
  assert (!m_precise_rgb && !m_original_color);
  if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_dufay->get_height ());
      for (int y = 0; y < m_dufay->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_dufay->get_width (); x++)
              {
                coord_t xs = x - m_dufay->get_xshift (),
                        ys = y - m_dufay->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata c = m_dufay->screen_tile_color (x, y);
                if (m_scr_to_img.get_type () == DioptichromeB)
                  std::swap (c.red, c.green);
                else if (m_scr_to_img.get_type () == ImprovedDioptichromeB
                         || m_scr_to_img.get_type () == Omnicolore)
                  std::swap (c.red, c.blue);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_strips->get_height ());
      for (int y = 0; y < m_strips->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_strips->get_width (); x++)
              {
                coord_t xs = x - m_strips->get_xshift (),
                        ys = y - m_strips->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata c = m_strips->screen_tile_color (x, y);
                if (m_scr_to_img.get_type () == Joly)
                  std::swap (c.blue, c.red);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else
    {
      if (progress)
        progress->set_task (task, m_paget->get_height ());
      for (int y = 0; y < m_paget->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_paget->get_width (); x++)
              {
                coord_t xs = x - m_paget->get_xshift (),
                        ys = y - m_paget->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata c = m_paget->screen_tile_color (x, y);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  return !progress || !progress->cancelled ();
}

/* Run ANALYZE on every screen point in the given AREA. If SCREEN is true,
   AREA is in screen coordinates, otherwise it is in image coordinates.
   Rendering must be initialized in precise_rgb mode from infrared channel.  */

bool
render_interpolate::analyze_rgb_patches (rgb_analyzer analyze,
                                         const char *task, bool screen,
                                         int_image_area area, progress_info *progress)
{
  assert (m_precise_rgb && !m_original_color);
  if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_dufay->get_height ());
      for (int y = 0; y < m_dufay->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_dufay->get_width (); x++)
              {
                coord_t xs = x - m_dufay->get_xshift (),
                        ys = y - m_dufay->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata r, g, b;
                m_dufay->screen_tile_rgb_color (r, g, b, x, y);
                if (m_scr_to_img.get_type () == DioptichromeB)
                  std::swap (r, g);
                else if (m_scr_to_img.get_type () == ImprovedDioptichromeB
                         || m_scr_to_img.get_type () == Omnicolore)
                  std::swap (r, b);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_strips->get_height ());
      for (int y = 0; y < m_strips->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_strips->get_width (); x++)
              {
                coord_t xs = x - m_strips->get_xshift (),
                        ys = y - m_strips->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata r, g, b;
                if (m_scr_to_img.get_type () == Joly)
                  std::swap (b, r);
                m_strips->screen_tile_rgb_color (r, g, b, x, y);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else
    {
      if (progress)
        progress->set_task (task, m_paget->get_height ());
      for (int y = 0; y < m_paget->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_paget->get_width (); x++)
              {
                coord_t xs = x - m_paget->get_xshift (),
                        ys = y - m_paget->get_yshift ();
                if (screen
                    && (xs < area.x || ys < area.y || xs > area.x + area.width || ys > area.y + area.height))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < area.x || imgp.y < area.y || imgp.x > area.x + area.width
                            || imgp.y > area.y + area.height))
                      continue;
                  }
                rgbdata r, g, b;
                m_paget->screen_tile_rgb_color (r, g, b, x, y);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  return !progress || !progress->cancelled ();
}

/* Dump patch density to OUT.  */

bool
render_interpolate::dump_patch_density (FILE *out)
{
  if (m_dufay)
    return m_dufay->dump_patch_density (out);
  if (m_strips)
    return m_strips->dump_patch_density (out);
  if (m_paget)
    return m_paget->dump_patch_density (out);

  fprintf (stderr, "unsuported screen format\n");
  return false;
}

/* Analyze screen patches for IMG using RPARAM and PARAM in given AREA.
   If SCREEN is true, AREA is in screen coordinates, otherwise it is in image coordinates.
   ANALYZE is called for every patch.  */

bool
analyze_patches (analyzer analyze, const char *task, image_data &img,
                 render_parameters &rparam, scr_to_img_parameters &param,
                 bool screen, int_image_area area, progress_info *progress)
{
  if (img.stitch)
    {
      stitch_project &stitch = *img.stitch;
      int_image_area full_area = area;
      full_area.x += img.xmin;
      full_area.y += img.ymin;

      if (progress)
        progress->set_task ("searching for tiles", 1);
      /* It is easy to add support for screen coordinates if needed.  */
      assert (!screen);
      std::vector<stitch_project::tile_range> ranges
          = stitch.find_ranges (full_area.x, full_area.x + full_area.width,
                                full_area.y, full_area.y + full_area.height, true, true);
      if (progress)
        progress->set_task (task, ranges.size ());
      for (auto r : ranges)
        {
          int tx = r.tile_x;
          int ty = r.tile_y;
          image_data &tile = *stitch.images[ty][tx].img;
          render_parameters my_rparam = rparam;
          rparam.get_tile_adjustment (&stitch, tx, ty).apply (&my_rparam);

	  {
	    if (!analyze_patches (
		    [&] (coord_t tsx, coord_t tsy, rgbdata c)
		      {
			int ttx, tty;
			point_t src
			    = stitch.images[ty][tx].img_scr_to_common_scr (
				{ tsx, tsy });
			point_t pfin
			    = stitch.common_scr_to_img.scr_to_final (src);
			if (pfin.x < full_area.x || pfin.y < full_area.y
                            || pfin.x > full_area.x + full_area.width
			    || pfin.y > full_area.y + full_area.height
			    || !stitch.tile_for_scr (&my_rparam, src.x, src.y,
						     &ttx, &tty, true)
			    || ttx != tx || tty != ty)
			  return true;
			return analyze (pfin.x - img.xmin, pfin.y - img.ymin, c);
		      },
		    "analyzing tile", tile, rparam, stitch.images[ty][tx].param,
		    true, {(int)-r.xmin, (int)-r.ymin, (int)(r.xmax - r.xmin)+1, (int)(r.ymax - r.ymin)+1}, progress))
	      return false;
	  }
          if (progress)
            progress->inc_progress ();
        }
      return true;
    }
  render_interpolate render (param, img, rparam, 256);
  render.set_unadjusted ();
  if (!screen)
    {
      if (!render.precompute_img_range (area, progress))
        return false;
    }
  else
    {
      if (!render.precompute (area, progress))
        return false;
    }
  if (progress && progress->cancel_requested ())
    return false;
  return render.analyze_patches (analyze, task, screen, area, progress);
}

/* Analyze RGB screen patches for IMG using RPARAM and PARAM in given AREA.
   If SCREEN is true, AREA is in screen coordinates, otherwise it is in image coordinates.
   ANALYZE is called for every patch.  */

bool
analyze_rgb_patches (rgb_analyzer analyze, const char *task, image_data &img,
                     render_parameters &rparam, scr_to_img_parameters &param,
                     bool screen, int_image_area area, progress_info *progress)
{
  if (img.stitch)
    {
      stitch_project &stitch = *img.stitch;
      int_image_area full_area = area;
      full_area.x += img.xmin;
      full_area.y += img.ymin;

      /* It is easy to add support for screen coordinates if needed.  */
      assert (!screen);
      if (progress)
        progress->set_task ("searching for tiles", 1);
      std::vector<stitch_project::tile_range> ranges
          = stitch.find_ranges (full_area.x, full_area.x + full_area.width,
                                full_area.y, full_area.y + full_area.height, true, true);
      if (progress)
        progress->set_task (task, ranges.size ());
      for (auto r : ranges)
        {
          int tx = r.tile_x;
          int ty = r.tile_y;
          image_data &tile = *stitch.images[ty][tx].img;
          render_parameters my_rparam = rparam;
          rparam.get_tile_adjustment (&stitch, tx, ty).apply (&my_rparam);

	  {
	    if (!analyze_rgb_patches (
		    [&] (coord_t tsx, coord_t tsy, rgbdata r_val, rgbdata g_val,
			 rgbdata b_val)
		      {
			int ttx, tty;
			point_t src
			    = stitch.images[ty][tx].img_scr_to_common_scr (
				{ tsx, tsy });
			point_t pfin
			    = stitch.common_scr_to_img.scr_to_final (src);
			if (pfin.x < full_area.x || pfin.y < full_area.y
                            || pfin.x > full_area.x + full_area.width
			    || pfin.y > full_area.y + full_area.height
			    || !stitch.tile_for_scr (&my_rparam, src.x, src.y,
						     &ttx, &tty, true)
			    || ttx != tx || tty != ty)
			  return true;
			return analyze (pfin.x - img.xmin, pfin.y - img.ymin, r_val,
					g_val, b_val);
		      },
		    "analyzing tile", tile, rparam, stitch.images[ty][tx].param,
		    true, {(int)-r.xmin, (int)-r.ymin, (int)(r.xmax - r.xmin)+1, (int)(r.ymax - r.ymin)+1}, progress))
	      return false;
	  }
          if (progress)
            progress->inc_progress ();
        }
      return true;
    }
  render_interpolate render (param, img, rparam, 256);
  render.set_precise_rgb ();
  render.set_unadjusted ();
  if (!screen)
    {
      if (!render.precompute_img_range (area, progress))
        return false;
    }
  else
    {
      if (!render.precompute (area, progress))
        return false;
    }
  if (progress && progress->cancel_requested ())
    return false;
  return render.analyze_rgb_patches (analyze, task, screen, area, progress);
}

/* Dump patch density for SCAN using PARAM and RPARAM to OUT.  */

bool
dump_patch_density (FILE *out, image_data &scan, scr_to_img_parameters &param,
                    render_parameters &rparam, progress_info *progress)
{
  render_interpolate render (param, scan, rparam, 256);
  if (!render.precompute_all (progress))
    return false;
  return render.dump_patch_density (out);
}

/* Return deltaE 2000 difference between colors FC1 and FC2.  */

static double
get_deltae (xyz fc1, xyz fc2, long *cln = NULL, xyz *mins = NULL,
            xyz *maxs = NULL)
{
  if (mins)
    {
      if (fc1.x < mins->x)
        mins->x = fc1.x;
      if (fc1.y < mins->y)
        mins->y = fc1.y;
      if (fc1.z < mins->z)
        mins->z = fc1.z;
      if (fc2.x < mins->x)
        mins->x = fc2.x;
      if (fc2.y < mins->y)
        mins->y = fc2.y;
      if (fc2.z < mins->z)
        mins->z = fc2.z;
      if (fc1.x > maxs->x)
        maxs->x = fc1.x;
      if (fc1.y > maxs->y)
        maxs->y = fc1.y;
      if (fc1.z > maxs->z)
        maxs->z = fc1.z;
      if (fc2.x > maxs->x)
        maxs->x = fc2.x;
      if (fc2.y > maxs->y)
        maxs->y = fc2.y;
      if (fc2.z > maxs->z)
        maxs->z = fc2.z;
    }
  /* DeltaE is meaningfully defined only in the range of xyz.  */
  bool cl = false;
  if (fc1.x < 0)
    fc1.x = 0, cl = true;
  if (fc1.x > 1)
    fc1.x = 1, cl = true;
  if (fc1.y < 0)
    fc1.y = 0, cl = true;
  if (fc1.y > 1)
    fc1.y = 1, cl = true;
  if (fc1.z < 0)
    fc1.z = 0, cl = true;
  if (fc1.z > 1)
    fc1.z = 1, cl = true;
  if (fc2.x < 0)
    fc2.x = 0, cl = true;
  if (fc2.x > 1)
    fc2.x = 1, cl = true;
  if (fc2.y < 0)
    fc2.y = 0, cl = true;
  if (fc2.y > 1)
    fc2.y = 1, cl = true;
  if (fc2.z < 0)
    fc2.z = 0, cl = true;
  if (fc2.z > 1)
    fc2.z = 1, cl = true;
  if (cl && cln)
    {
      (*cln)++;
    }
  cie_lab cc1 (fc1, srgb_white);
  cie_lab cc2 (fc2, srgb_white);
  luminosity_t delta = deltaE2000 (cc1, cc2);
  if (!(delta >= 0))
    abort ();
  return delta;
}

/* Compare two rendering methods defined by PARAM1, RPARAM1 and PARAM2, RPARAM2
   for image IMG. Store average deltaE to RET_AVG and maximum deltaE to RET_MAX.  */

bool
compare_deltae (image_data &img, scr_to_img_parameters &param1,
                render_parameters &rparam1, scr_to_img_parameters &param2,
                render_parameters &rparam2, const char *cmpname,
                double *ret_avg, double *ret_max, progress_info *progress)
{
  rparam1.output_profile = render_parameters::output_profile_xyz;
  rparam2.output_profile = render_parameters::output_profile_xyz;
  rparam1.observer_whitepoint = srgb_white;
  rparam2.observer_whitepoint = srgb_white;
  render_interpolate render1 (param1, img, rparam1, 256);
  render_interpolate render2 (param2, img, rparam2, 256);
  if (!render1.precompute_all (progress) || !render2.precompute_all (progress))
    return false;
  int border = 100;
  xyz mins = { 1, 1, 1 };
  xyz maxs = { 0, 0, 0 };
  long n = 0;
  long cln = 0;
  xyz fc1, fc2;

  {
    rgbdata out1 = render1.out_color.linear_hdr_color ({1, 0, 0});
    fc1 = {out1.red, out1.green, out1.blue};
    rgbdata out2 = render2.out_color.linear_hdr_color ({1, 0, 0});
    fc2 = {out2.red, out2.green, out2.blue};
  }
  if (progress)
    progress->pause_stdout ();
  printf ("Red primary 1: ");
  fc1.print (stdout);
  printf ("Red primary 2: ");
  fc2.print (stdout);
  printf ("Red primary deltaE 2000: %f\n", get_deltae (fc1, fc2));

  {
    rgbdata out1 = render1.out_color.linear_hdr_color ({0, 1, 0});
    fc1 = {out1.red, out1.green, out1.blue};
    rgbdata out2 = render2.out_color.linear_hdr_color ({0, 1, 0});
    fc2 = {out2.red, out2.green, out2.blue};
  }
  if (progress)
    progress->pause_stdout ();
  printf ("Green primary 1: ");
  fc1.print (stdout);
  printf ("Green primary 2: ");
  fc2.print (stdout);
  printf ("Green primary deltaE 2000: %f\n", get_deltae (fc1, fc2));

  {
    rgbdata out1 = render1.out_color.linear_hdr_color ({0, 0, 1});
    fc1 = {out1.red, out1.green, out1.blue};
    rgbdata out2 = render2.out_color.linear_hdr_color ({0, 0, 1});
    fc2 = {out2.red, out2.green, out2.blue};
  }
  if (progress)
    progress->pause_stdout ();
  printf ("Blue primary 1: ");
  fc1.print (stdout);
  printf ("Blue primary 2: ");
  fc2.print (stdout);
  printf ("Blue primary deltaE 2000: %f\n", get_deltae (fc1, fc2));
  if (progress)
    progress->resume_stdout ();

  int step = 4;
  if (progress)
    progress->set_task ("comparing", (img.height - 2 * border) / step * 2);
  histogram deltaE;
  luminosity_t sum = 0;
  #pragma omp parallel for default(none) shared(progress, img, render1, render2, border, step) reduction(+:sum) reduction(histogram_range:deltaE)
  for (int y = border; y < img.height - border; y += step)
    {
      if (!progress || !progress->cancel_requested ())
        for (int x = border; x < img.width - border; x += step)
          {
            rgbdata c1 = render1.fast_sample_pixel_img ({x, y});
            rgbdata c2 = render2.fast_sample_pixel_img ({x, y});
            xyz lfc1, lfc2;
            {
              rgbdata out1 = render1.out_color.linear_hdr_color (c1);
              lfc1 = {out1.red, out1.green, out1.blue};
              rgbdata out2 = render2.out_color.linear_hdr_color (c2);
              lfc2 = {out2.red, out2.green, out2.blue};
            }
            double delta = get_deltae (lfc1, lfc2);
            deltaE.pre_account (delta);
            sum += delta;
          }
      if (progress)
        progress->inc_progress ();
    }
  deltaE.finalize_range (65535);

  if (!cmpname)
    {
      for (int y = border; y < img.height - border; y += step)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = border; x < img.width - border; x += step)
              {
                rgbdata c1 = render1.fast_sample_pixel_img ({x, y});
                rgbdata c2 = render2.fast_sample_pixel_img ({x, y});
                {
                  rgbdata out1 = render1.out_color.linear_hdr_color (c1);
                  fc1 = {out1.red, out1.green, out1.blue};
                  rgbdata out2 = render2.out_color.linear_hdr_color (c2);
                  fc2 = {out2.red, out2.green, out2.blue};
                }
                double delta = get_deltae (fc1, fc2, &cln, &mins, &maxs);
                deltaE.account (delta);
                n++;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else
    {
      tiff_writer_params par;
      const char *error;
      par.filename = cmpname;
      par.width = (img.width - 2 * border) / step;
      par.height = (img.height - 2 * border) / step;
      par.depth = 32;
      par.hdr = true;
      tiff_writer tiff (par, &error);
      if (error)
        return false;
      for (int y = border; y < img.height - border; y += step)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = border; x < img.width - border; x += step)
              {
                rgbdata c1 = render1.fast_sample_pixel_img ({x, y});
                rgbdata c2 = render2.fast_sample_pixel_img ({x, y});
                {
                  rgbdata out1 = render1.out_color.linear_hdr_color (c1);
                  fc1 = {out1.red, out1.green, out1.blue};
                  rgbdata out2 = render2.out_color.linear_hdr_color (c2);
                  fc2 = {out2.red, out2.green, out2.blue};
                }
                double delta = get_deltae (fc1, fc2, &cln, &mins, &maxs);
                deltaE.account (delta);
                n++;
                tiff.put_hdr_pixel ((x - border) / step, fc1.x, fc1.y, fc1.z);
              }
          if (progress)
            progress->inc_progress ();
          if (!tiff.write_row ())
            return false;
        }
    }
  deltaE.finalize ();
  if (progress)
    progress->pause_stdout ();
  if (mins.x < 0)
    fprintf (stderr, "Warning: minimal x is %f; clamping\n", mins.x);
  if (mins.y < 0)
    fprintf (stderr, "Warning: minimal y is %f; clamping\n", mins.y);
  if (mins.z < 0)
    fprintf (stderr, "Warning: minimal z is %f; clamping\n", mins.z);
  if (maxs.x > 1)
    fprintf (stderr, "Warning: maximal x is %f; clamping\n", maxs.x);
  if (maxs.y > 1)
    fprintf (stderr, "Warning: maximal y is %f; clamping\n", maxs.y);
  if (maxs.z > 1)
    fprintf (stderr, "Warning: maximal z is %f; clamping\n", maxs.z);
  if (cln)
    fprintf (stderr, "clamped %li pixels out of %li (%f%%)\n", cln, n,
             cln * 100.0 / n);
  printf ("Average %f\n", sum / n);
  if (progress)
    progress->resume_stdout ();

  *ret_avg = deltaE.find_avg (0, 0.01);
  *ret_max = deltaE.find_max (0.01);
  return true;
}

}
