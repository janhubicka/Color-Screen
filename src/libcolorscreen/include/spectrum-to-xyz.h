#ifndef SPECTRUM_TO_XYZ_H
#define SPECTRUM_TO_XYZ_H
#include "dllpublic.h"
#include "color.h"

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

class DLL_PUBLIC spectrum_dyes_to_xyz
{
public:
  static const int default_observer = 1931;
  spectrum_dyes_to_xyz ()
    : rscale (1), gscale (1), bscale (1),
      xscale (1), yscale (1), zscale (1)
  {
  }
  spectrum backlight;
  spectrum red, green, blue;
  spectrum film_response;
  luminosity_t rscale, gscale, bscale;
  luminosity_t xscale, yscale, zscale;
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
  void set_daylight_backlight (luminosity_t temperature);
  void set_il_A_backlight ();
  void set_il_B_backlight ();
  void set_il_C_backlight ();
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


  /* Set dyes to given measured spectra.  */
  void set_dyes_to_dufay (int measurement, luminosity_t age);
  void set_dyes_to_dufay_manual ();
  void set_dyes_to_dufay_color_cinematography ();
  void set_dyes_to_autochrome ();
  void set_dyes_to_autochrome2 (luminosity_t orange_erythrosine, luminosity_t orange_rose, luminosity_t orange_tartrazine,
			        luminosity_t green_patent, luminosity_t green_tartrazine,
			        luminosity_t violet_crystal, luminosity_t violet_flexo, luminosity_t age);

  struct xyz
  dyes_rgb_to_xyz (luminosity_t r, luminosity_t g, luminosity_t b, int observer = default_observer)
    {
      spectrum s;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	s[i] = red [i] * r * rscale + green [i] * g * gscale + blue [i] * b * bscale;
      struct xyz ret = get_xyz (s, observer);
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
  color_matrix optimized_xyz_matrix ();

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
  bool write_film_response (const char *filename, luminosity_t *f, bool absolute, bool log = true);

  void synthetic_dufay_red (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_green (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_blue (luminosity_t d1, luminosity_t d2);
  void set_dyes_to_wratten_25_58_47 ();
  bool generate_simulated_argyll_ti3_file (FILE *f);
  bool generate_color_target_tiff (const char *filename, const char **error, bool white_balance, bool optimized);
  void set_response_to_neopan_100 ();
  void set_response_to_ilford_panchromatic ();
  void set_response_to_ilford_panchromatic_fp4 ();
  void set_response_to_equal ();
  void set_response_to_y ();
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

  bool tiff_with_primaries (const char *filename, rgbdata white);
  bool tiff_with_overlapping_filters (const char *filename, rgbdata white, const char *spectra_prefix);
  bool tiff_with_overlapping_filters_response (const char *filename, rgbdata white);

  private:
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

color_matrix dufaycolor_correction_matrix ();
void synthetic_dufay_red (spectrum s, luminosity_t d1, luminosity_t d2);
void synthetic_dufay_green (spectrum s, luminosity_t d1, luminosity_t d2);
void synthetic_dufay_blue (spectrum s, luminosity_t d1, luminosity_t d2);
bool tiff_with_strips (const char *filename, xyz filter_red, xyz filter_green, xyz filter_blue, xyz background, xyz white);

#endif
