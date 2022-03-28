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
#define D (70 * size) / size
#define DG (70 * size) / size

/* This should render the viewing screen, but because I though the older Thames screen was used
   it is wrong: it renders color dots rather than diagonal squares.
   It is not used anymore since I implemented better rendering algorithm.  */

#define RR 2048
void
screen::thames ()
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

	add[xx][yy][0] = 0;
	add[xx][yy][1] = 0.5;
	add[xx][yy][2] = 0;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	if (d1 < ((size/2) - DG))
	  {
	    mult[xx][yy][0] = RR * ((0.5 - d1 - DG) / (0.5 - DG));
	    if (mult[xx][yy][0] > 1)
	      mult[xx][yy][0] = 1;
	    mult[xx][yy][1] = 0;
	    mult[xx][yy][2] = 1 - mult[xx][yy][0];
	    continue;
	  }
	else if (d3 < ((size/2) - D))
	  {
	    mult[xx][yy][1] = RR * ((0.5 - d3 - D) / (0.5 - D));
	    if (mult[xx][yy][1] > 1)
	      mult[xx][yy][1] = 1;
	    mult[xx][yy][2] = 0;
	    mult[xx][yy][2] = 1 - mult[xx][yy][1];
	    continue;
	  }
	else
	  {
	    mult[xx][yy][0] = 0;
	    mult[xx][yy][1] = 0;
	    mult[xx][yy][2] = 1;
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
