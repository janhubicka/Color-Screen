#include <stdlib.h>
#include <sys/time.h>
#include "include/tiff-writer.h"
#include "include/colorscreen.h"
#include "include/render-fast.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "icc-srgb.h"
namespace {
/* Utilities to report time eneeded for a given operation.
  
   Time measurement started.
 
   TODO: This is leftover of colorscreen utility and should be integrated
   with progress_info and removed. */
static struct timeval start_time;

/* Start measurement.  */
static void
record_time ()
{
  gettimeofday (&start_time, NULL);
}

/* Finish measurement and output time.  */
static void
print_time ()
{
  struct timeval end_time;
  gettimeofday (&end_time, NULL);
  double time =
    end_time.tv_sec + end_time.tv_usec / 1000000.0 - start_time.tv_sec -
    start_time.tv_usec / 1000000.0;
  printf ("\n  ... done in %.3fs\n", time);
}

static void
print_progress (int p, int max)
{
  int percent = (p * 100 + max / 2) / max;
  int pastpercent = ((p - 1) * 100 + max / 2) / max;
  if (pastpercent != percent)
    {
      printf ("[3D%2i%%", percent);
      fflush (stdout);
    }
}

template<typename T, rgbdata (T::*sample_data)(coord_t x, coord_t y), rgbdata (T::*sample_scr_data)(coord_t x, coord_t y), bool support_tile>
const char *
produce_file (render_to_file_params p, T &render, progress_info *progress)
{
  const char *error = NULL;
  tiff_writer_params tp;
  tp.filename = p.filename;
  tp.width = p.width;
  tp.height = p.height;
  tp.hdr = p.hdr;
  tp.depth = p.depth;
  tp.icc_profile = p.icc_profile;
  tp.icc_profile_len = p.icc_profile_len;
  tp.tile = p.tile;
  if (p.tile)
    {
      if (!support_tile)
         abort ();
      tp.xoffset = p.xoffset;
      tp.yoffset = p.yoffset;
      tp.alpha = true;
    }
  if (progress)
    progress->set_task ("Opening tiff file", 1);
  tiff_writer out(tp, &error);
  if (error)
    return error;
  if (p.verbose)
    {
      if (progress)
        progress->pause_stdout ();
      printf ("Rendering %s in resolution %ix%i: 00%%", p.filename, p.width,
	      p.height);
      fflush (stdout);
      record_time ();
      if (progress)
        progress->resume_stdout ();
    }
  if (progress)
    progress->set_task ("Rendering and saving", p.height);
  for (int y = 0; y < p.height; y++)
    {
      if (p.antialias == 1)
	{
	  if (p.tile && support_tile)
#pragma omp parallel for default(none) shared(p,render,y,out)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		p.common_map->final_to_scr (xx, yy, &xx, &yy);
		if (!p.pixel_known_p (p.pixel_known_p_data, xx, yy))
		  {
		    if (!p.hdr)
		      out.kill_pixel (x);
		    else
		      out.kill_hdr_pixel (x);
		  }
		else
		  {
		    xx -= p.xpos;
		    yy -= p.ypos;
		    rgbdata d = (render.*sample_scr_data) (xx, yy);
		    if (!p.hdr)
		      {
			int rr, gg, bb;
			render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_pixel (x, rr, gg, bb);
		      }
		    else
		      {
			luminosity_t rr, gg, bb;
			render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_hdr_pixel (x, rr, gg, bb);
		      }
		  }
	      }
	  else
#pragma omp parallel for default(none) shared(p,render,y,out)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		rgbdata d = (render.*sample_data) (xx, yy);
		if (!p.hdr)
		  {
		    int rr, gg, bb;
		    render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_pixel (x, rr, gg, bb);
		  }
		else
		  {
		    luminosity_t rr, gg, bb;
		    render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_hdr_pixel (x, rr, gg, bb);
		  }
	      }
	}
      else
	{
	  coord_t asx = p.xstep / p.antialias;
	  coord_t asy = p.ystep / p.antialias;
	  luminosity_t sc = 1.0 / (p.antialias * p.antialias);
	  if (p.tile && support_tile)
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		coord_t xx2, yy2;
		p.common_map->final_to_scr (xx, yy, &xx2, &yy2);
		if (!p.pixel_known_p (p.pixel_known_p_data, xx2, yy2))
		  {
		    if (!p.hdr)
		      out.kill_pixel (x);
		    else
		      out.kill_hdr_pixel (x);
		  }
		else
		  {
		    rgbdata d = {0, 0, 0};
		    for (int ay = 0 ; ay < p.antialias; ay++)
		      for (int ax = 0 ; ax < p.antialias; ax++)
			{
			  p.common_map->final_to_scr (xx + ax * asx, yy + ay * asy, &xx2, &yy2);
			  xx2 -= p.xpos;
			  yy2 -= p.ypos;
			  d += (render.*sample_scr_data) (xx2 + ax * asx, yy2 + ay * asy);
			}
		    d.red *= sc;
		    d.green *= sc;
		    d.blue *= sc;
		    if (!p.hdr)
		      {
			int rr, gg, bb;
			render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_pixel (x, rr, gg, bb);
		      }
		    else
		      {
			luminosity_t rr, gg, bb;
			render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_hdr_pixel (x, rr, gg, bb);
		      }
		  }
	      }
	  else
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc)
	    for (int x = 0; x < p.width; x++)
	      {
		rgbdata d = {0, 0, 0};
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		for (int ay = 0 ; ay < p.antialias; ay++)
		  for (int ax = 0 ; ax < p.antialias; ax++)
		    d += (render.*sample_data) (xx + ax * asx, yy + ay * asy);
		d.red *= sc;
		d.green *= sc;
		d.blue *= sc;
		if (!p.hdr)
		  {
		    int rr, gg, bb;
		    render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_pixel (x, rr, gg, bb);
		  }
		else
		  {
		    luminosity_t rr, gg, bb;
		    render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_hdr_pixel (x, rr, gg, bb);
		  }
	      }
	  }
      if (!out.write_row ())
	return "Write error";
      if (progress && progress->cancel_requested ())
	return "Cancelled";
      if (progress)
	progress->inc_progress ();
      if (p.verbose)
	print_progress (y, p.height);
    }
  return NULL;
}

}

