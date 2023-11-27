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
  printf ("  ... done in %.3fs\n", time);
}

static void
print_help ()
{
  fprintf (stderr, "%s <command> [<args>]\n", binname);
  fprintf (stderr, "Supporteds command and parametrs are:\n\n");
  fprintf (stderr, "  render <scan> <pareters> <output> [<args>]\n");
  fprintf (stderr, "    render scan into tiff\n");
  fprintf (stderr, "    <scan> is image, <parametrs> is the ColorScreen parametr file\n");
  fprintf (stderr, "    <output> is tiff file to be produced\n");
  fprintf (stderr, "    Supported args:\n");
  fprintf (stderr, "      --help                    print help\n");
  fprintf (stderr, "      --verbose                 enable verbose output\n");
  fprintf (stderr, "      --mode=mode               select one of output modes:");
  for (int j = 0; j < output_mode_max; j++)
  {
    if (!(j % 4))
      fprintf (stderr, "\n                                 ");
    fprintf (stderr, " %s", render_to_file_params::output_mode_properties[j].name);
  }
  fprintf (stderr, "\n");
  fprintf (stderr, "      --hdr                     output HDR tiff\n");
  fprintf (stderr, "      --output-profile=profile  specify output profile\n");
  fprintf (stderr, "                                suported profiles:");
  for (int j = 0; j < render_parameters::output_profile_max; j++)
    fprintf (stderr, " %s", render_parameters::output_profile_names[j]);
  fprintf (stderr, "\n");
  fprintf (stderr, "      --precise                 force precise collection of patch density\n");
  fprintf (stderr, "      --detect-geometry         automatically detect screen\n");
  fprintf (stderr, "      --dye-balance=mode        force dye balance\n");
  fprintf (stderr, "      --output-gamma=gamma      set gamma correction of output file\n");
  fprintf (stderr, "                                suported modes:");
  for (int j = 0; j < render_parameters::dye_balance_max; j++)
    fprintf (stderr, " %s", render_parameters::dye_balance_names[j]);
  fprintf (stderr, "\n");
  fprintf (stderr, "\n");
  fprintf (stderr, "  analyze-backlight <scan> <output-backlight> <output-tiff> [<args>]\n");
  fprintf (stderr, "    produce backlight correction table from photo of empty backlight\n");
  fprintf (stderr, "    Supported args:\n");
  fprintf (stderr, "      --help                    print help\n");
  fprintf (stderr, "      --verbose                 enable verbose output\n");
  exit (1);
}

