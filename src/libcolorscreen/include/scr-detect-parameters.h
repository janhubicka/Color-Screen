#ifndef SCR_DETECT_PARAMETERS_H
#define SCR_DETECT_PARAMETERS_H
#include "dllpublic.h"
#include "color.h"
#include "base.h"
namespace colorscreen
{
/* Parameters controlling classification of screen colors before geometry
   detection.  */
struct scr_detect_parameters
{
  /* Initialize screen-color classification defaults.  */
  scr_detect_parameters ()
      : black ({ 0, 0, 0 }), red ({ 1, 0, 0 }), green ({ 0, 1, 0 }),
        blue ({ 0, 0, 1 }), min_luminosity (0.000), min_ratio (1),
        sharpen_radius (2), sharpen_amount (3)
  {
  }

  /* Typical values of red, green and blue dyes scaled to range (0,1) in the
     scan's gamma.  */
  color_t black, red, green, blue;
  /* Minimal luminosity for detection to be performed.  */
  luminosity_t min_luminosity;
  /* Determine dye as a given color if its luminosity is greater than ratio
     times the sum of luminosities of the other two colors.  */
  luminosity_t min_ratio;

  /* Legacy unsharp-mask parameters used before screen-color classification.
     Existing real Dufay integration scans still depend on the historical
     radius 2 / amount 3 default, so retain it until the blur-tolerant
     classifier can replace this compatibility aid.  Explicit zero values
     remain valid for benchmarking the unsharpened detector path.  Changing
     the default requires validation against the real Dufay integration
     fixtures.  */
  coord_t sharpen_radius;
  luminosity_t sharpen_amount;
  /* Return true when OTHER produces the same color classification.  This is
     also used as part of the screen-color cache key.  */
  bool
  operator== (const scr_detect_parameters &other) const
  {
    return black == other.black && red == other.red && green == other.green
           && blue == other.blue && min_luminosity == other.min_luminosity
           && min_ratio == other.min_ratio
           && sharpen_radius == other.sharpen_radius
           && sharpen_amount == other.sharpen_amount;
  }
  /* Return true when OTHER changes screen-color classification.  */
  bool
  operator!= (const scr_detect_parameters &other) const
  {
    return !(*this == other);
  }
};
}
#endif
