#ifndef DUFAYCOLOR_H
#define DUFAYCOLOR_H
#include "include/base.h"
#include "include/color.h"

/* Class expressing all knowledge on dufaycolor we have so far.  */
class dufaycolor
{
public:
  /* This is based on microscopic image of the filter structure of a Dufaycolor
     film. The Emulsion has been removed. The visible structures are not silver
     grain but the structure of the filter layers. 
     Credit: David Pfluger, ERC Advanced Grant FilmColors.
     Imaging was performed with support of the Center for Microscopy and Image Analysis, University of Zurich  */
  static constexpr const coord_t red_width = 21.0;
  static constexpr const coord_t green_blue_width = /*28.6*/49.9-red_width;  /* Measured on microscropic image as 49.3.  */
  static constexpr const coord_t blue_height = 22.7;
  static constexpr const coord_t green_height = 26.9;

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

  /* xyY coordinates of the dyes as listed in Color Cinematography table.  */
  static constexpr xyY red_dye = xyY (0.633, 0.365, 0.177);  /* dominating wavelength 601.7*/
  static constexpr xyY green_dye = xyY (0.233, 0.647, 0.43); /* dominating wavelength 549.6*/
  static constexpr xyY blue_dye = xyY (0.140, 0.089, 0.037 ); /* dominating wavelength 466.0*/
  /* Table in Color Cinematography lists the following wavelengths.  These does not seem
     to correspond to real colors with any reasonable choice of whitepoint.  */
  static constexpr luminosity_t dominant_wavelength_red =  601.7;
  static constexpr luminosity_t dominant_wavelength_green =  549.6;
  static constexpr luminosity_t dominant_wavelength_blue =  466.0;


  /* An attempt to correct possible misprint in blue dye Y specification.  
     While density of red and green dye mostly corresponds to what can be calculated
     from the spectral information, blue dye is a lot darker.*/
  static constexpr xyY correctedY_blue_dye = xyY (0.140, 0.089, 0.087 ); /* dominating wavelength 466.0*/


  inline static color_matrix dye_matrix()
  {
    return matrix_by_dye_xyY (red_dye, green_dye, blue_dye);
  }
  inline static color_matrix correctedY_dye_matrix()
  {
    return matrix_by_dye_xyY (red_dye, green_dye, correctedY_blue_dye);
  }
  static void print_report ();
  static void tiff_with_primaries (const char *, bool);
};
#endif
