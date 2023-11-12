#ifndef BACKLIGHT_CORRECTION_H
#define BACKLIGHT_CORRECTION_H
#include <vector>
#include <cstdio>
#include <cassert>
#include "color.h"
#include "base.h"
#include "imagedata.h"
struct memory_buffer 
{
  void *data;
  int pos;
  int len;
  char getc()
  {
    assert (pos < len);
    return *((char *)data+pos++);
  }
};
class backlight_correction
{
  struct entry
  {
    luminosity_t add[4];
    luminosity_t mult[4];
  };
public:
  enum channel {red, green, blue, ir, all_channels};
  backlight_correction () : m_width (0), m_height (0), m_weights (NULL), m_channel_enabled {true, true, true, false}
  {
  }
  bool
  alloc (int width, int height, bool enabled[4])
  {
    m_weights = (entry *)calloc (2*width * height, sizeof (entry));
    m_width = width;
    m_height = height;
    for (int i = 0; i < 4;i++)
      m_channel_enabled[i] = enabled[i];
    return (m_weights != NULL);
  }
  void
  set_weight (int x, int y, luminosity_t mult, luminosity_t add, enum channel channel = all_channels)
  {
    struct entry &e = m_weights[y * m_width + x];
    if (channel != all_channels)
      {
	e.add[(int)channel] = add;
	e.mult[(int)channel] = mult;
      }
    else
      for (int i = 0; i < 4; i++)
	{
	  e.add[i] = add;
	  e.mult[i] = mult;
	}
  }
  inline
  luminosity_t apply (float val, int width, int height, int x, int y, enum channel channel)
  {
#if 0
    if (x < 0)
      x = 0;
    if (x >= width)
      x = width;
    if (y < 0)
      y = 0;
    if (y >= height)
      y = height;
    if (xx < 0 || xx >= m_width || yy < 0 || yy >= m_height)
      return val;
#endif
#if 0
    coord_t xx = x * (m_width / (coord_t)width);
    coord_t yy = y * (m_height / (coord_t)height);
    int sx, sy;
    coord_t rx = my_modf (xx, &sx);
    coord_t ry = my_modf (yx, &sy);
    struct entry &e00 = m_weights[(yy + 0) * m_width + xx];
    struct entry &e10 = m_weights[std::max ((yy + 1) * m_width + xx];
    struct entry &e01 = m_weights[(yy + 0) * m_width + xx];
    struct entry &e11 = m_weights[(yy + 1) * m_width + xx];
#endif

#if 1
    int xx = x * (m_width / (coord_t)width);
    int yy = y * (m_height / (coord_t)height);
    struct entry &e = m_weights[yy * m_width + xx];
    return val * e.mult[channel]+e.add[channel];
#endif
  }
  ~backlight_correction ()
  {
    if (m_weights)
      free (m_weights);
  }
  static backlight_correction *load_captureone_lcc (memory_buffer *buf, bool verbose = false);
  static backlight_correction *analyze_scan (image_data &scan, luminosity_t gamma = 1);
  bool save (FILE *f);
  const char* save_tiff (const char *name);
  bool load (FILE *f, const char **);
private:
  int m_width, m_height;
  entry *m_weights;
  bool m_channel_enabled[4];
};

#endif
