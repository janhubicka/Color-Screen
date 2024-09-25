#include <math.h>
#include <memory>
#include <array>
#include "include/tiff-writer.h"
#include "include/dufaycolor.h"
#include "screen.h"
#include "gaussian-blur.h"
#include "dj_fft.h"
#include "fftw3.h"
#include "spline.h"
#include "icc.h"
namespace colorscreen
{
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
#define dist(x, y) fabs (xx + 0.5-(x)*size) + fabs (yy + 0.5-(y)*size)
	float d11 = dist (0, 0);
	float d21 = dist (1, 0);
	float d22 = dist (1, 1);
	float d23 = dist (0, 1);
	float dc = dist (0.5, 0.5);
	float dl = dist (0, 0.5);
	float dr = dist (1, 0.5);
	float dt = dist (0.5, 0);
	float db = dist (0.5, 1);
	float d1, d3;
#undef dist

	add[yy][xx][0] = 0;
	add[yy][xx][1] = 0;
	add[yy][xx][2] = 0;

	d1 = fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc))));
	d3 = fmin (dl, fmin (dr, fmin (dt, db)));
	if (d1 < ((size/2) - PDG) && d1 < d3)
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
screen::initialize_with_gaussian_blur (screen &scr, int clen, luminosity_t *cmatrix, luminosity_t *hblur, int c)
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

