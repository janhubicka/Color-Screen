#ifndef RENDER_PARAMETERS_H
#define RENDER_PARAMETERS_H
#include "base.h"
#include "color.h"
#include "progress-info.h"
#include "sensitivity.h"
#include "tone-curve.h"
#include "scr-to-img-parameters.h"
#include "backlight-correction-parameters.h"
#include "scanner-blur-correction-parameters.h"
namespace colorscreen
{
class render_type_parameters;
class render_type_property;
class stitch_project;
/* Parameters of rendering algorithms.  */
struct render_parameters
{
  /***** Scan linearization parmaeters  *****/

  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).
     Only positive values makes sense; meaningful range is approx 0.01 to 10.
   */
  luminosity_t gamma;

  /* TODO; Invert is applied before backlight correction which is wrong.  */
  class backlight_correction_parameters *backlight_correction;
  luminosity_t backlight_correction_black;

  class scanner_blur_correction_parameters *scanner_blur_correction;

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

  /* Radius (in pixels) and amount for unsharp-mask filter.  */
  luminosity_t sharpen_radius, sharpen_amount;

  /***** Tile Adjustment (used to adjust parameters of individual tiles) *****/

  struct tile_adjustment
  {
    /* Multiplicative correction to stitch-project global scan_exposure.  */
    luminosity_t exposure;
    /* Additive correction to stitch-project dark point.  */
    luminosity_t dark_point;
    /* If true tile is rendered, if false tile is not rendered.  */
    bool enabled;
    /* Coordinates of the tile in stitch project (used to check that tile
       adjustments match stitch project dimensions).  */
    unsigned char x, y;
    constexpr
    tile_adjustment ()
        : exposure (1), dark_point (0), enabled (true), x (0), y (0)
    {
    }
    bool
    operator== (tile_adjustment &other) const
    {
      return enabled == other.enabled && dark_point == other.dark_point
             && exposure == other.exposure;
    }
    bool
    operator!= (tile_adjustment &other) const
    {
      return !(*this == other);
    }
    void
    apply (render_parameters *p) const
    {
      p->dark_point = dark_point + exposure * p->dark_point;
      p->scan_exposure *= exposure;
    }
  };

  int tile_adjustments_width, tile_adjustments_height;
  std::vector<tile_adjustment> tile_adjustments;

  /***** Path density parameters  *****/

  /* Gamma curve of the film (to be replaced by HD curve eventually)  */
  luminosity_t film_gamma;
  /* The following is used by interpolated rendering only.  */
  /* If true use precise data collection.  */
  bool precise;
  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;

  /* Width of strips used to print Dufaycolor reseau (screen).
     This is relative portion in range 0..1.
     0 will give default values.  */
  coord_t dufay_red_strip_width, dufay_green_strip_width;

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
  DLL_PUBLIC static const char *color_model_names[(int)color_model_max];
  enum color_model_t color_model;
  /* Aging simulation (0 new dyes, 1 aged dyes).
     Only effective for color models that support aging simulation.  */
  luminosity_t age;
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
  DLL_PUBLIC static const char *dye_balance_names[(int)dye_balance_max];
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

  /***** Experimental (unfinished) stuff *****/

  hd_curve *film_characteristics_curve;
  hd_curve *output_curve;

  /* Use characteristics curves to restore original luminosity.  */
  bool restore_original_luminosity;

  render_parameters ()
      : /* Scan linearization.  */
        gamma (2.2), backlight_correction (NULL),
        backlight_correction_black (0), scanner_blur_correction (NULL), dark_point (0), scan_exposure (1),
        ignore_infrared (false), invert (false), mix_dark (0, 0, 0),
        mix_red (0.3), mix_green (0.1), mix_blue (1), sharpen_radius (0),
        sharpen_amount (0),

        /* Tile adjustment.  */
        tile_adjustments_width (0), tile_adjustments_height (0),
        tile_adjustments (),

        /* Patch density parameters.  */
        film_gamma (1), precise (true), screen_blur_radius (0.5),
        collection_threshold (0.2), dufay_red_strip_width (0),
        dufay_green_strip_width (0),

        /* Scanner profile.  */
        scanner_red (0, 0, 0), scanner_green (0, 0, 0), scanner_blue (0, 0, 0),

        /* Process profile.  */
        profiled_dark (0, 0, 0), profiled_red (1, 0, 0),
        profiled_green (0, 1, 0), profiled_blue (0, 0, 1),

        /* Output adjustment.  */
        white_balance ({ 1, 1, 1 }), presaturation (1),
        color_model (color_model_none), age (1), temperature (5000),
        backlight_temperature (5000),
        observer_whitepoint (/*srgb_white*/ d50_white),
        dye_balance (dye_balance_bradford), saturation (1), brightness (1),
        output_tone_curve (tone_curve::tone_curve_linear),
        target_film_gamma (1),

        /* Output profile  */
        output_profile (output_profile_sRGB), output_gamma (-1),
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
  operator== (render_parameters &other) const
  {
    if (tile_adjustments.size () != other.tile_adjustments.size ()
        || tile_adjustments_width != other.tile_adjustments_width
        || tile_adjustments_height != other.tile_adjustments_height)
      return false;
    for (unsigned int i = 0; i < tile_adjustments.size (); i++)
      if (tile_adjustments[i] != other.tile_adjustments[i])
        return false;
    return gamma == other.gamma && film_gamma == other.film_gamma
           && target_film_gamma == other.target_film_gamma
           && output_gamma == other.output_gamma
           && sharpen_radius == other.sharpen_radius
           && sharpen_amount == other.sharpen_amount
           && presaturation == other.presaturation
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
           && backlight_temperature == backlight_temperature
           && dark_point == other.dark_point
           && scan_exposure == other.scan_exposure
           && dufay_red_strip_width == other.dufay_red_strip_width
           && dufay_green_strip_width == other.dufay_green_strip_width
           && invert == other.invert
           && screen_blur_radius == other.screen_blur_radius
           && dye_balance == other.dye_balance && precise == other.precise
           && film_characteristics_curve == other.film_characteristics_curve
           && restore_original_luminosity == other.restore_original_luminosity
           && output_curve == other.output_curve
           && scanner_blur_correction == other.scanner_blur_correction
           && backlight_correction == other.backlight_correction
           && backlight_correction_black == other.backlight_correction_black;
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

  /* Initialize render parameters for showing original scan.
     In this case we do not want to apply color models etc.  */
  void original_render_from (render_parameters &rparam, bool color, bool profiled);

  void adjust_for (render_type_parameters &rtparam, render_parameters &rparam);
  color_matrix get_profile_matrix (rgbdata patch_proportions);
private:
  static const bool debug = colorscreen_checking;
  color_matrix get_dyes_matrix (bool *spectrum_based, bool *optimized,
                                const image_data *img);
  color_matrix get_balanced_dyes_matrix (const image_data *img,
                                         bool normalized_patches,
                                         rgbdata patch_proportions,
                                         xyz target_whitepoint = d50_white);
};
}
#endif
