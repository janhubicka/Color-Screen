#include "fft.h"
#include "fft.h"
#include "deconvolve.h"
#include "gaussian-blur.h"
#include "icc.h"
#include "include/dufaycolor.h"
#include "include/tiff-writer.h"
#include "screen.h"
#include "spline.h"
#include "include/render-parameters.h"
#include <array>
#include <complex>
#include <math.h>
#include <memory>
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
#define D (68 * size) / 256
#define DG (68 * size) / 256

void
screen::thames ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
#define dist(x, y)                                                            \
  (xx - (x) * size) * (xx - (x) * size) + (yy - (y) * size) * (yy - (y) * size)
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
        if (d1 < ((size / 2) - DG))
          {
            /* Green.  */
            mult[yy][xx][0] = 0;
            mult[yy][xx][1] = 1;
            mult[yy][xx][2] = 0;
            continue;
          }
        else if (d3 < ((size / 2) - D))
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

/* X,y are coordinates of entry in screen array of dimensions
   SIZE * SIZE.

   CX, CY are coordinates of another point (in screen coordiantes, i.e. range
   0..1).  Compute sum distance between them considering the periodicity.  */

static inline int
dist (int x, int y, coord_t cx, coord_t cy, int size)
{
  /* Work in 2 * size so we can represent x + 0.5 and y + 0.5 as integer.  */
  unsigned dx
      = ((unsigned)(cx * 2 * size) - (unsigned)(x * 2 + 1)) & (2 * size - 1);
  unsigned dy
      = ((unsigned)(cy * 2 * size) - (unsigned)(y * 2 + 1)) & (2 * size - 1);
  if (dx > (unsigned)size)
    dx = 2 * size - dx;
  if (dy > (unsigned)size)
    dy = 2 * size - dy;
  return (dx + dy) / 2;
}

/* We render the following pattern (empty spaces are blue):

     GGGGRRRRRRRRGGGG
     GGG  RRRRRR  GGG
     GG    RRRR    GG
     G      RR      G
     R      GG      R
     RR    GGGG    RR
     RRR  GGGGGG  RRR
     RRRRGGGGGGGGRRRR
     RRR  GGGGGG  RRR
     RR    GGGG    RR
     R      GG      R
     G      RR      G
     GG    RRRR    GG
     GGG  RRRRRR  GGG
     GGGGRRRRRRRRGGGG

  So here are 2 green dots with centers (0,0) and (0.5, 0.5)
  and 2 red dots with centers (0,0.5) and (0.5,0)
*/

void
screen::paget_finlay ()
{
  int xx, yy;

  /* Constant bellow specifies ratio of blue square diagonal to diagonal of red
     and green squares.

     Original paget announcement claims that blue squares are smaller
     so each of primaries occupies the same space.  Since in one period we
     have twice as many blue squares, to have all three colors of equal size,
     the 4 blue squares must occupy 1/3 of the space. Rest is occupied
     by red and green squares which have cut corners.  For this reason
     we compute ratio of blue square diagonal to read or green square
     diagonal and then render bigger ones.

     If all squares were of same size, with 45 degree rotation we get

     RRRbbb
     RRRbbb
     RRRbbb
     bbbGGG
     bbbGGG
     bbbGGG

     Here all edges are 0.5.  We need to adjust blue square edge:

     2*(x^2)=1/3 or x=1/sqrt(6) or x=0.40824829046 instead of 0.5.

     To achieve this we use

     const coord_t red_green_diagonal = (1-0.40824829046) * size;
   */

  /* Probably more realistic option based on Wall's History of color
     photography:

     The Paget Plate - This is believed was invention of C. Finlay
     and consits of square elements, the blue being 0.063mm. with
     0.085mm. for the red and green. It is stated by J. H. Pledge^20
     to be roughly blue 8, red 7, green 7 there being two blue
     elements in the unit.

     20: Brit. J. Phot. 1913 60 Col. Phot. Supp 7, 31.*/
  const coord_t red_green_diagonal = (0.085 / (0.063 + 0.085)) * size;
  /* This is 0.57422*size

     To achive the ratio 8:7:7 mentioned above one wants to set
     red_green_diagonal to be 1-sqrt(2/11) which is approximately

     0.57361*size

     so at least the numbers seems to match.  */
  // const coord_t red_green_diagonal = 0.57361*size;

  int r = 0, g = 0, b = 0;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {

        /* Distance from center of the nearest green dot.
           Centers of squares (modulo period) are (0,0)  and (0.5,0.5) for
           green. */
        int d1 = std::min (dist (xx, yy, 0, 0, size),
                           dist (xx, yy, 0.5, 0.5, size));

        /* Distance from center of the nearest red dot.
           Centers of squares (modulo period) are (0,0.5)  and (0.5,0) for
           green. */
        int d2 = std::min (dist (xx, yy, 0, 0.5, size),
                           dist (xx, yy, 0.5, 0, size));

        if (d1 < red_green_diagonal / 2 && d1 < d2)
          {
            /* Green.  */
            mult[yy][xx][0] = 0;
            mult[yy][xx][1] = 1;
            mult[yy][xx][2] = 0;
            g++;
          }
        else if (d2 < red_green_diagonal / 2)
          {
            /* Red.  */
            mult[yy][xx][0] = 1;
            mult[yy][xx][1] = 0;
            mult[yy][xx][2] = 0;
            r++;
          }
        else
          {
            /* Blue.  */
            mult[yy][xx][0] = 0;
            mult[yy][xx][1] = 0;
            mult[yy][xx][2] = 1;
            b++;
          }

        add[yy][xx][0] = 0;
        add[yy][xx][1] = 0;
        add[yy][xx][2] = 0;
      }
  if (0)
    printf ("screen ratios: %f %f %f\n", r / (double)(size * size),
            g / (double)(size * size), b / (double)(size * size));
}

void
screen::dufay (coord_t red_strip_width, coord_t green_strip_width)
{
  if (!red_strip_width)
    red_strip_width = dufaycolor::red_strip_width;
  if (!green_strip_width)
    green_strip_width = dufaycolor::green_strip_width;

  coord_t strip_width = size / 2 * (1 - red_strip_width);
  coord_t strip_height = size / 2 * green_strip_width;

  luminosity_t red[size];
  luminosity_t sum = 0;
  for (int yy = 0; yy < size; yy++)
    {
      if (yy >= ceil (strip_width) && yy + 1 <= floor (size - strip_width))
        red[yy] = 1;
      else if (yy + 1 <= floor (strip_width)
               || yy >= ceil (size - strip_width))
        red[yy] = 0;
      else if (yy == (int)strip_width)
        red[yy] = 1 - (strip_width - yy);
      else if (yy == (int)(size - strip_width))
        red[yy] = size - strip_width - yy;
      else
        {
          // printf ("%i %f \n",yy,strip_width);
          abort ();
        }
      sum += red[yy];
      assert (!colorscreen_checking || (red[yy] >= 0 && red[yy] <= 1));
      assert (!colorscreen_checking || yy < size / 2
              || fabs (red[yy] - red[size - 1 - yy]) < 0.0000001);
    }
  // printf ("scr: %f %f %f\n", red_strip_width, sum / size, strip_width);
  assert (!colorscreen_checking
          || fabs (sum / size - red_strip_width) < 0.00001);
  luminosity_t green[size];
  sum = 0;
  for (int xx = 0; xx < size; xx++)
    {
      if (xx >= ceil (strip_height) && xx + 1 <= floor (size - strip_height))
        green[xx] = 0;
      else if (xx + 1 <= floor (strip_height)
               || xx >= ceil (size - strip_height))
        green[xx] = 1;
      else if (xx == (int)strip_height)
        green[xx] = (strip_height - xx);
      else if (xx == (int)(size - strip_height))
        green[xx] = 1 - (size - strip_height - xx);
      else
        {
          ////printf ("b %i %f \n",xx,strip_height);
          abort ();
        }
      sum += green[xx];
      assert (!colorscreen_checking || (green[xx] >= 0 && green[xx] <= 1));
      assert (!colorscreen_checking || xx < size / 2
              || fabs (green[xx] - green[size - 1 - xx]) < 0.0000001);
      // printf (" %f \n", green[xx]);
    }
  // printf ("%f %f %i %i %i\n",red_strip_width, green_strip_width,strip_width,
  // strip_height, size);
  assert (!colorscreen_checking
          || fabs (sum / size - green_strip_width) < 0.00001);
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
  // printf ("%f %f %f\n",rsum, rsum / (size * size), red_strip_width);
  assert (!colorscreen_checking
          || fabs (rsum / (size * size) - red_strip_width) < 0.00001);
  // printf ("%f %f %f\n",gsum, gsum / (size * size), (1-red_strip_width) *
  // green_strip_width);
  assert (!colorscreen_checking
          || fabs (gsum / (size * size)
                   - (1 - red_strip_width) * green_strip_width)
                 < 0.00001);
  assert (!colorscreen_checking
          || fabs (bsum / (size * size)
                   - (1 - red_strip_width) * (1 - green_strip_width))
                 < 0.00001);
}