void
screen::initialize_with_gaussian_blur (screen &scr, coord_t blur_radius, int cmin, int cmax)
{
  if (blur_radius >= max_blur_radius)
    blur_radius = max_blur_radius;

  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (blur_radius * size, &cmatrix);
  luminosity_t hblur[size][size]; //= (luminosity_t *)malloc (size * size * sizeof (luminosity_t));
  /* Finetuning solver keeps recomputing screens with different blurs.  Specialize
     internal loops.  */
  for(int channel = cmin; channel <= cmax; channel++)
    switch (clen)
      {
#if 1
       case 1: initialize_with_gaussian_blur (scr, 1, cmatrix, &hblur[0][0], channel); break;
       case 3: initialize_with_gaussian_blur (scr, 3, cmatrix, &hblur[0][0], channel); break;
       case 5: initialize_with_gaussian_blur (scr, 5, cmatrix, &hblur[0][0], channel); break;
       case 7: initialize_with_gaussian_blur (scr, 7, cmatrix, &hblur[0][0], channel); break;
       case 9: initialize_with_gaussian_blur (scr, 9, cmatrix, &hblur[0][0], channel); break;
       case 11: initialize_with_gaussian_blur (scr, 11, cmatrix, &hblur[0][0], channel); break;
       case 13: initialize_with_gaussian_blur (scr, 13, cmatrix, &hblur[0][0], channel); break;
       case 15: initialize_with_gaussian_blur (scr, 15, cmatrix, &hblur[0][0], channel); break;
       case 17: initialize_with_gaussian_blur (scr, 17, cmatrix, &hblur[0][0], channel); break;
       case 19: initialize_with_gaussian_blur (scr, 19, cmatrix, &hblur[0][0], channel); break;
       case 21: initialize_with_gaussian_blur (scr, 21, cmatrix, &hblur[0][0], channel); break;
       case 23: initialize_with_gaussian_blur (scr, 23, cmatrix, &hblur[0][0], channel); break;
       case 25: initialize_with_gaussian_blur (scr, 25, cmatrix, &hblur[0][0], channel); break;
       case 27: initialize_with_gaussian_blur (scr, 27, cmatrix, &hblur[0][0], channel); break;
       case 29: initialize_with_gaussian_blur (scr, 29, cmatrix, &hblur[0][0], channel); break;
       case 31: initialize_with_gaussian_blur (scr, 31, cmatrix, &hblur[0][0], channel); break;
       case 33: initialize_with_gaussian_blur (scr, 33, cmatrix, &hblur[0][0], channel); break;
       case 35: initialize_with_gaussian_blur (scr, 35, cmatrix, &hblur[0][0], channel); break;
       case 37: initialize_with_gaussian_blur (scr, 37, cmatrix, &hblur[0][0], channel); break;
       case 39: initialize_with_gaussian_blur (scr, 39, cmatrix, &hblur[0][0], channel); break;
       case 41: initialize_with_gaussian_blur (scr, 41, cmatrix, &hblur[0][0], channel); break;
       case 43: initialize_with_gaussian_blur (scr, 43, cmatrix, &hblur[0][0], channel); break;
       case 45: initialize_with_gaussian_blur (scr, 45, cmatrix, &hblur[0][0], channel); break;
       case 47: initialize_with_gaussian_blur (scr, 47, cmatrix, &hblur[0][0], channel); break;
       case 49: initialize_with_gaussian_blur (scr, 49, cmatrix, &hblur[0][0], channel); break;
       case 51: initialize_with_gaussian_blur (scr, 51, cmatrix, &hblur[0][0], channel); break;
       case 53: initialize_with_gaussian_blur (scr, 53, cmatrix, &hblur[0][0], channel); break;
       case 55: initialize_with_gaussian_blur (scr, 55, cmatrix, &hblur[0][0], channel); break;
       case 57: initialize_with_gaussian_blur (scr, 57, cmatrix, &hblur[0][0], channel); break;
       case 59: initialize_with_gaussian_blur (scr, 59, cmatrix, &hblur[0][0], channel); break;
       case 61: initialize_with_gaussian_blur (scr, 61, cmatrix, &hblur[0][0], channel); break;
       case 63: initialize_with_gaussian_blur (scr, 63, cmatrix, &hblur[0][0], channel); break;
       case 65: initialize_with_gaussian_blur (scr, 65, cmatrix, &hblur[0][0], channel); break;
       case 67: initialize_with_gaussian_blur (scr, 67, cmatrix, &hblur[0][0], channel); break;
       case 69: initialize_with_gaussian_blur (scr, 69, cmatrix, &hblur[0][0], channel); break;
       case 71: initialize_with_gaussian_blur (scr, 71, cmatrix, &hblur[0][0], channel); break;
       case 73: initialize_with_gaussian_blur (scr, 73, cmatrix, &hblur[0][0], channel); break;
       case 75: initialize_with_gaussian_blur (scr, 75, cmatrix, &hblur[0][0], channel); break;
       case 77: initialize_with_gaussian_blur (scr, 77, cmatrix, &hblur[0][0], channel); break;
       case 79: initialize_with_gaussian_blur (scr, 79, cmatrix, &hblur[0][0], channel); break;
       case 81: initialize_with_gaussian_blur (scr, 81, cmatrix, &hblur[0][0], channel); break;
       case 83: initialize_with_gaussian_blur (scr, 83, cmatrix, &hblur[0][0], channel); break;
       case 85: initialize_with_gaussian_blur (scr, 85, cmatrix, &hblur[0][0], channel); break;
       case 87: initialize_with_gaussian_blur (scr, 87, cmatrix, &hblur[0][0], channel); break;
       case 89: initialize_with_gaussian_blur (scr, 89, cmatrix, &hblur[0][0], channel); break;
       case 91: initialize_with_gaussian_blur (scr, 91, cmatrix, &hblur[0][0], channel); break;
       case 93: initialize_with_gaussian_blur (scr, 93, cmatrix, &hblur[0][0], channel); break;
       case 95: initialize_with_gaussian_blur (scr, 95, cmatrix, &hblur[0][0], channel); break;
       case 97: initialize_with_gaussian_blur (scr, 97, cmatrix, &hblur[0][0], channel); break;
       case 99: initialize_with_gaussian_blur (scr, 99, cmatrix, &hblur[0][0], channel); break;

       case 101: initialize_with_gaussian_blur (scr, 101, cmatrix, &hblur[0][0], channel); break;
       case 103: initialize_with_gaussian_blur (scr, 103, cmatrix, &hblur[0][0], channel); break;
       case 105: initialize_with_gaussian_blur (scr, 105, cmatrix, &hblur[0][0], channel); break;
       case 107: initialize_with_gaussian_blur (scr, 107, cmatrix, &hblur[0][0], channel); break;
       case 109: initialize_with_gaussian_blur (scr, 109, cmatrix, &hblur[0][0], channel); break;
       case 111: initialize_with_gaussian_blur (scr, 111, cmatrix, &hblur[0][0], channel); break;
       case 113: initialize_with_gaussian_blur (scr, 113, cmatrix, &hblur[0][0], channel); break;
       case 115: initialize_with_gaussian_blur (scr, 115, cmatrix, &hblur[0][0], channel); break;
       case 117: initialize_with_gaussian_blur (scr, 117, cmatrix, &hblur[0][0], channel); break;
       case 119: initialize_with_gaussian_blur (scr, 119, cmatrix, &hblur[0][0], channel); break;
       case 121: initialize_with_gaussian_blur (scr, 121, cmatrix, &hblur[0][0], channel); break;
       case 123: initialize_with_gaussian_blur (scr, 123, cmatrix, &hblur[0][0], channel); break;
       case 125: initialize_with_gaussian_blur (scr, 125, cmatrix, &hblur[0][0], channel); break;
       case 127: initialize_with_gaussian_blur (scr, 127, cmatrix, &hblur[0][0], channel); break;
       case 129: initialize_with_gaussian_blur (scr, 129, cmatrix, &hblur[0][0], channel); break;
       case 131: initialize_with_gaussian_blur (scr, 131, cmatrix, &hblur[0][0], channel); break;
       case 133: initialize_with_gaussian_blur (scr, 133, cmatrix, &hblur[0][0], channel); break;
       case 135: initialize_with_gaussian_blur (scr, 135, cmatrix, &hblur[0][0], channel); break;
       case 137: initialize_with_gaussian_blur (scr, 137, cmatrix, &hblur[0][0], channel); break;
       case 139: initialize_with_gaussian_blur (scr, 139, cmatrix, &hblur[0][0], channel); break;
       case 141: initialize_with_gaussian_blur (scr, 141, cmatrix, &hblur[0][0], channel); break;
       case 143: initialize_with_gaussian_blur (scr, 143, cmatrix, &hblur[0][0], channel); break;
       case 145: initialize_with_gaussian_blur (scr, 145, cmatrix, &hblur[0][0], channel); break;
       case 147: initialize_with_gaussian_blur (scr, 147, cmatrix, &hblur[0][0], channel); break;
       case 149: initialize_with_gaussian_blur (scr, 149, cmatrix, &hblur[0][0], channel); break;
       case 151: initialize_with_gaussian_blur (scr, 151, cmatrix, &hblur[0][0], channel); break;
       case 153: initialize_with_gaussian_blur (scr, 153, cmatrix, &hblur[0][0], channel); break;
       case 155: initialize_with_gaussian_blur (scr, 155, cmatrix, &hblur[0][0], channel); break;
       case 157: initialize_with_gaussian_blur (scr, 157, cmatrix, &hblur[0][0], channel); break;
       case 159: initialize_with_gaussian_blur (scr, 159, cmatrix, &hblur[0][0], channel); break;
       case 161: initialize_with_gaussian_blur (scr, 161, cmatrix, &hblur[0][0], channel); break;
       case 163: initialize_with_gaussian_blur (scr, 163, cmatrix, &hblur[0][0], channel); break;
       case 165: initialize_with_gaussian_blur (scr, 165, cmatrix, &hblur[0][0], channel); break;
       case 167: initialize_with_gaussian_blur (scr, 167, cmatrix, &hblur[0][0], channel); break;
       case 169: initialize_with_gaussian_blur (scr, 169, cmatrix, &hblur[0][0], channel); break;
       case 171: initialize_with_gaussian_blur (scr, 171, cmatrix, &hblur[0][0], channel); break;
       case 173: initialize_with_gaussian_blur (scr, 173, cmatrix, &hblur[0][0], channel); break;
       case 175: initialize_with_gaussian_blur (scr, 175, cmatrix, &hblur[0][0], channel); break;
       case 177: initialize_with_gaussian_blur (scr, 177, cmatrix, &hblur[0][0], channel); break;
       case 179: initialize_with_gaussian_blur (scr, 179, cmatrix, &hblur[0][0], channel); break;
       case 181: initialize_with_gaussian_blur (scr, 181, cmatrix, &hblur[0][0], channel); break;
       case 183: initialize_with_gaussian_blur (scr, 183, cmatrix, &hblur[0][0], channel); break;
       case 185: initialize_with_gaussian_blur (scr, 185, cmatrix, &hblur[0][0], channel); break;
       case 187: initialize_with_gaussian_blur (scr, 187, cmatrix, &hblur[0][0], channel); break;
       case 189: initialize_with_gaussian_blur (scr, 189, cmatrix, &hblur[0][0], channel); break;
       case 191: initialize_with_gaussian_blur (scr, 191, cmatrix, &hblur[0][0], channel); break;
       case 193: initialize_with_gaussian_blur (scr, 193, cmatrix, &hblur[0][0], channel); break;
       case 195: initialize_with_gaussian_blur (scr, 195, cmatrix, &hblur[0][0], channel); break;
       case 197: initialize_with_gaussian_blur (scr, 197, cmatrix, &hblur[0][0], channel); break;
       case 199: initialize_with_gaussian_blur (scr, 199, cmatrix, &hblur[0][0], channel); break;

       case 201: initialize_with_gaussian_blur (scr, 201, cmatrix, &hblur[0][0], channel); break;
       case 203: initialize_with_gaussian_blur (scr, 203, cmatrix, &hblur[0][0], channel); break;
       case 205: initialize_with_gaussian_blur (scr, 205, cmatrix, &hblur[0][0], channel); break;
       case 207: initialize_with_gaussian_blur (scr, 207, cmatrix, &hblur[0][0], channel); break;
       case 209: initialize_with_gaussian_blur (scr, 209, cmatrix, &hblur[0][0], channel); break;
       case 211: initialize_with_gaussian_blur (scr, 211, cmatrix, &hblur[0][0], channel); break;
       case 213: initialize_with_gaussian_blur (scr, 213, cmatrix, &hblur[0][0], channel); break;
       case 215: initialize_with_gaussian_blur (scr, 215, cmatrix, &hblur[0][0], channel); break;
       case 217: initialize_with_gaussian_blur (scr, 217, cmatrix, &hblur[0][0], channel); break;
       case 219: initialize_with_gaussian_blur (scr, 219, cmatrix, &hblur[0][0], channel); break;
       case 221: initialize_with_gaussian_blur (scr, 221, cmatrix, &hblur[0][0], channel); break;
       case 223: initialize_with_gaussian_blur (scr, 223, cmatrix, &hblur[0][0], channel); break;
       case 225: initialize_with_gaussian_blur (scr, 225, cmatrix, &hblur[0][0], channel); break;
       case 227: initialize_with_gaussian_blur (scr, 227, cmatrix, &hblur[0][0], channel); break;
       case 229: initialize_with_gaussian_blur (scr, 229, cmatrix, &hblur[0][0], channel); break;
       case 231: initialize_with_gaussian_blur (scr, 231, cmatrix, &hblur[0][0], channel); break;
       case 233: initialize_with_gaussian_blur (scr, 233, cmatrix, &hblur[0][0], channel); break;
       case 235: initialize_with_gaussian_blur (scr, 235, cmatrix, &hblur[0][0], channel); break;
       case 237: initialize_with_gaussian_blur (scr, 237, cmatrix, &hblur[0][0], channel); break;
       case 239: initialize_with_gaussian_blur (scr, 239, cmatrix, &hblur[0][0], channel); break;
       case 241: initialize_with_gaussian_blur (scr, 241, cmatrix, &hblur[0][0], channel); break;
       case 243: initialize_with_gaussian_blur (scr, 243, cmatrix, &hblur[0][0], channel); break;
       case 245: initialize_with_gaussian_blur (scr, 245, cmatrix, &hblur[0][0], channel); break;
       case 247: initialize_with_gaussian_blur (scr, 247, cmatrix, &hblur[0][0], channel); break;
       case 249: initialize_with_gaussian_blur (scr, 249, cmatrix, &hblur[0][0], channel); break;
#endif
       default:
	  printf ("unspecialized clen %i %f %i\n", clen, blur_radius, channel);
	  initialize_with_gaussian_blur (scr, clen, cmatrix, &hblur[0][0], channel);
      }

  memcpy (add, scr.add, sizeof (add));

  //free (hblur);
  free (cmatrix);
}

