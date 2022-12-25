#include <stdlib.h>
#include <sys/time.h>
#include <tiffio.h>
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

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_output_file (const char *outfname, int outwidth, int outheight,
		  uint16_t ** outrow, bool verbose, const char **error,
		  progress_info *progress)
{
  TIFF *out = TIFFOpen (outfname, "wb");
  if (!out)
    {
      *error = "can not open output file";
      return NULL;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, outheight)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, sRGB_icc_len, sRGB_icc))
    {
      *error = "write error";
      return NULL;
    }
  *outrow = (uint16_t *) malloc (outwidth * 2 * 3);
  if (!*outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving", outheight);
    }
  if (verbose)
    {
      printf ("Rendering %s in resolution %ix%i: 00%%", outfname, outwidth,
	      outheight);
      fflush (stdout);
      record_time ();
    }
  return out;
}

/* Write one row.  */
static bool
write_row (TIFF * out, int y, uint16_t * outrow, const char **error, progress_info *progress)
{
  if (progress && progress->cancel_requested ())
    {
      free (outrow);
      TIFFClose (out);
      *error = "Cancelled";
      return false;
    }
  if (TIFFWriteScanline (out, outrow, y, 0) < 0)
    {
      free (outrow);
      TIFFClose (out);
      *error = "Write error";
      return false;
    }
   if (progress)
     progress->inc_progress ();
  return true;
}
}

