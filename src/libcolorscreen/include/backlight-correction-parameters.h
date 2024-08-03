#ifndef BACKLIGHT_CORRECTION_PARAMETERS_H
#define BACKLIGHT_CORRECTION_PARAMETERS_H
#include "base.h"
#include "color.h"
namespace colorscreen
{
class backlight_correction;
class image_data;
struct memory_buffer;
class backlight_correction_parameters
{
  struct entry
  {
    // luminosity_t add[4];
    luminosity_t lum[4];
  };

public:
  enum channel
  {
    red,
    green,
    blue,
    ir,
    all_channels
  };
  backlight_correction_parameters ();
  bool alloc (int width, int height, bool enabled[4]);
  DLL_PUBLIC ~backlight_correction_parameters ();
  DLL_PUBLIC static backlight_correction_parameters *
  load_captureone_lcc (FILE *f, bool verbose = false);
  DLL_PUBLIC static backlight_correction_parameters *
  analyze_scan (image_data &scan, luminosity_t gamma = 1);
  DLL_PUBLIC bool save (FILE *f);
  DLL_PUBLIC const char *save_tiff (const char *name);
  DLL_PUBLIC bool load (FILE *f, const char **);

  inline void
  set_luminosity (int x, int y, luminosity_t lum,
                  enum channel channel = all_channels)
  {
    struct entry &e = m_luminosities[y * m_width + x];
    if (channel != all_channels)
      {
        // e.add[(int)channel] = add;
        e.lum[(int)channel] = lum;
      }
    else
      for (int i = 0; i < 4; i++)
        {
          // e.add[i] = add;
          e.lum[i] = lum;
        }
  }

  /* Internal API.  */
  static backlight_correction_parameters *
  load_captureone_lcc (memory_buffer *buf, bool verbose = false);
  friend backlight_correction;

  /* Unique id of the image (used for caching).  */
  uint64_t id;

private:
  int m_width, m_height;
  entry *m_luminosities;
  bool m_channel_enabled[4];
};
}
#endif
