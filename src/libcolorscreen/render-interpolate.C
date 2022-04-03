#include <assert.h>
#include "include/render-interpolate.h"

render_interpolate::render_interpolate (scr_to_img_parameters param, image_data &img, int dst_maxval)
   : render_to_scr (param, img, dst_maxval), m_prec_red (0), m_prec_green (0), m_prec_blue (0), m_precise (false), m_screen (NULL), m_adjust_luminosity (NULL)
{
}

void
render_interpolate::set_screen (double radius)
{
  static screen blured_screen;
  static double r = -1;
  static enum scr_type t;
  double x, y, x2, y2;
  m_scr_to_img.to_scr (0, 0, &x, &y);
  m_scr_to_img.to_scr (1, 0, &x2, &y2);
  radius *= sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));

  if (t != m_scr_to_img.get_type () || fabs (r - radius) > 0.01)
    {
      screen *s = new screen;
      s->initialize (m_scr_to_img.get_type ());
      blured_screen.initialize_with_blur (*s, radius);
      t = m_scr_to_img.get_type ();
      r = radius;
    }
  m_screen = &blured_screen;
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
  if (m_scr_to_img.get_type () != Dufay)
    {
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
#define pixel(xo,yo,diag) m_precise ? sample_scr_diag_square ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo, diag)\
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
#undef pixel
    }
  else
    {
      /* G B .
	 R R .
	 . . .  */
      m_prec_red = (double *)malloc (m_prec_width * m_prec_height * sizeof (double) * 2);
      m_prec_green = (double *)malloc (m_prec_width * m_prec_height * sizeof (double));
      m_prec_blue = (double *)malloc (m_prec_width * m_prec_height * sizeof (double));
#define pixel(xo,yo,width,height) m_precise ? sample_scr_square ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo, width, height)\
			 : get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
      for (int x = 0; x < m_prec_width; x++)
	for (int y = 0 ; y < m_prec_height; y++)
	  {
	    dufay_prec_red (2 * x, y) = pixel (0, 0.5,0.5, 0.3333);
	    dufay_prec_red (2 * x + 1, y) = pixel (0.5, 0.5, 0.5, 0.3333);
	    dufay_prec_green (x, y) = pixel (0, 0, 0.5, 1 - 0.333);
	    dufay_prec_blue (x, y) = pixel (0.5, 0, 0.5, 1 - 0.333);
	  }
#undef pixel
    }
}

flatten_attr void
render_interpolate::render_pixel_scr (double x, double y, int *r, int *g, int *b)
{
  double red, green, blue;
  x += m_prec_xshift;
  y += m_prec_yshift;

  if (m_scr_to_img.get_type () != Dufay)
    {
      double xx = 2*(x-0.25);
      double yy = 2*(y-0.25);
      int xp = floor (xx), yp = floor (yy);
      double xo = xx - floor (xx), yo = yy - floor (yy);
#define get_blue(xx, yy) prec_blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue

      double xd, yd;
      to_diagonal_coordinates (x, y, &xd, &yd);
      xp = floor (xd);
      yp = floor (yd);
      xo = xd - floor (xd);
      yo = yd - floor (yd);

#define get_green(xx, yy) prec_diag_green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
      xp = floor (xd);
      yp = floor (yd);
      xo = xd - floor (xd);
      yo = yd - floor (yd);
#define get_red(xx, yy) prec_diag_red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
    }
  else
    {
      double xx = 2*(x);
      double yy = y-0.5;
      int xp = floor (xx), yp = floor (yy);
      double xo = xx - floor (xx), yo = yy - floor (yy);
#define get_red(xx, yy) dufay_prec_red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
      xx = x;
      yy = y;
      xp = floor (xx);
      yp = floor (yy);
      xo = xx - floor (xx);
      yo = yy - floor (yy);
#define get_green(xx, yy) dufay_prec_green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      xx = x-0.5;
      yy = y;
      xp = floor (xx);
      yp = floor (yy);
      xo = xx - floor (xx);
      yo = yy - floor (yy);
#define get_blue(xx, yy) dufay_prec_blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue
    }
  if (m_screen)
    {
      double lum = get_img_pixel_scr (x - m_prec_xshift, y - m_prec_yshift);
      int ix = (long long) round ((x - m_prec_xshift) * screen::size) & (screen::size - 1);
      int iy = (long long) round ((y - m_prec_yshift) * screen::size) & (screen::size - 1);
      double sr = m_screen->mult[iy][ix][0];
      double sg = m_screen->mult[iy][ix][1];
      double sb = m_screen->mult[iy][ix][2];
      double llum = red * sr + green * sg + blue * sb;
      double correction = std::max (std::min (lum / llum, 5.0), 0.0);
      set_color (red * correction, green * correction, blue * correction, r, g, b);
#if 0
      red = std::min (1.0, std::max (0.0, red));
      green = std::min (1.0, std::max (0.0, green));
      blue = std::min (1.0, std::max (0.0, blue));
      if (llum < 0.0001)
	llum = 0.0001;
      if (llum > 1)
	llum = 1;
      if (lum / llum > 2)
	lum = 2 * llum;
      //set_color_luminosity (red, green, blue, lum / llum * (red * rwght + green * gwght + blue * bwght), r, g, b);
      //set_color_luminosity (red, green, blue, lum / llum * (red + green + blue)*0.333, r, g, b);
#endif
    }
  else if (m_adjust_luminosity)
    set_color_luminosity (red, green, blue, get_img_pixel_scr (x - m_prec_xshift, y - m_prec_yshift), r, g, b);
  else
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
