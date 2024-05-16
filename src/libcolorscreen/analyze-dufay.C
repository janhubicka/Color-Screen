#include "include/analyze-dufay.h"
#include "include/screen.h"

/* Collect luminosity of individual color patches.
   Function is flattened so it should do only necessary work.  */
bool flatten_attr
analyze_dufay::analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
{
#pragma omp parallel shared(progress, render, scr_to_img, screen, collection_threshold, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
  {
#pragma omp for 
    for (int y = miny ; y < maxy; y++)
      {
	if (!progress || !progress->cancel_requested ())
#pragma omp simd
	  for (int x = minx; x < maxx; x++)
	    {
	      coord_t scr_x, scr_y;
	      scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
	      scr_x += m_xshift;
	      scr_y += m_yshift;
	      if (scr_x < 0 || scr_x > m_width - 1 || scr_y < 0 || scr_y > m_height - 1)
		continue;

	      luminosity_t l = render->get_unadjusted_data (x, y);
	      int ix = (uint64_t) nearest_int (scr_x * screen::size) & (unsigned)(screen::size - 1);
	      int iy = (uint64_t) nearest_int (scr_y * screen::size) & (unsigned)(screen::size - 1);
	      if (screen->mult[iy][ix][0] > collection_threshold)
		{
		  int xx = nearest_int (scr_x * 2 - (coord_t)0.5);
		  int yy = nearest_int (scr_y - (coord_t)0.5);
		  luminosity_t val = (screen->mult[iy][ix][0] - collection_threshold);
		  red_atomic_add (xx, yy, val * l);
		  luminosity_t &l = w_red [yy * m_width * 2 + xx];
#pragma omp atomic
		  l += val;
		}
	      if (screen->mult[iy][ix][1] > collection_threshold)
		{
		  int xx = nearest_int (scr_x);
		  int yy = nearest_int (scr_y);
		  luminosity_t val = (screen->mult[iy][ix][1] - collection_threshold);
		  green_atomic_add (xx, yy, val * l);
		  luminosity_t &l = w_green [yy * m_width + xx];
#pragma omp atomic
		  l += val;
		}
	      if (screen->mult[iy][ix][2] > collection_threshold)
		{
		  int xx = nearest_int (scr_x-(coord_t)0.5);
		  int yy = nearest_int (scr_y);
		  luminosity_t val = (screen->mult[iy][ix][2] - collection_threshold);
		  blue_atomic_add (xx, yy, val * l);
		  luminosity_t &l = w_blue [yy * m_width + xx];
#pragma omp atomic
		  l += val;
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
#pragma omp simd
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
#pragma omp simd
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
#pragma omp simd
	    for (int x = 0; x < m_width; x++)
	      if (w_blue [y * m_width + x] != 0)
		blue (x,y) /= w_blue [y * m_width + x];
	  if (progress)
	    progress->inc_progress ();
	}
    }
  }
  return !progress || !progress->cancelled ();
}
/* Collect luminosity of individual color patches.
   Function is flattened so it should do only necessary work.  */
bool flatten_attr
analyze_dufay::analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
{
#pragma omp parallel shared(progress, render, scr_to_img, screen, collection_threshold, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
  {
#pragma omp for 
    for (int y = miny ; y < maxy; y++)
      {
	if (!progress || !progress->cancel_requested ())
#pragma omp simd
	  for (int x = minx; x < maxx; x++)
	    {
	      coord_t scr_x, scr_y;
	      scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
	      scr_x += m_xshift;
	      scr_y += m_yshift;
	      if (scr_x < 0 || scr_x > m_width - 1 || scr_y < 0 || scr_y > m_height - 1)
		continue;

	      rgbdata d = render->get_unadjusted_rgb_pixel (x, y);
	      int ix = (uint64_t) nearest_int (scr_x * screen::size) & (unsigned)(screen::size - 1);
	      int iy = (uint64_t) nearest_int (scr_y * screen::size) & (unsigned)(screen::size - 1);
	      if (screen->mult[iy][ix][0] > collection_threshold)
		{
		  int xx = nearest_int (scr_x * 2 - (coord_t)0.5);
		  int yy = nearest_int (scr_y - (coord_t)0.5);
		  luminosity_t val = (screen->mult[iy][ix][0] - collection_threshold);
		  rgb_red_atomic_add (xx, yy, d * val);
		  luminosity_t &l = w_red [yy * m_width * 2 + xx];
#pragma omp atomic
		  l += val;
		}
	      if (screen->mult[iy][ix][1] > collection_threshold)
		{
		  int xx = nearest_int (scr_x);
		  int yy = nearest_int (scr_y);
		  luminosity_t val = (screen->mult[iy][ix][1] - collection_threshold);
		  rgb_green_atomic_add (xx, yy, d * val);
		  luminosity_t &l = w_green [yy * m_width + xx];
#pragma omp atomic
		  l += val;
		}
	      if (screen->mult[iy][ix][2] > collection_threshold)
		{
		  int xx = nearest_int (scr_x-(coord_t)0.5);
		  int yy = nearest_int (scr_y);
		  luminosity_t val = (screen->mult[iy][ix][2] - collection_threshold);
		  rgb_blue_atomic_add (xx, yy, d * val);
		  luminosity_t &l = w_blue [yy * m_width + xx];
#pragma omp atomic
		  l += val;
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
#pragma omp simd
	    for (int x = 0; x < m_width * 2; x++)
	      if (w_red [y * m_width * 2 + x] != 0)
		rgb_red (x,y) *= 1 / w_red [y * m_width * 2 + x];
	  if (progress)
	    progress->inc_progress ();
	}
#pragma omp for nowait
      for (int y = 0; y < m_height; y++)
	{
	  if (!progress || !progress->cancel_requested ())
#pragma omp simd
	    for (int x = 0; x < m_width; x++)
	      if (w_green [y * m_width + x] != 0)
		rgb_green (x,y) *= 1 / w_green [y * m_width + x];
	  if (progress)
	    progress->inc_progress ();
	}
#pragma omp for nowait
      for (int y = 0; y < m_height; y++)
	{
	  if (!progress || !progress->cancel_requested ())
#pragma omp simd
	    for (int x = 0; x < m_width; x++)
	      if (w_blue [y * m_width + x] != 0)
		rgb_blue (x,y) *= 1 / w_blue [y * m_width + x];
	  if (progress)
	    progress->inc_progress ();
	}
    }
  }
  return !progress || !progress->cancelled ();
}
bool flatten_attr
analyze_dufay::analyze_fast (render_to_scr *render,progress_info *progress)
{
  /* Old precise code is always disabled.  */
  const bool precise = false;
#define pixel(xo,yo,width,height) precise ? render->sample_scr_square ((x - m_xshift) + xo, (y - m_yshift) + yo, width, height)\
		     : render->get_unadjusted_img_pixel_scr ((x - m_xshift) + xo, (y - m_yshift) + yo)
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
#undef pixel
  return !progress || !progress->cancelled ();
}
/* Collect luminosity of individual color patches.
   Function is flattened so it should do only necessary work.  */
bool flatten_attr
analyze_dufay::analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
{
  luminosity_t weights[256];
  luminosity_t half_weights[256];
  coord_t pixel_size = render->pixel_size ();
  coord_t left = 128 - (pixel_size / 2) * 256;
  coord_t right = 128 + (pixel_size / 2) * 256;
  coord_t half_left = 128 - (pixel_size / 4) * 256;
  coord_t half_right = 128 + (pixel_size / 4) * 256;
  for (int i = 0; i < 256; i++)
    {
      if (i <= left)
	weights[i] = 0;
      else if (i >= right)
	weights[i] = 1;
      else
	weights[i] = (i - left) / (right - left);

      if (i <= half_left)
	half_weights[i] = 0;
      else if (i >= half_right)
	half_weights[i] = 1;
      else
	half_weights[i] = (i - half_left) / (half_right - half_left);
    }
#pragma omp parallel shared(progress, render, scr_to_img, w_blue, w_red, w_green, minx, miny, maxx, maxy, weights, half_weights) default(none)
  {
#pragma omp for 
    for (int y = miny ; y < maxy; y++)
      {
	if (!progress || !progress->cancel_requested ())
#pragma omp simd
	  for (int x = minx; x < maxx; x++)
	    {
	      rgbdata d = render->get_unadjusted_rgb_pixel (x, y);
	      coord_t scr_x, scr_y;
	      scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
	      scr_x += m_xshift;
	      scr_y += m_yshift;
	      if (scr_x < 1 || scr_x >= m_width - 2 || scr_y < 1 || scr_y >= m_height - 2)
		continue;

	      int xx, yy;
	      coord_t xxs = my_modf (scr_x * 2 - (coord_t)0.5, &xx);
	      coord_t yys = my_modf (scr_y - (coord_t)0.5, &yy);
	      xxs = half_weights[(int)(xxs * 255.5)];
	      yys = weights[(int)(yys * 255.5)];
	      luminosity_t &lr1 = w_red [(yy) * m_width * 2 + xx];
	      luminosity_t &lr2 = w_red [(yy) * m_width * 2 + xx + 1];
	      luminosity_t &lr3 = w_red [(yy + 1) * m_width * 2 + xx];
	      luminosity_t &lr4 = w_red [(yy + 1) * m_width * 2 + xx + 1];
	      luminosity_t val1 = (1 - xxs) * (1 - yys);
	      luminosity_t val2 = (xxs) * (1 - yys);
	      luminosity_t val3 = (1 - xxs) * (yys);
	      luminosity_t val4 = (xxs) * (yys);
	      red_atomic_add (xx, yy, d.red * val1);
	      red_atomic_add (xx + 1, yy, d.red * val2);
	      red_atomic_add (xx, yy + 1, d.red * val3);
	      red_atomic_add (xx + 1, yy + 1, d.red * val4);
#pragma omp atomic
	      lr1 += val1;
#pragma omp atomic
	      lr2 += val2;
#pragma omp atomic
	      lr3 += val3;
#pragma omp atomic
	      lr4 += val4;


	      xxs = my_modf (scr_x, &xx);
	      yys = my_modf (scr_y, &yy);
	      xxs = weights[(int)(xxs * 255.5)];
	      yys = weights[(int)(yys * 255.5)];
	      luminosity_t &lg1 = w_green [yy * m_width + xx];
	      luminosity_t &lg2 = w_green [yy * m_width + xx + 1];
	      luminosity_t &lg3 = w_green [(yy + 1) * m_width + xx];
	      luminosity_t &lg4 = w_green [(yy + 1) * m_width + xx + 1];
	      val1 = (1 - xxs) * (1 - yys);
	      val2 = (xxs) * (1 - yys);
	      val3 = (1 - xxs) * (yys);
	      val4 = (xxs) * (yys);
	      green_atomic_add (xx, yy, d.green * val1);
	      green_atomic_add (xx + 1, yy, d.green * val2);
	      green_atomic_add (xx, yy + 1, d.green * val3);
	      green_atomic_add (xx + 1, yy + 1, d.green * val4);
#pragma omp atomic
	      lg1 += val1;
#pragma omp atomic
	      lg2 += val2;
#pragma omp atomic
	      lg3 += val3;
#pragma omp atomic
	      lg4 += val4;


	      xxs = my_modf (scr_x-(coord_t)0.5, &xx);
	      xxs = weights[(int)(xxs * 255.5)];
	      luminosity_t &lb1 = w_blue [yy * m_width + xx];
	      luminosity_t &lb2 = w_blue [yy * m_width + xx + 1];
	      luminosity_t &lb3 = w_blue [(yy + 1) * m_width + xx];
	      luminosity_t &lb4 = w_blue [(yy + 1) * m_width + xx + 1];
	      val1 = (1 - xxs) * (1 - yys);
	      val2 = (xxs) * (1 - yys);
	      val3 = (1 - xxs) * (yys);
	      val4 = (xxs) * (yys);
	      blue_atomic_add (xx, yy, d.blue * val1);
	      blue_atomic_add (xx + 1, yy, d.blue * val2);
	      blue_atomic_add (xx, yy + 1, d.blue * val3);
	      blue_atomic_add (xx + 1, yy + 1, d.blue * val4);
#pragma omp atomic
	      lb1 += val1;
#pragma omp atomic
	      lb2 += val2;
#pragma omp atomic
	      lb3 += val3;
#pragma omp atomic
	      lb4 += val4;

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
#pragma omp simd
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
#pragma omp simd
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
#pragma omp simd
	    for (int x = 0; x < m_width; x++)
	      if (w_blue [y * m_width + x] != 0)
		blue (x,y) /= w_blue [y * m_width + x];
	  if (progress)
	    progress->inc_progress ();
	}
    }
  }
  return !progress || !progress->cancelled ();
}

bool
analyze_dufay::analyze (render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, mode mode, luminosity_t collection_threshold, progress_info *progress)
{
  assert (!m_red);
  m_width = width;
  m_height = height;
  m_xshift = xshift;
  m_yshift = yshift;
  /* G B .
     R R .
     . . .  */
  if (mode != precise_rgb)
    {
      m_red = (luminosity_t *)calloc (m_width * m_height * 2,  sizeof (luminosity_t));
      m_green = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
      m_blue = (luminosity_t *)calloc (m_width * m_height, sizeof (luminosity_t));
      if (!m_red || !m_green || !m_blue)
	return false;
    }
  else
    {
      m_rgb_red = (rgbdata *)calloc (m_width * m_height * 2,  sizeof (rgbdata));
      m_rgb_green = (rgbdata *)calloc (m_width * m_height, sizeof (rgbdata));
      m_rgb_blue = (rgbdata *)calloc (m_width * m_height, sizeof (rgbdata));
      if (!m_rgb_red || !m_rgb_green || !m_rgb_blue)
	return false;
    }
  if (mode == precise || mode == precise_rgb || mode == color)
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
	progress->set_task ("determining intensities of Dufay screen patches (precise mode)", maxy - miny + m_height * 3);

      if (mode == precise)
        analyze_precise (scr_to_img, render, screen, collection_threshold, w_red, w_green, w_blue, minx, miny, maxx, maxy, progress);
      else if (mode == precise_rgb)
        analyze_precise_rgb (scr_to_img, render, screen, collection_threshold, w_red, w_green, w_blue, minx, miny, maxx, maxy, progress);
      else
        analyze_color (scr_to_img, render, w_red, w_green, w_blue, minx, miny, maxx, maxy, progress);

      free (w_red);
      free (w_green);
      free (w_blue);
    }
  else
    {
      if (progress)
	progress->set_task ("determining intensities of Dufay screen patches (fast mode)", m_height);
      analyze_fast (render, progress);
    }
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
#if 0
		luminosity_t wsum = 0;
#endif
		luminosity_t ratsum1 = 0, ratsum2 = 0;
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
#if 0
				wsum += w;
#endif
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
