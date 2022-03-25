#include "render-fast.h"

render_fast::render_fast (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval, int dst_maxval)
 : render_to_scr (param, img, img_width, img_height, maxval, dst_maxval)
{
  m_scale = m_dst_maxval / (m_maxval * 2.0);
}

void
render_fast::render_pixel (int x, int y, int *r, int *g, int *b)
{
  double dx = (x - m_scr_xshift) + 0.5, dy = (y - m_scr_yshift) + 0.5;

/*  This is precise version:

       #define pixel(xo,yo) get_img_pixel_scr (dx + xo, dy + yo)

    In the following we assume that map is linear within single repeetition
    of the screen.  */
  double zx, zy;
  double xx, xy;
  double yx, yy;
  m_scr_to_img.to_img (dx, dy, &zx, &zy);
  m_scr_to_img.to_img (dx+1, dy, &xx, &xy);
  m_scr_to_img.to_img (dx, dy+1, &yx, &yy);
  xx = xx - zx;
  xy = xy - zy;
  yx = yx - zx;
  yy = yy - zy;

#define pixel(xo,yo) get_img_pixel (zx + xx * (xo) + yx * (yo), zy + xy * (xo) + yy * (yo))
  
  /* Thames, Finlay and Paget screen are organized as follows:
    
     G   R   G
       B   B
     R   G   R
       B   B
     G   R   G  */

  double green = (pixel (0,0) + pixel (0,1) + pixel (1,0) + pixel (1,1)) * 0.25 + pixel (0.5, 0.5);
  double red = (pixel (0.5, 0) + pixel (0, 0.5) + pixel (1, 0.5) + pixel (0.5, 1)) * 0.5;
  double blue = (pixel (0.25, 0.25) + pixel (0.75, 0.25) + pixel (0.25, 0.75) + pixel (0.75, 0.75)) * 0.5;
#undef getpixel
  double avg = (red + green + blue) * 0.3333;
  red = (avg + (red - avg) * 5) * m_scale;
  green = (avg + (green - avg) * 5) * m_scale;
  blue = (avg + (blue - avg) * 5) * m_scale;
  if (red < 0)
    red = 0;
  if (red >= m_dst_maxval)
    red = m_dst_maxval - 1;
  if (green < 0)
    green = 0;
  if (green >= m_dst_maxval)
    green = m_dst_maxval - 1;
  if (blue < 0)
    blue = 0;
  if (blue >= m_dst_maxval)
    blue = m_dst_maxval - 1;


  *r = red;
  *g = green;
  *b = blue;
}
