#ifndef SPECTRUM_TO_XYZ_H
#define SPECTRUM_TO_XYZ_H
#include "dllpublic.h"
#include "color.h"
#include "sensitivity.h"

namespace colorscreen
{

#define SPECTRUM_START 380
#define SPECTRUM_STEP  5
#define SPECTRUM_END   780
#define SPECTRUM_SIZE ((SPECTRUM_END - SPECTRUM_START) / SPECTRUM_STEP + 1)

typedef luminosity_t spectrum[SPECTRUM_SIZE];
class progress_info;
DLL_PUBLIC extern const spectrum cie_cmf_x;
DLL_PUBLIC extern const spectrum cie_cmf_y;
DLL_PUBLIC extern const spectrum cie_cmf_z;
DLL_PUBLIC extern const spectrum cie_cmf1964_x;
DLL_PUBLIC extern const spectrum cie_cmf1964_y;
DLL_PUBLIC extern const spectrum cie_cmf1964_z;
inline luminosity_t const_attr
transmitance_to_absorbance (luminosity_t t)
{
  return 2 - log10 (std::max (t, 0.000001) * 100);
}

inline luminosity_t const_attr
absorbance_to_transmitance (luminosity_t a)
{
  luminosity_t ret = pow (10, -a);
  return ret;
}

class spectrum_dyes_to_xyz
{
public:
  enum dyes {
    dufaycolor_color_cinematography,
    dufaycolor_harrison_horner,
    dufaycolor_collins_giles,
    dufaycolor_dufaycolor_manual,
    dufaycolor_photography_its_materials_and_processes,
    dufaycolor_separation_filters_photography_its_materials_and_processes,
    dufaycolor_narrow_cut_filters_harrison_horner,
    dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor,
    dufaycolor_aged_DC_MSI_NSMM11951,
    dufaycolor_aged_DC_MSI_NSMM11960,
    dufaycolor_aged_DC_MSI_NSMM11967,
    dufaycolor_aged_DC_MSI_NSMM12075,
    autochrome_ciortan_arteaga_trumpy,
    cinecolor,
    autochrome_reconstructed,
    autochrome_reconstructed_aged,
    wratten_25_58_47_color_cinematography,
    wratten_25_58_47_kodak_1945,
    kodachrome_25_sensitivity,
    phase_one_sensitivity,
    nikon_d3_sensitivity,
    nikon_coolscan_9000ED_sensitivity,
    thames_mees_pledge,
    dioptichrome_mees_pledge,
    autochrome_mees_pledge,
    debug_dyes,
    dyes_max
  };
  DLL_PUBLIC constexpr static const property_t dyes_names[dyes_max]  = {
  { "dufaycolor_color_cinematography", "Dufaycolor color cinematography", "" },
  { "dufaycolor_harrison_horner", "Dufaycolor harrison horner", "" },
  { "dufaycolor_collins_giles", "Dufaycolor collins giles", "" },
  { "dufaycolor_dufaycolor_manual", "Dufaycolor dufaycolor manual", "" },
  { "dufaycolor_photography_its_materials_and_processes", "Dufaycolor photography its materials and processes", "" },
  { "dufaycolor_separation_filters_photography_its_materials_and_processes", "Dufaycolor separation filters photography its materials and processes", "" },
  { "dufaycolor_narrow_cut_filters_harrison_hordner", "Dufaycolor narrow cut filters harrison hordner", "" },
  { "dufaycolor_NSMM_Bradford_11948", "Dufaycolor NSMM Bradford 11948", "" },
  { "dufaycolor_NSMM_Bradford_11951", "Dufaycolor NSMM Bradford 11951", "" },
  { "dufaycolor_NSMM_Bradford_11960", "Dufaycolor NSMM Bradford 11960", "" },
  { "dufaycolor_NSMM_Bradford_11967", "Dufaycolor NSMM Bradford 11967", "" },
  { "spicer_dufay_NSMM_Bradford_12075", "Spicer dufay NSMM Bradford 12075", "" },
  { "autochrome_ciortan_arteaga_trumpy", "Autochrome unfaded, Ciortan-Arteaga-Trumpy 2025", "" },
  { "cinecolor_koshofer", "Cinecolor koshofer", "" },
  { "autochrome_Casella_Tsukada", "Autochrome Casella Tsukada", "" },
  { "autochrome_Casella_Tsukada_aged", "Autochrome Casella Tsukada aged", "" },
  { "wratten_25_58_47_color_cinematograpjy", "Wratten 25 58 47 color cinematograpjy", "" },
  { "wratten_25_58_47_kodak_1945", "Wratten 25 58 47 kodak 1945", "" },
  { "kodachrome_25_sensitivity", "Kodachrome 25 sensitivity", "" },
  { "phase_one_sensitivity", "Phase one sensitivity", "" },
  { "nikon_d3_sensitivity", "Nikon d3 sensitivity", "" },
  { "nikon_coolscan_9000ED_sensitivity", "Nikon coolscan 9000ED sensitivity", "" },
  { "thames_Mees_Pledge", "Thames Mees Pledge", "" },
  { "dioptichrome_Mees_Pledge", "Dioptichrome Mees Pledge", "" },
  { "autochrome_Mees_Pledge", "Autochrome Mees Pledge", "" },
  { "debug", "Debug", "" },
};
  enum illuminants {
     il_A,
     il_B,
     il_C,
     il_D,
     il_band,
     il_equal_energy,
     il_nikon_coolscan_9000ED_LED_red,
     il_nikon_coolscan_9000ED_LED_green,
     il_nikon_coolscan_9000ED_LED_blue,
     il_debug,
     illuminants_max
  };
  DLL_PUBLIC constexpr static const property_t illuminants_names[illuminants_max]  = {
  { "A", "A", "" },
  { "B", "B", "" },
  { "C", "C", "" },
  { "D", "D", "" },
  { "band", "Band", "" },
  { "equal_energy", "Equal energy", "" },
  { "nikon_coolscan_9000ED_LED_red", "Nikon coolscan 9000ED LED red", "" },
  { "nikon_coolscan_9000ED_LED_green", "Nikon coolscan 9000ED LED green", "" },
  { "nikon_coolscan_9000ED_LED_blue", "Nikon coolscan 9000ED LED blue", "" },
  { "debug", "Debug", "" },
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
    aviphot_pan_400s,
    aviphot_pan_40_pe0,
    aviphot_pan_40_pe0_cut,
    monochromatic_ccd,
    observer_y,
    response_even,
    responses_max
  };
  DLL_PUBLIC constexpr static const property_t responses_names[responses_max]  = {
  { "neopan_100", "Neopan 100", "" },
  { "ilford_panchromatic", "Ilford panchromatic", "" },
  { "ilford_panchromatic_fp4", "Ilford panchromatic fp4", "" },
  { "ilford_sfx200", "Ilford sfx200", "" },
  { "ilford_fp4_plus", "Ilford fp4 plus", "" },
  { "rollei_retro_80s", "Rollei retro 80s", "" },
  { "spicer_dufay_guess", "Spicer dufay guess", "" },
  { "bergger_pancro_400", "Bergger pancro 400", "" },
  { "ilford_delta_400_professional", "Ilford delta 400 professional", "" },
  { "fomapan_400", "Fomapan 400", "" },
  { "fomapan_100_classic", "Fomapan 100 classic", "" },
  { "kodak_professional_trix_400", "Kodak professional trix 400", "" },
  { "dufaycolor_harrison_horner_emulsion_cut", "Dufaycolor harrison horner emulsion cut", "" },
  { "aviphot_pan_400s", "Aviphot pan 400s", "" },
  { "aviphot_pan_40_pe0", "Aviphot pan 40 pe0", "" },
  { "aviphot_pan_40_pe0_cut", "Aviphot pan 40 pe0 cut", "" },
  { "monochromatic_ccd", "Monochromatic ccd", "" },
  { "observer_y", "Observer y", "" },
  { "even", "Even", "" },
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
  DLL_PUBLIC constexpr static const property_t characteristic_curve_names[characteristic_curves_max]  = {
  { "linear_reversal", "Linear reversal", "" },
  { "input", "Input", "" },
  { "safe", "Safe", "" },
  { "safe_reversal", "Safe reversal", "" },
  { "kodachrome25", "Kodachrome25", "" },
  { "spicer_dufay_low", "Spicer dufay low", "" },
  { "spicer_dufay_mid", "Spicer dufay mid", "" },
  { "spicer_dufay_high", "Spicer dufay high", "" },
  { "spicer_dufay_reversal_low", "Spicer dufay reversal low", "" },
  { "spicer_dufay_reversal_mid", "Spicer dufay reversal mid", "" },
  { "spicer_dufay_reversal_high", "Spicer dufay reversal high", "" },
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
  DLL_PUBLIC rgbdata linear_film_rgb_response (luminosity_t *s);
  DLL_PUBLIC rgbdata film_rgb_response (luminosity_t *s);

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
  DLL_PUBLIC void set_characteristic_curve (enum characteristic_curves);
  /* Set dyes to given measured spectra.
     If dyes2 is set and age > 1, then mix the two spectras in given ratio.  */
  DLL_PUBLIC void set_dyes (enum dyes, enum dyes dyes2 = dyes_max, rgbdata age = {1,1,1});
  DLL_PUBLIC void set_backlight (enum illuminants il, luminosity_t temperature = 5400);
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
  DLL_PUBLIC void debug_write_spectra ();



  struct xyz  pure_attr __attribute__ ((noinline))
  dyes_rgb_to_xyz (luminosity_t r, luminosity_t g, luminosity_t b, int observer = default_observer)
    {
      spectrum s;
      if (!subtractive)
	{
	  for (int i = 0; i < SPECTRUM_SIZE; i++)
	    s[i] = red [i] * r * rscale + green [i] * g * gscale + blue [i] * b * bscale;
	}
      else
	{
	  r = transmitance_to_absorbance (std::max (r, (luminosity_t)0) * rscale + 1.0/65535);
	  g = transmitance_to_absorbance (std::max (g, (luminosity_t)0) * gscale + 1.0/65535);
	  b = transmitance_to_absorbance (std::max (b, (luminosity_t)0) * bscale + 1.0/65535);
	  for (int i = 0; i < SPECTRUM_SIZE; i++)
	    s[i] = absorbance_to_transmitance (cyan [i] * r + magenta [i] * g + yellow [i] * b);
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
  color_matrix optimized_xyz_matrix (spectrum_dyes_to_xyz *observing_spec = NULL, progress_info * = NULL);
  color_matrix process_transformation_matrix (spectrum_dyes_to_xyz *);

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

  DLL_PUBLIC void write_spectra (const char *red, const char *green, const char *blue, const char *backlight, int start = SPECTRUM_START, int end = SPECTRUM_END, bool absorbance = false);
  DLL_PUBLIC void write_responses (const char *red, const char *green, const char *blue, bool log = false, int start = SPECTRUM_START, int end = SPECTRUM_END);
  DLL_PUBLIC void write_ssf_json (const char *name);
  DLL_PUBLIC bool write_film_response (const char *filename, luminosity_t *f, bool absolute, bool log = true);
  DLL_PUBLIC void write_film_characteristic_curves (const char *red, const char *green, const char *blue);
  DLL_PUBLIC void write_film_hd_characteristic_curves (const char *red, const char *green, const char *blue);

  void synthetic_dufay_red (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_green (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_blue (luminosity_t d1, luminosity_t d2);
  bool generate_simulated_argyll_ti3_file (FILE *f);
  DLL_PUBLIC bool generate_color_target_tiff (const char *filename, const char **error, bool white_balance, bool optimized);
  DLL_PUBLIC void set_film_response (enum responses film);
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

  DLL_PUBLIC bool tiff_with_primaries (const char *filename, rgbdata white);
  DLL_PUBLIC bool tiff_with_overlapping_filters (const char *filename, rgbdata white, const char *spectra_prefix);
  DLL_PUBLIC bool tiff_with_overlapping_filters_response (const char *filename, rgbdata white);
  DLL_PUBLIC bool tiff_with_spectra_photo (const char *filename);

  private:
    synthetic_hd_curve *hd_curve;
    static const bool debug = false;
    /* Compute XYZ values.  */
    inline struct xyz pure_attr
    get_xyz (spectrum s, int observer = default_observer)
    {
      struct xyz ret = { 0, 0, 0 };
      luminosity_t sum = 0;
      //assert (observer == 1931 || observer == 1964);
      //printf ("%i\n",observer);
      /* TODO: CIE recommends going by 1nm bands and interpolate.
	 We can implement that easily if that makes difference.  */
      if (observer == 1931)
	for (int i = 0; i < SPECTRUM_SIZE; i++)
	  {
	    ret.x += cie_cmf_x[i] * s[i] * backlight[i];
	    ret.y += cie_cmf_y[i] * s[i] * backlight[i];
	    ret.z += cie_cmf_z[i] * s[i] * backlight[i];
	    sum +=   cie_cmf_y[i] * backlight[i];
	    //printf ("x %f y %f z %f light %f data %f\n", cie_cmf_x[i], cie_cmf_y[i], cie_cmf_z[i], backlight[i], s[i]);
	  }
      else
	for (int i = 0; i < SPECTRUM_SIZE; i++)
	  {
	    ret.x += cie_cmf1964_x[i] * s[i] * backlight[i];
	    ret.y += cie_cmf1964_y[i] * s[i] * backlight[i];
	    ret.z += cie_cmf1964_z[i] * s[i] * backlight[i];
	    sum += cie_cmf1964_y[i] * backlight[i];
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

color_matrix dufaycolor_correction_color_cinematography_matrix (luminosity_t temperature, luminosity_t backlight_temeperature, progress_info *progress = NULL);
color_matrix dufaycolor_correction_harrison_horner_matrix (luminosity_t temperature, luminosity_t backlight_temeperature, progress_info *progress = NULL);
color_matrix dufaycolor_correction_photography_its_materials_and_processes_matrix (luminosity_t temperature, luminosity_t backlight_temeperature, progress_info *progress = NULL);
color_matrix dufaycolor_correction_collins_and_giles_matrix (luminosity_t temperature, luminosity_t backlight_temeperature, progress_info *progress = NULL);
bool tiff_with_strips (const char *filename, xyz filter_red, xyz filter_green, xyz filter_blue, xyz background, xyz white);
bool write_optimal_response (color_matrix m, const char *redname, const char *greenname, const char *bluename, luminosity_t rw, luminosity_t gw, luminosity_t bw);
}
#endif
