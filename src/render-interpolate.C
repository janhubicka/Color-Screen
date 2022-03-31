#include <assert.h>
#include "render-interpolate.h"

render_interpolate::render_interpolate (scr_to_img_parameters param, image_data &img, int dst_maxval)
   : render_to_scr (param, img, dst_maxval), m_prec_red (0), m_prec_green (0), m_prec_blue (0), m_precise (true)
{
}

flatten_attr void
render_interpolate::precompute (double xmin, double ymin, double xmax, double ymax)
{
  assert (!m_prec_red);
  render::precompute (xmin, ymin, xmax, ymax);
  /* We need to compute bit more to get interpolation right.
     TODO: figure out how much.  */
  m_prec_xshift = -(xmin - 4);
  m_prec_yshift = -(ymin - 4);
  m_prec_width = xmax - xmin + 8;
  m_prec_height = ymax - ymin + 8;
  /* Thames, Finlay and Paget screen are organized as follows:
    
     G   R   .
       B   B
     R   G   .
       B   B
     .   .   .  
     2 reds and greens per one screen tile while there are 4 blues.  */
  m_prec_red = (double *)malloc (m_prec_width * m_prec_height * sizeof (double) * 2);
  m_prec_green = (double *)malloc (m_prec_width * m_prec_height * sizeof (double) * 2);
  m_prec_blue = (double *)malloc (m_prec_width * m_prec_height * sizeof (double) * 4);
  //#define pixel(xo,yo,ign) get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
  #define pixel(xo,yo,rad) m_precise ? sample_scr_diag_square ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo, rad)\
			   : get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
  for (int x = 0; x < m_prec_width; x++)
    for (int y = 0 ; y < m_prec_height; y++)
      {
	prec_red (x, 2 * y) = pixel (-0.5, 0, 0.5);
	prec_red (x, 2 * y + 1) = pixel (0, 0.5, 0.5);
	prec_green (x, 2 * y) = pixel (0.0, 0, 0.5);
	prec_green (x, 2 * y + 1) = pixel (0.5, 0.5, 0.5);
	prec_blue (2 * x, 2 * y) = pixel (0.25, 0.25, 0.3);
	prec_blue (2 * x + 1, 2 * y) = pixel (0.75, 0.25, 0.3);
	prec_blue (2 * x, 2 * y + 1) = pixel (0.25, 0.75, 0.3);
	prec_blue (2 * x + 1, 2 * y + 1) = pixel (0.75, 0.75, 0.3);
      }
}

flatten_attr void
render_interpolate::render_pixel_scr (double x, double y, int *r, int *g, int *b)
{
  x += m_prec_xshift;
  y += m_prec_yshift;

  double xx = 2*(x-0.25);
  double yy = 2*(y-0.25);
  int xp = floor (xx), yp = floor (yy);
  double xo = xx - floor (xx), yo = yy - floor (yy);
#define get_blue(xx, yy) prec_blue (xp + (xx), yp + (yy))
  double blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				   cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				   cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				   cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo),
				   xo);
#undef get_blue

  double xd, yd;
  to_diagonal_coordinates (x, y, &xd, &yd);
  xp = floor (xd);
  yp = floor (yd);
  xo = xd - floor (xp);
  yo = yd - floor (yp);
  /*xo=1;
  yo=1;*/
#define get_green(xx, yy) prec_diag_green (xp + (xx), yp + (yy))
  double green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				   cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				   cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				   cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo),
				   xo);
#undef get_green
  to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
  xp = floor (xd);
  yp = floor (yd);
  xo = xd - floor (xp);
  yo = yd - floor (yp);
  /*xo=1;
  yo=1;*/
#define get_red(xx, yy) prec_diag_red (xp + (xx), yp + (yy))
  double red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
				  cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
				  cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
				  cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo),
				  xo);
#undef get_red
  set_color (red, green, blue, r, g, b);
}

render_interpolate::~render_interpolate ()
{
  if (m_prec_red)
    {
      free (m_prec_red);
      free (m_prec_green);
      free (m_prec_blue);
    }
}
