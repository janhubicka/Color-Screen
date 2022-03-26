#include "render.h"

render::render (scr_to_img_parameters param, image_data &img, int dst_maxval)
{
  m_img = img;
  m_scr_to_img.set_parameters (param);
  m_dst_maxval = dst_maxval;
}