/* This computes the grid displayed by UI.  */

void
screen::preview ()
{
  int xx, yy;
  for (yy = 0; yy < size; yy++)
    for (xx = 0; xx < size; xx++)
      {
#define dist(x, y)                                                            \
  (xx - (x) * size) * (xx - (x) * size) + (yy - (y) * size) * (yy - (y) * size)
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
            if (xx < 10 * size / 256 || xx > size - 10 * size / 256
                || yy < 10 * size / 256 || yy > size - 10 * size / 256)
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
        if (yy < strip_height || yy > size - strip_height)
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
            if (xx > 7 * size / 16 && xx < 9 * size / 16)
              {
                add[yy][xx][0] = 0;
                add[yy][xx][1] = 0;
                add[yy][xx][2] = 0.5;
                mult[yy][xx][0] = 0.25;
                mult[yy][xx][1] = 0.25;
                mult[yy][xx][2] = 0.5;
              }
          }
        else if (yy > 7 * size / 16 && yy < 9 * size / 16)
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

__attribute__ ((always_inline)) inline void
screen::initialize_with_1d_kernel (screen &scr, int clen,
                                   luminosity_t *cmatrix, luminosity_t *hblur,
                                   int c)
{
  // #pragma omp parallel shared(scr, clen, cmatrix, hblur,c)
  for (int y = 0; y < size; y++)
    {
      luminosity_t mmult[size + clen];
      /* Make internal loop vectorizable by copying out data in right order. */
      for (int x = 0; x < size + clen; x++)
        mmult[x] = scr.mult[y][(x - clen / 2) & (size - 1)][c];
      for (int x = 0; x < size; x++)
        {
          luminosity_t sum = 0;
          for (int d = -clen / 2; d < clen / 2; d++)
            // sum += cmatrix[d + clen / 2] * scr.mult[y][(x + d) & (size -
            // 1)][c];
            sum += cmatrix[d + clen / 2] * mmult[x + d + clen / 2];
          hblur[x + y * size] = sum;
        }
    }
  // #pragma omp parallel shared(scr, clen, cmatrix, hblur,c)
  for (int x = 0; x < size; x++)
    {
      luminosity_t mmult[size + clen];
      /* Make internal loop vectorizable by copying out data in right order. */
      for (int y = 0; y < size + clen; y++)
        mmult[y] = hblur[x + ((y - clen / 2) & (size - 1)) * size];
      for (int y = 0; y < size; y++)
        {
          luminosity_t sum = 0;
          for (int d = -clen / 2; d < clen / 2; d++)
            // sum += cmatrix[d + clen / 2] * hblur[x+ ((y + d) & (size - 1)) *
            // size];
            sum += cmatrix[d + clen / 2] * mmult[y + d + clen / 2];
          mult[y][x][c] = sum;
        }
    }
}

#if 0
__attribute__ ((always_inline)) inline void
screen::initialize_with_2d_kernel (screen &scr, int clen,
                                   luminosity_t *cmatrix2d, int c)
{
  // #pragma omp parallel shared(scr, clen, cmatrix, hblur,c)
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      {
        luminosity_t sum = 0;
        for (int yd = -clen / 2; yd < clen / 2; yd++)
          for (int xd = -clen / 2; xd < clen / 2; xd++)
            sum += cmatrix2d[(yd + clen / 2) * clen + xd + clen / 2]
                   * scr.mult[(y + yd + size) % (size - 1)]
                             [(x + xd + size) % (size - 1)][c];
        mult[y][x][c] = sum;
      }
}
#endif

void
screen::initialize_with_gaussian_blur (screen &scr, coord_t blur_radius,
                                       int cmin, int cmax)
{
  if (blur_radius >= max_blur_radius)
    blur_radius = max_blur_radius;

  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (blur_radius * size, &cmatrix);
  luminosity_t hblur[size][size]; //= (luminosity_t *)malloc (size * size *
                                  // sizeof (luminosity_t));
  /* Finetuning solver keeps recomputing screens with different blurs.
     Specialize internal loops.  */
  for (int channel = cmin; channel <= cmax; channel++)
    switch (clen)
      {
#if 1
      case 1:
        initialize_with_1d_kernel (scr, 1, cmatrix, &hblur[0][0], channel);
        break;
      case 3:
        initialize_with_1d_kernel (scr, 3, cmatrix, &hblur[0][0], channel);
        break;
      case 5:
        initialize_with_1d_kernel (scr, 5, cmatrix, &hblur[0][0], channel);
        break;
      case 7:
        initialize_with_1d_kernel (scr, 7, cmatrix, &hblur[0][0], channel);
        break;
      case 9:
        initialize_with_1d_kernel (scr, 9, cmatrix, &hblur[0][0], channel);
        break;
      case 11:
        initialize_with_1d_kernel (scr, 11, cmatrix, &hblur[0][0], channel);
        break;
      case 13:
        initialize_with_1d_kernel (scr, 13, cmatrix, &hblur[0][0], channel);
        break;
      case 15:
        initialize_with_1d_kernel (scr, 15, cmatrix, &hblur[0][0], channel);
        break;
      case 17:
        initialize_with_1d_kernel (scr, 17, cmatrix, &hblur[0][0], channel);
        break;
      case 19:
        initialize_with_1d_kernel (scr, 19, cmatrix, &hblur[0][0], channel);
        break;
      case 21:
        initialize_with_1d_kernel (scr, 21, cmatrix, &hblur[0][0], channel);
        break;
      case 23:
        initialize_with_1d_kernel (scr, 23, cmatrix, &hblur[0][0], channel);
        break;
      case 25:
        initialize_with_1d_kernel (scr, 25, cmatrix, &hblur[0][0], channel);
        break;
      case 27:
        initialize_with_1d_kernel (scr, 27, cmatrix, &hblur[0][0], channel);
        break;
      case 29:
        initialize_with_1d_kernel (scr, 29, cmatrix, &hblur[0][0], channel);
        break;
      case 31:
        initialize_with_1d_kernel (scr, 31, cmatrix, &hblur[0][0], channel);
        break;
      case 33:
        initialize_with_1d_kernel (scr, 33, cmatrix, &hblur[0][0], channel);
        break;
      case 35:
        initialize_with_1d_kernel (scr, 35, cmatrix, &hblur[0][0], channel);
        break;
      case 37:
        initialize_with_1d_kernel (scr, 37, cmatrix, &hblur[0][0], channel);
        break;
      case 39:
        initialize_with_1d_kernel (scr, 39, cmatrix, &hblur[0][0], channel);
        break;
      case 41:
        initialize_with_1d_kernel (scr, 41, cmatrix, &hblur[0][0], channel);
        break;
      case 43:
        initialize_with_1d_kernel (scr, 43, cmatrix, &hblur[0][0], channel);
        break;
      case 45:
        initialize_with_1d_kernel (scr, 45, cmatrix, &hblur[0][0], channel);
        break;
      case 47:
        initialize_with_1d_kernel (scr, 47, cmatrix, &hblur[0][0], channel);
        break;
      case 49:
        initialize_with_1d_kernel (scr, 49, cmatrix, &hblur[0][0], channel);
        break;
      case 51:
        initialize_with_1d_kernel (scr, 51, cmatrix, &hblur[0][0], channel);
        break;
      case 53:
        initialize_with_1d_kernel (scr, 53, cmatrix, &hblur[0][0], channel);
        break;
      case 55:
        initialize_with_1d_kernel (scr, 55, cmatrix, &hblur[0][0], channel);
        break;
      case 57:
        initialize_with_1d_kernel (scr, 57, cmatrix, &hblur[0][0], channel);
        break;
      case 59:
        initialize_with_1d_kernel (scr, 59, cmatrix, &hblur[0][0], channel);
        break;
      case 61:
        initialize_with_1d_kernel (scr, 61, cmatrix, &hblur[0][0], channel);
        break;
      case 63:
        initialize_with_1d_kernel (scr, 63, cmatrix, &hblur[0][0], channel);
        break;
      case 65:
        initialize_with_1d_kernel (scr, 65, cmatrix, &hblur[0][0], channel);
        break;
      case 67:
        initialize_with_1d_kernel (scr, 67, cmatrix, &hblur[0][0], channel);
        break;
      case 69:
        initialize_with_1d_kernel (scr, 69, cmatrix, &hblur[0][0], channel);
        break;
      case 71:
        initialize_with_1d_kernel (scr, 71, cmatrix, &hblur[0][0], channel);
        break;
      case 73:
        initialize_with_1d_kernel (scr, 73, cmatrix, &hblur[0][0], channel);
        break;
      case 75:
        initialize_with_1d_kernel (scr, 75, cmatrix, &hblur[0][0], channel);
        break;
      case 77:
        initialize_with_1d_kernel (scr, 77, cmatrix, &hblur[0][0], channel);
        break;
      case 79:
        initialize_with_1d_kernel (scr, 79, cmatrix, &hblur[0][0], channel);
        break;
      case 81:
        initialize_with_1d_kernel (scr, 81, cmatrix, &hblur[0][0], channel);
        break;
      case 83:
        initialize_with_1d_kernel (scr, 83, cmatrix, &hblur[0][0], channel);
        break;
      case 85:
        initialize_with_1d_kernel (scr, 85, cmatrix, &hblur[0][0], channel);
        break;
      case 87:
        initialize_with_1d_kernel (scr, 87, cmatrix, &hblur[0][0], channel);
        break;
      case 89:
        initialize_with_1d_kernel (scr, 89, cmatrix, &hblur[0][0], channel);
        break;
      case 91:
        initialize_with_1d_kernel (scr, 91, cmatrix, &hblur[0][0], channel);
        break;
      case 93:
        initialize_with_1d_kernel (scr, 93, cmatrix, &hblur[0][0], channel);
        break;
      case 95:
        initialize_with_1d_kernel (scr, 95, cmatrix, &hblur[0][0], channel);
        break;
      case 97:
        initialize_with_1d_kernel (scr, 97, cmatrix, &hblur[0][0], channel);
        break;
      case 99:
        initialize_with_1d_kernel (scr, 99, cmatrix, &hblur[0][0], channel);
        break;

      case 101:
        initialize_with_1d_kernel (scr, 101, cmatrix, &hblur[0][0], channel);
        break;
      case 103:
        initialize_with_1d_kernel (scr, 103, cmatrix, &hblur[0][0], channel);
        break;
      case 105:
        initialize_with_1d_kernel (scr, 105, cmatrix, &hblur[0][0], channel);
        break;
      case 107:
        initialize_with_1d_kernel (scr, 107, cmatrix, &hblur[0][0], channel);
        break;
      case 109:
        initialize_with_1d_kernel (scr, 109, cmatrix, &hblur[0][0], channel);
        break;
      case 111:
        initialize_with_1d_kernel (scr, 111, cmatrix, &hblur[0][0], channel);
        break;
      case 113:
        initialize_with_1d_kernel (scr, 113, cmatrix, &hblur[0][0], channel);
        break;
      case 115:
        initialize_with_1d_kernel (scr, 115, cmatrix, &hblur[0][0], channel);
        break;
      case 117:
        initialize_with_1d_kernel (scr, 117, cmatrix, &hblur[0][0], channel);
        break;
      case 119:
        initialize_with_1d_kernel (scr, 119, cmatrix, &hblur[0][0], channel);
        break;
      case 121:
        initialize_with_1d_kernel (scr, 121, cmatrix, &hblur[0][0], channel);
        break;
      case 123:
        initialize_with_1d_kernel (scr, 123, cmatrix, &hblur[0][0], channel);
        break;
      case 125:
        initialize_with_1d_kernel (scr, 125, cmatrix, &hblur[0][0], channel);
        break;
      case 127:
        initialize_with_1d_kernel (scr, 127, cmatrix, &hblur[0][0], channel);
        break;
      case 129:
        initialize_with_1d_kernel (scr, 129, cmatrix, &hblur[0][0], channel);
        break;
      case 131:
        initialize_with_1d_kernel (scr, 131, cmatrix, &hblur[0][0], channel);
        break;
      case 133:
        initialize_with_1d_kernel (scr, 133, cmatrix, &hblur[0][0], channel);
        break;
      case 135:
        initialize_with_1d_kernel (scr, 135, cmatrix, &hblur[0][0], channel);
        break;
      case 137:
        initialize_with_1d_kernel (scr, 137, cmatrix, &hblur[0][0], channel);
        break;
      case 139:
        initialize_with_1d_kernel (scr, 139, cmatrix, &hblur[0][0], channel);
        break;
      case 141:
        initialize_with_1d_kernel (scr, 141, cmatrix, &hblur[0][0], channel);
        break;
      case 143:
        initialize_with_1d_kernel (scr, 143, cmatrix, &hblur[0][0], channel);
        break;
      case 145:
        initialize_with_1d_kernel (scr, 145, cmatrix, &hblur[0][0], channel);
        break;
      case 147:
        initialize_with_1d_kernel (scr, 147, cmatrix, &hblur[0][0], channel);
        break;
      case 149:
        initialize_with_1d_kernel (scr, 149, cmatrix, &hblur[0][0], channel);
        break;
      case 151:
        initialize_with_1d_kernel (scr, 151, cmatrix, &hblur[0][0], channel);
        break;
      case 153:
        initialize_with_1d_kernel (scr, 153, cmatrix, &hblur[0][0], channel);
        break;
      case 155:
        initialize_with_1d_kernel (scr, 155, cmatrix, &hblur[0][0], channel);
        break;
      case 157:
        initialize_with_1d_kernel (scr, 157, cmatrix, &hblur[0][0], channel);
        break;
      case 159:
        initialize_with_1d_kernel (scr, 159, cmatrix, &hblur[0][0], channel);
        break;
      case 161:
        initialize_with_1d_kernel (scr, 161, cmatrix, &hblur[0][0], channel);
        break;
      case 163:
        initialize_with_1d_kernel (scr, 163, cmatrix, &hblur[0][0], channel);
        break;
      case 165:
        initialize_with_1d_kernel (scr, 165, cmatrix, &hblur[0][0], channel);
        break;
      case 167:
        initialize_with_1d_kernel (scr, 167, cmatrix, &hblur[0][0], channel);
        break;
      case 169:
        initialize_with_1d_kernel (scr, 169, cmatrix, &hblur[0][0], channel);
        break;
      case 171:
        initialize_with_1d_kernel (scr, 171, cmatrix, &hblur[0][0], channel);
        break;
      case 173:
        initialize_with_1d_kernel (scr, 173, cmatrix, &hblur[0][0], channel);
        break;
      case 175:
        initialize_with_1d_kernel (scr, 175, cmatrix, &hblur[0][0], channel);
        break;
      case 177:
        initialize_with_1d_kernel (scr, 177, cmatrix, &hblur[0][0], channel);
        break;
      case 179:
        initialize_with_1d_kernel (scr, 179, cmatrix, &hblur[0][0], channel);
        break;
      case 181:
        initialize_with_1d_kernel (scr, 181, cmatrix, &hblur[0][0], channel);
        break;
      case 183:
        initialize_with_1d_kernel (scr, 183, cmatrix, &hblur[0][0], channel);
        break;
      case 185:
        initialize_with_1d_kernel (scr, 185, cmatrix, &hblur[0][0], channel);
        break;
      case 187:
        initialize_with_1d_kernel (scr, 187, cmatrix, &hblur[0][0], channel);
        break;
      case 189:
        initialize_with_1d_kernel (scr, 189, cmatrix, &hblur[0][0], channel);
        break;
      case 191:
        initialize_with_1d_kernel (scr, 191, cmatrix, &hblur[0][0], channel);
        break;
      case 193:
        initialize_with_1d_kernel (scr, 193, cmatrix, &hblur[0][0], channel);
        break;
      case 195:
        initialize_with_1d_kernel (scr, 195, cmatrix, &hblur[0][0], channel);
        break;
      case 197:
        initialize_with_1d_kernel (scr, 197, cmatrix, &hblur[0][0], channel);
        break;
      case 199:
        initialize_with_1d_kernel (scr, 199, cmatrix, &hblur[0][0], channel);
        break;

      case 201:
        initialize_with_1d_kernel (scr, 201, cmatrix, &hblur[0][0], channel);
        break;
      case 203:
        initialize_with_1d_kernel (scr, 203, cmatrix, &hblur[0][0], channel);
        break;
      case 205:
        initialize_with_1d_kernel (scr, 205, cmatrix, &hblur[0][0], channel);
        break;
      case 207:
        initialize_with_1d_kernel (scr, 207, cmatrix, &hblur[0][0], channel);
        break;
      case 209:
        initialize_with_1d_kernel (scr, 209, cmatrix, &hblur[0][0], channel);
        break;
      case 211:
        initialize_with_1d_kernel (scr, 211, cmatrix, &hblur[0][0], channel);
        break;
      case 213:
        initialize_with_1d_kernel (scr, 213, cmatrix, &hblur[0][0], channel);
        break;
      case 215:
        initialize_with_1d_kernel (scr, 215, cmatrix, &hblur[0][0], channel);
        break;
      case 217:
        initialize_with_1d_kernel (scr, 217, cmatrix, &hblur[0][0], channel);
        break;
      case 219:
        initialize_with_1d_kernel (scr, 219, cmatrix, &hblur[0][0], channel);
        break;
      case 221:
        initialize_with_1d_kernel (scr, 221, cmatrix, &hblur[0][0], channel);
        break;
      case 223:
        initialize_with_1d_kernel (scr, 223, cmatrix, &hblur[0][0], channel);
        break;
      case 225:
        initialize_with_1d_kernel (scr, 225, cmatrix, &hblur[0][0], channel);
        break;
      case 227:
        initialize_with_1d_kernel (scr, 227, cmatrix, &hblur[0][0], channel);
        break;
      case 229:
        initialize_with_1d_kernel (scr, 229, cmatrix, &hblur[0][0], channel);
        break;
      case 231:
        initialize_with_1d_kernel (scr, 231, cmatrix, &hblur[0][0], channel);
        break;
      case 233:
        initialize_with_1d_kernel (scr, 233, cmatrix, &hblur[0][0], channel);
        break;
      case 235:
        initialize_with_1d_kernel (scr, 235, cmatrix, &hblur[0][0], channel);
        break;
      case 237:
        initialize_with_1d_kernel (scr, 237, cmatrix, &hblur[0][0], channel);
        break;
      case 239:
        initialize_with_1d_kernel (scr, 239, cmatrix, &hblur[0][0], channel);
        break;
      case 241:
        initialize_with_1d_kernel (scr, 241, cmatrix, &hblur[0][0], channel);
        break;
      case 243:
        initialize_with_1d_kernel (scr, 243, cmatrix, &hblur[0][0], channel);
        break;
      case 245:
        initialize_with_1d_kernel (scr, 245, cmatrix, &hblur[0][0], channel);
        break;
      case 247:
        initialize_with_1d_kernel (scr, 247, cmatrix, &hblur[0][0], channel);
        break;
      case 249:
        initialize_with_1d_kernel (scr, 249, cmatrix, &hblur[0][0], channel);
        break;
#endif
      default:
        printf ("unspecialized clen %i %f %i\n", clen, blur_radius, channel);
        initialize_with_1d_kernel (scr, clen, cmatrix, &hblur[0][0], channel);
      }

  memcpy (add, scr.add, sizeof (add));

  // free (hblur);
  free (cmatrix);
}

/* Result of real fft is symmetric.  We need only N /2 + 1 complex values.
   Moreover point spread functions we compute are symmetric real functions so
   the FFT result is again a real function (all complex values should be 0 up
   to roundoff errors).  */
static constexpr const int fft_size = screen::size / 2 + 1;
typedef fftw_complex fft_1d[fft_size];
typedef fftw_complex fft_2d[screen::size * fft_size];

static int
gaussian_blur_mtf_fast (coord_t blur_radius, fft_complex_t<double>::type *out)
{
  luminosity_t *cmatrix;
  // blur_radius = 0;
  int clen = fir_blur::gen_convolve_matrix (blur_radius, &cmatrix);
  int half_clen = clen / 2;
  // luminosity_t nrm = std::sqrt(screen::size);
  std::vector<double, fft_allocator<double>> in (screen::size, 0.0);
  for (int i = 0; i < clen; i++)
    {
      int idx = (i - half_clen /*+ screen::size / 4*/) & (screen::size - 1);
      in[idx] += cmatrix[i] /** nrm*/;
    }
  auto plan = fft_plan_r2c_1d<double> (screen::size, in.data (), out);
  plan.execute_r2c (in.data (), out);
  // for (int i = 0; i < fft_size; i++)
  // printf ("%i: %f %f\n", i, out[i][0], out[i][1]);
  free (cmatrix);
  return clen;
}

static void
initialize_with_1D_fft_fast (screen &out_scr, const screen &scr,
                             const fft_complex_t<double>::type *weights, int cmin, int cmax)
{
  auto in = fft_alloc_complex<double> (screen::size * fft_size);
  std::vector<double, fft_allocator<double>> out (screen::size * screen::size);

  auto plan_2d_inv = fft_plan_c2r_2d<double> (screen::size, screen::size, in.get (), out.data ());
  auto plan_2d = fft_plan_r2c_2d<double> (screen::size, screen::size, out.data (), in.get ());
  for (int c = cmin; c <= cmax; c++)
    {
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          out[y * screen::size + x] = scr.mult[y][x][c];
      plan_2d.execute_r2c (out.data (), in.get ());
      for (int y = 0; y < fft_size; y++)
        {
          std::complex w2 (weights[y][0], weights[y][1]);
          for (int x = 0; x < fft_size; x++)
            {
              std::complex w1 (weights[x][0], weights[x][1]);
              std::complex v (in.get ()[y * fft_size + x][0],
                              in.get ()[y * fft_size + x][1]);
              in.get ()[y * fft_size + x][0] = real (v * w1 * w2);
              in.get ()[y * fft_size + x][1] = imag (v * w1 * w2);
            }
        }
      for (int y = 1; y < fft_size; y++)
        {
          std::complex w2 (weights[y][0], -weights[y][1]);
          for (int x = 0; x < fft_size; x++)
            {
              std::complex w1 (weights[x][0], weights[x][1]);
              std::complex v (in.get ()[(screen::size - y) * fft_size + x][0],
                              in.get ()[(screen::size - y) * fft_size + x][1]);
              in.get ()[(screen::size - y) * fft_size + x][0] = real (v * w1 * w2);
              in.get ()[(screen::size - y) * fft_size + x][1] = imag (v * w1 * w2);
            }
        }
      plan_2d_inv.execute_c2r (in.get (), out.data ());
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          {
            out_scr.mult[y][x][c]
                = out[y * screen::size + x]
                  * ((luminosity_t)1 / (screen::size * screen::size));
            // printf ("%f\n", out[y * screen::size + x]);
#if 0
            if (out_scr.mult[y][x][c] < 0)
              out_scr.mult[y][x][c] = 0;
            if (out_scr.mult[y][x][c] > 1)
              out_scr.mult[y][x][c] = 1;
#endif
          }
    }
}

static void
scale_by_weights (fft_complex_t<double>::type *in, const fft_2d weights)
{
  for (int i = 0; i < fft_size * screen::size; i++)
    {
      std::complex w (weights[i][0], weights[i][1]);
      std::complex v (in[i][0], in[i][1]);
      in[i][0] = real (v * w);
      in[i][1] = imag (v * w);
    }
}

/* Apply 2D FFT to SCR, scale by WEIGHTS and write to OUT_SCR.  */

static void
initialize_with_2D_fft_fast (screen &out_scr, const screen &scr,
                             const fft_2d weights, int cmin, int cmax)
{
  auto in = fft_alloc_complex<double> (screen::size * fft_size);
  std::vector<double, fft_allocator<double>> out (screen::size * screen::size);
  auto plan_2d_inv = fft_plan_c2r_2d<double> (screen::size, screen::size, in.get (), out.data ());
  auto plan_2d = fft_plan_r2c_2d<double> (screen::size, screen::size, out.data (), in.get ());
  for (int c = cmin; c <= cmax; c++)
    {
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          out[y * screen::size + x] = scr.mult[y][x][c];
      plan_2d.execute_r2c (out.data (), in.get ());
      scale_by_weights (in.get (), weights);
      plan_2d_inv.execute_c2r (in.get (), out.data ());
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
	  out_scr.mult[y][x][c]
	     //= std::clamp (out [y * screen::size + x], 0.0, 1.0);
	     = out [y * screen::size + x];
    }
}


/* Apply Richardson-Lucy deconvolution shaprening on SCR and write it to
   out_scr.  */

static void
initialize_with_richardson_lucy (screen &out_scr, const screen &scr,
				 const fft_2d weights, int cmin, int cmax,
				 int iterations, double sigma)
{
  auto in = fft_alloc_complex<double> (screen::size * fft_size);
  std::vector<double, fft_allocator<double>> estimate (screen::size * screen::size);
  std::vector<double, fft_allocator<double>> observed (screen::size * screen::size);
  std::vector<double, fft_allocator<double>> ratios (screen::size * screen::size);
  auto plan_2d_inv = fft_plan_c2r_2d<double> (screen::size, screen::size, in.get (), observed.data ());
  auto plan_2d = fft_plan_r2c_2d<double> (screen::size, screen::size, observed.data (), in.get ());
  for (int c = cmin; c <= cmax; c++)
    {
      const double contrast = 0.8;
      /* Be sure that observed has no zeros by reducing contrast.  */
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          observed[y * screen::size + x] = 0.5 + (scr.mult[y][x][c] - 0.5) * contrast;

      /* First blur the screen.  */
      plan_2d.execute_r2c (observed.data (), in.get ());
      scale_by_weights (in.get (), weights);
      plan_2d_inv.execute_c2r (in.get (), observed.data ());

      /* Now start sharpening back.  */
      memcpy (estimate.data (), observed.data (), estimate.size () * sizeof (double));

      /* TODO: one FFT can be saved first iteration.  */
      for (int i = 0; i < iterations; i++)
	{
	  /* Step A: Re-blur the current estimate.  */
	  plan_2d.execute_r2c (estimate.data (), in.get ());
	  scale_by_weights (in.get (), weights);
          /* We used plan_2d_inv with observed.data(), we need it with ratios.data().
             But wait, plan_2d_inv was created with in.get() and observed.data().
             Since caching is used, we can just create a new plan here and it will be cached.  */
          auto plan_ratios = fft_plan_c2r_2d<double> (screen::size, screen::size, in.get (), ratios.data ());
	  plan_ratios.execute_c2r (in.get (), ratios.data ());

	  /* Step B: ratio = observed / (re-blurred + epsilon)  */
	  double epsilon = 1e-12 /*1e-7 for float*/;
	  double scale = 1;
	  if (sigma > 0)
	    for (int j = 0; j < screen::size * screen::size; j++)
	      {
		double reblurred = ratios[j] * scale;
		double diff = observed[j] - reblurred;
		if (reblurred > epsilon && std::abs (diff) > 2 * sigma)
		  ratios[j] = 1.0 + (reblurred * diff) / (reblurred * reblurred + sigma * sigma);
		else
		  ratios[j] = 1.0;
	      }
	  else
	    for (int j = 0; j < screen::size * screen::size; j++)
	      {
		double reblurred = ratios[j] * scale;
		if (reblurred > epsilon)
		  ratios[j] = observed[j] / reblurred;
		else
		  ratios[j] = 1.0;
	      }
	  /* Step C: Update estimate
	     FFT(ratio) -> multiply by FFT(PSF_flipped) -> IFFT
	     estimate = estimate * result_of_Step_C  */

	  /* Do FFT of ratio */
          plan_2d.execute_r2c (ratios.data (), in.get ());
	  /* Scale by complex conjugate of blur kernel  */
	  for (int jj = 0; jj < fft_size * screen::size; jj++)
	    {
	      std::complex w (weights[jj][0], -weights[jj][1]);
	      std::complex v (in.get ()[jj][0], in.get ()[jj][1]);
	      in.get ()[jj][0] = real (v * w);
	      in.get ()[jj][1] = imag (v * w);
	    }
	  /* Now initialize ratios  */
	  plan_2d_inv.execute_c2r (in.get (), ratios.data ());

	  /* estimate = estimate * result_of_Step_C  */
	  for (int i = 0; i < screen::size * screen::size; i++)
	    estimate[i] *= ratios[i] * scale;
	}


      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
	  out_scr.mult[y][x][c]
	     = 0.5 + (estimate [y * screen::size + x] - 0.5) * (1 / contrast);
    }
}

template <typename T>
static void
compute_point_spread (T *data,
                      precomputed_function<luminosity_t> &point_spread,
                      luminosity_t scale)
{
  luminosity_t sum = 0;
  int range = point_spread.get_max ();
  for (int y = 0; y <= screen::size / 2; y++)
    for (int x = 0; x <= screen::size / 2; x++)
      {
        luminosity_t w = 0;

        for (int yy = -range * screen::size; yy <= (1 + range) * screen::size;
             yy += screen::size)
          for (int xx = -range * screen::size;
               xx <= (1 + range) * screen::size; xx += screen::size)
            {
              luminosity_t dist
                  = my_sqrt ((luminosity_t)((x - xx) * (x - xx)
                                            + (y - yy) * (y - yy)))
                    * (scale * (1 / (luminosity_t)screen::size));
              w += point_spread.apply (dist);
            }
        if (w < 0)
          w = 0;
        data[y * screen::size + x] = w;
        sum += w;
        if (x)
          {
            data[y * screen::size + (screen::size - x)] = w;
            sum += w;
          }
        if (y)
          {
            data[(screen::size - y) * screen::size + x] = w;
            sum += w;
            if (x)
              {
                data[(screen::size - y) * screen::size + (screen::size - x)]
                    = w;
                sum += w;
              }
          }
      }
  luminosity_t nrm = 1.0 / (screen::size * screen::size);
  // luminosity_t nrm = 1;
  // luminosity_t nrm = screen::size * screen::size;
  if (!sum)
    {
      sum = nrm;
      data[0] = 1;
    }
  else
    {
      for (int x = 0; x < screen::size * screen::size; x++)
        data[x] *= (nrm / sum);
    }
  if (0)
    {
      tiff_writer_params p;
      int tiles = 3;
      p.filename = "/tmp/ps.tif";
      p.width = screen::size * tiles;
      p.height = screen::size * tiles;
      // p.icc_profile = buffer;
      // p.icc_profile_len = len;
      p.depth = 16;
      const char *error;
      tiff_writer out (p, &error);
      // free (buffer);
      if (error)
        return;
      // printf ("%f\n", sum);
      T max = 0;
      for (int x = 0; x < screen::size * screen::size; x++)
        max = std::max (max, data[x]);
      for (int y = 0; y < screen::size * tiles; y++)
        {
          for (int x = 0; x < screen::size * tiles; x++)
            {
              int i = (data[(y % screen::size) * screen::size
                            + (x % screen::size)]
                       * 65535 / max);
              out.put_pixel (x, i, i, i);
            }
          if (!out.write_row ())
            return;
        }
    }
}

static void
point_spread_fft (fft_complex_t<double>::type *weights,
                  precomputed_function<luminosity_t> &point_spread,
                  luminosity_t scale)
{
  std::vector<double, fft_allocator<double>> data (screen::size * screen::size);
  compute_point_spread (data.data (), point_spread, scale);
  auto plan_2d = fft_plan_r2c_2d<double> (screen::size, screen::size, data.data (), weights);
#if 0
  for (int x = 0; x < screen::size; x++)
    printf ("%i %f\n", x, data[x]);
#endif
  plan_2d.execute_r2c (data.data (), weights);
}

/* Compute average error between the two implementations.  */
bool
screen::almost_equal_p (const screen &scr, luminosity_t *delta_ret,
                        luminosity_t maxdelta) const
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

/* Compare sums of individual channels.  */
bool
screen::sum_almost_equal_p (const screen &scr, rgbdata *delta_ret,
                            luminosity_t maxdelta) const
{
  rgbdata sum1, sum2, delta;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      {
        sum1[0] += scr.mult[y][x][0];
        sum1[1] += scr.mult[y][x][1];
        sum1[2] += scr.mult[y][x][2];
        sum2[0] += mult[y][x][0];
        sum2[1] += mult[y][x][1];
        sum2[2] += mult[y][x][2];
      }
  delta = sum1 - sum2;
  if (delta_ret)
    *delta_ret = delta;
  return sum1.almost_equal_p (sum2);
}

/* Compute gaussiab blur using 2d point spread and fft.
   Implemented primarily for testing.  It is always slower than
   1d fft version.  */

static void
initialize_with_gaussian_blur_fft2d (screen &dst, const screen &scr,
                                     luminosity_t radius, int clen, int cmin,
                                     int cmax)
{
  int nvals = (clen + 1) / 2;
  // printf ("nvals %i\n", nvals);
  std::unique_ptr<luminosity_t[]> vals (new luminosity_t[nvals + 2]);
  radius *= screen::size;
  if (nvals == 1)
    vals[0] = 1;
  else
    for (int i = 0; i < nvals; i++)
      vals[i] = fir_blur::gaussian_func_1d (i, radius);
  vals[nvals] = vals[nvals + 1] = 0;
  precomputed_function<luminosity_t> point_spread (
      0, (nvals + 2 - 1) * (1 / (luminosity_t)screen::size), vals.get (),
      nvals + 2);
#if 0
  for (int i = 0; i < nvals; i++)
  {
    printf ("%f %f\n", fir_blur::gaussian_func_1d (i, radius), point_spread.apply (i * (1 / (luminosity_t)screen::size)));
    assert ((fir_blur::gaussian_func_1d (i, radius) - point_spread.apply (i * (1 / (luminosity_t)screen::size))) < 0.000000001);
  }
#endif
  auto mtf = fft_alloc_complex<double> (screen::size * fft_size);
  point_spread_fft (mtf.get (), point_spread, 1);
  initialize_with_2D_fft_fast (dst, scr, mtf.get (), cmin, cmax);
}

void
screen::initialize_with_gaussian_blur (screen &scr, rgbdata blur_radius,
                                       blur_alg alg)
{
  // bool fft_used = false;
  memcpy (add, scr.add, sizeof (add));
  if (blur_radius.red <= 0 && blur_radius.green <= 0 && blur_radius.blue <= 0)
    {
      memcpy (mult, scr.mult, sizeof (mult));
      return;
    }
  bool all = (blur_radius.red == blur_radius.green)
             && (blur_radius.red == blur_radius.blue);
  // bool maxradius = false;
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
      // if (blur_radius[c] > max_blur_radius)
      // maxradius = true;
      bool do_fft = false;
      bool do_fft2d = false;
      if (alg == blur_fft2d)
        do_fft2d = true;
      else if (alg == blur_fft)
        do_fft = true;
      else if (alg == blur_auto)
        {
          // technically it is more precise to do 2d fft, but it is slower
          // and practically seems to make little difference.
          if (clen > screen::size / 2 && 0)
            do_fft2d = true;
          else
            do_fft = (blur_radius[c] >= max_blur_radius) || clen > 15;
        }
      if (do_fft2d)
        initialize_with_gaussian_blur_fft2d (*this, scr, blur_radius[c], clen,
                                             c, all ? 2 : c);
      else if (!do_fft)
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
          auto weights = fft_alloc_complex<double> (fft_size);
          gaussian_blur_mtf_fast (blur_radius[c] * screen::size, weights.get ());
          initialize_with_1D_fft_fast (*this, scr, weights.get (), c, all ? 2 : c);
        }
#endif
      if (all)
        break;
    }
}
void
screen::strip (coord_t first_strip_width, coord_t second_strip_width,
               int first_strip_color, int second_strip_color,
               int third_strip_color)
{
  /* Change from first to second.  */
  coord_t c1 = size * (first_strip_width * (coord_t)0.5);
  /* Change from second to third.  */
  coord_t c2 = size * (first_strip_width * (coord_t)0.5 + second_strip_width);
  /* Change from third back to first.  */
  coord_t c3 = size * (1 - (first_strip_width * (coord_t)0.5));
  for (int yy = 0; yy < size; yy++)
    {
      luminosity_t c[3] = { 0, 0, 0 };
      if (yy < (int)c1 || yy > (int)c3)
        c[first_strip_color] = 1;
      else if (yy == (int)c1)
        {
          coord_t v = c1 - (int)c1;
          c[first_strip_color] = 1 - v;
          c[second_strip_color] = v;
        }
      else if (yy < (int)c2)
        {
          c[second_strip_color] = 1;
        }
      else if (yy == (int)c2)
        {
          coord_t v = c2 - (int)c2;
          c[second_strip_color] = 1 - v;
          c[third_strip_color] = v;
        }
      else if (yy < (int)c3)
        c[third_strip_color] = 1;
      else
        {
          coord_t v = c3 - (int)c3;
          c[third_strip_color] = 1 - v;
          c[first_strip_color] = v;
        }
      for (int xx = 0; xx < size; xx++)
        {
          mult[xx][yy][0] = c[0];
          mult[xx][yy][1] = c[1];
          mult[xx][yy][2] = c[2];
          add[xx][yy][0] = 0;
          add[xx][yy][1] = 0;
          add[xx][yy][2] = 0;
        }
    }
}
void
screen::preview_strip (coord_t first_strip_width, coord_t second_strip_width,
                       int first_strip_color, int second_strip_color,
                       int third_strip_color)
{
  coord_t w = 0.2;
  coord_t h = 0.4;
  coord_t c1e = size * (first_strip_width * (w / 2)) + (coord_t)0.5;
  coord_t c1s = size * (1 - first_strip_width * (w / 2)) + (coord_t)0.5;
  /* Change from second to third.  */
  coord_t c2s = size
                    * (0.5 * first_strip_width
                       + (second_strip_width * ((coord_t)0.5 - w / 2)))
                + (coord_t)0.5;
  coord_t c2e = size
                    * (0.5 * first_strip_width
                       + (second_strip_width * ((coord_t)0.5 + w / 2)))
                + (coord_t)0.5;
  /* Change from third back to first.  */
  coord_t third_strip_width = 1 - first_strip_width - second_strip_width;
  coord_t c3s = size
                    * (1 - 0.5 * first_strip_width
                       - third_strip_width * ((coord_t)0.5 + w / 2))
                + (coord_t)0.5;
  coord_t c3e = size
                    * (1 - 0.5 * first_strip_width
                       - third_strip_width * ((coord_t)0.5 - w / 2))
                + (coord_t)0.5;
  for (int yy = 0; yy < size; yy++)
    {
      luminosity_t c[3] = { 0, 0, 0 };
      luminosity_t a[3] = { 0, 0, 0 };
      if (yy < (int)c1e || yy > (int)c1s)
        c[first_strip_color] = 1;
      else if (yy > (int)c2s && yy < (int)c2e)
        c[second_strip_color] = 1;
      else if (yy > (int)c3s && yy < (int)c3e)
        c[third_strip_color] = 1;
      if (c[0] || c[1] || c[2])
        for (int i = 0; i < 3; i++)
          {
            a[i] = c[i] * (coord_t)0.5;
            c[i] = (c[i] + 1) * (coord_t)0.5;
          }
      else
        c[0] = c[1] = c[2] = 1;
      for (int xx = 0; xx < size; xx++)
        {
          if (xx > size * (0.5 - h / 2) && xx < size * (0.5 + h / 2))
            {
              mult[xx][yy][0] = c[0];
              mult[xx][yy][1] = c[1];
              mult[xx][yy][2] = c[2];
              add[xx][yy][0] = a[0];
              add[xx][yy][1] = a[1];
              add[xx][yy][2] = a[2];
            }
          else
            {
              mult[xx][yy][0] = 1;
              mult[xx][yy][1] = 1;
              mult[xx][yy][2] = 1;
              add[xx][yy][0] = 0;
              add[xx][yy][1] = 0;
              add[xx][yy][2] = 0;
            }
        }
    }
}
void
screen::initialize (enum scr_type type, coord_t red_strip_width,
                    coord_t green_strip_width)
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
    case DioptichromeB:
      dufay (green_strip_width ? green_strip_width : 0.33,
             red_strip_width ? red_strip_width : 0.5);
      /* Strip is green instead of red, so swap red and green.  */
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
          std::swap (mult[y][x][0], mult[y][x][1]);
      break;
    case ImprovedDioptichromeB:
    case Omnicolore:
      dufay (red_strip_width ? 1 - red_strip_width : 0.33,
             green_strip_width ? green_strip_width : 0.5);
      /* Strip is blue instead of red, so swap blue and red.  */
      for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
          std::swap (mult[y][x][0], mult[y][x][2]);
      break;
    /* FIXME: In Warner Powrie screen it seems that green is not continous
     * strip.  */
    case WarnerPowrie:
      {
        if (red_strip_width && green_strip_width)
          strip (green_strip_width, 1 - red_strip_width - green_strip_width, 1,
                 2, 0);
        else
          strip (1.0 / 3, 1.0 / 3, 1, 2, 0);
      }
      break;
    case Joly:
      {
        if (red_strip_width && green_strip_width)
          strip (green_strip_width, red_strip_width, 1, 0, 2);
        else
          strip (1.0 / 3, 1.0 / 3, 1, 0, 2);
      }
      break;
    default:
      abort ();
      break;
    }
}
/* Initialize to a given screen for preview window.  */
void
screen::initialize_preview (enum scr_type type, coord_t red_strip_width,
                            coord_t green_strip_width)
{
  if (dufay_like_screen_p (type))
    {
      preview_dufay ();
      if (type == DioptichromeB)
        for (int y = 0; y < size; y++)
          for (int x = 0; x < size; x++)
            {
              std::swap (mult[y][x][0], mult[y][x][1]);
              std::swap (add[y][x][0], add[y][x][1]);
            }
      else if (type == ImprovedDioptichromeB || type == Omnicolore)
        /* Strip is blue instead of red, so swap blue and red.  */
        for (int y = 0; y < size; y++)
          for (int x = 0; x < size; x++)
            {
              std::swap (mult[y][x][0], mult[y][x][2]);
              std::swap (add[y][x][0], add[y][x][2]);
            }
    }
  else if (type == WarnerPowrie)
    {
      if (red_strip_width && green_strip_width)
        preview_strip (green_strip_width,
                       1 - red_strip_width - green_strip_width, 1, 2, 0);
      else
        preview_strip (1.0 / 3, 1.0 / 3, 1, 2, 0);
    }
  else if (type == Joly)
    {
      if (red_strip_width && green_strip_width)
        preview_strip (green_strip_width, red_strip_width, 1, 0, 2);
      else
        preview_strip (1.0 / 3, 1.0 / 3, 1, 0, 2);
    }
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
  mult[size / 2][size / 2][0] = mult[size / 2][size / 2][1]
      = mult[size / 2][size / 2][2] = 1;
}

