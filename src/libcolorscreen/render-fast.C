/* Fast rendering for previews.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "include/colorscreen.h"
#include "render-fast.h"

namespace colorscreen
{
/* Initialize renderer for PARAM, IMG, PARAMS and DST_MAXVAL.  */
render_fast::render_fast (const scr_to_img_parameters &param,
			  const image_data &img,
			  const render_parameters &params, int dst_maxval)
  : render_to_scr (param, img, params, dst_maxval)
{
}

/* Sample pixel at screen position P with image coordinates IMG_P.  */
pure_attr rgbdata
render_fast::sample_pixel (int_point_t p, point_t img_p) const
{
  luminosity_t red, green, blue;
  point_t z = img_p;
  point_t dx = m_scr_to_img.to_img ({(coord_t)(p.x + 1), (coord_t)p.y}) - z;
  point_t dy = m_scr_to_img.to_img ({(coord_t)p.x, (coord_t)(p.y + 1)}) - z;

#define pixel(xo,yo) get_img_pixel (z + dx * (coord_t)(xo) + dy * (coord_t)(yo))
  
  if (paget_like_screen_p (m_scr_to_img.get_type ()))
    {
      /* Thames, Finlay and Paget screen are organized as follows:
	
	 G   R   G
	   B   B
	 R   G   R
	   B   B
	 G   R   G  */

      green = ((pixel (0, 0) + pixel (0, 1) + pixel (1, 0) + pixel (1, 1))
	       * (coord_t)0.25 + pixel (0.5, 0.5)) * (coord_t)0.5;
      red = (pixel (0.5, 0) + pixel (0, 0.5) + pixel (1, 0.5) + pixel (0.5, 1))
	    * (coord_t)0.25;
      blue = (pixel (0.25, 0.25) + pixel (0.75, 0.25) + pixel (0.25, 0.75)
	      + pixel (0.75, 0.75)) * (coord_t)0.25;
    }
  else if (m_scr_to_img.get_type () == Dufay)
    {
      /* Dufay screen is 
	 G   B   G

	 R   R   R

	 G   B   G  */
      green = (pixel (0, 0) + pixel (0, 1) + pixel (1, 0) + pixel (1, 1))
	      * (coord_t)0.25;
      red = (pixel (0, 0.5) + pixel (1.0 / 3.0, 0.5) + pixel (2.0 / 3.0, 0.5)
	     + pixel (1, 0.5)) * (coord_t)0.25;
      blue = (pixel (0.5, 0) + pixel (0.5, 1)) * (coord_t)0.5;
    }
  else if (m_scr_to_img.get_type () == DioptichromeB)
    {
      /* DioptichromeB screen is 
	 B   B   B

	 G   G   G

	 B   B   B  */
      red = (pixel (0, 0) + pixel (0, 1) + pixel (1, 0) + pixel (1, 1))
	    * (coord_t)0.25;
      green = (pixel (0, 0.5) + pixel (1.0 / 3.0, 0.5) + pixel (2.0 / 3.0, 0.5)
	       + pixel (1, 0.5)) * (coord_t)0.25;
      blue = (pixel (0.5, 0) + pixel (0.5, 1)) * (coord_t)0.5;
    }
  else if (m_scr_to_img.get_type () == ImprovedDioptichromeB
	   || m_scr_to_img.get_type () == Omnicolore)
    {
      /* Dufay screen is 
	 G   R   G

	 B   B   B

	 G   R   G  */
      green = (pixel (0, 0) + pixel (0, 1) + pixel (1, 0) + pixel (1, 1))
	      * (coord_t)0.25;
      blue = (pixel (0, 0.5) + pixel (1.0 / 3.0, 0.5) + pixel (2.0 / 3.0, 0.5)
	      + pixel (1, 0.5)) * (coord_t)0.25;
      red = (pixel (0.5, 0) + pixel (0.5, 1)) * (coord_t)0.5;
    }
  else if (m_scr_to_img.get_type () == WarnerPowrie)
    {
      /* Warner Powrie screen is 
	 G B R   */
      green = pixel (0, 0);
      blue = pixel (1.0 / 3.0, 0);
      red = pixel (2.0 / 3.0, 0);
    }
  else if (m_scr_to_img.get_type () == Joly)
    {
      /* Joly screen is 
	 G R B   */
      green = pixel (0, 0);
      red = pixel (1.0 / 3.0, 0);
      blue = pixel (2.0 / 3.0, 0);
    }
  else
    __builtin_unreachable ();
#undef pixel
  return {red, green, blue};
}

/* Render final pixel at position P.  */
int_rgbdata
render_fast::render_pixel_final (point_t p) const
{
  point_t scr = m_scr_to_img.final_to_scr ({p.x - (coord_t)get_final_xshift (),
					    p.y - (coord_t)get_final_yshift ()});
  int_point_t isp = {nearest_int (scr.x), nearest_int (scr.y)};
  point_t pi = m_scr_to_img.to_img ({(coord_t)isp.x, (coord_t)isp.y});
  rgbdata d = sample_pixel (isp, pi);
  return out_color.final_color (d);
}

/* Render preview for GTKGUI.  To be replaced by render_tile later.
   SCAN is scanned image data, PARAM is screen mapping parameters,
   RPARAMS are rendering parameters. PIXELS is output buffer of size
   WIDTH x HEIGHT with ROWSTRIDE. Update PROGRESS and return true on success.  */
bool
render_preview (image_data &scan, const scr_to_img_parameters &param,
		const render_parameters &rparams, unsigned char *pixels,
		int width, int height, int rowstride, progress_info *progress)
{
  render_fast render (param, scan, rparams, 255);
  render.compute_final_range ();
  int scr_xsize = render.get_final_width (), scr_ysize = render.get_final_height ();
  if (!render.precompute_all (progress))
    return false;
  coord_t step = std::max (scr_xsize / (coord_t)width, scr_ysize / (coord_t)height);
  if (progress)
    progress->set_task ("rendering preview", height);
#pragma omp parallel for default(none) shared(render,pixels,step,width,height,rowstride,progress) collapse (2)
  for (int y = 0; y < height; y ++)
    for (int x = 0; x < width; x ++)
      {
	if (progress && progress->cancelled ())
	  continue;
	int_rgbdata out = render.render_pixel_final ({x * step, y * step});
	*(pixels + y * rowstride + x * 3) = out.red;
	*(pixels + y * rowstride + x * 3 + 1) = out.green;
	*(pixels + y * rowstride + x * 3 + 2) = out.blue;
      }
  return !progress || !progress->cancelled ();
}
}
