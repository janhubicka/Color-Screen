/* Render to file implementation for libcolorscreen.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <stdlib.h>
#include <sys/time.h>
#include <cstdio>
#include "icc-srgb.h"
#include "include/tiff-writer.h"
#include "include/colorscreen.h"
#include "include/stitch.h"
#include "render-scr-detect.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "icc.h"
#include "render-to-file.h"
#ifdef RENDER_EXTRA
#include "extra-render/render-extra.h"
#endif
#include "render-fast.h"

namespace colorscreen
{

const property_t render_to_file_params::geometry_names []  = {
  { "screen", "Screen", "" },
  { "scan", "Scan", "" },
  { "default", "Default", "" },
};

/** Complete rendering parameters based on scan data and rendering type.
    RTPARAMS - rendering type parameters.
    PARAM - screen to image parameters.
    SCAN - source scan data.
    STITCH - stitch project data (if any).
    P - output rendering to file parameters.
    Returns true on success.  */
bool
complete_rendered_file_parameters (render_type_parameters *rtparams, scr_to_img_parameters * param, image_data *scan, stitch_project *stitch, render_to_file_params *p)
{
  render_parameters rparam;
  if (scan && scan->stitch)
    {
      stitch = scan->stitch;
      scan = nullptr;
    }
  const render_type_property &prop = render_type_properties [(int)rtparams->type];

  if (p->geometry == render_to_file_params::default_geometry)
    {
      if (prop.flags & render_type_property::NEEDS_SCR_TO_IMG)
        p->geometry = render_to_file_params::screen_geometry;
    }
  /* Stitching always needs geometry correction.  */
  if (stitch)
    p->geometry = render_to_file_params::screen_geometry;
  
  /* Do we render using scr_to_img map?  */
  if (p->geometry == render_to_file_params::screen_geometry)
    {
      coord_t render_width, render_height;
      if (!stitch)
	{
	  render_img render (*param, *scan, rparam, 65535);
	  render.compute_final_range ();
	  render_width = render.get_final_width ();
	  render_height = render.get_final_height ();
	  if (!p->pixel_size)
	    {
	      p->pixel_size = render.pixel_size ();
	      if (prop.flags & render_type_property::PATCH_RESOLUTION)
		p->pixel_size = param->type == Dufay ? 0.5 : 1.0/3;
	      else if (prop.flags & render_type_property::SCREEN_RESOLUTION)
		p->pixel_size = 1;
	    }
	}
      else
	{
	  int xmin, xmax, ymin, ymax;
	  stitch->determine_viewport (xmin, xmax, ymin, ymax);
	  int_image_area viewport (xmin, ymin, xmax - xmin, ymax - ymin);
	  render_width = viewport.width;
	  render_height = viewport.height;
	  p->xpos = viewport.x;
	  p->ypos = viewport.y;
	  if (!p->pixel_size)
	    {
	      p->pixel_size = stitch->pixel_size;
	      if (prop.flags & render_type_property::PATCH_RESOLUTION)
		p->pixel_size = stitch->param.type == Dufay ? 0.5 : 1.0/3;
	      else if (prop.flags & render_type_property::SCREEN_RESOLUTION)
		p->pixel_size = 1;
	    }
	}
      if (!p->xstep)
	{
	  if (!p->screen_scale)
	    {
	      p->xstep = p->pixel_size / p->scale;
	      p->ystep = p->pixel_size / p->scale;
	    }
	  else
	    {
	      p->xstep = 1 / p->screen_scale;
	      p->ystep = 1 / p->screen_scale;
	    }
	}
      if (!p->antialias)
	{
	  if (prop.flags & render_type_property::ANTIALIAS)
	    p->antialias = round (4 * p->xstep / p->pixel_size);
	  else
	    p->antialias = round (1 * p->xstep / p->pixel_size);
	  /* Antialias should be odd, so middle of tile is sampled.  */
	  if (!(p->antialias & 1))
	    p->antialias++;
	}
      if (!p->width && !p->height)
	{
	  p->width = render_width / p->xstep;
	  p->height = render_height / p->ystep;
	}
      else if (!p->width && p->height)
	{
	  /* height specified: derive step and width to preserve aspect ratio.  */
	  double new_step = render_height / (double)p->height;
	  p->xstep = p->ystep = new_step;
	  p->width = (int)(render_width / new_step + 0.5);
	}
      else if (p->width && !p->height)
	{
	  /* width specified: derive step and height to preserve aspect ratio.  */
	  double new_step = render_width / (double)p->width;
	  p->xstep = p->ystep = new_step;
	  p->height = (int)(render_height / new_step + 0.5);
	}
      /* TODO: Make this work with stitched projects.  */
      if ((!p->xdpi || !p->ydpi) && scan && scan->xdpi && scan->ydpi)
	{
	  scr_to_img map;
	  if (!map.set_parameters (*param, *scan))
	    return false;
	  point_t c = map.img_to_final ({(coord_t)(scan->width / 2), (coord_t)(scan->height / 2)});
	  point_t z = map.final_to_img (c);
	  point_t imgp = map.final_to_img ({c.x + p->xstep, c.y});
	  imgp.x = (imgp.x - z.x) / scan->xdpi;
	  imgp.y = (imgp.y - z.y) / scan->ydpi;
	  coord_t len = my_sqrt (imgp.x * imgp.x + imgp.y * imgp.y);
	  /* This is approximate for deformed screens, so take average of X and Y resolution.  */
	  if (len && !p->xdpi)
	    {
	      coord_t xdpi = 1 / len;

	      imgp = map.final_to_img ({c.x, c.y + p->ystep});
	      imgp.x = (imgp.x - z.x) / scan->xdpi;
	      imgp.y = (imgp.y - z.y) / scan->ydpi;
	      coord_t len2 = my_sqrt (imgp.x * imgp.x + imgp.y * imgp.y);
	      if (len2 && !p->ydpi)
		{
		  p->xdpi = p->ydpi = (xdpi + 1 / len2) / 2;
		}
	    }
	}
    }
  else
    {
      if (!p->xstep)
	p->xstep = p->ystep = 1 / p->scale;
      if (!p->width && !p->height)
	{
	  p->width = scan->width / p->xstep;
	  p->height = scan->height / p->ystep;
	}
      else if (!p->width && p->height)
	{
	  double new_step = (double)scan->height / p->height;
	  p->xstep = p->ystep = new_step;
	  p->width = (int)(scan->width / new_step + 0.5);
	}
      else if (p->width && !p->height)
	{
	  double new_step = (double)scan->width / p->width;
	  p->xstep = p->ystep = new_step;
	  p->height = (int)(scan->height / new_step + 0.5);
	}
      if (!p->antialias)
	{
	  if (prop.flags & render_type_property::ANTIALIAS)
	    p->antialias = round (4 * p->xstep);
	  else
	    p->antialias = round (1 * p->ystep);
	  /* Antialias should be odd, so middle of tile is sampled.  */
	  if (!(p->antialias & 1))
	    p->antialias++;
	}
      if (!p->xdpi)
	p->xdpi = scan->xdpi / p->xstep;
      if (!p->ydpi)
	p->ydpi = scan->ydpi / p->ystep;
    }
  return true;
}

