#include "include/wratten.h"
#include "include/dufaycolor.h"
#include "include/spectrum-to-xyz.h"
namespace colorscreen
{
constexpr xyz wratten::filter_25_red;
constexpr xyz wratten::filter_58_green;
constexpr xyz wratten::filter_47_blue;
constexpr xy_t wratten::filter_25_red_xy;
constexpr xy_t wratten::filter_58_green_xy;
constexpr xy_t wratten::filter_47_blue_xy;
void
wratten::print_xyz_report ()
{
  printf ("Colors specified by Color Cinematography table:\n");
  printf ("color     book xyz             | book xy       | computed xy   |dominating wavelength\n");
  printf ("color     x      y      z      | x      y      | x      y      |book from xyz from xy\n");
  printf ("red       %5.4f %5.4f %5.4f | %5.4f %5.4f | %5.4f %5.4f | %5.1f %5.1f %5.1f\n", filter_25_red.x, filter_25_red.y, filter_25_red.z, filter_25_red_xy.x, filter_25_red_xy.y, ((xy_t)filter_25_red).x, ((xy_t)filter_25_red).y, filter_25_red_dominating_wavelength, dominant_wavelength (filter_25_red), dominant_wavelength (filter_25_red_xy));
  printf ("green     %5.4f %5.4f %5.4f | %5.4f %5.4f | %5.4f %5.4f | %5.1f %5.1f %5.1f\n", filter_58_green.x, filter_58_green.y, filter_58_green.z, filter_58_green_xy.x, filter_58_green_xy.y, ((xy_t)filter_58_green).x, ((xy_t)filter_58_green).y,filter_58_green_dominating_wavelength, dominant_wavelength (filter_58_green), dominant_wavelength (filter_58_green_xy));
  printf ("blue      %5.4f %5.4f %5.4f | %5.4f %5.4f | %5.4f %5.4f | %5.1f %5.1f %5.1f\n\n", filter_47_blue.x, filter_47_blue.y, filter_47_blue.z, filter_47_blue_xy.x, filter_47_blue_xy.y, ((xy_t)filter_47_blue).x, ((xy_t)filter_47_blue).y, filter_47_blue_dominating_wavelength, dominant_wavelength (filter_47_blue), dominant_wavelength (filter_47_blue_xy));
}
void
wratten::print_spectra_report ()
{
  spectrum_dyes_to_xyz spec;
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  spec.set_dyes (spectrum_dyes_to_xyz::wratten_25_58_47_kodak_1945);
  report_illuminant (spec, "CIE C", "wratten-screen.tif");
}
}
