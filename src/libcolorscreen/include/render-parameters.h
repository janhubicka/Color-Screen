/* Rendering parameters.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

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

/* Parameters for simulating a contact copy on a positive emulsion.  */
struct contact_copy_parameters
{
  /* If true, simulate the contact copy process.  */
  bool simulate = false;
  /* Emulsion characteristic curve (H&D curve).  */
  hd_curve_parameters emulsion_characteristic_curve;

  /* Light exposure before exposure in enlarger.  */
  luminosity_t preflash = 0;
  /* Exposure time in enlarger.  */
  luminosity_t exposure = 1;
  /* Density boost (development time).  */
  luminosity_t boost = 1;

  /* Default constructor.  */
  constexpr contact_copy_parameters () = default;

  /* Return true if THIS and O are equal.  */
  pure_attr bool
  operator== (const contact_copy_parameters &o) const
  {
    return simulate == o.simulate
	   && preflash == o.preflash && boost == o.boost
	   && emulsion_characteristic_curve == o.emulsion_characteristic_curve
	   && exposure == o.exposure;
  }
};


/* Parameters of sharpening.  */
struct sharpen_parameters
{
  /* Supported sharpening modes.  */
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
  enum sharpen_mode mode = none;

  /* Radius (in pixels) and amount for unsharp-mask filter.  */
  luminosity_t usm_radius = 0, usm_amount = 0;

  /* MTF curve of scanner.  */
  mtf_parameters scanner_mtf;

  /* Signal to noise ratio of the scanner.  */
  luminosity_t scanner_snr = 2000;

  /* Scale of scanner mtf. 0 disables deconvolution sharpening.  */
  luminosity_t scanner_mtf_scale = 1;

  /* Number of iterations of Richardson-Lucy deconvolution sharpening.
     If 0, much faster Wiener filter will be used.  */
  int richardson_lucy_iterations = 0;

  /* Dampening parameter sigma.  */
  luminosity_t richardson_lucy_sigma = 0;

  /* Supersampling for deconvolution sharpening.  */
  int supersample = 2;

  /* Return effective sharpening mode.  */
  pure_attr enum sharpen_mode get_mode () const
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

  /* Return true if sharpening mode is a deconvolution.  */
  pure_attr bool deconvolution_p () const
  {
    enum sharpen_mode mode = get_mode ();
    return mode == wiener_deconvolution || mode == richardson_lucy_deconvolution
	   || mode == blur_deconvolution;
  }

  /* Return true if THIS and O will produce same image.
     Used for caching.
     
     Allow small differences in scale since screen may change during editing.  */
  pure_attr bool
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
  pure_attr bool equal_p (const sharpen_parameters &o) const
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

  /* Default constructor.  */
  sharpen_parameters () = default;
};

/* Parameters of rendering algorithms.  */
struct render_parameters
{
  /* Type of capture.  */
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

  /* Properties of a capture type.  */
  class capture_type_property
  {
  public:
    /* Identifier.  */
    const char *name;
    /* Human readable name.  */
    const char *pretty_name;
    /* Flags.  */
    int flags;
    /* Supported flags.  */
    enum flag
    {
      SUPPORTS_SCR_DETECT = 1,
      HAS_IR = 2,
      MAYBE_MONOCHROMATIC_DEMOSAIC = 4
    };
  };
  DLL_PUBLIC static const capture_type_property capture_properties[capture_max];
  /* Type of capture.  */
  capture_type capture_type = capture_unknown;

  /* Demosaicing algorithm.  Not actually used for rendering, only passed
     to image_data::load.  */
  image_data::demosaicing_t demosaic = image_data::demosaic_default;

