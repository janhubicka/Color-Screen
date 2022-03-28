#include <math.h>
#include "screen.h"

/* Produce empty screen.  */
void
screen::empty ()
{
  int xx, yy;
  for (xx = 0; xx < size; xx++)
    for (yy = 0; yy < size; yy++)
      {
	add[xx][yy][0] = 0;
	add[xx][yy][1] = 0;
	add[xx][yy][2] = 0;
	mult[xx][yy][0] = 1;
	mult[xx][yy][1] = 1;
	mult[xx][yy][2] = 1;
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
  for (xx = 0; xx < size; xx++)
    for (yy = 0; yy < size; yy++)
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

	add[xx][yy][0] = 0;
	add[xx][yy][1] = 0;
	add[xx][yy][2] = 0;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	if (d1 < ((size/2) - DG))
	  {
	    /* Blue.  */
	    mult[xx][yy][0] = 0.714;
	    mult[xx][yy][1] = 0.192;
	    mult[xx][yy][2] = 0.298;
	    continue;
	  }
	else if (d3 < ((size/2) - D))
	  {
	    /* Green.  */
	    mult[xx][yy][0] = 0.275;
	    mult[xx][yy][1] = 0.557;
	    mult[xx][yy][2] = 0.463;
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[xx][yy][0] = 0.435;
	    mult[xx][yy][1] = 0.388;
	    mult[xx][yy][2] = 0.584;
	  }
      }
}

#define PD (58 * size) / size
#define PDG (58 * size) / size
void
screen::paget_finlay ()
{
  int xx, yy;
  for (xx = 0; xx < size; xx++)
    for (yy = 0; yy < size; yy++)
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

	add[xx][yy][0] = 0;
	add[xx][yy][1] = 0;
	add[xx][yy][2] = 0;

	d1 = fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc))));
	d3 = fmin (dl, fmin (dr, fmin (dt, db)));
	if (d1 < ((size/2) - PDG))
	  {
	    /* Blue.  */
	    mult[xx][yy][0] = 0.714;
	    mult[xx][yy][1] = 0.192;
	    mult[xx][yy][2] = 0.298;
	    continue;
	  }
	else if (d3 < ((size/2) - PD))
	  {
	    /* Green.  */
	    mult[xx][yy][0] = 0.275;
	    mult[xx][yy][1] = 0.557;
	    mult[xx][yy][2] = 0.463;
	    continue;
	  }
	else
	  {
	    /* Blue.  */
	    mult[xx][yy][0] = 0.435;
	    mult[xx][yy][1] = 0.388;
	    mult[xx][yy][2] = 0.584;
	  }
      }
}

/* This computes the grid displayed by UI.  */

void
screen::preview ()
{
  int xx, yy;
  for (xx = 0; xx < size; xx++)
    for (yy = 0; yy < size; yy++)
      {
	int d11 = xx * xx + yy * yy;
	int d21 = (size - xx) * (size - xx) + yy * yy;
	int d22 = (size - xx) * (size - xx) + (size - yy) * (size - yy);
	int d23 = xx * xx + (size - yy) * (size - yy);
	int dc = ((size/2) - xx) * ((size/2) - xx) + ((size/2) - yy) * ((size/2) - yy);
	int dl = xx * xx + ((size/2) - yy) * ((size/2) - yy);
	int dr = (size - xx) * (size - xx) + ((size/2) - yy) * ((size/2) - yy);
	int dt = ((size/2) - xx) * ((size/2) - xx) + (yy) * (yy);
	int db = ((size/2) - xx) * ((size/2) - xx) + (size - yy) * (size - yy);
	int d1, d3;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	add[xx][yy][0] = 0;
	add[xx][yy][1] = 0;
	add[xx][yy][2] = 0;
	mult[xx][yy][0] = 1;
	mult[xx][yy][1] = 1;
	mult[xx][yy][2] = 1;
	if (d1 < 30)
	  {
	    add[xx][yy][0] = 0.5;
	    add[xx][yy][1] = 0;
	    add[xx][yy][2] = 0;
	    mult[xx][yy][0] = 0.5;
	    mult[xx][yy][1] = 0.25;
	    mult[xx][yy][2] = 0.25;
	    continue;
	  }
	else if (d3 < 30)
	  {
	    add[xx][yy][0] = 0;
	    add[xx][yy][1] = 0.5;
	    add[xx][yy][2] = 0;
	    mult[xx][yy][0] = 0.25;
	    mult[xx][yy][1] = 0.5;
	    mult[xx][yy][2] = 0.25;
	    continue;
	  }
	else
	  {
	    if (xx < 10 || xx > size - 10 || yy < 10 || yy > size - 10)
	      {
		add[xx][yy][0] = 0;
		add[xx][yy][1] = 0;
		add[xx][yy][2] = 0.5;
		mult[xx][yy][0] = 0.25;
		mult[xx][yy][1] = 0.25;
		mult[xx][yy][2] = 0.5;
	      }
	  }
      }
}
