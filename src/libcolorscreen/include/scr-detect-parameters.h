#ifndef SCR_DETECT_PARAMETERS_H
#define SCR_DETECT_PARAMETERS_H
#include "dllpublic.h"
#include "color.h"
#include "base.h"
namespace colorscreen
{
struct scr_detect_parameters
{
  scr_detect_parameters ()
      : black ({ 0, 0, 0 }), red ({ 1, 0, 0 }), green ({ 0, 1, 0 }),
        blue ({ 0, 0, 1 }), min_luminosity (0.000), min_ratio (1),
        sharpen_radius (2), sharpen_amount (3)
  {
  }

  /* Typical valus of red, green and blue dyes scaled to range (0,1) in the
   * scan's gamma.  */
  color_t black, red, green, blue;
  /* Minimal luminosity for detection to be performed.  */
  luminosity_t min_luminosity;
  /* Determine dye as a given color if its luminosity is greater than ratio
   * times the sum of luminosities of the other two colors.  */
  luminosity_t min_ratio;

  /* Sharpening info.  */
  coord_t sharpen_radius;
  luminosity_t sharpen_amount;
  bool
  operator== (scr_detect_parameters &other) const
  {
    return black == other.black && red == other.red && green == other.green
           && blue == other.blue && sharpen_radius == other.sharpen_radius
           && sharpen_amount == other.sharpen_amount;
  }
  bool
  operator!= (scr_detect_parameters &other) const
  {
    return !(*this == other);
  }
};
}
#endif
