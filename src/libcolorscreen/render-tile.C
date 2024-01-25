#include <stdlib.h>
#include <functional>
#include <sys/time.h>
#include <mutex>
#include "pthread.h"
#include "include/colorscreen.h"
#include "include/render-fast.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "render-diff.h"

namespace {

static int stats = -1;
std::mutex global_rendering_lock;

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

/* Template for normal rendering, which calls render_pixel on every pixel.
   Main motivation to do rendering cores as templates is to get things nicely inlined.   */
template<typename T>
bool render_img_normal(render::render_type_parameters rtparam,
		       scr_to_img_parameters &param, image_data &img,
		       render_parameters &rparam,
		       unsigned char *pixels, int pixelbytes, int rowstride,
		       int width, int height,
		       double xoffset, double yoffset,
		       double step,
		       progress_info *progress)
{
  T render (param, img, rparam, 255);
  render.set_render_type (rtparam);
  if (progress)
    {
      progress->set_task ("precomputing", 1);
      progress->push ();
    }
  if (!render.precompute_img_range (xoffset * step, yoffset * step,
				    (width + xoffset) * step,
				    (height + yoffset) * step, progress))
    return false;
  if (progress)
    progress->pop ();
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
  return true;
}
template<typename T>
bool render_img_downscale(render::render_type_parameters rtparam,
			  scr_to_img_parameters &param, image_data &img,
			  render_parameters &rparam,
			  unsigned char *pixels, int pixelbytes, int rowstride,
			  int width, int height,
			  double xoffset, double yoffset,
			  double step,
			  progress_info *progress)
{
  T render (param, img, rparam, 255);
  render.set_render_type (rtparam);
  if (progress)
    {
      progress->set_task ("precomputing", 1);
      progress->push ();
    }
  if (!render.precompute_img_range (xoffset * step, yoffset * step,
				    (width + xoffset) * step,
				    (height + yoffset) * step, progress))
    return false;
  if (progress)
    progress->pop ();
  rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
  if (!data)
    return false;
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
  return true;
}

template<typename T> T*
init_render (render::render_type_parameters &rtparam, render_parameters &my_rparam, image_data &img, scr_to_img_parameters &param, progress_info *progress)
{
  T *r = new T (param, img, my_rparam, 255);
  r->set_render_type (rtparam);
  if (!r->precompute_all (progress))
    {
      delete r;
      r = NULL;
    }
  return r;
}

/* Render from stitched project.  We do not try todo antialiasing, since it would be slow.  */
template<typename T>
void render_stitched(render::render_type_parameters &rtparam,
		     image_data &img,
		     render_parameters &rparam,
    		     unsigned char *pixels, int pixelbytes, int rowstride,
		     int width, int height,
		     double xoffset, double yoffset,
		     double step,
		     bool do_antialias,
		     progress_info *progress)
{
  stitch_project &stitch = *img.stitch;
  int antialias = 1;

  if (do_antialias)
  {
    antialias = ceil (step / stitch.pixel_size);
    if (antialias > 4)
	    antialias = 4;
  }

  /* We initialize renderes to individual images on demand.  */
  assert (stitch.params.width * stitch.params.height < 256);
  //std::atomic<T *> renders[256];
  T * renders[256];
  pthread_mutex_t lock[256];
  for (int x = 0; x < stitch.params.width * stitch.params.height; x++)
  {
    renders[x] = NULL;
    if (pthread_mutex_init (&lock[x], NULL) != 0)
      perror ("lock");
  }
  //renders[0] = init_render (0, 0);

  int xmin = img.xmin, ymin = img.ymin;

#if 0
  for (int y = 0; y < stitch.params.height; y++)
    {
      for (int x = 0; x < stitch.params.width; x++)
	printf ("  %1.7f+%1.7f", rparam.get_tile_adjustment (&stitch, x, y).exposure, rparam.get_tile_adjustment (&stitch, x, y).dark_point);
      printf ("\n");
    }
#endif
  /* HACK: For some reason initializing renderers inside of the loop makes graydata to come out wrong.
     So initialize all renderers first.  */
  const bool hack = true;
  if (hack)
  {
    if (progress)
      progress->set_task ("checking visible tiles", height);
//#pragma omp parallel for default(none) shared(progress,renders,height, width,step,yoffset,xoffset,xmin,ymin,stitch,rparam)
    for (int y = 0; y < height; y++)
      {
	coord_t py = (y + yoffset) * step + ymin;
	if (!progress || !progress->cancel_requested ())
	  for (int x = 0; x < width; x++)
	    {
	      int ix, iy;
	      coord_t sx, sy;
	      stitch.common_scr_to_img.final_to_scr ((x + xoffset) * step + xmin, py, &sx, &sy);
	      if (stitch.tile_for_scr (&rparam, sx, sy, &ix, &iy, true))
		renders[iy * stitch.params.width + ix] = (T *)(size_t)1;
	    }
	if (progress)
	  progress->inc_progress ();
      }
    int n = 0;
    for (int y = 0; y < stitch.params.height; y++)
      for (int x = 0; x < stitch.params.width; x++)
	if (renders[y * stitch.params.width + x])
	  n++;
    if (progress)
      progress->set_task ("precomputing tiles", n);
    for (int iy = 0; iy < stitch.params.height; iy++)
      for (int ix = 0; ix < stitch.params.width; ix++)
	if (renders[iy * stitch.params.width + ix]
	    && (!progress || !progress->cancel_requested ()))
	  {
	    render_parameters rparam2 = rparam;
	    rparam.get_tile_adjustment (&stitch, ix, iy).apply (&rparam2);
	    if (progress)
	      progress->push ();
	    renders[iy * stitch.params.width + ix]  = init_render<T> (rtparam, rparam2, *img.stitch->images[iy][ix].img, img.stitch->images[iy][ix].param, progress);
	    if (progress)
	      {
		progress->pop ();
		progress->inc_progress ();
	      }
	  }
  }
  if (progress)
    progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(img,rtparam,rparam,progress,pixels,renders,pixelbytes,rowstride,height, width,step,yoffset,xoffset,xmin,ymin,stitch,lock,antialias)
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

	    int ix, iy;

	    /* If no tile was found, just render black pixel. */
	    if (!stitch.tile_for_scr (&rparam, sx, sy, &ix, &iy, true))
	      {
		putpixel (pixels, pixelbytes, rowstride, x, y, 0, 0, 0);
		continue;
	      }

	    /* If we are passing to new image, obtain new renderer.  */
	    if (ix != lastx || iy != lasty)
	      {
		/* Check if render is initialized.  */
		lastrender = renders[iy * stitch.params.width + ix];
		if (!hack && !lastrender)
		  {
		    if (hack && pthread_mutex_lock (&lock[iy * stitch.params.width + ix]) != 0)
		      perror ("lock");
		    {
		      /* Now we are in critical section, re-check to see if
		         someone initialized it in meantime.  */
		      lastrender = renders[iy * stitch.params.width + ix];
		      if (!lastrender)
			{
			  render_parameters rparam2 = rparam;
			  rparam.get_tile_adjustment (&stitch, ix, iy).apply (&rparam2);
			  if (progress)
			    progress->push ();
			  renders[iy * stitch.params.width + ix] = lastrender = init_render<T> (rtparam, rparam2, *img.stitch->images[iy][ix].img, img.stitch->images[iy][ix].param, progress);
			  if (progress)
			    progress->pop ();
			}
		    }
		    if (hack)
		      pthread_mutex_unlock (&lock[iy * stitch.params.width + ix]);
		    /* We took a time, check for cancelling.  */
		    if (progress && progress->cancel_requested ())
		      break;
		  }
		if (!lastrender)
		  {
		    lastx = -1;
		    putpixel (pixels, pixelbytes, rowstride, x, y, 0, 0, 0);
		    continue;
		  }
		lastx = ix;
		lasty = iy;
	      }
	    rgbdata d;
	    if (antialias == 1)
	      {
	        d = lastrender->sample_pixel_scr (sx - stitch.images[lasty][lastx].xpos, sy - stitch.images[lasty][lastx].ypos);
	      }
	    else
	      {
		coord_t substep = step / antialias;
		d = {0,0,0};
		for (int ax = 0; ax < antialias; ax++)
		  for (int ay = 0; ay < antialias; ay++)
		    d += lastrender->sample_pixel_scr (sx - stitch.images[lasty][lastx].xpos + ax * substep, sy - stitch.images[lasty][lastx].ypos + ay * substep);
		luminosity_t ainv = 1 / (luminosity_t)(antialias * antialias);
		d.red *= ainv;
		d.green *= ainv;
		d.blue *= ainv;
	      }
	    int r, g, b;
	    lastrender->set_color (d.red, d.green, d.blue, &r, &g, &b);
	    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	}
      if (progress)
	progress->inc_progress ();
    }
  for (int x = 0; x < stitch.params.width * stitch.params.height; x++)
    {
      T *r = renders[x];
      if (pthread_mutex_destroy (&lock[x]) != 0)
	perror ("lock_destroy");
      if (r && r != (T *)(size_t)1)
        delete r;
    }
}
/* Render from stitched project.  We do not try todo antialiasing, since it would be slow.  */
template<typename T>
bool do_render_tile(render::render_type_parameters &rtparam,
		    scr_to_img_parameters &param,
		    image_data &img,
		    render_parameters &rparam,
		    unsigned char *pixels, int pixelbytes, int rowstride,
		    int width, int height,
		    double xoffset, double yoffset,
		    double step,
		    progress_info *progress)
{
  if (img.stitch)
    {
      render_stitched<T> (rtparam, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, rtparam.antialias, progress);
      return true;
    }

  if (progress)
    progress->set_task ("rendering", height);
  if (step > 1 && rtparam.antialias)
    return render_img_downscale<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
  else
    return render_img_normal<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
}
}


