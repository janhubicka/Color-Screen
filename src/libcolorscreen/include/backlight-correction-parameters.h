#ifndef BACKLIGHT_CORRECTION_PARAMETERS_H
#define BACKLIGHT_CORRECTION_PARAMETERS_H
#include <memory>
#include <vector>
#include "base.h"
#include "color.h"
namespace colorscreen
{
class backlight_correction;
class image_data;
struct memory_buffer;
struct tile_parameters;
class backlight_correction_parameters
{
  struct entry
  {
    luminosity_t sub[4];
    luminosity_t lum[4];
  };

public:
  // Use vector instead of raw pointer for automatic memory management
  typedef std::vector<entry> luminosities_t;
  enum channel
  {
    red,
    green,
    blue,
    ir,
    all_channels
  };
  DLL_PUBLIC backlight_correction_parameters ();
  DLL_PUBLIC bool alloc (int width, int height, bool enabled[4]);
  virtual ~backlight_correction_parameters () = default;
  DLL_PUBLIC static std::shared_ptr <backlight_correction_parameters>
  load_captureone_lcc (FILE *f, bool verbose = false);
  DLL_PUBLIC static std::shared_ptr <backlight_correction_parameters>
  analyze_scan (image_data &scan, luminosity_t gamma = 1, image_data *black = NULL);
  DLL_PUBLIC bool save (FILE *f) const;
  DLL_PUBLIC const char *save_tiff (const char *name);
  DLL_PUBLIC bool load (FILE *f, const char **);

  inline void
  set_luminosity (int x, int y, luminosity_t lum,
                  enum channel channel = all_channels)
  {
    struct entry &e = m_luminosities[y * m_width + x];
    if (channel != all_channels)
      e.lum[(int)channel] = lum;
    else
      for (int i = 0; i < 4; i++)
        e.lum[i] = lum;
  }

  inline void
  set_sub (int x, int y, luminosity_t sub,
           enum channel channel = all_channels)
  {
    struct entry &e = m_luminosities[y * m_width + x];
    if (channel != all_channels)
      e.sub[(int)channel] = sub;
    else
      for (int i = 0; i < 4; i++)
        e.sub[i] = sub;
  }

  /* Read the exact persisted calibration grid without exposing its mutable
     storage. Callers must use valid cell and channel indices, as with the
     existing setters. These accessors support native JSON serialization. */
  inline int get_width () const { return m_width; }
  inline int get_height () const { return m_height; }
  inline bool channel_enabled (enum channel which) const
  {
    return m_channel_enabled[(int)which];
  }
  inline luminosity_t get_luminosity (int x, int y,
                                     enum channel which) const
  {
    return m_luminosities[(size_t)y * m_width + x].lum[(int)which];
  }
  inline luminosity_t get_sub (int x, int y, enum channel which) const
  {
    return m_luminosities[(size_t)y * m_width + x].sub[(int)which];
  }

  /* Compare all persisted calibration samples, including disabled channels.
     The cache identity is deliberately not part of document state.  */
  bool
  equal_p (const backlight_correction_parameters &other) const
  {
    if (black_correction != other.black_correction
        || m_width != other.m_width || m_height != other.m_height
        || m_luminosities.size () != other.m_luminosities.size ())
      return false;
    for (int channel = 0; channel < 4; ++channel)
      if (m_channel_enabled[channel] != other.m_channel_enabled[channel])
        return false;
    for (size_t i = 0; i < m_luminosities.size (); ++i)
      for (int channel = 0; channel < 4; ++channel)
        if (m_luminosities[i].lum[channel]
                != other.m_luminosities[i].lum[channel]
            || m_luminosities[i].sub[channel]
                   != other.m_luminosities[i].sub[channel])
          return false;
    return true;
  }

  /* Internal API.  */
  static std::shared_ptr <backlight_correction_parameters>
  load_captureone_lcc (memory_buffer *buf, bool verbose = false);
  friend backlight_correction;

  DLL_PUBLIC void render_preview (tile_parameters &tile, int scan_width, int scan_height, const int_image_area &scan_area, luminosity_t black) const;

  /* Unique id of the image (used for caching).  */
  uint64_t id;

  bool black_correction = false;

private:
  int m_width, m_height;
  std::vector<entry> m_luminosities;
  bool m_channel_enabled[4];
};
}
#endif
