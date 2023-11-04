#include <stdlib.h>
#include <sys/time.h>
#include "include/colorscreen.h"
#include "include/render-fast.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"

static int stats = -1;

static inline void
putpixel (unsigned char *pixels, int pixelbytes, int rowstride, int x, int y,
       	  int r, int g, int b)
{
  *(pixels + y * rowstride + x * pixelbytes) = r;
  *(pixels + y * rowstride + x * pixelbytes + 1) = g;
  *(pixels + y * rowstride + x * pixelbytes + 2) = b;
  if (pixelbytes > 3)
    *(pixels + y * rowstride + x * pixelbytes + 3) = 255;
}

bool
render_to_scr::render_tile (enum render_type_t render_type,
			    scr_to_img_parameters &param, image_data &img,
			    render_parameters &rparam, bool color,
			    unsigned char *pixels, int pixelbytes, int rowstride,
			    int width, int height,
			    double xoffset, double yoffset,
			    double step,
			    progress_info *progress)
{
  if (width <= 0 || height <= 0)
    return true;
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  if (progress)
    progress->set_task ("precomputing", 1);
  if (!img.stitch)
    switch (render_type)
      {
      case render_type_original:
	{
	  render_img render (param, img, rparam, 255);
	  if (color)
	    render.set_color_display ();
	  if (!render.precompute_all (progress))
	    return false;

	  if (!color && step > 1)
	    {
	      luminosity_t *data = (luminosity_t *)malloc (sizeof (luminosity_t) * width * height);
	      render.get_gray_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	      if (progress)
		progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	      for (int y = 0; y < height; y++)
		{
		  if (!progress || !progress->cancel_requested ())
		    for (int x = 0; x < width; x++)
		      {
			int r, g, b;
			render.set_color (data[x + width * y], data[x + width * y], data[x + width * y], &r, &g, &b);
			putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		      }
		  if (progress)
		    progress->inc_progress ();
		}
	      free (data);
	      break;
	    }
	  if (step > 1)
	    {
	      rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	      render.get_color_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	      if (progress)
		progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	      for (int y = 0; y < height; y++)
		{
		  if (!progress || !progress->cancel_requested ())
		    for (int x = 0; x < width; x++)
		      {
			int r, g, b;
			render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
			putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		      }
		  if (progress)
		    progress->inc_progress ();
		}
	      free (data);
	      break;
	    }

	  if (progress)
	    progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	  for (int y = 0; y < height; y++)
	    {
	      coord_t py = (y + yoffset) * step;
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					     &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	       if (progress)
		 progress->inc_progress ();
	    }
	}
	break;
      case render_type_preview_grid:
	{
	  render_superpose_img render (param, img,
				       rparam, 255, true);
	  if (color)
	    render.set_color_display ();
	  if (!render.precompute_all (progress))
	    return false;
	  if (step > 1)
	    {
	      rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	      render.get_color_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	      if (progress)
		progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	      for (int y = 0; y < height; y++)
		{
		  if (!progress || !progress->cancel_requested ())
		    for (int x = 0; x < width; x++)
		      {
			int r, g, b;
			render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
			putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		      }
		   if (progress)
		     progress->inc_progress ();
		}
	      free (data);
	      break;
	    }

	   if (progress)
	     progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	  for (int y = 0; y < height; y++)
	    {
	      coord_t py = (y + yoffset) * step;
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					     &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	       if (progress)
		 progress->inc_progress ();
	    }
	}
	break;
      case render_type_realistic:
	{
	  render_superpose_img render (param, img,
				       rparam, 255, false);
	  if (color)
	    render.set_color_display ();
	  if (!render.precompute_all (progress))
	    return false;
	  if (step > 1)
	    {
	      rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	      render.get_color_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	      if (progress)
		progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	      for (int y = 0; y < height; y++)
		{
		  if (!progress || !progress->cancel_requested ())
		    for (int x = 0; x < width; x++)
		      {
			int r, g, b;
			render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
			putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		      }
		   if (progress)
		     progress->inc_progress ();
		}
	      free (data);
	      break;
	    }

	   if (progress)
	     progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	  for (int y = 0; y < height; y++)
	    {
	      coord_t py = (y + yoffset) * step;
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.render_pixel_img_antialias ((x + xoffset) * step, py,
						       1 * step, 8, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	       if (progress)
		 progress->inc_progress ();
	    }
	}
	break;
      case render_type_interpolated:
      case render_type_combined:
      case render_type_predictive:
	{
	  bool adjust_luminosity = (render_type == render_type_combined);
	  bool screen_compensation = (render_type == render_type_predictive);
	  render_interpolate render (param, img,
				     rparam, 255, screen_compensation, adjust_luminosity);
	  if (!render.precompute_img_range (xoffset * step, yoffset * step,
					    (width + xoffset) * step,
					    (height + yoffset) * step, progress))
	    return false;

	  if (progress)
	    progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	  for (int y = 0; y < height; y++)
	    {
	      coord_t py = (y + yoffset) * step;
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;

		    render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					     &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	       if (progress)
		 progress->inc_progress ();
	    }
	}
	break;
      case render_type_fast:
	{
	  render_fast render (param, img, rparam, 255);
	  if (!render.precompute_all (progress))
	    return false;

	  if (progress)
	    progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	  for (int y = 0; y < height; y++)
	    {
	      coord_t py = (y + yoffset) * step;
	      if (!progress || !progress->cancel_requested ())
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;

		    render.render_pixel_img ((x + xoffset) * step, py, &r, &g,
					     &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	       if (progress)
		 progress->inc_progress ();
	    }
	}
      }
  else
    {
      if (progress)
	progress->set_task ("rendering", height);
      pthread_mutex_lock (&img.stitch->lock);
      for (int y = 0; y < height; y++)
	{
	  coord_t py = (y + yoffset) * step + img.ymin;
	  img.stitch->set_render_param (rparam);
	  enum stitch_image::render_mode mode2 = stitch_image::render_demosaiced;
	  if (render_type == render_type_fast)
	    mode2 = stitch_image::render_fast_stitch;
	  if (render_type == render_type_original)
	    mode2 = stitch_image::render_original;
	  if (render_type == render_type_predictive)
	    mode2 = stitch_image::render_predictive;
	  if (!progress || !progress->cancel_requested ())
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		coord_t sx, sy;
		int ix = 0,iy;
		img.stitch->common_scr_to_img.final_to_scr ((x + xoffset) * step + img.xmin, py, &sx, &sy);
		for (iy = 0 ; iy < img.stitch->params.height; iy++)
		  {
		    for (ix = 0 ; ix < img.stitch->params.width; ix++)
		      if (/*img.stitch->images[iy][ix].analyzed &&*/ img.stitch->images[iy][ix].pixel_known_p (sx, sy))
			break;
		    if (ix != img.stitch->params.width)
		      break;
		  }

		if (iy != img.stitch->params.height)
		  {
		    img.stitch->images[iy][ix].render_pixel (255, sx, sy, mode2, &r, &g, &b, progress);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
		else
		  putpixel (pixels, pixelbytes, rowstride, x, y, 0, 0, 0);
	      }
	   if (progress)
	     {
	       progress->set_task ("rendering", height);
	       progress->set_progress (y);
	     }
	}
      pthread_mutex_unlock (&img.stitch->lock);
    }
  if (stats)
    {
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;

      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
      printf ("\nRender type:%i resolution:%ix%i time:%.3fs fps:%.3f", render_type, width, height, time, 1/time);
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
  return !progress || !progress->cancelled ();
}
