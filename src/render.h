#ifndef RENDER_H
#define RENDER_H
#include <netpbm/pgm.h>
#include "scr-to-img.h"

/* Scanned image descriptor.  */
struct image_data
{
  gray **data;
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  int maxval;
};

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class render
{
public:
  render (scr_to_img_parameters param, image_data &img, int dstmaxval);
  inline double get_img_pixel (double x, double y);
  inline double fast_get_img_pixel (double x, double y);
  inline double get_img_pixel_scr (double x, double y);

protected:
  /* Scanned image.  */
  image_data m_img;
  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
};

/* Base class for renderes tha works in screen coordinates (so output image is
   geometrically corrected.  */
class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters param, image_data &img, int dstmaxval)
    : render (param, img, dstmaxval)
  {
    m_scr_to_img.get_range (m_img.width, m_img.height, &m_scr_xshift, &m_scr_yshift, &m_scr_width, &m_scr_height);
  }
  /* This returns screen coordinate width of rendered output.  */
  int get_width ()
  {
    return m_scr_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int get_height ()
  {
    return m_scr_height;
  }
protected:
  /* Rectangular section of the screen to which the whole image fits.

     The section is having dimensions scr_width x scr_height and will
     start at position (-scr_xshift, -scr_yshift).  */
  int m_scr_xshift, m_scr_yshift;
  int m_scr_width, m_scr_height;
};

/* Cubic interpolation helper.  */

static inline double
cubic_interpolate (double p0, double p1, double p2, double p3, double x)
{
  return p1 + 0.5 * x * (p2 - p0 +
			 x * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
			      x * (3.0 * (p1 - p2) + p3 - p0)));
}

/* Determine grayscale value at a given position in the image.  */

inline double
render::fast_get_img_pixel (double xp, double yp)
{
  int x = xp + 0.5, y = yp + 0.5;
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return m_img.data[y][x];
}


/* Determine grayscale value at a given position in the image.
   Use bicubit interpolation.  */

inline double
render::get_img_pixel (double xp, double yp)
{
  double val;
  int sx = xp, sy = yp;

  if (sx < 1 || sx >= m_img.width - 2 || sy < 1 || sy >= m_img.height - 2)
    return 0;
  double rx = xp - sx, ry = yp - sy;
  val = cubic_interpolate (cubic_interpolate (m_img.data[sy-1][sx-1], m_img.data[sy][sx-1], m_img.data[sy+1][sx-1], m_img.data[sy+2][sx-1], ry),
			   cubic_interpolate (m_img.data[sy-1][sx-0], m_img.data[sy][sx-0], m_img.data[sy+1][sx-0], m_img.data[sy+2][sx-0], ry),
			   cubic_interpolate (m_img.data[sy-1][sx+1], m_img.data[sy][sx+1], m_img.data[sy+1][sx+1], m_img.data[sy+2][sx+1], ry),
			   cubic_interpolate (m_img.data[sy-1][sx+2], m_img.data[sy][sx+2], m_img.data[sy+1][sx+2], m_img.data[sy+2][sx+2], ry),
			   rx);
  if (val < 0)
    val = 0;
  if (val > m_img.maxval - 1)
    val = m_img.maxval - 1;
  return val;
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
inline double
render::get_img_pixel_scr (double x, double y)
{
  double xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_img_pixel (xp, yp);
}
#endif
