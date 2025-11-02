#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <memory>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/spectrum-to-xyz.h"
#include "include/dufaycolor.h"
#include "include/wratten.h"
#include "include/stitch.h"
#include "render.h"
#include "icc.h"
#include "render-interpolate.h"
namespace colorscreen
{
const char * render_parameters::color_model_names [] = {
  "none",
  "scan",
  "sRGB",
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
  "dufaycolor_reseau_by_color_cinematography_spectra_correction",
  "dufaycolor_reseau_by_harrison_horner_spectra",
  "dufaycolor_reseau_by_harrison_horner_spectra_correction",
  "dufaycolor_reseau_by_collins_giles_spectra",
  "dufaycolor_reseau_by_collins_giles_spectra_correction",
  "dufaycolor_reseau_by_photography_its_materials_and_processes_spectra",
  "dufaycolor_reseau_by_photography_its_materials_and_processes_spectra_correction",
  "dufaycolor_NSMM_Bradford_11948",
  "dufaycolor_NSMM_Bradford_11951",
  "dufaycolor_NSMM_Bradford_11960",
  "dufaycolor_NSMM_Bradford_11967",
  "spicer_dufay_NSMM_Bradford_12075",
  "cinecolor_koshofer",
  "autochrome_Casella_Tsukada",
  "kodachrome25",
  "thames_Mees_Pledge",
  "dioptichrome_Mees_Pledge",
  "autochrome_Mees_Pledge",
};
const char * render_parameters::dye_balance_names [] = {
  "none",
  "brightness",
  "bradford",
  "neutral",
  "whitepoint"
};
const char * render_parameters::output_profile_names [] = {
  "sRGB",
  "XYZ",
  "original"
};


/* patch_portions describes how much percent of screen is occupied by red, green and blue
   patches respectively. It should have sum at most 1.  */
rgbdata patch_proportions (enum scr_type t, const render_parameters *rparam)
{
  switch (t)
    {
    case Paget:
    case Thames:
    case Finlay:
      /* TODO: Measure actual portions.  */
      return {1/3.0,1/3.0,1/3.0};
    case WarnerPowrie:
    case Joly:
      /* TODO: Measure actual portions.  */
      {
	coord_t red_strip_width = rparam && rparam->red_strip_width ? rparam->red_strip_width : 1/3.0;
	coord_t green_strip_width = rparam && rparam->green_strip_width ? rparam->green_strip_width : 1/3.0;
        return {red_strip_width, green_strip_width, 1 - red_strip_width - green_strip_width};
      }

    /* Red strips.  */
    case Dufay:
      {
	coord_t red_strip_width = rparam && rparam->red_strip_width ? rparam->red_strip_width : dufaycolor::red_strip_width;
	coord_t green_strip_width = rparam && rparam->green_strip_width ? rparam->green_strip_width : dufaycolor::green_strip_width;
        return {red_strip_width, green_strip_width * (1 - red_strip_width), (1 - green_strip_width) *  (1 - red_strip_width)};
      }
    /* Green strips.  */
    case DioptichromeB:
      {
	/* On scan of separate DiopticrhomeB screen (possibly early test sample) we get approx
	   red_strip_width: 0.489965
	   green_strip_width: 0.499510
	   So it seems that both strips were 50%.  */

	coord_t red_strip_width = rparam && rparam->red_strip_width ? rparam->red_strip_width : 0.5;
	coord_t green_strip_width = rparam && rparam->green_strip_width ? rparam->green_strip_width : 0.5;
        return {(1 - green_strip_width) * red_strip_width, green_strip_width, (1 - green_strip_width) *  (1 - red_strip_width)};
      }
    /* Blue strips.  */
    case ImprovedDioptichromeB:
      {
	coord_t red_strip_width = rparam && rparam->red_strip_width ? rparam->red_strip_width : 0.66;
	coord_t green_strip_width = rparam && rparam->green_strip_width ? rparam->green_strip_width : 0.5;
        return {red_strip_width * (1 - green_strip_width), green_strip_width * red_strip_width, 1 - red_strip_width};
      }
    /* Blue strips.  */
    case Omnicolore:
      {
	coord_t red_strip_width = rparam && rparam->red_strip_width ? rparam->red_strip_width : 0.69;
	coord_t green_strip_width = rparam && rparam->green_strip_width ? rparam->green_strip_width : 0.55;
        return {red_strip_width * (1 - green_strip_width), green_strip_width * red_strip_width, 1 - red_strip_width};
      }
    default:
      abort ();
    }
}

static void
print_mid_white (color_matrix m)
{
  xyz c;
  m.apply_to_rgb (0.5, 0.5, 0.5, &c.x, &c.y, &c.z);
  printf ("Mid neutral:");
  c.print (stdout);
}

/* Return true if dye balance applies to the model.  */

static bool
apply_balance_to_model (render_parameters::color_model_t color_model)
{
  return (color_model != render_parameters::color_model_none
	  && color_model != render_parameters::color_model_scan
	  && color_model != render_parameters::color_model_red
	  && color_model != render_parameters::color_model_green
	  && color_model != render_parameters::color_model_blue);
}

/* Return matrix that translate RGB values in the color process space into RGB or XYZ.
   If SPECTRUM_BASED is true, the the matrix is based on actual spectra of dyes and
   thus backlight_temeperature parameter is handled correctly.

   If OPTIMIZED is true, then the matrix is optimized camera matrix construted from
   process simulation and both temperature and backlight_temperature parameters are handled correctly.  */
color_matrix
render_parameters::get_dyes_matrix (bool *spectrum_based, bool *optimized, const image_data *img)
{
  std::unique_ptr <spectrum_dyes_to_xyz> spect = NULL;
  color_matrix dyes;
  bool is_srgb = false;
  xyz dye_whitepoint = srgb_white;
  *spectrum_based = false;
  *optimized = false;
  switch (color_model)
    {
      /* No color adjustemnts: dyes are translated to sRGB.  */
      case render_parameters::color_model_none:
	is_srgb = true;
	break;
      case render_parameters::color_model_scan:
	{
	  if (!img)
	    {
	      is_srgb = true;
	      break;
	    }
	  xyz zero = {0,0,0};
	  dyes = matrix_by_dye_xyY (scanner_red == zero ? img->primary_red : (xyY)scanner_red, 
				    scanner_green == zero ? img->primary_green : (xyY)scanner_green,
				    scanner_blue == zero ? img->primary_blue : (xyY)scanner_blue);
	  if (scanner_red != zero)
	    dye_whitepoint = d50_white;
	  else if (backlight_correction)
	    dyes.normalize_grayscale (dye_whitepoint.x, dye_whitepoint.y, dye_whitepoint.z);
	  else
	    dye_whitepoint = {1,1,1};
	}
	break;
#if 0
      case render_parameters::color_model_optimized:
	{
	  color_matrix m (optimized_red.x, optimized_green.x, optimized_blue.x, optimized_dark.x,
			  optimized_red.y, optimized_green.y, optimized_blue.y, optimized_dark.y,
			  optimized_red.z, optimized_green.z, optimized_blue.z, optimized_dark.z,
			  0              , 0                , 0               , 1);
	  dye_whitepoint = d50_white;
	  dyes = m;
	  /* Do not apply any of the backlight temperature adjustments.  */
	  assert (observer_whitepoint == d50_white);
	  return dyes;
	}
	break;
#endif
      case render_parameters::color_model_red:
	{
	  color_matrix m (1,  0, 0, 0,
			  0.5,0, 0, 0,
			  0.5,0, 0, 0,
			  0,  0, 0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_green:
	{
	  color_matrix m (0, 0.5,0, 0,
			  0, 1,  0, 0,
			  0, 0.5,0, 0,
			  0, 0,  0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_blue:
	{
	  color_matrix m (0, 0, 0.5,0,
			  0, 0, 0.5,0,
			  0, 0, 1,  0,
			  0, 0, 0,  1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_srgb:
	{
	  dyes = matrix_by_dye_xyY (xyY(0.6400, 0.3300, 0.2126),
				    xyY(0.3000, 0.6000, 0.7152),
				    xyY(0.1500, 0.0600, 0.0722 ));
	  dye_whitepoint = srgb_white;
	  break;
	}
      /* Color based on frequencies determined in Wall's Practical Color Photography
         as triggering best individual stimulus of an eye.  */
      case render_parameters::color_model_max_separation:
	{
	  dyes = matrix_by_dye_xy (0.7319933,0.2680067,  /*670nm */
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  dye_whitepoint = srgb_white;
	  break;
	}
      /* Colors found to be working for Finlays and Pagets pretty well.  */
      case render_parameters::color_model_paget:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325, 
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  dye_whitepoint = srgb_white;
	  break;
	}
      /* Colors derived from reconstructed filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_reconstructed_wager:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325,
				   0.182, 0.747,
				   0.151, 0.041);
	  dye_whitepoint = srgb_white;
	  break;
	}
      case render_parameters::color_model_wratten_25_58_47_xyz:
	{
	  color_matrix m (wratten::filter_25_red.x, wratten::filter_58_green.x, wratten::filter_47_blue.x , 0,
			  wratten::filter_25_red.y, wratten::filter_58_green.y, wratten::filter_47_blue.y , 0,
			  wratten::filter_25_red.z, wratten::filter_58_green.z, wratten::filter_47_blue.z , 0,
			  0, 0, 0, 1);
	  dyes = m;
	  dye_whitepoint = il_C_white;
	  break;
	}
      case render_parameters::color_model_wratten_25_58_47_spectra:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::wratten_25_58_47_kodak_1945);
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
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::cinecolor);
	  break;
	}
      case render_parameters::color_model_autochrome2:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::autochrome_reconstructed,
					    spectrum_dyes_to_xyz::autochrome_reconstructed_aged,
					    age);
	  break;
	}
      case render_parameters::color_model_dufay_manual:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_dufaycolor_manual);
	  break;
	}
      case render_parameters::color_model_dufay_color_cinematography_xyY:
	{
	  dyes = dufaycolor::color_cinematography_xyY_dye_matrix ();
	  dye_whitepoint = il_C_white;
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_xyY_correctedY:
	{
	  dyes = dufaycolor::corrected_dye_matrix ();
	  dye_whitepoint = il_C_white;
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_wavelength:
	{
	  dyes = matrix_by_dye_xyY (xyY(0.6345861569, 0.3649735847, 0.177), /* dominating wavelength 601.7*/
				    xyY(0.2987423914, 0.6949214652, 0.43), /* dominating wavelength 549.6*/
				    xyY(0.133509341, 0.04269239, 0.087) /* dominating wavelength 466.0*/);
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_spectra:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
	}
	break;
      case render_parameters::color_model_dufay_color_cinematography_spectra_correction:
	{
	  dyes = dufaycolor_correction_color_cinematography_matrix (temperature, backlight_temperature);
	  *optimized = true;
	  break;
	}
      case render_parameters::color_model_dufay_harrison_horner_spectra:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);
	}
	break;
      case render_parameters::color_model_dufay_harrison_horner_spectra_correction:
	{
	  dyes = dufaycolor_correction_harrison_horner_matrix (temperature, backlight_temperature);
	  *optimized = true;
	  break;
	}
      case render_parameters::color_model_dufay_photography_its_materials_and_processes_spectra:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_photography_its_materials_and_processes);
	}
	break;
      case render_parameters::color_model_dufay_photography_its_materials_and_processes_spectra_correction:
	{
	  dyes = dufaycolor_correction_photography_its_materials_and_processes_matrix (temperature, backlight_temperature);
	  *optimized = true;
	  break;
	}
      case render_parameters::color_model_dufay_collins_giles_spectra:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_collins_giles);
	}
	break;
      case render_parameters::color_model_dufay_collins_giles_spectra_correction:
	{
	  dyes = dufaycolor_correction_photography_its_materials_and_processes_matrix (temperature, backlight_temperature);
	  break;
	}
      case render_parameters::color_model_dufay1:
      case render_parameters::color_model_dufay2:
      case render_parameters::color_model_dufay3:
      case render_parameters::color_model_dufay4:
      case render_parameters::color_model_dufay5:
	{
	  spect = std::make_unique <spectrum_dyes_to_xyz> ();
	  spect->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography,
					    (spectrum_dyes_to_xyz::dyes)((int)color_model - (int)render_parameters::color_model_dufay1 + (int)spectrum_dyes_to_xyz::dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor), age);
	  break;
	}
      /* Kodachrome is subtractive, it needs to be computed by spectrum_dyes_to_xyz.  */
      case render_parameters::color_model_kodachrome25:
	{
	  color_matrix id;
	  return id;
	}
      case render_parameters::color_model_thames_mees_pledge:
	spect = std::make_unique <spectrum_dyes_to_xyz> ();
	spect->set_dyes (spectrum_dyes_to_xyz::thames_mees_pledge);
	break;
      case render_parameters::color_model_dioptichrome_mees_pledge:
	spect = std::make_unique <spectrum_dyes_to_xyz> ();
	spect->set_dyes (spectrum_dyes_to_xyz::dioptichrome_mees_pledge);
	break;
      case render_parameters::color_model_autochrome_mees_pledge:
	spect = std::make_unique <spectrum_dyes_to_xyz> ();
	spect->set_dyes (spectrum_dyes_to_xyz::autochrome_mees_pledge);
	break;
      case render_parameters::color_model_max:
	abort ();
    }
  if (is_srgb)
    {
      srgb_xyz_matrix m;
      dyes = m * dyes;
    }
  if (spect)
    {
      spect->set_backlight (spectrum_dyes_to_xyz::il_D, backlight_temperature);
      /* At the moment all conversion we do are linear conversions.  In that case
         we can build XYZ matrix and proceed with that.  */
      if (debug && !spect->is_linear ())
	abort ();
      else
	{
	  *spectrum_based = true;
	  dyes = spect->xyz_matrix ();
	}
    }
  /* dye_whitepoint is the whitepoint xyz values of dyes was measured for.
     To turn them to a different whitepoint, we use bradford adaptation.  */
  else if (!*optimized)
    {
      spectrum_dyes_to_xyz s;
      s.set_backlight (spectrum_dyes_to_xyz::il_D, backlight_temperature);
      xyz backlight_white = s.whitepoint_xyz ();
	
      //printf (" Dye :");
      //dye_whitepoint.print (stdout);
      //print_mid_white (dyes);
      //bradford_whitepoint_adaptation_matrix (d50_white, d65_white).print (stdout);
      //printf ("2");
      //bradford_d50_to_d65_matrix m;
      //m.print (stdout);
      //dyes.print (stdout);
      dyes = bradford_whitepoint_adaptation_matrix (dye_whitepoint, backlight_white) * dyes;
      //printf (" Backlight :");
      //backlight_white.print (stdout);
      //print_mid_white (dyes);
      //dyes.print (stdout);
    }
  return dyes;
}

