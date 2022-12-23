#include "analyze-paget.h"
#include "include/screen.h"
bool
analyze_paget::analyze (render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, bool precise, luminosity_t collection_threshold, progress_info *progress)
{
  assert (!m_red);
  m_width = width;
  m_height = height;
  m_xshift = xshift;
  m_yshift = yshift;
  m_red = (luminosity_t *)calloc (m_width * m_height * 2, sizeof (luminosity_t));
  m_green = (luminosity_t *)calloc (m_width * m_height * 2, sizeof (luminosity_t));
  m_blue = (luminosity_t *)calloc (m_width * m_height * 4, sizeof (luminosity_t));
  if (!m_red || !m_green || !m_blue)
    return false;
  /* Thames, Finlay and Paget screen are organized as follows:
    
     G   R   .
       B   B
     R   G   .
       B   B
     .   .   .  
     2 reds and greens per one screen tile while there are 4 blues.  */
  if (precise)
    {
      luminosity_t *w_red = (luminosity_t *)calloc (m_width * m_height * 2, sizeof (luminosity_t));
      luminosity_t *w_green = (luminosity_t *)calloc (m_width * m_height * 2, sizeof (luminosity_t));
      luminosity_t *w_blue = (luminosity_t *)calloc (m_width * m_height * 4, sizeof (luminosity_t));
      if (!w_red || !w_green || !w_blue)
	{
	  free (w_red);
	  free (w_green);
	  free (w_blue);
	  return false;
	}

      /* Determine region is image that is covered by screen.  */
      int minx, maxx, miny, maxy;
      coord_t dx, dy;
      scr_to_img->to_img (-m_xshift, -m_yshift, &dx, &dy);
      minx = maxx = dx;
      miny = maxy = dy;
      scr_to_img->to_img (-m_xshift + m_width, -m_yshift, &dx, &dy);
      minx = std::min ((int)dx, minx);
      miny = std::min ((int)dy, miny);
      maxx = std::max ((int)dx, maxx);
      maxy = std::max ((int)dy, maxy);
      scr_to_img->to_img (-m_xshift, -m_yshift + m_height, &dx, &dy);
      minx = std::min ((int)dx, minx);
      miny = std::min ((int)dy, miny);
      maxx = std::max ((int)dx, maxx);
      maxy = std::max ((int)dy, maxy);
      scr_to_img->to_img (-m_xshift + m_width, -m_yshift + m_height, &dx, &dy);
      minx = std::min ((int)dx, minx);
      miny = std::min ((int)dy, miny);
      maxx = std::max ((int)dx, maxx);
      maxy = std::max ((int)dy, maxy);

      minx = std::max (minx, 0);
      miny = std::max (miny, 0);
      maxx = std::min (maxx, img->width);
      maxy = std::min (maxy, img->height);

      if (progress)
	progress->set_task ("determining colors", maxy - miny + m_height * 2 * 3);

      /* Collect luminosity of individual color patches.  */
#pragma omp parallel shared(progress, render, scr_to_img, screen, collection_threshold, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
      {
#pragma omp for 
	  for (int y = miny ; y < maxy; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = minx; x < maxx; x++)
		  {
		    coord_t scr_x, scr_y;
		    scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
		    scr_x += m_xshift;
		    scr_y += m_yshift;
		    if (scr_x < 0 || scr_x >= m_width - 1 || scr_y < 0 || scr_y > m_height - 1)
		      continue;

		    luminosity_t l = render->fast_get_img_pixel (x, y);
		    int ix = (unsigned long long) nearest_int (scr_x * screen::size) & (unsigned)(screen::size - 1);
		    int iy = (unsigned long long) nearest_int (scr_y * screen::size) & (unsigned)(screen::size - 1);
		    if (screen->mult[iy][ix][0] > collection_threshold)
		      {
			coord_t xd, yd;
			to_diagonal_coordinates (scr_x + (coord_t)0.5, scr_y, &xd, &yd);
			xd = nearest_int (xd);
			yd = nearest_int (yd);
			int xx = ((int)xd + (int)yd) / 2;
			int yy = -(int)xd + (int)yd;
			if (xx >= 0 && xx < m_width && yy >= 0 && yy < m_height * 2)
			  {
#pragma omp atomic
			    red (xx, yy) += (screen->mult[iy][ix][0] - collection_threshold) * l;
#pragma omp atomic
			    w_red [yy * m_width + xx] += screen->mult[iy][ix][0] - collection_threshold;
			  }
		      }
		    if (screen->mult[iy][ix][1] > collection_threshold)
		      {
			coord_t xd, yd;
			to_diagonal_coordinates (scr_x, scr_y, &xd, &yd);
			xd = nearest_int (xd);
			yd = nearest_int (yd);
			int xx = ((int)xd + (int)yd) / 2;
			int yy = -(int)xd + (int)yd;
			if (xx >= 0 && xx < m_width && yy >= 0 && yy < m_height * 2)
			  {
#pragma omp atomic
			    green (xx, yy) += (screen->mult[iy][ix][1] - collection_threshold) * l;
#pragma omp atomic
			    w_green [yy * m_width + xx] += (screen->mult[iy][ix][1] - collection_threshold);
			  }
		      }
		    if (screen->mult[iy][ix][2] > collection_threshold)
		      {
			int xx = nearest_int (2*(scr_x-(coord_t)0.25));
			int yy = nearest_int (2*(scr_y-(coord_t)0.25));
#pragma omp atomic
			blue (xx, yy) += (screen->mult[iy][ix][2] - collection_threshold) * l;
#pragma omp atomic
			w_blue [yy * m_width * 2 + xx] += (screen->mult[iy][ix][2] - collection_threshold);
		      }
		  }
	      if (progress)
		progress->inc_progress ();
	    }
      if (!progress || !progress->cancel_requested ())
	{
#pragma omp for nowait
	  for (int y = 0; y < m_height * 2; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width; x++)
		  if (w_red [y * m_width + x] != 0)
		    red (x,y) /= w_red [y * m_width + x];
	      if (progress)
		progress->inc_progress ();
	    }
#pragma omp for nowait
	  for (int y = 0; y < m_height * 2; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width; x++)
		  if (w_green [y * m_width + x] != 0)
		    green (x,y) /= w_green [y * m_width + x];
	      if (progress)
		progress->inc_progress ();
	    }
#pragma omp for nowait
	  for (int y = 0; y < m_height * 2; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width * 2; x++)
		  if (w_blue [y * m_width * 2 + x] != 0)
		    blue (x,y) /= w_blue [y * m_width * 2 + x];
	      if (progress)
		progress->inc_progress ();
	    }
	}
      }
      free (w_red);
      free (w_green);
      free (w_blue);
    }
  else
    {
      if (progress)
	progress->set_task ("determining colors", m_height);
#define pixel(xo,yo,diag) render->get_img_pixel_scr ((x - m_xshift) + xo, (y - m_yshift) + yo)
#pragma omp parallel for default (none) shared (progress, render)
      for (int x = 0; x < m_width; x++)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int y = 0 ; y < m_height; y++)
	      {
		red (x, 2 * y) = pixel (-0.5, 0, 0.5);
		red (x, 2 * y + 1) = pixel (0, 0.5, 0.5);
		green (x, 2 * y) = pixel (0.0, 0, 0.5);
		green (x, 2 * y + 1) = pixel (0.5, 0.5, 0.5);
		blue (2 * x, 2 * y) = pixel (0.25, 0.25, 0.3);
		blue (2 * x + 1, 2 * y) = pixel (0.75, 0.25, 0.3);
		blue (2 * x, 2 * y + 1) = pixel (0.25, 0.75, 0.3);
		blue (2 * x + 1, 2 * y + 1) = pixel (0.75, 0.75, 0.3);
	      }
	  if (progress)
	    progress->inc_progress ();
	}
#undef pixel
    }
  return !progress || !progress->cancelled ();
}