static int
gaussian_blur_mtf (coord_t blur_radius, luminosity_t out [screen::size])
{
  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (blur_radius, &cmatrix);
  int half_clen = clen / 2;
  luminosity_t nrm = std::sqrt(screen::size);
  dj::fft_arg_fix<double, screen::size> in;
  for (int i = 0; i < screen::size; i++)
    in[i] = 0;
  for (int i = 0; i < clen; i++)
    {
      int idx = (i - half_clen /*+ screen::size / 4*/) & (screen::size - 1);
      in[idx] += cmatrix[i] * nrm;
    }
  free (cmatrix);
  dj::fft_arg_fix<double, screen::size> outf = dj::fft1d_fix<double,screen::size>(in, dj::fft_dir::DIR_FWD);
  for (int i = 0; i < screen::size; i++)
    {
      out[i] = outf[i].real ();
      //printf ("%i: %f+%fi %f %f\n",i, outf[i].real(), outf[i].imag(), in[i].real (), out[i]);
    }
  return clen;
}

/* Result of real fft is symmetric.  We need only N /2 + 1 complex values.  */
static constexpr const int fft_size = screen::size / 2 + 1;
typedef fftw_complex fft_1d[fft_size];
typedef fftw_complex fft_2d[screen::size * fft_size];

static int
gaussian_blur_mtf_fast (coord_t blur_radius, fft_1d out)
{
  fftw_plan plan_1d;
  luminosity_t *cmatrix;
  //blur_radius = 0;
  int clen = fir_blur::gen_convolve_matrix (blur_radius, &cmatrix);
  int half_clen = clen / 2;
  //luminosity_t nrm = std::sqrt(screen::size);
  double in[screen::size];
  for (int i = 0; i < screen::size; i++)
    in[i] = 0;
  for (int i = 0; i < clen; i++)
    {
      int idx = (i - half_clen /*+ screen::size / 4*/) & (screen::size - 1);
      in[idx] += cmatrix[i] /** nrm*/;
    }
  plan_1d = fftw_plan_dft_r2c_1d (screen::size, in, out, FFTW_ESTIMATE);
  fftw_execute (plan_1d);
  fftw_destroy_plan (plan_1d);
  //for (int i = 0; i < fft_size; i++)
    //printf ("%i: %f %f\n", i, out[i][0], out[i][1]);
  free (cmatrix);
  return clen;
}

