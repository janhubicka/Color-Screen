#ifndef SPECTRUM_TO_XYZ_H
#define SPECTRUM_TO_XYZ_H
#include "dllpublic.h"
#include "color.h"
#include "sensitivity.h"

#define SPECTRUM_START 380
#define SPECTRUM_STEP  5
#define SPECTRUM_END   780
#define SPECTRUM_SIZE ((SPECTRUM_END - SPECTRUM_START) / SPECTRUM_STEP + 1)

typedef luminosity_t spectrum[SPECTRUM_SIZE];
extern const DLL_PUBLIC spectrum cie_cmf_x;
extern const DLL_PUBLIC spectrum cie_cmf_y;
extern const DLL_PUBLIC spectrum cie_cmf_z;
extern const DLL_PUBLIC spectrum cie_cmf1964_x;
extern const DLL_PUBLIC spectrum cie_cmf1964_y;
extern const DLL_PUBLIC spectrum cie_cmf1964_z;
inline luminosity_t
transmitance_to_absorbance (luminosity_t t)
{
  return 2 - log10 (std::max (t, 0.000001) * 100);
}

inline luminosity_t
absorbance_to_transmitance (luminosity_t a)
{
  //return pow (10, 2 - a) * 0.01;
  luminosity_t ret = pow (10, -a);
#if 0
  if (ret > 1)
    {
      //printf ("%f\n",ret);
      return 1;
    }
  if (ret < 0)
    {
      //printf ("%f %f\n",a,ret);
      return 0;
    }
#endif
  return ret;
}

class DLL_PUBLIC spectrum_dyes_to_xyz
{
public:
  enum dyes {
    dufaycolor_color_cinematography,
    dufaycolor_harrison_horner,
    dufaycolor_dufaycolor_manual,
    dufaycolor_photography_its_materials_and_processes,
    dufaycolor_separation_filters_photography_its_materials_and_processes,
    dufaycolor_narrow_cut_filters_harrison_horner,
    dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor,
    dufaycolor_aged_DC_MSI_NSMM11951,
    dufaycolor_aged_DC_MSI_NSMM11960,
    dufaycolor_aged_DC_MSI_NSMM11967,
    dufaycolor_aged_DC_MSI_NSMM12075,
    cinecolor,
    autochrome_reconstructed,
    autochrome_reconstructed_aged,
    wratten_25_58_47_color_cinematography,
    wratten_25_58_47_kodak_1945,
    kodachrome_25_sensitivity,
    phase_one_sensitivity,
    nikon_d3_sensitivity,
    dyes_max
  };
  constexpr static const char *dyes_names[dyes_max] =
  {
    "dufaycolor_color_cinematography",
    "dufaycolor_harrison_horner",
    "dufaycolor_dufaycolor_manual",
    "dufaycolor_photography_its_materials_and_processes",
    "dufaycolor_separation_filters_photography_its_materials_and_processes",
    "dufaycolor_narrow_cut_filters_harrison_hordner",
    "dufaycolor_NSMM_Bradford_11948",
    "dufaycolor_NSMM_Bradford_11951",
    "dufaycolor_NSMM_Bradford_11960",
    "dufaycolor_NSMM_Bradford_11967",
    "spicer_dufay_NSMM_Bradford_12075",
    "cinecolor_koshofer",
    "autochrome_Casella_Tsukada",
    "autochrome_Casella_Tsukada_aged",
    "wratten_25_58_47_color_cinematograpjy",
    "wratten_25_58_47_kodak_1945",
    "kodachrome_25_sensitivity",
    "phase_one_sensitivity",
    "nikon_d3_sensitivity",
  };
  enum illuminants {
     il_A,
     il_B,
     il_C,
     il_D,
     il_band,
     il_equal_energy,
     illuminants_max
  };
  constexpr static const char *illuminants_names[illuminants_max] =
  {
     "A",
     "B",
     "C",
     "D",
     "band",
     "equal_energy",
  };
  enum responses {
    neopan_100,
    ilford_panchromatic,
    ilford_panchromatic_fp4,
    ilford_sfx200,
    ilford_fp4_plus,
    rollei_retro_80s,
    spicer_dufay_guess,
    bergger_pancro_400,
    ilford_delta_400_professional,
    fomapan_400,
    fomapan_100_classic,
    kodak_professional_trix_400,
    dufaycolor_harrison_horner_emulsion_cut,
    observer_y,
    response_even,
    responses_max
  };
  constexpr static const char *responses_names[responses_max] =
  {
    "neopan_100",
    "ilford_panchromatic",
    "ilford_panchromatic_fp4",
    "ilford_sfx200",
    "ilford_fp4_plus",
    "rollei_retro_80s",
    "spicer_dufay_guess",
    "bergger_pancro_400",
    "ilford_delta_400_professional",
    "fomapan_400",
    "fomapan_100_classic",
    "kodak_professional_trix_400",
    "dufaycolor_harrison_horner_emulsion_cut",
    "observer_y",
    "even"
  };
  enum characteristic_curves {
    linear_reversal_curve,
    input_curve,
    safe_output_curve,
    safe_reversal_output_curve,
    kodachrome25_curve,
    spicer_dufay_curve_low,
    spicer_dufay_curve_mid,
    spicer_dufay_curve_high,
    spicer_dufay_reversal_curve_low,
    spicer_dufay_reversal_curve_mid,
    spicer_dufay_reversal_curve_high,
    characteristic_curves_max
  };
  constexpr static const char *characteristic_curve_names[characteristic_curves_max] =
  {
    "linear_reversal",
    "input",
    "safe",
    "safe_reversal",
    "kodachrome25",
    "spicer_dufay_low",
    "spicer_dufay_mid",
    "spicer_dufay_high",
    "spicer_dufay_reversal_low",
    "spicer_dufay_reversal_mid",
    "spicer_dufay_reversal_high",
  };

