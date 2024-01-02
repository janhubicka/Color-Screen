#include <cassert>
#include "include/scr-detect.h"
#include "include/render.h"

void
scr_detect::set_parameters (scr_detect_parameters param, luminosity_t gamma, int maxval)
{
  m_param = param;
  lookup_table = render::get_lookup_table (gamma, maxval);
  rgbdata black = m_param.black.sgngamma (gamma);
  rgbdata red = m_param.red.sgngamma (gamma);
  rgbdata green = m_param.green.sgngamma (gamma);
  rgbdata blue = m_param.blue.sgngamma (gamma);
  red = (red - black).normalize ();
  green = (green - black).normalize ();
  blue = (blue - black).normalize ();
  color_matrix t (1, 0, 0, black.red,
		  0, 1, 0, black.green,
		  0, 0, 1, black.blue,
		  0, 0, 0, 1);
  color_matrix m (red.red,   green.red,   blue.red,   0,
		  red.green, green.green, blue.green, 0,
		  red.blue,  green.blue,  blue.blue,  0,
		  0, 0, 0, 1);
  //printf ("basis\n");
  //t.print(stdout);
  t = t * m;
  //t.print(stdout);
  //printf ("Forward color transform:\n");
  //t.print(stdout);
  //printf ("Backward color transform:\n");
  m_color_adjust = t.invert ();
  //m_color_adjust.print(stdout);

  //luminosity_t rr,gg,bb;
  //m_color_adjust.apply_to_rgb (m_param.red.sgngamma (gamma).red, m_param.red.sgngamma (gamma).green, m_param.red.sgngamma (gamma).blue, &rr, &gg, &bb);
  //printf ("red: %f %f %f\n",rr,gg,bb);
  //m_color_adjust.apply_to_rgb (m_param.black.sgngamma (gamma).red, m_param.black.sgngamma (gamma).green, m_param.black.sgngamma (gamma).blue, &rr, &gg, &bb);
  //printf ("black: %f %f %f\n",rr,gg,bb);
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
