#include "include/colorscreen.h"
#include "render-fast.h"

render_fast::render_fast (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval)
 : render_to_scr (param, img, params, dst_maxval)
{
}

pure_attr rgbdata
render_fast::sample_pixel (int x, int y, coord_t zx, coord_t zy)
{
  coord_t dx = x, dy = y;

/*  This is precise version:

       #define pixel(xo,yo) get_img_pixel_scr (dx + xo, dy + yo)

    In the following we assume that map is linear within single repetition
    of the screen.  This saves some matrix multiplication  */
  coord_t xx, xy;
  coord_t yx, yy;
  luminosity_t red, green, blue;
  //m_scr_to_img.to_img (dx, dy, &zx, &zy);
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
  return {red, green, blue};
}

/* Render preview for gtkgui. To be replaces by render_tile later.  */
bool
render_preview (image_data &scan, scr_to_img_parameters &param, render_parameters &rparams, unsigned char *pixels, int width, int height, int rowstride)
{
  render_fast render (param, scan, rparams, 255);
  render.compute_final_range ();
  int scr_xsize = render.get_final_width (), scr_ysize = render.get_final_height ();
  if (!render.precompute_all (NULL))
    return false;
  coord_t step = std::max (scr_xsize / (coord_t)width, scr_ysize / (coord_t)height);
#pragma omp parallel for default(none) shared(render,pixels,step,width,height,rowstride)
  for (int y = 0; y < height; y ++)
    for (int x = 0; x < width; x ++)
      {
	int red, green, blue;
	render.render_pixel_final (x * step, y * step, &red, &green, &blue);
	*(pixels + y * rowstride + x * 3) = red;
	*(pixels + y * rowstride + x * 3 + 1) = green;
	*(pixels + y * rowstride + x * 3 + 2) = blue;
      }
  return true;
}