void
screen::initialize_with_1D_fft(screen &scr, luminosity_t weights[size], int cmin, int cmax)
{
  dj::fft_arg_fix<double, size * size> imgData;
  for (int c = cmin; c <= cmax; c++)
    {
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
	  imgData[y * size + x] = scr.mult[y][x][c];
      dj::fft_arg_fix<double, size * size> imgDataFFT = dj::fft2d_fix<double,size>(imgData, dj::fft_dir::DIR_FWD);
      for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++)
	  imgDataFFT [y * size + x] *= weights[x] * weights[y];
      dj::fft_arg_fix<double, size * size> imgDataInvFFT = dj::fft2d_fix<double,size>(imgDataFFT, dj::fft_dir::DIR_BWD);
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
	  {
	    mult[y][x][c] = imgDataInvFFT[y * size + x].real ();
	    if (mult[y][x][c] < 0)
	      mult[y][x][c] = 0;
	  }
    }
}

static void
initialize_with_1D_fft_fast (screen &out_scr, screen &scr, fft_1d weights, int cmin, int cmax)
{
  fftw_plan plan_2d_inv, plan_2d;
  fft_2d in;
  double out[screen::size * screen::size];

  plan_2d_inv = fftw_plan_dft_c2r_2d (screen::size, screen::size, in, out, FFTW_ESTIMATE);
  plan_2d = fftw_plan_dft_r2c_2d (screen::size, screen::size, out, in, FFTW_ESTIMATE);
  for (int c = cmin; c <= cmax; c++)
    {
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
	  out[y * screen::size + x] = scr.mult[y][x][c];
      fftw_execute (plan_2d);
#if 0
      for (int y = 0; y < fft_size; y++)
	{
	  for (int x = 0; x < fft_size; x++)
	    {
	      std::complex w1(weights[x][0], weights[x][1]);
	      std::complex w2(weights[y][0], weights[y][1]);
	      std::complex v(in [y * screen::size + x][0], in [y * screen::size + x][1]);
	      in [y * screen::size + x][0] = real (v * w1 * w2);
	      in [y * screen::size + x][1] = imag (v * w1 * w2);
	    }
	  for (int x = 1; x < fft_size; x++)
	    {
	      std::complex w1(weights[x][0], -weights[x][1]);
	      std::complex w2(weights[y][0], weights[y][1]);
	      std::complex v(in [y * screen::size + screen::size - x][0], in [y * screen::size + screen::size - x][1]);
	      in [y * screen::size + screen::size - x][0] = real (v * w1 * w2);
	      in [y * screen::size + screen::size - x][1] = imag (v * w1 * w2);
	    }
	}
#else
      for (int y = 0; y < fft_size; y++)
	{
          std::complex w2(weights[y][0], weights[y][1]);
	  for (int x = 0; x < fft_size; x++)
	    {
	      std::complex w1(weights[x][0], weights[x][1]);
	      std::complex v(in [y * fft_size + x][0], in [y * fft_size + x][1]);
	      in [y * fft_size + x][0] = real (v * w1 * w2);
	      in [y * fft_size + x][1] = imag (v * w1 * w2);
	    }
	}
      for (int y = 1; y < fft_size; y++)
	{
          std::complex w2(weights[y][0], -weights[y][1]);
	  for (int x = 0; x < fft_size; x++)
	    {
	      std::complex w1(weights[x][0], weights[x][1]);
	      std::complex v(in [(screen::size - y) * fft_size + x][0], in [(screen::size - y) * fft_size + x][1]);
	      in [(screen::size - y) * fft_size + x][0] = real (v * w1 * w2);
	      in [(screen::size - y) * fft_size + x][1] = imag (v * w1 * w2);
	    }
	}
#endif
      fftw_execute (plan_2d_inv);
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
	  {
	    out_scr.mult[y][x][c] = out[y * screen::size + x] * ((luminosity_t)1 / (screen::size * screen::size));
	    //printf ("%f\n", out[y * screen::size + x]);
	    if (out_scr.mult[y][x][c] < 0)
	      out_scr.mult[y][x][c] = 0;
	    if (out_scr.mult[y][x][c] > 1)
	      out_scr.mult[y][x][c] = 1;
	  }
    }
  fftw_destroy_plan (plan_2d);
  fftw_destroy_plan (plan_2d_inv);
}

