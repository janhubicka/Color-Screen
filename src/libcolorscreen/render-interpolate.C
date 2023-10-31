#include <assert.h>
#include "render-interpolate.h"

render_interpolate::render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval, bool screen_compensation, bool adjust_luminosity)
   : render_to_scr (param, img, rparam, dst_maxval), m_screen (NULL), m_screen_compensation (screen_compensation), m_adjust_luminosity (adjust_luminosity)
{
}

flatten_attr bool
render_interpolate::precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress)
{
  if (!render_to_scr::precompute (true, xmin, ymin, xmax, ymax, progress))
    return false;
  if (m_screen_compensation || m_params.precise)
    {
      coord_t radius = m_params.screen_blur_radius * pixel_size ();
      m_screen = get_screen (m_scr_to_img.get_type (), false, radius, progress);
      if (!m_screen)
	return false;
    }
  /* We need to compute bit more to get interpolation right.
     TODO: figure out how much.  */
  int xshift = -(xmin - 4);
  int yshift = -(ymin - 4);
  int width = xmax - xmin + 8;
  int height = ymax - ymin + 8;
  if (m_scr_to_img.get_type () != Dufay)
    {
      if (!m_paget.analyze (this, &m_img, &m_scr_to_img, m_screen, width, height, xshift, yshift, m_params.precise, m_params.collection_threshold, progress))
	return false;
    }
  else
    if (!m_dufay.analyze (this, &m_img, &m_scr_to_img, m_screen, width, height, xshift, yshift, m_params.precise, m_params.collection_threshold, progress))
      return false;
  return !progress || !progress->cancelled ();
}

flatten_attr void
render_interpolate::render_pixel_scr_int (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b, luminosity_t *lum)
{
  luminosity_t red, green, blue;
  int xshift, yshift;

  if (m_scr_to_img.get_type () != Dufay)
    {
      xshift = m_paget.get_xshift ();
      yshift = m_paget.get_yshift ();
      x += xshift;
      y += yshift;
      coord_t xx = 2*(x-0.25);
      coord_t yy = 2*(y-0.25);
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_blue(xx, yy) m_paget.blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue

      coord_t xd, yd;
      analyze_paget::to_diagonal_coordinates (x, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);

#define get_green(xx, yy) m_paget.diag_green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      analyze_paget::to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);
#define get_red(xx, yy) m_paget.diag_red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
    }
  else
    {
      xshift = m_dufay.get_xshift ();
      yshift = m_dufay.get_yshift ();
      x += xshift;
      y += yshift;
      coord_t xx = 2*(x - 0.25);
      coord_t yy = y-0.5;
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_red(xx, yy) m_dufay.red (xp + (xx), yp + (yy))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
      xx = x;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_green(xx, yy) m_dufay.green (xp + (xx), yp + (yy))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      xx = x-0.5;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_blue(xx, yy) m_dufay.blue (xp + (xx), yp + (yy))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue
    }
  if (m_screen_compensation)
    {
      coord_t lum = get_img_pixel_scr (x - xshift, y - yshift);
      int ix = (long long) nearest_int ((x - xshift) * screen::size) & (screen::size - 1);
      int iy = (long long) nearest_int ((y - yshift) * screen::size) & (screen::size - 1);
      luminosity_t sr = m_screen->mult[iy][ix][0];
      luminosity_t sg = m_screen->mult[iy][ix][1];
      luminosity_t sb = m_screen->mult[iy][ix][2];

      red = std::max (red, (luminosity_t)0);
      green = std::max (green, (luminosity_t)0);
      blue = std::max (blue, (luminosity_t)0);

      luminosity_t llum = red * sr + green * sg + blue * sb;
      luminosity_t correction = llum ? lum / llum : lum * 100;

#if 1
      luminosity_t redmin = lum - (1 - sr);
      luminosity_t redmax = lum + (1 - sr);
      if (red * correction < redmin)
	correction = redmin / red;
      else if (red * correction > redmax)
	correction = redmax / red;

      luminosity_t greenmin = lum - (1 - sg);
      luminosity_t greenmax = lum + (1 - sg);
      if (green * correction < greenmin)
	correction = greenmin / green;
      else if (green * correction > greenmax)
	correction = greenmax / green;

      luminosity_t bluemin = lum - (1 - sb);
      luminosity_t bluemax = lum + (1 - sb);
      if (blue * correction < bluemin)
	correction = bluemin / blue;
      else if (blue * correction > bluemax)
	correction = bluemax / blue;
#endif
      correction = std::max (std::min (correction, (luminosity_t)5.0), (luminosity_t)0.0);

      *r = red * correction;
      *g = green * correction;
      *b = blue * correction;
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
  else if (m_adjust_luminosity && lum)
    {
      *r = red;
      *g = green;
      *b = blue;
      *lum = get_img_pixel_scr (x - xshift, y - yshift);
    }
  else
    {
      *r = red;
      *g = green;
      *b = blue;
      //set_color (red, green, blue, r, g, b);
    }
}
flatten_attr void
render_interpolate::render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b)
{
  luminosity_t red, green, blue, lum = 0;
  render_pixel_scr_int (x, y, &red, &green, &blue, &lum);
  if (m_adjust_luminosity)
    set_color_luminosity (red, green, blue, lum, r, g, b);
  else
    set_color (red, green, blue, r, g, b);
}
flatten_attr void
render_interpolate::render_hdr_pixel_scr (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  luminosity_t red, green, blue, lum = 0;
  render_pixel_scr_int (x, y, &red, &green, &blue, &lum);
  /* TODO: Implement or drop.  */
#if 0
  if (m_adjust_luminosity)
    set_color_luminosity (red, green, blue, lum, r, g, b);
  else
#endif
    set_hdr_color (red, green, blue, r, g, b);
}

render_interpolate::~render_interpolate ()
{
  if (m_screen)
    release_screen (m_screen);
}
