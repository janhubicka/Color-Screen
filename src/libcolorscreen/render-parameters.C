#include "include/render.h"
#include "include/stitch.h"
#include "icc.h"
#include "dufaycolor.h"
#include "wratten.h"
#include "include/spectrum-to-xyz.h"
const char * render_parameters::color_model_names [] = {
  "none",
  "scan",
  "red",
  "green",
  "blue",
  "Wall_max_separation",
  "paget",
  "Miethe_Goerz_reconstructed_by_Wagner",
  "Miethe_Goerz_mesured_by_Wagner",
  "Wratten_25_58_47_xyz",
  "Wratten_25_58_47_spectra",
  "dufaycolor_reseau_by_dufaycolor_manual",
  "dufaycolor_reseau_by_color_cinematography_xyY",
  "dufaycolor_reseau_by_color_cinematography_xyY_correctedY",
  "dufaycolor_reseau_by_color_cinematography_wavelength",
  "dufaycolor_reseau_by_color_cinematography_spectra",
  "dufaycolor_reseau_by_harrison_horner_spectra",
  "dufaycolor_reseau_by_color_cinematography_spectra_correction",
  "dufaycolor_reseau_by_harrison_horner_spectra_correction",
  "dufaycolor_NSMM_Bradford_11948",
  "dufaycolor_NSMM_Bradford_11951",
  "dufaycolor_NSMM_Bradford_11960",
  "dufaycolor_NSMM_Bradford_11967",
  "spicer_dufay_NSMM_Bradford_12075",
  "cinecolor_koshofer",
  "autochrome_Casella_Tsukada",
};
const char * render_parameters::dye_balance_names [] = {
  "none",
  "neutral",
  "whitepoint"
};
const char * render_parameters::output_profile_names [] = {
  "sRGB",
  "original"
};

/* Return matrix which contains the color of dyes either as rgb or xyz.
   If REALISTIC is true, use actual dye colors. Otheriwse re-scale them so white balance is
   corrected for interpolated rendering.  */
