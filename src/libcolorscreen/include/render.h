#ifndef RENDER_H
#define RENDER_H
#include <math.h>
#include <assert.h>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "base.h"
#include "imagedata.h"
#include "color.h"
#include "progress-info.h"
#include "sensitivity.h"
#include "backlight-correction.h"
#include "tone-curve.h"

enum render_type_t
{
  render_type_original,
  render_type_interpolated_original,
  render_type_profiled_original,
  render_type_interpolated_profiled_original,
  render_type_interpolated_diff,
  render_type_preview_grid,
  render_type_realistic,
  render_type_interpolated,
  render_type_predictive,
  render_type_combined,
  render_type_fast,
  render_type_adjusted_color,
  render_type_first_scr_detect = render_type_adjusted_color,
  render_type_normalized_color,
  render_type_pixel_colors,
  render_type_realistic_scr,
  render_type_scr_nearest,
  render_type_scr_nearest_scaled,
  render_type_scr_relax,
  render_type_max
};

struct render_type_property
{
  const char *name;
  int flags;
  enum flag
  {
    NEEDS_SCR_TO_IMG = 1,
    NEEDS_RGB = 2,
    NEEDS_SCR_DETECT = 6, /* scr detect needs RGB.  */
    OUTPUTS_SCAN_PROFILE = 8,
    OUTPUTS_PROCESS_PROFILE = 16,
    OUTPUTS_SRGB_PROFILE = 32,
    SUPPORTS_IR_RGB_SWITCH = 64,
    SCAN_RESOLUTION = 128,
    SCREEN_RESOLUTION = 256,
    PATCH_RESOLUTION = 512,
    RESET_BRIGHTNESS_ETC = 1024,
  };
};

namespace {
static const constexpr render_type_property render_type_properties[render_type_max] =
{
   {"original", render_type_property::OUTPUTS_SCAN_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::SCAN_RESOLUTION},
   {"interpolated-original", render_type_property::OUTPUTS_SCAN_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"profiled-original", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::SCAN_RESOLUTION},
   {"interpolated-profiled-original", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"interpolated-diff", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::NEEDS_RGB | render_type_property::PATCH_RESOLUTION},
   {"preview-grid", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"realistic", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::SUPPORTS_IR_RGB_SWITCH | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"interpolated", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::PATCH_RESOLUTION},
   {"interpolated-predictive", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"interpolated-combined", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCAN_RESOLUTION},
   {"fast", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_TO_IMG | render_type_property::SCREEN_RESOLUTION},
   {"detected-adjusted-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCAN_RESOLUTION},
   {"detected-normalized-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION | render_type_property::RESET_BRIGHTNESS_ETC},
   {"detected-screen-color", render_type_property::OUTPUTS_SRGB_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION | render_type_property::RESET_BRIGHTNESS_ETC},
   {"detected-realistic", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION},
   {"detected-interpolated", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION},
   {"detected-interpolated-scaled", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION},
   {"detected-relaxation-scaled", render_type_property::OUTPUTS_PROCESS_PROFILE | render_type_property::NEEDS_SCR_DETECT | render_type_property::SCREEN_RESOLUTION},
};
}


//static const enum render_type_t first_scr_detect = render_type_adjusted_color;
struct render_type_parameters
{
  enum render_type_t type;
  bool color;
  bool antialias;
  render_type_parameters ()
  : type (render_type_original), color (true), antialias (true)
  { }
};

/* Parameters of rendering algorithms.  */
struct DLL_PUBLIC render_parameters
{
  render_parameters()
  : gamma (2.2),  film_gamma (1), target_film_gamma (1),
    output_gamma (-1), sharpen_radius (0), sharpen_amount (0), presaturation (1), saturation (1),
    brightness (1), collection_threshold (0.8), white_balance ({1, 1, 1}),
    mix_dark (0, 0, 0),
    mix_red (0.3), mix_green (0.1), mix_blue (1), temperature (5000), backlight_temperature (5000), observer_whitepoint (/*srgb_white*/d50_white),
    age(1),
    dye_balance (dye_balance_neutral),
    screen_blur_radius (0.5),
    color_model (color_model_none),
    ignore_infrared (false),
    profiled_dark (0, 0, 0),
    profiled_red (1, 0, 0),
    profiled_green (0, 1, 0),
    profiled_blue (0, 0, 1),
    scanner_red (0, 0, 0),
    scanner_green (0, 0, 0),
    scanner_blue (0, 0, 0),
    output_profile (output_profile_sRGB), output_tone_curve (tone_curve::tone_curve_linear), dark_point (0), scan_exposure (1),
    dufay_red_strip_width (0), dufay_green_strip_width (0),
    film_characteristics_curve (&film_sensitivity::linear_sensitivity), output_curve (NULL),
    backlight_correction (NULL), backlight_correction_black (0), invert (false),
    restore_original_luminosity (true), precise (true), tile_adjustments_width (0), tile_adjustments_height (0), tile_adjustments ()
  {
  }
  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).
     Only positive values makes sense; meaningful range is approx 0.01 to 10.  */
  luminosity_t gamma;
  luminosity_t film_gamma, target_film_gamma;
  /* Output gamma.  -1 means sRGB transfer curve.  */
  luminosity_t output_gamma;
  /* Radious (in pixels) and amount for unsharp-mask filter.  */
  luminosity_t sharpen_radius, sharpen_amount;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of sharpness).
     Only positive values makes sense; meaningful range is approx 0.1 to 10.  */
  luminosity_t presaturation;
  /* Saturation increase.  */
  luminosity_t saturation;
  /* Brightness adjustments.  */
  luminosity_t brightness;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;
  /* White balance adjustment in dye coordinates.  */
  rgbdata white_balance;
  /* Black subtracted before channel mixing.  */
  rgbdata mix_dark;
  /* Parameters used to turn RGB data to grayscale:
     mix_red,green and blue are relative weights.  */
  luminosity_t mix_red, mix_green, mix_blue;
  /* Temperature in K of daylight in photograph.  */
  luminosity_t temperature;
  /* Temperature in K of backlight when viewing the slide.  */
  luminosity_t backlight_temperature;
  /* Whitepoint observer's eye is adapted to.  */
  xy_t observer_whitepoint;
  static const int temperature_min = 2500;
  static const int temperature_max = 25000;

