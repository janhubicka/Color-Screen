#include "scr-to-img.h"

void
scr_to_img::set_parameters (scr_to_img_parameters param)
{
  m_param = param;
  /* Translate 0 xstart/ystart.  */
  matrix4x4 translation;
  translation.m_elements[0][2] = param.center_x;
  translation.m_elements[1][2] = param.center_y;

  /* Change basis.  */
  matrix4x4 basis;
  basis.m_elements[0][0] = param.base1_x;
  basis.m_elements[0][1] = param.base1_y;
  basis.m_elements[1][0] = param.base2_x;
  basis.m_elements[1][1] = param.base2_y;
  m_matrix = translation * basis;
}