/* Render image to TIFF file OUTFNAME.  */
bool
render_to_file (enum output_mode mode, const char *outfname,
		image_data & scan, scr_to_img_parameters & param,
		scr_detect_parameters & dparam, render_parameters & rparam,
		progress_info * progress, bool verbose, const char **error)
{
  if (verbose)
    {
      printf ("Precomputing...");
      fflush (stdout);
      record_time ();
    }
  TIFF *out;
  uint16_t *outrow;

  /* Initialize rendering engine.  */

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
	double render_width = render.get_final_width ();
	double render_height = render.get_final_height ();
	double out_stepx, out_stepy;
	int outwidth;
	int outheight;

#if 0
	/* FIXME: it should be same as realistic.  */
	outwidth = render_width * 4;
	outheight = render_height * 4;
	out_stepy = out_stepx = 0.25;
#endif
	double pixelsize = render.pixel_size ();
	printf ("Pixel size:%f\n", pixelsize);
	pixelsize = 0.119245;
	outwidth = render_width / pixelsize;
	outheight = render_height / pixelsize;
	out_stepx = out_stepy = pixelsize;

	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < outheight; y++)
	  {
	    for (int x = 0; x < outwidth; x++)
	      {
		int rr, gg, bb;
		render.render_pixel_final (x * out_stepx, y * out_stepy, &rr, &gg,
				     &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	  }
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
	double render_width = render.get_final_width ();
	double render_height = render.get_final_height ();
	double out_stepx, out_stepy;
	int outwidth;
	int outheight;

	/* In predictive and combined mode try to stay close to scan resolution.  */
	if (mode == predictive || mode == combined)
	  {
	    double pixelsize = render.pixel_size ();
	    outwidth = render_width / pixelsize;
	    outheight = render_height / pixelsize;
	    out_stepx = out_stepy = pixelsize;
	  }
	/* Interpolated mode makes no sense past 4 pixels per screen tile.  */
	else
	  {
	    outwidth = render_width * 4;
	    outheight = render_height * 4;
	    out_stepy = out_stepx = 0.25;
	  }
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < outheight; y++)
	  {
	    for (int x = 0; x < outwidth; x++)
	      {
		int rr, gg, bb;
		render.render_pixel_final (x * out_stepx, y * out_stepy, &rr, &gg,
				     &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	  }
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
	int scale = 1;
	int outwidth = scan.width;
	int outheight = scan.height;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height * scale; y++)
	  {
	    for (int x = 0; x < scan.width * scale; x++)
	      {
		int rr, gg, bb;
		render.render_pixel_img_antialias (x / (double) scale,
						   y / (double) scale,
						   1.0 / scale, 128, &rr, &gg,
						   &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	  }
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
	int downscale = 1;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
		      rgbdata rgb =
			render.fast_sample_pixel_img (x * downscale + xx,
						      y * downscale + yy);
		      srr += rgb.red;
		      sgg += rgb.green;
		      sbb += rgb.blue;
		    }
		int r, g, b;
		render.set_color (srr / (downscale * downscale),
				  sgg / (downscale * downscale),
				  sbb / (downscale * downscale), &r, &g, &b);
		outrow[3 * x] = r;
		outrow[3 * x + 1] = g;
		outrow[3 * x + 2] = b;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	    if (verbose)
	      print_progress (y, scan.height / downscale);
	  }
	break;
      }
    case detect_adjusted:
      {
	render_scr_detect render (dparam, scan, rparam, 65535);
	if (!render.precompute_all (progress))
	  {
	    *error = "Precomputation failed (out of memory)";
	    return false;
	  }
	if (verbose)
	  print_time ();
	int downscale = 1;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
		      rgbdata rgb =
			render.fast_get_adjusted_pixel (x * downscale + xx,
							y * downscale + yy);
		      srr += rgb.red;
		      sgg += rgb.green;
		      sbb += rgb.blue;
		    }
		int r, g, b;
		render.set_color (srr / (downscale * downscale),
				  sgg / (downscale * downscale),
				  sbb / (downscale * downscale), &r, &g, &b);
		outrow[3 * x] = r;
		outrow[3 * x + 1] = g;
		outrow[3 * x + 2] = b;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	    if (verbose)
	      print_progress (y, scan.height / downscale);
	  }
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
	int downscale = 1;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		int srr = 0, sgg = 0, sbb = 0;
		int rr, gg, bb;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
		      render.render_pixel_img (x * downscale + xx,
					       y * downscale + yy,
					       &rr, &gg, &bb);
		      srr += rr;
		      sgg += gg;
		      sbb += bb;
		    }
		outrow[3 * x] = srr / (downscale * downscale);
		outrow[3 * x + 1] = sgg / (downscale * downscale);
		outrow[3 * x + 2] = sbb / (downscale * downscale);
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	    if (verbose)
	      print_progress (y, scan.height / downscale);
	  }
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
	int downscale = 1;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
		      luminosity_t rr, gg, bb;
		      render.render_raw_pixel_img (x * downscale + xx,
						   y * downscale + yy,
						   &rr, &gg, &bb);
		      srr += rr;
		      sgg += gg;
		      sbb += bb;
		    }
		int r, g, b;
		render.set_color (srr / (downscale * downscale),
				  sgg / (downscale * downscale),
				  sbb / (downscale * downscale), &r, &g, &b);
		outrow[3 * x] = r;
		outrow[3 * x + 1] = g;
		outrow[3 * x + 2] = b;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	    if (verbose)
	      print_progress (y, scan.height / downscale);
	  }
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
	int downscale = 1;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	out =
	  open_output_file (outfname, outwidth, outheight, &outrow, verbose,
			    error, progress);
	if (!out)
	  return false;
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
		      luminosity_t rr, gg, bb;
		      render.render_raw_pixel_img (x * downscale + xx,
						   y * downscale + yy,
						   &rr, &gg, &bb);
		      srr += rr;
		      sgg += gg;
		      sbb += bb;
		    }
		int r, g, b;
		render.set_color (srr / (downscale * downscale),
				  sgg / (downscale * downscale),
				  sbb / (downscale * downscale), &r, &g, &b);
		outrow[3 * x] = r;
		outrow[3 * x + 1] = g;
		outrow[3 * x + 2] = b;
	      }
	    if (!write_row (out, y, outrow, error, progress))
	      return false;
	    if (verbose)
	      print_progress (y, scan.height / downscale);
	  }
      }
      break;
    default:
      abort ();
    }
  TIFFClose (out);
  free (outrow);
  return true;
}