bool
get_rendered_file_dimensions (scr_to_img_parameters & param, image_data &scan, render_to_file_params *p)
{
  render_parameters rparam;
  switch (p->mode)
    {
    case none:
    case corrected_color:
    case interpolated:
    case predictive:
    case combined:
    case realistic:
    case preview_grid:
    case color_preview_grid:
      {
	render_img render (param, scan, rparam, 65535);
	coord_t render_width = render.get_final_width ();
	coord_t render_height = render.get_final_height ();
	if (!p->pixel_size)
	  p->pixel_size = render.pixel_size ();
	if (p->mode == interpolated)
	  p->pixel_size = param.type == Dufay ? 0.5 : 0.25;
	if (!p->xstep)
	  {
	    /* For interpolated the pixels are rotated, so it makes
	       sense to enlarge image somewhat.  */
	    p->xstep = p->pixel_size / p->scale * 0.5;
	    p->ystep = p->pixel_size / p->scale * 0.5;
	  }
	if (!p->antialias)
	  {
	    p->antialias = round (1 * p->xstep / p->pixel_size);
	    if (p->mode == predictive || p->mode == realistic)
	      p->antialias = round (4 * p->xstep / p->pixel_size);
	    if (!p->antialias)
	      p->antialias = 1;
	  }
	if (!p->width)
	  {
	    p->width = render_width / p->xstep;
	    p->height = render_height / p->ystep;
	  }
      }
      break;
    case detect_realistic:
    case detect_adjusted:
    case detect_nearest:
    case detect_nearest_scaled:
    case detect_relax:
      {
	if (!p->xstep)
	  p->xstep = p->ystep = 1 / p->scale;
	if (!p->width)
	  {
	    p->width = scan.width / p->xstep;
	    p->height = scan.height / p->ystep;
	  }
	if (!p->antialias)
	  {
	    if (p->mode == detect_realistic || p->mode == detect_adjusted)
	      p->antialias = round (4 * p->xstep);
	    else
	      p->antialias = round (1 * p->ystep);
	    if (!p->antialias)
	      p->antialias = 1;
	  }
      }
      break;
    }
  return true;
}

