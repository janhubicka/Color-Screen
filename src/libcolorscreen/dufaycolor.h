#include "include/base.h"
#include "include/color.h"
class dufaycolor
{
public:
  /* This is based on microscopic image of the filter structure of a Dufaycolor
     film. The Emulsion has been removed. The visible structures are not silver
     grain but the structure of the filter layers. 
     Credit: David Pfluger, ERC Advanced Grant FilmColors.
     Imaging was performed with support of the Center for Microscopy and Image Analysis, University of Zurich  */
  static constexpr const coord_t red_width = 21.0;
  static constexpr const coord_t green_blue_width = /*28.6*/49.6-red_width;  /* Measured on microscropic image as 49.3.  */
  static constexpr const coord_t blue_height = 22.7;
  static constexpr const coord_t green_height = 26.9;

  /* Size of the screen.  */
  static constexpr const coord_t red_size = red_width * (blue_height + green_height);
  static constexpr const coord_t green_size = green_blue_width * green_height;
  static constexpr const coord_t blue_size = green_blue_width * blue_height;
  static constexpr const coord_t screen_size = red_size + green_size + blue_size;

  /* Proportio of the color in screen.  Interpolated rendering needs these correction
     factors to match realistic rendering.  */
  static constexpr const coord_t red_portion = red_size * 3 / screen_size ;
  static constexpr const coord_t green_portion = red_size * 3 / screen_size ;
  static constexpr const coord_t blue_portion = red_size * 3 / screen_size ;

  /* This is based on table in Color Cinematography.
     There seems to be missprint in the book, since dominating wavelengths does not match
     specified xy coordinates.
     Comparing with the spectra, also Y of blue seems to be wrong and it should be 0.087
     instead of 0.037.  This makes screen more white balanced.  */
  static constexpr xyY red_dye = xyY (0.633, 0.365, 0.177);  /* dominating wavelength 601.7*/
  static constexpr xyY green_dye = xyY (0.233, 0.647, 0.43); /* dominating wavelength 549.6*/
  static constexpr xyY blue_dye = xyY (0.140, 0.089, /*0.037*/ 0.087 ); /* dominating wavelength 466.0*/
};
