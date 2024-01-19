#include <sys/time.h>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/spectrum-to-xyz.h"
#include "../libcolorscreen/dufaycolor.h"
#include "../libcolorscreen/wratten.h"

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
  fprintf (stderr, "Supported commands and parameters are:\n\n");
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
  fprintf (stderr, "      --dng                     output DNG\n");
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
  fprintf (stderr, "  lab <subcommnad>\n");
  fprintf (stderr, "    various commands useful for testing.  Supported commands are:\n");
  fprintf (stderr, "      dufay-xyY: print report about Dufaycolor resau xyY table from Color Cinematography book\n");
  fprintf (stderr, "      dufay-spectra: print report about Dufaycolor resau spectra\n");
  fprintf (stderr, "      dufay-synthetic: print report about mixing Dufaycolor reseau from known dyes\n");
  fprintf (stderr, "      wratten-xyz: print report about Wratten trichromatic set xyY values\n");
  fprintf (stderr, "      wratten-spectra: print report about Wratten trichromatic set spectra\n");
  fprintf (stderr, "      save-film-sensitivity: save film sensitivity curve\n");
  fprintf (stderr, "      save-film-log-sensitivity: save film sensitivity curve with log sensitivity (wedge spectrogram)\n");
  fprintf (stderr, "      save-film-linear-characteristic-curve: save film characteristic curve\n");
  fprintf (stderr, "      save-film-hd-characteristic-curve: save film characteristic H&D (Hurter and Driffield) curve\n");
  fprintf (stderr, "      save-dyes: save spectra of dyes\n");
  fprintf (stderr, "      save-ssf-jason: save spectral sensitivity functions to jsason file for dcamprof\n");
  fprintf (stderr, "      render-target: render color target\n");
  fprintf (stderr, "      render-wb-target: render color target with auto white balance\n");
  fprintf (stderr, "      render-optimized-target: render color target with optimized camera matrix\n");
  fprintf (stderr, "      render-spectra-photo: render photo of spectrum taken over filters\n");
  fprintf (stderr, "      render-tone-curve: save tone curve in linear gamma\n");
  fprintf (stderr, "      render-tone-hd-curve: save tone curve as hd curve\n");
  fprintf (stderr, "    Each subcommand has its own help.\n");
  fprintf (stderr, "  read-chemcad-spectra <out_filename> <in_filename>\n");
  fprintf (stderr, "    read spectrum in checad database format and output it in format that can be built into libcolorscreen\n");
  exit (1);
}

template<typename T,const char *names[],int max>
T
parse_enum (const char *arg, const char *errmsg)
{
  for (int i = 0; i < max; i++)
    if (!strcmp (arg, names[i]))
      return (T)i;
  fprintf (stderr, errmsg, arg);
  fprintf (stderr, "Possible values are: ");
  for (int i = 0; i < max; i++)
    fprintf (stderr, " %s", names[i]);
  fprintf (stderr, "\n");
  exit (1);
  return (T)max;
}

/* Parse output mode.  */
static enum output_mode
parse_mode (const char *mode)
{
  for (int i = 0; i < output_mode_max; i++)
    if (!strcmp (mode, render_to_file_params::output_mode_properties[i].name))
      return (output_mode)i;
  fprintf (stderr, "Unkonwn rendering mode:%s\n", mode);
  fprintf (stderr, "Possible values are: ");
  for (int i = 0; i < output_mode_max; i++)
    fprintf (stderr, " %s", render_to_file_params::output_mode_properties[i].name);
  exit (1);
  return output_mode_max;
}

/* Parse output mode.  */
static enum render_parameters::output_profile_t
parse_output_profile (const char *profile)
{
  return parse_enum<enum render_parameters::output_profile_t,
		    render_parameters::output_profile_names,
		    (int)render_parameters::output_profile_max> (profile, "Unkonwn output profile:%s\n");
}

