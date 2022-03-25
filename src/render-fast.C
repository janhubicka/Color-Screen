#include "render-fast.h"

render_fast::render_fast (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval)
 : render_to_scr (param, img, img_width, img_height, maxval)
{
}

void
render_fast::render_pixel (int x, int y, int *r, int *g, int *b)
{
  double dx = (x - m_scr_xshift) + 0.5, dy = (y - m_scr_yshift) + 0.5;
  double avg;
  /* Thames, Finlay and Paget screen are organized as follows:
    
     G   R   G
       B   B
     R   G   R
       B   B
     G   R   G  */
  double green = get_img_pixel_scr (dx, dy)
		 + get_img_pixel_scr (dx + 1, dy + 1)
		 + get_img_pixel_scr (dx, dy + 1)
		 + get_img_pixel_scr (dx + 1, dy)
		 + get_img_pixel_scr (dx + 0.5, dy + 0.5);
  double red = get_img_pixel_scr (dx + 0.5, dy)
	       + get_img_pixel_scr (dx, dy + 0.5)
	       + get_img_pixel_scr (dx + 1, dy + 0.5)
	       + get_img_pixel_scr (dx + 0.5, dy + 1);
  double blue = get_img_pixel_scr (dx + 0.25, dy + 0.25)
		+ get_img_pixel_scr (dx + 0.75, dy + 0.25)
	       	+ get_img_pixel_scr (dx + 0.25, dy + 0.75)
	       	+ get_img_pixel_scr (dx + 0.75, dy + 0.75);
  green = green * 51;
  red = red * 64;
  blue = blue * 64;
  avg = (red + green + blue) / 3;
  red = avg + (red - avg) * 5;
  if (red < 0)
    red = 0;
  if (red >= m_maxval * 256)
    red = m_maxval * 256 - 1;
  green = avg + (green - avg) * 5;
  if (green < 0)
    green = 0;
  if (green >= m_maxval * 256)
    green = m_maxval * 256 - 1;
  blue = avg + (blue - avg) * 5;
  if (blue < 0)
    blue = 0;
  if (blue >= m_maxval * 256)
    blue = m_maxval * 256 - 1;
  *r = red;
  *g = green;
  *b = blue;
}
