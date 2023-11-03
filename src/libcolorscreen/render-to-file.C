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

struct produce_file_params
{
  const char *filename;
  int width, height;
  coord_t stepx, stepy;
  bool hdr;
  bool verbose;
  void *icc_profile;
  size_t icc_profile_len;
  int antialias;
  coord_t dpi;
};

template<typename T, rgbdata (T::*sample_data)(coord_t x, coord_t y)>
const char *
produce_file (produce_file_params p, T &render, progress_info *progress)
{
  const char *error = NULL;
  tiff_writer_params tp;
  tp.filename = p.filename;
  tp.width = p.width;
  tp.height = p.height;
  tp.hdr = p.hdr;
  tp.icc_profile = p.icc_profile;
  tp.icc_profile_len = p.icc_profile_len;
  tiff_writer out(tp, &error);
  if (error)
    return error;
  if (p.verbose)
    {
      printf ("Rendering %s in resolution %ix%i: 00%%", p.filename, p.width,
	      p.height);
      fflush (stdout);
      record_time ();
    }
  for (int y = 0; y < p.height; y++)
    {
      if (p.antialias == 1)
	{
#pragma omp parallel for default(none) shared(p,render,y,out)
	  for (int x = 0; x < p.width; x++)
	    {
	      rgbdata d = (render.*sample_data) (x * p.stepx, y * p.stepy);
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
	  coord_t asx = p.stepx / p.antialias;
	  coord_t asy = p.stepy / p.antialias;
	  luminosity_t sc = 1.0 / (p.antialias * p.antialias);
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc)
	  for (int x = 0; x < p.width; x++)
	    {
	      rgbdata d = {0, 0, 0};
	      for (int ay = 0 ; ay < p.antialias; ay++)
		for (int ax = 0 ; ax < p.antialias; ax++)
		  {
		    d += (render.*sample_data) (x * p.stepx + ax * asx, y * p.stepy + ay * asy);
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
get_rendered_file_dimensions (enum output_mode mode, scr_to_img_parameters & param, image_data &scan, int *width, int *height, coord_t *stepx, coord_t *stepy, int *antialias, coord_t *dpi)
{
  render_parameters rparam;
  switch (mode)
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
	coord_t pixelsize = render.pixel_size ();
	/* Interpolated mode makes no sense past 4 pixels per screen tile.  */
	if (mode == interpolated)
	  pixelsize = 0.25;
	*antialias = 1;
	if (mode == predictive || mode == realistic)
	  *antialias = 4;
	*width = render_width / pixelsize;
	*height = render_height / pixelsize;
	*stepx = pixelsize;
	*stepy = pixelsize;
	*dpi = 0;
      }
      break;
    case detect_realistic:
    case detect_adjusted:
    case detect_nearest:
    case detect_nearest_scaled:
    case detect_relax:
      {
	*width = scan.width;
	*height = scan.height;
	if (mode == detect_realistic || mode == detect_adjusted)
	  *antialias = 4;
	else
	  *antialias = 1;
	*stepx = 1;
	*stepy = 1;
	*dpi = 0;
      }
      break;
    }
  return true;
}

/* Render image to TIFF file OUTFNAME.  */
bool
render_to_file (enum output_mode mode, const char *outfname,
		image_data & scan, scr_to_img_parameters & param,
		scr_detect_parameters & dparam, render_parameters rparam,
		bool hdr,
		progress_info * progress, bool verbose, const char **error)
{
  if (verbose)
    {
      printf ("Precomputing...");
      fflush (stdout);
      record_time ();
    }
  void *icc_profile = sRGB_icc;
  size_t icc_profile_len = sRGB_icc_len;

  if (hdr)
    {
      rparam.output_gamma = 1;
      rparam.output_profile = render_parameters::output_profile_original;
    }

  if (rparam.output_profile == render_parameters::output_profile_original)
    icc_profile_len = rparam.get_icc_profile (&icc_profile);

  /* Initialize rendering engine.  */
  produce_file_params pfparams;
  pfparams.filename = outfname;
  pfparams.hdr = hdr;
  pfparams.verbose = verbose;
  if (!get_rendered_file_dimensions (mode, param, scan, &pfparams.width, &pfparams.height, &pfparams.stepx, &pfparams.stepy, &pfparams.antialias, &pfparams.dpi))
    {
      *error = "Precomputation failed (out of memory)";
      return false;
    }
  pfparams.icc_profile = icc_profile;
  pfparams.icc_profile_len = icc_profile_len;

  switch (mode)
    {
    case none:
    case corrected_color:
      {
	render_img render (param, scan, rparam, 65535);
	if (mode == corrected_color)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (verbose)
	  print_time ();

	pfparams.icc_profile = scan.icc_profile;
	pfparams.icc_profile_len = scan.icc_profile_size;
	// TODO: For HDR output we want to linearize the ICC profile.
	*error = produce_file<render_img, &render_img::sample_pixel_final> (pfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case interpolated:
    case predictive:
    case combined:
      {
	bool screen_compensation = mode == predictive;
	bool adjust_luminosity = mode == combined;

	render_interpolate render (param, scan, rparam, 65535,
				   screen_compensation, adjust_luminosity);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (verbose)
	  print_time ();

	*error = produce_file<render_interpolate, &render_interpolate::sample_pixel_final> (pfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    case realistic:
    case preview_grid:
    case color_preview_grid:
      {
	render_superpose_img render (param, scan, rparam, 65535, mode != realistic);
	if (mode == color_preview_grid && scan.rgbdata)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (verbose)
	  print_time ();
	/* TODO: Maybe preview_grid and color_preview_grid
	   should be in the scan profile.  */
	*error = produce_file<render_superpose_img, &render_superpose_img::sample_pixel_final> (pfparams, render, progress);
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
	if (verbose)
	  print_time ();
	*error = produce_file<render_scr_detect_superpose_img, &render_scr_detect_superpose_img::sample_pixel_img> (pfparams, render, progress);
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
	if (verbose)
	  print_time ();
	*error = produce_file<render_scr_detect, &render_scr_detect::get_adjusted_pixel> (pfparams, render, progress);
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
	if (verbose)
	  print_time ();
	*error = produce_file<render_scr_nearest, &render_scr_nearest::sample_pixel_img> (pfparams, render, progress);
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
	if (verbose)
	  print_time ();
	*error = produce_file<render_scr_nearest_scaled, &render_scr_nearest_scaled::sample_pixel_img> (pfparams, render, progress);
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
	if (verbose)
	  print_time ();
	*error = produce_file<render_scr_relax, &render_scr_relax::sample_pixel_img> (pfparams, render, progress);
	if (*error)
	  return false;
      }
      break;
    default:
      abort ();
    }
  return true;
}
