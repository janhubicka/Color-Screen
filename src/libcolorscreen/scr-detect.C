#include <cassert>
#include "include/scr-detect.h"
#include "include/render.h"

void
scr_detect::set_parameters (scr_detect_parameters param, luminosity_t gamma, int maxval)
{
  m_param = param;
  lookup_table = render::get_lookup_table (gamma, maxval);
#if 0
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
  t = m * t;
  //t.print(stdout);
  //printf ("Forward color transform:\n");
  //t.print(stdout);
  //printf ("Backward color transform:\n");
#endif

  /* Same logic is also in scr-detect-colors.C.  */
  color_matrix subtract_dark (1, 0, 0, -m_param.black.red,
			      0, 1, 0, -m_param.black.green,
			      0, 0, 1, -m_param.black.blue,
			      0, 0, 0, 1);
  color_matrix process_colors (m_param.red.red  ,  m_param.green.red   , m_param.blue.red, 0,
			       m_param.red.green,  m_param.green.green , m_param.blue.green, 0,
			       m_param.red.blue ,  m_param.green.blue  ,m_param.blue.blue, 0,
			       0, 0, 0, 1);
  m_color_adjust = process_colors.invert ();
  m_color_adjust = m_color_adjust * subtract_dark;
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
