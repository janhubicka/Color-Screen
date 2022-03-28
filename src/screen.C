#include <math.h>
#include "screen.h"

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
#define D (85 * size) / size
#define DG (85 * size) / size

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
	    mult[yy][xx][0] = 0.714;
	    mult[yy][xx][1] = 0.192;
	    mult[yy][xx][2] = 0.298;
	    continue;
	  }
	else if (d3 < ((size/2) - D))
	  {
	    /* Red.  */
	    mult[yy][xx][0] = 0.275;
	    mult[yy][xx][1] = 0.557;
	    mult[yy][xx][2] = 0.463;
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[yy][xx][0] = 0.435;
	    mult[yy][xx][1] = 0.388;
	    mult[yy][xx][2] = 0.584;
	  }
	mult[yy][xx][0] = pow (mult[yy][xx][0], 2.2);
	mult[yy][xx][1] = pow (mult[yy][xx][1], 2.2);
	mult[yy][xx][2] = pow (mult[yy][xx][2], 2.2);
      }
}

#define PD (58 * size) / size
#define PDG (58 * size) / size
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
	    mult[yy][xx][0] = 0.56;
	    mult[yy][xx][1] = 0.771;
	    mult[yy][xx][2] = 0.0590;
#if 0
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 1;
	    mult[yy][xx][2] = 0;
#endif
	    continue;
	  }
	else if (d3 < ((size/2) - PD))
	  {
	    /* Red.  */
	    mult[yy][xx][0] = 0.768;
	    mult[yy][xx][1] = 0.0;
	    mult[yy][xx][2] = 0.442;
#if 0
	    mult[yy][xx][0] = 1;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 0;
#endif
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[yy][xx][0] = 0.45;
	    mult[yy][xx][1] = 0.015;
	    mult[yy][xx][2] = 0.683;
#if 0
	    mult[yy][xx][0] = 0;
	    mult[yy][xx][1] = 0;
	    mult[yy][xx][2] = 1;
#endif
	  }
	mult[yy][xx][0] = pow (mult[yy][xx][0], 2.2);
	mult[yy][xx][1] = pow (mult[yy][xx][1], 2.2);
	mult[yy][xx][2] = pow (mult[yy][xx][2], 2.2);
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
	if (d1 < 30)
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
	else if (d3 < 30)
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
	    if (xx < 10 || xx > size - 10 || yy < 10 || yy > size - 10)
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