  static const int default_observer = 1931;
  spectrum_dyes_to_xyz ()
    : rscale (1), gscale (1), bscale (1),
      xscale (1), yscale (1), zscale (1),
      dark (0,0,0),
      exp_adjust {1,1,1},
      red_characteristic_curve (NULL), green_characteristic_curve (NULL), blue_characteristic_curve (NULL),
      subtractive (false),
      hd_curve (NULL)
  {
  }
  ~spectrum_dyes_to_xyz ()
  {
    if (red_characteristic_curve)
      delete (red_characteristic_curve);
    if (green_characteristic_curve && green_characteristic_curve != red_characteristic_curve)
      delete (green_characteristic_curve);
    if (blue_characteristic_curve && blue_characteristic_curve != red_characteristic_curve)
      delete (blue_characteristic_curve);
    if (hd_curve)
      delete (hd_curve);
  }
  spectrum backlight;
  /* Transmitance spectra of dyes for additive color synthetis.  */
  spectrum red, green, blue;
  /* Absorbance spectra of dyes for subtractive color synthetsis.  */
  spectrum cyan, magenta, yellow;
  spectrum film_response;
  luminosity_t rscale, gscale, bscale;
  luminosity_t xscale, yscale, zscale;
  xyz dark;
  rgbdata exp_adjust;

  /* Characteristic curves of film . */
  film_sensitivity *red_characteristic_curve, *green_characteristic_curve, *blue_characteristic_curve;


  bool subtractive;
  rgbdata linear_film_rgb_response (luminosity_t *s);
  rgbdata film_rgb_response (luminosity_t *s);

  void
  set_backlight (spectrum s)
    {
      memcpy (backlight, s, sizeof (backlight));
    }
  void
  set_dyes (spectrum r, spectrum g, spectrum b)
    {
      memcpy (red, r, sizeof (backlight));
      memcpy (green, g, sizeof (backlight));
      memcpy (blue, b, sizeof (backlight));
    }
  void set_characteristic_curve (enum characteristic_curves);
  /* Set dyes to given measured spectra.
     If dyes2 is set and age > 1, then mix the two spectras in given ratio.  */
  void set_dyes (enum dyes, enum dyes dyes2 = dufaycolor_color_cinematography, luminosity_t age = 0);
  void set_backlight (enum illuminants il, luminosity_t temperature = 5400);
  /* Adjust rscale, gscale and bscale so dye tgb (1,1,1) results
     in white in a given temperature of daylight.  */
  void normalize_dyes (luminosity_t temperature);
  /* Adjust xscale, yscale and zscale so dye rgb (1,1,1) results
     in intensity 1  */
  void normalize_brightness ();
  /* Adjust xscale, yscale and zscale so dye rgb (1,1,1) results
     in sRGB white  */
  void normalize_xyz_to_backlight_whitepoint ();
  /* Write spectra to tmp directory so they can be printed by gnuplot script.  */
  void debug_write_spectra ();