  /* Aging simulation (0 new dyes, 1 aged dyes).  */
  luminosity_t age;
  enum dye_balance_t
  {
    dye_balance_none,
    dye_balance_brightness,
    dye_balance_bradford,
    dye_balance_neutral,
    dye_balance_whitepoint,
    dye_balance_max
  };
  DLL_PUBLIC static const char *dye_balance_names [(int)dye_balance_max];
  /* How to balance dye colors.  */
  enum dye_balance_t dye_balance;
  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  enum color_model_t
    {
      color_model_none,
      color_model_scan,
      color_model_red,
      color_model_green,
      color_model_blue,
      color_model_max_separation,
      color_model_paget,
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
      color_model_autochrome,
      color_model_autochrome2,
      color_model_kodachrome25,
      color_model_max
    };
  DLL_PUBLIC static const char *color_model_names [(int)color_model_max];
  /* If true apply color model of Finlay taking plate.  */
  enum color_model_t color_model;
  enum output_profile_t
    {
      output_profile_sRGB,
      output_profile_xyz,
      output_profile_original,
      output_profile_max
    };

  /* Ignore infrared channel and produce fake one using RGB data.  */
  bool ignore_infrared;

  /* Profile used to convert RGB data of scanner to RGB data of the color process.
     Dark is the dark point of scanner (which is subtracted first).
     Red is scanner's response to red filter etc.  */
  rgbdata profiled_dark;
  rgbdata profiled_red;
  rgbdata profiled_green;
  rgbdata profiled_blue;

  /* Matrix profile of scanner.
     If values are (0,0,0) then data obtained from image_data will be used.
     It is only valid for RAW images where libraw understand the camera matrix.  */
  xyz scanner_red;
  xyz scanner_green;
  xyz scanner_blue;

  output_profile_t output_profile;
  enum tone_curve::tone_curves output_tone_curve;
  DLL_PUBLIC static const char *output_profile_names [(int)output_profile_max];
  /* After linearizing we apply (val - dark_point) * scan_exposure  */
  luminosity_t dark_point, scan_exposure;

  /* Width of strips used to print Dufaycolor reseau (screen).
     This is relative portion in range 0..1.
     0 will give default values.  */
  coord_t dufay_red_strip_width, dufay_green_strip_width;

  hd_curve *film_characteristics_curve;
  hd_curve *output_curve;
  class backlight_correction_parameters *backlight_correction;
  luminosity_t backlight_correction_black;

  /* True if negatuve should be inverted to positive.  */
  bool invert;

  /* Use characteristics curves to restore original luminosity.  */
  bool restore_original_luminosity;

  /* The following is used by interpolated rendering only.  */
  /* If true use precise data collection.  */
  bool precise;

  struct tile_adjustment
  {
    /* Multiplicative correction to stitch-project global scan_exposure.  */
    luminosity_t exposure;
    /* Additive correction to stitch-project dark point.  */
    luminosity_t dark_point;
    bool enabled;
    unsigned char x, y;
    constexpr tile_adjustment()
    : exposure (1), dark_point (0), enabled (true), x(0), y(0)
    {}
    bool operator== (tile_adjustment &other) const
    {
      return enabled == other.enabled
	     && dark_point == other.dark_point
	     && exposure == other.exposure;
    }
    bool operator!= (tile_adjustment &other) const
    {
      return !(*this == other);
    }
    void apply (render_parameters *p) const
    {
      p->dark_point = dark_point + exposure * p->dark_point;
      p->scan_exposure *= exposure;
    }
  };

  int tile_adjustments_width, tile_adjustments_height;
  std::vector<tile_adjustment> tile_adjustments;

  color_matrix get_rgb_to_xyz_matrix (image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint = d50_white);
  color_matrix get_rgb_adjustment_matrix (bool normalized_patches, rgbdata patch_proportions);
  size_t get_icc_profile (void **buf, image_data *img, bool normalized_dyes);
  const tile_adjustment& get_tile_adjustment (stitch_project *stitch, int x, int y) const;
  tile_adjustment& get_tile_adjustment_ref (stitch_project *stitch, int x, int y);
  tile_adjustment& get_tile_adjustment (int x, int y);

