#include <sys/time.h>
#include <tiffio.h>
#include "../libcolorscreen/include/colorscreen.h"

/* Supported output modes.  */
enum output_mode
{
  none,
  realistic,
  interpolated,
  predictive,
  combined,
  detect_adjusted,
  detect_realistic,
  detect_nearest,
  detect_nearest_scaled,
  detect_relax,
};

static bool verbose = false;
const char *binname;

/* Utilities to report time eneeded for a given operation.
  
   Time measurement started.  */
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
  double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
  printf ("\n  ... done in %.3fs\n", time);
}

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_output_file (const char *outfname, int outwidth, int outheight, uint16_t **outrow)
{
  TIFF *out= TIFFOpen(outfname, "wb");
  if (!out)
    {
      fprintf (stderr, "Can not open %s\n", outfname);
      exit (1);
    }
  TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, outheight);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  *outrow = (uint16_t *)malloc (outwidth * 2 * 3);
  if (!*outrow)
    {
      fprintf (stderr, "Out of memory allocating output buffer\n");
      exit (1);
    }
  if (verbose)
    {
      printf ("Rendering %s in resolution %ix%i: 00%%", outfname, outwidth, outheight);
      fflush (stdout);
      record_time ();
    }
  return out;
}

static void
print_progress (int p, int max)
{
  if (!verbose)
    return;
  int percent = (p * 100 + max / 2) / max;
  int pastpercent = ((p - 1) * 100 + max / 2) / max;
  if (pastpercent != percent)
    {
      printf ("[3D%2i%%", percent);
      fflush (stdout);
    }
}

/* Write one row.  */
void
write_row (TIFF *out, int y, uint16_t *outrow)
{
  if (TIFFWriteScanline(out, outrow, y, 0) < 0)
    {
      fprintf (stderr, "Write error on line %i\\n", y);
      exit (1);
    }
}

void
print_help ()
{
  fprintf (stderr, "%s <scan> <config>.csp <out>.tif\n", binname);
  exit (1);
}

/* Parse output mode.  */
enum output_mode
parse_mode (const char *mode)
{
  if (!strcmp (mode, "none"))
    return none;
  else if (!strcmp (mode, "realistic"))
    return realistic;
  else if (!strcmp (mode, "interpolated"))
    return interpolated;
  else if (!strcmp (mode, "predictive"))
    return predictive;
  else if (!strcmp (mode, "combined"))
    return combined;
  else if (!strcmp (mode, "detect-realistic"))
    return detect_realistic;
  else if (!strcmp (mode, "detect-nearest"))
    return detect_nearest;
  else if (!strcmp (mode, "detect-nearest-scaled"))
    return detect_nearest_scaled;
  else if (!strcmp (mode, "detect-relaxation"))
    return detect_relax;
  else
    {
      fprintf (stderr, "Unkonwn rendering mode:%s\n", mode);
      print_help ();
      return predictive;
    }
}

/* Parse color model.  */
enum render_parameters::color_model_t
parse_color_model (const char *model)
{
  int j;
  for (j = 0; j < render_parameters::color_model_max; j++)
    if (!strcmp (model, render_parameters::color_model_names[j]))
      return (render_parameters::color_model_t)j;
  fprintf (stderr, "Unkonwn color model:%s\n", model);
  print_help ();
  return render_parameters::color_model_max;
}