bool
screen::save_tiff (const char *filename, bool normalize, int tiles) const
{
  tiff_writer_params p;
  rgbdata max = { 0, 0, 0 };
  //void *buffer;
  //size_t len = create_linear_srgb_profile (&buffer);
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
  //p.icc_profile = buffer;
  //p.icc_profile_len = len;
  p.depth = 16;
  const char *error;
  tiff_writer out (p, &error);
  //free (buffer);
  if (error)
    return false;
  for (int y = 0; y < size * tiles; y++)
    {
      for (int x = 0; x < size * tiles; x++)
        {
          int r = invert_gamma (mult[y % size][x % size][0] / max.red, -1) * 65536;
          if (r < 0)
            r = 0;
          if (r > 65535)
            r = 65535;
          int g = invert_gamma (mult[y % size][x % size][1] / max.green, -1) * 65536;
          if (g < 0)
            g = 0;
          if (g > 65535)
            g = 65535;
          int b = invert_gamma (mult[y % size][x % size][2] / max.blue, -1) * 65536;
          if (b < 0)
            b = 0;
          if (b > 65535)
            b = 65535;
          out.put_pixel (x, r, g, b);
        }
      if (!out.write_row ())
        return false;
    }
  return true;
}

void
screen::initialize_with_sharpen_parameters (screen &scr,
					    sharpen_parameters *sharpen[3],
					    bool anticipate_sharpening)
{
  auto fft = fft_alloc_complex<double> (screen::size * fft_size);
  bool all = *sharpen[0] == *sharpen[1] && *sharpen[0] == *sharpen[2];
#if 0
  printf ("Initial patch proportions:");
  scr.patch_proportions ().print (stdout);
  printf ("\n");
#endif
  for (int c = 0; c < 3; c++)
    {
      sharpen_parameters::sharpen_mode mode = sharpen[c]->get_mode ();
      if (!anticipate_sharpening)
	mode = sharpen_parameters::none;
      if (!c || !(*sharpen[c] == *sharpen[c - 1]))
        {
          luminosity_t step = sharpen[c]->scanner_mtf_scale;
          luminosity_t data_scale = 1.0 / (screen::size * screen::size);
	  luminosity_t snr = sharpen[c]->scanner_snr;
          luminosity_t k_const = snr > 0 ? 1.0f / snr : 0;
	  mtf::mtf_cache_t::cached_ptr mtf = mtf::get_mtf (sharpen[c]->scanner_mtf, NULL);
	  mtf->precompute ();
	  int this_psf_size = mtf->psf_size (sharpen[c]->scanner_mtf_scale * screen::size);
	  //printf ("screen step %f %f psf size %i\n", step, screen::size * step, this_psf_size);
	  /* PSF may revisit this_psf_size.  */
	  if (this_psf_size > screen::size)
	    {
	      mtf->precompute_psf ();
	      this_psf_size = mtf->psf_size (sharpen[c]->scanner_mtf_scale * screen::size);
	    }

	  /* Small PSF size: use fast path of producing its FFT directly.  */
	  if (this_psf_size < screen::size)
	    {
	      for (int y = 0; y < fft_size; y++)
		for (int x = 0; x < fft_size; x++)
		  {
		    std::complex ker (
			std::clamp (mtf->get_mtf (x, y, step),
				    (luminosity_t)0, (luminosity_t)1),
			(luminosity_t)0);
		    if (mode == sharpen_parameters::wiener_deconvolution)
		      ker = ker * (conj (ker) / (std::norm (ker) + k_const));
		    else if (mode == sharpen_parameters::blur_deconvolution)
		      ker = ker * ker;
		    ker = ker * data_scale;
		    fft[y * fft_size + x][0] = real (ker);
		    fft[y * fft_size + x][1] = imag (ker);
		    if (y)
		      {
			fft[(screen::size - y) * fft_size + x][0] = real (ker);
			fft[(screen::size - y) * fft_size + x][1] = imag (ker);
		      }
		  }
	    }
	  /* Large PSF: Compute its periodic form and do FFT.  */
	  else
	    {
	      std::vector <double> wrapped_psf (screen::size * screen::size, 0.0);
	      //printf ("This psf size %i\n", this_psf_size);

#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(wrapped_psf,mtf,step,this_psf_size)
	      for (int yy = 0; yy < screen::size; yy++)
	        for (int xx = 0; xx < screen::size; xx++)
		  {
		    double sum = 0.0;
		    for (int y = yy; y < this_psf_size; y += screen::size)
		      for (int x = xx; x < this_psf_size; x += screen::size)
			sum += mtf->get_psf (x, y, (step * screen::size));
		    
		    for (int y = yy; y < this_psf_size; y += screen::size)
		      for (int x = screen::size - xx; x < this_psf_size; x += screen::size)
			sum += mtf->get_psf (x, y, (step * screen::size));
		    
		    for (int y = screen::size - yy; y < this_psf_size; y += screen::size)
		      for (int x = xx; x < this_psf_size; x += screen::size)
			sum += mtf->get_psf (x, y, (step * screen::size));
		    
		    for (int y = screen::size - yy; y < this_psf_size; y += screen::size)
		      for (int x = screen::size - xx; x < this_psf_size; x += screen::size)
			sum += mtf->get_psf (x, y, (step * screen::size));
		    
		    wrapped_psf[yy * screen::size + xx] = sum;
		  }
	      double sum = 0;
	      for (int x = 0; x < screen::size * screen::size; x++)
		  sum += wrapped_psf [x];
	      double sum_inv = 1 / sum;
	      if (snr == 0)
		sum_inv /= screen::size * screen::size;
	      for (int x = 0; x < screen::size * screen::size; x++)
		wrapped_psf [x] *= sum_inv;
	      if (0)
		{
		  tiff_writer_params p;
		  int tiles = 3;
		  p.filename = "/tmp/wrapped-ps.tif";
		  p.width = screen::size * tiles;
		  p.height = screen::size * tiles;
		  p.depth = 16;
		  const char *error;
		  tiff_writer out (p, &error);
		  if (error)
		    return;
		  double max = 0;
		  for (int x = 0; x < screen::size * screen::size; x++)
		    max = std::max (max, wrapped_psf[x]);
		  for (int y = 0; y < screen::size * tiles; y++)
		    {
		      for (int x = 0; x < screen::size * tiles; x++)
			{
			  int i = (wrapped_psf[(y % screen::size) * screen::size
						+ (x % screen::size)]
				   * 65535 / max);
			  out.put_pixel (x, i, i, i);
			}
		      if (!out.write_row ())
			return;
		    }
		}
	      auto plan_2d = fft_plan_r2c_2d<double> (screen::size, screen::size, wrapped_psf.data (), fft.get ());
	      plan_2d.execute_r2c (wrapped_psf.data (), fft.get ());
	      //printf ("screen snr %f mtf0 %f %f", snr, fft[0][0], fft[0][1]);
	      for (int x = 0; x < fft_size * screen::size; x++)
		{
		  std::complex ker (fft[x][0], fft[x][1]);
		  if (mode == sharpen_parameters::wiener_deconvolution)
		    ker = ker * (conj (ker) / (std::norm (ker) + k_const));
		  fft[x][0] = real (ker) * (1.0 / (screen::size * screen::size));
		  fft[x][1] = imag (ker) * (1.0 / (screen::size * screen::size));
		}
	    }
        }
      if (mode != sharpen_parameters::richardson_lucy_deconvolution)
        initialize_with_2D_fft_fast (*this, scr, fft.get (), c, all ? 2 : c);
      else
        initialize_with_richardson_lucy (*this, scr, fft.get (), c, all ? 2 : c, sharpen[c]->richardson_lucy_iterations, sharpen[c]->richardson_lucy_sigma);
      if (all)
	break;
    }
#if 0
  printf ("Modified patch proportions:");
  scr.patch_proportions ().print (stdout);
  printf ("\n");
#endif
}
void
screen::initialize_with_point_spread (
    screen &scr, precomputed_function<luminosity_t> *point_spread[3],
    rgbdata scale)
{
  auto mtf = fft_alloc_complex<double> (screen::size * fft_size);
  for (int c = 0; c < 3; c++)
    {
      point_spread_fft (mtf.get (), *(point_spread[c]), scale[c]);
      initialize_with_2D_fft_fast (*this, scr, mtf.get (), c, c);
    }
}

