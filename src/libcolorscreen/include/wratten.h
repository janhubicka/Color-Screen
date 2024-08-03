#ifndef WRATTEN_H
#define WRATTEN_H
#include "base.h"
#include "color.h"
#include "dllpublic.h"
namespace colorscreen
{
class wratten
{
public:
  /* Color Cinematography table page 158; CIE illuminant C. */
  static constexpr xyz filter_25_red_color_cinematography = {0.3028, 0.1425, 0.0001};
  static constexpr xyz filter_58_green = {0.0891, 0.2451, 0.0219};
  static constexpr xyz filter_47_blue = {0.0862, 0.0240, 0.4730};

  /* MacAdam, D. L. Colorimetric specifications of Wratten light filters. Journal of
     the Optical Society of America 35, 10 (1945), 670–675.  */
  static constexpr xyz filter_25_red = {0.3038, 0.1425, 0.0001};

  static constexpr xy_t filter_25_red_xy = {0.6805, 0.3193};
  static constexpr xy_t filter_58_green_xy = {0.2500, 0.6885};
  static constexpr xy_t filter_47_blue_xy = {0.1477, 0.0412};

  static constexpr luminosity_t filter_25_red_dominating_wavelength = 615.3;
  static constexpr luminosity_t filter_58_green_dominating_wavelength = 541.5;
  static constexpr luminosity_t filter_47_blue_dominating_wavelength = 461.3;
  DLL_PUBLIC static void print_xyz_report ();
  DLL_PUBLIC static void print_spectra_report ();
};
}
#endif
