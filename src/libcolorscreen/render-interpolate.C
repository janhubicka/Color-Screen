#include <assert.h>
#include "include/render-interpolate.h"

render_interpolate::render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval)
   : render_to_scr (param, img, rparam, dst_maxval), m_prec_red (0), m_prec_green (0), m_prec_blue (0), m_screen (NULL)
{
}

flatten_attr void
render_interpolate::precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax)
{
  assert (!m_prec_red);
  render::precompute (xmin, ymin, xmax, ymax);
  if (/*m_params.screen_compensation*/1)
    {
      static screen blured_screen;
      static coord_t r = -1;
      static enum scr_type t;
      coord_t x, y, x2, y2;
      coord_t radius = m_params.screen_blur_radius;
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
      if (m_params.precise)
	{
	  m_prec_red = (luminosity_t *)calloc (m_prec_width * m_prec_height * 2, sizeof (luminosity_t));
	  m_prec_green = (luminosity_t *)calloc (m_prec_width * m_prec_height * 2, sizeof (luminosity_t));
	  m_prec_blue = (luminosity_t *)calloc (m_prec_width * m_prec_height * 4, sizeof (luminosity_t));
	  luminosity_t *w_red = (luminosity_t *)calloc (m_prec_width * m_prec_height * 2, sizeof (luminosity_t));
	  luminosity_t *w_green = (luminosity_t *)calloc (m_prec_width * m_prec_height * 2, sizeof (luminosity_t));
	  luminosity_t *w_blue = (luminosity_t *)calloc (m_prec_width * m_prec_height * 4, sizeof (luminosity_t));

	  /* Determine region is image that is covered by screen.  */
	  int minx, maxx, miny, maxy;
	  coord_t dx, dy;
	  m_scr_to_img.to_img (-m_prec_xshift, -m_prec_yshift, &dx, &dy);
	  minx = maxx = dx;
	  miny = maxy = dy;
	  m_scr_to_img.to_img (-m_prec_xshift + m_prec_width, -m_prec_yshift, &dx, &dy);
	  minx = std::min ((int)dx, minx);
	  miny = std::min ((int)dy, miny);
	  maxx = std::max ((int)dx, maxx);
	  maxy = std::max ((int)dy, maxy);
	  m_scr_to_img.to_img (-m_prec_xshift, -m_prec_yshift + m_prec_height, &dx, &dy);
	  minx = std::min ((int)dx, minx);
	  miny = std::min ((int)dy, miny);
	  maxx = std::max ((int)dx, maxx);
	  maxy = std::max ((int)dy, maxy);
	  m_scr_to_img.to_img (-m_prec_xshift + m_prec_width, -m_prec_yshift + m_prec_height, &dx, &dy);
	  minx = std::min ((int)dx, minx);
	  miny = std::min ((int)dy, miny);
	  maxx = std::max ((int)dx, maxx);
	  maxy = std::max ((int)dy, maxy);

	  minx = std::max (minx, 0);
	  miny = std::max (miny, 0);
	  maxx = std::min (maxx, m_img.width);
	  maxy = std::min (maxy, m_img.height);

	  /* Collect luminosity of individual color patches.  */
#pragma omp parallel shared(w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
	  {
#pragma omp for 
	      for (int y = miny ; y < maxy; y++)
		for (int x = minx; x < maxx; x++)
		  {
		    coord_t scr_x, scr_y;
		    m_scr_to_img.to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
		    scr_x += m_prec_xshift;
		    scr_y += m_prec_yshift;
		    if (scr_x < 0 || scr_x >= m_prec_width - 1 || scr_y < 0 || scr_y > m_prec_height - 1)
		      continue;

		    luminosity_t l = fast_get_img_pixel (x, y);
		    int ix = (unsigned long long) round (scr_x * screen::size) & (unsigned)(screen::size - 1);
		    int iy = (unsigned long long) round (scr_y * screen::size) & (unsigned)(screen::size - 1);
		    if (m_screen->mult[iy][ix][0] > m_params.collection_threshold)
		      {
			coord_t xd, yd;
			to_diagonal_coordinates (scr_x + (coord_t)0.5, scr_y, &xd, &yd);
			xd = round (xd);
			yd = round (yd);
			int xx = ((int)xd + (int)yd) / 2;
			int yy = -(int)xd + (int)yd;
			if (xx >= 0 && xx < m_prec_width && yy >= 0 && yy < m_prec_height * 2)
			  {
#pragma omp atomic
			    prec_red (xx, yy) += m_screen->mult[iy][ix][0] * l;
#pragma omp atomic
			    w_red [yy * m_prec_width + xx] += m_screen->mult[iy][ix][0];
			  }
		      }
		    if (m_screen->mult[iy][ix][1] > m_params.collection_threshold)
		      {
			coord_t xd, yd;
			to_diagonal_coordinates (scr_x, scr_y, &xd, &yd);
			xd = round (xd);
			yd = round (yd);
			int xx = ((int)xd + (int)yd) / 2;
			int yy = -(int)xd + (int)yd;
			if (xx >= 0 && xx < m_prec_width && yy >= 0 && yy < m_prec_height * 2)
			  {
#pragma omp atomic
			    prec_green (xx, yy) += m_screen->mult[iy][ix][1] * l;
#pragma omp atomic
			    w_green [yy * m_prec_width + xx] += m_screen->mult[iy][ix][1];
			  }
		      }
		    if (l < 0)
		      l = 0;
		    if (m_screen->mult[iy][ix][2] > m_params.collection_threshold)
		      {
			int xx = roundf (2*(scr_x-(coord_t)0.25));
			int yy = roundf (2*(scr_y-(coord_t)0.25));
#pragma omp atomic
			prec_blue (xx, yy) += m_screen->mult[iy][ix][2] * l;
#pragma omp atomic
			w_blue [yy * m_prec_width * 2 + xx] += m_screen->mult[iy][ix][2];
		      }
		  }
	    {
#pragma omp for nowait
	      for (int y = 0; y < m_prec_height * 2; y++)
		for (int x = 0; x < m_prec_width; x++)
		  if (w_red [y * m_prec_width + x] != 0)
		    prec_red (x,y) /= w_red [y * m_prec_width + x];
#pragma omp for nowait
	      for (int y = 0; y < m_prec_height * 2; y++)
		for (int x = 0; x < m_prec_width; x++)
		  if (w_green [y * m_prec_width + x] != 0)
		    prec_green (x,y) /= w_green [y * m_prec_width + x];
#pragma omp for nowait
	      for (int y = 0; y < m_prec_height * 2; y++)
		for (int x = 0; x < m_prec_width * 2; x++)
		  if (w_blue [y * m_prec_width * 2 + x] != 0)
		    prec_blue (x,y) /= w_blue [y * m_prec_width * 2 + x];
	    }
	  }
	  free (w_red);
	  free (w_green);
	  free (w_blue);
	}
      else
	{
	  m_prec_red = (luminosity_t *)malloc (m_prec_width * m_prec_height * 2 * sizeof (luminosity_t));
	  m_prec_green = (luminosity_t *)malloc (m_prec_width * m_prec_height * 2 * sizeof (luminosity_t));
	  m_prec_blue = (luminosity_t *)malloc (m_prec_width * m_prec_height * 4 * sizeof (luminosity_t));
#define pixel(xo,yo,diag) m_params.precise && 0 ? sample_scr_diag_square ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo, diag)\
			 : get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
#pragma omp parallel for default (none)
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
    }
  else
    {
      /* G B .
	 R R .
	 . . .  */
      m_prec_red = (luminosity_t *)malloc (m_prec_width * m_prec_height * sizeof (luminosity_t) * 2);
      m_prec_green = (luminosity_t *)malloc (m_prec_width * m_prec_height * sizeof (luminosity_t));
      m_prec_blue = (luminosity_t *)malloc (m_prec_width * m_prec_height * sizeof (luminosity_t));
#define pixel(xo,yo,width,height) m_params.precise ? sample_scr_square ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo, width, height)\
			 : get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
#pragma omp parallel for default (none)
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
render_interpolate::render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b)
{
  luminosity_t red, green, blue;
  x += m_prec_xshift;
  y += m_prec_yshift;

  if (m_scr_to_img.get_type () != Dufay)
    {
      coord_t xx = 2*(x-0.25);
      coord_t yy = 2*(y-0.25);
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_blue(xx, yy) prec_blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue

      coord_t xd, yd;
      to_diagonal_coordinates (x, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);

#define get_green(xx, yy) prec_diag_green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);
#define get_red(xx, yy) prec_diag_red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
    }
  else
    {
      coord_t xx = 2*(x);
      coord_t yy = y-0.5;
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_red(xx, yy) dufay_prec_red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
      xx = x;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_green(xx, yy) dufay_prec_green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      xx = x-0.5;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_blue(xx, yy) dufay_prec_blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue
    }
  if (m_params.screen_compensation)
    {
      coord_t lum = get_img_pixel_scr (x - m_prec_xshift, y - m_prec_yshift);
      int ix = (long long) round ((x - m_prec_xshift) * screen::size) & (screen::size - 1);
      int iy = (long long) round ((y - m_prec_yshift) * screen::size) & (screen::size - 1);
      luminosity_t sr = m_screen->mult[iy][ix][0];
      luminosity_t sg = m_screen->mult[iy][ix][1];
      luminosity_t sb = m_screen->mult[iy][ix][2];
      luminosity_t llum = red * sr + green * sg + blue * sb;
      luminosity_t correction = std::max (std::min (lum / llum, (luminosity_t)5.0), (luminosity_t)0.0);
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
  else if (m_params.adjust_luminosity)
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
