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