/* Compute average error between the two implementations.  */
bool
screen::almost_eq (const screen &scr, luminosity_t *delta_ret, luminosity_t maxdelta) const
{
  luminosity_t delta = 0;
  for (int y = 0; y < size; y++)
   for (int x = 0; x < size; x++)
     for (int c = 0; c < 3; c++)
       delta += fabs (scr.mult[y][x][c] - mult[y][x][c]);
  delta /= size * size * 3;
  if (delta_ret)
    *delta_ret = delta;
  return delta < maxdelta;
}

void
screen::initialize_with_gaussian_blur (screen &scr, rgbdata blur_radius, blur_alg alg)
{
  //bool fft_used = false;
  memcpy (add, scr.add, sizeof (add));
  if (blur_radius.red <= 0 && blur_radius.green <= 0 && blur_radius.blue <= 0)
    {
      memcpy (mult, scr.mult, sizeof (mult));
      return;
    }
  //const bool verify = false;
#if 0
  if (blur_radius.red >= max_blur_radius)
    blur_radius.red = max_blur_radius;
  if (blur_radius.green >= max_blur_radius)
    blur_radius.green = max_blur_radius;
  if (blur_radius.blue >= max_blur_radius)
    blur_radius.blue = max_blur_radius;
#endif
  bool all = (blur_radius.red == blur_radius.green) && (blur_radius.red == blur_radius.blue);
  //bool maxradius = false;
  int clen = 0;
  for (int c = 0; c < 3; c++)
    {
      clen = fir_blur::convolve_matrix_length (blur_radius[c] * screen::size);
      if (clen <= 1 || !(blur_radius[c] > 0))
	{
	  if (!all)
	    for (int y = 0; y < size; y++)
	     for (int x = 0; x < size; x++)
	       mult[y][x][c] = scr.mult[y][x][c];
	  else
	    {
	      memcpy (mult, scr.mult, sizeof (mult));
	      break;
	    }
	  continue;
	}
      //if (blur_radius[c] > max_blur_radius)
	//maxradius = true;
      bool do_fft = false;
      if (alg == blur_fft)
	do_fft = true;
      else if (alg == blur_auto)
	do_fft = (blur_radius[c] >= max_blur_radius) || clen > 15;
      if (!do_fft)
        initialize_with_gaussian_blur (scr, blur_radius[c], c, all ? 2 : c);
#if 0
      else
        {
	  luminosity_t weights[size];
	  gaussian_blur_mtf (blur_radius[c] * screen::size, weights);
	  screen::initialize_with_1D_fft (scr, weights, c, all ? 2 : c);
	  fft_used = true;
        }
#else
      else
        {
	  fft_1d weights;
	  gaussian_blur_mtf_fast (blur_radius[c] * screen::size, weights);
	  initialize_with_1D_fft_fast (*this, scr, weights, c, all ? 2 : c);
        }
#endif
      if (all)
	break;
    }
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

void
screen::initialize_dot ()
{
  for (int y = 0; y < size; y++)
   for (int x = 0; x < size; x++)
     for (int c = 0; c < 3; c++)
       {
	 mult[y][x][c] = 0;
	 add[y][x][c] = 0;
       }
  mult[size/2][size/2][0] = mult[size/2][size/2][1] = mult[size/2][size/2][2] = 1;
}

bool
screen::save_tiff (const char *filename, bool normalize, int tiles)
{
  tiff_writer_params p;
  rgbdata max = {0,0,0};
  void *buffer;
  size_t len = create_linear_srgb_profile (&buffer);
  if (!normalize)
    max.red = max.green = max.blue = 1;
  else
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
	{
	  max.red = std::max (mult[y][x][0], max.red);
	  max.green = std::max (mult[y][x][0], max.green);
	  max.blue = std::max (mult[y][x][0], max.blue);
	}
  p.filename = filename;
  p.width = size * tiles;
  p.height = size * tiles;
  p.icc_profile = buffer;
  p.icc_profile_len = len;
  p.depth = 16;
  const char *error;
  tiff_writer out (p, &error);
  free (buffer);
  if (error)
    return false;
  for (int y = 0; y < size * tiles; y++)
    {
      for (int x = 0; x < size * tiles; x++)
	{
	  int r = mult[y % size][x % size][0] / max.red * 65536;
	  if (r < 0)
	    r = 0;
	  if (r > 65535)
	    r = 65535;
	  int g = mult[y % size][x % size][1] / max.green * 65536;
	  if (g < 0)
	    g = 0;
	  if (g > 65535)
	    g = 65535;
	  int b = mult[y % size][x % size][2] / max.blue * 65536;
	  if (b < 0)
	    b = 0;
	  if (b > 65535)
	    b = 65535;
	  out.put_pixel (x, r, g, b );
	}
      if (!out.write_row ())
	return false;
    }
  return true;
}

/* Specify mtf75, mtf50, mtf25 and mtf8.  */
static precomputed_function<luminosity_t> *
mtf_by_4_vals (luminosity_t mtf[4])
{
  luminosity_t y[] = {1, 1, 1, 0.75, 0.5, 0.25, 0, 0, 0};
  luminosity_t x[] = {-0.02, -0.01, 0, mtf[0], mtf[1], mtf[2], mtf[3], mtf[3] + 0.01, mtf[3] + 0.02};
  //spline<luminosity_t> p(x, y, 9);
  //return p.precompute (0, mtf[3]+1, 1024);
  return new precomputed_function<luminosity_t> (0, mtf[3]+1, 1024, x, y, 9);
}
/* Specify mtf75, mtf50, mtf25 and mtf8.  */
static precomputed_function<luminosity_t> *
point_spread_by_4_vals (luminosity_t mtf[4])
{
  luminosity_t y[] = {0, 0, 0, 0.25, 0.5, 0.75, 1, 0.75, 0.5, 0.25, 0, 0, 0};
  luminosity_t x[] = {-(mtf[3] + 0.02), -(mtf[3] + 0.01), -mtf[3], -mtf[2], -mtf[1], -mtf[0], 0, mtf[0], mtf[1], mtf[2], mtf[3], mtf[3] + 0.01, mtf[3] + 0.02};
  spline<luminosity_t> p(x, y, 13);
  return p.precompute (0, mtf[3]+1, 1024);
}

/* Sily way of computing fourier integral.  */
static precomputed_function<luminosity_t> *
mtf_to_point_spread (precomputed_function<luminosity_t> *mtf)
{
  /* we only need sqrt(2)/2 = 1.41/2 = 0.71 */
  std::complex<double> vals[screen::size];
  double v = mtf->apply (0);
  for (int i = 0; i< screen::size; i++)
    vals[i] = v;
  for (double a = 0.1; a < screen::size; a += 0.1)
    {
      double v = mtf->apply (a);
      if (v)
        for (int i = 0; i< screen::size; i++)
	  vals[i] += std::polar ((double)1, (double)(a * i)) * v;
    }
  luminosity_t vv[screen::size];
  for (int i = 0; screen::size; i++)
    vv[i] = vals[i].real ();
  return new precomputed_function<luminosity_t> (0, screen::size, vv, screen::size);
}

static const int tiles = 1;

void
screen::print_mtf (FILE *f, luminosity_t mtf[4], coord_t pixel_size)
{
  std::unique_ptr <precomputed_function<luminosity_t>> mtfc(mtf_by_4_vals (mtf));
  luminosity_t step = 1.0 / tiles;
  coord_t dpi = 4500;
  printf ("mtf75:%f (screen reciprocal pixels) %f (scan reciprocal pixels) %f lp/mm at %f DPI\n", mtf[0], mtf[0] * pixel_size, mtf[0] * pixel_size * dpi / 25.4, dpi);
  printf ("mtf50:%f (screen reciprocal pixels) %f (scan reciprocal pixels) %f lp/mm at %f DPI\n", mtf[1], mtf[1] * pixel_size, mtf[1] * pixel_size * dpi / 25.4, dpi);
  printf ("mtf25:%f (screen reciprocal pixels) %f (scan reciprocal pixels) %f lp/mm at %f DPI\n", mtf[2], mtf[2] * pixel_size, mtf[2] * pixel_size * dpi / 25.4, dpi);
  printf ("mtf0: %f (screen reciprocal pixels) %f (scan reciprocal pixels) %f lp/mm at %f DPI\n", mtf[3], mtf[3] * pixel_size, mtf[3] * pixel_size * dpi / 25.4, dpi);
  for (int x = 0; x <= (size * tiles) / 2; x++)
    {
      luminosity_t w = mtfc->apply (x * step);
      if (w > 0)
	{
	  printf ("%4.2f %6.3f:",x * step, x * step * pixel_size);
	  for (int i = 0; i < 80 * w; i++)
	    printf (" ");
	  printf ("* %2.2f\n", w * 100);
	}
    }
}

void
screen::initialize_with_2D_fft(screen &scr, precomputed_function<luminosity_t> *mtf[3], rgbdata scale)
{
  dj::fft_arg_fix<double, size * size * tiles * tiles> imgData;
  for (int c = 0; c < 3; c++)
    {
      for (int yy = 0; yy < tiles; yy++)
        for (int y = 0; y < size; y++)
	  for (int xx = 0; xx < tiles; xx++)
            for (int x = 0; x < size; x++)
	      imgData[(y + yy * size) * size * tiles + x + xx * size] = scr.mult[y][x][c];

      dj::fft_arg_fix<double, size * size * tiles * tiles> imgDataFFT = dj::fft2d_fix<double,size * tiles>(imgData, dj::fft_dir::DIR_FWD);
      luminosity_t step = scale[c] / tiles;


      for (int y = 0; y <= size * tiles / 2; y++)
	for (int x = 0; x <= size * tiles / 2; x++)
	  if (!x && !y)
	    ;
	  else
	    {
	      luminosity_t w = mtf[c]->apply (sqrt (x * x + y * y) * step);
	      if (w < 0)
		w = 0;
	      if (w > 1)
		w = 1;
	      imgDataFFT [y * size * tiles + x] *= w;
	      if (x && (x != size * tiles / 2))
		imgDataFFT [y * size * tiles + (size * tiles - x)] *= w;
	      if (y && (y != size * tiles / 2))
		{
		  imgDataFFT [(size * tiles - y) * size * tiles + x] *= w;
		  if (x && (x != size * tiles / 2))
		    imgDataFFT [(size * tiles - y) * size * tiles + (size * tiles - x)] *= w;
		}
	    }
      dj::fft_arg_fix<double, size * size * tiles * tiles> imgDataInvFFT = dj::fft2d_fix<double,size * tiles>(imgDataFFT, dj::fft_dir::DIR_BWD);
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
	  {
	    mult[y][x][c] = imgDataInvFFT[y * size * tiles + x].real ();
	    if (mult[y][x][c] < 0)
	      mult[y][x][c] = 0;
	  }
    }
}

dj::fft_arg_fix<double,screen::size * screen::size>
point_spread_fft (precomputed_function<luminosity_t> &point_spread, luminosity_t scale)
{
  dj::fft_arg_fix<double,screen::size * screen::size> in;
  double sum = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
	int x2 = screen::size - x;
	int y2 = screen::size - y;
	int dist = std::min ((x * x + y * y), std::min ((x2 * x2 + y * y), std::min ((x * x + y2 * y2), (x2 * x2 + y2 * y2))));
	luminosity_t w = point_spread.apply (my_sqrt ((luminosity_t)dist) * (scale* screen::size));
	if (w < 0)
	  w = 0;
	in [y * screen::size + x] = w;
	sum += w;
      }
  luminosity_t nrm = std::sqrt(screen::size * screen::size);
  for (int x = 0; x < screen::size * screen::size; x++)
      in [x] *= (nrm / sum);
  return dj::fft2d_fix<double,screen::size>(in, dj::fft_dir::DIR_FWD);
}

