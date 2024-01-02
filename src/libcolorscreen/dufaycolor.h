#ifndef DUFAYCOLOR_H
#define DUFAYCOLOR_H
#include <cstdio>
#include "include/base.h"
#include "include/color.h"

/* Class expressing all knowledge on dufaycolor we have so far.  */
class dufaycolor
{
public:
  /* Sizes in percent based on color cinematography screen.  */
  static constexpr const coord_t red_width = 21.0;
  static constexpr const coord_t green_blue_width = /*28.6*/49.9-red_width;  /* Measured on microscropic image as 49.3.  */
  static constexpr const coord_t blue_height = 22.7;
  static constexpr const coord_t green_height = 26.9;
#if 0
#if 0
  static constexpr const coord_t red_width = 35.7;
  static constexpr const coord_t green_blue_width = 100-red_width;
  static constexpr const coord_t green_height = 34.6;
  static constexpr const coord_t blue_height = 100-green_height;
#else
  static constexpr const coord_t red_width = 39.9;
  static constexpr const coord_t green_blue_width = 100-red_width;
  static constexpr const coord_t green_height = 60.3;
  static constexpr const coord_t blue_height = 100-green_height;
#endif
#endif

  /* These are measurements from color microscopic photograph of Dufaycolor reseau in Color Cinematography. */
  static constexpr const coord_t red_width2 = /*4.984845*/ 5.206335;
  static constexpr const coord_t green_blue_width2 = /*9.140138*/ 8.827431;
  static constexpr const coord_t blue_height2 = /*8.132923*/ 8.651135;
  static constexpr const coord_t green_height2 = /*5.850212*/ 5.564918;

  /* These are measurements from b&W microscopic photograph of Dufaycolor reseau in Color Cinematography. */
  static constexpr const coord_t red_width3 = 6.412904;
  static constexpr const coord_t green_blue_width3 = 13.327374;
  static constexpr const coord_t blue_height3 = 11.435067;
  static constexpr const coord_t green_height3 = 8.406011;

  /* This is based on microscopic image of the filter structure of a Dufaycolor
     film. The Emulsion has been removed. The visible structures are not silver
     grain but the structure of the filter layers. 
     Credit: David Pfluger, ERC Advanced Grant FilmColors.
     Imaging was performed with support of the Center for Microscopy and Image Analysis, University of Zurich  */
  static constexpr const coord_t red_width4 = 21.0;
  static constexpr const coord_t green_blue_width4 = /*28.6*/49.9-red_width;  /* Measured on microscropic image as 49.3.  */
  static constexpr const coord_t blue_height4 = 22.7;
  static constexpr const coord_t green_height4 = 26.9;

  /* Relative widths of the two strips used to print Dufaycolor reseau.  */
  static constexpr const coord_t red_strip_width = red_width / (red_width + green_blue_width);
  static constexpr const coord_t green_strip_width = green_height / (green_height + blue_height);

  /* Size of the individual patches within screen.  */
  static constexpr const coord_t red_size = red_width * (blue_height + green_height);
  static constexpr const coord_t green_size = green_blue_width * green_height;
  static constexpr const coord_t blue_size = green_blue_width * blue_height;
  static constexpr const coord_t screen_size = red_size + green_size + blue_size;

  /* Proportions of the color in screen.  Interpolated rendering needs these correction
     factors to match realistic rendering.  */
  static constexpr const coord_t red_portion = red_size * 3 / screen_size;
  static constexpr const coord_t green_portion = green_size * 3 / screen_size;
  static constexpr const coord_t blue_portion = blue_size * 3 / screen_size;

