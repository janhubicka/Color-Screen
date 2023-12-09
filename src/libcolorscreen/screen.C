#include <math.h>
#include "dufaycolor.h"
#include "include/screen.h"
#include "gaussian-blur.h"

/* Produce empty screen.  */
void
screen::empty ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;
	mult[yy][xx][0] = 1;
	mult[yy][xx][1] = 1;
	mult[yy][xx][2] = 1;
      }
}
/* The screen is sqare.  In the center there is green circle
   of diameter DG, on corners there are red circles of diameter D  
   RR is a blurring radius.  */
#define D (85 * size) / 256
#define DG (85 * size) / 256

void
screen::thames ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
#define dist(x, y) (xx-(x)*size) * (xx-(x)*size) +  (yy-(y)*size) * (yy-(y)*size)
	int d11 = dist (0, 0);
	int d21 = dist (1, 0);
	int d22 = dist (1, 1);
	int d23 = dist (0, 1);
	int dc = dist (0.5, 0.5);
	int dl = dist (0, 0.5);
	int dr = dist (1, 0.5);
	int dt = dist (0.5, 0);
	int db = dist (0.5, 1);
	int d1, d3;
#undef dist

	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	if (d1 < ((size/2) - DG))
	  {
	    /* Green.  */
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 1;
	    mult[yy][xx][2] = 0;
	    continue;
	  }
	else if (d3 < ((size/2) - D))
	  {
	    /* Red.  */
	    mult[yy][xx][0] = 1;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 0;
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 1;
	  }
      }
}

#define PD (54 * size) / 256
#define PDG (54 * size) /256
void
screen::paget_finlay ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
#define dist(x, y) fabs (xx-(x)*size) + fabs (yy-(y)*size)
	int d11 = dist (0, 0);
	int d21 = dist (1, 0);
	int d22 = dist (1, 1);
	int d23 = dist (0, 1);
	int dc = dist (0.5, 0.5);
	int dl = dist (0, 0.5);
	int dr = dist (1, 0.5);
	int dt = dist (0.5, 0);
	int db = dist (0.5, 1);
	int d1, d3;
#undef dist

	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;

	d1 = fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc))));
	d3 = fmin (dl, fmin (dr, fmin (dt, db)));
	if (d1 < ((size/2) - PDG))
	  {
	    /* Green.  */
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 1;
	    mult[yy][xx][2] = 0;
	    continue;
	  }
	else if (d3 < ((size/2) - PD))
	  {
	    /* Red.  */
	    mult[yy][xx][0] = 1;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 0;
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 1;
	  }
      }
}

void
screen::dufay (coord_t red_strip_width, coord_t green_strip_width)
{
  if (!red_strip_width)
    red_strip_width = dufaycolor::red_strip_width;
  if (!green_strip_width)
    green_strip_width = dufaycolor::green_strip_width;

  coord_t strip_width = size / 2 * (1-red_strip_width);
  coord_t strip_height = size / 2 * green_strip_width;

  luminosity_t red[size];
  luminosity_t sum = 0;
  for (int yy = 0; yy < size; yy++)
    {
      if (yy >= ceil (strip_width) && yy + 1 <= floor (size - strip_width))
	red[yy] = 1;
      else if (yy + 1 <= floor (strip_width) || yy >= ceil (size - strip_width))
	red[yy] = 0;
      else if (yy == (int)strip_width)
	red[yy] = 1-(strip_width - yy);
      else if (yy == (int) (size - strip_width))
	red[yy] =  size - strip_width - yy;
      else
	{
	  //printf ("%i %f \n",yy,strip_width);
	  abort ();
	}
      sum += red[yy];
      assert (red[yy] >= 0 && red[yy] <= 1);
      assert (yy < size / 2 || red[yy] == red[size - 1 - yy]);
      //printf (" %f ", red[yy]);
    }
  //printf ("scr: %f %f %f", red_strip_width, sum / size, strip_width);
  assert (fabs (sum / size - red_strip_width) < 0.00001);
  luminosity_t green[size];
  sum = 0;
  for (int xx = 0; xx < size; xx++)
    {
      if (xx >= ceil (strip_height) && xx + 1 <= floor (size - strip_height))
	green[xx] = 0;
      else if (xx + 1 <= floor (strip_height) || xx >= ceil (size - strip_height))
	green[xx] = 1;
      else if (xx == (int)strip_height)
	green[xx] = (strip_height - xx);
      else if (xx == (int) (size - strip_height))
	green[xx] = 1 - (size - strip_height - xx);
      else
	{
	  ////printf ("b %i %f \n",xx,strip_height);
	  abort ();
	}
      sum += green[xx];
      assert (green[xx] >= 0 && green[xx] <= 1);
      assert (xx < size / 2 || green[xx] == green[size - 1 - xx]);
      //printf (" %f \n", green[xx]);
    }
  //printf ("%f %f %i %i %i\n",red_strip_width, green_strip_width,strip_width, strip_height, size);
  assert (fabs (sum / size - green_strip_width) < 0.00001);
  luminosity_t rsum = 0, gsum = 0, bsum = 0;
  for (int yy = 0; yy < size; yy++)
    for (int xx = 0; xx < size; xx++)
      {
	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;
	mult[yy][xx][0] = red[yy];
	mult[yy][xx][1] = green[xx] * (1 - red[yy]);
	mult[yy][xx][2] = 1 - mult[yy][xx][0] - mult[yy][xx][1];
	rsum += mult[yy][xx][0];
	gsum += mult[yy][xx][1];
	bsum += mult[yy][xx][2];
      }
  //printf ("%f %f %f\n",rsum, rsum / (size * size), red_strip_width);
  assert (fabs (rsum / (size * size) - red_strip_width) < 0.00001);
  //printf ("%f %f %f\n",gsum, gsum / (size * size), (1-red_strip_width) * green_strip_width);
  assert (fabs (gsum / (size * size) - (1-red_strip_width) * green_strip_width) < 0.00001);
  assert (fabs (bsum / (size * size) - (1-red_strip_width) * (1-green_strip_width)) < 0.00001);
}