void
screen::initialize_with_point_spread(screen &scr, precomputed_function<luminosity_t> *point_spread[3], rgbdata scale)
{
  std::unique_ptr <dj::fft_arg_fix<double, size * size>> scaleFFT (new (dj::fft_arg_fix<double, size * size>));
  std::unique_ptr <dj::fft_arg_fix<double, size * size>> imgData (new (dj::fft_arg_fix<double, size * size>));
  std::unique_ptr <dj::fft_arg_fix<double, size * size>> imgDataFFT (new (dj::fft_arg_fix<double, size * size>));
  std::unique_ptr <dj::fft_arg_fix<double, size * size>> imgDataInvFFT (new (dj::fft_arg_fix<double, size * size>));
  for (int c = 0; c < 3; c++)
    {
      for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++)
	  (*imgData)[y * size + x] = scr.mult[y][x][c];

      *imgDataFFT = dj::fft2d_fix<double,size>(*imgData, dj::fft_dir::DIR_FWD);

      *scaleFFT = point_spread_fft (*point_spread[c], scale[c] / size);
      for (int x = 0; x < size * size; x++)
	(*imgDataFFT)[x] *= (*scaleFFT)[x];

      *imgDataInvFFT = dj::fft2d_fix<double,size>(*imgDataFFT, dj::fft_dir::DIR_BWD);
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
	  {
	    mult[y][x][c] = (*imgDataInvFFT)[y * size + x].real ();
	    if (mult[y][x][c] < 0)
	      mult[y][x][c] = 0;
	  }
    }
}

