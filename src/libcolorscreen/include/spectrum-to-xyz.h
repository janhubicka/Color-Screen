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
  /* Adjust rscale, gscale and bscale so dye tgb (1,1,1) results
     in white in a given temperature of daylight.  */
  void normalize_dyes (luminosity_t temperature);
  /* Adjust xscale, yscale and zscale so dye rgb (1,1,1) results
     in intensity 1  */
  void normalize_brightness ();
  /* Adjust xscale, yscale and zscale so dye rgb (1,1,1) results
     in sRGB white  */
  void normalize_xyz_to_backlight_whitepoint ();
  /* Write spectras to tmp directory so they can be printed by gnuplot script.  */
  void debug_write_spectras ();


  /* Set dyes to given measured spectras.  */
  void set_dyes_to_duffay (int measurement);
  void set_dyes_to_autochrome ();

  struct xyz
  dyes_rgb_to_xyz (luminosity_t r, luminosity_t g, luminosity_t b)
    {
      spectrum s;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	s[i] = red [i] * r * rscale + green [i] * g * gscale + blue [i] * b * bscale;
      struct xyz ret = get_xyz (s);
      ret.x *= xscale;
      ret.y *= yscale;
      ret.z *= zscale;
      return ret;
    }
  xyz whitepoint_xyz ()
    {
      spectrum nofilter;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	nofilter[i]=1;
      return get_xyz (nofilter);
    }
  private:
    const bool debug = false;
    /* Compute XYZ values.  */
    inline struct xyz
    get_xyz (spectrum s)
    {
      struct xyz ret = { 0, 0, 0 };
      luminosity_t sum = 0;
      /* TODO: CIE recommends going by 1nm bands and interpolate.
	 We can implement that easily if that makes difference.  */
      for (int i = 0; i < SPECTRUM_SIZE; i++)
	{
	  ret.x += (cie_cmf_x[i] * s[i]) * backlight[i];
	  ret.y += (cie_cmf_y[i] * s[i]) * backlight[i];
	  ret.z += (cie_cmf_z[i] * s[i]) * backlight[i];
	  sum += cie_cmf_y[i] * backlight[i];
	}
      luminosity_t scale = 1 / sum;
      ret.x *= scale;
      ret.y *= scale;
      ret.z *= scale;
      /* Argyll scales by backlight.  */
      return ret;
    }
};


#endif
