#include <tiffio.h>
#include "../libcolorscreen/include/colorscreen.h"
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
  FILE *in = fopen (infname, "r");
  if (!in)
    {
      perror (infname);
      exit (1);
    }
  image_data scan;
  if (verbose)
    printf ("Loading: %s\n", infname);
  if (!scan.load (infname, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      exit (1);
    }
  fclose (in);

  /* Load color screen and rendering parameters.  */
  scr_to_img_parameters param;
  render_parameters rparam;
  in = fopen (cspname, "rt");
  if (verbose)
    printf ("Loading: %s\n", cspname);
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
    printf ("Precomputing\n");

  /* Initialize rendering engine.  */

  render_interpolate render (param, scan, rparam, 65535);
  render.precompute_all ();
  /* Produce output file.  */
  TIFF *out= TIFFOpen("new.tif", "w");
  if (!out)
    {
      fprintf (stderr, "Can not open %s\n", outfname);
      exit (1);
    }
  int scale = 4;
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
    printf ("Rendering %s %ix%i\n", outfname, outwidth, outheight);
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
  TIFFClose (out);
  return 0;
}