  bool operator== (render_parameters &other) const
  {
    if (tile_adjustments.size () != other.tile_adjustments.size ()
	|| tile_adjustments_width != other.tile_adjustments_width
	|| tile_adjustments_height != other.tile_adjustments_height)
      return false;
    for (unsigned int i = 0; i < tile_adjustments.size (); i++)
      if (tile_adjustments[i] != other.tile_adjustments[i])
        return false;
    return gamma == other.gamma
	   && film_gamma == other.film_gamma
	   && target_film_gamma == other.target_film_gamma
	   && output_gamma == other.output_gamma
	   && sharpen_radius == other.sharpen_radius
	   && sharpen_amount == other.sharpen_amount
	   && presaturation == other.presaturation
	   && saturation == other.saturation
	   && brightness == other.brightness
	   && collection_threshold == other.collection_threshold
	   && mix_red == other.mix_red
	   && mix_green == other.mix_green
	   && mix_blue == other.mix_blue
	   && color_model == other.color_model
	   && ignore_infrared == other.ignore_infrared
	   && profiled_dark == other.profiled_dark
	   && profiled_red == other.profiled_red
	   && profiled_green == other.profiled_green
	   && profiled_blue == other.profiled_blue
	   && scanner_red == other.scanner_red
	   && scanner_green == other.scanner_green
	   && scanner_blue == other.scanner_blue
	   && age == other.age
	   && backlight_temperature == backlight_temperature
	   && dark_point == other.dark_point
	   && scan_exposure == other.scan_exposure
	   && dufay_red_strip_width == other.dufay_red_strip_width
	   && dufay_green_strip_width == other.dufay_green_strip_width
	   && invert == other.invert
	   && screen_blur_radius == other.screen_blur_radius
    	   && dye_balance == other.dye_balance
	   && precise == other.precise
 	   && film_characteristics_curve == other.film_characteristics_curve
	   && restore_original_luminosity == other.restore_original_luminosity
 	   && output_curve == other.output_curve
	   && backlight_correction == other.backlight_correction
	   && backlight_correction_black == other.backlight_correction_black;
  }
  bool operator!= (render_parameters &other) const
  {
    return !(*this == other);
  }
  /* Set invert, exposure and dark_point for a given range of values
     in input scan.  Used to interpret old gray_range parameter
     and can be removed eventually.  */
  void set_gray_range (int gray_min, int gray_max, int maxval)
  {
    if (gray_min < gray_max)
      {
	luminosity_t min2 = apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
	luminosity_t max2 = apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
	scan_exposure = 1 / (max2 - min2);
	dark_point = min2;
	invert = false;
      }
    else if (gray_min > gray_max)
      {
	luminosity_t min2 = 1 / apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
	luminosity_t max2 = 1 / apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
	scan_exposure = 1 / (max2 - min2);
	dark_point = min2;
	invert = true;
      }
  }
  void get_gray_range (int *min, int *max, int maxval)
  {
    if (!invert)
      {
       *min = invert_gamma (dark_point, gamma) * maxval - 0.5;
       *max = invert_gamma (dark_point + 1 / scan_exposure, gamma) * maxval - 0.5;
      }
    else
      {
       *min = (invert_gamma (1/dark_point, gamma) * maxval) - 0.5;
       *max = (invert_gamma (1/(((dark_point + 1 / scan_exposure))), gamma) * maxval) - 0.5;
      }
  }
  void set_tile_adjustments_dimensions (int w, int h);
  /* Initialize render parameters for showing original scan.
     In this case we do not want to apply color models etc.  */
  void
  original_render_from (render_parameters &rparam, bool color, bool profiled)
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
    color_model = color ? (profiled ? rparam.color_model : render_parameters::color_model_scan) : render_parameters::color_model_none;
    output_tone_curve = rparam.output_tone_curve;
    tile_adjustments = rparam.tile_adjustments;
    tile_adjustments_width = rparam.tile_adjustments_width;
    tile_adjustments_height = rparam.tile_adjustments_height;
    output_profile = rparam.output_profile;
    if (color)
      white_balance = rparam.white_balance;
    else
      {
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

  void
  adjust_for (render_type_parameters &rtparam, render_parameters &rparam)
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
  color_matrix get_profile_matrix (rgbdata patch_proportions)
  {
    color_matrix subtract_dark (1, 0, 0, -profiled_dark.red,
				0, 1, 0, -profiled_dark.green,
				0, 0, 1, -profiled_dark.blue,
				0, 0, 0, 1);
    color_matrix process_colors (profiled_red.red * patch_proportions.red,   profiled_green.red * patch_proportions.green,   profiled_blue.red * patch_proportions.blue, 0,
				 profiled_red.green * patch_proportions.red, profiled_green.green * patch_proportions.green, profiled_blue.green * patch_proportions.blue, 0,
				 profiled_red.blue * patch_proportions.red,  profiled_green.blue * patch_proportions.green,  profiled_blue.blue * patch_proportions.blue, 0,
				 0, 0, 0, 1);
    color_matrix ret = process_colors.invert ();
    ret = ret * subtract_dark;
    return ret;
  }
  void
  compute_mix_weights (rgbdata patch_proportions)
  {
    rgbdata sprofiled_red = profiled_red /*/ patch_proportions.red*/;
    rgbdata sprofiled_green = profiled_green /*/ patch_proportions.green*/;
    rgbdata sprofiled_blue= profiled_blue /*/ patch_proportions.blue*/;
    bool verbose = true;
    color_matrix process_colors (sprofiled_red.red,   sprofiled_green.red,   sprofiled_blue.red, 0,
				 sprofiled_red.green, sprofiled_green.green, sprofiled_blue.green, 0,
				 sprofiled_red.blue,  sprofiled_green.blue,  sprofiled_blue.blue, 0,
				 0, 0, 0, 1);
    process_colors.transpose ();
    mix_dark = profiled_dark;
    process_colors.invert ().apply_to_rgb (3 * patch_proportions.red / white_balance.red, 3 * patch_proportions.green / white_balance.green, 3 * patch_proportions.blue / white_balance.blue, &mix_red, &mix_green, &mix_blue);
    white_balance = {1, 1, 1};
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
	printf ("%f %f\n", profiled_red.red * mix_red + profiled_red.green * mix_green + profiled_red.blue * mix_blue, patch_proportions.red);
	printf ("%f %f\n", profiled_green.red * mix_red + profiled_green.green * mix_green + profiled_green.blue * mix_blue, patch_proportions.green);
	printf ("%f %f\n", profiled_blue.red * mix_red + profiled_blue.green * mix_green + profiled_blue.blue * mix_blue, patch_proportions.blue);
      }
  }
private:
  static const bool debug = false;
  color_matrix get_dyes_matrix (bool *spectrum_based, bool *optimized, image_data *img);
  color_matrix get_balanced_dyes_matrix (image_data *img, bool normalized_patches, rgbdata patch_proportions, xyz target_whitepoint = d50_white);
};

