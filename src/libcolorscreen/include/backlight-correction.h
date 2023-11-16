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
  static backlight_correction_parameters *analyze_scan (image_data &scan, luminosity_t gamma = 1);
  /* Unique id of the image (used for caching).  */
  unsigned long id;
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
  luminosity_t apply (float val, int x, int y, enum backlight_correction_parameters::channel channel)
  {
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
    int xx = x * m_img_width_rec;
    int yy = y * m_img_height_rec;
    struct entry &e = m_weights[yy * m_width + xx];
    //printf ("%i %i %i %i %f\n",x,y,xx,yy,e.mult[channel]);
    return (val - m_black) * e.mult[channel] + m_black/*+e.add[channel]*/;
#endif
  }
  bool initialized_p ()
  {
    return m_weights != NULL;
  }
  int id;
private:
  backlight_correction_parameters &m_params;
  luminosity_t m_black;
  int m_img_width, m_img_height;
  int m_width, m_height;
  coord_t m_img_width_rec;
  coord_t m_img_height_rec;
  entry *m_weights;
};

#endif
