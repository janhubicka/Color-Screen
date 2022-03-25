#include "render.h"

render::render (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval, int dst_maxval)
{
  m_img = img;
  m_img_width = img_width;
  m_img_height = img_height;
  m_scr_to_img.set_parameters (param);
  m_maxval = maxval;
  m_dst_maxval = dst_maxval;
}