/* Helper for downscaling template for color rendering
   data += lum * scale.  */
inline void
account_rgb_pixel (rgbdata *data, rgbdata lum, luminosity_t scale)
{
  data->red += lum.red * scale;
  data->green += lum.green * scale;
  data->blue += lum.blue * scale;
}

/* Helper for downscaling template for grayscale rendering
   data += lum * scale.  */
inline void
account_pixel (luminosity_t *data, luminosity_t lum, luminosity_t scale)
{
  *data += lum * scale;
}

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class DLL_PUBLIC render
{
public:
  render (image_data &img, render_parameters &rparam, int dstmaxval)
  : m_img (img), m_params (rparam), m_gray_data_id (img.id), m_sharpened_data (NULL), m_sharpened_data_holder (NULL), m_maxval (img.data ? img.maxval : 65535), m_dst_maxval (dstmaxval),
    m_rgb_lookup_table (NULL), m_out_lookup_table (NULL), m_spectrum_dyes_to_xyz (NULL), m_backlight_correction (NULL), m_tone_curve (NULL)
  {
    if (m_params.invert)
      {
	static synthetic_hd_curve c (10, safe_output_curve_params);
	m_params.output_curve = &c;
      }
    else
      m_params.output_curve = NULL;
  }
  ~render ();
  inline luminosity_t get_img_pixel (coord_t x, coord_t y);
  inline luminosity_t get_unadjusted_img_pixel (coord_t x, coord_t y);
  inline void get_img_rgb_pixel (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  inline void get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  inline luminosity_t sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2);
  inline luminosity_t fast_get_img_pixel (int x, int y);
    
  static const int num_color_models = render_parameters::color_model_max;
  static luminosity_t *get_lookup_table (luminosity_t gamma, int maxval);
  static void release_lookup_table (luminosity_t *);
  inline void set_color (luminosity_t, luminosity_t, luminosity_t, int *, int *, int *);
  inline void set_linear_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *);
  inline void set_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *);
  inline luminosity_t get_data (int x, int y);
  inline luminosity_t get_unadjusted_data (int x, int y);
  inline luminosity_t adjust_luminosity_ir (luminosity_t);
  inline luminosity_t get_data_red (int x, int y);
  inline luminosity_t get_data_green (int x, int y);
  inline luminosity_t get_data_blue (int x, int y);
  inline luminosity_t get_linearized_data_red (int x, int y);
  inline luminosity_t get_linearized_data_green (int x, int y);
  inline luminosity_t get_linearized_data_blue (int x, int y);
  bool precompute_all (bool grayscale_needed, bool normalized_patches, rgbdata patch_proportions, progress_info *progress);
  inline rgbdata
  get_linearized_rgb_pixel (int x, int y)
  {
    rgbdata d = {m_rgb_lookup_table [m_img.rgbdata[y][x].r],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].g],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].b]};
    return d;
  }
  inline rgbdata
  get_unadjusted_rgb_pixel (int x, int y)
  {
    rgbdata d = get_linearized_rgb_pixel (x, y);
    if (m_backlight_correction)
      {
	d.red = m_backlight_correction->apply (d.red, x, y, backlight_correction_parameters::red, true);
	d.green = m_backlight_correction->apply (d.green, x, y, backlight_correction_parameters::green, true);
	d.blue = m_backlight_correction->apply (d.blue, x, y, backlight_correction_parameters::blue, true);
	/* TODO do inversion and film curves if requested.  */
      }
    return d;
  }
  inline rgbdata
  adjust_rgb (rgbdata d)
  {
    d.red = (d.red - m_params.dark_point) * m_params.scan_exposure;
    d.green = (d.green - m_params.dark_point) * m_params.scan_exposure;
    d.blue = (d.blue - m_params.dark_point) * m_params.scan_exposure;
    return d;
  }
  inline rgbdata
  get_rgb_pixel (int x, int y)
  {
    return adjust_rgb (get_unadjusted_rgb_pixel (x, y));
  }
  /* PATCH_PORTIONS describes how much percent of screen is occupied by red, green and blue
     patches respectively. It should have sum at most 1.
     
     If NORMALIZED_PATCHES is true, the rgbdata represents patch intensities regardless of their
     size (as in interpolated rendering) and the dye matrix channels needs to be scaled by
     PATCH_PROPORTIONS.  */
  color_matrix get_rgb_to_xyz_matrix (bool normalized_patches, rgbdata patch_proportions)
  {
    return m_params.get_rgb_to_xyz_matrix (&m_img, normalized_patches, patch_proportions);
  }
  void get_gray_data (luminosity_t *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);