void
screen::initialize_with_fft_blur (screen &scr, rgbdata blur_radius)
{
  /* This is sample MTF curve of a camera taken from IMOD's mtffliter.
     first column are spartial frequencies in reciprocal pixels and second
     column is a contrast loss.  */
  const static luminosity_t data[][2]
      = { { 0.0085, 0.98585 }, { 0.0221, 0.94238 }, { 0.0357, 0.89398 },
          { 0.0493, 0.83569 }, { 0.0629, 0.76320 }, { 0.0765, 0.69735 },
          { 0.0901, 0.63647 }, { 0.1037, 0.56575 }, { 0.1173, 0.49876 },
          { 0.1310, 0.43843 }, { 0.1446, 0.38424 }, { 0.1582, 0.34210 },
          { 0.1718, 0.30289 }, { 0.1854, 0.26933 }, { 0.1990, 0.23836 },
          { 0.2126, 0.21318 }, { 0.2262, 0.18644 }, { 0.2398, 0.15756 },
          { 0.2534, 0.14863 }, { 0.2670, 0.12485 }, { 0.2806, 0.11436 },
          { 0.2942, 0.09183 }, { 0.3078, 0.08277 }, { 0.3214, 0.07021 },
          { 0.3350, 0.05714 }, { 0.3486, 0.04388 }, { 0.3622, 0.03955 },
          { 0.3759, 0.03367 }, { 0.3895, 0.02844 }, { 0.4031, 0.02107 },
          { 0.4167, 0.02031 }, { 0.4303, 0.01796 }, { 0.4439, 0.00999 },
          { 0.4575, 0.01103 }, { 0.4711, 0.00910 }, { 0.4898, 0.00741 } };
  int data_size = sizeof (data) / sizeof (luminosity_t) / 2 - 1;
  bool use_sqrt = false;
  memcpy (add, scr.add, sizeof (add));

  if (!use_sqrt)
    {
      bool all = (blur_radius.red == blur_radius.green)
                 && (blur_radius.red == blur_radius.blue);
      for (int c = 0; c < 3; c++)
        {
          auto weights = fft_alloc_complex<double> (fft_size);
          /* blur_radius is blur in the screen dimensions.
             step should be 1 / size, but blur_radius of 1 corresponds to size
             so this evens out. Compensate so the blur is approximately same as
             gaussian blur.	 */
          luminosity_t step = blur_radius[c] * 0.5 * (0.75 / 0.61);
          luminosity_t f = step;
          weights.get ()[0][0] = 1;
          weights.get ()[0][1] = 0;
          for (int x = 1, p = 0; x < fft_size; x++, f += step)
            {
              while (p < data_size - 1 && data[p + 1][0] < f)
                p++;
              luminosity_t w = data[p][1]
                               + (data[p + 1][1] - data[p][1])
                                     * (f - data[p][0])
                                     / (data[p + 1][0] - data[p][0]);
              // printf ("%f %i %f d1 %f %f d2 %f
              // %f\n",f,p,w,data[p][0],data[p][1],data[p+1][0],data[p+1][1]);
              if (w < 0)
                w = 0;
              if (w > 1)
                w = 1;
              weights.get ()[x][0] = w;
              weights.get ()[x][1] = 0;
            }
          initialize_with_1D_fft_fast (*this, scr, weights.get (), c, all ? 2 : c);
          if (all)
            break;
        }
    }
  else
    {
      abort ();
#if 0
      static precomputed_function<luminosity_t> v (0, 0.5, size, data,
                                                   data_size);
      precomputed_function<luminosity_t> *vv[3] = { &v, &v, &v };
      /* TODO: Implement correctly.   */
      initialize_with_2D_fft (scr, vv, blur_radius * 0.5 * (0.75 / 0.61));
#endif
    }
}