color_matrix
render_parameters::get_dyes_matrix (bool *is_srgb, bool *spectrum_based, image_data *img, bool normalized_dyes)
{
  spectrum_dyes_to_xyz *m_spectrum_dyes_to_xyz = NULL;
  color_matrix dyes;
  *spectrum_based = false;
  *is_srgb = false;
  switch (color_model)
    {
      /* No color adjustemnts: dyes are translated to sRGB.  */
      case render_parameters::color_model_none:
	*is_srgb = true;
	break;
      case render_parameters::color_model_scan:
	if (!img)
	  {
	    *is_srgb = true;
	    break;
	  }
	  dyes = matrix_by_dye_xyY (img->primary_red, img->primary_green, img->primary_blue);
	  if (backlight_correction)
	    {
	      xyz white = xyz::from_linear_srgb (1, 1, 1);
	      dyes.normalize_grayscale (white.x, white.y, white.z);
	    }
	break;
      case render_parameters::color_model_red:
	{
	  color_matrix m (1, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0, 0, 0, 1);
	  dyes = m;
	  *is_srgb = true;
	}
	break;
      case render_parameters::color_model_green:
	{
	  color_matrix m (0, 0.5,  0, 0,
			  0, 1,0, 0,
			  0, 0.5,0, 0,
			  0, 0, 0,1);
	  dyes = m;
	  *is_srgb = true;
	}
	break;
      case render_parameters::color_model_blue:
	{
	  color_matrix m (0, 0, 0.5,  0,
			  0, 0, 0.5,0,
			  0, 0, 1,0,
			  0, 0, 0, 1);
	  dyes = m;
	  *is_srgb = true;
	}
	break;
      /* Color based on frequencies determined in Wall's Practical Color Photography
         as triggering best individual stimulus of an eye.  */
      case render_parameters::color_model_max_separation:
	{
	  dyes = matrix_by_dye_xy (0.7319933,0.2680067,  /*670nm */
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  break;
	}
      /* Colors found to be working for Finlays and Pagets pretty well.  */
      case render_parameters::color_model_paget:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325, 
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  break;
	}
      /* Colors derived from reconstructed filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_reconstructed_wager:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325,
				   0.182, 0.747,
				   0.151, 0.041);
	  break;
	}
      case render_parameters::color_model_wratten_25_58_47_xyz:
	{
	  color_matrix m (wratten::filter_25_red.x, wratten::filter_58_green.x, wratten::filter_47_blue.x , 0,
			  wratten::filter_25_red.y, wratten::filter_58_green.y, wratten::filter_47_blue.y , 0,
			  wratten::filter_25_red.z, wratten::filter_58_green.z, wratten::filter_47_blue.z , 0,
			  0, 0, 0, 1);
	  dyes = m;
	  break;
	}
      case render_parameters::color_model_wratten_25_58_47_spectra:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::wratten_25_58_47_kodak_1945);
	}
      /* Colors derived from filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_original_wager:
	{
	  dyes = matrix_by_dye_xy (0.620, 0.315,
				   0.304, 0.541,
				   0.182, 0.135);
	  break;
	}
      case render_parameters::color_model_autochrome:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::cinecolor);
	  break;
	}
      case render_parameters::color_model_autochrome2:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::autochrome_reconstructed,
					    spectrum_dyes_to_xyz::autochrome_reconstructed_aged,
					    age);
	  break;
	}
      case render_parameters::color_model_dufay_manual:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_dufaycolor_manual);
	  break;
	}
      case render_parameters::color_model_dufay_color_cinematography_xyY:
	{
	  dyes = dufaycolor::color_cinematography_xyY_dye_matrix ();
	  dyes.normalize_xyz_brightness ();
	  xyz white (0, 0, 0);
	  dyes.apply_to_rgb (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion, &white.x, &white.y, &white.z);
	  //printf ("Normalized: %i white %f %f %f\n", normalized_dyes, white.x, white.y, white.z);
	  if (normalized_dyes)
	    dyes.scale_channels (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion);
	  dyes = bradford_whitepoint_adaptation_matrix (/*white*/ il_B_white, srgb_white) * dyes;
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_xyY_correctedY:
	{
	  dyes = dufaycolor::corrected_dye_matrix ();
	  dyes.normalize_xyz_brightness ();
	  xyz white (0, 0, 0);
	  dyes.apply_to_rgb (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion, &white.x, &white.y, &white.z);
	  //printf ("Normalized: %i white %f %f %f\n", normalized_dyes, white.x, white.y, white.z);
	  if (normalized_dyes)
	    dyes.scale_channels (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion);
	  dyes = bradford_whitepoint_adaptation_matrix (/*white*/ il_B_white, srgb_white) * dyes;
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_wavelength:
	{
	// https://www.luxalight.eu/en/cie-convertor
	  dyes = matrix_by_dye_xyY (xyY(0.6345861569, 0.3649735847, 0.177), /* dominating wavelength 601.7*/
				    xyY(0.2987423914, 0.6949214652, 0.43), /* dominating wavelength 549.6*/
				    xyY(0.133509341, 0.04269239, 0.087) /* dominating wavelength 466.0*/);
	  dyes.normalize_xyz_brightness ();
	  xyz white (0, 0, 0);
	  dyes.apply_to_rgb (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion, &white.x, &white.y, &white.z);
	  //printf ("Normalized: %i\n", normalized_dyes);
	  if (normalized_dyes)
	    dyes.scale_channels (dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion);
	  dyes = bradford_whitepoint_adaptation_matrix (white, srgb_white) * dyes;
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_spectra:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_spectra_correction:
	{
	  dyes = dufaycolor_correction_color_cinematography_matrix ();
	  break;
	}
      case render_parameters::color_model_dufay_harrison_horner_spectra:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);
	}
	break;
      case render_parameters::color_model_dufay_harrison_horner_spectra_correction:
	{
	  dyes = dufaycolor_correction_harrison_horner_matrix ();
	  break;
	}
      case render_parameters::color_model_dufay1:
      case render_parameters::color_model_dufay2:
      case render_parameters::color_model_dufay3:
      case render_parameters::color_model_dufay4:
      case render_parameters::color_model_dufay5:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography,
					    (spectrum_dyes_to_xyz::dyes)((int)color_model - (int)render_parameters::color_model_dufay1 + (int)spectrum_dyes_to_xyz::dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor), age);
	  break;
	}
      case render_parameters::color_model_max:
	abort ();
    }
  if (m_spectrum_dyes_to_xyz)
    {
      m_spectrum_dyes_to_xyz->set_daylight_backlight (backlight_temperature);
      switch (dye_balance)
	{
	  case render_parameters::dye_balance_none:
	    m_spectrum_dyes_to_xyz->normalize_brightness ();
	    break;
	  case render_parameters::dye_balance_neutral:
	    m_spectrum_dyes_to_xyz->normalize_dyes (6500);
	    break;
	  case render_parameters::dye_balance_whitepoint:
	    m_spectrum_dyes_to_xyz->normalize_xyz_to_backlight_whitepoint ();
	    break;
	  default:
	    abort ();
	}
      /* At the moment all conversion we do are linear conversions.  In that case
         we can build XYZ matrix and proceed with that.  */
      if (debug && !m_spectrum_dyes_to_xyz->is_linear ())
	{
#if 0
	  xyz_srgb_matrix m2;
	  color = m2 * color;
#endif
	  /* There is disabled code in render.h to optimize codegen.  */
	  abort ();
	}
      else
	{
	  *spectrum_based = true;
#if 0
	  if (presaturation != 1)
	    {
	      presaturation_matrix m (presaturation);
	      color = m * color;
	    }
	  color_matrix mm, m = m_spectrum_dyes_to_xyz->xyz_matrix ();
	  xyz_srgb_matrix m2;
	  mm = m2 * m;
	  color = mm * color;
	  delete (m_spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz = NULL;
#endif
	  dyes = m_spectrum_dyes_to_xyz->xyz_matrix ();
	}
    }
  return dyes;
}