/* Render image to TIFF file OUTFNAME.  */
bool
render_to_file (image_data & scan, scr_to_img_parameters & param,
		scr_detect_parameters & dparam, render_parameters rparam /* modified */,
		struct render_to_file_params rfparams /* modified */,
		progress_info * progress, const char **error)
{
  if (rfparams.verbose)
    {
      printf ("Precomputing...");
      fflush (stdout);
      record_time ();
    }
  void *icc_profile /*= sRGB_icc*/ = NULL;
  size_t icc_profile_len = /*sRGB_icc_len =*/ 0;

  if ((int)rfparams.mode >= (int)detect_adjusted && !scan.rgbdata)
    {
      fprintf (stderr, "Screen detection is imposible in monochromatic scan\n");
      exit (1);
    }
  if ((int)rfparams.mode == (int)corrected_color && !scan.rgbdata)
    {
      fprintf (stderr, "Corrected color is imposible in monochromatic scan\n");
      exit (1);
    }
  if ((int)rfparams.mode == (int)color_preview_grid && !scan.rgbdata)
    {
      fprintf (stderr, "Color preview grid is imposible in monochromatic scan\n");
      exit (1);
    }
  if (rfparams.hdr)
    {
      rparam.output_gamma = 1;
      rparam.output_profile = render_parameters::output_profile_original;
    }

  if (rparam.output_profile == render_parameters::output_profile_original)
    icc_profile_len = rparam.get_icc_profile (&icc_profile);

  /* Initialize rendering engine.  */
  if (!get_rendered_file_dimensions (param, scan, &rfparams))
    {
      *error = "Precomputation failed (out of memory)";
      return false;
    }
  bool icc_profile_set = rfparams.icc_profile;
  if (!icc_profile_set)
    {
      rfparams.icc_profile = icc_profile;
      rfparams.icc_profile_len = icc_profile_len;
    }
  if (progress)
    progress->set_task ("precomputing", 1);

  switch (rfparams.mode)
    {
    case none:
    case corrected_color:
      {
	render_img render (param, scan, rparam, 65535);
	if (rfparams.mode == corrected_color)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();

	if (!icc_profile_set)
	  {
	    rfparams.icc_profile = scan.icc_profile;
	    rfparams.icc_profile_len = scan.icc_profile_size;
	  }
	// TODO: For HDR output we want to linearize the ICC profile.
	*error = produce_file<render_img, &render_img::sample_pixel_final, &render_img::sample_pixel_scr, true> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case interpolated:
    case predictive:
    case combined:
      {
	bool screen_compensation = rfparams.mode == predictive;
	bool adjust_luminosity = rfparams.mode == combined;

	render_interpolate render (param, scan, rparam, 65535,
				   screen_compensation, adjust_luminosity);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();

	*error = produce_file<render_interpolate, &render_interpolate::sample_pixel_final, &render_interpolate::sample_pixel_scr, true> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case realistic:
    case preview_grid:
    case color_preview_grid:
      {
	render_superpose_img render (param, scan, rparam, 65535, rfparams.mode != realistic);
	if (rfparams.mode == color_preview_grid && scan.rgbdata)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	/* TODO: Maybe preview_grid and color_preview_grid
	   should be in the scan profile.  */
	*error = produce_file<render_superpose_img, &render_superpose_img::sample_pixel_final, &render_superpose_img::sample_pixel_scr, true> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case detect_realistic:
      {
	render_scr_detect_superpose_img render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	*error = produce_file<render_scr_detect_superpose_img, &render_scr_detect_superpose_img::sample_pixel_img, &render_scr_detect_superpose_img::sample_pixel_img, false> (rfparams, render, progress);
	if (*error)
	  return false;
	break;
      }
    case detect_adjusted:
      {
	render_scr_detect render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (false, progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	*error = produce_file<render_scr_detect, &render_scr_detect::get_adjusted_pixel, &render_scr_detect::get_adjusted_pixel, false> (rfparams, render, progress);
	if (*error)
	  return false;
	break;
      }
    case detect_nearest:
      {
	render_scr_nearest render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	*error = produce_file<render_scr_nearest, &render_scr_nearest::sample_pixel_img, &render_scr_nearest::sample_pixel_img, false> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case detect_nearest_scaled:
      {
	render_scr_nearest_scaled render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	*error = produce_file<render_scr_nearest_scaled, &render_scr_nearest_scaled::sample_pixel_img, &render_scr_nearest_scaled::sample_pixel_img, false> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case detect_relax:
      {
	render_scr_relax render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (rfparams.verbose)
	  print_time ();
	*error = produce_file<render_scr_relax, &render_scr_relax::sample_pixel_img, &render_scr_relax::sample_pixel_img, false> (rfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    default:
      abort ();
    }
  return true;
}
