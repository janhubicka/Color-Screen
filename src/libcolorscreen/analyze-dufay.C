#include "include/analyze-dufay.h"
#include "include/screen.h"

bool flatten_attr
analyze_dufay::analyze (render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, bool precise, luminosity_t collection_threshold, progress_info *progress)
{
  assert (!m_red);
  m_width = width;
  m_height = height;
  m_xshift = xshift;
  m_yshift = yshift;
  /* G B .
     R R .
     . . .  */
  m_red = (luminosity_t *)calloc (m_width * m_height * 2,  sizeof (luminosity_t));
  m_green = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
  m_blue = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
  if (!m_red || !m_green || !m_blue)
    return false;
  if (precise)
    {
      luminosity_t *w_red = (luminosity_t *)calloc (m_width * m_height * 2, sizeof (luminosity_t));
      luminosity_t *w_green = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
      luminosity_t *w_blue = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
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
	progress->set_task ("determining intensities of Dufay screen patches (precise mode)", maxy - miny + m_height * 2 * 3);

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
			int xx = nearest_int (scr_x * 2 - 0.5);
			int yy = nearest_int (scr_y - 0.5);
#pragma omp atomic
			red (xx, yy) += (screen->mult[iy][ix][2] - collection_threshold) * l;
#pragma omp atomic
			w_red [yy * m_width * 2 + xx] += (screen->mult[iy][ix][2] - collection_threshold);
		      }
		    if (screen->mult[iy][ix][1] > collection_threshold)
		      {
			int xx = nearest_int (scr_x);
			int yy = nearest_int (scr_y);
#pragma omp atomic
			green (xx, yy) += (screen->mult[iy][ix][2] - collection_threshold) * l;
#pragma omp atomic
			w_green [yy * m_width + xx] += (screen->mult[iy][ix][2] - collection_threshold);
		      }
		    if (screen->mult[iy][ix][2] > collection_threshold)
		      {
			int xx = nearest_int (scr_x-(coord_t)0.5);
			int yy = nearest_int (scr_y);
#pragma omp atomic
			blue (xx, yy) += (screen->mult[iy][ix][2] - collection_threshold) * l;
#pragma omp atomic
			w_blue [yy * m_width + xx] += (screen->mult[iy][ix][2] - collection_threshold);
		      }
		  }
	      if (progress)
		progress->inc_progress ();
	    }
      if (!progress || !progress->cancel_requested ())
	{
#pragma omp for nowait
	  for (int y = 0; y < m_height; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width * 2; x++)
		  if (w_red [y * m_width * 2 + x] != 0)
		    red (x,y) /= w_red [y * m_width * 2 + x];
	      if (progress)
		progress->inc_progress ();
	    }
#pragma omp for nowait
	  for (int y = 0; y < m_height; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width; x++)
		  if (w_green [y * m_width + x] != 0)
		    green (x,y) /= w_green [y * m_width + x];
	      if (progress)
		progress->inc_progress ();
	    }
#pragma omp for nowait
	  for (int y = 0; y < m_height; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < m_width; x++)
		  if (w_blue [y * m_width + x] != 0)
		    blue (x,y) /= w_blue [y * m_width + x];
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
	progress->set_task ("determining intensities of Dufay screen patches (fast mode)", m_height);
#define pixel(xo,yo,width,height) precise ? render->sample_scr_square ((x - m_xshift) + xo, (y - m_yshift) + yo, width, height)\
		     : render->get_img_pixel_scr ((x - m_xshift) + xo, (y - m_yshift) + yo)
#pragma omp parallel for default (none) shared (precise,render,progress)
      for (int x = 0; x < m_width; x++)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int y = 0 ; y < m_height; y++)
	      {
		red (2 * x, y) = pixel (0.25, 0.5, 0.5, 0.5);
		red (2 * x + 1, y) = pixel (0.75, 0.5,0.5, 0.5);
		green (x, y) = pixel (0, 0, 0.5, 0.5);
		blue (x, y) = pixel (0.5, 0, 0.5, 0.5);
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    }
#undef pixel
  return !progress || !progress->cancelled ();
}

bool
analyze_dufay::analyze_contrast (render_to_scr *render, image_data *img, scr_to_img *scr_to_img, progress_info *progress)
{
  m_contrast = (contrast_info *)malloc (m_width * m_height * sizeof (contrast_info));
  if (!m_contrast)
    return 0;
  if (progress)
    progress->set_task ("collecting contrast info", img->height);
#pragma omp parallel for default (none) 
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
	get_contrast (x,y).min = 10000;
	get_contrast (x,y).max = -10000;
      }
