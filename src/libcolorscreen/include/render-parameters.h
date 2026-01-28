#ifndef RENDER_PARAMETERS_H
#define RENDER_PARAMETERS_H
#include <array>
#include "base.h"
#include "color.h"
#include "progress-info.h"
#include "sensitivity.h"
#include "tone-curve.h"
#include "mtf-parameters.h"
#include "scr-to-img-parameters.h"
#include "backlight-correction-parameters.h"
#include "scanner-blur-correction-parameters.h"
#include "imagedata.h"
namespace colorscreen
{
class render_type_parameters;
class render_type_property;
class stitch_project;



/* Parameters of sharpening.  */
struct sharpen_parameters
{
  enum sharpen_mode
  {
    none,
    unsharp_mask,
    wiener_deconvolution,
    richardson_lucy_deconvolution,
    blur_deconvolution,
    sharpen_mode_max
  };
  DLL_PUBLIC static const property_t sharpen_mode_names[(int)sharpen_mode_max];

  /* Sharpening mode.  */
  enum sharpen_mode mode;

  /* Radius (in pixels) and amount for unsharp-mask filter.  */
  luminosity_t usm_radius, usm_amount;

  /* MTF curve of scanner.  */
  mtf_parameters scanner_mtf;

  /* Signal to noise ratio of the scanner.  */
  luminosity_t scanner_snr;

  /* Scale of scanner mtf. 0 disables deconvolution sharpening.  */
  luminosity_t scanner_mtf_scale;

  /* Number of iterations of Richardson-Lucy deconvolution sharpening.
     If 0, much faster Weiner filter will be used.  */
  int richardson_lucy_iterations;

  /* Dampening parameter sigma.  */
  luminosity_t richardson_lucy_sigma;

  /* Supersampling for deconvolution sharpening.  */
  int supersample;

  enum sharpen_mode get_mode () const
  {
    switch (mode)
    {
      case none:
	return none;
      case unsharp_mask:
	return usm_radius > 0 && usm_amount > 0 ? unsharp_mask : none;
      case wiener_deconvolution:
        return scanner_mtf_scale > 0 ? wiener_deconvolution : none;
      case richardson_lucy_deconvolution:
        return scanner_mtf_scale > 0 && richardson_lucy_iterations
	       ? richardson_lucy_deconvolution : none;
      case blur_deconvolution:
        return scanner_mtf_scale > 0 
	       ? blur_deconvolution : none;
      default:
	abort ();
    }
  };
  bool deconvolution_p () const
  {
    enum sharpen_mode mode = get_mode ();
    return mode == wiener_deconvolution || mode == richardson_lucy_deconvolution
	   || mode == blur_deconvolution;
  }

