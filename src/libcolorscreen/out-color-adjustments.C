#include <algorithm>
#include <assert.h>
#include "include/render-parameters.h"
#include "include/progress-info.h"
#include "lru-cache.h"
#include "out-color-adjustments.h"

namespace colorscreen
{
namespace
{
/* Parameters for output lookup table cache.  */
struct out_lookup_table_params
{
  int maxval;
  luminosity_t output_gamma;

  /* Return true if this structure is equal to O.  */
  bool
  operator== (const out_lookup_table_params &o) const
  {
    return maxval == o.maxval && output_gamma == o.output_gamma;
  }
};

/* Construct a new output lookup table for given parameters P.  The table
   translates linear values to output gamma.  */
std::unique_ptr<precomputed_function<luminosity_t>>
get_new_out_lookup_table (out_lookup_table_params &p, progress_info *)
{
  luminosity_t gamma = p.output_gamma;
  if (gamma != (luminosity_t)-1.0)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  int maxval = p.maxval;

  return std::make_unique<precomputed_function<luminosity_t>> (
      0, 1, out_color_adjustments::out_lookup_table_size,
      [=] (luminosity_t x) {
	return invert_gamma (x, gamma) * maxval + (luminosity_t)0.5;
      });
}

static lru_cache<out_lookup_table_params, precomputed_function<luminosity_t>,
		 get_new_out_lookup_table, 4>
    out_lookup_table_cache ("out lookup tables");
}

/* Precompute color transformation matrices and lookup tables for given
   RPARAM and IMG.  NORMALIZED_PATCHES and PATCH_PROPORTIONS control
   spectral data processing.  Report progress to PROGRESS.  Return false
   on failure.  */
bool
out_color_adjustments::precompute (render_parameters &m_params,
				   const image_data *m_img,
				   bool normalized_patches,
				   rgbdata patch_proportions,
				   progress_info *progress)
{
  m_output_gamma = m_params.output_gamma;
  m_gamut_warning = m_params.gamut_warning;

  out_lookup_table_params out_par = { m_dst_maxval, m_params.output_gamma };
  m_out_lookup_table = out_lookup_table_cache.get (out_par, progress);
  if (!m_out_lookup_table && progress && progress->cancel_requested ())
    return false;

  color_matrix color;

  if (m_params.output_profile != render_parameters::output_profile_original)
    {
      /* See if we want to do some output adjustments in pro photo RGB space.
	 These should closely follow what DNG reference recommends.  */
      bool do_pro_photo
	  = m_params.output_tone_curve != tone_curve::tone_curve_linear;

      /* Matrix converting dyes to XYZ.  */
      color = m_params.get_rgb_to_xyz_matrix (
	  m_img, normalized_patches, patch_proportions,
	  do_pro_photo ? d50_white : d65_white);

      /* For subtractive processes we do post-processing in separate matrix
	 after spectrum dyes to xyz are applied.  */
      if (m_params.color_model == render_parameters::color_model_kodachrome25)
	{
	  m_spectrum_dyes_to_xyz = std::make_unique<spectrum_dyes_to_xyz> ();
	  m_spectrum_dyes_to_xyz->set_film_response (
	      spectrum_dyes_to_xyz::response_even);
	  m_spectrum_dyes_to_xyz->set_dyes (
	      spectrum_dyes_to_xyz::kodachrome_25_sensitivity);
	  m_spectrum_dyes_to_xyz->set_backlight (
	      spectrum_dyes_to_xyz::il_D, m_params.backlight_temperature);

	  spectrum_dyes_to_xyz film;
	  film.set_film_response (
	      spectrum_dyes_to_xyz::dufaycolor_harrison_horner_emulsion_cut);
	  film.set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);
	  film.set_backlight (spectrum_dyes_to_xyz::il_D,
			      m_params.backlight_temperature);

	  color = color
		  * m_spectrum_dyes_to_xyz->process_transformation_matrix (
		      &film);
	  m_spectrum_dyes_to_xyz->set_characteristic_curve (
	      spectrum_dyes_to_xyz::kodachrome25_curve);

	  saturation_matrix m (m_params.saturation);
	  m_color_matrix2 = (bradford_whitepoint_adaptation_matrix (
				 m_spectrum_dyes_to_xyz->whitepoint_xyz (),
				 do_pro_photo ? d50_white : d65_white)
			     * m)
			    * (luminosity_t)1.5;
	  if (m_params.output_profile == render_parameters::output_profile_xyz)
	    ;
	  else if (do_pro_photo)
	    {
	      xyz_pro_photo_rgb_matrix m_matrix;
	      m_color_matrix2 = m_matrix * m_color_matrix2;
	      m_tone_curve = std::make_unique<tone_curve> (
		  m_params.output_tone_curve,
		  m_params.output_tone_curve_control_points);
	      assert (!m_tone_curve->is_linear ());
	    }
	  else
	    {
	      xyz_srgb_matrix m_matrix;
	      m_color_matrix2 = m_matrix * m_color_matrix2;
	    }
	}
      else
	{
	  if (m_params.output_profile == render_parameters::output_profile_xyz)
	    ;
	  else if (do_pro_photo)
	    {
	      xyz_pro_photo_rgb_matrix m_matrix;
	      color = m_matrix * color;
	      m_tone_curve = std::make_unique<tone_curve> (
		  m_params.output_tone_curve,
		  m_params.output_tone_curve_control_points);
	      assert (!m_tone_curve->is_linear ());
	    }
	  else
	    {
	      xyz_srgb_matrix m_matrix;
	      color = m_matrix * color;
	    }
	}
    }
  else
    color = m_params.get_rgb_adjustment_matrix (normalized_patches,
						patch_proportions);
  m_color_matrix = color;
  return true;
}
}