/* Parse output mode.  */
static enum output_mode
parse_mode (const char *mode)
{
  for (int i = 0; i < output_mode_max; i++)
    if (!strcmp (mode, render_to_file_params::output_mode_properties [i].name))
      return (output_mode)i;
  fprintf (stderr, "Unkonwn rendering mode:%s\n", mode);
  print_help ();
  return output_mode_max;
}
/* Parse output mode.  */
static enum render_parameters::output_profile_t
parse_output_profile (const char *profile)
{
  int j;
  for (j = 0; j < render_parameters::output_profile_max; j++)
    if (!strcmp (profile, render_parameters::output_profile_names[j]))
      return (render_parameters::output_profile_t)j;
  fprintf (stderr, "Unkonwn output profile:%s\n", profile);
  print_help ();
  return render_parameters::output_profile_max;
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

/* If there is --arg param or --arg=param at the command line
   position *i, return non-NULL and in the first case increment
   *i.  */

static char *
arg_with_param (int argc, char **argv, int *i, const char *arg)
{
  char *cargv=argv[*i];
  if (cargv[0]!='-' || cargv[1]!='-')
    return NULL;
  if (!strcmp (cargv + 2, arg))
    {
      if (*i == argc - 1)
	print_help ();
      (*i)++;
      return argv[*i];
    }
  size_t len = strlen (arg);
  if (!strncmp (cargv + 2, arg, len)
      && cargv[len + 2]=='=')
    return cargv + len + 3;
  return NULL;
}

static bool
parse_float_param (int argc, char **argv, int *i, const char *arg, float &val, float min, float max)
{
  const char *param = arg_with_param (argc, argv, i, arg);
  if (!param)
    return false;
  if (!sscanf (param, "%f",&val))
    {
      fprintf (stderr, "invalid parameter of %s\n", param);
      print_help ();
    }
  if (val < min || val > max)
    {
      fprintf (stderr, "parameter %s=%f is out of range %f...%f\n", arg, val, min, max);
      print_help ();
    }
  return true;
}

static void
render (int argc, char **argv)
{
  const char *infname = NULL, *cspname = NULL, *error = NULL;
  float age = -100;
  enum render_parameters::output_profile_t output_profile = render_parameters::output_profile_max;
  render_parameters::color_model_t color_model = render_parameters::color_model_max;
  render_parameters::dye_balance_t dye_balance = render_parameters::dye_balance_max;
  struct solver_parameters solver_param;
  render_to_file_params rfparams;
  bool force_precise = false;
  bool detect_geometry = false;
  float scan_dpi = 0;
  float scale = 0;
  float output_gamma = -4;


  for (int i = 0; i < argc; i++)
    {
      if (!strcmp (argv[i], "--help") || !strcmp (argv[i], "-h"))
	print_help();
      else if (!strcmp (argv[i], "--verbose") || !strcmp (argv[i], "-v"))
	verbose = true;
      else if (const char *str = arg_with_param (argc, argv, &i, "mode"))
	rfparams.mode = parse_mode (str);
      else if (!strcmp (argv[i], "--hdr"))
	rfparams.hdr = true;
      else if (const char *str = arg_with_param (argc, argv, &i, "output-profile"))
	output_profile = parse_output_profile (str);
      else if (parse_float_param (argc, argv, &i, "scan-ppi", scan_dpi, 1, 1000000)
	       || parse_float_param (argc, argv, &i, "age", age, -1000, 1000)
	       || parse_float_param (argc, argv, &i, "scale", scale, 0.0000001, 100)
	       || parse_float_param (argc, argv, &i, "output-gamma", output_gamma, 0.000001, 100))
	;
      else if (const char *str = arg_with_param (argc, argv, &i, "color-model"))
	color_model = parse_color_model (str);
      else if (!strcmp (argv[i], "--detect-geometry"))
	detect_geometry = true;
      else if (!strcmp (argv[i], "--precise"))
	force_precise = true;
      else if (const char *str = arg_with_param (argc, argv, &i, "dye-balance"))
	dye_balance = parse_dye_balance (str);
      else if (!infname)
	infname = argv[i];
      else if (!cspname)
	cspname = argv[i];
      else if (!rfparams.filename)
	rfparams.filename = argv[i];
      else
        print_help ();
    }
  if (!infname || !cspname || !rfparams.filename)
    print_help ();
  file_progress_info progress (verbose ? stdout : NULL);

  /* Load scan data.  */
  image_data scan;
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading scan %s\n", infname);
      record_time ();
      progress.resume_stdout ();
    }
  if (!scan.load (infname, false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      exit (1);
    }
  if (scan_dpi)
    scan.set_dpi (scan_dpi, scan_dpi);

  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Scan resolution %ix%i", scan.width, scan.height);
      if (scan.xdpi && scan.xdpi == scan.ydpi)
	printf (", PPI %f", scan.xdpi);
      else
	{
	  if (scan.xdpi)
	    printf (", horisontal PPI %f", scan.xdpi);
	  if (scan.ydpi)
	    printf (", vertical PPI %f", scan.ydpi);
	}
      printf ("\n");
      print_time ();
      progress.resume_stdout ();
    }
  if (rfparams.mode == detect_nearest && !scan.rgbdata)
    {
      progress.pause_stdout ();
      fprintf (stderr, "Screen detection is imposible in monochromatic scan\n");
      exit (1);
    }

  /* Load color screen and rendering parameters.  */
  scr_to_img_parameters param;
  render_parameters rparam;
  scr_detect_parameters dparam;
  FILE *in = fopen (cspname, "rt");
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading color screen parameters: %s\n", cspname);
      progress.resume_stdout ();
    }
  if (!in)
    {
      progress.pause_stdout ();
      perror (cspname);
      exit (1);
    }
  if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);
  if (force_precise)
    rparam.precise = true;

  if (detect_geometry && scan.rgbdata)
    {
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Detecting geometry\n");
	  record_time ();
	  progress.resume_stdout ();
	}
      abort ();
      // TODO update call once it is clear what we want to do.
      //detected_screen d = detect_regular_screen (scan, dparam, rparam.gamma, solver_param, false, false, false, NULL);
      //param.mesh_trans = d.mesh_trans;
    }
  else if (solver_param.npoints
	   && !param.mesh_trans)
    {
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Computing mesh\n");
	  record_time ();
	  progress.resume_stdout ();
	}
      param.mesh_trans = solver_mesh (&param, scan, solver_param);
      if (verbose)
	{
	  progress.pause_stdout ();
	  print_time ();
	  progress.resume_stdout ();
	}
    }

  /* Apply command line parameters.  */
  if (age != -100)
    rparam.age = age;
  if (color_model != render_parameters::color_model_max)
    rparam.color_model = color_model;
  if (dye_balance != render_parameters::dye_balance_max)
    rparam.dye_balance = dye_balance;
  if (output_profile != render_parameters::output_profile_max)
    rparam.output_profile = output_profile;
  if (output_gamma != -4)
    rparam.output_gamma = output_gamma;
  if (scale)
    rfparams.scale = scale;

  /* ... and render!  */
  rfparams.verbose = verbose;
  if (!render_to_file (scan, param, dparam, rparam, rfparams, &progress, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not save %s: %s\n", rfparams.filename, error);
      exit (1);
    }
  exit (0);
}