/* Return matrix which contains the color of dyes either as rgb or xyz.
   PATCH_PORTIONS describes how much percent of screen is occupied by red, green and blue
   patches respectively. It should have sum at most 1.
     
   If NORMALIZED_PATCHES is true, the rgbdata represents patch intensities regardless of their
   size (as in interpolated rendering) and the dye matrix channels needs to be scaled by
   patch_portions.
   
   TARGET_WHITEPOINT is a whitepoint of the target color space,
   default is D50 for XYZ_D50.  */

color_matrix
render_parameters::get_balanced_dyes_matrix (const image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint)
{
  bool optimized;
  bool spectrum_based;
  bool correct_whitepoints = true;
  color_matrix dyes = get_dyes_matrix (&spectrum_based, &optimized, img);

  /* If dyes are normalised, we need to scale primaries to match their proportions in actual sreen.  */
  if (normalized_patches)
    dyes.scale_channels (patch_proportions.red, patch_proportions.green, patch_proportions.blue);
  dyes.verify_last_row_0001 ();

  if (apply_balance_to_model (color_model))
    {
      /* Determine whitepoint of the screen.  For normalized patches it is {1, 1, 1} since color screens
	 should be neutral.  */
      rgbdata screen_whitepoint = {1, 1, 1};
      if (!normalized_patches)
	screen_whitepoint = patch_proportions;

      /* Determine actual whitepoint of the screen.  */
      xyz dye_whitepoint;
      dyes.apply_to_rgb (screen_whitepoint.red, screen_whitepoint.green, screen_whitepoint.blue, &dye_whitepoint.x, &dye_whitepoint.y, &dye_whitepoint.z);

      /* Different dye balances.  */
      switch (dye_balance)
	{
	  case render_parameters::dye_balance_none:
	    break;

	  /* Bradford correct the white of color screen into target whitepoint.  */
	  case render_parameters::dye_balance_bradford:
	    dyes = bradford_whitepoint_adaptation_matrix (dye_whitepoint, target_whitepoint) * dyes;
	    /* Save one step and go directly to target white.  */
	    correct_whitepoints = false;
	    break;

	  /* Scale so y of screen white is 1.  */
	  case render_parameters::dye_balance_brightness:
	    if (dye_whitepoint.y > 0)
	      {
	        dyes = dyes * (1/dye_whitepoint.y);
		dyes.m_elements[3][3] = 1;
	      }
	    break;

	  /* Scale intensity of dyes so they produce given whitepoint.  This correspond to adjusting
	     sizes of color patches in the emulsion.  */
	  case render_parameters::dye_balance_neutral:
	    {
	      xyz white = observer_whitepoint;
	      rgbdata scales;
	      dyes.invert ().apply_to_rgb (white.x, white.y, white.z, &scales.red, &scales.green, &scales.blue);
	      scales /= screen_whitepoint;
	      dyes.apply_to_rgb (scales.red,scales.green,scales.blue, &white.x, &white.y, &white.z);
	      dyes.scale_channels (scales.red, scales.green, scales.blue);
	    }
	    break;

	  /* Scale final XYZ values to obtain wihtepoint.  This is probably always
	     worse than Bradford correction.  */
	  case render_parameters::dye_balance_whitepoint:
	    for (int i = 0; i < 4; i++)
	      {
		xyz white = observer_whitepoint;
		dyes.m_elements[i][0] *= white.x / dye_whitepoint.x;
		dyes.m_elements[i][1] *= white.y / dye_whitepoint.y;
		dyes.m_elements[i][2] *= white.z / dye_whitepoint.z;
	      }
	    break;
	  default:
	    abort ();
	}
    }
  
  dyes.verify_last_row_0001 ();
  /* After balancing to observer whitepoint bradford correct to target whitepoint.  */
  //printf (" Observer:");
  //((xyz)observer_whitepoint).print (stdout);
  //print_mid_white (dyes);
  if (correct_whitepoints && (xyz)observer_whitepoint != target_whitepoint) 
    {
      dyes = bradford_whitepoint_adaptation_matrix ((xyz)observer_whitepoint, target_whitepoint) * dyes;
      dyes.verify_last_row_0001 ();
    }
  /* Don't do this as part of early adjustments, it confuses dark point of optimized matrices.  */
  color_matrix br (brightness, 0, 0, 0,
		   0, brightness, 0, 0,
		   0, 0, brightness, 0,
		   0, 0, 0, 1);
  dyes = br * dyes;
  //printf (" Target:");
  //target_whitepoint.print (stdout);
  //print_mid_white (dyes);
  return dyes;
}

