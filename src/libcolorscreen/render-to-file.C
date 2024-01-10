#include <stdlib.h>
#include <sys/time.h>
#include "include/tiff-writer.h"
#include "include/colorscreen.h"
#include "include/stitch.h"
#include "include/render-fast.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "icc-srgb.h"
#include "icc.h"

const struct render_to_file_params::output_mode_property render_to_file_params::output_mode_properties[output_mode_max]
{
	{"corrected", false},
	{"corrected-color", true},
	{"realistic", false},
	{"preview-grid", false},
	{"color-preview-grid", true},
	{"interpolated", false},
	{"predictive", false},
	{"combined", false},
	{"detect-adjusted", true},
	{"detect-realistic", true},
	{"detect-nearest", true},
	{"detect-nearest-scaled", true},
	{"detect-relaxation", true}
};
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
  printf ("  ... done in %.3fs\n", time);
}

template<typename T, rgbdata (T::*sample_data)(coord_t x, coord_t y), rgbdata (T::*sample_scr_data)(coord_t x, coord_t y), bool support_tile>
const char *
produce_file (render_to_file_params p, T &render, int black, progress_info *progress)
{
  const char *error = NULL;
  tiff_writer_params tp;
  tp.filename = p.filename;
  tp.width = p.width;
  tp.height = p.height;
  if (p.dng)
    {
      p.depth=16;
      p.hdr = false;
      tp.dng = true; 
      tp.dye_to_xyz = render.get_dye_to_xyz_matrix ();
      tp.black= black;
    }
  tp.hdr = p.hdr;
  tp.depth = p.depth;
  tp.icc_profile = p.icc_profile;
  tp.icc_profile_len = p.icc_profile_len;
  tp.tile = p.tile;
  tp.xdpi = p.xdpi;
  tp.ydpi = p.ydpi;
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
      printf ("Rendering %s in resolution %ix%i and depth %i", p.filename, p.width,
	      p.height, p.depth);
      if (p.hdr)
	printf (", HDR");
      if (p.xdpi && p.xdpi == p.ydpi)
	printf (", PPI %f", p.xdpi);
      else
	{
	  if (p.xdpi)
	    printf (", horisontal PPI %f", p.xdpi);
	  if (p.ydpi)
	    printf (", vertical PPI %f", p.ydpi);
	}
      fflush (stdout);
      record_time ();
      printf ("\n");
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
    }
  if (progress)
    progress->set_task ("Closing tiff file", 1);
  if (p.verbose)
    {
      if (progress)
	progress->pause_stdout ();
      print_time ();
      if (progress)
	progress->resume_stdout ();
    }
  return NULL;
}

}

