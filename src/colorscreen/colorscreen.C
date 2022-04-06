#include "../libcolorscreen/include/colorscreen.h"
int
main (int argc, char **argv)
{
  const char *infname = NULL, *outfname = NULL, *cspname = NULL, *error = NULL;
  bool verbose = 1;
  if (argc != 4)
    {
      fprintf (stderr, "%s <scan>.pgm <config>.csp <out>.pnm", argv[0]);
      exit (1);
    }
  infname = argv[1];
  cspname = argv[2];
  outfname = argv[3];

  FILE *in = fopen (infname, "r");
  if (!in)
    {
      perror (infname);
      exit (1);
    }
  image_data scan;
  if (verbose)
    printf ("Loading: %s\n", infname);
  if (!scan.load_pnm (in, NULL, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      exit (1);
    }
  fclose (in);

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
  render_interpolate render (param, scan, rparam, 65535);
  render.precompute_all ();
  FILE *out = fopen (outfname, "w");
  if (!out)
    {
      fprintf (stderr, "Can not open %s: %s\n", cspname, error);
      exit (1);
    }
  if (verbose)
    printf ("Rendering\n");
  int scale = 4;
  pixel *outrow = ppm_allocrow (render.get_width () * scale);
  ppm_writeppminit (out, render.get_width () * scale, render.get_height() * scale, 65535, 0);
  for (int y = 0; y < render.get_height () * scale; y++)
    {
      for (int x = 0; x < render.get_width () * scale; x++)
	{
	  int rr, gg, bb;
	  render.render_pixel (x/(double)scale, y/(double)scale,&rr, &gg, &bb);
	  outrow[x].r = rr;
	  outrow[x].g = gg;
	  outrow[x].b = bb;
	}
      ppm_writeppmrow (out, outrow, render.get_width() * scale, 65535, 0);
    }
  free (outrow);
  fclose (out);

  return 0;
}
