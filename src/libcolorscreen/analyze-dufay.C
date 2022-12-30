#include <tiffio.h>
#include <vector>
#include "include/analyze-dufay.h"
#include "include/screen.h"

extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;

bool
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
  if (progress)
    progress->set_task ("determining colors", m_height);
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
#if 0
		dufay_prec_red (2 * x, y) = pixel (0.25, 0.5, 0.3333, 0.5);
		dufay_prec_red (2 * x + 1, y) = pixel (0.75, 0.5,0.3333, 0.5);
		dufay_prec_green (x, y) = pixel (0, 0, 1 - 0.333, 0.5);
		dufay_prec_blue (x, y) = pixel (0.5, 0, 1 - 0.333, 0.5);
#endif
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    }
#undef pixel
  return !progress || !progress->cancelled ();
}
bool
analyze_dufay::find_best_match (int percentage, analyze_dufay &other, const char *filename1, const char *filename2, int *xshift_ret, int *yshift_ret, progress_info *progress)
{
  if (filename1)
    {
      FILE *f = fopen ("project-cpfind.pto","wt");
      if (!f)
	return false;
      int mwidth = std::max (m_width, other.m_width);
      int mheight = std::max (m_height, other.m_height);
      luminosity_t rmin, rmax, gmin, gmax, bmin, bmax;
      luminosity_t rmin2, rmax2, gmin2, gmax2, bmin2, bmax2;
      analyze_range (&rmin, &rmax, &gmin, &gmax, &bmin, &bmax);
      other.analyze_range (&rmin2, &rmax2, &gmin2, &gmax2, &bmin2, &bmax2);
      rmin = std::min (rmin, rmin2);
      rmax = std::max (rmax, rmax2);
      gmin = std::min (gmin, gmin2);
      gmax = std::max (gmax, gmax2);
      bmin = std::min (bmin, bmin2);
      bmax = std::max (bmax, bmax2);
      const char *error;
      if (!write_screen ("screen1.tif", NULL, &error, progress, rmin, rmax, gmin, gmax, bmin, bmax)
          || !other.write_screen ("screen2.tif", NULL, &error, progress, rmin, rmax, gmin, gmax, bmin, bmax))
	{
	  if (progress)
	    progress->pause_stdout ();
	  printf ("Failed to write screen\n");
	  return false;
	}
      fprintf (f, "p f2 w3000 h1500 v360  k0 E0 R0 n\"TIFF_m c:LZW r:CROP\"\n m i0\n"
		  "i w%i h%i f0 v10 Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n"
		  "i w%i h%i f0 v10 Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n", /*m_width, m_height,*/ mwidth, mheight, /*filename1*/"screen1.tif", /*other.m_width, other.m_height,*/ mwidth, mheight, /*filename2*/"screen2.tif");
      fclose (f);
      if (progress)
	progress->pause_stdout ();
      if (system ("cpfind --fullscale --ransacmode=rpy project-cpfind.pto -o project-cpfind-out.pto"))
	{
	  printf ("Failed to execute cpfind\n");
	  progress->resume_stdout ();
	  return false;
	}
      f = fopen ("project-cpfind-out.pto", "r");
      if (!f)
	{
	  printf ("Failed to open cpfind output file\n");
	  progress->resume_stdout ();
	  return false;
	}
      struct offsets {
	int x, y, n;
      };
      std::vector<offsets> off;
      int npoints = 0;
      while (!feof (f))
	{
	  int c;
	  if ((c = fgetc (f)) != 'c'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'n'
	      || (c = fgetc (f)) != '0'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'N'
	      || (c = fgetc (f)) != '1'
	      || (c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'x')
	    {
	      while (c != EOF && c != '\n')
		c = fgetc (f);
	      continue;
	    }
	  float x1, y1, x2, y2;
	  npoints++;
	  if (fscanf (f, "%f", &x1) != 1)
	    {
	      printf ("Parse error 1\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'y')
	    {
	      printf ("Parse error 2\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &y1) != 1)
	    {
	      printf ("Parse error 3\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'X')
	    {
	      printf ("Parse error 4\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &x2) != 1)
	    {
	      printf ("Parse error 5\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if ((c = fgetc (f)) != ' '
	      || (c = fgetc (f)) != 'Y')
	    {
	      printf ("Parse error 6\n");
	      progress->resume_stdout ();
	      return false;
	    }
	  if (fscanf (f, "%f", &y2) != 1)
	    {
	      printf ("Parse error 7\n");
	      progress->resume_stdout ();
	      return false;
	    }
#if 0
	  if (fscanf (f, "x%f y%f X%f Y%f to\n", &x1, &y1, &x2, &y2) != 4)
	    {
	      printf ("Parse error\n");
	      progress->resume_stdout ();
	      return false;
	    }
#endif

	  int xo = round (x2 - x1);
	  int yo = round (y2 - y1);
	  if (fabs ((x2 - x1) - xo) > 0.3
	      || fabs ((y2 - y1) - yo) > 0.3)
	    {
	      printf ("Control point %f %f %f %f identified by cpfind discarded since offset is not integer\n",x1,y1,x2,y2);
	      continue;
	    }
	  printf ("Control point: x1 %f y2 %f x2 %f y2 %f offsetx %i offsety %i\n",x1,y1,x2,y2,xo,yo);
	  bool found = false;
	  for (offsets &o : off)
	    if (o.x == xo && o.y == yo)
	      {
		 o.n++;
		 found = true;
	      }
	  if (!found)
	    off.push_back ((struct offsets){xo,yo,1});
	}
      fclose (f);
      if (off.size ())
	{
	  offsets max;
	  max = off[0];
	  for (offsets &o : off)
	    if (o.n > max.n)
	      max = o;
	  if (max.n > npoints / 2)
	    {
	      *xshift_ret = -max.x - (m_xshift - other.m_xshift);
	      *yshift_ret = -max.y - (m_yshift - other.m_yshift);
	      printf ("Best offset %i %i with %i points, shifts %i %i\n", *xshift_ret, *yshift_ret, max.n, m_xshift - other.m_xshift, m_yshift - other.m_yshift);
	      if (progress)
		progress->resume_stdout ();
	      return true;
	    }
	  else
	    printf ("Cpfind result does not seem reliable. Best match is only %i points out of %i\n", max.n, npoints);
	}
      else
        printf ("Cpfind did not ifnd useful points; trying to find match myself\n");
      if (progress)
	progress->resume_stdout ();
    }
  int xstart, xend, ystart, yend;
  bool found = false;
  luminosity_t best_sqsum = 0;
  luminosity_t best_rscale, best_gscale, best_bscale;
  int best_xshift = 0, best_yshift = 0;
  int best_noverlap;

  xstart = -other.m_width + 2;
  ystart = -other.m_height + 2;
  xend = m_width - 2;
  yend = m_height - 2;

  xstart -= m_xshift - other.m_xshift;
  ystart -= m_yshift - other.m_yshift;
  xend -= m_xshift - other.m_xshift;
  yend -= m_yshift - other.m_yshift;
  if (progress)
    progress->set_task ("determining best overlap", (yend - ystart));
#pragma omp parallel for default (none) shared (progress, xstart, xend, ystart, yend, other, percentage, found, best_sqsum, best_xshift, best_yshift, best_rscale, best_gscale, best_bscale, best_noverlap)
  for (int y = ystart; y < yend; y++)
    {
      bool lfound = false;
      luminosity_t lbest_sqsum = 0;
      int lbest_xshift = 0, lbest_yshift = 0;
      luminosity_t lbest_rscale = 0, lbest_gscale = 0, lbest_bscale = 0;
      int lbest_noverlap;
      for (int x = xstart; x < xend; x++)
	{
	  int noverlap = 0;
	  int xxstart = -m_xshift;
	  int xxend = -m_xshift + m_width;
	  int yystart = -m_yshift;
	  int yyend = -m_yshift + m_height;
	  luminosity_t sqsum = 0;
	  luminosity_t lum_sum = 0;

	  xxstart = std::max (-other.m_xshift + x, xxstart);
	  yystart = std::max (-other.m_yshift + y, yystart);
	  xxend = std::min (-other.m_xshift + other.m_width + x, xxend);
	  yyend = std::min (-other.m_yshift + other.m_height + y, yyend);

	  //if (yystart >= yyend || xxstart >= xxend)
	    //continue;
	  assert (yystart < yyend && xxstart < xxend);
	  if ((xxend - xxstart) * (yyend - yystart) * 100 < m_n_known_pixels * percentage)
	    continue;

	  int xstep = std::max ((xxend - xxstart) / 30, 1);
	  int ystep = std::max ((yyend - yystart) / 30, 1);
	  luminosity_t rsum1 = 0, rsum2 = 0, gsum1 = 0, gsum2 = 0, bsum1 = 0, bsum2 = 0;

	  //printf ("Shift %i %i checking %i to %i, %i to %i; img1 %i %i %i %i; img2 %i %i %i %i\n", x, y, xxstart, xxend, yystart, yyend, m_xshift, m_yshift, m_width, m_height, other.m_xshift, other.m_yshift, other.m_width, other.m_height);
	  
	  for (int yy = yystart; yy < yyend; yy+= ystep)
	    {
	      for (int xx = xxstart; xx < xxend; xx+= xstep)
		{
		  int x1 = xx + m_xshift;
		  int y1 = yy + m_yshift;
		  if (!m_known_pixels->test_bit (x1, y1))
		    continue;
		  int x2 = xx - x + other.m_xshift;
		  int y2 = yy - y + other.m_yshift;
		  if (!m_known_pixels->test_bit (x2, y2))
		    continue;
		  rsum1 += red (2 * x1, y1) + red (2 * x1 + 1, y1);
		  rsum2 += other.red (2 * x2, y2) + other.red (2 * x2 + 1, y2);
		  gsum1 += green (x1, y1);
		  gsum2 += other.green (x2, y2);
		  bsum1 += blue (x1, y1);
		  bsum2 += other.blue (x2, y2);
		  noverlap++;
		}
	    }
	  if (noverlap * xstep * ystep * 100 < std::min (m_n_known_pixels, other.m_n_known_pixels) * percentage)
	    continue;
	  if (!rsum1 || !gsum1 || !bsum1)
	    continue;
	  int rscale = rsum2 / rsum1;
	  int gscale = gsum2 / gsum1;
	  int bscale = bsum2 / bsum1;
	  const luminosity_t exposure_tolerance = 0.1;
	  if (fabs (rscale - 1) > exposure_tolerance
	      || fabs (gscale -1) > exposure_tolerance
	      || fabs (bscale -1) > exposure_tolerance)
	    continue;

	  for (int yy = yystart; yy < yyend; yy+= ystep)
	    {
	      for (int xx = xxstart; xx < xxend; xx+= xstep)
		{
		  int x1 = xx + m_xshift;
		  int y1 = yy + m_yshift;
		  //printf ("%i %i\n",x1,y1);
		  ////if (!(x1 >= 0 && x1 < m_width && y1 >= 0 && y1 < m_height))
		    //printf ("%i %i\n",x1,y1);
		  //assert (x1 >= 0 && x1 < m_width && y1 >= 0 && y1 < m_height);
		  if (!m_known_pixels->test_bit (x1, y1))
		    continue;
		  int x2 = xx - x + other.m_xshift;
		  int y2 = yy - y + other.m_yshift;
		  //if (!(x2 >= 0 && x2 < other.m_width && y2 >= 0 && y2 < other.m_height))
		    //printf ("%i %i\n",x1,y1);
		  //assert (x2 >= 0 && x2 < other.m_width && y2 >= 0 && y2 < other.m_height);
		  if (!m_known_pixels->test_bit (x2, y2))
		    continue;
#if 0
		  sqsum += fabs (green (x1, y1) * gscale - other.green (x2, y2)) / std::max (green (x1, y1) * gscale, (luminosity_t)0.00001);
		  sqsum += fabs (blue (x1, y1) * bscale - other.blue (x2, y2)) / std::max (blue (x1, y1) * bscale, (luminosity_t)0.00001);
#else
		  //luminosity_t lum = /*red (2 * x1, y1) + red (2 * x1 + 1, y1) +*/ green (x1, y1) + blue (x1, y1);;
		  //lum_sum += lum;
		  luminosity_t lum = 1;
		  sqsum += (red (2 * x1, y1) * rscale - other.red (2 * x2, y2)) * (red (2 * x1, y1) * rscale - other.red (2 * x2, y2)) / (lum * lum);
		  sqsum += (red (2 * x1 + 1, y1) * rscale - other.red (2 * x2 + 1, y2)) * (red (2 * x1 + 1, y1) * rscale - other.red (2 * x2 + 1, y2)) / (lum * lum);
		  sqsum += (green (x1, y1) * gscale - other.green (x2, y2)) * (green (x1, y1) * gscale - other.green (x2, y2)) / (lum * lum);
		  sqsum += (blue (x1, y1) * bscale - other.blue (x2, y2)) * (blue (x1, y1) * bscale - other.blue (x2, y2)) / (lum * lum);
#if 0
		  if (fabs (red (2 * x1, y1) * rscale - other.red (2 * x2, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (red (2 * x1 + 1, y1) * rscale - other.red (2 * x2 + 1, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (green (x1, y1) * gscale - other.green (x2, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (blue (x1, y1) * gscale - other.blue (x2, y2)) > 0.01)
		    sqsum += 1;
#endif
#endif
		}
	    }
	  if (noverlap * xstep * ystep * 100 < std::min (m_n_known_pixels, other.m_n_known_pixels) * percentage)
	    continue;
	  //printf ("Overlap %i, known pixels %i\n", noverlap *= step * step, m_n_known_pixels);
	  sqsum /= noverlap;
	  //sqsum /= lum_sum;
	  if (!lfound || sqsum < lbest_sqsum)
	    {
	      lfound = true;
	      lbest_sqsum = sqsum;
	      lbest_xshift = x;
	      lbest_yshift = y;
	      lbest_noverlap = noverlap * xstep * ystep;
	      lbest_rscale = rscale;
	      lbest_gscale = gscale;
	      lbest_bscale = bscale;
	    }
	}
      if (progress)
	progress->inc_progress ();
      if (lfound)
#pragma omp critical
	{
	  if (!found || lbest_sqsum < best_sqsum)
	    {
	      found = 1;
	      best_sqsum = lbest_sqsum;
	      best_xshift = lbest_xshift;
	      best_yshift = lbest_yshift;
	      best_rscale = lbest_rscale;
	      best_gscale = lbest_gscale;
	      best_bscale = lbest_bscale;
	      best_noverlap = lbest_noverlap;
	    }
	}
    }
  *xshift_ret = best_xshift;
  *yshift_ret = best_yshift;
  if (found)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Best match on offset %i,%i sqsum %f scales %f,%f,%f overlap %f%%\n", best_xshift, best_yshift, best_sqsum, best_rscale, best_gscale, best_bscale, 100.0 * best_noverlap / std::min (m_n_known_pixels, other.m_n_known_pixels));
      if (progress)
	progress->resume_stdout ();
    }
  return found;
}
bool
analyze_dufay::write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress, luminosity_t rmin, luminosity_t rmax, luminosity_t gmin, luminosity_t gmax, luminosity_t bmin, luminosity_t bmax)
{
  TIFF *out = TIFFOpen (filename, "wb");
  
  if (!out)
    {
      *error = "can not open screen file";
      return false;
    }
  uint16_t extras[] = {EXTRASAMPLE_UNASSALPHA};
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, m_width)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, m_height)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 4)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_EXTRASAMPLES, 1, extras)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, sRGB_icc_len, sRGB_icc))
    {
      *error = "write error";
      return false;
    }
  uint16_t *outrow = (uint16_t *) malloc (m_width * 2 * 4);
  if (!*outrow)
    {
      *error = "Out of memory allocating output buffer";
      return false;
    }
  if (progress)
    {
      progress->set_task ("Saving screen", m_height);
    }
  progress->pause_stdout ();
  printf ("Saving screen to %s in resolution %ix%i\n", filename, m_width, m_height);
  progress->resume_stdout ();
  luminosity_t rscale = rmax > rmin ? 1/(rmax-rmin) : 1;
  luminosity_t gscale = gmax > gmin ? 1/(gmax-gmin) : 1;
  luminosity_t bscale = bmax > bmin ? 1/(bmax-bmin) : 1;
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	{
	  if ((known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y))
	    {
	      outrow[x * 4 + 0] = std::max (std::min (linear_to_srgb (((red (x * 2, y) + red (x * 2 + 1, y)) / 2 - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      outrow[x * 4 + 1] = std::max (std::min (linear_to_srgb ((green (x, y) - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      outrow[x * 4 + 2] = std::max (std::min (linear_to_srgb ((blue (x, y) - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      outrow[x * 4 + 3] = 65535;
	    }
	  else
	    {
	      outrow[x * 4 + 0] = 0;
	      outrow[x * 4 + 1] = 0;
	      outrow[x * 4 + 2] = 0;
	      outrow[x * 4 + 3] = 0;
	    }
	}
      if (TIFFWriteScanline (out, outrow, y, 0) < 0)
	{
	  free (outrow);
	  TIFFClose (out);
	  *error = "Write error";
	  return false;
	}
     if (progress)
       progress->inc_progress ();
    }
  free (outrow);
  TIFFClose (out);
  return out;
}
void
analyze_dufay::analyze_range (luminosity_t *rrmin, luminosity_t *rrmax, luminosity_t *rgmin, luminosity_t *rgmax, luminosity_t *rbmin, luminosity_t *rbmax)
{
  std::vector<luminosity_t> rvec;
  std::vector<luminosity_t> gvec;
  std::vector<luminosity_t> bvec;
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
	rvec.push_back (red (x * 2 + 1, y));
	rvec.push_back (red (x * 2 + 1, y));
	gvec.push_back (green (x, y));
	bvec.push_back (blue (x, y));
      }
  if (!rvec.size ())
  {
    *rrmin = 0;
    *rrmax = 1;
    *rgmin = 0;
    *rgmax = 1;
    *rbmin = 0;
    *rbmax = 1;
    return;
  }
  sort (rvec.begin(), rvec.end ());
  sort (gvec.begin(), gvec.end ());
  sort (bvec.begin(), bvec.end ());
  *rrmin = rvec[rvec.size () * 0.03];
  *rgmin = gvec[gvec.size () * 0.03];
  *rbmin = bvec[bvec.size () * 0.03];
  *rrmax = rvec[ceil ((rvec.size ()-1) * 0.97)];
  *rgmax = gvec[ceil ((gvec.size ()-1) * 0.97)];
  *rbmax = bvec[ceil ((bvec.size ()-1) * 0.97)];
#if 0
  luminosity_t rmin = 1, rmax = 0;
  luminosity_t gmin = 1, gmax = 0;
  luminosity_t bmin = 1, bmax = 0;
  std::

  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
	rmin = std::min (rmin, red (x * 2 + 1, y));
	rmax = std::max (rmax, red (x * 2 + 1, y));
	rmin = std::min (rmin, red (x * 2 + 1, y));
	rmax = std::max (rmax, red (x * 2 + 1, y));
	gmin = std::min (gmin, green (x, y));
	gmax = std::max (gmax, green (x, y));
	bmin = std::min (bmin, blue (x, y));
	bmax = std::max (bmax, blue (x, y));
      }
   *rrmin = rmin;
   *rrmax = rmax;
   *rgmin = gmin;
   *rgmax = gmax;
   *rbmin = bmin;
   *rbmax = bmax;
#endif
}
