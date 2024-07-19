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
  uint64_t pos;
  uint64_t len;
  char getc()
  {
    assert (pos < len);
    return *((char *)data+pos++);
  }
  bool load_file (FILE *f);
};
class backlight_correction;
class backlight_correction_parameters
{
  struct entry
  {
    //luminosity_t add[4];
    luminosity_t lum[4];
  };
public:
  enum channel {red, green, blue, ir, all_channels};
  backlight_correction_parameters ();
  bool
  alloc (int width, int height, bool enabled[4])
  {
    m_luminosities = (entry *)calloc (width * height, sizeof (entry));
    m_width = width;
    m_height = height;
    for (int i = 0; i < 4;i++)
      m_channel_enabled[i] = enabled[i];
    return (m_luminosities != NULL);
  }
  void
  set_luminosity (int x, int y, luminosity_t lum, enum channel channel = all_channels)
  {
    struct entry &e = m_luminosities[y * m_width + x];
    if (channel != all_channels)
      {
	//e.add[(int)channel] = add;
	e.lum[(int)channel] = lum;
      }
    else
      for (int i = 0; i < 4; i++)
	{
	  //e.add[i] = add;
	  e.lum[i] = lum;
	}
  }
  ~backlight_correction_parameters ()
  {
    if (m_luminosities)
      free (m_luminosities);
  }
  static backlight_correction_parameters *load_captureone_lcc (memory_buffer *buf, bool verbose = false);
  DLL_PUBLIC static backlight_correction_parameters *load_captureone_lcc (FILE *f, bool verbose = false);
  static backlight_correction_parameters *analyze_scan (image_data &scan, luminosity_t gamma = 1);
  /* Unique id of the image (used for caching).  */
  uint64_t id;
  bool save (FILE *f);
  const char* save_tiff (const char *name);
  bool load (FILE *f, const char **);
  friend backlight_correction;
private:
  int m_width, m_height;
  entry *m_luminosities;
  bool m_channel_enabled[4];
};


class
backlight_correction
{
  struct entry
  {
    //luminosity_t add[4];
    luminosity_t mult[4];
  };
public:
  backlight_correction (backlight_correction_parameters &params, int width, int height, luminosity_t black, bool white_balance, progress_info *progress);
  ~backlight_correction ()
  {
    free (m_weights);
  }
  inline
  luminosity_t apply (float val, int x, int y, enum backlight_correction_parameters::channel channel, bool safe = false, luminosity_t *mul = NULL)
  {
    int xx, yy;
    coord_t rx = my_modf (x * m_img_width_rec, &xx);
    coord_t ry = my_modf (y * m_img_height_rec, &yy);
    if (!safe && (xx < 0 || xx >= m_width || y < 0 || yy >= m_width))
      return val;
    struct entry &e00 = m_weights[yy * m_width + xx];
    struct entry &e10 = m_weights[yy * m_width + xx + (xx == m_width - 1 ? 0 : 1)];
    luminosity_t mult0 = e00.mult[channel] * (1 - rx) + e10.mult[channel] * rx;;
    struct entry &e01 = m_weights[yy * m_width + (yy == m_height - 1 ? 0 : m_width) + xx];
    struct entry &e11 = m_weights[yy * m_width + (yy == m_height - 1 ? 0 : m_width) + xx + (xx == m_width - 1 ? 0 : 1)];
    luminosity_t mult1 = e01.mult[channel] * (1 - rx) + e11.mult[channel] * rx;;
    luminosity_t mult = mult0 * (1 - ry) + mult1 * ry;
    if (mul)
      *mul = mult;
    //printf ("%f %f %f %i\n",val,m_black,mult, channel);
    return (val - m_black) * mult + m_black;
  }
  bool initialized_p ()
  {
    return m_weights != NULL;
  }
  int id;
private:
  backlight_correction_parameters &m_params;
  luminosity_t m_black;
  //int m_img_width, m_img_height;
  int m_width, m_height;
  coord_t m_img_width_rec;
  coord_t m_img_height_rec;
  entry *m_weights;
};

#endif
