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
      assert (yy < size / 2 || fabs (red[yy] - red[size - 1 - yy]) < 0.0000001);
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
      assert (xx < size / 2 || fabs (green[xx] - green[size - 1 - xx]) < 0.0000001);
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

__attribute__ ((always_inline))
inline void
screen::initialize_with_blur (screen &scr, int clen, luminosity_t *cmatrix, luminosity_t *hblur)
{
  for (int c = 0; c < 3; c++)
    {
//#pragma omp parallel shared(scr, clen, cmatrix, hblur,c)
      for (int y = 0; y < size; y++)
	{
	  luminosity_t mmult[size + clen];
	  /* Make internal loop vectorizable by copying out data in right order.  */
	  for (int x = 0; x < size + clen; x++)
	    mmult[x] = scr.mult[y][(x - clen / 2) & (size - 1)][c];
	  for (int x = 0; x < size; x++)
	    {
	      luminosity_t sum = 0;
	      for (int d = - clen / 2; d < clen / 2 ; d++)
		//sum += cmatrix[d + clen / 2] * scr.mult[y][(x + d) & (size - 1)][c];
		sum += cmatrix[d + clen / 2] * mmult[x + d + clen / 2];
	      hblur[x + y * size] = sum;
	    }
	}
//#pragma omp parallel shared(scr, clen, cmatrix, hblur,c)
      for (int x = 0; x < size; x++)
	{
	  luminosity_t mmult[size + clen];
	  /* Make internal loop vectorizable by copying out data in right order.  */
	  for (int y = 0; y < size + clen; y++)
	    mmult[y] = hblur[x + ((y - clen / 2) & (size - 1)) * size];
          for (int y = 0; y < size; y++)
	    {
	      luminosity_t sum = 0;
	      for (int d = - clen / 2; d < clen / 2 ; d++)
		//sum += cmatrix[d + clen / 2] * hblur[x+ ((y + d) & (size - 1)) * size];
		sum += cmatrix[d + clen / 2] * mmult[y + d + clen / 2];
	      mult[y][x][c] = sum;
	    }
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
  if (blur_radius >= max_blur_radius)
    blur_radius = max_blur_radius;

  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (blur_radius * size, &cmatrix);
  luminosity_t hblur[size][size]; //= (luminosity_t *)malloc (size * size * sizeof (luminosity_t));
  /* Finetuning solver keeps recomputing screens with different blurs.  Specialize
     internal loops.  */
  switch (clen)
    {
#if 1
     case 1: initialize_with_blur (scr, 1, cmatrix, &hblur[0][0]); break;
     case 3: initialize_with_blur (scr, 3, cmatrix, &hblur[0][0]); break;
     case 5: initialize_with_blur (scr, 5, cmatrix, &hblur[0][0]); break;
     case 7: initialize_with_blur (scr, 7, cmatrix, &hblur[0][0]); break;
     case 9: initialize_with_blur (scr, 9, cmatrix, &hblur[0][0]); break;
     case 11: initialize_with_blur (scr, 11, cmatrix, &hblur[0][0]); break;
     case 13: initialize_with_blur (scr, 13, cmatrix, &hblur[0][0]); break;
     case 15: initialize_with_blur (scr, 15, cmatrix, &hblur[0][0]); break;
     case 17: initialize_with_blur (scr, 17, cmatrix, &hblur[0][0]); break;
     case 19: initialize_with_blur (scr, 19, cmatrix, &hblur[0][0]); break;
     case 21: initialize_with_blur (scr, 21, cmatrix, &hblur[0][0]); break;
     case 23: initialize_with_blur (scr, 23, cmatrix, &hblur[0][0]); break;
     case 25: initialize_with_blur (scr, 25, cmatrix, &hblur[0][0]); break;
     case 27: initialize_with_blur (scr, 27, cmatrix, &hblur[0][0]); break;
     case 29: initialize_with_blur (scr, 29, cmatrix, &hblur[0][0]); break;
     case 31: initialize_with_blur (scr, 31, cmatrix, &hblur[0][0]); break;
     case 33: initialize_with_blur (scr, 33, cmatrix, &hblur[0][0]); break;
     case 35: initialize_with_blur (scr, 35, cmatrix, &hblur[0][0]); break;
     case 37: initialize_with_blur (scr, 37, cmatrix, &hblur[0][0]); break;
     case 39: initialize_with_blur (scr, 39, cmatrix, &hblur[0][0]); break;
     case 41: initialize_with_blur (scr, 41, cmatrix, &hblur[0][0]); break;
     case 43: initialize_with_blur (scr, 43, cmatrix, &hblur[0][0]); break;
     case 45: initialize_with_blur (scr, 45, cmatrix, &hblur[0][0]); break;
     case 47: initialize_with_blur (scr, 47, cmatrix, &hblur[0][0]); break;
     case 49: initialize_with_blur (scr, 49, cmatrix, &hblur[0][0]); break;
     case 51: initialize_with_blur (scr, 51, cmatrix, &hblur[0][0]); break;
     case 53: initialize_with_blur (scr, 53, cmatrix, &hblur[0][0]); break;
     case 55: initialize_with_blur (scr, 55, cmatrix, &hblur[0][0]); break;
     case 57: initialize_with_blur (scr, 57, cmatrix, &hblur[0][0]); break;
     case 59: initialize_with_blur (scr, 59, cmatrix, &hblur[0][0]); break;
     case 61: initialize_with_blur (scr, 61, cmatrix, &hblur[0][0]); break;
     case 63: initialize_with_blur (scr, 63, cmatrix, &hblur[0][0]); break;
     case 65: initialize_with_blur (scr, 55, cmatrix, &hblur[0][0]); break;
     case 67: initialize_with_blur (scr, 67, cmatrix, &hblur[0][0]); break;
     case 69: initialize_with_blur (scr, 69, cmatrix, &hblur[0][0]); break;
     case 71: initialize_with_blur (scr, 71, cmatrix, &hblur[0][0]); break;
     case 73: initialize_with_blur (scr, 73, cmatrix, &hblur[0][0]); break;
     case 75: initialize_with_blur (scr, 75, cmatrix, &hblur[0][0]); break;
     case 77: initialize_with_blur (scr, 77, cmatrix, &hblur[0][0]); break;
     case 79: initialize_with_blur (scr, 79, cmatrix, &hblur[0][0]); break;
     case 81: initialize_with_blur (scr, 81, cmatrix, &hblur[0][0]); break;
     case 83: initialize_with_blur (scr, 83, cmatrix, &hblur[0][0]); break;
     case 85: initialize_with_blur (scr, 85, cmatrix, &hblur[0][0]); break;
     case 87: initialize_with_blur (scr, 87, cmatrix, &hblur[0][0]); break;
     case 89: initialize_with_blur (scr, 89, cmatrix, &hblur[0][0]); break;
     case 91: initialize_with_blur (scr, 91, cmatrix, &hblur[0][0]); break;
     case 93: initialize_with_blur (scr, 93, cmatrix, &hblur[0][0]); break;
     case 95: initialize_with_blur (scr, 95, cmatrix, &hblur[0][0]); break;
     case 97: initialize_with_blur (scr, 97, cmatrix, &hblur[0][0]); break;
     case 99: initialize_with_blur (scr, 99, cmatrix, &hblur[0][0]); break;

     case 101: initialize_with_blur (scr, 101, cmatrix, &hblur[0][0]); break;
     case 103: initialize_with_blur (scr, 103, cmatrix, &hblur[0][0]); break;
     case 105: initialize_with_blur (scr, 105, cmatrix, &hblur[0][0]); break;
     case 107: initialize_with_blur (scr, 107, cmatrix, &hblur[0][0]); break;
     case 109: initialize_with_blur (scr, 109, cmatrix, &hblur[0][0]); break;
     case 111: initialize_with_blur (scr, 111, cmatrix, &hblur[0][0]); break;
     case 113: initialize_with_blur (scr, 113, cmatrix, &hblur[0][0]); break;
     case 115: initialize_with_blur (scr, 115, cmatrix, &hblur[0][0]); break;
     case 117: initialize_with_blur (scr, 117, cmatrix, &hblur[0][0]); break;
     case 119: initialize_with_blur (scr, 119, cmatrix, &hblur[0][0]); break;
     case 121: initialize_with_blur (scr, 121, cmatrix, &hblur[0][0]); break;
     case 123: initialize_with_blur (scr, 123, cmatrix, &hblur[0][0]); break;
     case 125: initialize_with_blur (scr, 125, cmatrix, &hblur[0][0]); break;
     case 127: initialize_with_blur (scr, 127, cmatrix, &hblur[0][0]); break;
     case 129: initialize_with_blur (scr, 129, cmatrix, &hblur[0][0]); break;
     case 131: initialize_with_blur (scr, 131, cmatrix, &hblur[0][0]); break;
     case 133: initialize_with_blur (scr, 133, cmatrix, &hblur[0][0]); break;
     case 135: initialize_with_blur (scr, 135, cmatrix, &hblur[0][0]); break;
     case 137: initialize_with_blur (scr, 137, cmatrix, &hblur[0][0]); break;
     case 139: initialize_with_blur (scr, 139, cmatrix, &hblur[0][0]); break;
     case 141: initialize_with_blur (scr, 141, cmatrix, &hblur[0][0]); break;
     case 143: initialize_with_blur (scr, 143, cmatrix, &hblur[0][0]); break;
     case 145: initialize_with_blur (scr, 145, cmatrix, &hblur[0][0]); break;
     case 147: initialize_with_blur (scr, 147, cmatrix, &hblur[0][0]); break;
     case 149: initialize_with_blur (scr, 149, cmatrix, &hblur[0][0]); break;
     case 151: initialize_with_blur (scr, 151, cmatrix, &hblur[0][0]); break;
     case 153: initialize_with_blur (scr, 153, cmatrix, &hblur[0][0]); break;
     case 155: initialize_with_blur (scr, 155, cmatrix, &hblur[0][0]); break;
     case 157: initialize_with_blur (scr, 157, cmatrix, &hblur[0][0]); break;
     case 159: initialize_with_blur (scr, 159, cmatrix, &hblur[0][0]); break;
     case 161: initialize_with_blur (scr, 161, cmatrix, &hblur[0][0]); break;
     case 163: initialize_with_blur (scr, 163, cmatrix, &hblur[0][0]); break;
     case 165: initialize_with_blur (scr, 155, cmatrix, &hblur[0][0]); break;
     case 167: initialize_with_blur (scr, 167, cmatrix, &hblur[0][0]); break;
     case 169: initialize_with_blur (scr, 169, cmatrix, &hblur[0][0]); break;
     case 171: initialize_with_blur (scr, 171, cmatrix, &hblur[0][0]); break;
     case 173: initialize_with_blur (scr, 173, cmatrix, &hblur[0][0]); break;
     case 175: initialize_with_blur (scr, 175, cmatrix, &hblur[0][0]); break;
     case 177: initialize_with_blur (scr, 177, cmatrix, &hblur[0][0]); break;
     case 179: initialize_with_blur (scr, 179, cmatrix, &hblur[0][0]); break;
     case 181: initialize_with_blur (scr, 181, cmatrix, &hblur[0][0]); break;
     case 183: initialize_with_blur (scr, 183, cmatrix, &hblur[0][0]); break;
     case 185: initialize_with_blur (scr, 185, cmatrix, &hblur[0][0]); break;
     case 187: initialize_with_blur (scr, 187, cmatrix, &hblur[0][0]); break;
     case 189: initialize_with_blur (scr, 189, cmatrix, &hblur[0][0]); break;
     case 191: initialize_with_blur (scr, 191, cmatrix, &hblur[0][0]); break;
     case 193: initialize_with_blur (scr, 193, cmatrix, &hblur[0][0]); break;
     case 195: initialize_with_blur (scr, 195, cmatrix, &hblur[0][0]); break;
     case 197: initialize_with_blur (scr, 197, cmatrix, &hblur[0][0]); break;
     case 199: initialize_with_blur (scr, 199, cmatrix, &hblur[0][0]); break;

     case 201: initialize_with_blur (scr, 201, cmatrix, &hblur[0][0]); break;
     case 203: initialize_with_blur (scr, 203, cmatrix, &hblur[0][0]); break;
     case 205: initialize_with_blur (scr, 205, cmatrix, &hblur[0][0]); break;
     case 207: initialize_with_blur (scr, 207, cmatrix, &hblur[0][0]); break;
     case 209: initialize_with_blur (scr, 209, cmatrix, &hblur[0][0]); break;
     case 211: initialize_with_blur (scr, 211, cmatrix, &hblur[0][0]); break;
     case 213: initialize_with_blur (scr, 213, cmatrix, &hblur[0][0]); break;
     case 215: initialize_with_blur (scr, 215, cmatrix, &hblur[0][0]); break;
     case 217: initialize_with_blur (scr, 217, cmatrix, &hblur[0][0]); break;
     case 219: initialize_with_blur (scr, 219, cmatrix, &hblur[0][0]); break;
     case 221: initialize_with_blur (scr, 221, cmatrix, &hblur[0][0]); break;
     case 223: initialize_with_blur (scr, 223, cmatrix, &hblur[0][0]); break;
     case 225: initialize_with_blur (scr, 225, cmatrix, &hblur[0][0]); break;
     case 227: initialize_with_blur (scr, 227, cmatrix, &hblur[0][0]); break;
     case 229: initialize_with_blur (scr, 229, cmatrix, &hblur[0][0]); break;
     case 231: initialize_with_blur (scr, 231, cmatrix, &hblur[0][0]); break;
     case 233: initialize_with_blur (scr, 233, cmatrix, &hblur[0][0]); break;
     case 235: initialize_with_blur (scr, 235, cmatrix, &hblur[0][0]); break;
     case 237: initialize_with_blur (scr, 237, cmatrix, &hblur[0][0]); break;
     case 239: initialize_with_blur (scr, 239, cmatrix, &hblur[0][0]); break;
     case 241: initialize_with_blur (scr, 241, cmatrix, &hblur[0][0]); break;
     case 243: initialize_with_blur (scr, 243, cmatrix, &hblur[0][0]); break;
     case 245: initialize_with_blur (scr, 245, cmatrix, &hblur[0][0]); break;
     case 247: initialize_with_blur (scr, 247, cmatrix, &hblur[0][0]); break;
     case 249: initialize_with_blur (scr, 249, cmatrix, &hblur[0][0]); break;
	printf ("unspecialized clen %i %f\n", clen, blur_radius);
#endif
     default:
        initialize_with_blur (scr, clen, cmatrix, &hblur[0][0]);
    }

  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      {
	add[y][x][0] = scr.add[y][x][0];
	add[y][x][1] = scr.add[y][x][1];
	add[y][x][2] = scr.add[y][x][2];
      }

  //free (hblur);
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