size_t
render_parameters::get_icc_profile (void **buffer, image_data *img, bool normalized_dyes)
{
  bool is_rgb;
  bool spectrum_based;
  color_matrix dyes = get_dyes_matrix (&is_rgb, &spectrum_based, img, normalized_dyes);
  xyz r = {dyes.m_elements[0][0], dyes.m_elements[0][1], dyes.m_elements[0][2]};
  xyz g = {dyes.m_elements[1][0], dyes.m_elements[1][1], dyes.m_elements[1][2]};
  xyz b = {dyes.m_elements[2][0], dyes.m_elements[2][1], dyes.m_elements[2][2]};
  return create_profile (color_model_names[color_model], r, g, b, r+g+b, output_gamma, buffer);
}

void
render_parameters::set_tile_adjustments_dimensions (int w, int h)
{
  static tile_adjustment default_tile_adjustment;
  tile_adjustments.resize (0);
  tile_adjustments.resize (w * h);
  for (int x = 0; x < w * h; x++)
    {
      tile_adjustments[x] = default_tile_adjustment;
      assert (tile_adjustments[x].enabled);
    }
  tile_adjustments_width = w;
  tile_adjustments_height = h;
}

const render_parameters::tile_adjustment&
render_parameters::get_tile_adjustment (stitch_project *stitch, int x, int y) const
{
  static tile_adjustment default_tile_adjustment;
  assert (default_tile_adjustment.enabled);
  assert (x >= 0 && x < stitch->params.width && y >= 0 && y < stitch->params.height);
  if (tile_adjustments_width != stitch->params.width || tile_adjustments_height != stitch->params.height)
    return default_tile_adjustment;
  return tile_adjustments[y * stitch->params.width + x];
}

render_parameters::tile_adjustment&
render_parameters::get_tile_adjustment_ref (stitch_project *stitch, int x, int y)
{
  assert (x >= 0 && x < stitch->params.width && y >= 0 && y < stitch->params.height);
  if (tile_adjustments_width != stitch->params.width || tile_adjustments_height != stitch->params.height)
    set_tile_adjustments_dimensions (stitch->params.width, stitch->params.height);
  return tile_adjustments[y * stitch->params.width + x];
}
render_parameters::tile_adjustment&
render_parameters::get_tile_adjustment (int x, int y)
{
  assert (x >= 0 && x < tile_adjustments_width && y >= 0 && y < tile_adjustments_height);
  return tile_adjustments[y * tile_adjustments_width + x];
}
