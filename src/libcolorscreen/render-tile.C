#include <stdlib.h>
#include <sys/time.h>
#include "include/colorscreen.h"
#include "include/render-fast.h"

static int stats = -1;

static inline void
putpixel (unsigned char *pixels, int pixelbytes, int rowstride, int x, int y,
       	  int r, int g, int b)
{
  *(pixels + y * rowstride + x * pixelbytes) = r;
  *(pixels + y * rowstride + x * pixelbytes + 1) = g;
  *(pixels + y * rowstride + x * pixelbytes + 2) = b;
  *(pixels + y * rowstride + x * pixelbytes + 3) = 255;
}

void
render_to_scr::render_tile (enum render_type_t render_type,
			    scr_to_img_parameters &param, image_data &img,
			    render_parameters &rparam, bool color,
			    unsigned char *pixels, int pixelbytes, int rowstride,
			    int width, int height,
			    double xoffset, double yoffset,
			    double step)
{
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  switch (render_type)
    {
    case render_type_original:
      {
	render_img render (param, img, rparam, 255);
	if (color)
	  render.set_color_display ();
	render.precompute_all ();

	if (!color && step > 1)
	  {
	    luminosity_t *data = (luminosity_t *)malloc (sizeof (luminosity_t) * width * height);
	    render.get_gray_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y], data[x + width * y], data[x + width * y], &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_color_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
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
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_color_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
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
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_color_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img_antialias ((x + xoffset) * step, py,
						   1 * step, 8, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
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

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;

		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_fast:
      {
	render_fast render (param, img, rparam, 255);
	render.precompute_all ();

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;

		render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					 &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
    }
  if (stats)
    {
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;

      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
      printf ("Render type:%i resolution:%ix%i time:%.3fs fps:%.3f", render_type, width, height, time, 1/time);
      if (prev_time_set)
	{
	  double time2 = end_time.tv_sec + end_time.tv_usec/1000000.0 - prev_time.tv_sec - prev_time.tv_usec/1000000.0;
          printf (" last run %.3f ago overall fps:%.3f\n", time2, 1/(time2+time));
	}
      else
        printf ("\n");
      prev_time = end_time;
      prev_time_set = true;
      fflush (stdout);
    }
}
