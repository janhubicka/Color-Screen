#include "include/render.h"
#include "icc.h"
const char * render_parameters::color_model_names [] = {
  "none",
  "red",
  "green",
  "blue",
  "Wall_max_separation",
  "paget",
  "Miethe_Goerz_reconstructed_by_Wagner",
  "Miethe_Goerz_mesured_by_Wagner",
  "dufaycolor_reseau_by_dufaycolor_manual",
  "dufaycolor_reseau_by_color_cinematography_xyY",
  "dufaycolor_reseau_by_color_cinematography_spectra",
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

/* Return matrix which contains the color of dyes either as rgb or xyz.  */
color_matrix
render_parameters::get_dyes_matrix (bool *is_srgb, bool *spectrum_based)
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
	  m_spectrum_dyes_to_xyz->set_dyes_to_autochrome ();
	  break;
	}
      case render_parameters::color_model_autochrome2:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_autochrome2 (1, 1, 19.7 / (20.35),
							   1, 21 / (20.35),
							   1,1,age);
	  break;
	}
      case render_parameters::color_model_dufay_manual:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_dufay_manual ();
	  break;
	}
      case render_parameters::color_model_dufay_color_cinematography_xyY:
	{
#if 0
	  dyes = matrix_by_dye_xy (0.633, 0.365, /*Y 17.7% dominating wavelength 601.7*/
				   0.233, 0.647, /*Y 43% dominating wavelength 549.6*/
				   0.140, 0.089 /*Y 3.7% dominating wavelength 466.0*/);
#else
	  dyes = matrix_by_dye_xyY (0.633, 0.365, 0.177, /* dominating wavelength 601.7*/
				    0.233, 0.647, 0.43, /* dominating wavelength 549.6*/
				    0.140, 0.089, 0.037 /* dominating wavelength 466.0*/);
#endif
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_spectra:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_dufay_color_cinematography ();
	}
	break;
      case render_parameters::color_model_dufay1:
      case render_parameters::color_model_dufay2:
      case render_parameters::color_model_dufay3:
      case render_parameters::color_model_dufay4:
      case render_parameters::color_model_dufay5:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_dufay ((int)color_model - (int)render_parameters::color_model_dufay1, age);
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
render_parameters::get_icc_profile (void **buffer)
{
  bool is_rgb;
  bool spectrum_based;
  color_matrix dyes = get_dyes_matrix (&is_rgb, &spectrum_based);
  dyes.print (stdout);
  xyz r = {dyes.m_elements[0][0], dyes.m_elements[0][1], dyes.m_elements[0][2]};
  xyz g = {dyes.m_elements[1][0], dyes.m_elements[1][1], dyes.m_elements[1][2]};
  xyz b = {dyes.m_elements[2][0], dyes.m_elements[2][1], dyes.m_elements[2][2]};
  return create_profile (color_model_names[color_model], r, g, b, output_gamma, buffer);
}
