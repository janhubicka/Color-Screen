#include "include/render.h"
#include "include/stitch.h"
#include "icc.h"
#include "dufaycolor.h"
#include "wratten.h"
#include "include/spectrum-to-xyz.h"
#include "render-interpolate.h"
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
rgbdata patch_proportions (enum scr_type t)
{
  switch (t)
    {
    case Paget:
    case Thames:
    case Finlay:
      /* TODO: Measure actual portions.  */
      return {1/3.0,1/3.0,1/3.0};
    case Dufay:
      return {dufaycolor::red_portion, dufaycolor::green_portion, dufaycolor::blue_portion};
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
render_parameters::get_dyes_matrix (bool *spectrum_based, bool *optimized, image_data *img)
{
  spectrum_dyes_to_xyz *m_spectrum_dyes_to_xyz = NULL;
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
	  color_matrix m (1, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0, 0, 0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_green:
	{
	  color_matrix m (0, 0.5,  0, 0,
			  0, 1,0, 0,
			  0, 0.5,0, 0,
			  0, 0, 0,1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_blue:
	{
	  color_matrix m (0, 0, 0.5,  0,
			  0, 0, 0.5,0,
			  0, 0, 1,0,
			  0, 0, 0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
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
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
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
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);
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
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_photography_its_materials_and_processes);
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
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_collins_giles);
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
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography,
					    (spectrum_dyes_to_xyz::dyes)((int)color_model - (int)render_parameters::color_model_dufay1 + (int)spectrum_dyes_to_xyz::dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor), age);
	  break;
	}
      /* Kodachrome is subtractive, it needs to be computed by spectrum_dyes_to_xyz.  */
      case render_parameters::color_model_kodachrome25:
	{
	  color_matrix id;
	  return id;
	}
      case render_parameters::color_model_max:
	abort ();
    }
  if (is_srgb)
    {
      srgb_xyz_matrix m;
      dyes = m * dyes;
    }
  if (m_spectrum_dyes_to_xyz)
    {
      m_spectrum_dyes_to_xyz->set_backlight (spectrum_dyes_to_xyz::il_D, backlight_temperature);
      /* At the moment all conversion we do are linear conversions.  In that case
         we can build XYZ matrix and proceed with that.  */
      if (debug && !m_spectrum_dyes_to_xyz->is_linear ())
	abort ();
      else
	{
	  *spectrum_based = true;
	  dyes = m_spectrum_dyes_to_xyz->xyz_matrix ();
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
render_parameters::get_balanced_dyes_matrix (image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint)
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
render_parameters::get_rgb_to_xyz_matrix (image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint)
{
  color_matrix color = get_rgb_adjustment_matrix (normalized_patches, patch_proportions);
  color = get_balanced_dyes_matrix (img, normalized_patches, patch_proportions, target_whitepoint) * color;
  //color = color * brightness;
  //
  if (saturation != 1 && color_model != color_model_kodachrome25)
    {
      saturation_matrix m (saturation);
      color = m * color;
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

/* Determine black and brightness by analysing tile of a scan.  */
bool
render_parameters::auto_dark_brightness (image_data &img, scr_to_img_parameters &param, int xmin, int ymin, int xmax, int ymax, progress_info *progress, luminosity_t dark_cut, luminosity_t light_cut)
{
  rgb_histogram hist;
  render_parameters rparam = *this;
  rparam.precise = true;
  render_interpolate render (param, img, rparam, 256);
  render.precompute_img_range (xmin, ymin, xmax, ymax, progress);
  if (progress && progress->cancel_requested ())
    return false;
  render.collect_histogram (hist, xmin, xmax, ymin, ymax, progress);
  if (hist.num_samples () < 2 || (progress && progress->cancel_requested ()))
    return false;
  rgbdata minvals = hist.find_min (dark_cut);
  rgbdata maxvals = hist.find_max (light_cut);
  minvals.print (stdout);
  maxvals.print (stdout);
  dark_point = std::min (std::min (minvals.red, minvals.green), minvals.blue);
  brightness = 1 / (std::max (std::max (maxvals.red, maxvals.green), maxvals.blue) - dark_point);
  return true;
}
