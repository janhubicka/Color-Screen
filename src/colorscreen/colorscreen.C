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
  detect_nearest,
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
      printf ("Rendering %s in resolution %ix%i:", outfname, outwidth, outheight);
      fflush (stdout);
      record_time ();
    }
  return out;
}

/* Write one row.  */
void
write_row (TIFF *out, int y, uint16_t *outrow)
{
  if (TIFFWriteScanline(out, outrow, y, 0) < 0)
    {
      fprintf (stderr, "Write error on line %i\n", y);
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
  else if (!strcmp (mode, "detect-nearest"))
    return detect_nearest;
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
    case detect_nearest:
      {
	render_scr_nearest render (dparam, scan, rparam, 65535);
	render.precompute_all ();
	if (verbose)
	  print_time ();
	double scale = 0.2;
	int outwidth = scan.width * scale;
	int outheight = scan.height * scale;
	uint16_t *outrow;
	TIFF *out = open_output_file (outfname, outwidth, outheight, &outrow);
	for (int y = 0; y < scan.height * scale; y++)
	  {
	    for (int x = 0; x < scan.width * scale; x++)
	      {
		int rr, gg, bb;
		render.render_pixel_img (x / (double) scale,
					 y / (double) scale,
					 &rr, &gg, &bb);
		outrow[3 * x] = rr;
		outrow[3 * x + 1] = gg;
		outrow[3 * x + 2] = bb;
	      }
	    write_row (out, y, outrow);
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