protected:
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);


  template<typename T, typename D, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  void process_line (T *data, int *pixelpos, luminosity_t *weights,
		     int xstart, int xend,
		     int width, int height,
		     int py, int yy,
		     bool y0, bool y1,
		     luminosity_t scale, luminosity_t yweight);

  template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
  void process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T val, luminosity_t scale, luminosity_t xweight, luminosity_t yweight);

  template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  bool downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *);

  /* Scanned image.  */
  image_data &m_img;
  /* Rendering parameters.
     Make local copy for performance reasons and also because render-tile releases rparam
     after constructing renderer.  */
  render_parameters m_params;
  /* ID of graydata computed.  */
  uint64_t m_gray_data_id;
  /* Sharpened data we render from.  */
  mem_luminosity_t *m_sharpened_data;
  /* Wrapping class to cause proper destruction.  */
  class sharpened_data *m_sharpened_data_holder;
  /* Maximal value in m_data.  */
  int m_maxval;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input rgb channel values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_rgb_lookup_table;
  /* Translates back to gamma 2.  */
  luminosity_t *m_out_lookup_table;
  /* Color matrix.  For additvie processes it converts process RGB to prophoto RGB.
     For subtractive processes it only applies transformations does in process RGB.  */
  color_matrix m_color_matrix;

  /* For subtractive processes it converts dyes RGB to xyz.  */
  spectrum_dyes_to_xyz *m_spectrum_dyes_to_xyz;

  /* For cubstractive processes it converts xyz to prophoto RGB applying
     corrections, like saturation control.  */
  color_matrix m_color_matrix2;

  backlight_correction *m_backlight_correction;

private:
  static const bool debug = false;
  tone_curve *m_tone_curve;
};

typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;

/* Cubic interpolation helper.  */

static inline luminosity_t const_attr
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}
static inline vec_luminosity_t const_attr
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_unadjusted_data (int x, int y)
{
  /* TODO do inversion and film curves if requested.  */
  return m_sharpened_data [y * m_img.width + x];
}

inline luminosity_t
render::adjust_luminosity_ir (luminosity_t lum)
{
  lum = (lum - m_params.dark_point) * m_params.scan_exposure;
  if (m_params.film_gamma != 1)
    lum = invert_gamma (lum, m_params.film_gamma);
  return lum;
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_data (int x, int y)
{
  return adjust_luminosity_ir (get_unadjusted_data (x, y));
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_linearized_data_red (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].r];
  /* TODO do inversion and film curves if requested.  */
}

inline luminosity_t
render::get_linearized_data_green (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].g];
  /* TODO do inversion and film curves if requested.  */
}
inline luminosity_t
render::get_linearized_data_blue (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].b];
  /* TODO do inversion and film curves if requested.  */
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_data_red (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].r];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::red, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

inline luminosity_t
render::get_data_green (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].g];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::green, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

inline luminosity_t
render::get_data_blue (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].b];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::blue, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

/* Compute color in linear HDR image.  */
inline void
render::set_linear_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
#if 0
  r *= m_params.white_balance.red;
  g *= m_params.white_balance.green;
  b *= m_params.white_balance.blue;
  if (m_spectrum_dyes_to_xyz)
    {
      /* At the moment all conversions are linear.
         Simplify the codegen here.  */
      if (1)
	abort ();
      else
	{
	  if (m_params.presaturation != 1)
	    {
	      presaturation_matrix m (m_params.presaturation);
	      m.apply_to_rgb (r, g, b, &r, &g, &b);
	    }
	  struct xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
	  r = c.x;
	  g = c.y;
	  b = c.z;
	}
    }