  /***** Scan linearization parameters *****/

  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).
     Only positive values makes sense; meaningful range is approx 0.01 to 10.
     If -1, then sRGB transfer curve is used.  */
  luminosity_t gamma = -1;

  /* Rotation of scan (in multiples of 90) to be used by UI and file output.  */
  int scan_rotation = 0;
  /* If set mirror the image horizontally.  */
  bool scan_mirror = false;
  /* Crop of scan (in image coordinates).  */
  int_optional_image_area scan_crop;
  /* Area of scan containing the actual image (no bordrs).  */
  int_optional_image_area image_area;
  
  /* Parameters for backlight correction.
     TODO; Invert is applied before backlight correction which is wrong.  */
  std::shared_ptr <backlight_correction_parameters> backlight_correction = nullptr;
  /* Dark point for backlight correction.  */
  luminosity_t backlight_correction_black = 0;

  /* Parameters for scanner blur correction.  */
  std::shared_ptr <scanner_blur_correction_parameters> scanner_blur_correction = nullptr;

  /* After linearizing we apply (val - dark_point) * scan_exposure.  */
  luminosity_t dark_point = 0, scan_exposure = 1;

  /* Ignore infrared channel and produce fake one using RGB data.  */
  bool ignore_infrared = false;

  /* Black subtracted before channel mixing.  */
  rgbdata mix_dark = {0, 0, 0};
  /* Parameters used to turn RGB data to grayscale (fake infrared channel):
     mix_red, green and blue are relative weights.  */
  luminosity_t mix_red = 0.3, mix_green = 0.1, mix_blue = 1;

  /* Sharpening parameters.  */
  sharpen_parameters sharpen;

  /***** Tile Adjustment (used to adjust parameters of individual tiles) *****/

  /* Adjustment parameters for a single tile.  */
  struct tile_adjustment
  {
    /* Multiplicative correction to stitch-project global scan_exposure.  */
    luminosity_t exposure = 1;
    /* Additive correction to stitch-project dark point.  */
    luminosity_t dark_point = 0;
    /* Scanner blur usually differs for every capture.  */
    std::shared_ptr <scanner_blur_correction_parameters> scanner_blur_correction = nullptr;
    /* If true tile is rendered, if false tile is not rendered.  */
    bool enabled = true;
    /* Coordinates of the tile in stitch project (used to check that tile
       adjustments match stitch project dimensions).  */
    unsigned char x = 0, y = 0;

    /* Default constructor.  */
    constexpr tile_adjustment () = default;

    /* Return true if THIS and OTHER are equal.  */
    pure_attr bool
    operator== (const tile_adjustment &other) const
    {
      return enabled == other.enabled && dark_point == other.dark_point
             && exposure == other.exposure
             && scanner_blur_correction == other.scanner_blur_correction;
    }

    /* Return true if THIS and OTHER are not equal.  */
    pure_attr bool
    operator!= (const tile_adjustment &other) const
    {
      return !(*this == other);
    }

    /* Apply tile adjustment to render parameters P.  */
    void
    apply (render_parameters *p) const
    {
      p->dark_point = dark_point + exposure * p->dark_point;
      p->scan_exposure *= exposure;
      if (scanner_blur_correction)
        p->scanner_blur_correction = scanner_blur_correction;
    }
  };

  /* Dimensions of tile adjustments vector.  */
  int tile_adjustments_width = 0, tile_adjustments_height = 0;
  /* Vector of tile adjustments.  */
  std::vector<tile_adjustment> tile_adjustments;

  /***** Patch density parameters  *****/

  /* Parameters for simulating contact copy on positive emulsion.  */
  contact_copy_parameters contact_copy;

  /* The following is used by interpolated rendering only.  */

  /* Quality used when collecting data for demosaicing.  */
  enum collection_quality_t
  {
    fast_collection,
    simple_screen_collection,
    simulated_screen_collection,
    max_collection_quality
  };
  /* Quality used when collecting data for demosaicing.  */
  collection_quality_t collection_quality = simple_screen_collection;
  DLL_PUBLIC static const property_t collection_quality_names[(int)max_collection_quality];

  /* Demosaicing algorithms for screen rendering.  */
  enum screen_demosaic_t
  {
    default_demosaic,
    nearest_demosaic,
    linear_demosaic,
    bicubic_demosaic,
    hamilton_adams_demosaic,
    ahd_demosaic,
    amaze_demosaic,
    rcd_demosaic,
    lmmse_demosaic,
    generic_demosaic,
    max_screen_demosaic
  };
  /* Quality used when collecting data for demosaicing.  */
  screen_demosaic_t screen_demosaic = default_demosaic;

  /* Scaling algorithms for demosaiced data.  */
  enum demosaiced_scaling_t
  {
    default_scaling,
    nearest_scaling,
    bspline_scaling,
    linear_scaling,
    bicubic_scaling,
    lanczos3_scaling,
    max_demosaiced_scaling
  };
  /* Quality used when collecting data for demosaicing.  */
  demosaiced_scaling_t demosaiced_scaling = default_scaling;

  DLL_PUBLIC static const property_t demosaiced_scaling_names[(int)max_demosaiced_scaling];
  DLL_PUBLIC static const property_t screen_demosaic_names[(int)max_screen_demosaic];

  /* Radius (in image pixels) the screen should be blurred.  */
  coord_t screen_blur_radius = 0.5;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold = 0.2;

  /* Width of strips used to print the screen.
     This is relative portion in range 0..1.
     0 will give default values.  
     
     It only has effect for Dufay type screens where
     all values in range 0...1 are meaningful since the
     two strips are printed in angle.

     For screens with vertical strips this is only meaningful
     if the sum of two is strictly less than 1, so there is 
     space for the last strip.  */
  coord_t red_strip_width = 0, green_strip_width = 0;

  /***** Scanner profile *****/

  /* Matrix profile of scanner.
     If values are (0,0,0) then data obtained from image_data will be used.
     It is only valid for RAW images where libraw understand the camera matrix.
   */
  xyz scanner_red = {0, 0, 0};
  xyz scanner_green = {0, 0, 0};
  xyz scanner_blue = {0, 0, 0};

  /***** Process profile *****/

  /* Profile used to convert RGB data of scanner to RGB data of the color
     process. Dark is the dark point of scanner (which is subtracted first).
     Red is scanner's response to red filter etc.  */
  rgbdata profiled_dark = {0, 0, 0};
  rgbdata profiled_red = {1, 0, 0};
  rgbdata profiled_green = {0, 1, 0};
  rgbdata profiled_blue = {0, 0, 1};

  /***** Output Adjustment *****/

  /* White balance adjustment in dye coordinates.  */
  rgbdata white_balance = {1, 1, 1};
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of
     sharpness). Only positive values makes sense; meaningful range is approx
     0.1 to 10.  */
  luminosity_t presaturation = 1;

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
    color_model_autochrome_lavedrine1,
    color_model_autochrome_lavedrine2,
    color_model_autochrome_lavedrine3,
    color_model_autochrome_lavedrine4,
    color_model_autochrome_lavedrine5,
    color_model_autochrome_lavedrine6,
    color_model_autochrome_lavedrine7,
    color_model_autochrome_lavedrine8,
    color_model_autochrome_lavedrine9,
    color_model_autochrome_lavedrine10,
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
  /* Properties of a color model.  */
  struct color_model_property
  {
    /* Identifier.  */
    const char *name;
    /* Human readable name.  */
    const char *pretty_name;
    /* Description.  */
    const char *description;
    /* Flags.  */
    unsigned int flags;
  };
  static const int SUPPORTS_AGING = 1;
  static const int SPECTRA_BASED = 2; // For informational purposes
  DLL_PUBLIC static const color_model_property color_model_properties[(int)color_model_max];
  /* Selected color model.  */
  color_model_t color_model = color_model_wratten_25_58_47_spectra;
  /* Aging simulation (0 new dyes, 1 aged dyes).
     Only effective for color models that support aging simulation.  */
  rgbdata age = {1, 1, 1};
  /* Density (concentration) adjustment of dyes in color screen.  */
  rgbdata dye_density = {1, 1, 1};
  /* Temperature in K of daylight in photograph.  */
  static const int temperature_min = 2500;
  static const int temperature_max = 25000;
  /* Temperature of photograph.  */
  luminosity_t temperature = 5000;
  /* Temperature in K of backlight when viewing the slide.  */
  luminosity_t backlight_temperature = 5000;
  /* Whitepoint observer's eye is adapted to.  */
  xy_t observer_whitepoint = d50_white;
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
  enum dye_balance_t dye_balance = dye_balance_bradford;

  /* Saturation increase.  */
  luminosity_t saturation = 1;
  /* Brightness adjustments.  */
  luminosity_t brightness = 1;
  /* Tone curve used for output.  */
  enum tone_curve::tone_curves output_tone_curve = tone_curve::tone_curve_linear;
  /* Control points for output tone curve.  */
  std::vector<point_t> output_tone_curve_control_points = tone_curve::default_control_points ();

  /***** Output Profile *****/

  /* Output profile type.  */
  enum output_profile_t
  {
    output_profile_sRGB,
    output_profile_xyz,
    output_profile_original,
    output_profile_max
  };

  /* Selected output profile.  */
  output_profile_t output_profile = output_profile_sRGB;
  DLL_PUBLIC static const char *output_profile_names[(int)output_profile_max];

  /* Output gamma.  -1 means sRGB transfer curve.  */
  luminosity_t output_gamma = -1;

  /* If true, warn about out of gamut colors.  */
  bool gamut_warning = false;

  /* Default constructor.  */
  render_parameters () = default;

  /* Accessors.  */

  /* Return matrix transforming RGB values of scanner into XYZ or sRGB.
     IMG is the image being rendered.
     NORMALIZED_PATCHES is true if color information is collected on
     pre-interpolated screen.
     PATCH_PROPORTIONS describes portions of individual patches.
     TARGET_WHITEPOINT is the whitepoint of target color space.  */
  color_matrix get_rgb_to_xyz_matrix (const image_data *img,
                                      bool normalized_patches,
                                      rgbdata patch_proportions,
                                      xyz target_whitepoint = d50_white);
  /* Return matrix adjusting RGB values read from the scan.
     NORMALIZED_PATCHES is true if color information is collected on
     pre-interpolated screen.
     PATCH_PROPORTIONS describes portions of individual patches.  */
  color_matrix get_rgb_adjustment_matrix (bool normalized_patches,
                                          rgbdata patch_proportions);
  /* Return ICC profile for given rendering parameters.
     BUF is pointer to buffer where profile will be stored.
     IMG is the image being rendered.
     NORMALIZED_DYES is true if dyes are normalized.  */
  size_t get_icc_profile (void **buf, image_data *img, bool normalized_dyes);
  /* Return tile adjustment for given tile (X, Y) in STITCH project.  */
  DLL_PUBLIC const tile_adjustment &get_tile_adjustment (const stitch_project *stitch, int x, int y) const;
  /* Return tile adjustment reference for given tile (X, Y) in STITCH project.  */
  tile_adjustment &get_tile_adjustment_ref (const stitch_project *stitch, int x, int y);
  /* Return tile adjustment for given tile (X, Y).  */
  DLL_PUBLIC tile_adjustment &get_tile_adjustment (int x, int y);
  DLL_PUBLIC const tile_adjustment &get_tile_adjustment (int x, int y) const;

  /* Return true if THIS and OTHER are equal.  */
  pure_attr bool
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
	   && gamma == other.gamma 
	   && contact_copy == other.contact_copy
           && output_gamma == other.output_gamma
	   && scan_rotation == other.scan_rotation
	   && scan_mirror == other.scan_mirror
	   && scan_crop == other.scan_crop
	   && image_area == other.image_area
	   && sharpen.equal_p (other.sharpen)
           && presaturation == other.presaturation
	   && gamut_warning == other.gamut_warning
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
           && screen_blur_radius == other.screen_blur_radius
           && dye_balance == other.dye_balance
	   && collection_quality == other.collection_quality
	   && screen_demosaic == other.screen_demosaic
	   && demosaiced_scaling == other.demosaiced_scaling
           && scanner_blur_correction == other.scanner_blur_correction
           && backlight_correction == other.backlight_correction
           && backlight_correction_black == other.backlight_correction_black
	   && observer_whitepoint == other.observer_whitepoint
	   && output_tone_curve == other.output_tone_curve
	   && output_tone_curve_control_points == other.output_tone_curve_control_points
	   && white_balance == other.white_balance;
  }
  /* Return true if THIS and OTHER are not equal.  */
  pure_attr bool
  operator!= (render_parameters &other) const
  {
    return !(*this == other);
  }
  /* Set exposure and dark_point for a given range of values
     in input scan.  Used to interpret old gray_range parameter
     and can be removed eventually.
     GRAY_MIN and GRAY_MAX are values in the scan.
     MAXVAL is maximal value possible in scan.  */
  DLL_PUBLIC void set_gray_range (int gray_min, int gray_max, int maxval);
  /* Get exposure and dark_point for a given range of values
     in input scan.  Used to interpret old gray_range parameter
     and can be removed eventually.
     MIN and MAX are returned values in the scan.
     MAXVAL is maximal value possible in scan.  */
  DLL_PUBLIC void get_gray_range (int *min, int *max, int maxval);
  /* Set dimensions of tile adjustments vector to W x H.  */
  DLL_PUBLIC void set_tile_adjustments_dimensions (int w, int h);
  /* Compute mixing weights for given PATCH_PROPORTIONS.  */
  DLL_PUBLIC void compute_mix_weights (rgbdata patch_proportions);
  /* Choose best color model for given screen TYPE.  */
  DLL_PUBLIC bool auto_color_model (enum scr_type type);
  /* Automatically choose dark and brightness point for image IMG.
     PAR are screen registration parameters.
     AREA is area to analyze.
     PROGRESS is progress info.
     DARK_CUT and LIGHT_CUT are percentages of pixels to be cut.  */
  DLL_PUBLIC bool auto_dark_brightness (image_data &img,
                                        scr_to_img_parameters &par,
                                        int_image_area area,
                                        progress_info *progress = NULL,
                                        luminosity_t dark_cut = 0.01,
                                        luminosity_t light_cut = 0.001);
  /* Automatically choose mixing weights for image IMG.
     PARAM are screen registration parameters.
     AREA is area to analyze.
     PROGRESS is progress info.  */
  nodiscard_attr DLL_PUBLIC bool auto_mix_weights (image_data &img,
                                    scr_to_img_parameters &param,
                                    int_image_area area,
                                    progress_info *progress);
  /* Automatically choose mixing dark point for image IMG.
     PARAM are screen registration parameters.
     AREA is area to analyze.
     PROGRESS is progress info.  */
  nodiscard_attr DLL_PUBLIC bool auto_mix_dark (image_data &img,
                                 scr_to_img_parameters &param,
                                 int_image_area area,
                                 progress_info *progress);
  /* Automatically choose mixing weights using infrared channel for image IMG.
     PARAM are screen registration parameters.
     AREA is area to analyze.
     PROGRESS is progress info.  */
  nodiscard_attr DLL_PUBLIC bool auto_mix_weights_using_ir (image_data &img,
                                             scr_to_img_parameters &param,
                                             int_image_area area,
                                             progress_info *progress);
  /* Automatically white balance image IMG.
     PAR are screen registration parameters.
     AREA is area to analyze.
     PROGRESS is progress info.
     DARK_CUT and LIGHT_CUT are percentages of pixels to be cut.  */
  DLL_PUBLIC bool auto_white_balance (image_data &img,
                                      scr_to_img_parameters &par,
                                      int_image_area area,
                                      progress_info *progress = NULL,
                                      luminosity_t dark_cut = 0.01,
                                      luminosity_t light_cut = 0.001);
  /* Transmission data for individual color channels.  */
  struct transmission_data
  {
    /* Minimal frequency (wavelength) in nm.  */
    luminosity_t min_freq;
    /* Maximal frequency (wavelength) in nm.  */
    luminosity_t max_freq;
    /* Transmission of red patches.  */
    std::vector<luminosity_t> red;
    /* Transmission of green patches.  */
    std::vector<luminosity_t> green;
    /* Transmission of blue patches.  */
    std::vector<luminosity_t> blue;
    /* Spectrum of backlight.  */
    std::vector<luminosity_t> backlight;
  };
  /* Return transmission data in DATA.  Return false if not available.  */
  nodiscard_attr DLL_PUBLIC bool get_transmission_data (transmission_data &data) const;

  /* Initialize render parameters for showing original scan.
     In this case we do not want to apply color models etc.
     RPARAM are the original parameters.
     COLOR is true if color scan should be shown.
     PROFILED is true if scanner profile should be applied.  */
  void original_render_from (render_parameters &rparam, bool color,
                             bool profiled);

  /* Adjust RPARAM for given rendering type parameters RTPARAM.  */
  void adjust_for (render_type_parameters &rtparam, render_parameters &rparam);
  /* Return matrix transforming scanner RGB values to profiled RGB values.
     PATCH_PROPORTIONS describes portions of individual patches.  */
  color_matrix get_profile_matrix (rgbdata patch_proportions);
  /* Return matrix transforming process dyes to XYZ or sRGB.
     SPECTRUM_BASED is set to true if result is based on spectra.
     OPTIMIZED is set to true if result is optimized camera matrix.
     IMG is the image being rendered.
     TRANSMISSION_DATA is optionally filled with transmittance data.  */
  color_matrix get_dyes_matrix (bool *spectrum_based, bool *optimized,
                                const image_data *img, transmission_data *transmission_data = NULL) const;

  /* Return crop of the scan in image coordinates.
     IMG_WIDTH and IMG_HEIGHT are dimensions of the image.  */
  pure_attr int_image_area
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
  /* Return crop of the scan in image coordinates.
     IMG_WIDTH and IMG_HEIGHT are dimensions of the image.  */
  pure_attr int_image_area
  get_image_area (int img_width, int img_height) const
  {
    int_image_area img (0, 0, img_width, img_height);
    if (!image_area.set)
      return get_scan_crop (img_width, img_height);
    int_image_area intersection = image_area.intersect (img);
    if (intersection.empty_p ())
      return img;
    return intersection;
  }

  /* Return effective capture type for CAPTURE_TYPE and SCAN.  */
  pure_attr static enum capture_type
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
  /* Return effective capture type for this and SCAN.  */
  pure_attr enum capture_type
  get_capture_type (image_data *scan)
  {
    return get_capture_type (capture_type, scan);
  }

  /* Return true if profile is set.  */
  pure_attr bool has_correction_profile () const
  {
    rgbdata dark = {0,0,0};
    rgbdata red = {1,0,0};
    rgbdata green = {0,1,0};
    rgbdata blue = {0,0,1};
    return profiled_dark != dark
	   || profiled_red != red
	   || profiled_green != green
	   || profiled_blue != blue;
  }

  /* Gamut of a color model.  */
  struct gamut
  {
    /* Chromaticity of red primary.  */
    xy_t red;
    /* Chromaticity of green primary.  */
    xy_t green;
    /* Chromaticity of blue primary.  */
    xy_t blue;
    /* Chromaticity of whitepoint.  */
    xy_t whitepoint;
  };
  /* Return gamut for given color screen TYPE.  CORRECTED is true if
     profile should be applied.  */
  pure_attr DLL_PUBLIC gamut get_gamut (bool corrected, scr_type type) const;

private:
  /* If true, enable debugging checks.  */
  static const bool debug = colorscreen_checking;
  /* Return matrix transforming process dyes to XYZ or sRGB taking into account
     dye balance.
     IMG is the image being rendered.
     NORMALIZED_PATCHES is true if color information is collected on
     pre-interpolated screen.
     PATCH_PROPORTIONS describes portions of individual patches.
     TARGET_WHITEPOINT is the whitepoint of target color space.  */
  color_matrix get_balanced_dyes_matrix (const image_data *img,
                                         bool normalized_patches,
                                         rgbdata patch_proportions,
                                         xyz target_whitepoint = d50_white) const;
};
}
#endif