/** Complete rendering parameters (wrapper).  */
bool
complete_rendered_file_parameters (render_type_parameters &rtparams, scr_to_img_parameters & param, image_data &scan, render_to_file_params *p)
{
  return complete_rendered_file_parameters (&rtparams, &param, &scan, nullptr, p);
}

/** Main entry point for rendering an image to a file.
    SCAN - source scan data.
    PARAM - screen to image parameters.
    DPARAM - screen detection parameters.
    IN_RPARAM - rendering parameters.
    RFPARAMS - render to file specific parameters.
    RTPARAM - rendering type parameters.
    PROGRESS - progress reporting object.
    ERROR - set to error string if rendering fails.
    Returns true on success.  */
bool
render_to_file (image_data & scan, scr_to_img_parameters & param,
		scr_detect_parameters & dparam, render_parameters &in_rparam,
		struct render_to_file_params rfparams /* modified */,
		render_type_parameters &rtparam,
		progress_info * progress, const char **error)
{
  bool free_profile = false;
  int black = 0;
  if (scan.stitch)
    return scan.stitch->write_tiles (in_rparam, &rfparams, rtparam, 1, progress, error);
  render_parameters rparam;
  rparam.adjust_for (rtparam, in_rparam);
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
      if (progress)
        progress->resume_stdout ();
    }
  void *icc_profile = nullptr;
  size_t icc_profile_len = 0;

  const render_type_property &prop = render_type_properties [(int)rtparam.type];
  if ((prop.flags & render_type_property::NEEDS_RGB) && !scan.has_rgb ())
    {
      *error = "Selected rendering algorithm is impossible on monochromatic scan";
      return false;
    }
  if (rfparams.hdr || rfparams.dng)
    rparam.output_profile = render_parameters::output_profile_original;

  if (rparam.output_profile == render_parameters::output_profile_original)
    {
      if (prop.flags & render_type_property::OUTPUTS_SCAN_PROFILE)
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
      if (prop.flags & render_type_property::OUTPUTS_SRGB_PROFILE)
        {
	  rfparams.icc_profile = sRGB_icc;
	  rfparams.icc_profile_len = sRGB_icc_len;
	  rparam.output_gamma = -1;
        }
      else if (!free_profile)
        {
          icc_profile_len = rparam.get_icc_profile (&icc_profile, &scan, false /*TODO*/);
	  free_profile = true;
        }
    }

  /* Initialize rendering engine.  */
  if (!complete_rendered_file_parameters (rtparam, param, scan, &rfparams))
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
  if ((int)rtparam.type < (int)render_type_adjusted_color || rtparam.type >= (int)render_type_profiled_original)
    render_to_scr::render_to_file (rfparams, rtparam, param, rparam, scan, black, progress);
  else
    render_scr_detect::render_to_file (rfparams, rtparam, param, dparam, rparam, scan, black, progress);
  if (free_profile)
    free (icc_profile);
  return true;
}
}