  /* Return true if THIS and O will produce same image.
     Used for caching.
     
     Allow small differences in scale since screen may change during editing.  */
  bool
  operator== (const sharpen_parameters &o) const
  {
    enum sharpen_mode mode = get_mode ();
    if (o.get_mode () != mode)
      return false;
    switch (mode)
      {
      case none:
	return true;
      case unsharp_mask:
	return fabs (usm_radius - o.usm_radius) < 0.001
	       && usm_amount == o.usm_amount;
      case wiener_deconvolution:
	return scanner_mtf == o.scanner_mtf
	       && fabs (scanner_mtf_scale - o.scanner_mtf_scale) < 0.001
	       && scanner_snr == o.scanner_snr
	       && supersample == o.supersample;
      case richardson_lucy_deconvolution:
        return scanner_mtf == o.scanner_mtf
	       && fabs (scanner_mtf_scale - o.scanner_mtf_scale) < 0.001
	       && richardson_lucy_iterations == o.richardson_lucy_iterations
	       && richardson_lucy_sigma == o.richardson_lucy_sigma
	       && supersample == o.supersample;
      case blur_deconvolution:
        return scanner_mtf == o.scanner_mtf
	       && fabs (scanner_mtf_scale - o.scanner_mtf_scale) < 0.001;
      default:
	abort ();
      }
    abort ();
  }
  /* Return true if THIS and O have same data.  */
  bool equal_p (const sharpen_parameters &o) const
  {
    return mode == o.mode
	   && usm_radius == o.usm_radius
           && usm_amount == o.usm_amount
	   && scanner_mtf.equal_p (o.scanner_mtf)
	   && scanner_snr == o.scanner_snr
	   && scanner_mtf_scale == o.scanner_mtf_scale
	   && richardson_lucy_iterations == o.richardson_lucy_iterations
	   && richardson_lucy_sigma == o.richardson_lucy_sigma
	   && supersample == o.supersample;
  }
  sharpen_parameters ()
  : mode (none), usm_radius (0), usm_amount (0), scanner_snr (2000), scanner_mtf_scale (1),
    richardson_lucy_iterations (0), richardson_lucy_sigma (0), supersample (2)
  { }
};

/* Parameters of rendering algorithms.  */
struct render_parameters
{
  enum capture_type
  {
    capture_unknown,
    capture_transparency_with_screen,
    capture_transparency_with_screen_and_infrared,
    capture_negative_with_screen,
    capture_negative_with_screen_and_infrared,
    capture_transparency,
    capture_negative,
    capture_max
  };
  class capture_type_property
  {
  public:
    const char *name;
    const char *pretty_name;
    int flags;
    enum flag
    {
      SUPPORTS_SCR_DETECT = 1,
      HAS_IR = 2,
      MAYBE_MONOCHROMATIC_DEMOSAIC = 4
    };
  };
  DLL_PUBLIC static const capture_type_property capture_properties[capture_max];
  capture_type capture_type;
  /* Demosaicing algorithm.  Not actually used for rendering, only passed
     to image_data::load.  */
  image_data::demosaicing_t demosaic;
  /***** Scan linearization parmaeters  *****/

  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).
     Only positive values makes sense; meaningful range is approx 0.01 to 10.
   */
  luminosity_t gamma;

  /* Rotation of scan (in multiples of 90) to be used by UI nad file output.  */
  int scan_rotation;
  /* If set mirror the image horisontally.  */
  bool scan_mirror;
  /* Crop of scan (in image coordinates.  */
  int_optional_image_area scan_crop;
  

  /* TODO; Invert is applied before backlight correction which is wrong.  */
  std::shared_ptr <backlight_correction_parameters> backlight_correction;
  luminosity_t backlight_correction_black;

  std::shared_ptr <scanner_blur_correction_parameters> scanner_blur_correction;

  /* After linearizing we apply (val - dark_point) * scan_exposure  */
  luminosity_t dark_point, scan_exposure;

  /* Ignore infrared channel and produce fake one using RGB data.  */
  bool ignore_infrared;

  /* True if negatuve should be inverted to positive.  */
  bool invert;

  /* Black subtracted before channel mixing.  */
  rgbdata mix_dark;
  /* Parameters used to turn RGB data to grayscale (fake infrared channel):
     mix_red,green and blue are relative weights.  */
  luminosity_t mix_red, mix_green, mix_blue;

  sharpen_parameters sharpen;

  /***** Tile Adjustment (used to adjust parameters of individual tiles) *****/

  struct tile_adjustment
  {
    /* Multiplicative correction to stitch-project global scan_exposure.  */
    luminosity_t exposure;
    /* Additive correction to stitch-project dark point.  */
    luminosity_t dark_point;
    /* Scanner blur usually differs for every capture  */
    std::shared_ptr <scanner_blur_correction_parameters> scanner_blur_correction;
    /* If true tile is rendered, if false tile is not rendered.  */
    bool enabled;
    /* Coordinates of the tile in stitch project (used to check that tile
       adjustments match stitch project dimensions).  */
    unsigned char x, y;
    constexpr
    tile_adjustment ()
        : exposure (1), dark_point (0), scanner_blur_correction (NULL),
          enabled (true), x (0), y (0)
    {
    }
    bool
    operator== (const tile_adjustment &other) const
    {
      return enabled == other.enabled && dark_point == other.dark_point
             && exposure == other.exposure
             && scanner_blur_correction == other.scanner_blur_correction;
    }
    bool
    operator!= (const tile_adjustment &other) const
    {
      return !(*this == other);
    }
    void
    apply (render_parameters *p) const
    {
      p->dark_point = dark_point + exposure * p->dark_point;
      p->scan_exposure *= exposure;
      if (scanner_blur_correction)
        p->scanner_blur_correction = scanner_blur_correction;
    }
  };