  struct xyz
  dyes_rgb_to_xyz (luminosity_t r, luminosity_t g, luminosity_t b, int observer = default_observer)
    {
      spectrum s;
#if 0
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	      printf (" %f", cyan[i]);
      printf ("\n");
#endif
      if (!subtractive)
	{
	  for (int i = 0; i < SPECTRUM_SIZE; i++)
	    s[i] = red [i] * r * rscale + green [i] * g * gscale + blue [i] * b * bscale;
	}
      else
	{
	  r = transmitance_to_absorbance (r * rscale);
	  g = transmitance_to_absorbance (g * gscale);
	  b = transmitance_to_absorbance (b * bscale);
#if 0
	  r/=3;
	  g/=3;
	  b/=3;
#endif
	  for (int i = 0; i < SPECTRUM_SIZE; i++)
	    {
	      s[i] = absorbance_to_transmitance (cyan [i] * r + magenta [i] * g + yellow [i] * b);
#if 0
	      luminosity_t ir = (1-(1-cyan [i]) * (1-r * rscale));
	      luminosity_t ig = (1-(1-magenta [i]) * (1-g * gscale));
	      luminosity_t ib = (1-(1-yellow [i]) * (1-b * bscale));
	      //printf ("%f %f %f %f %f %f %f %f %f %f %f %f\n", ir, ig, ib, rscale, bscale, gscale,r,g,b, cyan[i], magenta[i], yellow[i]);
	      //assert (ir >= 0 && ig >= 0 && ib >= 0 && ir <= 1 && ig <= 1 && ib <= 1);
	      s[i] = ir * ig * ib;
#endif
	    }
	}
      struct xyz ret = get_xyz (s, observer) - dark;
      ret.x *= xscale;
      ret.y *= yscale;
      ret.z *= zscale;
      return ret;
    }

  /* Return color matrix converting dyes rgb to xyz.  */
  color_matrix
  xyz_matrix (int observer = default_observer)
    {
      xyz r = dyes_rgb_to_xyz (1, 0, 0, observer);
      xyz g = dyes_rgb_to_xyz (0, 1, 0, observer);
      xyz b = dyes_rgb_to_xyz (0, 0, 1, observer);
      color_matrix m (r.x, g.x, b.x, 0,
		      r.y, g.y, b.y, 0,
		      r.z, g.z, b.z, 0,
		      0  , 0  , 0  , 1);
      return m;
    }
  color_matrix optimized_xyz_matrix (spectrum_dyes_to_xyz *observing_spec = NULL);

  /* Figure out relative sizes of patches which makes screen to look neutral with current dyes
     and backlight.  */
  rgbdata
  xyz_to_dyes_rgb (xyz color, int observer = default_observer)
  {
    color_matrix m = xyz_matrix (observer);
    rgbdata ret;
    m.normalize_grayscale (color.x, color.y, color.z, &ret.red, &ret.green, &ret.blue);
    return ret;
  }
  /* Return XYZ of white color seen through the dyes.  */
  xyz whitepoint_xyz (int observer = default_observer)
    {
      spectrum nofilter;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	nofilter[i]=1;
      return get_xyz (nofilter, observer);
    }
  static xyz temperature_xyz (luminosity_t temperature);
  /* Return true if dyes_rgb_to_xyz behaves linearly.  */
  bool is_linear ();

  void write_spectra (const char *red, const char *green, const char *blue, const char *backlight, int start = SPECTRUM_START, int end = SPECTRUM_END, bool absorbance = false);
  void write_responses (const char *red, const char *green, const char *blue, bool log = false, int start = SPECTRUM_START, int end = SPECTRUM_END);
  void write_ssf_json (const char *name);
  bool write_film_response (const char *filename, luminosity_t *f, bool absolute, bool log = true);
  void write_film_characteristic_curves (const char *red, const char *green, const char *blue);

