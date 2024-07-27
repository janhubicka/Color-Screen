#ifndef BACKLIGHT_CORRECTION_H
#define BACKLIGHT_CORRECTION_H
#include <vector>
#include <cstdio>
#include <cassert>
#include "include/color.h"
#include "include/base.h"
#include "include/imagedata.h"
#include "include/backlight-correction-parameters.h"
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