/* Return matrix adjusting RGB values read from the scan.  */
color_matrix
render_parameters::get_rgb_adjustment_matrix (bool normalized_patches, rgbdata patch_proportions)
{
  color_matrix color (white_balance.red , 0, 0, 0,
		      0, white_balance.green, 0, 0,
		      0, 0, white_balance.blue , 0,
		      0, 0, 0, 1);
  if (presaturation != 1)
    {
      presaturation_matrix m (presaturation);
      color = m * color;
    }
  color.verify_last_row_0001 ();
  return color;
}

/* PATCH_PORTIONS describes how much percent of screen is occupied by red, green and blue
   patches respectively. It should have sum at most 1.
   
   If NORMALIZED_PATCHES is true, the rgbdata represents patch intensities regardless of their
   size (as in interpolated rendering) and the dye matrix channels needs to be scaled by
   PATCH_PROPORTIONS.  */
color_matrix
render_parameters::get_rgb_to_xyz_matrix (const image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint)
{
  color_matrix color = get_rgb_adjustment_matrix (normalized_patches, patch_proportions);
  color = get_balanced_dyes_matrix (img, normalized_patches, patch_proportions, target_whitepoint) * color;
  //color = color * brightness;
  //
  if (saturation != 1 && color_model != color_model_kodachrome25)
    {
      xyz_srgb_matrix xr;
      srgb_xyz_matrix rx;
      saturation_matrix m (saturation);
      color = xr * color;
      color = m * color;
      color = rx * color;
    }
  color.verify_last_row_0001 ();
  return color;
}

