#include "include/analyze-dufay.h"
bool
analyze_dufay::analyze (render_to_scr *render, int width, int height, int xshift, int yshift, bool precise, progress_info *progress)
{
  assert (!m_red);
  m_width = width;
  m_height = height;
  m_xshift = xshift;
  m_yshift = yshift;
  /* G B .
     R R .
     . . .  */
  m_red = (luminosity_t *)malloc (m_width * m_height * sizeof (luminosity_t) * 2);
  m_green = (luminosity_t *)malloc (m_width * m_height * sizeof (luminosity_t));
  m_blue = (luminosity_t *)malloc (m_width * m_height * sizeof (luminosity_t));
  if (!m_red || !m_green || !m_blue)
    return false;
  if (progress)
    progress->set_task ("determining colors", m_height);
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
#undef pixel
  return !progress || !progress->cancelled ();
}
bool
analyze_dufay::find_best_match (int percentage, analyze_dufay &other, int skiptop, int skipbottom, int skipleft, int skipright, int *xshift_ret, int *yshift_ret, progress_info *progress)
{
  int xstart, xend, ystart, yend;
  bool found = false;
  luminosity_t best_sqsum = 0;
  int best_xshift = 0, best_yshift = 0;
  percentage=20;

  xstart = -other.m_width + 2 + m_width * skipleft / 100;
  ystart = -other.m_height + 2 + m_height * skiptop / 100;
  xend = m_width * (100 - skipright) / 100 - 2;
  yend = m_height * (100 - skipbottom) / 100 - 2;

  xstart -= m_xshift - other.m_xshift;
  ystart -= m_yshift - other.m_yshift;
  xend -= m_xshift - other.m_xshift;
  yend -= m_yshift - other.m_yshift;
  if (progress)
    progress->set_task ("determining best overlap", (yend - ystart));
  if (progress)
    progress->pause_stdout ();
  //printf ("Searching range %i to %i, %i to %i; num pixels: %i %i skip top %i skip bottom %i skip left %i skip right %i\n", xstart, xend, ystart, yend, m_n_known_pixels, other.m_n_known_pixels, skiptop, skipbottom, skipleft, skipright);
  if (progress)
    progress->resume_stdout ();
#pragma omp parallel for default (none) shared (progress, xstart, xend, ystart, yend, other, percentage, found, best_sqsum, best_xshift, best_yshift, skiptop, skipbottom, skipleft, skipright)
  for (int y = ystart; y < yend; y++)
    {
      bool lfound = false;
      int step = 32;
      luminosity_t lbest_sqsum = 0;
      int lbest_xshift = 0, lbest_yshift = 0;
      for (int x = xstart; x < xend; x++)
	{
	  int noverlap = 0;
	  int xxstart = -m_xshift + m_width * skipleft / 100;
	  int xxend = -m_xshift + m_width * (100 - skipright) / 100;
	  int yystart = -m_yshift + m_height * skiptop / 100;
	  int yyend = -m_yshift + m_height * (100 - skipbottom) / 100;
	  luminosity_t sqsum = 0;
	  luminosity_t lum_sum = 0;

	  xxstart = std::max (-other.m_xshift + x, xxstart);
	  yystart = std::max (-other.m_yshift + y, yystart);
	  xxend = std::min (-other.m_xshift + other.m_width + x, xxend);
	  yyend = std::min (-other.m_yshift + other.m_height + y, yyend);

	  if (yystart >= yyend || xxstart >= xxend)
	    continue;
	  assert (yystart < yyend && xxstart < xxend);
	  //if ((xxend - xxstart) * (yyend - yystart) * 100 < m_n_known_pixels * percentage)
	    //continue;

	  //printf ("Shift %i %i checking %i to %i, %i to %i; img1 %i %i %i %i; img2 %i %i %i %i\n", x, y, xxstart, xxend, yystart, yyend, m_xshift, m_yshift, m_width, m_height, other.m_xshift, other.m_yshift, other.m_width, other.m_height);

	  for (int yy = yystart; yy < yyend; yy+= step)
	    {
	      for (int xx = xxstart; xx < xxend; xx+= step)
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
#if 1
		  luminosity_t lum = red (2 * x1, y1) + red (2 * x1 + 1, y1) + green (x1, y1) + blue (x1, y1);;
		  //lum_sum += lum;
		lum = 1;
		  sqsum += (red (2 * x1, y1) - other.red (2 * x2, y2)) * (red (2 * x1, y1) - other.red (2 * x2, y2)) / (lum * lum);
		  sqsum += (red (2 * x1 + 1, y1) - other.red (2 * x2 + 1, y2)) * (red (2 * x1 + 1, y1) - other.red (2 * x2 + 1, y2)) / (lum * lum);
		  sqsum += (green (x1, y1) - other.green (x2, y2)) * (green (x1, y1) - other.green (x2, y2)) / (lum * lum);
		  sqsum += (blue (x1, y1) - other.blue (x2, y2)) * (blue (x1, y1) - other.blue (x2, y2)) / (lum * lum);
#else
		  if (fabs (red (2 * x1, y1) - other.red (2 * x2, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (red (2 * x1 + 1, y1) - other.red (2 * x2 + 1, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (green (x1, y1) - other.green (x2, y2)) > 0.01)
		    sqsum += 1;
		  if (fabs (blue (x1, y1) - other.blue (x2, y2)) > 0.01)
		    sqsum += 1;
#endif
		  noverlap++;
		}
	    }
	  if (noverlap * step * step * 100 < m_n_known_pixels * percentage)
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
	    }
	}
    }
  *xshift_ret = best_xshift;
  *yshift_ret = best_yshift;
  if (found)
    {
      if (progress)
	progress->pause_stdout ();
      //printf ("Best match on offset %i,%i sqsum %f\n", best_xshift, best_yshift, best_sqsum);
      if (progress)
	progress->resume_stdout ();
    }
  return found;
}