void
screen::initialize_with_fft_blur(screen &scr, rgbdata blur_radius)
{
  /* This is sample MTF curve of a camera taken from IMOD's mtffliter.
     first column are spartial frequencies in reciprocal pixels and second column
     is a contrast loss.  */
  const static luminosity_t data[][2] = {
      { 0.0085, 0.98585},
      { 0.0221, 0.94238}, 
      { 0.0357, 0.89398}, 
      { 0.0493, 0.83569}, 
      { 0.0629, 0.76320},
      { 0.0765, 0.69735},
      { 0.0901, 0.63647},
      { 0.1037, 0.56575},
      { 0.1173, 0.49876},
      { 0.1310, 0.43843},
      { 0.1446, 0.38424},
      { 0.1582, 0.34210},
      { 0.1718, 0.30289},
      { 0.1854, 0.26933},
      { 0.1990, 0.23836},
      { 0.2126, 0.21318},
      { 0.2262, 0.18644},
      { 0.2398, 0.15756},
      { 0.2534, 0.14863},
      { 0.2670, 0.12485},
      { 0.2806, 0.11436},
      { 0.2942, 0.09183},
      { 0.3078, 0.08277},
      { 0.3214, 0.07021},
      { 0.3350, 0.05714},
      { 0.3486, 0.04388},
      { 0.3622, 0.03955},
      { 0.3759, 0.03367},
      { 0.3895, 0.02844},
      { 0.4031, 0.02107},
      { 0.4167, 0.02031},
      { 0.4303, 0.01796},
      { 0.4439, 0.00999},
      { 0.4575, 0.01103},
      { 0.4711, 0.00910},
      { 0.4898, 0.00741}
  };
  int data_size = sizeof (data) / sizeof (luminosity_t) / 2 - 1;
  bool use_sqrt = true;
  memcpy (add, scr.add, sizeof (add));

  if (!use_sqrt)
    {
      bool all = (blur_radius.red == blur_radius.green) && (blur_radius.red == blur_radius.blue);
      for (int c = 0; c < 3; c++)
        {
          luminosity_t weights[size];
	  /* blur_radius is blur in the screen dimensions.
	     step should be 1 / size, but blur_radius of 1 corresponds to size so this evens out.
	     Compensate so the blur is approximately same as gaussian blur.	 */
	  luminosity_t step = blur_radius[c] * 0.5 * (0.75 / 0.61);
	  luminosity_t f = step;
	  weights[0] = 1;
	  for (int x = 1, p = 0; x <= size / 2; x++, f+= step)
	    {
	      while (p < data_size - 1 && data[p + 1][0] < f)
		p++;
	      luminosity_t w = data[p][1] + (data[p + 1][1] - data[p][1]) * (f - data[p][0]) / (data[p + 1][0] - data[p][0]);
	      //printf ("%f %i %f d1 %f %f d2 %f %f\n",f,p,w,data[p][0],data[p][1],data[p+1][0],data[p+1][1]);
	      if (w < 0)
		w = 0;
	      if (w > 1)
		w = 1;
	      if (x == size / 2)
		weights[x] = w;
	      else
		weights[x] = weights[size - x] = w;
	    }
          initialize_with_1D_fft (scr, weights, c, all ? 2 : c);
	  if (all)
	    break;
        }
    }
  else
    {
      static precomputed_function<luminosity_t> v (0, 0.5, size, data, data_size);
      precomputed_function<luminosity_t> *vv[3] = {&v,&v,&v};
      initialize_with_2D_fft (scr, vv, blur_radius * 0.5 * (0.75 / 0.61));
    }
}