#endif
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  if (m_spectrum_dyes_to_xyz)
    {
      if (m_spectrum_dyes_to_xyz->red_characteristic_curve)
	r=m_spectrum_dyes_to_xyz->red_characteristic_curve->apply (r);
      if (m_spectrum_dyes_to_xyz->green_characteristic_curve)
	g=m_spectrum_dyes_to_xyz->green_characteristic_curve->apply (g);
      if (m_spectrum_dyes_to_xyz->blue_characteristic_curve)
	b=m_spectrum_dyes_to_xyz->blue_characteristic_curve->apply (b);
      xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);

      m_color_matrix2.apply_to_rgb (c.x, c.y, c.z, &r, &g, &b);
    }
  if (m_params.output_curve)
    {
      luminosity_t lum = r * rwght + g * gwght + b * bwght;
      luminosity_t lum2;
      lum2 = m_params.output_curve->apply (lum);
      if (lum != lum2)
	{
	  r *= lum2 / lum;
	  g *= lum2 / lum;
	  b *= lum2 / lum;
	}
    }

  /* Apply DNG-style tone curve correction.  */
  if (m_tone_curve)
    {
      rgbdata c = {r,g,b};
      assert (!m_tone_curve->is_linear ());
      c = m_tone_curve->apply_to_rgb (c);
      color_matrix cm;
      pro_photo_rgb_xyz_matrix m1;
      cm = m1 * cm;
      bradford_d50_to_d65_matrix m2;
      cm = m2 * cm;
      xyz_srgb_matrix m;
      cm = m * cm;
      cm.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green, &c.blue);
      *rr = c.red;
      *gg = c.green;
      *bb = c.blue;
      return;
    }

  *rr = r;
  *gg = g;
  *bb = b;
}
inline void
render::set_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
  luminosity_t r1, g1, b1;
  render::set_linear_hdr_color (r, g, b, &r1, &g1, &b1);
  *rr = invert_gamma (r1, m_params.output_gamma);
  *gg = invert_gamma (g1, m_params.output_gamma);
  *bb = invert_gamma (b1, m_params.output_gamma);
}

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval.  */
inline void
render::set_color (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb)
{
  set_linear_hdr_color (r, g, b, &r, &g, &b);
  // Show gammut warnings
  //if ( r < 0 || r > 1 || g < 0 || g >1 || b < 0 || b > 1)
	  //r = g = b = 0.5;
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}

#if 0
/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval
   combining color and luminosity information.  */

inline void
render::set_color_luminosity (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t l, int *rr, int *gg, int *bb)
{
  luminosity_t r1, g1, b1;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  m_color_matrix.apply_to_rgb (l, l, l, &r1, &g1, &b1);
  l = r1 * rwght + g1 * gwght + b1 * bwght;
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  l = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, l));
  luminosity_t gr = (r * rwght + g * gwght + b * bwght);
  if (gr <= 0.00001 || l <= 0.00001)
    r = g = b = l;
  else
    {
      gr = l / gr;
      r *= gr;
      g *= gr;
      b *= gr;
    }
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));

  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}
#endif

/* Determine grayscale value at a given position in the image.  */

inline luminosity_t
render::fast_get_img_pixel (int x, int y)
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline luminosity_t
render::get_unadjusted_img_pixel (coord_t xp, coord_t yp)
{
  luminosity_t val;

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  //int sx = xp, sy = yp;
  //luminosity_t rx = xp - sx, ry = yp - sy;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      vec_luminosity_t v1 = {get_unadjusted_data (sx-1, sy-1), get_unadjusted_data (sx, sy-1), get_unadjusted_data (sx+1, sy-1), get_unadjusted_data (sx+2, sy-1)};
      vec_luminosity_t v2 = {get_unadjusted_data (sx-1, sy-0), get_unadjusted_data (sx, sy-0), get_unadjusted_data (sx+1, sy-0), get_unadjusted_data (sx+2, sy-0)};
      vec_luminosity_t v3 = {get_unadjusted_data (sx-1, sy+1), get_unadjusted_data (sx, sy+1), get_unadjusted_data (sx+1, sy+1), get_unadjusted_data (sx+2, sy+1)};
      vec_luminosity_t v4 = {get_unadjusted_data (sx-1, sy+2), get_unadjusted_data (sx, sy+2), get_unadjusted_data (sx+1, sy+2), get_unadjusted_data (sx+2, sy+2)};
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      val = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
      return val;
    }
    return 0;
  return val;
}

inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp)
{
  return adjust_luminosity_ir (get_unadjusted_img_pixel (xp, yp));
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline flatten_attr void
render::get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      luminosity_t rr, gg, bb;
      rr = cubic_interpolate (cubic_interpolate (get_linearized_data_red ( sx-1, sy-1), get_linearized_data_red (sx-1, sy), get_linearized_data_red (sx-1, sy+1), get_linearized_data_red (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx-0, sy-1), get_linearized_data_red (sx-0, sy), get_linearized_data_red (sx-0, sy+1), get_linearized_data_red (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx+1, sy-1), get_linearized_data_red (sx+1, sy), get_linearized_data_red (sx+1, sy+1), get_linearized_data_red (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx+2, sy-1), get_linearized_data_red (sx+2, sy), get_linearized_data_red (sx+2, sy+1), get_linearized_data_red (sx+2, sy+2), ry),
			      rx);
      gg = cubic_interpolate (cubic_interpolate (get_linearized_data_green ( sx-1, sy-1), get_linearized_data_green (sx-1, sy), get_linearized_data_green (sx-1, sy+1), get_linearized_data_green (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx-0, sy-1), get_linearized_data_green (sx-0, sy), get_linearized_data_green (sx-0, sy+1), get_linearized_data_green (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx+1, sy-1), get_linearized_data_green (sx+1, sy), get_linearized_data_green (sx+1, sy+1), get_linearized_data_green (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx+2, sy-1), get_linearized_data_green (sx+2, sy), get_linearized_data_green (sx+2, sy+1), get_linearized_data_green (sx+2, sy+2), ry),
			      rx);
      bb = cubic_interpolate (cubic_interpolate (get_linearized_data_blue ( sx-1, sy-1), get_linearized_data_blue (sx-1, sy), get_linearized_data_blue (sx-1, sy+1), get_linearized_data_blue (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx-0, sy-1), get_linearized_data_blue (sx-0, sy), get_linearized_data_blue (sx-0, sy+1), get_linearized_data_blue (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx+1, sy-1), get_linearized_data_blue (sx+1, sy), get_linearized_data_blue (sx+1, sy+1), get_linearized_data_blue (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx+2, sy-1), get_linearized_data_blue (sx+2, sy), get_linearized_data_blue (sx+2, sy+1), get_linearized_data_blue (sx+2, sy+2), ry),
			      rx);
      if (m_backlight_correction)
	{
	  rr = m_backlight_correction->apply (rr, xp, yp, backlight_correction_parameters::red, true);
	  gg = m_backlight_correction->apply (gg, xp, yp, backlight_correction_parameters::green, true);
	  bb = m_backlight_correction->apply (bb, xp, yp, backlight_correction_parameters::blue, true);
	}
      *r = rr;
      *g = gg;
      *b = bb;
    }
  else
    {
      *r = 0;
      *g = 0;
      *b = 0;
      return;
    }
}
inline flatten_attr void
render::get_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  get_unadjusted_img_rgb_pixel (xp, yp, r, g, b);
  *r = (*r - m_params.dark_point) * m_params.scan_exposure;
  *g = (*g - m_params.dark_point) * m_params.scan_exposure;
  *b = (*b - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
}

/* Sample square patch with center xc and yc and x1/y1, x2/y2 determining a coordinates
   of top left and top right corner.  */

luminosity_t
render::sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2)
{
  luminosity_t acc = 0, weights = 0;
  int xmin = std::max ((int)(std::min (std::min (std::min (xc - x1, xc + x1), xc - x2), xc + x2) - 0.5), 0);
  int xmax = std::min ((int)ceil (std::max(std::max (std::max (xc - x1, xc + x1), xc - x2), xc + x2) + 0.5), m_img.width - 1);
  /* If the resolution is too small, just sample given point.  */
  if (xmax-xmin < 2)
    return get_img_pixel (xc, yc);
  /* For bigger resolution we can sample few points in the square.  */
  if (xmax-xmin < 6)
    {
      /* Maybe this will give more reproducible results, but it is very slow.  */
      int samples = (sqrt (x1 * x1 + y1 * y1) + 0.5) * 2;
      luminosity_t rec = 1.0 / samples;
      if (!samples)
	return get_img_pixel (xc, yc);
      for (int y = -samples ; y <= samples; y++)
	for (int x = -samples ; x <= samples; x++)
	  {
	    luminosity_t w = 1 + (samples - abs (x) - abs (y));
	    if (w < 0)
	      continue;
	    acc += w * get_img_pixel (xc + (x1 * x + x2 * y) * rec, yc + (y1 * x + y2 * y) * rec);
	    weights += w;
	  }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      int ymin = std::max ((int)(std::min (std::min (std::min (yc - y1, yc + y1), yc - y2), yc + y2) - 0.5), 0);
      int ymax = std::min ((int)ceil (std::max(std::max (std::max (yc - y1, yc + y1), yc - y2), yc + y2) + 0.5), m_img.height - 1);
      matrix2x2<coord_t> base (x1, x2,
			      y1, y2);
      matrix2x2<coord_t> inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
	{
	  for (int x = xmin ; x <= xmax; x++)
	    {
	      coord_t cx = x+0.5 -xc;
	      coord_t cy = y+0.5 -yc;
	      coord_t ccx, ccy;
	      inv.apply_to_vector (cx, cy, &ccx, &ccy);
	      luminosity_t w = fabs (ccx) + fabs (ccy);

	      //if (w < 1)
		//printf ("%.1f ",w);
	      //else
		//printf ("    ",w);
	      if (w < 1)
		{
		  w = (1 - w);
		  acc += w * get_data (x, y);
		  weights += w;
		}
	    }
	    //printf ("\n");
	 }
    }
  if (weights)
    return acc / weights;
  return 0;
}