//#pragma omp parallel for default (none) shared (render,img,scr_to_img,progress)
  for (int y = 0 ; y < img->height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < img->width; x++)
	  {
	    coord_t scr_x, scr_y;
	    scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
	    int ix = floor (scr_x);
	    int iy = floor (scr_y);
	    ix += m_xshift;
	    iy += m_yshift;
	    if (ix >= 0 && ix < m_width && iy >= 0 && iy < m_height)
	      {
		luminosity_t d = render->get_data_red (x, y);
//#pragma omp critical
		{
		  get_contrast (ix,iy).min = std::min (get_contrast (ix,iy).min, d);
		  get_contrast (ix,iy).max = std::max (get_contrast (ix,iy).max, d);
		}
	      }
	  }
	if (progress)
	  progress->inc_progress ();
    }
  return !progress || !progress->cancelled ();
}

luminosity_t
analyze_dufay::compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress)
{
  const int tile_size = 10;
  luminosity_t max_ratio = 0;
  int maxx = 0, maxy = 0;
  if (progress)
    progress->set_task ("comparing contrast", m_height);
  for (int y = 0; y < m_height - tile_size; y++)
    {
      int y2 = y - m_yshift - ypos + other.m_yshift;
      if (y2 >= 0 && y2 < other.m_height - tile_size)
        for (int x = 0; x < m_width - tile_size; x++)
	  {
	    int x2 = x - m_xshift - xpos + other.m_xshift;
	    if (x2 >= 0 && x2 < other.m_width - tile_size)
	      {
		bool skip = false;
#if 1
		luminosity_t wsum = 0;
		luminosity_t ratsum1 = 0, ratsum2 = 0;
#endif
		luminosity_t diffsum1 = 0, diffsum2 = 0, rsum1 = 0, rsum2 = 0;
		const luminosity_t threshold = 0.1;
		const luminosity_t minthreshold = 0.01;
		int n = 0;
		for (int yy = 0; yy < tile_size && !skip; yy++)
		  for (int xx = 0; xx < tile_size && !skip; xx++)
		    {
		      if (!m_known_pixels->test_bit (x+xx, y+yy)
			  || !other.m_known_pixels->test_bit (x2+xx, y2+yy))
			skip = true;
		      else
			{
			  diffsum1 += get_contrast (x + xx, y + yy).max - get_contrast (x + xx, y + yy).min;
			  diffsum2 += other.get_contrast (x2 + xx, y2 + yy).max - other.get_contrast (x2 + xx, y2 + yy).min;
			  rsum1 += red ((x + xx)*2, y + yy);
			  rsum2 += other.red ((x2 + xx)*2, y2 + yy);
			  if (get_contrast (x + xx, y + yy).max > threshold && other.get_contrast (x2 + xx, y2 + yy).max > threshold
			      && get_contrast (x + xx, y + yy).min > minthreshold && other.get_contrast (x2 + xx, y2 + yy).min > minthreshold)
			    {
				n++;
				luminosity_t w =  /*= get_contrast (x + xx, y + yy).max*/ 1;
				//luminosity_t ratio = (get_contrast (x + xx, y + yy).max - get_contrast (x + xx, y + yy).min) / (other.get_contrast (x2 + xx, y2 + yy).max - other.get_contrast (x2 + xx, y2 + yy).min);
				luminosity_t ratio1 = (get_contrast (x + xx, y + yy).max / get_contrast (x + xx, y + yy).min);
				luminosity_t ratio2 = (other.get_contrast (x2 + xx, y2 + yy).max / other.get_contrast (x2 + xx, y2 + yy).min);
				wsum += w;
				ratsum1 += ratio1 * w;
				ratsum2 += ratio2 * w;
			    }
			}
		    }
		if (!skip && n > tile_size * tile_size / 10)
		  {
		    //luminosity_t ratio = rsum / wsum;
		    luminosity_t ratio = ratsum1 / ratsum2;
		    //luminosity_t ratio = (diffsum1/rsum1) / (diffsum2/rsum2);
		    if (ratio < 1)
		      ratio = 1 / ratio;
		    if (ratio > max_ratio)
		      {
			max_ratio = ratio;
			maxx = x;
			maxy = y;
		      }
		  }
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  if (!max_ratio)
    return -1;
  coord_t xi, yi;
  map.to_img (maxx + tile_size / 2 - m_xshift, maxy + tile_size / 2 - m_yshift, &xi, &yi);
  *x1 = xi;
  *y1 = yi;
  other_map.to_img (maxx + tile_size / 2 - m_xshift -xpos, maxy + tile_size / 2 - m_yshift - ypos, &xi, &yi);
  *x2 = xi;
  *y2 = yi;
  return max_ratio;
}