int
main (int argc, char **argv)
{
  const char *infname = NULL, *outfname = NULL, *cspname = NULL, *error = NULL;
  enum output_mode mode = interpolated;
  float age = -100;
  render_parameters::color_model_t color_model = render_parameters::color_model_max;

  binname = argv[0];

  for (int i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "--help") || !strcmp (argv[i], "-h"))
	print_help();
      else if (!strcmp (argv[i], "--verbose") || !strcmp (argv[i], "-v"))
	{
	  verbose = true;
	}
      else if (!strcmp (argv[i], "--mode"))
	{
	  if (i == argc - 1)
	    print_help ();
	  i++;
	  mode = parse_mode (argv[i]);
	}
      else if (!strcmp (argv[i], "--age"))
	{
	  if (i == argc - 1)
	    print_help ();
	  i++;
	  if (!sscanf (argv[i], "%f",&age))
	    print_help ();
	}
      else if (!strcmp (argv[i], "--color-model"))
	{
	  if (i == argc - 1)
	    print_help ();
	  i++;
	  color_model = parse_color_model (argv[i]);
	}
      else if (!strncmp (argv[i], "--mode=", 7))
	mode = parse_mode (argv[i]+7);
      else if (!infname)
	infname = argv[i];
      else if (!cspname)
	cspname = argv[i];
      else if (!outfname)
	outfname = argv[i];
      else
        print_help ();
    }
  if (!infname || !cspname || !outfname)
    print_help ();

  /* Load scan data.  */
  image_data scan;
  if (verbose)
    {
      printf ("Loading scan %s:", infname);
      fflush (stdout);
      record_time ();
    }
  if (!scan.load (infname, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      exit (1);
    }
  if (verbose)
    {
      printf (" (resolution %ix%i)", scan.width, scan.height);
      print_time ();
    }
  if (mode == detect_nearest && !scan.rgbdata)
    {
      fprintf (stderr, "Screen detection is imposible in monochromatic scan\n");
      exit (1);
    }

  /* Load color screen and rendering parameters.  */
  scr_to_img_parameters param;
  render_parameters rparam;
  scr_detect_parameters dparam;
  FILE *in = fopen (cspname, "rt");
  if (verbose)
    printf ("Loading color screen parameters: %s\n", cspname);
  if (!in)
    {
      perror (cspname);
      exit (1);
    }
  if (!load_csp (in, &param, &dparam, &rparam, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);
  if (age != -100)
    rparam.age = age;

  if (color_model != render_parameters::color_model_max)
    rparam.color_model = color_model;

  if (verbose)
    {
      printf ("Precomputing...");
      fflush (stdout);
      record_time ();
    }

  /* Initialize rendering engine.  */

  switch (mode)
    {
    case none:
      {
	render_img render (param, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	double render_width = render.get_width ();
	double render_height = render.get_height ();
	double out_stepx, out_stepy;
	int outwidth; 
	int outheight;
	uint16_t *outrow;

	/* FIXME: it should be same as realistic.  */
	outwidth = render_width * 4;
	outheight = render_height * 4;
	out_stepy = out_stepx = 0.25;

	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
	for (int y = 0; y < outheight; y++)
	  {
	    for (int x = 0; x < outwidth; x++)
	      {
		int rr, gg, bb;
		render.render_pixel (x * out_stepx, y * out_stepy, &rr, &gg, &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    write_row (out, y, outrow);
	  }
	TIFFClose (out);
      }
      break;
    case interpolated:
    case predictive:
    case combined:
      {
        rparam.precise = true;
        rparam.screen_compensation = mode == predictive;
        rparam.adjust_luminosity = mode == combined;

	render_interpolate render (param, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	double render_width = render.get_width ();
	double render_height = render.get_height ();
	double out_stepx, out_stepy;
	int outwidth; 
	int outheight;
	uint16_t *outrow;

	/* In predictive and combined mode try to stay close to scan resolution.  */
	if (mode == predictive || mode == combined)
	  {
	    double pixelsize =  render.pixel_size ();
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
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
	for (int y = 0; y < outheight; y++)
	  {
	    for (int x = 0; x < outwidth; x++)
	      {
		int rr, gg, bb;
		render.render_pixel (x * out_stepx, y * out_stepy, &rr, &gg, &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    write_row (out, y, outrow);
	  }
	TIFFClose (out);
      }
      break;
    case realistic:
      {
	render_superpose_img render (param, scan, rparam, 65535, false,
				     false);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int scale = 1;
	int outwidth = scan.width;
	int outheight = scan.height;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
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
	    write_row (out, y, outrow);
	  }
	TIFFClose (out);
      }
      break;
    case detect_realistic:
      {
	render_scr_detect_superpose_img render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int downscale = 5;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
			rgbdata rgb = render.fast_sample_pixel_img (x * downscale + xx,
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
	    write_row (out, y, outrow);
	    print_progress (y, scan.height / downscale);
	  }
	TIFFClose (out);
	break;
      }
    case detect_adjusted:
      {
	render_scr_detect render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int downscale = 5;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
	for (int y = 0; y < scan.height / downscale; y++)
	  {
	    for (int x = 0; x < scan.width / downscale; x++)
	      {
		luminosity_t srr = 0, sgg = 0, sbb = 0;
		for (int yy = 0; yy < downscale; yy++)
		  for (int xx = 0; xx < downscale; xx++)
		    {
			rgbdata rgb = render.fast_get_adjusted_pixel (x * downscale + xx,
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
	    write_row (out, y, outrow);
	    print_progress (y, scan.height / downscale);
	  }
	TIFFClose (out);
	break;
      }
    case detect_nearest:
      {
	render_scr_nearest render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int downscale = 5;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
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
	    write_row (out, y, outrow);
	    print_progress (y, scan.height / downscale);
	  }
	TIFFClose (out);
      }
      break;
    case detect_nearest_scaled:
      {
	render_scr_nearest_scaled render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int downscale = 5;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
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
	    write_row (out, y, outrow);
	    print_progress (y, scan.height / downscale);
	  }
	TIFFClose (out);
      }
      break;
    case detect_relax:
      {
	render_scr_relax render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	int downscale = 5;
	int outwidth = scan.width / downscale;
	int outheight = scan.height / downscale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
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
	    write_row (out, y, outrow);
	    print_progress (y, scan.height / downscale);
	  }
	TIFFClose (out);
      }
      break;
    default:
      abort ();
    }
  if (verbose)
    print_time ();
  return 0;
}