size_t
render_parameters::get_icc_profile (void **buffer, image_data *img, bool normalized_patches)
{
  // TODO: Handle patch proportions right
  color_matrix dyes = get_rgb_to_xyz_matrix (img, normalized_patches, {1/3.0,1/3.0,1/3.0});
  xyz r = {dyes.m_elements[0][0], dyes.m_elements[0][1], dyes.m_elements[0][2]};
  xyz g = {dyes.m_elements[1][0], dyes.m_elements[1][1], dyes.m_elements[1][2]};
  xyz b = {dyes.m_elements[2][0], dyes.m_elements[2][1], dyes.m_elements[2][2]};
  return create_profile (color_model_names[color_model], r, g, b, observer_whitepoint, output_gamma, buffer);
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

/* Set reasonable color model for screen of TYPE.  */
bool
render_parameters::auto_color_model (enum scr_type type)
{
  if (type == Dufay)
    color_model = color_model_dufay_color_cinematography_spectra;
  else if (type == Paget || type == Finlay)
    color_model = color_model_miethe_goerz_reconstructed_wager;
  else
    return false;
  return true;
}
static rgbdata
get_color (image_data &img, render_parameters &rparam, scr_to_img_parameters &param, rgbdata c, progress_info *progress)
{
  render r(img, rparam, 256);
  r.precompute_all (false, true, patch_proportions (param.type, &rparam), progress);
  c.red = r.adjust_luminosity_ir (c.red);
  c.green = r.adjust_luminosity_ir (c.green);
  c.blue = r.adjust_luminosity_ir (c.blue);
  r.set_hdr_color (c.red, c.green, c.blue, &c.red, &c.green, &c.blue);
  return c;
}
static luminosity_t
get_max_color (image_data &img, render_parameters &rparam, scr_to_img_parameters &param, rgbdata color, progress_info *progress)
{
  rgbdata c = get_color (img, rparam, param, color, progress);
  return std::max (c.red, std::max (c.green, c.blue));
}

/* Determine black and brightness by analysing tile of a scan.  */
bool
render_parameters::auto_dark_brightness (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress, luminosity_t dark_cut, luminosity_t light_cut)
{
  render_parameters rparam = *this;
  rparam.precise = true;
  {
    /* Produce histogram.  */
    rgb_histogram hist;
    if (!analyze_patches ([&] (coord_t x, coord_t y, rgbdata c)
			  {
			    hist.pre_account (c);
			    return true;
			  },
			  "determining value ranges",
			  img, rparam, param, false,
			  xmin, xmax, ymin, ymax, progress))
      return false;
    hist.finalize_range (65536*256);
    if (!analyze_patches ([&] (coord_t x, coord_t y, rgbdata c)
			  {
			    hist.account (c);
			    return true;
			  },
			  "producing histograms",
			  img, rparam, param, false,
			  xmin, xmax, ymin, ymax, progress))
      return false;
    hist.finalize ();

    /* Give up if the number of samples is too small.  */
    if (hist.num_samples () < 2 || (progress && progress->cancel_requested ()))
      return false;
    rgbdata minvals = hist.find_min (dark_cut);
    rgbdata maxvals = hist.find_max (light_cut);
    dark_point = std::min (std::min (minvals.red, minvals.green), minvals.blue);
    brightness = 1 / ((std::max (std::max (maxvals.red, maxvals.green), maxvals.blue) - dark_point) * rparam.scan_exposure);
    //printf ("Initial color %f\n",get_max_color (img, *this, param, maxvals, progress));
    /* Finetune brightness so color is white after whitepoint adjustments.  */
    int n = 0;
    while (get_max_color (img, *this, param, maxvals, progress) < 1 - 1.0 / 256 && n < 65535)
    {
      brightness *= 1 + 1.0 / 65536;
      n++;
    }
    while (get_max_color (img, *this, param, maxvals, progress) > 1 - 1.0 / 256 && n < 65535)
    {
    //printf ("Brightness2 %i Color %f\n", brightness,get_max_color (img, *this, param, maxvals, progress));
      brightness *= 1 - 1.0 / 65536;
      n++;
    }
    //printf ("Color %f attepts %i\n",get_max_color (img, *this, param, maxvals, progress), n);
  }
#if 0
  {
    std::vector<rgbdata> reds;
    std::vector<rgbdata> greens;
    std::vector<rgbdata> blues;
    FILE *rf = fopen ("/tmp/red.txt", "wt");
    FILE *gf = fopen ("/tmp/green.txt", "wt");
    FILE *bf = fopen ("/tmp/blue.txt", "wt");
    FILE *nrf = fopen ("/tmp/norm_red.txt", "wt");
    FILE *ngf = fopen ("/tmp/norm_green.txt", "wt");
    FILE *nbf = fopen ("/tmp/norm_blue.txt", "wt");
    render_interpolate render (param, img, rparam, 256);
    render.set_precise_rgb ();
    render.precompute_img_range (xmin, ymin, xmax, ymax, progress);
    render.analyze_rgb_tiles ([&] (coord_t x, coord_t y, rgbdata r, rgbdata g, rgbdata b)
			      {
				reds.push_back (r);
				greens.push_back (g);
				blues.push_back (b);
				fprintf (rf, "%f %f %f\n", r.red, r.green, r.blue);
				fprintf (gf, "%f %f %f\n", g.red, g.green, g.blue);
				fprintf (bf, "%f %f %f\n", b.red, b.green, b.blue);
				luminosity_t rlum = r.red + r.green + r.blue;
				luminosity_t glum = g.red + g.green + g.blue;
				luminosity_t blum = b.red + b.green + b.blue;
				YPbPr rr(r);
				fprintf (nrf, "%f %f %f\n", rr.Y, rr.Pb/rr.Y, rr.Pr/rr.Y);
				YPbPr gg(g);
				fprintf (ngf, "%f %f %f\n", gg.Y, gg.Pb/gg.Y, gg.Pr/gg.Y);
				YPbPr bb(b);
				fprintf (nbf, "%f %f %f\n", bb.Y, bb.Pb/bb.Y, bb.Pr/bb.Y);
				return true;
			      },
			      "collecting tile colors",
			      xmin, xmax, ymin, ymax, progress);
    fclose (rf);
    fclose (gf);
    fclose (bf);
    fclose (nrf);
    fclose (ngf);
    fclose (nbf);
    scr_detect_parameters dparam;
    optimize_screen_colors (&dparam, reds.data (), reds.size (), greens.data (), greens.size (), blues.data (), blues.size (), progress, stdout);
    render_scr_detect ddrender (dparam, img, rparam, 256);
    FILE *arf = fopen ("/tmp/adj_red.txt", "wt");
    FILE *agf = fopen ("/tmp/adj_green.txt", "wt");
    FILE *abf = fopen ("/tmp/adj_blue.txt", "wt");
    for (rgbdata r : reds)
      {
	r = ddrender.adjust_linearized_color (r);
	YPbPr rr(r);
	fprintf (arf, "%f %f %f\n", rr.Y, rr.Pb/rr.Y, rr.Pr/rr.Y);
      }
    for (rgbdata r : greens)
      {
	r = ddrender.adjust_linearized_color (r);
	YPbPr rr(r);
	fprintf (agf, "%f %f %f\n", rr.Y, rr.Pb/rr.Y, rr.Pr/rr.Y);
      }
    for (rgbdata r : blues)
      {
	r = ddrender.adjust_linearized_color (r);
	YPbPr rr(r);
	fprintf (abf, "%f %f %f\n", rr.Y, rr.Pb/rr.Y, rr.Pr/rr.Y);
      }
    fclose (arf);
    fclose (agf);
    fclose (abf);
  }
#endif

  return true;
}

bool 
render_parameters::auto_mix_weights (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress)
{
  render_parameters rparam = *this;
  rparam.precise = true;
  rgb_histogram hist_red, hist_green, hist_blue;
  if (!analyze_rgb_patches ([&] (coord_t x, coord_t y, rgbdata r, rgbdata g, rgbdata b)
			    {
			      hist_red.pre_account (r);
			      hist_green.pre_account (g);
			      hist_blue.pre_account (b);
			      return true;
			    },
			    "determining RGB value ranges",
			    img, rparam, param, false,
			    xmin, xmax, ymin, ymax, progress))
    return false;
  hist_red.finalize_range (65536);
  hist_green.finalize_range (65536);
  hist_blue.finalize_range (65536);
  if (!analyze_rgb_patches ([&] (coord_t x, coord_t y, rgbdata r, rgbdata g, rgbdata b)
			    {
			      hist_red.account (r);
			      hist_green.account (g);
			      hist_blue.account (b);
			      return true;
			    },
			    "determining RGB value ranges",
			    img, rparam, param, false,
			    xmin, xmax, ymin, ymax, progress))
    return false;
  hist_red.finalize ();
  hist_green.finalize ();
  hist_blue.finalize ();

  /* Give up if the number of samples is too small.  */
  if (hist_red.num_samples () < 2 || hist_green.num_samples () < 2 || hist_blue.num_samples () < 2
      || (progress && progress->cancel_requested ()))
    return false;

  rgbdata gray_red = hist_red.find_avg (0.05, 0.05) - mix_dark;
  rgbdata gray_green = hist_green.find_avg (0.05, 0.05) - mix_dark;
  rgbdata gray_blue = hist_blue.find_avg (0.05, 0.05) - mix_dark;

  color_matrix process_colors (gray_red.red,   gray_green.red,   gray_blue.red, 0,
			       gray_red.green, gray_green.green, gray_blue.green, 0,
			       gray_red.blue,  gray_green.blue,  gray_blue.blue, 0,
			       0, 0, 0, 1);
  process_colors.transpose ();
  process_colors.invert ().apply_to_rgb (1/3.0, 1/3.0, 1/3.0, &mix_red, &mix_green, &mix_blue);
  luminosity_t sum = mix_red + mix_green + mix_blue;
  mix_red /= sum;
  mix_green /= sum;
  mix_blue /= sum;
  bool verbose = true;
  if (verbose)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("mix dark: ");
      mix_dark.print (stdout);
      printf ("gray red: ");
      gray_red.print (stdout);
      printf ("gray green: ");
      gray_green.print (stdout);
      printf ("gray blue: ");
      gray_blue.print (stdout);
      printf ("mix weights %f %f %f\n", mix_red, mix_green, mix_blue);
      printf ("adjusted red %f\n", gray_red.red * mix_red + gray_red.green * mix_green + gray_red.blue * mix_blue);
      printf ("adjusted green %f\n", gray_green.red * mix_red + gray_green.green * mix_green + gray_green.blue * mix_blue);
      printf ("adjusted blue %f\n", gray_blue.red * mix_red + gray_blue.green * mix_green + gray_blue.blue * mix_blue);
      if (progress)
	progress->resume_stdout ();
    }
  return true;
}

