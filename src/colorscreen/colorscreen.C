#include <sys/time.h>
#include "../libcolorscreen/include/colorscreen.h"

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

static void
print_help ()
{
  fprintf (stderr, "%s <scan> <config>.csp <out>.tif\n", binname);
  exit (1);
}

/* Parse output mode.  */
static enum output_mode
parse_mode (const char *mode)
{
  if (!strcmp (mode, "none"))
    return none;
  else if (!strcmp (mode, "realistic"))
    return realistic;
  else if (!strcmp (mode, "preview-grid"))
    return preview_grid;
  else if (!strcmp (mode, "color-preview-grid"))
    return color_preview_grid;
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
static enum render_parameters::color_model_t
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

static enum render_parameters::dye_balance_t
parse_dye_balance (const char *model)
{
  int j;
  for (j = 0; j < render_parameters::dye_balance_max; j++)
    if (!strcmp (model, render_parameters::dye_balance_names[j]))
      return (render_parameters::dye_balance_t)j;
  fprintf (stderr, "Unkonwn dye balancel:%s\n", model);
  print_help ();
  return render_parameters::dye_balance_max;
}

int
main (int argc, char **argv)
{
  const char *infname = NULL, *outfname = NULL, *cspname = NULL, *error = NULL;
  enum output_mode mode = interpolated;
  float age = -100;
  render_parameters::color_model_t color_model = render_parameters::color_model_max;
  render_parameters::dye_balance_t dye_balance = render_parameters::dye_balance_max;
  struct solver_parameters solver_param;
  bool force_precise = false;

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
      else if (!strcmp (argv[i], "--precise"))
	force_precise = true;
      else if (!strcmp (argv[i], "--dye-balance"))
	{
	  if (i == argc - 1)
	    print_help ();
	  i++;
	  dye_balance = parse_dye_balance (argv[i]);
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
  if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);
  if (force_precise)
    rparam.precise = true;

  if (solver_param.npoints)
    {
      if (verbose)
	{
	  printf ("Computing mesh");
	  record_time ();
	}
      param.mesh_trans = solver_mesh (&param, scan, solver_param);
      if (verbose)
	print_time ();
    }

  /* Apply command line parameters.  */
  if (age != -100)
    rparam.age = age;
  if (color_model != render_parameters::color_model_max)
    rparam.color_model = color_model;
  if (dye_balance != render_parameters::dye_balance_max)
    rparam.dye_balance = dye_balance;

  /* ... and render!  */
  if (!render_to_file (mode, outfname, scan, param, dparam, rparam, NULL, verbose, &error))
    {
      fprintf (stderr, "Can not save %s: %s\n", outfname, error);
      exit (1);
    }
  if (verbose)
    print_time ();
  return 0;
}
