/* Render tools for color screen reconstruction using interpolation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef RENDER_INTERPOLATE_H
#define RENDER_INTERPOLATE_H
#include <functional>
#include "include/histogram.h"
#include "solver.h"
#include "render-to-scr.h"
#include "screen.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
#include "analyze-strips.h"
#include "include/imagedata.h"
#include "include/scr-to-img-parameters.h"

namespace colorscreen
{

class demosaic_paget;
class demosaic_dufay;
typedef std::function <bool (coord_t, coord_t, rgbdata)> analyzer;
typedef std::function <bool (coord_t, coord_t, rgbdata, rgbdata, rgbdata)> rgb_analyzer;

/* Rendering class that reconstructs colors from screen data using interpolation.  */
class render_interpolate : public render_to_scr
{
public:
  /* Construct render_interpolate with given PARAM, IMG, RPARAM and DST_MAXVAL.  */
  render_interpolate (const scr_to_img_parameters &param, const image_data &img, render_parameters &rparam, int dst_maxval);

  /* Precompute internal data structures for AREA. PROGRESS is used for progress reporting.  */
  nodiscard_attr bool precompute (int_image_area area, progress_info *progress);

  /* Sample pixel at screen coordinate P and return its color in final coordinates.  */
  pure_attr inline rgbdata sample_pixel_final (point_t p) const
  {
    point_t ps = m_scr_to_img.final_to_scr ({p.x - get_final_xshift (), p.y - get_final_yshift ()});
    return sample_pixel_scr (ps);
  }

  /* Set rendering type based on RTPARAM.  */
  void set_render_type (render_type_parameters rtparam);

  /* Enable precise RGB mode.  */
  void set_precise_rgb ()
  {
    m_precise_rgb = true;
  }

  /* Disable color adjustments.  */
  void set_unadjusted ()
  {
    m_unadjusted = true;
  }

  /* Enable predictive rendering.  */
  void set_predictive ()
  {
    m_unadjusted = true;
  }

  /* Enable luminosity adjustment.  */
  void set_adjust_luminosity ()
  {
    m_unadjusted = true;
  }

  /* Precompute whole image range.  */
  nodiscard_attr bool precompute_all (progress_info *progress)
  {
    return precompute (int_image_area (m_scr_to_img.get_range (m_img.width, m_img.height)), progress);
  }

  /* Precompute whole image range with additional options.  */
  nodiscard_attr bool precompute_all (bool grayscale_needed, bool normalized_patches, rgbdata patch_proportions, progress_info *progress)
  {
    abort ();
  }

  /* Precompute image range for given AREA.  */
  nodiscard_attr bool precompute_img_range (int_image_area area, progress_info *progress) 
  {
     return precompute (int_image_area (m_scr_to_img.get_range (image_area (area))), progress);
  }

  /* Sample pixel at screen coordinate P and return its color.  */
  pure_attr rgbdata sample_pixel_scr (point_t p) const;

  /* Sample pixel at image coordinate P and return its color.  */
  pure_attr inline rgbdata sample_pixel_img (point_t p) const
  {
    point_t ps = m_scr_to_img.to_scr (p);
    return sample_pixel_scr (ps);
  }

  /* Sample pixel at integer image coordinate P and return its color.  */
  pure_attr inline rgbdata fast_sample_pixel_img (int_point_t p) const
  {
    point_t ps = m_scr_to_img.to_scr ({p.x+(coord_t)0.5, p.y+(coord_t)0.5});
    return sample_pixel_scr (ps);
  }

  /* Set original color mode. If PROFILED is true, apply profiling.  */
  void original_color (bool profiled)
  {
    if (m_img.has_rgb ())
      {
	m_original_color = true;
	if (profiled)
	{
	  profile_matrix = m_params.get_profile_matrix (m_scr_to_img.patch_proportions (&m_params));
	  m_profiled = true;
	}
      }
  }

  /* Store downscaled image data starting at P of size WIDTH x HEIGHT to DATA.  */
  bool get_color_data (rgbdata *data, point_t p, int width,
                      int height, coord_t pixelsize, progress_info *progress);

  /* Analyze screen patches in given AREA. If SCREEN is true, AREA is in screen coordinates,
     otherwise it is in image coordinates. ANALYZE is called for every patch.  */
  bool analyze_patches (analyzer analyze, const char *task, bool screen, int_image_area area, progress_info *progress = NULL);

  /* Analyze RGB screen patches in given AREA. If SCREEN is true, AREA is in screen coordinates,
     otherwise it is in image coordinates. ANALYZE is called for every patch.  */
  bool analyze_rgb_patches (rgb_analyzer analyze, const char *task, bool screen, int_image_area area, progress_info *progress = NULL);

  /* Dump patch density to OUT.  */
  bool dump_patch_density (FILE *out);

private:
  /* Compensate saturation loss at screen coordinate P for color C.  */
  rgbdata compensate_saturation_loss_scr (point_t p, rgbdata c) const;

  /* Compensate saturation loss at image coordinate P for color C.  */
  rgbdata compensate_saturation_loss_img (point_t p, rgbdata c) const;

  std::shared_ptr<screen> m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;
  bool m_original_color;
  bool m_unadjusted;
  bool m_profiled;
  bool m_precise_rgb;

  std::shared_ptr<analyze_dufay> m_dufay;
  std::shared_ptr<analyze_paget> m_paget;
  std::shared_ptr<analyze_strips> m_strips;
  std::shared_ptr<demosaic_paget> m_demosaic_paget;
  std::shared_ptr<demosaic_dufay> m_demosaic_dufay;
  color_matrix m_saturation_matrix;
  color_matrix profile_matrix;
  // Proportions after permutations we do to handle screen variants.
  rgbdata m_interpolation_proportions;
};

/* Analyze screen patches for IMG using RPARAM and PARAM. If SCREEN is true,
   AREA is in screen coordinates, otherwise it is in image coordinates.
   ANALYZE is called for every patch.  */
bool analyze_patches (analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int_image_area area, progress_info *progress);

/* Analyze RGB screen patches for IMG using RPARAM and PARAM. If SCREEN is true,
   AREA is in screen coordinates, otherwise it is in image coordinates.
   ANALYZE is called for every patch.  */
bool analyze_rgb_patches (rgb_analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int_image_area area, progress_info *progress);

/* Increase LRU cache sizes for stitch projects by N.  */
void render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n);

}
#endif