/* Helper for downscaling template.
   PIXEL is a pixel obtained from source image.  Account PIXEL*SCALE
   to DATA at coordinates (px,py), (px,py+1), (py+1, px) and (px+1,py+1)
   and distribute its value according to XWEIGHT and YWEIHT (here 0,0 means
   that pixel is accounted only to px,py.

   x0,x1,y0,y1 is used to disable updating for certain rows and columns to void
   accessing out of range data. 
  
   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
void
render::process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T pixel, luminosity_t scale, luminosity_t xweight, luminosity_t yweight)
{
  if (0)
    {
      assert (px >= (x0?0:-1) && px < (x1 ? width - 1 : width));
      assert (py >= (y0?0:-1) && py < (y1 ? height - 1: height));
    }
  
  if (x0)
    {
      if (y0)
	account_pixel (data + px + py * width, pixel, scale * (1 - yweight) * (1 - xweight));
      if (y1)
	account_pixel (data + px + (py + 1) * width, pixel, scale * yweight * (1 - xweight));
    }
  if (x1)
    {
      if (y0)
        account_pixel (data + px + (py * width) + 1, pixel, scale * (1 - yweight) * xweight);
      if (y1)
	account_pixel (data + px + (py + 1) * width + 1, pixel, scale * yweight * xweight);
    }
}

/* Helper for downscaling template.  Process line (in range XSTART..XEND) if input image with
   coordinate YY and account it (scaled by SCALE) to line of DATA with coordinate PY and PY+1.
   PY gets 1-yweight of the data, while py+1 get yweight of data. 
   PIXELPOS and WEIGHTS are precoputed scaling data for for x coordinate.

   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, typename D, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
void
render::process_line (T *data, int *pixelpos, luminosity_t *weights,
		      int xstart, int xend,
		      int width, int height,
		      int py, int yy,
		      bool y0, bool y1,
		      luminosity_t scale, luminosity_t yweight)
{
  int px = xstart;
  int xx = pixelpos[px];
  int stop;
  if (yy < 0 || yy >= m_img.height || xx >= m_img.width)
    return;
  if (px >= 0 && xx >= 0)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, false, true, y0, y1, pixel, scale, weights[px], yweight);
    }
  xx++;
  if (xx < 0)
    xx = 0;
  stop = pixelpos[px + 1];
  for (; xx < stop; xx++)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
    }
  px++;
  while (px <= xend)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, true, y0, y1, pixel, scale, weights[px], yweight);
      stop = pixelpos[px + 1];
      xx++;
      for (; xx < stop; xx++)
	{
	  T pixel = (((D *)this)->*get_pixel) (xx, yy);
	  process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
	}
      px++;
    }
   if (xx < m_img.width)
     {
       T pixel = (((D *)this)->*get_pixel) (xx, yy);
       process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, false, y0, y1, pixel, scale, weights[px], yweight);
     }
}

/* Template for paralelized downscaling of image.
   GET_PIXEL is used to access input image which is of type T and ACCOUNT_PIXEL is used to account
   pixels to given position of DATA.
 
   DATA is an output pixmap with dimensions WIDTH*HEIGHT.
   pixelsize if size of output pixel inside of input image.
   X,Y are coordinates of the top left corner of the output image in the input image.  */

template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
bool
render::downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  memset ((void *)data, 0, sizeof (T) * width * height);

  if (pxstart > pxend)
    return true;

  if (progress)
    {
      int pystart = std::max (0, (int)(-y / pixelsize));
      int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
      progress->set_task ("downscaling", pyend - pystart + 1);
    }

  /* Precompute to which column of output image given colon of input image shold be accounted to.  */
  int *pixelpos = (int *)malloc (sizeof (int) * (width + 1));
  luminosity_t *weights = (luminosity_t *)malloc (sizeof (luminosity_t) * (width + 1));

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

#pragma omp parallel shared(progress,data,pixelsize,width,height,pixelpos,x,y,pxstart,pxend,weights) default (none)
  {
    luminosity_t scale = 1 / (pixelsize * pixelsize);
    int pystart = std::max (0, (int)(-y / pixelsize));
    int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
#ifdef _OPENMP
    int tn = omp_get_thread_num ();
    int threads = omp_get_max_threads ();
#else
    int tn = 0;
    int threads = 1;
#endif
    int ystart = pystart + (pyend + 1 - pystart) * tn / threads;
    int yend = pystart + (pyend + 1 - pystart) * (tn + 1) / threads - 1;

    int py = ystart;
    int yy = ypixelpos(py);
    int stop;

    if (ystart > yend)
      goto end;
    if (py >= 0 && yy >= 0)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, false, true, scale, weight(py));
    yy++;
    stop = std::min (ypixelpos(py + 1), m_img.height);
    for (; yy < stop; yy++)
      {
	process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
      }
    py++;
    if (progress)
      progress->inc_progress ();
    while (py <= yend && (!progress || !progress->cancel_requested ()))
      {
        process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, true, scale, weight (py));
	stop = std::min (ypixelpos(py + 1), m_img.height);
	yy++;
	for (; yy < stop; yy++)
	  {
	    process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
	  }
	py++;
	if (progress)
	  progress->inc_progress ();
      }
     if (yy < m_img.height)
       process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, false, scale, weight (py));
     end:;
  }

#undef ypixelpos
#undef weight
  free (pixelpos);
  free (weights);
  return !progress || !progress->cancelled ();
}
#endif