  int tile_adjustments_width, tile_adjustments_height;
  std::vector<tile_adjustment> tile_adjustments;

  /***** Patch density parameters  *****/

  /* Gamma curve of the film (to be replaced by HD curve eventually)  */
  luminosity_t film_gamma;
  /* The following is used by interpolated rendering only.  */

  /* Quality used when collection data for demosaicing.  */
  enum collection_quality_t
  {
    fast_collection,
    simple_screen_collection,
    simulated_screen_collection,
    max_collection_quality
  };
  collection_quality_t collection_quality;
  DLL_PUBLIC static const property_t collection_quality_names[(int)max_collection_quality];

  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;

  /* Width of strips used to print the screen.
     This is relative portion in range 0..1.
     0 will give default values.  
     
     It only has effect for Dufay type screens where
     all values in range 0...1 are meaningful since the
     two strips are printed in angel.

     For screens with vertical strips this is only meaingful
     if the sum of two is strictly less than 1, so there is 
     space for the last strip.  */
  coord_t red_strip_width, green_strip_width;

  /***** Scanner profile *****/

  /* Matrix profile of scanner.
     If values are (0,0,0) then data obtained from image_data will be used.
     It is only valid for RAW images where libraw understand the camera matrix.
   */
  xyz scanner_red;
  xyz scanner_green;
  xyz scanner_blue;

  /***** Process profile profile *****/

  /* Profile used to convert RGB data of scanner to RGB data of the color
     process. Dark is the dark point of scanner (which is subtracted first).
     Red is scanner's response to red filter etc.  */
  rgbdata profiled_dark;
  rgbdata profiled_red;
  rgbdata profiled_green;
  rgbdata profiled_blue;

  /***** Output Adjustment *****/

