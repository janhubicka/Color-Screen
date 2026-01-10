#ifndef SCANNER_BLUR_CORRECTION_PARAMETERS_H
#define SCANNER_BLUR_CORRECTION_PARAMETERS_H
#include "base.h"
#include "color.h"
namespace colorscreen
{
class scanner_blur_correction_parameters
{
public:
  enum correction_mode
  {
    blur_radius,
    mtf_defocus,
    mtf_blur_diameter,
    max_correction
  };
  DLL_PUBLIC static const char *correction_names [max_correction];
  DLL_PUBLIC static const char *pretty_correction_names [max_correction];
  DLL_PUBLIC scanner_blur_correction_parameters ();
  DLL_PUBLIC bool alloc (int width, int height, enum correction_mode mode);
  DLL_PUBLIC ~scanner_blur_correction_parameters ();
  DLL_PUBLIC bool save (FILE *f);
  DLL_PUBLIC const char *save_tiff (const char *name);
  DLL_PUBLIC bool load (FILE *f, const char **);
  inline void set_correction (int x, int y, luminosity_t radius)
  {
    m_corrections[y * m_width + x] = radius;
  }
  inline luminosity_t get_correction (int x, int y)
  {
    return m_corrections[y * m_width + x];
  }
  inline int get_width ()
  {
    return m_width;
  }
  inline int get_height ()
  {
    return m_height;
  }
  correction_mode get_mode ()
  {
    return m_mode;
  }

  /* Unique id of the image (used for caching).  */
  uint64_t id;

private:
  int m_width, m_height;
  luminosity_t *m_corrections;
  enum correction_mode m_mode;
};
}
#endif