bool
render_to_scr::render_tile (render_type_parameters rtparam,
			    scr_to_img_parameters &param, image_data &img,
			    render_parameters &rparam,
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
  bool ok = true;
  const bool lock_p = false;
  if (lock_p)
    global_rendering_lock.lock ();

  if (rtparam.color && !img.has_rgb ())
    rtparam.color = false;

  /* These rendering types requires RGB.  */
  if (!img.has_rgb ()
      && (rtparam.type == render_type_interpolated_original || rtparam.type == render_type_interpolated_profiled_original
	  || rtparam.type == render_type_interpolated_diff))
    rtparam.type = render_type_original;

  if (rtparam.type == render_type_fast
      || rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original
      || rtparam.type == render_type_interpolated)
    rtparam.antialias = false;

  if (progress)
    progress->set_task ("precomputing", 1);
  switch (rtparam.type)
    {
    case render_type_original:
    case render_type_profiled_original:
      {
	render_parameters my_rparam;
	my_rparam.original_render_from (rparam, rtparam.color, rtparam.type == render_type_profiled_original);

        if (!img.stitch && !rtparam.color && step > 1)
	  {
	    render_img render (param, img, my_rparam, 255);
	    render.set_render_type (rtparam);
	    if (!render.precompute_all (progress))
	      {
		if (lock_p)
		  global_rendering_lock.unlock ();
		return false;
	      }
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
	ok = do_render_tile<render_img> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      }
      break;
    case render_type_preview_grid:
    case render_type_realistic:
      ok = do_render_tile<render_superpose_img> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_interpolated_original:
    case render_type_interpolated_profiled_original:
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      {
	render_parameters my_rparam;

	if (rtparam.type == render_type_interpolated_original
	    || rtparam.type == render_type_interpolated_profiled_original)
	{
	  my_rparam.original_render_from (rparam, true, rtparam.type == render_type_interpolated_profiled_original);
	}
	else
	  my_rparam = rparam;
        ok = do_render_tile<render_interpolate> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      }
      break;
    case render_type_interpolated_diff:
      ok = do_render_tile<render_diff> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_fast:
      ok = do_render_tile<render_fast> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    default:
      abort ();
    }
  if (stats)
    {
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;

      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
      printf ("\nRender type:%i resolution:%ix%i time:%.3fs fps:%.3f", rtparam.type, width, height, time, 1/time);
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
  if (lock_p)
    global_rendering_lock.unlock ();
  return (!progress || !progress->cancelled ()) && ok;
}
