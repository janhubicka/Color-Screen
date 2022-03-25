#ifndef RENDER_H
#define RENDER_H
#include <netpbm/pgm.h>
#include "scr-to-img.h"
class render
{
public:
  render (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval);
  double get_img_pixel (double x, double y);
  double get_img_pixel_scr (double x, double y);

protected:
  gray **m_img;
  scr_to_img m_scr_to_img;
  int m_img_width, m_img_height;
  int m_maxval;
};

class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval)
    : render (param, img, img_width, img_height, maxval)
  {
    m_scr_to_img.get_range (img_width, img_height, &m_scr_xshift, &m_scr_yshift, &m_scr_width, &m_scr_height);
  }
  int get_width ()
  {
    return m_scr_width;
  }
  int get_height ()
  {
    return m_scr_height;
  }
protected:
  int m_scr_xshift, m_scr_yshift;
  int m_scr_width, m_scr_height;
};
#endif