bool
render_parameters::auto_mix_dark (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress)
{
  render_parameters rparam = *this;
  rparam.precise = true;
  rgb_histogram hist;
  if (ymin < 0)
    ymin = 0;
  if (xmin < 0)
    xmin = 0;
  if (xmax >= img.width)
    xmax = img.width - 1;
  if (ymax >= img.height)
    ymax = img.height - 1;
  if (ymax <= ymin || xmax <= xmin)
    return false;
  /* TODO: Impleent for stitched projects.  */
  if (img.stitch)
    return false;
  {
    if (progress)
      progress->set_task ("collecting color", (ymax - ymin + 1) * 2);
    render_parameters rparam = *this;
    rparam.ignore_infrared = 0;
    rparam.sharpen_radius = 0;
    rparam.invert = false;
    rparam.dark_point = 0;
    render render (img, rparam, 256);
    if (!render.precompute_all (false, false, {1/3.0, 1/3.0, 1/3.0}, progress))
      return false;
    for (int yy = 0; yy <= ymax - ymin; yy ++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int xx = 0; xx <= xmax - xmin; xx ++)
	    {
	      rgbdata c = render.get_unadjusted_rgb_pixel (xx + xmin, yy + ymin);
	      hist.pre_account (c);
	    }
      }
    hist.finalize_range (65536*256);
    for (int yy = 0; yy <= ymax - ymin; yy ++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int xx = 0; xx <= xmax - xmin; xx ++)
	    {
	      rgbdata c = render.get_unadjusted_rgb_pixel (xx + xmin, yy + ymin);
	      hist.account (c);
	    }
      }
    hist.finalize ();
  }
  if (progress && progress->cancel_requested ())
    return false;
  mix_dark = hist.find_avg (0.3, 0.3);
  bool verbose = true;
  if (verbose)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("mix dark: ");
      mix_dark.print (stdout);
      if (progress)
	progress->resume_stdout ();
    }
  return true;
}

