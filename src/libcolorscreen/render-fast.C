#include "include/render-fast.h"

render_fast::render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval)
 : render_to_scr (param, img, params, dst_maxval)
{
}

flatten_attr void
render_fast::render_pixel (int x, int y, int *r, int *g, int *b)
{
  coord_t dx = (x - m_scr_xshift), dy = (y - m_scr_yshift);

/*  This is precise version:

       #define pixel(xo,yo) get_img_pixel_scr (dx + xo, dy + yo)

    In the following we assume that map is linear within single repetition
    of the screen.  This saves some matrix multiplication  */
  coord_t zx, zy;
  coord_t xx, xy;
  coord_t yx, yy;
  luminosity_t red, green, blue;
  m_scr_to_img.to_img (dx, dy, &zx, &zy);
  m_scr_to_img.to_img (dx+1, dy, &xx, &xy);
  m_scr_to_img.to_img (dx, dy+1, &yx, &yy);
  xx = xx - zx;
  xy = xy - zy;
  yx = yx - zx;
  yy = yy - zy;

#define pixel(xo,yo) get_img_pixel (zx + xx * (xo) + yx * (yo), zy + xy * (xo) + yy * (yo))
  
  if (m_scr_to_img.get_type () != Dufay)
    {
      /* Thames, Finlay and Paget screen are organized as follows:
	
	 G   R   G
	   B   B
	 R   G   R
	   B   B
	 G   R   G  */

      green = ((pixel (0,0) + pixel (0,1) + pixel (1,0) + pixel (1,1)) * 0.25 + pixel (0.5, 0.5)) * 0.5;
      red = (pixel (0.5, 0) + pixel (0, 0.5) + pixel (1, 0.5) + pixel (0.5, 1)) * 0.25;
      blue = (pixel (0.25, 0.25) + pixel (0.75, 0.25) + pixel (0.25, 0.75) + pixel (0.75, 0.75)) * 0.25;
    }
  else
    {
      /* Dufay screen is 
	 G   B   G

	 R   R   R

	 G   B   G  */
      green = (pixel (0,0) + pixel (0,1) + pixel (1,0) + pixel (1,1)) * 0.25;
      red = (pixel (0, 0.5) + pixel (0.33, 0.5) + pixel (0.66, 0.5) + pixel (1, 0.5)) * 0.25;
      blue = (pixel (0.5, 0) + pixel (0.5, 1)) * 0.5;
    }
#undef getpixel
  set_color (red, green, blue, r, g, b);
}
