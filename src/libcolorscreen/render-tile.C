#include <stdlib.h>
#include <functional>
#include <sys/time.h>
#include "pthread.h"
#include "include/colorscreen.h"
#include "include/render-fast.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"

namespace {

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

/* Render from stitched project.  We do not try todo antialiasing, since it would be slow.  */
template<typename T>
void render_stitched(std::function<T *(int x, int y)> init_render, image_data &img,
    		     unsigned char *pixels, int pixelbytes, int rowstride,
		     int width, int height,
		     double xoffset, double yoffset,
		     double step,
		     progress_info *progress)
{
  stitch_project &stitch = *img.stitch;

  /* We initialize renderes to individual images on demand.  */
  assert (stitch.params.width * stitch.params.height < 256);
  std::atomic<T *> renders[256];
  pthread_mutex_t lock[256];
  for (int x = 0; x < stitch.params.width * stitch.params.height; x++)
  {
    renders[x] = NULL;
    pthread_mutex_init (&lock[x], NULL);
  }

  if (progress)
    progress->set_task ("rendering", height);
  int xmin = img.xmin, ymin = img.ymin;
#pragma omp parallel for default(none) shared(progress,pixels,renders,pixelbytes,rowstride,height, width,step,yoffset,xoffset,xmin,ymin,stitch,init_render,lock)
  for (int y = 0; y < height; y++)
    {
      /* Try to use same renderer as for last tile to avoid accessing atomic pointer.  */
      int lastx = -1, lasty = 0;
      coord_t py = (y + yoffset) * step + ymin;
      T *lastrender = NULL;

      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < width; x++)
	  {
	    coord_t sx, sy;
	    stitch.common_scr_to_img.final_to_scr ((x + xoffset) * step + xmin, py, &sx, &sy);

	    int ix = 0, iy;
	    /* Lookup tile to use. */
	    for (iy = 0 ; iy < stitch.params.height; iy++)
	      {
		for (ix = 0 ; ix < stitch.params.width; ix++)
		  if (stitch.images[iy][ix].img
		      && stitch.images[iy][ix].pixel_known_p (sx, sy))
		    break;
		if (ix != stitch.params.width)
		  break;
	      }

	    /* If no tile was found, just render black pixel. */
	    if (iy == stitch.params.height)
	      {
		putpixel (pixels, pixelbytes, rowstride, x, y, 0, 0, 0);
		continue;
	      }

	    /* If we are passing to new image, obtain new renderer.  */
	    if (ix != lastx || iy != lasty)
	      {
		lastx = ix;
		lasty = iy;

		/* Check if render is initialized.  */
		lastrender = renders[iy * stitch.params.width + ix];
		if (!lastrender)
		  {
		    pthread_mutex_lock (&lock[iy * stitch.params.width + ix]);
		    {
		      /* Now we are in critical section, re-check to see if
		         someone initialized it in meantime.  */
		      lastrender = renders[iy * stitch.params.width + ix];
		      if (!lastrender)
			renders[iy * stitch.params.width + ix] = lastrender = init_render (ix, iy);
		    }
		    pthread_mutex_unlock (&lock[iy * stitch.params.width + ix]);
		    /* We took a time, check for cancelling.  */
		    if (!lastrender
			|| (progress && progress->cancel_requested ()))
		      break;
		  }
	      }
	    int r, g, b;
	    lastrender->render_pixel_scr (sx - stitch.images[lasty][lastx].xpos, sy - stitch.images[lasty][lastx].ypos, &r, &g, &b);
	    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	}
      if (progress)
	progress->inc_progress ();
    }
  for (int x = 0; x < stitch.params.width * stitch.params.height; x++)
    delete renders[x];
}

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
  if (!img.stitch ||1)
    switch (render_type)
      {
      case render_type_original:
	{
	  if (img.stitch)
	    {
	      render_stitched<render_img> (
		  [&param,&img,&rparam,color,&progress] (int x, int y) mutable
		  {
		    render_img *r = new render_img (img.stitch->images[y][x].param, *img.stitch->images[y][x].img, rparam, 255);
		    if (!r->precompute_all (progress))
		      {
		        delete r;
			r = NULL;
		      }
		    return r;
		  },
		  img, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
	      break;
	    }
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
	  if (img.stitch)
	    {
	      render_stitched<render_superpose_img> (
		  [&param,&img,&rparam,color,&progress] (int x, int y) mutable
		  {
		    render_superpose_img *r = new render_superpose_img (img.stitch->images[y][x].param, *img.stitch->images[y][x].img, rparam, 255, true);
		    if (!r->precompute_all (progress))
		      {
		        delete r;
			r = NULL;
		      }
		    return r;
		  },
		  img, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
	      break;
	    }
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
	  if (img.stitch)
	    {
	      render_stitched<render_superpose_img> (
		  [&param,&img,&rparam,color,&progress] (int x, int y) mutable
		  {
		    render_superpose_img *r = new render_superpose_img (img.stitch->images[y][x].param, *img.stitch->images[y][x].img, rparam, 255, false);
		    if (!r->precompute_all (progress))
		      {
		        delete r;
			r = NULL;
		      }
		    return r;
		  },
		  img, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
	      break;
	    }
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
	  if (img.stitch)
	    {
	      render_stitched<render_interpolate> (
		  [&param,&img,&rparam,screen_compensation,adjust_luminosity,&progress] (int x, int y) mutable
		  {
		    render_interpolate *r = new render_interpolate (img.stitch->images[y][x].param, *img.stitch->images[y][x].img, rparam, 255,screen_compensation, adjust_luminosity);
		    if (!r->precompute_all (progress))
		      {
		        delete r;
			r = NULL;
		      }
		    return r;
		  },
		  img, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
	      break;
	    }
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
	  if (img.stitch)
	    break;
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
