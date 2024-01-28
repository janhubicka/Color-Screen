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
#include "render-to-file.h"

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
	  render.set_color_display (false);
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

	render_interpolate render (param, scan, rparam, 65535);
	if (rfparams.mode == predictive)
	  render.set_predictive ();
	if (rfparams.mode == combined)
	  render.set_adjust_luminosity ();
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
	render_superpose_img render (param, scan, rparam, 65535);
	if (rfparams.mode != realistic)
	  render.set_preview_grid ();
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