  /* xyY coordinates of the dyes as listed in Color Cinematography table.
     Inspection is done under Illuminant B.  */
  static constexpr xyY red_dye_color_cinematography_xyY = xyY (0.633, 0.365, 0.177);  /* dominating wavelength 601.7*/
  static constexpr xyY green_dye_color_cinematography_xyY = xyY (0.233, 0.647, 0.43); /* dominating wavelength 549.6*/
  static constexpr xyY blue_dye_color_cinematography_xyY = xyY (0.140, 0.089, 0.037 ); /* dominating wavelength 466.0*/
  static constexpr luminosity_t dominant_wavelength_red =  601.7;
  static constexpr luminosity_t dominant_wavelength_green =  549.6;
  static constexpr luminosity_t dominant_wavelength_blue =  466.0;

  /* xy coordinates based on Color Cinematography table.  */
  static constexpr xy_t red_dye_chromatcity_diagram {0.6325670792971702, 0.3664879168169032};
  static constexpr xy_t green_dye_chromatcity_diagram {0.23628435113386248, 0.6420773521151741};
  static constexpr xy_t blue_dye_chromacity_diagram {0.16489656909044215, 0.09082762295884284};

  /* These xyY coordinates are based on table above however with apparent typo
     in X coordinate of blue dye is corected using xy coordinates printed above.
     xy coordinates to match documented dominating wavelength.
     Moreover the Y coordinate of blue dye is adjusted too, to get more neutral
     screen color.

     Dominating wavelength of green dye is also wrong, however since the xyY data matches
     xy chart, I believe it is the dominating wavelength that is likely wrong.  */

  static constexpr xyY red_dye = xyY (0.633, 0.365, 0.177);  
  //static constexpr xyY green_dye = xyY (0.236, 0.642, 0.43); 
  static constexpr xyY green_dye = xyY (0.233/*0.293*/, 0.647, 0.43);
  //					  0.293?
  static constexpr xyY blue_dye = xyY (/*0.14*/ 0.164, 0.089, /*0.037*/ 0.073 /*0.087*/ ); 
  //static constexpr xyY blue_dye = xyY (0.1640, 0.089, 0.037 ); /* dominating wavelength 466.0*/




  /* An attempt to correct possible misprint in blue dye Y specification.  
     While density of red and green dye mostly corresponds to what can be calculated
     from the spectral information, blue dye is a lot darker.*/
  //static constexpr xyY correctedY_blue_dye = xyY (0.140, 0.089, 0.087 ); /* dominating wavelength 466.0*/


  inline static color_matrix color_cinematography_xyY_dye_matrix()
  {
    return matrix_by_dye_xyY (red_dye_color_cinematography_xyY, green_dye_color_cinematography_xyY, blue_dye_color_cinematography_xyY);
  }
  inline static color_matrix corrected_dye_matrix()
  {
    return matrix_by_dye_xyY (red_dye, green_dye, blue_dye);
  }
  static xyz get_color_cinematography_xyY_dufay_white ()
  {
    return ((xyz)red_dye_color_cinematography_xyY * (red_size / screen_size)) + ((xyz)green_dye_color_cinematography_xyY * (green_size / screen_size)) + ((xyz)blue_dye_color_cinematography_xyY * (blue_size / screen_size));
  }
  static xyz get_corrected_dufay_white ()
  {
    return ((xyz)red_dye * (red_size / screen_size)) + ((xyz)green_dye * (green_size / screen_size)) + ((xyz)blue_dye * (blue_size / screen_size));
  }
  static void determine_relative_patch_sizes_by_whitepoint (luminosity_t *r, luminosity_t *g, luminosity_t *b);
  static void determine_relative_patch_sizes_by_simulated_response (luminosity_t *r, luminosity_t *g, luminosity_t *b);
  static void print_xyY_report ();
  static void print_spectra_report ();
  static void print_synthetic_dyes_report ();
  static bool tiff_with_primaries (const char *, bool);
  static bool generate_ti3_file (FILE *);
  static bool generate_color_target_tiff (const char *filename, const char **error);
};
void report_illuminant (class spectrum_dyes_to_xyz &spec, const char *name, const char *filename, const char *filename2 = NULL);
#endif
