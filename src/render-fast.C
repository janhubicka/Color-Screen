#include "render-fast.h"

render_fast::render_fast (scr_to_img_parameters param, image_data &img, int dst_maxval)
 : render_to_scr (param, img, dst_maxval)
{
}

void
render_fast::render_pixel (int x, int y, int *r, int *g, int *b)
{
  double dx = (x - m_scr_xshift), dy = (y - m_scr_yshift);

/*  This is precise version:

       #define pixel(xo,yo) get_img_pixel_scr (dx + xo, dy + yo)

    In the following we assume that map is linear within single repetition
    of the screen.  This saves some matrix multiplication  */
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

#define pixel(xo,yo) fast_get_img_pixel (zx + xx * (xo) + yx * (yo), zy + xy * (xo) + yy * (yo))
  
  /* Thames, Finlay and Paget screen are organized as follows:
    
     G   R   G
       B   B
     R   G   R
       B   B
     G   R   G  */

#if 1
  double green = (pixel (0,0) + pixel (0,1) + pixel (1,0) + pixel (1,1)) * 0.25 + pixel (0.5, 0.5);
  double red = (pixel (0.5, 0) + pixel (0, 0.5) + pixel (1, 0.5) + pixel (0.5, 1)) * 0.5;
  double blue = (pixel (0.25, 0.25) + pixel (0.75, 0.25) + pixel (0.25, 0.75) + pixel (0.75, 0.75)) * 0.5;
#else
  double green = (pixel (0,0) + pixel (0,1) + pixel (1,0) + pixel (1,1)) * 0.5;
  double red = (pixel (0, 0.5) + pixel (0.33, 0.5) + pixel (0.66, 0.5) + pixel (1, 0.5)) * 0.5;
  double blue = (pixel (0.5, 0) + pixel (0.5, 1));
#endif
#undef getpixel
  double avg = (red + green + blue) * 0.3333;
  set_color (red, green, blue, r, g, b);
}
