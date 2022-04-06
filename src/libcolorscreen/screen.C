#include <math.h>
#include "include/screen.h"

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
screen::dufay ()
{
  int xx, yy;
  int strip_height = size / 3;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
	if (yy < strip_height || yy > size-strip_height)
	  {
	    if (xx < size / 4 || xx > size - size / 4)
	      {
		mult[yy][xx][0] = 0;
		mult[yy][xx][1] = 1;
		mult[yy][xx][2] = 0;
	      }
	    else
	      {
		mult[yy][xx][0] = 0;
		mult[yy][xx][1] = 0;
		mult[yy][xx][2] = 1;
	      }
	  }
	else
	  {
	    mult[yy][xx][0] = 1;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 0;
	  }
	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;
      }
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
}
