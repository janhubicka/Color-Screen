#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <mutex>
#include "include/colorscreen.h"
#include "include/stitch.h"
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
template<typename T, typename P, typename RP>
bool render_img_normal(render::render_type_parameters rtparam,
		       P &param, image_data &img,
		       RP &rparam,
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
template<typename T, typename P,typename RP>
bool render_img_downscale(render::render_type_parameters rtparam,
			  P &param, image_data &img,
			  RP &rparam,
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
template<typename T>
bool render_img_gray_downscale(render::render_type_parameters rtparam,
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
  luminosity_t *data = (luminosity_t *)malloc (sizeof (luminosity_t) * width * height);
  if (!data)
    return false;
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
  return true;
}

template<typename T,typename P,typename RP> T*
init_render_scr (RP &rtparam, render_parameters &my_rparam, image_data &img, scr_to_img_parameters &param, P &oparam, progress_info *progress)
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
template<typename T,typename P,typename RP> T*
init_render_img (RP &rtparam, render_parameters &my_rparam, image_data &img, scr_to_img_parameters &param, P &oparam, progress_info *progress)
{
  T *r = new T (oparam, img, my_rparam, 255);
  r->set_render_type (rtparam);
  if (!r->precompute_all (progress))
    {
      delete r;
      r = NULL;
    }
  return r;
}

/* Sample pixel on screen coordinates X and Y of size STEP with ANTIALIAS.
   This is used for render engines which does their own translation of scr to img coordinates
   (inherited from render_scr)  */
template<typename T> inline rgbdata
render_loop_scr (T &render, scr_to_img &, int antialias, coord_t x, coord_t y, coord_t step)
{
  rgbdata d;
  if (antialias == 1)
    d = render.sample_pixel_scr (x, y);
  else
    {
      coord_t substep = step / antialias;
      d = {0,0,0};
      for (int ax = 0; ax < antialias; ax++)
	for (int ay = 0; ay < antialias; ay++)
	  d += render.sample_pixel_scr (x + ax * substep, y + ay * substep);
      luminosity_t ainv = 1 / (luminosity_t)(antialias * antialias);
      d.red *= ainv;
      d.green *= ainv;
      d.blue *= ainv;
    }
  return d;
}

/* Same but for render engines which handle only image coordinates.  */
template<typename T> inline rgbdata
render_loop_img (T &render, scr_to_img &map, int antialias, coord_t x, coord_t y, coord_t step)
{
  rgbdata d;
  if (antialias == 1)
    {
      coord_t xx, yy;
      map.to_img (x, y, &xx, &yy);
      d = render.sample_pixel_img (xx, yy);
    }
  else
    {
      coord_t substep = step / antialias;
      d = {0,0,0};
      for (int ax = 0; ax < antialias; ax++)
	for (int ay = 0; ay < antialias; ay++)
	  {
	    coord_t xx, yy;
	    map.to_img (x + ax * substep, y + ay * substep, &xx, &yy);
	    d += render.sample_pixel_img (xx, yy);
	  }
      luminosity_t ainv = 1 / (luminosity_t)(antialias * antialias);
      d.red *= ainv;
      d.green *= ainv;
      d.blue *= ainv;
    }
  return d;
}


/* Render from stitched project.  We do not try todo antialiasing, since it would be slow.  */
template<typename T, typename P,typename RP,
	 /* Function to render pixel at a given coordinates and with a given anti-aliasing.  */
       	 rgbdata (render_loop) (T &, scr_to_img &, int, coord_t, coord_t, coord_t),
	 /* Function to initialize renderer on demand.  */
       	 T *(init_render) (RP &, render_parameters &, image_data &, scr_to_img_parameters &, P &, progress_info *progress)>
void render_stitched(RP &rtparam, P &outer_param,
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
	    renders[iy * stitch.params.width + ix]  = init_render (rtparam, rparam2, *img.stitch->images[iy][ix].img, img.stitch->images[iy][ix].param, outer_param, progress);
	    if (progress)
	      {
		progress->pop ();
		progress->inc_progress ();
	      }
	  }
  }
  if (progress)
    progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(img,rtparam,rparam,progress,pixels,renders,pixelbytes,rowstride,height, width,step,yoffset,xoffset,xmin,ymin,stitch,lock,antialias,outer_param)
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
			  renders[iy * stitch.params.width + ix] = lastrender = init_render (rtparam, rparam2, *img.stitch->images[iy][ix].img, img.stitch->images[iy][ix].param, outer_param, progress);
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
	  rgbdata d = render_loop (*lastrender, stitch.images[lasty][lastx].scr_to_img_map, antialias, sx - stitch.images[lasty][lastx].xpos, sy - stitch.images[lasty][lastx].ypos, step);
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

/* Main entry to rendering.   */
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
      render_stitched<T,scr_to_img_parameters,render::render_type_parameters,render_loop_scr,init_render_scr<T,scr_to_img_parameters,render::render_type_parameters>> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, rtparam.antialias, progress);
      return true;
    }

  if (progress)
    progress->set_task ("rendering", height);
  if (step > 1 && rtparam.antialias)
    return render_img_downscale<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
  else
    return render_img_normal<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
}
/* Main entry to rendering if graydata needs to be handled specially.   */
template<typename T>
bool do_render_tile_with_gray(render::render_type_parameters &rtparam,
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
      render_stitched<T,scr_to_img_parameters,render::render_type_parameters,render_loop_scr,init_render_scr<T,scr_to_img_parameters,render::render_type_parameters>> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, rtparam.antialias, progress);
      return true;
    }

  if (progress)
    progress->set_task ("rendering", height);
  if (step > 1 && rtparam.antialias)
    {
      if (!rtparam.color)
        return render_img_gray_downscale<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      else
        return render_img_downscale<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
    }
  else
    return render_img_normal<T> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
}
template<typename T>
bool do_render_tile_img(render::render_type_parameters &rtparam,
			scr_detect_parameters &param,
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
      render_stitched<T,scr_detect_parameters,render::render_type_parameters,render_loop_img,init_render_img<T,scr_detect_parameters,render::render_type_parameters>> (rtparam, param, img, rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, rtparam.antialias, progress);
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
