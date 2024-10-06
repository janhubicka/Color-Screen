#ifndef SCANNER_BLUR_CORRECTION_PARAMETERS_H
#define SCANNER_BLUR_CORRECTION_PARAMETERS_H
#include "base.h"
#include "color.h"
namespace colorscreen
{
class scanner_blur_correction_parameters
{
public:
  DLL_PUBLIC scanner_blur_correction_parameters ();
  DLL_PUBLIC bool alloc (int width, int height);
  DLL_PUBLIC ~scanner_blur_correction_parameters ();
  DLL_PUBLIC bool save (FILE *f);
  DLL_PUBLIC const char *save_tiff (const char *name);
  DLL_PUBLIC bool load (FILE *f, const char **);
  DLL_PUBLIC inline void set_gaussian_blur_radius (int x, int y, luminosity_t radius)
  {
    m_gaussian_blurs[y * m_width + x] = radius;
  }

  /* Unique id of the image (used for caching).  */
  uint64_t id;

private:
  int m_width, m_height;
  luminosity_t *m_gaussian_blurs;
};
}
#endif