/* This computes the grid displayed by UI.  */

void
screen::preview ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
#define dist(x, y) (xx-(x)*size) * (xx-(x)*size) +  (yy-(y)*size) * (yy-(y)*size)
	int d11 = dist (0, 0);
	int d21 = dist (1, 0);
	int d22 = dist (1, 1);
	int d23 = dist (0, 1);
	int dc = dist (0.5, 0.5);
	int dl = dist (0, 0.5);
	int dr = dist (1, 0.5);
	int dt = dist (0.5, 0);
	int db = dist (0.5, 1);
	int d1, d3;
#undef dist

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;
	mult[yy][xx][0] = 1;
	mult[yy][xx][1] = 1;
	mult[yy][xx][2] = 1;
	if (d1 < 30 * size / 256)
	  {
	    /* Green.  */
	    add[yy][xx][0] = 0;
	    add[yy][xx][1] = 0.5;
	    add[yy][xx][2] = 0;
	    mult[yy][xx][0] = 0.25;
	    mult[yy][xx][1] = 0.5;
	    mult[yy][xx][2] = 0.25;
	    continue;
	  }
	else if (d3 < 30 * size / 256)
	  {
	    /* Red.  */
	    add[yy][xx][0] = 0.5;
	    add[yy][xx][1] = 0;
	    add[yy][xx][2] = 0;
	    mult[yy][xx][0] = 0.5;
	    mult[yy][xx][1] = 0.25;
	    mult[yy][xx][2] = 0.25;
	    continue;
	  }
	else
	  {
	    if (xx < 10 * size / 256 || xx > size - 10 * size / 256 || yy < 10 * size / 256 || yy > size - 10 * size / 256)
	      {
	        /* Maybe blue.  */
		add[yy][xx][0] = 0;
		add[yy][xx][1] = 0;
		add[yy][xx][2] = 0.5;
		mult[yy][xx][0] = 0.25;
		mult[yy][xx][1] = 0.25;
		mult[yy][xx][2] = 0.5;
	      }
	  }
      }
}
void
screen::preview_dufay ()
{
  int xx, yy;
  int strip_height = size / 6;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;
	mult[yy][xx][0] = 1;
	mult[yy][xx][1] = 1;
	mult[yy][xx][2] = 1;
	if (yy < strip_height || yy > size-strip_height)
	  {
	    if (xx < size / 16 || xx > size - size / 16)
	      {
		add[yy][xx][0] = 0;
		add[yy][xx][1] = 0.5;
		add[yy][xx][2] = 0;
		mult[yy][xx][0] = 0.25;
		mult[yy][xx][1] = 0.5;
		mult[yy][xx][2] = 0.25;
	      }
	    if (xx > 7* size / 16 && xx < 9 * size / 16)
	      {
		add[yy][xx][0] = 0;
		add[yy][xx][1] = 0;
		add[yy][xx][2] = 0.5;
		mult[yy][xx][0] = 0.25;
		mult[yy][xx][1] = 0.25;
		mult[yy][xx][2] = 0.5;
	      }
	  }
	else  if (yy > 7* size / 16 && yy < 9 * size / 16)
	  {
	    add[yy][xx][0] = 0.5;
	    add[yy][xx][1] = 0;
	    add[yy][xx][2] = 0;
	    mult[yy][xx][0] = 0.5;
	    mult[yy][xx][1] = 0.25;
	    mult[yy][xx][2] = 0.25;
	  }
      }
}

