#include <sys/time.h>
#include <tiffio.h>
#include "../libcolorscreen/include/colorscreen.h"

static struct timeval start_time;

static void
record_time ()
{
  gettimeofday (&start_time, NULL);
}

static void
print_time ()
{
  struct timeval end_time;
  gettimeofday (&end_time, NULL);
  double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
  printf (" ... done in %.3fs\n", time);
}
int
main (int argc, char **argv)
{
  const char *infname = NULL, *outfname = NULL, *cspname = NULL, *error = NULL;
  bool verbose = 1;
  if (argc != 4)
    {
      fprintf (stderr, "%s <scan> <config>.csp <out>.tif\n", argv[0]);
      exit (1);
    }
  infname = argv[1];
  cspname = argv[2];
  outfname = argv[3];

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
      printf (" resolution: %ix%i\n", scan.width, scan.height);
      print_time ();
    }

  /* Load color screen and rendering parameters.  */
  scr_to_img_parameters param;
  render_parameters rparam;
  FILE *in = fopen (cspname, "rt");
  if (verbose)
    printf ("Loading color screen parameters: %s\n", cspname);
  if (!in)
    {
      perror (cspname);
      exit (1);
    }
  if (!load_csp (in, param, rparam, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);

  if (verbose)
    {
      printf ("Precomputing...");
      fflush (stdout);
      record_time ();
    }

  /* Initialize rendering engine.  */

  rparam.screen_compensation = true;
  rparam.adjust_luminosity = false;
  rparam.precise = true;

  /* TODO: Jedno rendruje interpolacne a druhe simuluje screen.
     az budu mit cas tak ten kod sjednotim.  */
#if 1
  render_interpolate render (param, scan, rparam, 65535);
  render.precompute_all ();
  if (verbose)
    print_time ();
  /* Produce output file.  */
  TIFF *out= TIFFOpen(outfname, "wb");
  if (!out)
    {
      fprintf (stderr, "Can not open %s\n", outfname);
      exit (1);
    }
  int scale = 8;
  int outwidth = render.get_width () * scale;
  int outheight = render.get_height () * scale;
  TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, outheight);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  uint16_t *outrow = (uint16_t *)malloc (outwidth * 2 * 3);
  if (!outrow)
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
  for (int y = 0; y < render.get_height () * scale; y++)
    {
      for (int x = 0; x < render.get_width () * scale; x++)
	{
	  int rr, gg, bb;
	  render.render_pixel (x/(double)scale, y/(double)scale,&rr, &gg, &bb);
	  outrow[3*x] = rr;
	  outrow[3*x+1] = gg;
	  outrow[3*x+2] = bb;
	}
      if (TIFFWriteScanline(out, outrow, y, 0) < 0)
	{
	  fprintf (stderr, "Write error on line %s\n", y);
	  exit (1);
	}
    }
#else
  render_superpose_img render (param, scan, rparam, 65535, false, false);
  render.precompute_all ();
  if (verbose)
    print_time ();
  /* Produce output file.  */
  TIFF *out= TIFFOpen(outfname, "wb");
  if (!out)
    {
      fprintf (stderr, "Can not open %s\n", outfname);
      exit (1);
    }
  int scale = 1;
  int outwidth = scan.width;
  int outheight = scan.height;
  TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth);
  TIFFSetField(out, TIFFTAG_IMAGELENGTH, outheight);
  TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  uint16_t *outrow = (uint16_t *)malloc (outwidth * 2 * 3);
  if (!outrow)
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
  for (int y = 0; y < scan.height * scale; y++)
    {
      for (int x = 0; x < scan.width * scale; x++)
	{
	  int rr, gg, bb;
	  render.render_pixel_img_antialias (x/(double)scale, y/(double)scale, 1.0 / scale, 128 ,&rr, &gg, &bb);
	  outrow[3*x] = rr;
	  outrow[3*x+1] = gg;
	  outrow[3*x+2] = bb;
	}
      if (TIFFWriteScanline(out, outrow, y, 0) < 0)
	{
	  fprintf (stderr, "Write error on line %s\n", y);
	  exit (1);
	}
    }
#endif
  if (verbose)
    print_time ();
  TIFFClose (out);
  return 0;
}