/* Parse color model.  */
static enum render_parameters::color_model_t
parse_color_model (const char *model)
{
  return parse_enum<enum render_parameters::color_model_t,
		    render_parameters::color_model_names,
		    (int)render_parameters::color_model_max> (model, "Unkonwn color model:%s\n");
}

static enum render_parameters::dye_balance_t
parse_dye_balance (const char *model)
{
  return parse_enum<enum render_parameters::dye_balance_t,
		    render_parameters::dye_balance_names,
		    (int)render_parameters::dye_balance_max> (model, "Unkonwn dye balance:%s\n");
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
      else if (!strcmp (argv[i], "--dng"))
	rfparams.dng = true;
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

void
read_chemcad (int argc, char **argv)
{
  FILE *f;
  spectrum s;
  spectrum sum;
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    s[i] = sum[i] = 0;
  if (argc == 0)
    f = stdin;
  if (argc > 1)
    print_help ();
  if (argc == 1)
    f = fopen (argv[0], "rt");
  if (!f)
    {
      perror ("can not open input file");
      exit (1);
    }
  while (getc (f) != '\n')
    if (feof (f))
      {
        fprintf (stderr, "parse error\n");
	exit (1);
      }
  while (!feof (f))
    {
      float b,v;
      scanf ("%f %f\n",&b,&v);
      //printf ("%f %f\n",b,v);
      int band = nearest_int ((b - SPECTRUM_START) / SPECTRUM_STEP);
      if (band >= 0 && band < SPECTRUM_SIZE)
        {
	  s[band] += v;
	  sum[band]++;
        }
    }
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    {
      float v = 0;
      if (sum[i])
	v = s[i] / sum[i];
      if (!(i%10))
	printf ("\n  ");
      if (i != SPECTRUM_SIZE - 1)
        printf ("%f, ", v);
      else
	printf ("%f\n", v);
    }
}

static enum spectrum_dyes_to_xyz::dyes
parse_dyes (const char *profile)
{
  return parse_enum<enum spectrum_dyes_to_xyz::dyes,
		    (const char **)spectrum_dyes_to_xyz::dyes_names,
		    (int)spectrum_dyes_to_xyz::dyes_max> (profile, "Unkonwn dye:%s\n");
}
static enum spectrum_dyes_to_xyz::illuminants
parse_illuminant (const char *il, luminosity_t *temperature)
{
  *temperature = 6500;
  if ((il[0] == 'D' || il[0] == 'd') && strlen (il) == 3
      && il[1]>'0' && il[1]<='9' && il[2]>='0' && il[2] <= '9')
    {
      *temperature = (il[1] - '0') * 1000 + (il[2] - '0') * 100;
      return spectrum_dyes_to_xyz::il_D;
    }
  if (strlen(il) == 3
      && il[0] >= '3' && il[0]<='7'
      && il[1] >= '0' && il[1]<='9'
      && il[2] >= '0' && il[2]<='9')
    {
      *temperature = atoi (il);
      return spectrum_dyes_to_xyz::il_band;
    }
  for (int i = 0; i < (int)spectrum_dyes_to_xyz::illuminants_max; i++)
    if (!strcmp (il, spectrum_dyes_to_xyz::illuminants_names[i])
	&& (spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_D
	&& (spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_band)
      return (spectrum_dyes_to_xyz::illuminants)i;
  fprintf (stderr, "Unknown illuminant %s\n", il);
  fprintf (stderr, "Possible values are: D[0-9][0-9] for daylight, [3-7][0-9][0-9] for single band");
  for (int i = 0; i < (int)spectrum_dyes_to_xyz::illuminants_max; i++)
    if ((spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_D
        && (spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_band)
    fprintf (stderr, ", %s", spectrum_dyes_to_xyz::illuminants_names[i]);
  fprintf (stderr, "\n");
  exit (1);
}
static enum tone_curve::tone_curves
parse_tone_curve (const char *profile)
{
  return parse_enum<enum tone_curve::tone_curves,
		    (const char **)tone_curve::tone_curve_names,
		    (int)tone_curve::tone_curve_max> (profile, "Unkonwn tone curve:%s\n");
}
static enum spectrum_dyes_to_xyz::responses
parse_response (const char *profile)
{
  return parse_enum<enum spectrum_dyes_to_xyz::responses,
		    (const char **)spectrum_dyes_to_xyz::responses_names,
		    (int)spectrum_dyes_to_xyz::responses_max> (profile, "Unkonwn film response:%s\n");
}
static enum spectrum_dyes_to_xyz::characteristic_curves
parse_characteristic_curve (const char *profile)
{
  return parse_enum<enum spectrum_dyes_to_xyz::characteristic_curves,
		    (const char **)spectrum_dyes_to_xyz::characteristic_curve_names,
		    (int)spectrum_dyes_to_xyz::characteristic_curves_max> (profile, "Unkonwn film characteristic curve:%s\n");
}

void
parse_filename_and_camera_setup (int argc, char **argv, const char **filename, spectrum_dyes_to_xyz &spec, bool patch_sizes = false)
{
  if (argc != 5 + (patch_sizes ? 3 : 0)
      && (!patch_sizes || (argc != 6 || strcmp (argv[5],"dufay"))))
    {
      fprintf (stderr, "Expected parameters <filename> <backlight> <dyes> <film-sensitivity> <film-characteristic-cuve>%s\n",
	       patch_sizes ? " <rscal> <gscale> <bscale>":"");
      if (patch_sizes)
	fprintf (stderr, "last three options can be also replaced by \"dufay\"\n");
      print_help ();
    }
  *filename = argv[0];
  luminosity_t temperature;
  auto il = parse_illuminant (argv[1], &temperature);
  spec.set_backlight (il, temperature);
  spec.set_dyes (parse_dyes (argv[2]));
  spec.set_film_response (parse_response (argv[3]));
  spec.set_characteristic_curve (parse_characteristic_curve (argv[4]));
  if (patch_sizes && argc != 6)
    {
      spec.rscale = atof (argv[5]);
      spec.gscale = atof (argv[6]);
      spec.bscale = atof (argv[7]);
    }
  else if (patch_sizes)
    {
      spec.rscale = dufaycolor::red_portion;
      spec.gscale = dufaycolor::green_portion;
      spec.bscale = dufaycolor::blue_portion;
    }
}

void
digital_laboratory (int argc, char **argv)
{
  if (argc == 1 && !strcmp (argv[0], "dufay-xyY"))
    dufaycolor::print_xyY_report ();
  else if (argc == 1 && !strcmp (argv[0], "dufay-spectra"))
    dufaycolor::print_spectra_report ();
  else if (argc == 1 && !strcmp (argv[0], "dufay-synthetic"))
    dufaycolor::print_synthetic_dyes_report ();
  else if (argc == 1 && !strcmp (argv[0], "wratten-xyz"))
    wratten::print_xyz_report ();
  else if (argc == 1 && !strcmp (argv[0], "wratten-spectra"))
    wratten::print_spectra_report ();
  else if (!strcmp (argv[0], "save-film-sensitivity"))
    {
      if (argc != 3 && argc != 4)
	{
	  printf ("Expected <emulsion> [<illuminant>]\n");
	  print_help ();
	}
      spectrum_dyes_to_xyz spec;
      spec.set_film_response (parse_response (argv[2]));
      if (argc == 4)
	{
	  luminosity_t temperature;
	  auto il = parse_illuminant (argv[3], &temperature);
	  spec.set_backlight (il, temperature);
	}
      spec.write_film_response (argv[1], NULL, argc == 3 ? true : false, false);
    }
  else if (!strcmp (argv[0], "save-film-log-sensitivity"))
    {
      if (argc != 3 && argc != 4)
	{
	  printf ("Expected <emulsion> [<illuminant>]\n");
	  print_help ();
	}
      spectrum_dyes_to_xyz spec;
      spec.set_film_response (parse_response (argv[2]));
      if (argc == 4)
	{
	  luminosity_t temperature;
	  auto il = parse_illuminant (argv[3], &temperature);
	  spec.set_backlight (il, temperature);
	}
      spec.write_film_response (argv[1], NULL, argc == 3 ? true : false, true);
    }
  else if (!strcmp (argv[0], "save-film-linear-characteristic-curve"))
    {
      if (argc != 3 && argc != 5)
	{
	  printf ("Expected <red-file> <green-file> <blue-file> <film-characterstic-curve>\n");
	  printf ("         or <red-file> <film-characterstic-curve>\n");
	  print_help ();
	}
      spectrum_dyes_to_xyz spec;
      spec.set_characteristic_curve (parse_characteristic_curve (argv[argc-1]));
      spec.write_film_characteristic_curves (argv[1], argc==5 ? argv[2] : NULL, argc==5 ? argv[3] : NULL);
    }
  else if (!strcmp (argv[0], "save-film-hd-characteristic-curve"))
    {
      if (argc != 3 && argc != 5)
	{
	  printf ("Expected <red-file> <green-file> <blue-file> <film-characterstic-curve>\n");
	  printf ("         or <red-file> <film-characterstic-curve>\n");
	  print_help ();
	}
      spectrum_dyes_to_xyz spec;
      spec.set_characteristic_curve (parse_characteristic_curve (argv[argc-1]));
      spec.write_film_hd_characteristic_curves (argv[1], argc==5 ? argv[2] : NULL, argc==5 ? argv[3] : NULL);
    }
  else if (!strcmp (argv[0], "save-illuminant"))
    {
      if (argc != 3)
	{
	  printf ("Expected <illuminat_filename> <illuminant>\n");
	  print_help ();
	}
      spectrum_dyes_to_xyz spec;
      luminosity_t temperature;
      auto il = parse_illuminant (argv[2], &temperature);
      spec.set_backlight (il, temperature);
      spec.write_spectra (NULL, NULL, NULL, argv[1]);
    }
  else if (!strcmp (argv[0], "save-dyes"))
    {
      if (argc != 5)
	{
	  printf ("Expected <red_filename> <green_filename> <blue_filename> <dyes>\n");
	  exit (1);
	}
      spectrum_dyes_to_xyz spec;
      spec.set_dyes (parse_dyes (argv[4]));
      printf ("Saving to %s %s %s\n", argv[1], argv[2], argv[3]);
      spec.write_spectra (argv[1], argv[2], argv[3], NULL);
    }
  else if (!strcmp (argv[0], "save-responses"))
    {
      if (argc != 6)
	{
	  printf ("Expected <red_filename> <green_filename> <blue_filename> <dyes> <respnse>\n");
	  exit (1);
	}
      spectrum_dyes_to_xyz spec;
      spec.set_dyes (parse_dyes (argv[4]));
      spec.set_film_response (parse_response (argv[5]));
      printf ("Saving to %s %s %s\n", argv[1], argv[2], argv[3]);
      spec.write_responses (argv[1], argv[2], argv[3], false);
    }
  else if (!strcmp (argv[0], "save-ssf-json"))
    {
      if (argc != 4)
	{
	  printf ("Expected <filename> <dyes> <respnse>\n");
	  exit (1);
	}
      spectrum_dyes_to_xyz spec;
      spec.set_dyes (parse_dyes (argv[2]));
      spec.set_film_response (parse_response (argv[3]));
      spec.write_ssf_json (argv[1]);
    }
  else if (!strcmp (argv[0], "render-target"))
    {
      spectrum_dyes_to_xyz spec;
      const char *filename;
      const char *error;

      parse_filename_and_camera_setup (argc - 1, argv + 1, &filename, spec, true);
      spec.generate_color_target_tiff (filename, &error, false, false);
    }
  else if (!strcmp (argv[0], "render-wb-target"))
    {
      spectrum_dyes_to_xyz spec;
      const char *filename;
      const char *error;

      parse_filename_and_camera_setup (argc - 1, argv + 1, &filename, spec);
      spec.generate_color_target_tiff (filename, &error, true, false);
    }
  else if (!strcmp (argv[0], "render-optimized-target"))
    {
      spectrum_dyes_to_xyz spec;
      const char *filename;
      const char *error;

      parse_filename_and_camera_setup (argc - 1, argv + 1, &filename, spec);
      if (!spec.generate_color_target_tiff (filename, &error, true, true))
	{
	  fprintf (stderr, "%s\n", error);
	  exit (1);
	}
    }
  else if (!strcmp (argv[0], "render-spectra-photo"))
    {
      spectrum_dyes_to_xyz spec;
      const char *filename;
      parse_filename_and_camera_setup (argc - 1, argv + 1, &filename, spec);
      if (!spec.tiff_with_spectra_photo (filename))
	{
	  fprintf (stderr, "Error writting %s\n", filename);
	  exit (1);
	}
    }
  else if (!strcmp (argv[0], "save-tone-curve"))
    {
      if (argc != 3)
	{
	  printf ("Expected <filename> <tone-curve>\n");
	  exit (1);
	}
      tone_curve c(parse_tone_curve(argv[2]));
      FILE *f = fopen (argv[1], "wt");
      if (!f)
        {
	  perror (argv[1]);
	  exit(1);
        }
      for (float i = 0; i < 1; i+= 0.01)
	fprintf (f, "%f %f\n", i, (float)c.apply_to_rgb((rgbdata){i,i,i}).red);
      fclose (f);
    }
  else if (!strcmp (argv[0], "save-tone-hd-curve"))
    {
      if (argc != 3)
	{
	  printf ("Expected <filename> <tone-curve>\n");
	  exit (1);
	}
      tone_curve c(parse_tone_curve(argv[2]));
      FILE *f = fopen (argv[1], "wt");
      if (!f)
        {
	  perror (argv[1]);
	  exit(1);
        }
      for (float i = 0; i < 1; i+= 0.001)
        {
	  luminosity_t val = c.apply_to_rgb((rgbdata){i,i,i}).red;
	  if (val >= 1/255.0)
	    fprintf (f, "%f %f\n", log10(i)/*-log10 (0.001)*/, log10 (1/val));
        }
#if 0
      for (float i = -2; i < 2; i+= 0.01)
        {
	    fprintf (f, "%f %f\n", i, c.apply_to_rgb((rgbdata){i,i,i}).red);
#if 0
	  float ii = pow (10, i);
	  luminosity_t val = c.apply_to_rgb((rgbdata){ii,ii,ii}).red;
	  if (val >= 1/255.0)
	    fprintf (f, "%f %f\n", log10(ii)/*-log10 (0.001)*/, log10 (1/val));
#endif
        }
#endif
      fclose (f);
    }
  else
    print_help ();
}

int
main (int argc, char **argv)
{
  binname = argv[0];
  if (argc == 1)
    print_help ();
  else if (!strcmp (argv[1], "render"))
    render (argc-2, argv+2);
  else if (!strcmp (argv[1], "analyze-backlight"))
    analyze_backlight (argc-2, argv+2);
  else if (!strcmp (argv[1], "export-lcc"))
    export_lcc (argc-2, argv+2);
  else if (!strcmp (argv[1], "digital-laboratory")
	   || !strcmp (argv[1], "lab"))
    digital_laboratory (argc-2, argv+2);
  else if (!strcmp (argv[1], "read-chemcad-spectra"))
    read_chemcad (argc-2, argv+2);
  else
    print_help ();
  return 0;
}