void
screen::initialize_with_blur (screen &scr, coord_t blur_radius)
{
  if (blur_radius <= 0)
    {
      memcpy (mult, scr.mult, sizeof (mult));
      memcpy (add, scr.add, sizeof (add));
      return;
    }
  if (blur_radius >= 10)
    blur_radius = 10;

  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (blur_radius * size, &cmatrix);
  luminosity_t *hblur = (luminosity_t *)malloc (size * size * sizeof (luminosity_t));
  for (int c = 0; c < 3; c++)
    {
      for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++)
	  {
	    luminosity_t sum = 0;
	    for (int d = - clen / 2; d < clen / 2 ; d++)
	      sum += cmatrix[d + clen / 2] * scr.mult[y][(x + d) & (size - 1)][c];
	    hblur[x + y * size] = sum;
	  }
      for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++)
	  {
	    luminosity_t sum = 0;
	    for (int d = - clen / 2; d < clen / 2 ; d++)
	      sum += cmatrix[d + clen / 2] * hblur[x+ ((y + d) & (size - 1)) * size];
	    mult[y][x][c] = sum;
	  }
    }

  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      {
	add[y][x][0] = scr.add[y][x][0];
	add[y][x][1] = scr.add[y][x][1];
	add[y][x][2] = scr.add[y][x][2];
      }

  free (hblur);
  free (cmatrix);

#if 0
  if (blur_radius >= 1)
    blur_radius = 1;
  int radius = blur_radius * size;
  if (radius >= size)
    radius = size - 1;
  luminosity_t weights[size * 2][size * 2];
  luminosity_t weight = 0;

  for (int yy = 0; yy <= 2 * radius; yy++)
    for (int xx = 0; xx <= 2 * radius; xx++)
      {
        coord_t dist = sqrt ((yy-radius) * (yy - radius) + (xx - radius) * (xx - radius)) / size;
        if (dist < blur_radius)
	  {
	    coord_t w = blur_radius - dist;
	    weights[yy][xx] = w;
	    weight += w;
	  }
	else
	  weights[yy][xx] = 0;
      }
  #pragma omp parallel for default(none) shared(radius,scr,weights,weight)
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      {
	luminosity_t r = 0, g = 0, b = 0;
	for (int yy = y - radius; yy <= y + radius; yy++)
	  for (int xx = x - radius; xx <= x + radius; xx++)
	    {
	      luminosity_t w = weights [yy - (y - radius)][xx - (x - radius)];
	      r += scr.mult[(yy + size) & (size - 1)][(xx + size) & (size - 1)][0] * w;
	      g += scr.mult[(yy + size) & (size - 1)][(xx + size) & (size - 1)][1] * w;
	      b += scr.mult[(yy + size) & (size - 1)][(xx + size) & (size - 1)][2] * w;
	    }
	mult[y][x][0] = r / weight;
	mult[y][x][1] = g / weight;
	mult[y][x][2] = b / weight;
	add[y][x][0] = scr.add[y][x][0];
	add[y][x][1] = scr.add[y][x][1];
	add[y][x][2] = scr.add[y][x][2];
      }
#endif
}
void
screen::initialize (enum scr_type type, coord_t red_strip_width, coord_t green_strip_width)
{
  switch (type)
  {
    case Finlay:
    case Paget:
      paget_finlay ();
      break;
    case Dufay:
      dufay (red_strip_width, green_strip_width);
      break;
    case Thames:
      thames ();
      break;
    default:
      abort ();
      break;
  }
}
/* Initialize to a given screen for preview window.  */
void
screen::initialize_preview (enum scr_type type)
{
if (type == Dufay)
  preview_dufay ();
else
  preview ();
}
