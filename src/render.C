#include "render.h"

render::render (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval)
{
  m_img = img;
  m_img_width = img_width;
  m_img_height = img_height;
  m_scr_to_img.set_parameters (param);
  m_maxval = maxval;
}

static inline double
cubic_interpolate (double p[4], double x)
{
  return p[1] + 0.5 * x * (p[2] - p[0] +
			   x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
				x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

static inline double
bicubic_interpolate (double p[4][4], double x, double y)
{
  double arr[4];
  if (x < 0 || x > 1 || y < 0 || y > 1)
    abort ();
  arr[0] = cubic_interpolate (p[0], y);
  arr[1] = cubic_interpolate (p[1], y);
  arr[2] = cubic_interpolate (p[2], y);
  arr[3] = cubic_interpolate (p[3], y);
  return cubic_interpolate (arr, x);
}


/* Determine grayscale value at a given position in the image.  */

double
render::get_img_pixel (double xp, double yp)
{
  int sx, sy;
  double p[4][4];
  double val;
  sx = xp, sy = yp;

  if (xp < 2 || xp >= m_img_width - 2 || yp < 2 || yp >= m_img_height - 2)
    return 0;
  p[0][0] = m_img[sy - 1][sx - 1];
  p[1][0] = m_img[sy - 1][sx - 0];
  p[2][0] = m_img[sy - 1][sx + 1];
  p[3][0] = m_img[sy - 1][sx + 2];
  p[0][1] = m_img[sy - 0][sx - 1];
  p[1][1] = m_img[sy - 0][sx - 0];
  p[2][1] = m_img[sy - 0][sx + 1];
  p[3][1] = m_img[sy - 0][sx + 2];
  p[0][2] = m_img[sy + 1][sx - 1];
  p[1][2] = m_img[sy + 1][sx - 0];
  p[2][2] = m_img[sy + 1][sx + 1];
  p[3][2] = m_img[sy + 1][sx + 2];
  p[0][3] = m_img[sy + 2][sx - 1];
  p[1][3] = m_img[sy + 2][sx - 0];
  p[2][3] = m_img[sy + 2][sx + 1];
  p[3][3] = m_img[sy + 2][sx + 2];
  val = bicubic_interpolate (p, xp - sx, yp - sy);
  if (val < 0)
    val = 0;
  if (val > m_maxval - 1)
    val = m_maxval - 1;
  return val;
}
/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
double
render::get_img_pixel_scr (double x, double y)
{
  double xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_img_pixel (xp, yp);
}