/* Use IR to tune mixing weights to simulate IR channel from RGB to match actual IR channel.  */

bool 
render_parameters::auto_mix_weights_using_ir (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress)
{
  xmin = std::max (0, xmin);
  xmax = std::min (img.width, xmax);
  ymin = std::max (0, ymin);
  ymax = std::min (img.height, ymax);
  if (ymin < 0)
    ymin = 0;
  if (xmin < 0)
    xmin = 0;
  if (xmax >= img.width)
    xmax = img.width - 1;
  if (ymax >= img.height)
    ymax = img.height - 1;
  if (!img.data || !img.rgbdata || xmax <= xmin || ymax <= ymin)
    return false;
  xmax++;
  ymax++;
  int nvariables = 4;
  long nequations = (((long)xmax - xmin) * ((long)ymax - ymin));
  int step = 1;
  const int maxpoints = 10000000;
  if (nequations > maxpoints)
    step = sqrt (nequations / maxpoints);
  int xsteps = (xmax - xmin + step - 1) / step;
  int ysteps = (ymax - ymin + step - 1) / step;
  nequations =  ((long)xsteps) * ysteps;
  gsl_matrix *X = gsl_matrix_alloc (nequations, nvariables);
  gsl_vector *y = gsl_vector_alloc (nequations);
  gsl_vector *w = gsl_vector_alloc (nequations);
  gsl_vector *c = gsl_vector_alloc (nvariables);
  gsl_matrix *cov = gsl_matrix_alloc (nvariables, nvariables);
  /* TODO: Impleent for stitched projects.  */
  if (img.stitch)
    return false;

  {
    if (progress)
      progress->set_task ("collecting color and IR data", ysteps);
    render_parameters rparam = *this;
    rparam.ignore_infrared = 0;
    rparam.sharpen_radius = 0;
    rparam.invert = false;
    render render (img, rparam, 256);
    if (!render.precompute_all (true, false, {1/3.0, 1/3.0, 1/3.0}, progress))
      return false;

#pragma omp parallel for default(none) shared(progress, xmin, ymin, xsteps, ysteps, step, X, y, w, render)
    for (int yy = 0; yy < ysteps; yy ++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int xx = 0; xx < xsteps; xx ++)
	    {
	      int cx = xx * step + xmin;
	      int cy = yy * step + ymin;
	      luminosity_t l = render.get_unadjusted_data (cx, cy);
	      rgbdata c = render.get_unadjusted_rgb_pixel (cx, cy);
	      int n = yy * xsteps + xx;
	      gsl_matrix_set (X, n, 0, 1);
	      gsl_matrix_set (X, n, 1, c.red);
	      gsl_matrix_set (X, n, 2, c.green);
	      gsl_matrix_set (X, n, 3, c.blue);
	      gsl_vector_set (y, n, l);
	      gsl_vector_set (w, n, l > 0 ? 1/l : 1);
	      n++;
	    }
	if (progress)
	  progress->inc_progress ();
      }
  }
  if (progress && progress->cancel_requested ())
    {
      gsl_matrix_free (X);
      gsl_vector_free (y);
      gsl_vector_free (w);
      gsl_matrix_free (cov);
      gsl_vector_free (c);
      return false;
    }
  double chisq;
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (nequations, nvariables);
  if (progress)
    progress->set_task ("optimizing mixing weights", 1);
  gsl_multifit_wlinear (X, w, y, c, cov,
			&chisq, work);
  gsl_multifit_linear_free (work);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_matrix_free (cov);
  mix_red = gsl_vector_get (c, 1);
  mix_green = gsl_vector_get (c, 2);
  mix_blue = gsl_vector_get (c, 3);
  //printf ("solution using %i samples step %i: red:%f green:%f blue:%f dark:%f chi %f\n", n, step, mix_red, mix_green, mix_blue, gsl_vector_get (c, 0), chisq);
  mix_dark = {-gsl_vector_get (c, 0) / mix_red, 0, 0};
  //mix_dark.print (stdout);
  gsl_vector_free (c);
  return true;
}