  /* White balance adjustment in dye coordinates.  */
  rgbdata white_balance;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of
     sharpness). Only positive values makes sense; meaningful range is approx
     0.1 to 10.  */
  luminosity_t presaturation;

  /* Specify spectra or XYZ coordinates of color dyes used in the process.  */
  enum color_model_t
  {
    color_model_srgb,
    color_model_red,
    color_model_green,
    color_model_blue,
    color_model_miethe_goerz_reconstructed_wager,
    color_model_miethe_goerz_original_wager,
    color_model_wratten_25_58_47_xyz,
    color_model_wratten_25_58_47_spectra,
    color_model_dufay_manual,
    color_model_dufay_color_cinematography_xyY,
    color_model_dufay_color_cinematography_xyY_correctedY,
    color_model_dufay_color_cinematography_wavelength,
    color_model_dufay_color_cinematography_spectra,
    color_model_dufay_color_cinematography_spectra_correction,
    color_model_dufay_harrison_horner_spectra,
    color_model_dufay_harrison_horner_spectra_correction,
    color_model_dufay_collins_giles_spectra,
    color_model_dufay_collins_giles_spectra_correction,
    color_model_dufay_photography_its_materials_and_processes_spectra,
    color_model_dufay_photography_its_materials_and_processes_spectra_correction,
    color_model_dufay1,
    color_model_dufay2,
    color_model_dufay3,
    color_model_dufay4,
    color_model_dufay5,
    color_model_autochrome_ciortan_arteaga_trumpy,
    color_model_autochrome,
    color_model_autochrome2,
    color_model_thames_mees_pledge,
    color_model_dioptichrome_mees_pledge,
    color_model_autochrome_mees_pledge,
    color_model_kodachrome25,
    color_model_paget,
    color_model_max_separation,
    color_model_none,
    color_model_scan,
    color_model_max
  };
  struct color_model_property
  {
    const char *name;
    const char *pretty_name;
    const char *description;
    unsigned int flags;
  };
  static const int SUPPORTS_AGING = 1;
  static const int SPECTRA_BASED = 2; // For informational purposes
  DLL_PUBLIC static const color_model_property color_model_properties[(int)color_model_max];
  enum color_model_t color_model;
  /* Aging simulation (0 new dyes, 1 aged dyes).
     Only effective for color models that support aging simulation.  */
  rgbdata age;
  /* Density (concentration) adjustment of dyes in color screen.  */
  rgbdata dye_density;
  /* Temperature in K of daylight in photograph.  */
  static const int temperature_min = 2500;
  static const int temperature_max = 25000;
  luminosity_t temperature;
  /* Temperature in K of backlight when viewing the slide.  */
  luminosity_t backlight_temperature;
  /* Whitepoint observer's eye is adapted to.  */
  xy_t observer_whitepoint;
  /* White balancing to apply to color dyes.  */
  enum dye_balance_t
  {
    dye_balance_none,
    dye_balance_brightness,
    dye_balance_bradford,
    dye_balance_neutral,
    dye_balance_whitepoint,
    dye_balance_max
  };
  DLL_PUBLIC static const property_t dye_balance_names[(int)dye_balance_max];
  /* How to balance dye colors.  */
  enum dye_balance_t dye_balance;

  /* Saturation increase.  */
  luminosity_t saturation;
  /* Brightness adjustments.  */
  luminosity_t brightness;
  enum tone_curve::tone_curves output_tone_curve;
  /* desired gamma of the resulting image.  */
  luminosity_t target_film_gamma;

  /***** Output Profile *****/

  /* If true apply color model of Finlay taking plate.  */
  enum output_profile_t
  {
    output_profile_sRGB,
    output_profile_xyz,
    output_profile_original,
    output_profile_max
  };

  output_profile_t output_profile;
  DLL_PUBLIC static const char *output_profile_names[(int)output_profile_max];

  /* Output gamma.  -1 means sRGB transfer curve.  */
  luminosity_t output_gamma;

  bool gammut_warning;

  /***** Experimental (unfinished) stuff *****/

  hd_curve *film_characteristics_curve;
  hd_curve *output_curve;

  /* Use characteristics curves to restore original luminosity.  */
  bool restore_original_luminosity;

  render_parameters ()
      : 
	demosaic (image_data::demosaic_default),
	/* Scan linearization.  */
        gamma (-1), scan_rotation (0), scan_mirror (false), backlight_correction (NULL),
        backlight_correction_black (0), scanner_blur_correction (NULL),
        dark_point (0), scan_exposure (1), ignore_infrared (false),
        invert (false), mix_dark (0, 0, 0), mix_red (0.3), mix_green (0.1),
        mix_blue (1),  sharpen (),

        /* Tile adjustment.  */
        tile_adjustments_width (0), tile_adjustments_height (0),
        tile_adjustments (),

        /* Patch density parameters.  */
        film_gamma (1), collection_quality (simple_screen_collection), screen_blur_radius (0.5),
        collection_threshold (0.2), red_strip_width (0),
        green_strip_width (0),

        /* Scanner profile.  */
        scanner_red (0, 0, 0), scanner_green (0, 0, 0), scanner_blue (0, 0, 0),

        /* Process profile.  */
        profiled_dark (0, 0, 0), profiled_red (1, 0, 0),
        profiled_green (0, 1, 0), profiled_blue (0, 0, 1),

        /* Output adjustment.  */
        white_balance ({ 1, 1, 1 }), presaturation (1),
        color_model (color_model_wratten_25_58_47_spectra), age {1,1,1}, dye_density {1,1,1}, temperature (5000),
        backlight_temperature (5000),
        observer_whitepoint (/*srgb_white*/ d50_white),
        dye_balance (dye_balance_bradford), saturation (1), brightness (1),
        output_tone_curve (tone_curve::tone_curve_linear),
        target_film_gamma (1),

        /* Output profile  */
        output_profile (output_profile_sRGB), output_gamma (-1), gammut_warning (false),
        film_characteristics_curve (&film_sensitivity::linear_sensitivity),
        output_curve (NULL), restore_original_luminosity (true)
  {
  }

  /* Accessors.  */

  color_matrix get_rgb_to_xyz_matrix (const image_data *img,
                                      bool normalized_patches,
                                      rgbdata patch_proportions,
                                      xyz target_whitepoint = d50_white);
  color_matrix get_rgb_adjustment_matrix (bool normalized_patches,
                                          rgbdata patch_proportions);
  size_t get_icc_profile (void **buf, image_data *img, bool normalized_dyes);
  const tile_adjustment &get_tile_adjustment (stitch_project *stitch, int x,
                                              int y) const;
  tile_adjustment &get_tile_adjustment_ref (stitch_project *stitch, int x,
                                            int y);
  DLL_PUBLIC tile_adjustment &get_tile_adjustment (int x, int y);

  bool
  operator== (const render_parameters &other) const
  {
    if (tile_adjustments.size () != other.tile_adjustments.size ()
        || tile_adjustments_width != other.tile_adjustments_width
        || tile_adjustments_height != other.tile_adjustments_height)
      return false;
    for (unsigned int i = 0; i < tile_adjustments.size (); i++)
      if (tile_adjustments[i] != other.tile_adjustments[i])
        return false;
    return demosaic == other.demosaic
	   && gamma == other.gamma && film_gamma == other.film_gamma
           && target_film_gamma == other.target_film_gamma
           && output_gamma == other.output_gamma
	   && scan_rotation == other.scan_rotation
	   && scan_mirror == other.scan_mirror
	   && scan_crop == other.scan_crop
	   && sharpen.equal_p (other.sharpen)
           && presaturation == other.presaturation
	   && gammut_warning == other.gammut_warning
           && saturation == other.saturation && brightness == other.brightness
           && collection_threshold == other.collection_threshold
           && mix_dark == other.mix_dark && mix_red == other.mix_red
           && mix_green == other.mix_green && mix_blue == other.mix_blue
           && color_model == other.color_model
           && ignore_infrared == other.ignore_infrared
           && profiled_dark == other.profiled_dark
           && profiled_red == other.profiled_red
           && profiled_green == other.profiled_green
           && profiled_blue == other.profiled_blue
           && scanner_red == other.scanner_red
           && scanner_green == other.scanner_green
           && scanner_blue == other.scanner_blue && age == other.age
	   && dye_density == other.dye_density
           && backlight_temperature == other.backlight_temperature
           && dark_point == other.dark_point
           && scan_exposure == other.scan_exposure
           && red_strip_width == other.red_strip_width
           && green_strip_width == other.green_strip_width
           && invert == other.invert
           && screen_blur_radius == other.screen_blur_radius
           && dye_balance == other.dye_balance && collection_quality == other.collection_quality
           && film_characteristics_curve == other.film_characteristics_curve
           && restore_original_luminosity == other.restore_original_luminosity
           && output_curve == other.output_curve
           && scanner_blur_correction == other.scanner_blur_correction
           && backlight_correction == other.backlight_correction
           && backlight_correction_black == other.backlight_correction_black
	   && observer_whitepoint == other.observer_whitepoint
	   && white_balance == other.white_balance;
  }
  bool
  operator!= (render_parameters &other) const
  {
    return !(*this == other);
  }
  /* Set invert, exposure and dark_point for a given range of values
     in input scan.  Used to interpret old gray_range parameter
     and can be removed eventually.  */
  DLL_PUBLIC void set_gray_range (int gray_min, int gray_max, int maxval);
  DLL_PUBLIC void get_gray_range (int *min, int *max, int maxval);
  DLL_PUBLIC void set_tile_adjustments_dimensions (int w, int h);
  DLL_PUBLIC void compute_mix_weights (rgbdata patch_proportions);
  DLL_PUBLIC bool auto_color_model (enum scr_type type);
  DLL_PUBLIC bool auto_dark_brightness (image_data &img,
                                        scr_to_img_parameters &par, int xmin,
                                        int ymin, int xmax, int ymax,
                                        progress_info *progress = NULL,
                                        luminosity_t dark_cut = 0.01,
                                        luminosity_t light_cut = 0.001);
  DLL_PUBLIC bool auto_mix_weights (image_data &img,
                                    scr_to_img_parameters &param, int xmin,
                                    int ymin, int xmax, int ymax,
                                    progress_info *progress);
  DLL_PUBLIC bool auto_mix_dark (image_data &img,
                                 scr_to_img_parameters &param, int xmin,
                                 int ymin, int xmax, int ymax,
                                 progress_info *progress);
  DLL_PUBLIC bool auto_mix_weights_using_ir (image_data &img,
                                             scr_to_img_parameters &param,
                                             int xmin, int ymin, int xmax,
                                             int ymax,
                                             progress_info *progress);
  DLL_PUBLIC bool auto_white_balance (image_data &img,
                                      scr_to_img_parameters &par, int xmin,
                                      int ymin, int xmax, int ymax,
                                      progress_info *progress = NULL,
                                      luminosity_t dark_cut = 0.01,
                                      luminosity_t light_cut = 0.001);
  struct transmission_data
  {
    luminosity_t min_freq;
    luminosity_t max_freq;
    std::vector<luminosity_t> red;
    std::vector<luminosity_t> green;
    std::vector<luminosity_t> blue;
    std::vector<luminosity_t> backlight;
  };
  DLL_PUBLIC bool get_transmission_data (transmission_data &data) const;

  /* Initialize render parameters for showing original scan.
     In this case we do not want to apply color models etc.  */
  void original_render_from (render_parameters &rparam, bool color,
                             bool profiled);

  void adjust_for (render_type_parameters &rtparam, render_parameters &rparam);
  color_matrix get_profile_matrix (rgbdata patch_proportions);
  color_matrix get_dyes_matrix (bool *spectrum_based, bool *optimized,
                                const image_data *img, transmission_data *transmission_data = NULL) const;

  int_image_area
  get_scan_crop (int img_width, int img_height) const
  {
    int_image_area img (0, 0, img_width, img_height);
    if (!scan_crop.set)
      return img;
    int_image_area intersection = scan_crop.intersect (img);
    if (intersection.empty_p ())
      return img;
    return intersection;
  }

  static enum capture_type
  get_capture_type (enum capture_type capture_type, image_data *scan)
  {
    if (!scan)
      return capture_unknown;
    switch (capture_type)
      {
	case capture_unknown:
	case capture_transparency:
	case capture_negative:
	  return capture_unknown;
	  break;
	case capture_transparency_with_screen:
	  if (!scan->has_rgb ())
	    return capture_transparency;
	  return capture_transparency_with_screen;
	  break;
	case capture_negative_with_screen:
	  if (!scan->has_rgb ())
	    return capture_negative;
	  return capture_negative_with_screen;
	  break;
	case capture_transparency_with_screen_and_infrared:
	  if (!scan->has_rgb ())
	    return capture_transparency;
	  if (!scan->has_grayscale_or_ir ())
	    return capture_transparency_with_screen;
	  return capture_transparency_with_screen_and_infrared;
	  break;
	case capture_negative_with_screen_and_infrared:
	  if (!scan->has_rgb ())
	    return capture_negative;
	  if (!scan->has_grayscale_or_ir ())
	    return capture_negative_with_screen;
	  return capture_negative_with_screen_and_infrared;
	  break;
	default:
	  abort ();
      }
  }
  enum capture_type
  get_capture_type (image_data *scan)
  {
    return get_capture_type (capture_type, scan);
  }

private:
  static const bool debug = colorscreen_checking;
  color_matrix get_balanced_dyes_matrix (const image_data *img,
                                         bool normalized_patches,
                                         rgbdata patch_proportions,
                                         xyz target_whitepoint = d50_white);
};
}
#endif
