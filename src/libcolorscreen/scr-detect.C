#include <cassert>
#include "include/scr-detect.h"
#include "include/render.h"

void
scr_detect::set_parameters (scr_detect_parameters param, luminosity_t gamma, int maxval)
{
  m_param = param;
  lookup_table = render::get_lookup_table (gamma, maxval);
  color_t red = (m_param.red.sgngamma (1 / gamma) - m_param.black.sgngamma (1 / gamma)).normalize ();
  color_t green = (m_param.green.sgngamma (1 / gamma) - m_param.black.sgngamma (1 / gamma)).normalize ();
  color_t blue = (m_param.blue.sgngamma (1 / gamma) - m_param.black.sgngamma (1 / gamma)).normalize ();
  color_matrix t (1, 0, 0, m_param.black.sgngamma (1 / gamma).red,
		  0, 1, 0, m_param.black.sgngamma (1 / gamma).green,
		  0, 0, 1, m_param.black.sgngamma (1 / gamma).blue,
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

scr_detect::~scr_detect ()
{
  if (lookup_table)
    render::release_lookup_table (lookup_table);
}
