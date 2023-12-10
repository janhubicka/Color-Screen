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
  spectrum_dyes_to_xyz ()
    : rscale (1), gscale (1), bscale (1),
      xscale (1), yscale (1), zscale (1)
  {
  }
  spectrum backlight;
  spectrum red, green, blue;
  luminosity_t rscale, gscale, bscale;
  luminosity_t xscale, yscale, zscale;

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
  dyes_rgb_to_xyz (luminosity_t r, luminosity_t g, luminosity_t b, int observer = 1964)
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
  /* Return XYZ of white color seen through the dyes.  */
  xyz whitepoint_xyz (int observer = 1964)
    {
      spectrum nofilter;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	nofilter[i]=1;
      return get_xyz (nofilter, observer);
    }
  static xyz temperature_xyz (luminosity_t temperature);
  /* Return true if dyes_rgb_to_xyz behaves linearly.  */
  bool is_linear ();
  color_matrix xyz_matrix ();

  void write_spectra (const char *red, const char *green, const char *blue, const char *backlight, int start = SPECTRUM_START, int end = SPECTRUM_END);

  void synthetic_dufay_red (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_green (luminosity_t d1, luminosity_t d2);
  void synthetic_dufay_blue (luminosity_t d1, luminosity_t d2);

  private:
    static const bool debug = false;
    /* Compute XYZ values.  */
    inline struct xyz
    get_xyz (spectrum s, int observer = 1964)
    {
      struct xyz ret = { 0, 0, 0 };
      luminosity_t sum = 0;
      assert (observer == 1931 || observer == 1964);
      /* TODO: CIE recommends going by 1nm bands and interpolate.
	 We can implement that easily if that makes difference.  */
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	{
	  ret.x += (observer == 1931 ? cie_cmf_x : cie_cmf1964_x)[i] * s[i] * backlight[i];
	  ret.y += (observer == 1931 ? cie_cmf_y : cie_cmf1964_y)[i] * s[i] * backlight[i];
	  ret.z += (observer == 1931 ? cie_cmf_z : cie_cmf1964_z)[i] * s[i] * backlight[i];
	  sum += (observer == 1931 ? cie_cmf_y : cie_cmf1964_y)[i] * backlight[i];
	}
      luminosity_t scale = 1 / sum;
      ret.x *= scale;
      ret.y *= scale;
      ret.z *= scale;
      /* Argyll scales by backlight.  */
      return ret;
    }
};

color_matrix dufaycolor_correction_matrix ();
void synthetic_dufay_red (spectrum s, luminosity_t d1, luminosity_t d2);
void synthetic_dufay_green (spectrum s, luminosity_t d1, luminosity_t d2);
void synthetic_dufay_blue (spectrum s, luminosity_t d1, luminosity_t d2);

#endif