void
analyze_backlight (int argc, char **argv)
{
  const char *error = NULL;

  printf ("%i\n",argc);
  if (argc < 2 || argc > 3)
    print_help ();
  file_progress_info progress (verbose ? stdout : NULL);
  image_data scan;
  if (!scan.load (argv[0], false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", argv[0], error);
      exit (1);
    }
  backlight_correction_parameters *cor = backlight_correction_parameters::analyze_scan (scan, 1.0);
  FILE *out = fopen (argv[1], "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror ("Can not open output file");
      exit (1);
    }
  if (!cor->save (out))
    {
      fprintf (stderr, "Can not write %s\n", argv[1]);
      exit (1);
    }
  fclose (out);
  if (argc == 3)
    {
      error = cor->save_tiff (argv[2]);
      if (error)
	{
	  fprintf (stderr, "Failed to save output file: %s\n", error);
	  exit (1);
	}
    }
  delete cor;
}

void
export_lcc (int argc, char **argv)
{
  const char *error = NULL;

  printf ("%i\n",argc);
  if (argc < 2 || argc > 3)
    print_help ();
  file_progress_info progress (verbose ? stdout : NULL);
  image_data scan;
  if (!scan.load (argv[0], false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", argv[0], error);
      exit (1);
    }
  if (!scan.lcc)
    {
      progress.pause_stdout ();
      fprintf (stderr, "No PhaseOne LCC in scan: %s\n", argv[0]);
      exit (1);
    }
  backlight_correction_parameters *cor = scan.lcc;
  FILE *out = fopen (argv[1], "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror ("Can not open output file");
      exit (1);
    }
  if (!cor->save (out))
    {
      fprintf (stderr, "Can not write %s\n", argv[1]);
      exit (1);
    }
  fclose (out);
  if (argc == 3)
    {
      error = cor->save_tiff (argv[2]);
      if (error)
	{
	  fprintf (stderr, "Failed to save output file: %s\n", error);
	  exit (1);
	}
    }
  delete cor;
}

int
main (int argc, char **argv)
{
  binname = argv[0];
  if (argc == 1)
    print_help ();
  if (!strcmp (argv[1], "render"))
    render (argc-2, argv+2);
  if (!strcmp (argv[1], "analyze-backlight"))
    analyze_backlight (argc-2, argv+2);
  if (!strcmp (argv[1], "export-lcc"))
    export_lcc (argc-2, argv+2);
  else
    print_help ();
  return 0;
}