bool
render_parameters::auto_white_balance (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress, luminosity_t dark_cut, luminosity_t light_cut)
{
  render_parameters rparam = *this;
  /* Produce histogram.  */
  rgb_histogram hist;
  if (!analyze_patches ([&] (coord_t x, coord_t y, rgbdata c)
			{
			  hist.pre_account (c);
			  return true;
			},
			"determining value ranges",
			img, rparam, param, false,
			xmin, xmax, ymin, ymax, progress))
    return false;
  hist.finalize_range (65536*256);
  if (!analyze_patches ([&] (coord_t x, coord_t y, rgbdata c)
			{
			  hist.account (c);
			  return true;
			},
			"producing histograms",
			img, rparam, param, false,
			xmin, xmax, ymin, ymax, progress))
    return false;
  hist.finalize ();

  /* Give up if the number of samples is too small.  */
  if (hist.num_samples () < 2 || (progress && progress->cancel_requested ()))
    return false;
  rgbdata c = hist.find_avg (dark_cut, light_cut);
  render r(img, rparam, 256);
  c.red = r.adjust_luminosity_ir (c.red);
  c.green = r.adjust_luminosity_ir (c.green);
  c.blue = r.adjust_luminosity_ir (c.blue);
  luminosity_t avg = (c.red + c.green + c.blue) / 3;
  white_balance.red = avg / c.red;
  white_balance.green = avg / c.green;
  white_balance.blue = avg / c.blue;
  if (progress)
    progress->pause_stdout ();
  printf ("Adjusted color ");
  c.print (stdout);
  printf ("White balance %f %f %f\n", white_balance.red, white_balance.green, white_balance.blue);
  if (progress)
    progress->resume_stdout ();
  return true;
}

