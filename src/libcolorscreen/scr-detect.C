#include <cassert>
#include "include/scr-detect.h"

void
scr_detect::set_parameters (scr_detect_parameters param)
{
  m_param = param;
  color_t red = (m_param.red - m_param.black).normalize ();
  color_t green = (m_param.green - m_param.black).normalize ();
  color_t blue = (m_param.blue - m_param.black).normalize ();
  color_matrix m (red.red,   red.blue,   red.green,   m_param.black.red,
		  green.red, green.blue, green.green, m_param.black.green,
		  blue.red,  blue.blue,  blue.green,  m_param.black.blue,
		  0, 0, 0, 1);
  m.print(stdout);
  m_color_adjust = m.invert ();
  m_color_adjust.print(stdout);
}
