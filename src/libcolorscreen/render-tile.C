#include "include/colorscreen.h"
#include "include/render-fast.h"

static inline void
putpixel (unsigned char *pixels, int rowstride, int x, int y,
       	  int r, int g, int b)
{
  *(pixels + y * rowstride + x * 4) = r;
  *(pixels + y * rowstride + x * 4 + 1) = g;
  *(pixels + y * rowstride + x * 4 + 2) = b;
}

void
render::render_tile (enum render_type_t render_type,
		     scr_to_img_parameters &param, image_data &img,
		     render_parameters &rparam, bool color,
		     unsigned char *pixels, int rowstride,
		     int width, int height,
		     double xoffset, double yoffset,
		     double step)
{
  switch (render_type)
    {
    case render_type_original:
      {
	render_img render (param, img, rparam, 255);
	if (color)
	  render.set_color_display ();
	render.precompute_all ();

#pragma omp parallel for default(none) shared(render,pixels,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_preview_grid:
      {
	render_superpose_img render (param, img,
				     rparam, 255, false, true);
	if (color)
	  render.set_color_display ();
	render.precompute_all ();

#pragma omp parallel for default(none) shared(render,pixels,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_realistic:
      {
	render_superpose_img render (param, img,
				     rparam, 255, false, false);
	if (color)
	  render.set_color_display ();
	render.precompute_all ();

#pragma omp parallel for default(none) shared(render,pixels,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img_antialias ((x + xoffset) * step, py,
						   1 * step, 8, &r, &g, &b);
		putpixel (pixels, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      {
	rparam.adjust_luminosity = (render_type == render_type_combined);
	rparam.screen_compensation = (render_type == render_type_predictive);
	render_interpolate render (param, img,
				   rparam, 255);
	render.precompute_img_range (xoffset * step, yoffset * step,
				     (width + xoffset) * step,
				     (height + yoffset) * step);

#pragma omp parallel for default(none) shared(render,pixels,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;

		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_fast:
      {
	render_fast render (param, img, rparam, 255);
	render.precompute_all ();

#pragma omp parallel for default(none) shared(render,pixels,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;

		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, rowstride, x, y, r, g, b);
	      }
	  }
      }
    }
}