void
render_parameters::adjust_for (render_type_parameters &rtparam, render_parameters &rparam)
{
  const render_type_property &prop = render_type_properties[rtparam.type];
  if (prop.flags & render_type_property::OUTPUTS_SCAN_PROFILE)
    original_render_from (rparam, rtparam.color, false);
  else if (prop.flags & render_type_property::OUTPUTS_SRGB_PROFILE)
    {
      *this = rparam;
      color_model = color_model_none;
      presaturation = 1;
      saturation = 1;
      if (prop.flags & render_type_property::RESET_BRIGHTNESS_ETC)
	{
	  output_tone_curve = tone_curve::tone_curve_linear;
	  brightness = 1;
	  dark_point = 0;
	}
    }
  else
    *this = rparam;
}
void
render_parameters::compute_mix_weights (rgbdata patch_proportions)
{
  rgbdata sprofiled_red = profiled_red /*/ patch_proportions.red*/;
  rgbdata sprofiled_green = profiled_green /*/ patch_proportions.green*/;
  rgbdata sprofiled_blue = profiled_blue /*/ patch_proportions.blue*/;
  bool verbose = true;
  color_matrix process_colors (sprofiled_red.red,   sprofiled_green.red,   sprofiled_blue.red,   0,
			       sprofiled_red.green, sprofiled_green.green, sprofiled_blue.green, 0,
			       sprofiled_red.blue,  sprofiled_green.blue,  sprofiled_blue.blue,  0,
			       0, 0, 0, 1);
  process_colors.transpose ();
  mix_dark = profiled_dark;
  process_colors.invert ().apply_to_rgb (
      3 * patch_proportions.red / white_balance.red,
      3 * patch_proportions.green / white_balance.green,
      3 * patch_proportions.blue / white_balance.blue, &mix_red, &mix_green,
      &mix_blue);
  white_balance = { 1, 1, 1 };
  if (verbose)
    {
      printf ("profiled dark ");
      profiled_dark.print (stdout);
      printf ("Scaled profiled red ");
      sprofiled_red.print (stdout);
      printf ("Scaled profiled green ");
      sprofiled_green.print (stdout);
      printf ("Scaled profiled blue ");
      sprofiled_blue.print (stdout);
      printf ("mix weights %f %f %f\n", mix_red, mix_green, mix_blue);
      printf ("%f %f\n",
	      profiled_red.red * mix_red + profiled_red.green * mix_green
		  + profiled_red.blue * mix_blue,
	      patch_proportions.red);
      printf ("%f %f\n",
	      profiled_green.red * mix_red + profiled_green.green * mix_green
		  + profiled_green.blue * mix_blue,
	      patch_proportions.green);
      printf ("%f %f\n",
	      profiled_blue.red * mix_red + profiled_blue.green * mix_green
		  + profiled_blue.blue * mix_blue,
	      patch_proportions.blue);
    }
}
/* Initialize render parameters for showing original scan.
   In this case we do not want to apply color models etc.  */
void
render_parameters::original_render_from (render_parameters &rparam, bool color, bool profiled)
{
  if (profiled)
    {
      *this = rparam;
      return;
    }
  backlight_correction = rparam.backlight_correction;
  backlight_correction_black = rparam.backlight_correction_black;
  gamma = rparam.gamma;
  invert = rparam.invert;
  scan_exposure = rparam.scan_exposure;
  ignore_infrared = rparam.ignore_infrared;
  scanner_red = rparam.scanner_red;
  scanner_green = rparam.scanner_green;
  scanner_blue = rparam.scanner_blue;
  dark_point = rparam.dark_point;
  brightness = rparam.brightness;
  color_model = color ? (profiled ? rparam.color_model
				  : render_parameters::color_model_scan)
		      : render_parameters::color_model_none;
  output_tone_curve = rparam.output_tone_curve;
  tile_adjustments = rparam.tile_adjustments;
  tile_adjustments_width = rparam.tile_adjustments_width;
  tile_adjustments_height = rparam.tile_adjustments_height;
  output_profile = rparam.output_profile;
  if (color)
    white_balance = rparam.white_balance;
  else
    {
      mix_dark = rparam.mix_dark;
      mix_red = rparam.mix_red;
      mix_green = rparam.mix_green;
      mix_blue = rparam.mix_blue;
    }
  sharpen_amount = rparam.sharpen_amount;
  sharpen_radius = rparam.sharpen_radius;

  /* Copy setup of interpolated rendering algorithm.  */
  precise = rparam.precise;
  collection_threshold = rparam.collection_threshold;
  screen_blur_radius = rparam.screen_blur_radius;
}

color_matrix
render_parameters::get_profile_matrix (rgbdata patch_proportions)
{
  color_matrix subtract_dark (1, 0, 0, -profiled_dark.red,
			      0, 1, 0, -profiled_dark.green,
			      0, 0, 1, -profiled_dark.blue,
			      0, 0, 0, 1);
  color_matrix process_colors (profiled_red.red * patch_proportions.red, profiled_green.red * patch_proportions.green, profiled_blue.red * patch_proportions.blue, 0,
			       profiled_red.green * patch_proportions.red, profiled_green.green * patch_proportions.green, profiled_blue.green * patch_proportions.blue, 0,
			       profiled_red.blue * patch_proportions.red, profiled_green.blue * patch_proportions.green, profiled_blue.blue * patch_proportions.blue, 0,
			       0, 0, 0, 1);
  color_matrix ret = process_colors.invert ();
  ret = ret * subtract_dark;
  return ret;
}

/* Set invert, exposure and dark_point for a given range of values
   in input scan.  Used to interpret old gray_range parameter
   and can be removed eventually.  */
void 
render_parameters::set_gray_range (int gray_min, int gray_max, int maxval)
{
  if (gray_min < gray_max)
    {
      luminosity_t min2
	  = apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
      luminosity_t max2
	  = apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
      scan_exposure = 1 / (max2 - min2);
      dark_point = min2;
      invert = false;
    }
  else if (gray_min > gray_max)
    {
      luminosity_t min2
	  = 1 / apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
      luminosity_t max2
	  = 1 / apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
      scan_exposure = 1 / (max2 - min2);
      dark_point = min2;
      invert = true;
    }
}
void
render_parameters::get_gray_range (int *min, int *max, int maxval)
{
  if (!invert)
    {
      *min = invert_gamma (dark_point, gamma) * maxval - 0.5;
      *max = invert_gamma (dark_point + 1 / scan_exposure, gamma) * maxval
	     - 0.5;
    }
  else
    {
      *min = (invert_gamma (1 / dark_point, gamma) * maxval) - 0.5;
      *max = (invert_gamma (1 / (((dark_point + 1 / scan_exposure))), gamma)
	      * maxval)
	     - 0.5;
    }
}
}