void
screen::initialize_with_blur (screen &scr, coord_t blur_radius, enum blur_type type, enum blur_alg alg)
{
  initialize_with_blur (scr, {blur_radius, blur_radius, blur_radius}, type, alg);
}
void
screen::initialize_with_blur (screen &scr, rgbdata blur_radius, enum blur_type type, enum blur_alg alg)
{
  if (type == blur_gaussian)
    initialize_with_gaussian_blur (scr, blur_radius, alg);
  else
    initialize_with_fft_blur (scr, blur_radius);
}
void
screen::initialize_with_blur (screen &scr, luminosity_t mtf[4], enum blur_alg alg)
{
#if 0
  /* An attempt to get more data points form MTF. Way too slow.  */
  std::unique_ptr <precomputed_function<luminosity_t>> mtfc(point_spread_by_4_vals (mtf));
  std::unique_ptr <precomputed_function<luminosity_t>> ps(mtf_to_point_spread (mtfc.get ()));
  precomputed_function<luminosity_t> *vv[3] = {ps.get (), ps.get (), ps.get ()};
  screen::initialize_with_point_spread (scr, vv, {1.0, 1.0, 1.0});
#elif 1
  std::unique_ptr <precomputed_function<luminosity_t>> mtfc(point_spread_by_4_vals (mtf));
  precomputed_function<luminosity_t> *vv[3] = {mtfc.get (), mtfc.get (), mtfc.get ()};
  initialize_with_2D_fft (scr, vv, {1.0, 1.0, 1.0});
#else
  std::unique_ptr <precomputed_function<luminosity_t>> point_spreadc(ps_by_4_vals (mtf));
  precomputed_function<luminosity_t> *vv[3] = {ps.get (), ps.get (), ps.get ()};
  screen::initialize_with_point_spread (scr, vv, {1.0, 1.0, 1.0});
#endif
}
}