  void synthetic_dufay_red (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_green (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_blue (luminosity_t d1, luminosity_t d2);
  bool generate_simulated_argyll_ti3_file (FILE *f);
  bool generate_color_target_tiff (const char *filename, const char **error, bool white_balance, bool optimized);
  void set_film_response (enum responses film);
  void set_response_to_kodachrome_25 ();
  void adjust_film_response_for_zeiss_contact_prime_cp2_lens ();
  void adjust_film_response_for_canon_CN_E_85mm_T1_3_lens ();

  /* Figure out relative sizes of patches which makes screen to look neutral with current dyes
     and backlight.  */
  rgbdata
  determine_relative_patch_sizes_by_whitepoint (int observer = default_observer)
  {
    xyz whitepoint = whitepoint_xyz (observer);
    rgbdata white = xyz_to_dyes_rgb (whitepoint, observer);
    luminosity_t sum = white.red + white.green + white.blue;
    return white / sum;
  }
  rgbdata determine_patch_weights_by_simulated_response (int observer = default_observer);

  rgbdata
  determine_relative_patch_sizes_by_simulated_response (int observer = default_observer)
  {
    rgbdata ret = determine_patch_weights_by_simulated_response (observer);
    luminosity_t sum = ret.red + ret.green + ret.blue;
    return ret / sum;
  }

  void
  adjust_exposure ()
  {
    rgbdata film_white = linear_film_rgb_response (NULL);
    luminosity_t sum = (film_white.red + film_white.green + film_white.blue) / 3;
    exp_adjust.red = exp_adjust.green = exp_adjust.blue = 1 / sum;
  }

  bool tiff_with_primaries (const char *filename, rgbdata white);
  bool tiff_with_overlapping_filters (const char *filename, rgbdata white, const char *spectra_prefix);
  bool tiff_with_overlapping_filters_response (const char *filename, rgbdata white);
  bool tiff_with_spectra_photo (const char *filename);

  private:
    synthetic_hd_curve *hd_curve;
    static const bool debug = false;
    /* Compute XYZ values.  */
    inline struct xyz
    get_xyz (spectrum s, int observer = default_observer)
    {
      struct xyz ret = { 0, 0, 0 };
      luminosity_t sum = 0;
      assert (observer == 1931 || observer == 1964);
      //printf ("%i\n",observer);
      /* TODO: CIE recommends going by 1nm bands and interpolate.
	 We can implement that easily if that makes difference.  */
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	{
	  ret.x += (observer == 1931 ? cie_cmf_x : cie_cmf1964_x)[i] * s[i] * backlight[i];
	  ret.y += (observer == 1931 ? cie_cmf_y : cie_cmf1964_y)[i] * s[i] * backlight[i];
	  ret.z += (observer == 1931 ? cie_cmf_z : cie_cmf1964_z)[i] * s[i] * backlight[i];
	  sum += (observer == 1931 ? cie_cmf_y : cie_cmf1964_y)[i] * backlight[i];
	  //printf ("x %f y %f z %f light %f data %f\n", cie_cmf_x[i], cie_cmf_y[i], cie_cmf_z[i], backlight[i], s[i]);
	}
      luminosity_t scale = 1 / sum;
      //printf ("scale %f ",scale);
      //ret.print (stdout);
      ret.x *= scale;
      ret.y *= scale;
      ret.z *= scale;
      //printf ("scaled ",scale);
      //ret.print (stdout);
      /* Argyll scales by backlight.  */
      return ret;
    }
};

color_matrix dufaycolor_correction_color_cinematography_matrix (luminosity_t temperature, luminosity_t backlight_temeperature);
color_matrix dufaycolor_correction_harrison_horner_matrix (luminosity_t temperature, luminosity_t backlight_temeperature);
color_matrix dufaycolor_correction_photography_its_materials_and_processes_matrix (luminosity_t temperature, luminosity_t backlight_temeperature);
bool tiff_with_strips (const char *filename, xyz filter_red, xyz filter_green, xyz filter_blue, xyz background, xyz white);
bool write_optimal_response (color_matrix m, const char *redname, const char *greenname, const char *bluename, luminosity_t rw, luminosity_t gw, luminosity_t bw);

#endif