void
screen::initialize_with_blur (screen &scr, coord_t blur_radius,
                              enum blur_type type, enum blur_alg alg)
{
  initialize_with_blur (scr, { blur_radius, blur_radius, blur_radius }, type,
                        alg);
}
void
screen::initialize_with_blur (screen &scr, rgbdata blur_radius,
                              enum blur_type type, enum blur_alg alg)
{
  if (type == blur_gaussian)
    initialize_with_gaussian_blur (scr, blur_radius, alg);
  else
    initialize_with_fft_blur (scr, blur_radius);
}
void
screen::clamp ()
{
  for (int yy = 0; yy < size; yy++)
    for (int xx = 0; xx < size; xx++)
      {
        mult[yy][xx][0] = std::clamp (mult[yy][xx][0], (luminosity_t)0, (luminosity_t)1);
        mult[yy][xx][1] = std::clamp (mult[yy][xx][1], (luminosity_t)0, (luminosity_t)1);
        mult[yy][xx][2] = std::clamp (mult[yy][xx][2], (luminosity_t)0, (luminosity_t)1);
      }
}
rgbdata
screen::patch_proportions () const
{
  rgbdata sum = {0, 0, 0};
  for (int yy = 0; yy < size; yy++)
    for (int xx = 0; xx < size; xx++)
      {
        sum.red += mult[yy][xx][0]; 
        sum.green += mult[yy][xx][1]; 
        sum.blue += mult[yy][xx][2];
      }
  luminosity_t s = sum.red + sum.green + sum.blue;
  sum /= s;
  return sum;
}
}
