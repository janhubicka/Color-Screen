#include <cassert>
#include "include/scr-detect.h"

void
scr_detect::set_parameters (scr_detect_parameters param)
{
  m_param = param;
  color_t red = (m_param.red - m_param.black).normalize ();
  color_t green = (m_param.green - m_param.black).normalize ();
  color_t blue = (m_param.blue - m_param.black).normalize ();
  color_matrix t (1, 0, 0, m_param.black.red,
		  0, 1, 0, m_param.black.green,
		  0, 0, 1, m_param.black.blue,
		  0, 0, 0, 1);
  color_matrix m (red.red,   green.red,   blue.red,   0,
		  red.green, green.green, blue.green, 0,
		  red.blue,  green.blue,  blue.blue,  0,
		  0, 0, 0, 1);
  //printf ("basis\n");
  //m.print(stdout);
  t = t * m;
  //printf ("Forward color transform:\n");
  //t.print(stdout);
  //printf ("Backward color transform:\n");
  m_color_adjust = t.invert ();
  //printf ("Inverse:\n");
  //m_color_adjust.print(stdout);
  //printf ("combined:\n");
  //(t*m_color_adjust).print(stdout);
}
