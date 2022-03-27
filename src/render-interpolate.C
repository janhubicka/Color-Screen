#include <assert.h>
#include "render-interpolate.h"

render_interpolate::render_interpolate (scr_to_img_parameters param, image_data &img, int dst_maxval)
   : render_to_scr (param, img, dst_maxval), m_prec_red (0), m_prec_green (0), m_prec_blue (0)
{
}

#if 0
static inline double
cubicInterpolate (double p[4], double x)
{
  return p[1] + 0.5 * x * (p[2] - p[0] +
			   x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
				x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

static inline double
bicubicInterpolate (double p[4][4], double x, double y)
{
  double arr[4];
  if (x < 0 || x > 1 || y < 0 || y > 1)
    abort ();
  arr[0] = cubicInterpolate (p[0], y);
  arr[1] = cubicInterpolate (p[1], y);
  arr[2] = cubicInterpolate (p[2], y);
  arr[3] = cubicInterpolate (p[3], y);
  return cubicInterpolate (arr, x);
}

double
render_interpolate::getmatrixsample (double **sample, int *shift, int pos, int xp, int x, int y)
{
  int line = (pos + NRED + x + y) % NRED;
  return sample[line][((xp + y * 2 - x * 2) - shift[line]) / 4];
}

void
render_interpolate::render_row (int y, pixel ** outrow)
{
  double **redsample = m_redsample;
  double **greensample = m_greensample;
  double **bluesample = m_bluesample;
  int *bluepos = m_bluepos;
  int *redpos = m_redpos;
  int *redshift = m_redshift;
  int *greenpos = m_greenpos;
  int *greenshift = m_greenshift;
  int x;
  int sx;
  int sy;
  int scale = m_scale;

  if (y % 4 == 0)
    {
      for (x = 0; x < m_scr_width; x++)
	greensample[m_greenp][x] =
	  get_img_pixel_scr (x - m_scr_xshift,
		  (y - m_scr_yshift * 4) / 4.0);
      m_greenpos[m_greenp] = y;
      greenshift[m_greenp] = 0;
      m_greenp++;
      m_greenp %= 8;

      for (x = 0; x < m_scr_width; x++)
	redsample[m_redp][x] =
	  get_img_pixel_scr (x - m_scr_xshift + 0.5,
		  (y - m_scr_yshift * 4) / 4.0);
      redpos[m_redp] = y;
      redshift[m_redp] = 2;
      m_redp++;
      m_redp %= 8;
    }
  if (y % 4 == 2)
    {
      for (x = 0; x < m_scr_width; x++)
	redsample[m_redp][x] =
	  get_img_pixel_scr (x - m_scr_xshift,
		  (y - m_scr_yshift * 4) / 4.0);
      redpos[m_redp] = y;
      redshift[m_redp] = 0;
      m_redp++;
      m_redp %= 8;

      for (x = 0; x < m_scr_width; x++)
	greensample[m_greenp][x] =
	  get_img_pixel_scr (x - m_scr_xshift + 0.5,
		  (y - m_scr_yshift * 4) / 4.0);
      m_greenpos[m_greenp] = y;
      greenshift[m_greenp] = 2;
      m_greenp++;
      m_greenp %= 8;
    }
  if (y % 4 == 1 || y % 4 == 3)
    {
      bluepos[m_bluep] = y;
      for (x = 0; x < m_scr_width; x++)
	{
	  bluesample[m_bluep][x * 2] =
	    get_img_pixel_scr (x - m_scr_xshift + 0.25,
		    (y - m_scr_yshift * 4) / 4.0);
	  bluesample[m_bluep][x * 2 + 1] =
	    get_img_pixel_scr (x - m_scr_xshift + 0.75,
		    (y - m_scr_yshift * 4) / 4.0);
	}
      m_bluep++;
      m_bluep %= NBLUE;
    }
  if (y > 8 * 4)
    {
#define OFFSET  7
      int rendery = y - OFFSET;
      int bluestart =
	(m_bluep + NBLUE - ((OFFSET + 1) / 2 + 2)) % NBLUE;
      double bluey;
      int redcenter = (m_redp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int greencenter = (m_greenp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int xx, yy;

      if (bluepos[(bluestart + 2) % NBLUE] == rendery)
	bluestart = (bluestart + 1) % NBLUE;
      assert (bluepos[(bluestart + 1) % NBLUE] <= rendery);
      assert (bluepos[(bluestart + 2) % NBLUE] > rendery);

      if (redpos[(redcenter + 1) % NRED] == rendery)
	redcenter = (redcenter + 1) % NRED;
      assert (redpos[(redcenter) % NBLUE] <= rendery);
      assert (redpos[(redcenter + 1) % NBLUE] > rendery);

      if (greenpos[(greencenter + 1) % NRED] == rendery)
	greencenter = (greencenter + 1) % NRED;
      assert (greenpos[(greencenter) % NBLUE] <= rendery);
      assert (greenpos[(greencenter + 1) % NBLUE] > rendery);
      for (yy = 0; yy < scale; yy++)
	{
	  bluey =
	    (rendery + ((double) yy) / scale -
	     bluepos[(bluestart + 1) % NBLUE]) / 2.0;
	  for (x = 8; x < m_scr_width * 4; x++)
	    for (xx = 0; xx < scale; xx++)
	      {
		double p[4][4];
		double red, green, blue;
		double xo, yo;
		int np;
		int bluex = (x - 1) / 2;
		double val;

		p[0][0] = bluesample[bluestart][bluex - 1];
		p[1][0] = bluesample[bluestart][bluex];
		p[2][0] = bluesample[bluestart][bluex + 1];
		p[3][0] = bluesample[bluestart][bluex + 2];
		p[0][1] = bluesample[(bluestart + 1) % NBLUE][bluex - 1];
		p[1][1] = bluesample[(bluestart + 1) % NBLUE][bluex];
		p[2][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 1];
		p[3][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 2];
		p[0][2] = bluesample[(bluestart + 2) % NBLUE][bluex - 1];
		p[1][2] = bluesample[(bluestart + 2) % NBLUE][bluex];
		p[2][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 1];
		p[3][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 2];
		p[0][3] = bluesample[(bluestart + 3) % NBLUE][bluex - 1];
		p[1][3] = bluesample[(bluestart + 3) % NBLUE][bluex];
		p[2][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 1];
		p[3][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 2];
		xo = (double) (x + ((double) xx) / scale - 1) / 2;
		blue = bicubicInterpolate (p, xo - (int) xo, bluey);
		{
		  int sx = ((x - redshift[redcenter]) + 2) / 4;
		  int dx = (x - redshift[redcenter]) - sx * 4;
		  int dy = rendery - redpos[redcenter];
		  int currcenter = redcenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (redcenter + NRED - 1) % NRED;
		      sx = ((x - redshift[currcenter]) + 2) / 4;
		    }
		  red = redsample[currcenter][sx];

		  /*red = getmatrixsample (redsample, redshift, currcenter, sx * 4 + redshift[currcenter], 0, 0); */
		  sx = sx * 4 + redshift[currcenter];
		  p[0][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     -1);
		  p[0][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     0);
		  p[0][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     1);
		  p[0][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     2);
		  p[1][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     -1);
		  p[1][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     0);
		  p[1][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     1);
		  p[1][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     2);
		  p[2][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     -1);
		  p[2][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     0);
		  p[2][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     1);
		  p[2][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     2);
		  p[3][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     -1);
		  p[3][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     0);
		  p[3][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     1);
		  p[3][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     2);
		  distx = x - sx;
		  disty = rendery - redpos[currcenter];
		  red =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		}
		{
		  int sx = ((x - greenshift[greencenter]) + 2) / 4;
		  int dx = (x - greenshift[greencenter]) - sx * 4;
		  int dy = rendery - greenpos[greencenter];
		  int currcenter = greencenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (greencenter + NRED - 1) % NRED;
		      sx = ((x - greenshift[currcenter]) + 2) / 4;
		    }
		  green = greensample[currcenter][sx];

		  /*green = getmatrixsample (greensample, greenshift, currcenter, sx * 4 + greenshift[currcenter], 0, 0); */
		  sx = sx * 4 + greenshift[currcenter];
		  p[0][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, -1);
		  p[0][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 0);
		  p[0][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 1);
		  p[0][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 2);
		  p[1][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, -1);
		  p[1][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 0);
		  p[1][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 1);
		  p[1][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 2);
		  p[2][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, -1);
		  p[2][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 0);
		  p[2][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 1);
		  p[2][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 2);
		  p[3][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, -1);
		  p[3][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 0);
		  p[3][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 1);
		  p[3][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 2);
		  distx = x - sx;
		  disty = rendery - greenpos[currcenter];
		  green =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		}


#if 0
		val =
		  get_img_pixel_scr ((x - m_scr_xshift * 4 + xx / (double) m_scale) / 4.0, (rendery - m_scr_yshift * 4 + yy / (double) m_scale) / 4.0);
		if (red != 0 || green != 0 || blue != 0)
		  {
		    double sum = (red + green + blue) * 0.33333;
		    red = red * val / sum;
		    green = green * val / sum;
		    blue = blue * val / sum;
		  }
		else
		  red = green = blue = val;
		int rr, gg, bb;
		set_color (red, green, blue, &rr, &gg, &bb);
#endif
		int rr, gg, bb;
		render_pixel ((x - m_scr_xshift * 4 + xx / (double) m_scale) / 4.0, (rendery - m_scr_yshift * 4 + yy / (double) m_scale) / 4.0,&rr, &gg, &bb);
		outrow[yy][(x * scale + xx)].r = rr;
		outrow[yy][(x * scale + xx)].g = gg;
		outrow[yy][(x * scale + xx)].b = bb;
	      }
	}
    }
}
#endif

void
render_interpolate::precompute (double xmin, double ymin, double xmax, double ymax)
{
  assert (!m_prec_red);
  /* We need to compute bit more to get interpolation right.
     TODO: figure out how much.  */
  m_prec_xshift = -(xmin - 4);
  m_prec_yshift = -(ymin - 4);
  m_prec_width = xmax - xmin + 8;
  m_prec_height = ymax - ymin + 8;
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
  #define pixel(xo,yo) get_img_pixel_scr ((x - m_prec_xshift) + xo, (y - m_prec_yshift) + yo)
  for (int x = 0; x < m_prec_width; x++)
    for (int y = 0 ; y < m_prec_height; y++)
      {
	prec_red (x, 2 * y) = pixel (-0.5, 0);
	prec_red (x, 2 * y + 1) = pixel (0, 0.5);
	prec_green (x, 2 * y) = pixel (0.0, 0);
	prec_green (x, 2 * y + 1) = pixel (0.5, 0.5);
	prec_blue (2 * x, 2 * y) = pixel (0.25, 0.25);
	prec_blue (2 * x + 1, 2 * y) = pixel (0.75, 0.25);
	prec_blue (2 * x, 2 * y + 1) = pixel (0.25, 0.75);
	prec_blue (2 * x + 1, 2 * y + 1) = pixel (0.75, 0.75);
      }
}

void
render_interpolate::render_pixel (double x, double y, int *r, int *g, int *b)
{
  x += m_prec_xshift;
  y += m_prec_yshift;

  int xp = 2*(x-0.25), yp = 2*(y-0.25);
  double xo = 2*(x-0.25) - xp, yo = 2*(y-0.25) - yp;
#define get_blue(xx, yy) prec_blue (xp + (xx), yp + (yy))
  double blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				   cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				   cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				   cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo),
				   xo);
#undef get_blue

  double xd, yd;
  to_diagonal_coordinates (x, y, &xd, &yd);
  xp = floor (xd);
  yp = floor (yd);
  xo = xd - xp;
  yo = yd - yp;
  /*xo=1;
  yo=1;*/
#define get_green(xx, yy) prec_diag_green (xp + (xx), yp + (yy))
  double green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				   cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				   cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				   cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo),
				   xo);
#undef get_green
  to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
  xp = floor (xd);
  yp = floor (yd);
  xo = xd - xp;
  yo = yd - yp;
  /*xo=1;
  yo=1;*/
#define get_red(xx, yy) prec_diag_red (xp + (xx), yp + (yy))
  double red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
				  cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
				  cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
				  cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo),
				  xo);
#undef get_red
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
