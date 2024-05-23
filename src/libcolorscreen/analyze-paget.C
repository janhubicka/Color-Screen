#include "include/analyze-paget.h"
#include "include/screen.h"
#include "include/tiff-writer.h"
/* Collect luminosity of individual color patches.
   Function is flattened so it should do only necessary work.  */
bool flatten_attr
analyze_paget::analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
{
  /* Collect luminosity of individual color patches.  */
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
	      if (scr_x < 0 || scr_x >= m_width - 1 || scr_y < 0 || scr_y > m_height - 1)
		continue;

	      luminosity_t l = render->get_unadjusted_data (x, y);
	      int ix = (uint64_t) nearest_int (scr_x * screen::size) & (unsigned)(screen::size - 1);
	      int iy = (uint64_t) nearest_int (scr_y * screen::size) & (unsigned)(screen::size - 1);
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
		      red_atomic_add (xx, yy, (screen->mult[iy][ix][0] - collection_threshold) * l);
		      luminosity_t &l = w_red [yy * m_width + xx];
		      luminosity_t val = screen->mult[iy][ix][0] - collection_threshold;
#pragma omp atomic
		      l+=val;
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
		      green_atomic_add (xx, yy, (screen->mult[iy][ix][1] - collection_threshold) * l);
		      luminosity_t &l = w_green [yy * m_width + xx];
		      luminosity_t val = screen->mult[iy][ix][1] - collection_threshold;
#pragma omp atomic
		      l+=val;
		    }
		}
	      if (screen->mult[iy][ix][2] > collection_threshold)
		{
		  int xx = nearest_int (2*(scr_x-(coord_t)0.25));
		  int yy = nearest_int (2*(scr_y-(coord_t)0.25));
		  blue_atomic_add (xx, yy, (screen->mult[iy][ix][2] - collection_threshold) * l);
		  luminosity_t &l = w_blue [yy * m_width * 2 + xx];
		  luminosity_t val = screen->mult[iy][ix][2] - collection_threshold;
#pragma omp atomic
		  l+=val;
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
#pragma omp simd
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
#pragma omp simd
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
#pragma omp simd
	    for (int x = 0; x < m_width * 2; x++)
	      if (w_blue [y * m_width * 2 + x] != 0)
		blue (x,y) /= w_blue [y * m_width * 2 + x];
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
analyze_paget::analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress)
{
  printf ("TODO: implement more precise data collection.");

  /* Collect luminosity of individual color patches.  */
#pragma omp parallel shared(progress, render, scr_to_img, w_blue, w_red, w_green, minx, miny, maxx, maxy) default(none)
  {
#pragma omp for 
    for (int y = miny ; y < maxy; y++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int x = minx; x < maxx; x++)
	    {
	      rgbdata d = render->get_unadjusted_rgb_pixel (x, y);
	      coord_t scr_x, scr_y;
	      scr_to_img->to_scr (x + (coord_t)0.5, y + (coord_t)0.5, &scr_x, &scr_y);
	      scr_x += m_xshift;
	      scr_y += m_yshift;
	      if (scr_x < 0 || scr_x >= m_width - 1 || scr_y < 0 || scr_y > m_height - 1)
		continue;

	      {
		coord_t xd, yd;
		to_diagonal_coordinates (scr_x + (coord_t)0.5, scr_y, &xd, &yd);
		xd = nearest_int (xd);
		yd = nearest_int (yd);
		int xx = ((int)xd + (int)yd) / 2;
		int yy = -(int)xd + (int)yd;
		if (xx >= 0 && xx < m_width && yy >= 0 && yy < m_height * 2)
		  {
		    red_atomic_add (xx, yy, d.red);
		    luminosity_t &l = w_red [yy * m_width + xx];
#pragma omp atomic
		    l+=1;
		  }
	      }
	      {
		coord_t xd, yd;
		to_diagonal_coordinates (scr_x, scr_y, &xd, &yd);
		xd = nearest_int (xd);
		yd = nearest_int (yd);
		int xx = ((int)xd + (int)yd) / 2;
		int yy = -(int)xd + (int)yd;
		if (xx >= 0 && xx < m_width && yy >= 0 && yy < m_height * 2)
		  {
		    green_atomic_add (xx, yy, d.green);
		    luminosity_t &l = w_green [yy * m_width + xx];
#pragma omp atomic
		    l+=1;
		  }
	      }
	      {
		int xx = nearest_int (2*(scr_x-(coord_t)0.25));
		int yy = nearest_int (2*(scr_y-(coord_t)0.25));
		blue_atomic_add (xx, yy, d.blue);
		luminosity_t &l = w_blue [yy * m_width * 2 + xx];
#pragma omp atomic
		l+=1;
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
  return !progress || !progress->cancelled ();
}
bool flatten_attr
analyze_paget::analyze_fast (render_to_scr *render,progress_info *progress)
{
	/* TODO: Use unadjusted data  */
#define pixel(xo,yo,diag) render->get_unadjusted_img_pixel_scr ((x - m_xshift) + xo, (y - m_yshift) + yo)
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
  return !progress || !progress->cancelled ();
}
bool
analyze_paget::analyze (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, const screen *screen, int width, int height, int xshift, int yshift, mode mode, luminosity_t collection_threshold, progress_info *progress)
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
  if (mode == precise || mode == color)
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
	progress->set_task ("determining intensities of Paget/Finlay screen patches (precise mode)", maxy - miny + m_height * 2 * 3);

      if (mode == precise)
        analyze_precise (scr_to_img, render, screen, collection_threshold, w_red, w_green, w_blue, minx, miny, maxx, maxy, progress);
      else
        analyze_color (scr_to_img, render, w_red, w_green, w_blue, minx, miny, maxx, maxy, progress);

      free (w_red);
      free (w_green);
      free (w_blue);
    }
  else
    {
      if (progress)
	progress->set_task ("determining intensities of Paget screen patches (fast mode)", m_height);
      analyze_fast (render, progress);
    }
  return !progress || !progress->cancelled ();
}

/* For stitching we need to write screen with 2 pixels per repetition of screen pattern.  */
bool
analyze_paget::write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress, luminosity_t rmin, luminosity_t rmax, luminosity_t gmin, luminosity_t gmax, luminosity_t bmin, luminosity_t bmax)
{
  tiff_writer_params p;
  p.filename = filename;
  p.width = 2 * m_width;
  p.height = 2 * m_height;
  p.alpha = true;
  tiff_writer out(p, error);
  if (*error)
    return false;
  if (progress)
    progress->set_task ("Saving screen", m_height * 2);
  luminosity_t rscale = rmax > rmin ? 1/(rmax-rmin) : 1;
  luminosity_t gscale = gmax > gmin ? 1/(gmax-gmin) : 1;
  luminosity_t bscale = bmax > bmin ? 1/(bmax-bmin) : 1;
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	{
	  if (x > 0 && y / 2 > 0 && x < m_width - 1 && y / 2 < m_height - 1
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x-1, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x+1, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2-1)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2+1))
	    {
	      luminosity_t red1 = 0, green1 = 0, blue1 = 0;
	      luminosity_t red2 = 0, green2 = 0, blue2 = 0;
	      if (!(y & 1))
	        {
		  red1 = (red (x, y) + red (x+1, y) + red (x, y - 1) + red (x, y + 1)) * 0.25;
		  green1 = green (x, y);
		  red2 = red (x+1, y);
		  green2 = (green (x, y) + green (x+1, y) + green (x, y - 1) + green (x, y + 1)) * 0.25;
	        }
	      else
	        {
		  red1 = red (x, y);
		  green1 = (green (x, y) + green (x-1, y) + green (x, y - 1) + green (x, y + 1)) * 0.25;
		  red2 = (red (x, y) + red (x+1, y) + red (x+1, y - 1) + red (x+1, y + 1)) * 0.25;
		  green2 = green (x, y);
	        }
	      blue1 = (blue (2 * x - 1, y) + blue (2 * x, y) + blue (2 * x, y - 1) + blue (2 * x - 1, y -1)) * 0.25;
	      blue2 = (blue (2 * x, y) + blue (2 * x + 1, y) + blue (2 * x, y - 1) + blue (2 * x + 1, y - 1)) * 0.25;

	      red1 = green1 = blue1 = (red1+green1+blue1) / 3;
	      red2 = green2 = blue2 = (red2+green2+blue2) / 3;
	      out.row16bit ()[2 * x * 4 + 0] = std::max (std::min (linear_to_srgb ((red1 - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 1] = std::max (std::min (linear_to_srgb ((green1 - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 2] = std::max (std::min (linear_to_srgb ((blue1 - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 3] = 65535;
	      out.row16bit ()[(2 * x + 1) * 4 + 0] = std::max (std::min (linear_to_srgb ((red2 - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 1] = std::max (std::min (linear_to_srgb ((green2 - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 2] = std::max (std::min (linear_to_srgb ((blue2 - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 3] = 65535;
	    }
	  else
	    {
	      out.row16bit ()[2 * x * 4 + 0] = 0;
	      out.row16bit ()[2 * x * 4 + 1] = 0;
	      out.row16bit ()[2 * x * 4 + 2] = 0;
	      out.row16bit ()[2 * x * 4 + 3] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 0] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 1] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 2] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 3] = 0;
	    }
	}
      if (!out.write_row ())
	{
	  *error = "Write error";
	  return false;
	}
     if (progress)
       progress->inc_progress ();
    }
  return true;
}
int
analyze_paget::find_best_match (int percentage, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress)
{
  /* Top left corner of other scan in screen coordinates.  */
  coord_t lx, ly;
  other_map.to_scr (0, 0, &lx, &ly);
  if (cpfind)
    {
      if (find_best_match_using_cpfind (other, xshift_ret, yshift_ret, direction, map, other_map, 2, report_file, progress))
	return true;
    }
  /* TODO: Implement brute forcing.  */
  return false;
}

bool
analyze_paget::dump_patch_density (FILE *out)
{
  fprintf (out, "Paget dimenstion: %i %i\n", m_width, m_height);
  fprintf (out, "Red %i %i\n", m_width , 2*m_height);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", red (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Green %i %i\n", m_width , 2*m_height);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", green (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Blue %i %i\n", m_width * 2, m_height * 2);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width * 2; x++)
	fprintf (out, "  %f", blue (x, y));
      fprintf (out, "\n");
    }
  return true;
}