bool
complete_rendered_file_parameters (scr_to_img_parameters * param, image_data *scan, stitch_project *stitch, render_to_file_params *p)
{
  render_parameters rparam;
  if (scan && scan->stitch)
    {
      stitch = scan->stitch;
      scan = NULL;
    }
  switch (p->mode)
    {
    case corrected:
    case corrected_color:
    case interpolated:
    case predictive:
    case combined:
    case realistic:
    case preview_grid:
    case color_preview_grid:
      {
	coord_t render_width, render_height;
	if (!stitch)
	  {
	    render_img render (*param, *scan, rparam, 65535);
	    render_width = render.get_final_width ();
	    render_height = render.get_final_height ();
	    if (!p->pixel_size)
	      p->pixel_size = render.pixel_size ();
	    if (p->mode == interpolated)
	      p->pixel_size = param->type == Dufay ? 0.5 : 1.0/3;
	  }
	else
	  {
	    int xmin, xmax, ymin, ymax;
	    stitch->determine_viewport (xmin, xmax, ymin, ymax);
	    render_width = xmax - xmin;
	    render_height = ymax - ymin;
	    p->xpos = xmin;
	    p->ypos = ymin;
	    if (!p->pixel_size)
	      p->pixel_size = stitch->pixel_size;
	    if (p->mode == interpolated)
	      p->pixel_size = stitch->params.type == Dufay ? 0.5 : 1.0/3;
	  }
	if (!p->xstep)
	  {
	    p->xstep = p->pixel_size / p->scale;
	    p->ystep = p->pixel_size / p->scale;
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
	/* TODO: Make this work with stitched projects.  */
	if ((!p->xdpi || !p->ydpi) && scan && scan->xdpi && scan->ydpi)
	  {
	    coord_t zx, zy, xx, yy;
	    coord_t cx, cy;
	    scr_to_img map;
	    map.set_parameters (*param, *scan);
	    map.img_to_final (scan->width / 2, scan->height / 2, &cx, &cy);
	    map.final_to_img (cx, cy, &zx, &zy);
	    map.final_to_img (cx + p->xstep, cy, &xx, &yy);
	    //fprintf (stderr, "%f %f %f %f\n",zx,zy,xx,yy);
	    xx = (xx - zx) / scan->xdpi;
	    yy = (yy - zy) / scan->ydpi;
	    coord_t len = sqrt (xx * xx + yy * yy);
	    //fprintf (stderr, "%f %f %f %f %f\n", scan.xdpi, len, p->xstep, xx, yy);
	    /* This is approximate for defomated screens, so take average of X and Y resolution.  */
	    if (len && !p->xdpi)
	      {
		coord_t xdpi = 1 / len;

		map.final_to_img (cx, cy + p->ystep, &xx, &yy);
		xx = (xx - zx) / scan->xdpi;
		yy = (yy - zy) / scan->ydpi;
		len = sqrt (xx * xx + yy * yy);
		if (len && !p->ydpi)
		  {
		    p->xdpi = p->ydpi = (xdpi + 1 / len) / 2;
		  }
	      }
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
	    p->width = scan->width / p->xstep;
	    p->height = scan->height / p->ystep;
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
	if (!p->xdpi)
	  p->xdpi = scan->xdpi / p->xstep;
	if (!p->ydpi)
	  p->ydpi = scan->ydpi / p->ystep;
      }
      break;
    default:
      abort ();
    }
  return true;
}
bool
complete_rendered_file_parameters (scr_to_img_parameters & param, image_data &scan, render_to_file_params *p)
{
  return complete_rendered_file_parameters (&param, &scan, NULL, p);
}

/* Render image to TIFF file OUTFNAME.  */
bool
render_to_file (image_data & scan, scr_to_img_parameters & param,
		scr_detect_parameters & dparam, render_parameters rparam /* modified */,
		struct render_to_file_params rfparams /* modified */,
		progress_info * progress, const char **error)
{
  bool free_profile = false;
  int black = 0;
  if (scan.stitch)
    return scan.stitch->write_tiles (rparam, &rfparams, 1, progress, error);
  if (rfparams.dng)
   {
     rparam.output_gamma = 1;
     black = rparam.dark_point * 65536;
     rparam.dark_point = 0;
     rparam.scan_exposure = rparam.brightness = 1;
     rparam.white_balance.red = rparam.white_balance.green = rparam.white_balance.blue = 1;
   }
  if (rfparams.verbose)
    {
      if (progress)
        progress->pause_stdout ();
      printf ("Precomputing\n");
      fflush (stdout);
      record_time ();
      if (progress)
        progress->resume_stdout ();
    }
  void *icc_profile /*= sRGB_icc*/ = NULL;
  size_t icc_profile_len = /*sRGB_icc_len =*/ 0;

  if ((int)rfparams.mode >= (int)detect_adjusted && !scan.rgbdata)
    {
      *error = "Screen detection is imposible in monochromatic scan";
      return false;
    }
  if ((int)rfparams.mode == (int)corrected_color && !scan.rgbdata)
    {
      *error = "Corrected color is imposible in monochromatic scan";
      return false;
    }
  if ((int)rfparams.mode == (int)color_preview_grid && !scan.rgbdata)
    {
      *error = "color preview grid is imposible in monochromatic scan";
      return false;
    }
  if (rfparams.hdr || rfparams.dng)
    rparam.output_profile = render_parameters::output_profile_original;

  if (rparam.output_profile == render_parameters::output_profile_original)
    {
      if (rfparams.mode == corrected_color)
        {
	  if (scan.icc_profile
	      && rparam.gamma == rparam.output_gamma)
	    {
	      rfparams.icc_profile = scan.icc_profile;
	      rfparams.icc_profile_len = scan.icc_profile_size;
	    }
	  else
	    {
	      xyz red = xyY_to_xyz (scan.primary_red.x, scan.primary_red.y, scan.primary_red.Y);
	      xyz green = xyY_to_xyz (scan.primary_green.x, scan.primary_green.y, scan.primary_green.Y);
	      xyz blue = xyY_to_xyz (scan.primary_blue.x, scan.primary_blue.y, scan.primary_blue.Y);
	      icc_profile_len = create_profile ("ColorScreen produced profile based on original scan", red, green, blue, red+green+blue, rparam.output_gamma, &icc_profile);
	      free_profile = true;
	    }
        }
      else
        icc_profile_len = rparam.get_icc_profile (&icc_profile, &scan, false /*TODO*/);
    }

  /* Initialize rendering engine.  */
  if (!complete_rendered_file_parameters (param, scan, &rfparams))
    {
      *error = "Precomputation failed (out of memory)";
      if (free_profile)
	free (icc_profile);
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
    case corrected:
    case corrected_color:
      {
	render_img render (param, scan, rparam, 65535);
	if (rfparams.mode == corrected_color)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }

	// TODO: For HDR output we want to linearize the ICC profile.
	*error = produce_file<render_img, &render_img::sample_pixel_final, &render_img::sample_pixel_scr, true> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
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
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }

	*error = produce_file<render_interpolate, &render_interpolate::sample_pixel_final, &render_interpolate::sample_pixel_scr, true> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
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
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	/* TODO: Maybe preview_grid and color_preview_grid
	   should be in the scan profile.  */
	*error = produce_file<render_superpose_img, &render_superpose_img::sample_pixel_final, &render_superpose_img::sample_pixel_scr, true> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
      }
      break;
    case detect_realistic:
      {
	render_scr_detect_superpose_img render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    if (free_profile)
	      free (icc_profile);
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	*error = produce_file<render_scr_detect_superpose_img, &render_scr_detect_superpose_img::sample_pixel_img, &render_scr_detect_superpose_img::sample_pixel_img, false> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	break;
      }
    case detect_adjusted:
      {
	render_scr_detect render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (false, false, progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	*error = produce_file<render_scr_detect, &render_scr_detect::get_adjusted_pixel, &render_scr_detect::get_adjusted_pixel, false> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	break;
      }
    case detect_nearest:
      {
	render_scr_nearest render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	*error = produce_file<render_scr_nearest, &render_scr_nearest::sample_pixel_img, &render_scr_nearest::sample_pixel_img, false> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
      }
      break;
    case detect_nearest_scaled:
      {
	render_scr_nearest_scaled render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	*error = produce_file<render_scr_nearest_scaled, &render_scr_nearest_scaled::sample_pixel_img, &render_scr_nearest_scaled::sample_pixel_img, false> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
      }
      break;
    case detect_relax:
      {
	render_scr_relax render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
	if (rfparams.verbose)
	  {
	    if (progress)
	      progress->pause_stdout ();
	    print_time ();
	    if (progress)
	      progress->resume_stdout ();
	  }
	*error = produce_file<render_scr_relax, &render_scr_relax::sample_pixel_img, &render_scr_relax::sample_pixel_img, false> (rfparams, render, black, progress);
	if (*error)
	  {
	    if (free_profile)
	      free (icc_profile);
	    return false;
	  }
      }
      break;
    default:
      abort ();
    }
  if (free_profile)
    free (icc_profile);
  return true;
}
