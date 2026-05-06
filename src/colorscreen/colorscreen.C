/* Color-Screen command line utility
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <string>
#include <string_view>
#include <charconv>
#include <unistd.h>
#include <sys/time.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef WIN32
#include <io.h>
#define F_OK 0
#define access _access
#endif

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-scanner-blur.h"
#include "../libcolorscreen/include/dufaycolor.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/histogram.h"
#include "../libcolorscreen/include/spectrum-to-xyz.h"
#include "../libcolorscreen/include/stitch.h"
#include "../libcolorscreen/include/tiff-writer.h"
#include "../libcolorscreen/include/wratten.h"
/* For PACKAGE_VERSION.  */
#include "../libcolorscreen/config.h"

using namespace colorscreen;

static bool verbose = false;
static bool verbose_tasks = false;
const char *binname;

static enum subhelp {
  help_basic,
  help_render,
  help_autodetect,
  help_analyze_backlight,
  help_analyze_scanner_blur,
  help_dump_lcc,
  help_stitch,
  help_dump_patch_density,
  help_finetune,
  help_lab,
  help_read_chemcad_spectra,
  help_has_regular_screen,
  help_mtf,
  help_slanted_edge
} subhelp
    = help_basic;

static void
print_help (char *err = NULL)
{
  if (err)
    fprintf (stderr, "Unknown parameter %s\n", err);
  fprintf (stderr, "%s [<args>] <command>\n", binname);
  fprintf (stderr, "Supported commands and arguments are:\n\n");
  fprintf (stderr, "  Supported common args:\n");
  fprintf (stderr, "      --help                    print help\n");
  fprintf (stderr, "      --verbose                 enable verbose output\n");
  fprintf (stderr, "      --version                 print version\n");
  fprintf (stderr, "      --threads=n               set number of threads\n");
  fprintf (stderr, "      --time-report             report time spent in tasks\n");
  if (subhelp == help_slanted_edge || subhelp == help_basic)
    {
      fprintf (stderr, "  slanted-edge <image-file> [<args>]\n");
      fprintf (stderr, "    run slanted edge analysis on an image\n");
    }
  if (subhelp == help_slanted_edge)
    {
      fprintf (stderr, "    Supported args:\n");
      fprintf (stderr, "      --gamma=val               set gamma correction of input file (default 1.0)\n");
      fprintf (stderr, "      --save-mtf=file           save measured MTF to a file in QuickMTF format\n");
      fprintf (stderr, "      --compare-with=file tol   compare measured MTF with reference and fail if difference > tol\n");
    }
  if (subhelp == help_render || subhelp == help_basic)
    {
      fprintf (stderr, "  render <scan> <parameters> <output> [<args>]\n");
      fprintf (stderr, "    render scan into tiff\n");
    }
  if (subhelp == help_render)
    {
      fprintf (stderr, "    <scan> is image, <parameters> is the ColorScreen "
                       "parameter file\n");
      fprintf (stderr, "    <output> is tiff file to be produced\n");
      fprintf (stderr, "    Supported args:\n");
      fprintf (stderr, "      --mode=mode               select one of output modes:");
      for (int j = 0; j < render_type_max; j++)
        {
          if (!(j % 4))
            fprintf (stderr, "\n                                 ");
          fprintf (stderr, " %s", render_type_properties[j].name);
        }
      fprintf (stderr, "\n");
      fprintf (stderr, "      --solver                  run solver if solver "
                       "points are present\n");
      fprintf (stderr, "      --hdr                     output HDR tiff\n");
      fprintf (stderr, "      --dng                     output DNG\n");
      fprintf (stderr, "      --output-profile=profile  specify output profile\n");
      fprintf (stderr, "                                supported profiles:");
      for (int j = 0; j < render_parameters::output_profile_max; j++)
        fprintf (stderr, " %s", render_parameters::output_profile_names[j]);
      fprintf (stderr, "\n");
      fprintf (stderr, "      --geometry=scan|screen    specify output file geometry\n");
      fprintf (stderr, "      --antialias=N             specify aliasing using NxN grid\n");
      fprintf (stderr, "      --detect-geometry         automatically detect screen\n");
      fprintf (stderr, "      --auto-color-model        automatically choose "
                       "color model for given screen type\n");
      fprintf (stderr, "      --auto-levels             automatically choose "
                       "brightness and dark point\n");
      fprintf (stderr, "      --dye-balance=mode        force dye balance\n");
      fprintf (stderr, "                                supported modes:");
      for (int j = 0; j < (int)render_parameters::dye_balance_max; j++)
        fprintf (stderr, " %s", render_parameters::dye_balance_names[j].name);
      fprintf (stderr, "\n");
      fprintf (stderr, "      --output-gamma=gamma      set gamma correction "
                       "of output file\n");
      fprintf (stderr, "      --scan-ppi=val            specify resolution of scan\n");
      fprintf (stderr, "      --age=val                 specify age of color model\n");
      fprintf (stderr, "      --scale=val               specify scale of output file\n");
      fprintf (stderr, "      --screen-scale=val        specify scale of output file relative to screen dimensions\n");
      fprintf (stderr, "      --ignore-infrared         force use of simulated IR channel\n");
    }
  if (subhelp == help_autodetect || subhelp == help_basic)
    {
      fprintf (stderr, "  autodetect <scan> <output par> [<args>]\n");
      fprintf (stderr, "    automatically detect geometry of scan and write "
                       "it to output.\n");
    }
  if (subhelp == help_autodetect)
    {
      fprintf (stderr, "      --par=name.par            load parameters\n");
      fprintf (stderr, "      --report=name.txt         save report\n");
      fprintf (stderr, "      --auto-color-model        automatically choose "
                       "color model for given screen type\n");
      fprintf (stderr, "      --no-auto-color-model     do not choose color "
                       "model for given screen type\n");
      fprintf (stderr, "      --auto-levels             automatically choose "
                       "brightness and dark point\n");
      fprintf (stderr, "      --no-auto-levels          do not choose "
                       "brightness and dark point\n");
    }
  if (subhelp == help_analyze_backlight || subhelp == help_basic)
    {
      fprintf (stderr, "  analyze-backlight <scan> <output-backlight> "
                       "<output-tiff> [<args>]\n");
      fprintf (stderr, "    produce backlight correction table from photo of "
                       "empty backlight\n");
    }
  if (subhelp == help_analyze_scanner_blur || subhelp == help_basic)
    {
      fprintf (stderr, "  analyze-scanner-blur <scan> <par> [<args>]\n");
      fprintf (stderr, "    produce scanner blur correction table and save it into <par>\n");
    }
  if (subhelp == help_analyze_scanner_blur)
    {
      fprintf (stderr, "    Supported args:\n");
      fprintf (stderr, "      --out=name.par            save parameters to a given file instead of overwriting original\n");
      fprintf (stderr, "      --out-tiff=name.tif       save resulting table also as tiff file\n");
      fprintf (stderr, "      --strip-width=n           number of horizontal samples used to detec strip widths\n");
      fprintf (stderr, "      --strip-height=n          number of vertical samples used to detect strip widths\n");
      fprintf (stderr, "      --reoptimize-strip-widths optimize strip widths also during blur detection\n");
      fprintf (stderr, "      --width=n                 width of the correction table\n");
      fprintf (stderr, "      --height=n                height of the correction table\n");
      fprintf (stderr, "      --xsamples=n              number of horizontal samples to analyze for every entry in table\n");
      fprintf (stderr, "      --ysamples=n              number of vertical samples to analyze for every entry in table\n");
      fprintf (stderr, "      --tolerance=max           maximal difference between minimal and maximal blur radius in robust average\n");
      fprintf (stderr, "      --optimize-fog            enable finetuning of fog (dark point)\n");
      fprintf (stderr, "      --simulate-infrared       simuate infrared layer\n");
      fprintf (stderr, "      --normalize               normalize colors\n");
      fprintf (stderr, "      --no-normalize            do not normalize colors\n");
      fprintf (stderr, "      --no-data-collection      do not determine colors by data collection\n");
    }
  if (subhelp == help_dump_lcc || subhelp == help_basic)
    {
      fprintf (stderr, "  dump-lcc <filename>\n");
      fprintf (stderr, "    dump annotated contents of CaptureOne LCC file\n");
    }
  if (subhelp == help_stitch || subhelp == help_basic)
    {
      fprintf (stderr, "  stitch <parameters> <tiles> [<args>]\n");
      fprintf (stderr, "    produce stitched project\n");
    }
  if (subhelp == help_stitch)
    {
      fprintf (stderr, "    Supported args:\n");
      fprintf (stderr, "      --par=filename.par         load given screen "
                       "discovery and rendering parameters\n");
      fprintf (stderr, "      --ncols=n                  number of columns of tiles\n");
      fprintf (stderr, "      --load-project=name.par    store analysis to a "
                       "project file\n");
      fprintf (stderr, "      --scan-ppi=scan-ppi        PPI of input scan\n");
      fprintf (stderr, "      --load-registration        load registration "
                       "from corresponding par files\n");
      fprintf (stderr, "     output files:\n");
      fprintf (stderr, "      --report=name.txt          store report about "
                       "stitching operation to a file\n");
      fprintf (stderr, "      --out=name.csprj           store analysis to a "
                       "project file\n");
      fprintf (stderr, "      --hugin-pto=name.pto       store project file for hugin\n");
      fprintf (stderr, "     tiles to ouptut:\n");
      fprintf (stderr, "      --screen-tiles             store screen tiles "
                       "(for verification)\n");
      fprintf (stderr, "      --known-screen-tiles       store screen tiles "
                       "where unanalyzed pixels are transparent\n");
      fprintf (stderr, "     overlap detection:\n");
      fprintf (stderr, "      --cpfind                   enable use of "
                       "Hugin's cpfind to determine overlap\n");
      fprintf (stderr, "      --no-cpfind                disable use of "
                       "Hugin's cpfind to determine overlap\n");
      fprintf (stderr, "      --cpfind-verification      use cpfind to verify "
                       "results of internal overlap detection\n");
      fprintf (stderr, "      --min-overlap=precentage   minimal overlap\n");
      fprintf (stderr, "      --max-overlap=precentage   maximal overlap\n");
      fprintf (stderr, "      --outer-tile-border=prcnt  border to ignore in "
                       "outer files\n");
      fprintf (stderr, "      --inner-tile-border=prcnt  border to ignore in "
                       "inner parts files\n");
      fprintf (stderr, "      --max-contrast=precentage  report differences "
                       "in contrast over this threshold\n");
      fprintf (stderr, "      --max-avg-distance=npixels maximal average distance of "
               "real screen patches to estimated ones via affine transform\n");
      fprintf (stderr, "      --max-max-distance=npixels maximum distance of "
               "real screen patches to estimated ones via affine transform\n");
      fprintf (stderr, "      --geometry-info            store info about "
                       "geometry mismatches to tiff files\n");
      fprintf (stderr, "      --individual-geometry-info store info about geometry "
               "mismatches to tiff files; produce file for each pair\n");
      fprintf (stderr, "      --outlier-info             store info about outliers\n");
      fprintf (stderr, "     hugin output:\n");
      fprintf (stderr, "      --num-control-points=n     number of control "
                       "points for each pair of images\n");
      fprintf (stderr, "      --hfov=val                 lens horizontal "
                       "field of view saved to hugin file\n");
      fprintf (stderr, "     other:\n");
      fprintf (stderr, "      --panorama-map             print panorama map "
                       "in ascii-art\n");
      fprintf (stderr, "      --reoptimize-colors        auto-optimize screen "
                       "colors after initial screen analysis\n");
      fprintf (stderr, "      --limit-directions         do limit overlap "
                       "checking to expected directions\n");
      fprintf (stderr, "      --no-limit-directions      do not limit overlap "
                       "checking to expected directions\n");
      fprintf (stderr, "      --min-patch-contrast=val   specify minimal "
                       "contrast accepted in patch discovery\n");
      fprintf (stderr, "      --diffs                    produce diff files "
                       "for each overlapping pair of tiles\n");
    }
  if (subhelp == help_dump_patch_density || subhelp == help_basic)
    {
      fprintf (stderr, "  dump-patch-density <scan> <parameters> <output>\n");
      fprintf (stderr, "    dump patch densities in text format for external "
               "processing (requires parameters with screen geometry)\n");
    }
  if (subhelp == help_finetune || subhelp == help_basic)
    {
      fprintf (stderr, "  finetune <scan> <parameters> <output> [<args>]\n");
      fprintf (stderr, "    finetune parameters of different parts of the input scan. "
               "Requires parameters with screen geometry\n");
    }
  if (subhelp == help_finetune)
    {
      fprintf (stderr, "    Supported args:\n");
      fprintf (stderr, "      --width=n                 analyze n samples horizontally "
          "(number of vertical samples depends on aspect ratio)\n");
      fprintf (stderr, "      --optimize-position       enable finetuning of "
                       "screen registration\n");
      fprintf (stderr, "      --optimize-fog            enable finetuning of "
                       "fog (dark point)\n");
      fprintf (stderr, "      --optimize-screen-blur    enable finetuning of "
                       "screen blur radius\n");
      fprintf (stderr, "      --optimize-screen-channel-blur enable finetuning of "
               "screen blur radius with each channel independently\n");
      fprintf (stderr, "      --optimize-scanner-mtf-sigma enable finetuning of "
                       "scanner MTF (gaussian blur)\n");
      fprintf (stderr, "      --optimize-scanner-mtf-defocus enable finetuning of "
                       "scanner MTF defocus\n");
      fprintf (stderr, "      --optimize-scanner-mtf-channel-defocus enable finetuning of "
                       "scanner MTF defocus individually for each channel\n");
      fprintf (stderr, "      --optimize-emulsion-blur  enable finetuning of "
                       "emulsion blur radius\n");
      fprintf (stderr, "      --optimize-sharpening     enable finetuning of image sharpening\n");
      fprintf (stderr, "                                requires known screen blur, "
               "mixing weights in input file and monochrome channel use\n");
      fprintf (stderr, "      --optimize-strips         enable finetuning of "
                       "strip widths used to print dufay or screens with strips\n");
      fprintf (stderr, "      --use-monochrome-channel  analyze using "
                       "monochrome channel even when RGB is available\n");
      fprintf (stderr, "      --no-data-collection      do not determine "
                       "colors by data collection\n");
      fprintf (stderr, "      --no-least-squares        do not use least "
                       "squares to optimize screen colors\n");
      fprintf (stderr, "      --multi-tile=n            analyze n times n "
                       "samples and choose best result on each spot\n");
      fprintf (stderr,
               "      --no-normalize            do not normalize colors\n");
      fprintf (stderr,
               "      --simulate-infrared       simulate infrared layer\n");
      fprintf (stderr, "      --blur-tiff=name          write finetuned blur "
                       "radius (either screen, screen channel or emulsion) "
                       "parameters as tiff file\n");
      fprintf (stderr, "      --fog-tiff=name           write finetuned fog "
                       "as tiff file\n");
      fprintf (stderr, "      --position-tiff=name      write finetuned "
                       "position as tiff file\n");
      fprintf (stderr, "      --strips-tiff=name        write finetuned dufay "
                       "strip parameters as tiff file\n");
      fprintf (stderr, "      --screen-color-tiff-base=name write screen colors as tiff files\n");
      fprintf (stderr, "      --orig-tiff-base=name     write analyzed tiles "
                       "into tiff files <name>-y-x.tif\n");
      fprintf (stderr, "      --simulated-tiff-base=name write simulated "
                       "tiles into tiff files <name>-y-x.tif\n");
      fprintf (stderr, "      --diff-tiff-base=name     write diff between "
                       "original and simultated tiles to <name>-y-x.tif\n");
      fprintf (stderr, "      --simulate-infrared       simulate infrared layer\n");
    }
  if (subhelp == help_lab || subhelp == help_basic)
    {
      fprintf (stderr, "  lab <subcommand>\n");
      fprintf (stderr, "    various commands useful for testing.\n");
    }
  if (subhelp == help_lab)
    {
      fprintf (stderr, "    Supported commands are:\n");
      fprintf (stderr, "      dufay-xyY: print report about Dufaycolor reseau "
                       "xyY table from Color Cinematography book\n");
      fprintf (stderr, "      dufay-spectra: print report about Dufaycolor "
                       "reseau spectra\n");
      fprintf (stderr, "      dufay-synthetic: print report about mixing "
                       "Dufaycolor reseau from known dyes\n");
      fprintf (stderr, "      wratten-xyz: print report about Wratten "
                       "trichromatic set xyY values\n");
      fprintf (stderr, "      wratten-spectra: print report about Wratten "
                       "trichromatic set spectra\n");
      fprintf (stderr,
               "      save-film-sensitivity: save film sensitivity curve\n");
      fprintf (stderr,
               "      save-film-log-sensitivity: save film sensitivity curve "
               "with log sensitivity (wedge spectrogram)\n");
      fprintf (stderr, "      save-film-linear-characteristic-curve: save "
                       "film characteristic curve\n");
      fprintf (stderr, "      save-film-hd-characteristic-curve: save film "
                       "characteristic H&D (Hurter and Driffield) curve\n");
      fprintf (stderr, "      save-dyes: save spectra of dyes\n");
      fprintf (stderr, "      save-responses: save responses of filter+emulsion\n");
      fprintf (stderr, "      save-ssf-json: save spectral sensitivity "
                       "functions to json file for dcamprof\n");
      fprintf (stderr, "      render-target: render color target\n");
      fprintf (stderr, "      render-wb-target: render color target with auto "
                       "white balance\n");
      fprintf (stderr, "      render-optimized-target: render color target "
                       "with optimized camera matrix\n");
      fprintf (stderr, "      render-spectra-photo: render photo of spectrum "
                       "taken over filters\n");
      fprintf (stderr,
               "      render-tone-curve: save tone curve in linear gamma\n");
      fprintf (stderr,
               "      render-tone-hd-curve: save tone curve as hd curve\n");
      fprintf (stderr, "      scan-primaries: produce matrix profile specialized for "
               "given backlight, response and process dyes\n");
      fprintf (stderr, "    Each subcommand has its own help.\n");
    }
  if (subhelp == help_read_chemcad_spectra || subhelp == help_basic)
    {
      fprintf (stderr,
               "  read-chemcad-spectra <out_filename> <in_filename>\n");
      fprintf (stderr,
               "    read spectrum in chemcad database format and output it in "
               "format that can be built into libcolorscreen\n");
    }
  if (subhelp == help_has_regular_screen || subhelp == help_basic)
    {
      fprintf (stderr,
               "  has-regular-screen <in_filename>\n");
      fprintf (stderr,
               "    attempt to detect regular screen in scan\n");
    }
  if (subhelp == help_has_regular_screen)
    {
      fprintf (stderr, "      --save-tiles=base         save tiles analyzed to tiff\n");
      fprintf (stderr, "      --save-fft=base           save FFT of tiles analyzed to tiff\n");
      fprintf (stderr, "      --threshold=n             threshold needed to consider tile to have regular screen\n");
      fprintf (stderr, "      --tile-threshold=n        percentage of tiles needed to agree on given period\n");
      fprintf (stderr, "      --gamma=n                 gamma of input scan (2.2 is default)\n");
      fprintf (stderr, "      --xtiles                  number of tiles to analyze in horizontal direction\n");
      fprintf (stderr, "      --ytiles                  number of tiles to analyze in vertical direction\n");
      fprintf (stderr, "      --report=filename         save report about analysis\n");
      fprintf (stderr, "      --save-matches=filename   save filenames of files with regular pattern\n");
      fprintf (stderr, "      --save-misses=filename    save filenames of files where no regular pattern is detected\n");
      fprintf (stderr, "      --min-period=p            minimal period considered interesting\n");
      fprintf (stderr, "      --max-period=p		maximal period considered interesting\n");
      fprintf (stderr, "      --must-match		terminate on first scan not matching\n");
    }
  if (subhelp == help_render || subhelp == help_autodetect
      || subhelp == help_stitch)
    {
      fprintf (stderr, "     parameters of screen geometry detection:\n");
      fprintf (stderr, "      --scanner-type=type       specify scanner type\n");
      fprintf (stderr, "                                supported scanner types:");
      for (int j = 0; j < max_scanner_type; j++)
        {
          if (!(j % 4))
            fprintf (stderr, "\n                                 ");
          fprintf (stderr, " %s", scanner_type_names[j].name);
        }
      fprintf (stderr, "\n");
      fprintf (stderr, "      --screen-type=type       specify scanner type\n");
      fprintf (stderr,
               "                                supported scanner types:");
      for (int j = 0; j < max_scr_type; j++)
        {
          if (!(j % 4))
            fprintf (stderr, "\n                                 ");
          fprintf (stderr, " %s", scr_names[j].name);
        }
      fprintf (stderr, "\n");
      fprintf (stderr, "      --mesh                    compute mesh of "
                       "non-linear transformations\n");
      fprintf (stderr, "      --no-mesh                 do not compute mesh "
                       "of non-linear transformations\n");
      fprintf (stderr, "      --optimize-colors         try to automatically "
                       "optimize colors of patches for screen discovery\n");
      fprintf (stderr, "      --no-optimize-colors      do not automatically "
                       "optimize colors of patches for screen discovery\n");
      fprintf (stderr, "      --min-screen-percentage   specify minimum "
                       "percentage of screen to be detected\n");
      fprintf (stderr, "      --max-unknown-screen-range maximum range of screen with undetected patches considered to be acceptable.\n");
      fprintf (stderr, "      --min-patch-contrast      specify minimum "
                       "contrast for patch detection\n");
      fprintf (stderr, "      --fast-floodfill          enable use of fast "
                       "patch detection\n");
      fprintf (stderr, "      --no-fast-floodfill       disable use of fast "
                       "patch detection\n");
      fprintf (stderr, "      --slow-floodfill          enable use of slow "
                       "patch detection\n");
      fprintf (stderr, "      --no-slow-floodfill       disable use of slow "
                       "patch detection\n");
    }
  if (subhelp == help_autodetect)
    {
      fprintf (stderr, "      --top/bottom/left/right   assume that given part "
                       "of scan is not part of an image and insert fake point "
                       "to improve geometry of binding tape\n");
      fprintf (stderr, "      --border-top=percent      assume that given percent from the "
          "top of the scan is a border and does not contain screen\n");
      fprintf (stderr, "      --border-bottom=percent   same for bottom\n");
      fprintf (stderr, "      --border-left=percent     same for left\n");
      fprintf (stderr, "      --border-right=percent    same for right\n");
    }
  if (subhelp == help_mtf  || subhelp == help_basic)
    {
      fprintf (stderr,
               "  mtf <in_filename>\n");
      fprintf (stderr,
               "    various operations with scanner MTF settings\n");
    }
  if (subhelp == help_has_regular_screen)
    {
      fprintf (stderr, "      --save-csv=name.csv       save MTF (or match) in CSV format\n");
      fprintf (stderr, "      --save-psf=name.tif       save point spread function to TIF\n");
      fprintf (stderr, "      --load-quickmtf=name.txt  Load measured MTF data in quickmtf format\n");
      fprintf (stderr, "      --match  		        match measured MTF with parameters\n");
      fprintf (stderr, "      --save-matched-psf=n.tif  save point spread function of matched parameters to TIF\n");
      fprintf (stderr, "      --sigma=pixel_sigma       specify lens sigma (gaussian blur) in pixels\n");
      fprintf (stderr, "      --blur-diameter=pixels    specify lens blur diameter (used only when there is no info for difraction model)\n");
      fprintf (stderr, "      --pixel-pitch=um 		specify sensor's pixel size in micrometers\n");
      fprintf (stderr, "      --wavelength=nm 		specify light wavelength in nanometers\n");
      fprintf (stderr, "      --f-stop=f 		specify lens nominal f-stop\n");
      fprintf (stderr, "      --defocus=mm 		specify defocus from sensor plane in millimeters\n");
      fprintf (stderr, "      --scan-dpi=dpi 		specify scanned DPI (needed to compute magnification)\n");
    }
  fprintf (stderr, "\n");
  if (subhelp == help_basic)
    fprintf (stderr, "Use %s <command> --help for help on specific command\n",
             binname);
  fprintf (stderr,
           "See also "
           "https://github.com/janhubicka/Color-Screen/wiki/colorscreen\n");
  exit (1);
}

template <typename T, const char *const names[], int max>
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


/* Parse ARG according to NAMES of length MAX and return its value.
   On error, use ERRMSG to print error.  */

template <typename T, typename P, const P names[], int max>
T
parse_enum_property (const char *arg, const char *errmsg)
{
  for (int i = 0; i < max; i++)
    if (!strcmp (arg, names[i].name))
      return (T)i;
  fprintf (stderr, errmsg, arg);
  fprintf (stderr, "Possible values are: ");
  for (int i = 0; i < max; i++)
    fprintf (stderr, " %s", names[i].name);
  fprintf (stderr, "\n");
  exit (1);
  return (T)max;
}

/* Parse output mode MODE.  */
static enum render_type_t
parse_mode (const char *mode)
{
  for (int i = 0; i < render_type_max; i++)
    if (!strcmp (mode, render_type_properties[i].name))
      return (render_type_t)i;
  fprintf (stderr, "Unknown rendering mode:%s\n", mode);
  fprintf (stderr, "Possible values are: ");
  for (int i = 0; i < render_type_max; i++)
    fprintf (stderr, " %s", render_type_properties[i].name);
  fprintf (stderr, "\n");
  exit (1);
  return render_type_max;
}

/* Parse output profile PROFILE.  */
static enum render_parameters::output_profile_t
parse_output_profile (const char *profile)
{
  return parse_enum<enum render_parameters::output_profile_t,
                    render_parameters::output_profile_names,
                    (int)render_parameters::output_profile_max> (
      profile, "Unknown output profile:%s\n");
}

/* Parse color model MODEL.  */
static enum render_parameters::color_model_t
parse_color_model (const char *model)
{
  return parse_enum_property<enum render_parameters::color_model_t,
                             render_parameters::color_model_property,
                             render_parameters::color_model_properties,
                             (int)render_parameters::color_model_max> (
      model, "Unknown color model:%s\n");
}

/* Parse demosaicing algorithm MODEL.  */
static enum image_data::demosaicing_t
parse_demosaic (const char *model)
{
  return parse_enum_property<enum image_data::demosaicing_t,
                    property_t,
                    image_data::demosaic_names,
                    (int)image_data::demosaic_max> (
      model, "Unknown demosaicing algorithm:%s\n");
}

/* Parse scanner type MODEL.  */
static scanner_type
parse_scanner_type (const char *model)
{
  return parse_enum_property<scanner_type, property_t, scanner_type_names,
                    (int)max_scanner_type> (model, "Unknown scanner type:%s\n");
}
/* Parse screen type MODEL.  */
static enum scr_type
parse_scr_type (const char *model)
{
  return parse_enum_property<scr_type, scr_type_property_t, scr_names, (int)max_scr_type> (
      model, "Unknown screen type:%s\n");
}

/* Parse dye balance MODEL.  */
static enum render_parameters::dye_balance_t
parse_dye_balance (const char *model)
{
  return parse_enum_property<enum render_parameters::dye_balance_t,
                    property_t,
                    render_parameters::dye_balance_names,
                    (int)render_parameters::dye_balance_max> (
      model, "Unknown dye balance:%s\n");
}

/* If there is --ARG param or --ARG=PARAM at the command line
   position *I, return non-NULL and in the first case increment *I.
   ARGC and ARGV are standard command line arguments.  */

static char *
arg_with_param (int argc, char **argv, int *i, std::string_view arg)
{
  std::string_view cargv (argv[*i]);
  if (cargv.size() < 2 || cargv[0] != '-' || cargv[1] != '-')
    return nullptr;
  
  std::string_view opt = cargv.substr (2);
  if (opt.size() >= arg.size() && opt.substr(0, arg.size()) == arg)
    {
      if (opt.size() == arg.size())
        {
          if (*i == argc - 1)
            print_help ();
          (*i)++;
          return argv[*i];
        }
      if (opt[arg.size()] == '=')
        return argv[*i] + arg.size() + 3;
    }
  return nullptr;
}

/* Parse float parameter ARG with value VAL in range MIN...MAX.
   ARGC and ARGV are standard command line arguments and I is current position.  */
static bool
parse_float_param (int argc, char **argv, int *i, const char *arg, float &val,
                   float min, float max)
{
  const char *param = arg_with_param (argc, argv, i, arg);
  if (!param)
    return false;

  std::string_view s (param);
  auto [ptr, ec] = std::from_chars (s.data (), s.data () + s.size (), val);

  if (ec != std::errc ())
    {
      fprintf (stderr, "invalid parameter of %s: %s\n", arg, param);
      print_help ();
    }
  if (val < min || val > max)
    {
      fprintf (stderr, "parameter %s=%f is out of range %f...%f\n", arg, val,
               min, max);
      print_help ();
    }
  return true;
}
/* Parse int parameter ARG with value VAL in range MIN...MAX.
   ARGC and ARGV are standard command line arguments and I is current position.  */
static bool
parse_int_param (int argc, char **argv, int *i, const char *arg, int &val,
                 int min, int max)
{
  const char *param = arg_with_param (argc, argv, i, arg);
  if (!param)
    return false;

  std::string_view s (param);
  auto [ptr, ec] = std::from_chars (s.data (), s.data () + s.size (), val);

  if (ec != std::errc ())
    {
      fprintf (stderr, "invalid parameter of %s: %s\n", arg, param);
      print_help ();
    }
  if (val < min || val > max)
    {
      fprintf (stderr, "parameter %s=%i is out of range %i...%i\n", arg, val,
               min, max);
      print_help ();
    }
  return true;
}

/* Parse common flags in ARGC/ARGV at position I.  */
bool
parse_common_flags (int argc, char **argv, int *i)
{
  std::string_view arg (argv[*i]);
  if (arg == "--help" || arg == "-h")
    {
      print_help ();
      exit (0);
    }
  if (arg == "--version")
    {
      printf ("colorscreen version %s\n", PACKAGE_VERSION);
      exit (0);
    }
  if (arg == "--verbose")
    {
      verbose = true;
      return true;
    }
  if (arg == "--verbose-tasks")
    {
      verbose_tasks = true;
      return true;
    }
  if (arg == "--time-report")
    {
      colorscreen::time_report = true;
      return true;
    }
  if (const char *param = arg_with_param (argc, argv, i, "threads"))
    {
      int nthreads;
      std::string_view s (param);
      if (std::from_chars (s.data (), s.data () + s.size (), nthreads).ec == std::errc ())
        {
#ifdef _OPENMP
          omp_set_num_threads (nthreads);
#endif
          return true;
        }
    }
  return false;
}

/* Parse regular screen detection parameters in DSPARAMS.
   IN_PANORAMA is true if we are in panorama mode.
   ARGC, ARGV and I are standard command line arguments.  */
static bool
parse_detect_regular_screen_params (detect_regular_screen_params &dsparams,
                                    bool in_panorama, int argc, char **argv,
                                    int *i)
{
  float flt;
  if (!strcmp (argv[*i], "--slow-floodfill"))
    dsparams.slow_floodfill = true;
  else if (!strcmp (argv[*i], "--fast-floodfill"))
    dsparams.fast_floodfill = true;
  else if (!strcmp (argv[*i], "--no-slow-floodfill"))
    dsparams.slow_floodfill = false;
  else if (!strcmp (argv[*i], "--no-fast-floodfill"))
    dsparams.fast_floodfill = false;
  else if (const char *str = arg_with_param (argc, argv, i, "scanner-type"))
    dsparams.scanner_type = parse_scanner_type (str);
  else if (const char *str = arg_with_param (argc, argv, i, "screen-type"))
    dsparams.scr_type = parse_scr_type (str);
  else if (!strcmp (argv[*i], "--mesh"))
    dsparams.do_mesh = true;
  else if (!strcmp (argv[*i], "--no-mesh"))
    dsparams.do_mesh = false;
  else if (!strcmp (argv[*i], "--optimize-colors"))
    dsparams.optimize_colors = true;
  else if (!strcmp (argv[*i], "--no-optimize-colors"))
    dsparams.optimize_colors = false;
  else if (parse_float_param (argc, argv, i, "gamma", flt, -2, 100))
    dsparams.gamma = flt;
  else if (parse_float_param (argc, argv, i, "min-screen-percentage", flt, 0,
                              100))
    dsparams.min_screen_percentage = flt;
  else if (parse_int_param (argc, argv, i, "max-unknown-screen-range",
	   dsparams.max_unknown_screen_range, 0, 100000))
    ;
  else if (parse_float_param (argc, argv, i, "min-patch-contrast", flt, 0,
                              1000))
    dsparams.min_patch_contrast = flt;
  else if (in_panorama)
    return false;
  else if (!strcmp (argv[*i], "--top"))
    dsparams.top = true;
  else if (!strcmp (argv[*i], "--bottom"))
    dsparams.bottom = true;
  else if (!strcmp (argv[*i], "--left"))
    dsparams.left = true;
  else if (!strcmp (argv[*i], "--right"))
    dsparams.right = true;
  else if (parse_float_param (argc, argv, i, "border-top", flt, 0, 100))
    dsparams.border_top = flt;
  else if (parse_float_param (argc, argv, i, "border-bottom", flt, 0, 100))
    dsparams.border_bottom = flt;
  else if (parse_float_param (argc, argv, i, "border-left", flt, 0, 100))
    dsparams.border_left = flt;
  else if (parse_float_param (argc, argv, i, "border-right", flt, 0, 100))
    dsparams.border_right = flt;
  else
    return false;
  return true;
}

/* Parse geometry PROFILE.  */
static enum render_to_file_params::output_geometry
parse_geometry (const char *profile)
{
  return parse_enum_property<enum render_to_file_params::output_geometry,
                    property_t,
                    render_to_file_params::geometry_names,
                    render_to_file_params::max_geometry>
		    (profile, "Unknown geometry:%s\n");
}

/* Implement "render" command with ARGC and ARGV.  */
static int
render_cmd (int argc, char **argv)
{
  const char *infname = NULL, *cspname = NULL, *error = NULL;
  float age = -100;
  enum render_parameters::output_profile_t output_profile
      = render_parameters::output_profile_max;
  render_parameters::color_model_t color_model
      = render_parameters::color_model_max;
  render_parameters::dye_balance_t dye_balance
      = render_parameters::dye_balance_max;
  bool solver = false;
  struct solver_parameters solver_param;
  render_to_file_params rfparams;
  render_type_parameters rtparam;
  bool detect_geometry = false;
  bool detect_color_model = false;
  bool detect_brightness = false;
  float scan_dpi = 0;
  float scale = 0;
  float screen_scale = 0;
  float output_gamma = -4;
  subhelp = help_render;
  detect_regular_screen_params dsparams;
  bool ignore_infrared = false;

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (parse_detect_regular_screen_params (dsparams, false, argc, argv,
                                                   &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "mode"))
        rtparam.type = parse_mode (str);
      else if (arg == "--hdr")
        rfparams.hdr = true;
      else if (arg == "--dng")
        rfparams.dng = true;
      else if (arg == "--solver")
        solver = true;
      else if (arg == "--ignore-infrared")
        ignore_infrared = true;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "output-profile"))
        output_profile = parse_output_profile (str);
      else if (parse_float_param (argc, argv, &i, "scan-ppi", scan_dpi, 1,
                                  1000000)
               || parse_float_param (argc, argv, &i, "age", age, -1000, 1000)
               || parse_float_param (argc, argv, &i, "scale", scale, 0.0000001,
                                     100)
               || parse_float_param (argc, argv, &i, "screen-scale", screen_scale, 0.0000001,
                                     100)
               || parse_float_param (argc, argv, &i, "output-gamma",
                                     output_gamma, -1, 100))
        ;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "color-model"))
        color_model = parse_color_model (str);
      else if (arg == "--detect-geometry")
        detect_geometry = true;
      else if (arg == "--auto-color-model")
        detect_color_model = true;
      else if (arg == "--auto-levels")
        detect_brightness = true;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "dye-balance"))
        dye_balance = parse_dye_balance (str);
      else if (const char *str
               = arg_with_param (argc, argv, &i, "geometry"))
        rfparams.geometry = parse_geometry (str);
      else if (parse_int_param (argc, argv, &i, "antialias",
	       rfparams.antialias, 1, 1024))
        ;
      else if (!infname)
        infname = argv[i];
      else if (!cspname)
        cspname = argv[i];
      else if (!rfparams.filename)
        rfparams.filename = argv[i];
      else
        print_help (argv[i]);
    }
  if (!infname || !cspname || !rfparams.filename)
    print_help ();
  file_progress_info progress (stdout, verbose, verbose_tasks);

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
      return 1;
    }
  if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      return 1;
    }
  fclose (in);


  /* Load scan data.  */
  image_data scan;
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading scan %s\n", infname);
      progress.resume_stdout ();
    }
  if (!scan.load (infname, false, &error, &progress, rparam.demosaic))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      return 1;
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
            printf (", horizontal PPI %f", scan.xdpi);
          if (scan.ydpi)
            printf (", vertical PPI %f", scan.ydpi);
        }
      printf ("\n");
      progress.resume_stdout ();
    }
  if (detect_geometry && scan.has_rgb ())
    {
      if (verbose)
        {
          progress.pause_stdout ();
          printf ("Detecting geometry\n");
          if (dsparams.scr_type == max_scr_type)
            dsparams.scr_type = param.type;
          if (!dsparams.gamma)
            dsparams.gamma = rparam.gamma;
          if (dsparams.scanner_type == max_scanner_type)
            dsparams.scanner_type = param.scanner_type;
          auto detected = detect_regular_screen (scan, dparam, solver_param,
                                                 &dsparams, &progress);
          if (!detected.success)
            {
              progress.pause_stdout ();
              fprintf (stderr, "Autodetection failed\n");
              return 1;
            }
        }
    }
  else if (solver && solver_param.n_points ())
    {
      if (verbose)
        {
          progress.pause_stdout ();
          printf ("Computing mesh\n");
          progress.resume_stdout ();
        }
      if (param.mesh_trans)
        param.mesh_trans = NULL;
      param.mesh_trans = solver_mesh (&param, scan, solver_param);
      param.mesh_trans_is_scr_to_img = false;
    }
  if (detect_color_model)
    rparam.auto_color_model (param.type);
  if (detect_brightness)
    rparam.auto_dark_brightness (
        scan, param,
        { (int)(scan.width / 10), (int)(scan.height / 10),
          (int)(8 * scan.width / 10), (int)(8 * scan.height / 10) },
        &progress);

  /* Apply command line parameters.  */
  if (age != -100)
    rparam.age = {age, age, age};
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
  if (screen_scale)
    rfparams.screen_scale = screen_scale;
  if (ignore_infrared)
    rparam.ignore_infrared = true;

  /* ... and render!  */
  rfparams.verbose = verbose;
  if (!render_to_file (scan, param, dparam, rparam, rfparams, rtparam,
                       &progress, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not save %s: %s\n", rfparams.filename, error);
      return 1;
    }
  progress.pause_stdout ();
  return 0;
}

/* Implement "autodetect" command with ARGC and ARGV.  */
static int
autodetect (int argc, char **argv)
{
  const char *cspname = NULL;
  const char *infname = NULL;
  const char *outname = NULL;
  const char *repname = NULL;
  float scan_dpi = 0;
  bool detect_color_model = true;
  bool detect_brightness = true;
  scr_detect_parameters dparam;
  detect_regular_screen_params dsparams;
  subhelp = help_autodetect;
  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (parse_detect_regular_screen_params (dsparams, false, argc, argv,
                                                   &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "par"))
        cspname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "report"))
        repname = str;
      else if (arg == "--auto-color-model")
        detect_color_model = true;
      else if (arg == "--no-auto-color-model")
        detect_color_model = false;
      else if (arg == "--auto-levels")
        detect_brightness = true;
      else if (arg == "--no-auto-levels")
        detect_brightness = false;
      else if (!infname)
        infname = argv[i];
      else if (!outname)
        outname = argv[i];
      else
        print_help (argv[i]);
    }
  if (!outname)
    print_help ();
  file_progress_info progress (stdout, verbose, verbose_tasks);
  scr_to_img_parameters param;
  render_parameters rparam;
  solver_parameters solver_param;
  const char *error;
  /* Load scan data.  */
  image_data scan;
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading scan %s\n", infname);
      progress.resume_stdout ();
    }
  if (!scan.load (infname, false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", infname, error);
      return 1;
    }
  if (cspname)
    {
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
          return 1;
        }
      if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
        {
          progress.pause_stdout ();
          fprintf (stderr, "Can not load %s: %s\n", cspname, error);
          return 1;
        }
      fclose (in);

      /* Copy parameters from par file except those specified by user.  */
      if (dsparams.scr_type == max_scr_type)
        dsparams.scr_type = param.type;
      else
        param.type = dsparams.scr_type;
      if (!dsparams.gamma)
        dsparams.gamma = rparam.gamma;
      else
        rparam.gamma = dsparams.gamma;
      if (dsparams.scanner_type == max_scanner_type)
        dsparams.scanner_type = param.scanner_type;
      else
        param.scanner_type = dsparams.scanner_type;
    }
  else
    {
      /* Fixed lens is a reasonable default.  */
      if (dsparams.scanner_type == max_scanner_type)
        dsparams.scanner_type = fixed_lens;
    }
  if (!scan.has_rgb () && !scan.stitch)
    {
      progress.pause_stdout ();
      fprintf (stderr, "Autodetection is only implemented for RGB scans and "
                       "stitched projects");
      return 1;
    }
  rparam.gamma = dsparams.gamma;
  if (scan.has_rgb () && !scan.stitch)
    {
      FILE *report = NULL;
      if (repname && !(report = fopen (repname, "wt")))
        {
          progress.pause_stdout ();
          perror (repname);
          return 1;
        }
      if (!dsparams.gamma)
        rparam.gamma = dsparams.gamma = scan.gamma != -2 ? scan.gamma : 0;

      if (verbose)
        {
          progress.pause_stdout ();
          printf ("Detecting geometry\n");
          progress.resume_stdout ();
        }
      if (param.mesh_trans)
        param.mesh_trans = NULL;
      /* We will warn later on using default.  */
      if (dsparams.gamma == 0)
	dsparams.gamma = 2.2;
      auto detected = detect_regular_screen (scan, dparam, solver_param,
                                             &dsparams, &progress, report);
      param = detected.param;
      param.mesh_trans = detected.mesh_trans;
      param.mesh_trans_is_scr_to_img = true;
      if (report)
        fclose (report);
      if (!detected.success)
        {
          progress.pause_stdout ();
          fprintf (stderr, "Autodetection failed\n");
          return 1;
        }
    }
  else
    {
      param.type = scan.stitch->images[0][0].param.type;
      if (!dsparams.gamma)
        rparam.gamma = dsparams.gamma
            = scan.stitch->images[0][0].img->gamma != -2
                  ? scan.stitch->images[0][0].img->gamma
                  : 0;
    }
  if (rparam.gamma == 0)
    {
      fprintf (stderr, "Warning: unable to detect gamma and assuming 2.2; "
                       "please use --gamma parameter\n");
      dsparams.gamma = rparam.gamma = 2.2;
    }
  if (scan_dpi)
    scan.set_dpi (scan_dpi, scan_dpi);
  if (detect_color_model)
    rparam.auto_color_model (param.type);
  if (detect_brightness)
    {
      if (verbose)
        {
          progress.pause_stdout ();
          printf ("Detecting levels\n");
          progress.resume_stdout ();
        }
      rparam.auto_dark_brightness (
          scan, param,
          { (int)(scan.width / 10), (int)(scan.height / 10),
            (int)(8 * scan.width / 10), (int)(8 * scan.height / 10) },
          &progress);
    }
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Saving %s\n", outname);
      progress.resume_stdout ();
    }
  FILE *out = fopen (outname, "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror (outname);
      return 1;
    }
  if (!save_csp (out, &param, &dparam, &rparam, &solver_param))
    {
      fprintf (stderr, "saving failed\n");
      return 1;
    }
  fclose (out);
  return 0;
}

/* Implement "analyze-backlight" command with ARGC and ARGV.  */
void
analyze_backlight (int argc, char **argv)
{
  luminosity_t gamma = 1.0;
  const char *error = NULL;
  subhelp = help_analyze_backlight;
  const char *blacks = NULL;
  const char *white = NULL;
  const char *out_file = NULL;
  const char *tiff = NULL;
  image_data::demosaicing_t demosaic = image_data::demosaic_default;
  for (int i = 0; i < argc; i++)
    {
      float flt;
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (parse_float_param (argc, argv, &i, "gamma", flt, 0, 2))
	gamma = flt;
      else if (const char *str = arg_with_param (argc, argv, &i, "black"))
        blacks = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "demosaic"))
        demosaic = parse_demosaic (str);
      else if (!white)
	white = argv[i];
      else if (!out_file)
	out_file = argv[i];
      else if (!tiff)
	tiff = argv[i];
      else
        print_help (argv[i]);
    }

  if (!out_file)
    print_help ();
  file_progress_info progress (stdout, verbose, verbose_tasks);
  image_data scan;
  if (!scan.load (white, false, &error, &progress, demosaic))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", white, error);
      exit (1);
    }
  std::unique_ptr<image_data> blacks_scan;
  if (blacks)
    {
      blacks_scan = std::make_unique <image_data> ();
      /* Black references usually does not have enough data to determine scaling weights.  */
      if (!blacks_scan->load (blacks, false, &error, &progress, demosaic == image_data::demosaic_monochromatic_bayer_corrected ? image_data::demosaic_monochromatic : demosaic))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load black reference %s: %s\n", blacks, error);
	  exit (1);
	}
    }
  progress.set_task ("analyzing backlight", 1);
  auto cor = backlight_correction_parameters::analyze_scan (scan, gamma, blacks_scan.get ());
  progress.set_task ("writing output", 1);
  FILE *out = fopen (out_file, "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror ("Cannot open output file");
      exit (1);
    }
  if (!cor->save (out))
    {
      fprintf (stderr, "Cannot write %s\n", out_file);
      exit (1);
    }
  fclose (out);
  if (tiff)
    {
      error = cor->save_tiff (tiff);
      if (error)
        {
          fprintf (stderr, "Failed to save output file: %s\n", error);
          exit (1);
        }
    }
}

/* Return correction factor for given MODE and RES.  */
coord_t
get_correction (scanner_blur_correction_parameters::correction_mode mode, finetune_result &res)
{
  switch (mode)
    {
    case scanner_blur_correction_parameters::blur_radius:
      return res.screen_blur_radius;
      break;
    case scanner_blur_correction_parameters::mtf_defocus:
      return res.scanner_mtf_defocus;
      break;
    case scanner_blur_correction_parameters::mtf_blur_diameter:
      return res.scanner_mtf_blur_diameter;
      break;
    case scanner_blur_correction_parameters::max_correction:
      abort ();
    }
  abort ();
}

/* Analyze scanner blur in SCAN using parameters PARAM and RPARAM.
   STRIP_XSTEPS, STRIP_YSTEPS, XSTEPS, YSTEPS, XSUBSTEPS, YSUBSTEPS specify
   sampling density.
   FLAGS specify optimization flags.
   REOPTIMIZE_STRIP_WIDTHS enables strip width optimization.
   SKIPMIN and SKIPMAX specify ranges to skip.
   TOLERANCE is robust average tolerance.
   PROGRESS is progress info.  */
std::unique_ptr <scanner_blur_correction_parameters>
analyze_scanner_blur_img (scr_to_img_parameters &param, 
			  render_parameters &rparam,
			  image_data &scan,
			  int strip_xsteps, int strip_ysteps,
			  int xsteps, int ysteps,
			  int xsubsteps, int ysubsteps,
			  uint64_t flags,
			  bool reoptimize_strip_widths,
			  coord_t skipmin, coord_t skipmax,
			  coord_t tolerance,
			  progress_info *progress)
{
  analyze_scanner_blur_worker worker (param, rparam, scan);
  worker.strip_xsteps = strip_xsteps;
  worker.strip_ysteps = strip_ysteps;
  worker.xsteps = xsteps;
  worker.ysteps = ysteps;
  worker.xsubsteps = xsubsteps;
  worker.ysubsteps = ysubsteps;
  worker.flags = flags;
  worker.reoptimize_strip_widths = reoptimize_strip_widths;
  worker.skipmin = skipmin;
  worker.skipmax = skipmax;
  worker.tolerance = tolerance;
  worker.progress = progress;
  worker.verbose = verbose;
  if (!worker.step1 ())
    return NULL;
  if (worker.do_strips ())
    {
#pragma omp parallel for default(none) collapse(2) schedule(dynamic) shared (worker)
      for (int y = 0; y < worker.strip_ysteps; y++)
	for (int x = 0; x < worker.strip_xsteps; x++)
	  worker.analyze_strips (x, y);
    }
  if (!worker.step2 ())
    return NULL;
#pragma omp parallel for default(none) collapse(2) schedule(dynamic) shared (worker)
  for (int y = 0; y < worker.ysteps * worker.ysubsteps; y++)
    for (int x = 0; x < worker.xsteps * worker.xsubsteps; x++)
      worker.analyze_blur (x, y);
  return worker.step3 ();
}

/* Implement "analyze-scanner-blur" command with ARGC and ARGV.  */
static bool
analyze_scanner_blur (int argc, char **argv)
{
  const char *infname = NULL, *cspname = NULL, *error = NULL;
  const char *outcspname = NULL;
  const char *outtifname = NULL;
  subhelp = help_analyze_scanner_blur;
  int xsteps = 0, ysteps = 0;
  int xsubsteps = 0, ysubsteps = 0;
  float skipmin = 25;
  float skipmax = 25;
  float tolerance = -1;
  int strip_xsteps = 0;
  int strip_ysteps = 0;
  bool reoptimize_strip_widths = false;
  uint64_t flags = finetune_position | finetune_no_progress_report
		   | finetune_scanner_mtf_defocus;

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "out"))
        outcspname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "out-tiff"))
        outtifname = str;
      else if (arg == "--reoptimize-strip-widths")
        reoptimize_strip_widths = true;
      else if (arg == "--no-reoptimize-strip-widths")
        reoptimize_strip_widths = false;
      else if (arg == "--optimize-screen-blur")
        {
          flags &= ~(finetune_screen_channel_blurs | finetune_screen_blur | finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus);
          flags |= finetune_screen_blur;
        }
      else if (arg == "--optimize-screen-channel-blur")
        {
          flags &= ~(finetune_screen_channel_blurs | finetune_screen_blur | finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus);
          flags |= finetune_screen_channel_blurs;
        }
      else if (arg == "--optimize-scanner-mtf-defocus")
        {
          flags &= ~(finetune_screen_channel_blurs | finetune_screen_blur | finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus);
          flags |= finetune_scanner_mtf_defocus;
        }
      else if (arg == "--optimize-scanner-mtf-channel-defocus")
        {
          flags &= ~(finetune_screen_channel_blurs | finetune_screen_blur | finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus);
          flags |= finetune_scanner_mtf_channel_defocus;
        }
      else if (arg == "--optimize-fog")
        flags |= finetune_fog;
      else if (arg == "--no-optimize-fog")
        flags &= ~finetune_fog;
      else if (arg == "--simulate-infrared")
        flags |= finetune_simulate_infrared;
      else if (arg == "--normalize")
        flags &= ~finetune_no_normalize;
      else if (arg == "--no-normalize")
        flags |= finetune_no_normalize;
      else if (arg == "--data-collection")
        flags &= ~finetune_no_data_collection;
      else if (arg == "--no-data-collection")
        flags |= finetune_no_data_collection;
      else if (parse_int_param (argc, argv, &i, "width", xsteps, 1,
                                1024 * 1024))
        ;
      else if (parse_int_param (argc, argv, &i, "height", ysteps, 1,
                                1024 * 1024))
        ;
      else if (parse_int_param (argc, argv, &i, "xsamples", xsubsteps, 1,
                                1024 * 1024))
        ;
      else if (parse_int_param (argc, argv, &i, "ysamples", ysubsteps, 1,
                                1024 * 1024))
        ;
      else if (parse_float_param (argc, argv, &i, "tolerance", tolerance, 0,
                                  10))
        ;
      else if (parse_float_param (argc, argv, &i, "skip-min", skipmin, 0, 50))
        ;
      else if (parse_float_param (argc, argv, &i, "skip-max", skipmin, 0, 50))
        ;
      else if (parse_int_param (argc, argv, &i, "strip-width", xsteps, 1,
                                1024 * 1024))
        ;
      else if (parse_int_param (argc, argv, &i, "strip-height", ysteps, 1,
                                1024 * 1024))
        ;
      else if (!infname)
        infname = argv[i];
      else if (!cspname)
        cspname = argv[i];
      else
        print_help (argv[i]);
    }
  if (!infname || !cspname)
    print_help ();

  file_progress_info progress (stdout, verbose, verbose_tasks);
  /* Load scan data.  */
  image_data scan;
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading scan %s\n", infname);
      progress.resume_stdout ();
    }
  if (!scan.load (infname, false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", infname, error);
      return 1;
    }
  /* Load color screen and rendering parameters.  */
  struct solver_parameters solver_param;
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
      return 1;
    }
  if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", cspname, error);
      return 1;
    }
  fclose (in);
#ifdef _OPENMP
  omp_set_nested (1);
#endif

  if (!scan.stitch)
    {
      rparam.scanner_blur_correction = analyze_scanner_blur_img (
	  param, rparam, scan, strip_xsteps, strip_ysteps, xsteps, ysteps,
	  xsubsteps, ysubsteps, flags, reoptimize_strip_widths, skipmin, skipmax,
	  tolerance, &progress);
      if (!rparam.scanner_blur_correction)
	return 1;
    }
  else
    {
      if (rparam.tile_adjustments_width != scan.stitch->params.width
	  || rparam.tile_adjustments_height != scan.stitch->params.height)
	rparam.set_tile_adjustments_dimensions (scan.stitch->params.width, scan.stitch->params.height);
      progress.set_task ("analyzing tiles", scan.stitch->params.width * scan.stitch->params.height);
      for (int y = 0; y < scan.stitch->params.height; y++)
        for (int x = 0; x < scan.stitch->params.width; x++)
	  {
	    //if (!scan.stitch->images[y][x].param.load (scan.stitch,)
	    int stack = progress.push ();
	    const char *error;
	    if (!scan.stitch->images[y][x].load_img (&error, &progress))
	      {
		progress.pop (stack);
		if (error)
		  {
		    progress.pause_stdout ();
		    printf ("Failed to load image: %s\n", error);
		    progress.resume_stdout ();
		    return 1;
		  }
	        return 1;
	      }
	    rparam.get_tile_adjustment (x, y).scanner_blur_correction = analyze_scanner_blur_img (
		scan.stitch->images[y][x].param, rparam, *scan.stitch->images[y][x].img.get(), strip_xsteps, strip_ysteps, xsteps, ysteps,
		xsubsteps, ysubsteps, flags, reoptimize_strip_widths, skipmin, skipmax,
		tolerance, &progress);
	    if (!rparam.get_tile_adjustment (x, y).scanner_blur_correction)
	      {
		progress.pop (stack);
		return 1;
	      }
	    scan.stitch->images[y][x].release_img ();
	    //scan.stitch->images[y][x].release_image_data (&progress);
	    progress.pop (stack);
	    progress.inc_progress ();
	  }
    }

  if (!outcspname)
    outcspname = cspname;
  progress.set_task ("writing parameters", 1);
  FILE *out = fopen (outcspname, "wt");
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Saving color screen parameters: %s\n", outcspname);
      progress.resume_stdout ();
    }
  if (!out)
    {
      progress.pause_stdout ();
      perror (outcspname);
      return 1;
    }
  if (!save_csp (out, &param, &dparam, &rparam, &solver_param))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot save %s\n", outcspname);
      return 1;
    }
  fclose (out);
  if (outtifname)
  {
    if (!scan.stitch)
      {
	if ((error = rparam.scanner_blur_correction->save_tiff (outtifname)))
	  {
	    progress.pause_stdout ();
	    fprintf (stderr, "Failed saving tiff file %s: %s\n", outtifname, error);
	    return 1;
	  }
      }
    else
      {
	for (int y = 0; y < scan.stitch->params.height; y++)
	  for (int x = 0; x < scan.stitch->params.width; x++)
	  {
	    char pos[100];
	    sprintf (pos, "-%02i-%02i",y,x);
	    std::string name = (std::string)outtifname + (std::string)pos + (std::string)".tif";
	    if ((error = rparam.get_tile_adjustment (x,y).scanner_blur_correction->save_tiff (name.c_str ())))
	      {
		progress.pause_stdout ();
		fprintf (stderr, "Failed saving tiff file %s: %s\n", outtifname, error);
		return 1;
	      }
	  }
      }
  }
  return 0;
}

/* Implement "dump-lcc" command with ARGC and ARGV.  */
bool
dump_lcc (int argc, char **argv)
{
  subhelp = help_dump_lcc;
  if (argc != 1)
    print_help ();
  FILE *f = fopen (argv[0], "rt");
  if (!f)
    {
      perror (argv[0]);
      return 1;
    }
  auto c = backlight_correction_parameters::load_captureone_lcc (f, true);
  if (!c)
    {
      fprintf (stderr, "Failed to load %s\n", argv[0]);
      return 1;
    }
  // delete c; // shared_ptr handles this
  return 0;
}

/* Implement "export-lcc" command with ARGC and ARGV.  */
void
export_lcc (int argc, char **argv)
{
  const char *error = NULL;

  if (argc < 2 || argc > 3)
    print_help ();
  file_progress_info progress (stdout, verbose, verbose_tasks);
  image_data scan;
  if (!scan.load (argv[0], false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", argv[0], error);
      exit (1);
    }
  if (!scan.backlight_corr)
    {
      progress.pause_stdout ();
      fprintf (stderr, "No PhaseOne LCC in scan: %s\n", argv[0]);
      exit (1);
    }
  auto cor = scan.backlight_corr;
  FILE *out = fopen (argv[1], "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror ("Cannot open output file");
      exit (1);
    }
  if (!cor->save (out))
    {
      fprintf (stderr, "Cannot write %s\n", argv[1]);
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
  // delete cor; // shared_ptr handles this
}

/* Implement "read-chemcad-spectra" command with ARGC and ARGV.  */
void
read_chemcad (int argc, char **argv)
{
  FILE *f = NULL;
  spectrum s;
  spectrum sum;
  subhelp = help_read_chemcad_spectra;
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
      perror ("cannot open input file");
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
      float b, v;
      scanf ("%f %f\n", &b, &v);
      // printf ("%f %f\n",b,v);
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
      if (!(i % 10))
        printf ("\n  ");
      if (i != SPECTRUM_SIZE - 1)
        printf ("%f, ", v);
      else
        printf ("%f\n", v);
    }
}

/* Parse dye type PROFILE.  */
static enum spectrum_dyes_to_xyz::dyes
parse_dyes (const char *profile)
{
  return parse_enum_property<enum spectrum_dyes_to_xyz::dyes, property_t,
                    spectrum_dyes_to_xyz::dyes_names,
                    (int)spectrum_dyes_to_xyz::dyes_max> (profile,
                                                          "Unknown dye:%s\n");
}
/* Parse illuminant IL and return its temperature in TEMPERATURE.  */
static enum spectrum_dyes_to_xyz::illuminants
parse_illuminant (const char *il, luminosity_t *temperature)
{
  *temperature = 6500;
  if ((il[0] == 'D' || il[0] == 'd') && strlen (il) == 3 && il[1] > '0'
      && il[1] <= '9' && il[2] >= '0' && il[2] <= '9')
    {
      *temperature = (il[1] - '0') * 1000 + (il[2] - '0') * 100;
      return spectrum_dyes_to_xyz::il_D;
    }
  if (strlen (il) == 3 && il[0] >= '3' && il[0] <= '7' && il[1] >= '0'
      && il[1] <= '9' && il[2] >= '0' && il[2] <= '9')
    {
      *temperature = atoi (il);
      return spectrum_dyes_to_xyz::il_band;
    }
  for (int i = 0; i < (int)spectrum_dyes_to_xyz::illuminants_max; i++)
    if (!strcmp (il, spectrum_dyes_to_xyz::illuminants_names[i].name)
        && (spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_D
        && (spectrum_dyes_to_xyz::illuminants)i
               != spectrum_dyes_to_xyz::il_band)
      return (spectrum_dyes_to_xyz::illuminants)i;
  fprintf (stderr, "Unknown illuminant %s\n", il);
  fprintf (stderr, "Possible values are: D[0-9][0-9] for daylight, "
                   "[3-7][0-9][0-9] for single band");
  for (int i = 0; i < (int)spectrum_dyes_to_xyz::illuminants_max; i++)
    if ((spectrum_dyes_to_xyz::illuminants)i != spectrum_dyes_to_xyz::il_D
        && (spectrum_dyes_to_xyz::illuminants)i
               != spectrum_dyes_to_xyz::il_band)
      fprintf (stderr, ", %s", spectrum_dyes_to_xyz::illuminants_names[i].name);
  fprintf (stderr, "\n");
  exit (1);
}
/* Parse tone curve PROFILE.  */
static enum tone_curve::tone_curves
parse_tone_curve (const char *profile)
{
  return parse_enum_property<enum tone_curve::tone_curves, property_t,
                              tone_curve::tone_curve_names,
                              (int)tone_curve::tone_curve_max> (
      profile, "Unknown tone curve:%s\n");
}
/* Parse film response PROFILE.  */
static enum spectrum_dyes_to_xyz::responses
parse_response (const char *profile)
{
  return parse_enum_property<enum spectrum_dyes_to_xyz::responses, property_t,
                    spectrum_dyes_to_xyz::responses_names,
                    (int)spectrum_dyes_to_xyz::responses_max> (
      profile, "Unknown film response:%s\n");
}
/* Parse characteristic curve PROFILE.  */
static enum spectrum_dyes_to_xyz::characteristic_curves
parse_characteristic_curve (const char *profile)
{
  return parse_enum_property<enum spectrum_dyes_to_xyz::characteristic_curves,
                    property_t,
                    spectrum_dyes_to_xyz::characteristic_curve_names,
                    (int)spectrum_dyes_to_xyz::characteristic_curves_max> (
      profile, "Unknown film characteristic curve:%s\n");
}

/* Parse filename and camera setup from ARGC/ARGV.
   Return result in FILENAME and SPEC.
   If PATCH_SIZES is true, parse patch sizes as well.  */
void
parse_filename_and_camera_setup (int argc, char **argv, const char **filename,
                                 spectrum_dyes_to_xyz &spec,
                                 bool patch_sizes = false)
{
  if (argc != 5 + (patch_sizes ? 3 : 0)
      && (!patch_sizes || (argc != 6 || strcmp (argv[5], "dufay"))))
    {
      fprintf (stderr,
               "Expected parameters <filename> <backlight> <dyes> "
               "<film-sensitivity> <film-characteristic-curve>%s\n",
               patch_sizes ? " <rscal> <gscale> <bscale>" : "");
      if (patch_sizes)
        fprintf (stderr,
                 "last three options can be also replaced by \"dufay\"\n");
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

/* Implement "digital-laboratory" command with ARGC and ARGV.  */
void
digital_laboratory (int argc, char **argv)
{
  subhelp = help_lab;
  if (!argc)
    print_help ();
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
      spec.write_film_response (argv[1], NULL, argc == 3 ? true : false,
                                false);
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
          printf ("Expected <red-file> <green-file> <blue-file> "
                  "<film-characteristic-curve>\n");
          printf ("         or <red-file> <film-characteristic-curve>\n");
          print_help ();
        }
      spectrum_dyes_to_xyz spec;
      spec.set_characteristic_curve (
          parse_characteristic_curve (argv[argc - 1]));
      spec.write_film_characteristic_curves (
          argv[1], argc == 5 ? argv[2] : NULL, argc == 5 ? argv[3] : NULL);
    }
  else if (!strcmp (argv[0], "save-film-hd-characteristic-curve"))
    {
      if (argc != 3 && argc != 5)
        {
          printf ("Expected <red-file> <green-file> <blue-file> "
                  "<film-characteristic-curve>\n");
          printf ("         or <red-file> <film-characteristic-curve>\n");
          print_help ();
        }
      spectrum_dyes_to_xyz spec;
      spec.set_characteristic_curve (
          parse_characteristic_curve (argv[argc - 1]));
      spec.write_film_hd_characteristic_curves (
          argv[1], argc == 5 ? argv[2] : NULL, argc == 5 ? argv[3] : NULL);
    }
  else if (!strcmp (argv[0], "save-illuminant"))
    {
      if (argc != 3)
        {
          printf ("Expected <illuminant_filename> <illuminant>\n");
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
          printf ("Expected <red_filename> <green_filename> <blue_filename> "
                  "<dyes>\n");
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
          printf ("Expected <red_filename> <green_filename> <blue_filename> "
                  "<dyes> <response>\n");
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
          printf ("Expected <filename> <dyes> <response>\n");
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

      parse_filename_and_camera_setup (argc - 1, argv + 1, &filename, spec,
                                       true);
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
          fprintf (stderr, "Error writing %s\n", filename);
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
      tone_curve::tone_curves c = parse_tone_curve (argv[2]);
      FILE *f = fopen (argv[1], "wt");
      if (!f)
        {
          perror (argv[1]);
          exit (1);
        }
      tone_curve::save_tone_curve (f, c, false);
      fclose (f);
    }
  else if (!strcmp (argv[0], "save-tone-hd-curve"))
    {
      if (argc != 3)
        {
          printf ("Expected <filename> <tone-curve>\n");
          exit (1);
        }
      tone_curve::tone_curves c = parse_tone_curve (argv[2]);
      FILE *f = fopen (argv[1], "wt");
      if (!f)
        {
          perror (argv[1]);
          exit (1);
        }
      tone_curve::save_tone_curve (f, c, true);
      fclose (f);
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
    }
  else if (!strcmp (argv[0], "scan-primaries"))
    {
      spectrum_dyes_to_xyz spec;
      spectrum_dyes_to_xyz spec2;
      if (argc != 6)
        {
          printf (
              "Expected <scanner-backlight> <scanner-dyes> "
              "<scanner-ccd-response> <observing-backlight> <process-dyes>\n");
          exit (1);
        }

      luminosity_t temperature;
      auto il = parse_illuminant (argv[1], &temperature);
      spec.set_backlight (il, temperature);
      spec.set_dyes (parse_dyes (argv[2]));
      spec.set_film_response (parse_response (argv[3]));

      il = parse_illuminant (argv[4], &temperature);
      spec2.set_backlight (il, temperature);
      spec2.set_dyes (parse_dyes (argv[5]));

      spectrum white_spectrum;
      for (int i = 0; i < SPECTRUM_SIZE; i++)
        white_spectrum[i] = 1;
      rgbdata white_res = spec.linear_film_rgb_response (white_spectrum);
      printf ("Scanner white ");
      white_res.print (stdout);

      for (int do_balance = 0; do_balance < 2; do_balance++)
        {
          rgbdata colors[5];
          xyz targets[5];
          rgbdata balance;
	  bool do_white = false;
          if (do_balance)
            {
              balance = { 1 / white_res.red, 1 / white_res.green,
                          1 / white_res.blue };
              printf ("\n\nScanner white balance: %f %f %f\n", balance.red,
                      balance.green, balance.blue);
            }
          else
            {
              luminosity_t max = std::max (
                  white_res.red, std::max (white_res.green, white_res.blue));
              balance = { 1 / max, 1 / max, 1 / max };
              printf ("\n\nScanner exposure: %f\n", 1 / max);
            }
          colors[0] = spec.linear_film_rgb_response (spec2.red) * balance;
          colors[1] = spec.linear_film_rgb_response (spec2.green) * balance;
          colors[2] = spec.linear_film_rgb_response (spec2.blue) * balance;
          // colors[2] = spec.linear_film_rgb_response (white_spectrum) *
          // balance;
          colors[3] = { 0, 0, 0 };
	  if (do_white)
            colors[4] = spec.linear_film_rgb_response (white_spectrum) * balance;
          luminosity_t maxv = 0;
          for (int i = 0; i < 4; i++)
            {
              maxv = std::max (maxv, colors[i].red);
              maxv = std::max (maxv, colors[i].green);
              maxv = std::max (maxv, colors[i].blue);
            }
          for (int i = 0; i < 4; i++)
            colors[i] *= 0.3 / maxv;
          printf ("Additional exposure adjustment: %f\n", 1 / maxv);

          targets[0] = spec2.dyes_rgb_to_xyz (1, 0, 0);
          targets[1] = spec2.dyes_rgb_to_xyz (0, 1, 0);
          targets[2] = spec2.dyes_rgb_to_xyz (0, 0, 1);
          targets[3] = { 0, 0, 0 };
	  if (do_white)
            targets[4] = spec2.whitepoint_xyz ();
          for (int i = 0; i < (do_white ? 5 : 4); i++)
            {
              printf ("target %i simulated scanner readout: ", i);
              colors[i].print (stdout);
              printf ("target %i actual color: ", i);
              (targets[i] * (1 / 3.0)).print (stdout);
            }
          color_matrix m1 = determine_color_matrix (colors, targets, NULL, do_white ? 5 : 4,
                                                    spec2.whitepoint_xyz ());
          printf ("scanner to xyz matrix:\n");
          m1.print (stdout);
#if 0
	  for (int i = 0; i < 5; i++)
	  {
	    luminosity_t x,y,z;
	    m1.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &x, &y, &z);
	    printf ("i:%f %f %f %f %f %f\n", targets[i].x, targets[i].y, targets[i].z, x, y, z);
	  }
#endif
          {
            xyz wh;
            m1.apply_to_rgb (colors[4].red, colors[4].green, colors[4].blue,
                             &wh.x, &wh.y, &wh.z);
            printf ("Whitepoint drift from:");
            (targets[4] * 0.5).print (stdout);
            printf ("Whitepoint drift to: ");
            (wh * (targets[4].y / wh.y / 2)).print (stdout);
          }
          // printf ("xyz to scanner matrix:\n");
          // m1 = m1.invert ();
          // m1.print (stdout);
          printf ("\nScanner primary red: ");
          xyz red_primary
		  = { m1(0, 0), m1(1, 0), m1(2, 0) };
          red_primary.print (stdout);
          printf ("Scanner primary green: ");
          xyz green_primary
		  = { m1(0, 1), m1(1, 1), m1(2, 1) };
          green_primary.print (stdout);
          printf ("Scanner primary blue: ");
          xyz blue_primary
		  = { m1(0, 2), m1(1, 2), m1(2, 2) };
          blue_primary.print (stdout);
          m1 = bradford_whitepoint_adaptation_matrix (spec2.whitepoint_xyz (),
                                                      d50_white)
               * m1;
          printf ("\nScanner primary red Bradford corrected to D50: ");
          xyz corrected_red_primary
              = { m1(0, 0), m1(1, 0), m1(2, 0) };
          corrected_red_primary.print (stdout);
          printf ("Scanner primary green Bradford corrected to D50: ");
          xyz corrected_green_primary
              = { m1(0, 1), m1(1, 1), m1(2, 1) };
          corrected_green_primary.print (stdout);
          printf ("Scanner primary blue Bradford corrected to D50: ");
          xyz corrected_blue_primary
              = { m1(0, 2), m1(1, 2), m1(2, 2) };
          corrected_blue_primary.print (stdout);
          if (do_balance)
            printf ("\nThe following can be used in parameter file with "
                    "backlight correction on:\n");
          else
            printf ("\nThe following can be used in parameter file with no "
                    "backlight correction\n");
          printf ("scanner_red: %f %f %f\n", corrected_red_primary.x,
                  corrected_red_primary.y, corrected_red_primary.z);
          printf ("scanner_green: %f %f %f\n", corrected_green_primary.x,
                  corrected_green_primary.y, corrected_green_primary.z);
          printf ("scanner_blue: %f %f %f\n", corrected_blue_primary.x,
                  corrected_blue_primary.y, corrected_blue_primary.z);
        }
    }
  else if (!strcmp (argv[0], "compare-deltaE"))
    {
      if (argc != 4 && argc != 5)
        printf ("Expected <scan1> <par1> <par2> [<cmpfname>]\n");
      static const char *error;
      image_data scan;
      file_progress_info progress (stdout, verbose, verbose_tasks);
      scr_to_img_parameters param1, param2;
      render_parameters rparam1, rparam2;
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Loading scan %s\n", argv[1]);
	  progress.resume_stdout ();
	}
      if (!scan.load (argv[1], false, &error, &progress))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", argv[1], error);
	  exit(1);
	}
      FILE *in = fopen (argv[2], "rt");
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Loading color screen parameters: %s\n", argv[2]);
	  progress.resume_stdout ();
	}
      if (!in)
	{
	  progress.pause_stdout ();
	  perror (argv[2]);
	  return;
	}
      if (!load_csp (in, &param1, NULL, &rparam1, NULL, &error))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", argv[2], error);
	  exit(1);
	}
      fclose (in);
      in = fopen (argv[3], "rt");
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Loading color screen parameters: %s\n", argv[3]);
	  progress.resume_stdout ();
	}
      if (!in)
	{
	  progress.pause_stdout ();
	  perror (argv[3]);
	  exit(1);
	}
      if (!load_csp (in, &param2, NULL, &rparam2, NULL, &error))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", argv[3], error);
	  exit(1);
	}
      fclose (in);
      double deltae_avg, deltae_max;
      if (!compare_deltae (scan, param1, rparam1, param2, rparam2, argc == 4 ? NULL : argv[4], &deltae_avg, &deltae_max, &progress))
        {
	  fprintf (stderr, "Comparison failed\n");
	  exit (1);
        }
      progress.pause_stdout ();
      printf ("Robust deltaE 2000 (ignoring 1%% of exceptional samples) avg %f, max %f\n", deltae_avg, deltae_max);
    }
  else if (!strcmp (argv[0], "compare-images"))
    {
      if (argc != 3)
        {
          printf ("Expected <image1> <image2> %i\n", argc);
	  print_help ();
        }
      image_data scan,scan2;
      static const char *error;
      file_progress_info progress (stdout, verbose, verbose_tasks);
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Loading image %s\n", argv[1]);
	  progress.resume_stdout ();
	}
      if (!scan.load (argv[1], false, &error, &progress))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", argv[1], error);
	  exit(1);
	}
      if (verbose)
	{
	  progress.pause_stdout ();
	  printf ("Loading image %s\n", argv[2]);
	  progress.resume_stdout ();
	}
      if (!scan2.load (argv[2], false, &error, &progress))
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", argv[2], error);
	  exit(1);
	}
      if (scan.width != scan2.width || scan.height != scan2.height)
        {
	  fprintf (stderr, "Image dimensions differ: %ix%i compared to %ix%i\n",
		   scan.width, scan.height, scan2.width, scan2.height);
	  exit (1);
        }
      progress.set_task ("comparing data", 1);
      if (scan.has_grayscale_or_ir ())
        {
	  float max_diff = 0;
	  if (!scan2.has_grayscale_or_ir ())
	    {
	      fprintf (stderr, "One image has BW/IR channel while the other does not\n");
	      exit (1);
	    }
	  for (int y = 0; y < scan.height; y++)
	    for (int x = 0; x < scan.width; x++)
	    {
	      float diff = fabs (scan.get_pixel (x, y) / (float)scan.maxval -
				 scan2.get_pixel (x, y) / (float)scan2.maxval);
	      max_diff = std::max (max_diff, diff);
	    }
	  if (max_diff * 2 * 65536 > 1)
	    {
	      fprintf (stderr, "Images differ; max difference is %f\n", max_diff);
	      exit (1);
	    }
        }
      if (scan.has_rgb ())
        {
	  float max_diff = 0;
	  if (!scan2.has_rgb ())
	    {
	      fprintf (stderr, "One image has RGB channel while the other does not\n");
	      exit (1);
	    }
	  for (int y = 0; y < scan.height; y++)
	    for (int x = 0; x < scan.width; x++)
	    {
	      float diff = fabs (scan.get_rgb_pixel (x, y).r / (float)scan.maxval -
				 scan2.get_rgb_pixel (x, y).r / (float)scan2.maxval);
	      max_diff = std::max (max_diff, diff);
	      diff = fabs (scan.get_rgb_pixel (x, y).g / (float)scan.maxval -
			   scan2.get_rgb_pixel (x, y).g / (float)scan2.maxval);
	      max_diff = std::max (max_diff, diff);
	      diff = fabs (scan.get_rgb_pixel (x, y).b / (float)scan.maxval -
			   scan2.get_rgb_pixel (x, y).b / (float)scan2.maxval);
	      max_diff = std::max (max_diff, diff);
	    }
	  if (max_diff * 2 * 65536 > 1)
	    {
	      fprintf (stderr, "Images differ; max difference is %f\n", max_diff);
	      exit (1);
	    }
        }
      exit (0);
    }
  else
    print_help ();
}

/* Return screen channel C from result R.  */
static rgbdata
get_screen_channel (finetune_result &r, int c)
{
	if (c == 0)
		return r.screen_red;
	else if (c == 1)
		return r.screen_green;
	return r.screen_blue;
}

/* Implement "finetune" command with ARGC and ARGV.  */
static void
finetune (int argc, char **argv)
{
  const char *error = NULL;

  const char *infname = NULL;
  const char *cspname = NULL;
  const char *screen_blur_tiff_name = NULL;
  const char *fog_tiff_name = NULL;
  const char *strip_width_tiff_name = NULL;
  const char *position_tiff_name = NULL;
  const char *orig_tiff_base = NULL;
  const char *simulated_tiff_base = NULL;
  const char *diff_tiff_base = NULL;
  const char *screen_color_tiff_base = NULL;
  int multitile = 1;
  int_point_t steps = { 32, 0 };
  int border = 5;
  int steps_x_tmp = steps.x;
  uint64_t flags = 0;
  //*finetune_no_progress_report | finetune_screen_blur| finetune_dufay_strips;
  subhelp = help_finetune;

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (arg == "--optimize-position")
        flags |= finetune_position;
      else if (arg == "--optimize-fog")
        flags |= finetune_fog;
      else if (arg == "--optimize-screen-blur")
        flags |= finetune_screen_blur;
      else if (arg == "--optimize-scanner-mtf-sigma")
        flags |= finetune_scanner_mtf_sigma;
      else if (arg == "--optimize-scanner-mtf-defocus")
        flags |= finetune_scanner_mtf_defocus;
      else if (arg == "--optimize-scanner-mtf-channel-defocus")
        flags |= finetune_scanner_mtf_channel_defocus;
      else if (arg == "--optimize-screen-channel-blur")
        flags |= finetune_screen_channel_blurs;
      else if (arg == "--optimize-emulsion-blur")
        flags |= finetune_emulsion_blur;
      else if (arg == "--use-monochrome-channel")
        flags |= finetune_bw;
      else if (arg == "--optimize-strips")
        flags |= finetune_strips;
      else if (arg == "--optimize-sharpening")
        flags |= finetune_sharpening;
      else if (arg == "--no-normalize")
        flags |= finetune_no_normalize;
      else if (arg == "--no-least-squares")
        flags |= finetune_no_least_squares;
      else if (arg == "--no-data-collection")
        flags |= finetune_no_data_collection;
      else if (arg == "--simulate-infrared")
        flags |= finetune_simulate_infrared;
      else if (parse_int_param (argc, argv, &i, "multitile", multitile, 1,
                                100))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "fog-tiff"))
        fog_tiff_name = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "blur-tiff"))
        screen_blur_tiff_name = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "strips-tiff"))
        strip_width_tiff_name = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "position-tiff"))
        position_tiff_name = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "orig-tiff-base"))
        orig_tiff_base = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "screen-color-tiff-base"))
        screen_color_tiff_base = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "simulated-tiff-base"))
        simulated_tiff_base = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "diff-tiff-base"))
        diff_tiff_base = str;
      else if (parse_int_param (argc, argv, &i, "width", steps_x_tmp, 1, 65536 / 2))
        steps.x = steps_x_tmp;
      else if (!infname)
        infname = argv[i];
      else if (!cspname)
        cspname = argv[i];
      else
        print_help (argv[i]);
    }
  if (!flags)
    {
      fprintf (stderr, "No --optimize flags set\n");
      print_help ();
    }
  if (!infname)
    {
      fprintf (stderr, "Missing input file\n");
      print_help ();
    }
  if (!cspname)
    {
      fprintf (stderr, "Missing parameter file\n");
      print_help ();
    }

  if (!infname || !cspname || !flags)
    print_help ();
  flags |= finetune_no_progress_report;
  file_progress_info progress (stdout, verbose, verbose_tasks);
  image_data scan;
  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Loading scan %s\n", infname);
      progress.resume_stdout ();
    }
  if (!scan.load (infname, true, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", infname, error);
      exit (1);
    }

  FILE *in = fopen (cspname, "rt");
  if (!in)
    {
      progress.pause_stdout ();
      perror (cspname);
      exit (1);
    }

  render_parameters rparam;
  scr_to_img_parameters param;
  if (!load_csp (in, &param, NULL, &rparam, NULL, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);

  steps.y = (steps.x * scan.height + scan.width / 2) / scan.width;
  if (!steps.y)
    steps.y = 1;

  std::vector<finetune_result> results (steps.y * steps.x);
  progress.set_task ("analyzing samples", steps.y * steps.x);
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(steps, rparam, scan, flags, border, progress, param,               \
               orig_tiff_base, simulated_tiff_base, diff_tiff_base, results,  \
               multitile)
  for (int y = 0; y < steps.y; y++)
    for (int x = 0; x < steps.x; x++)
      {
        int_point_t border_px = { scan.width * border / 100, scan.height * border / 100 };
        point_t sample_pos = { (coord_t)(border_px.x + x * (scan.width - 2 * border_px.x) / steps.x),
                               (coord_t)(border_px.y + y * (scan.height - 2 * border_px.y) / steps.y) };
        char pos[256];
        std::string orig_file;
        std::string simulated_file;
        std::string diff_file;
        sprintf (pos, "-%04i-%04i.tif", y, x);
        finetune_parameters fparam;
        fparam.flags = flags;
        fparam.multitile = multitile;
        if (orig_tiff_base)
          {
            orig_file = (std::string)orig_tiff_base + (std::string)pos;
            fparam.orig_file = orig_file.c_str ();
          }
        if (simulated_tiff_base)
          {
            simulated_file
                = (std::string)simulated_tiff_base + (std::string)pos;
            fparam.simulated_file = simulated_file.c_str ();
          }
        if (diff_tiff_base)
          {
            diff_file = (std::string)diff_tiff_base + (std::string)pos;
            fparam.diff_file = diff_file.c_str ();
          }
        results[y * steps.x + x] = finetune (
            rparam, param, scan, { sample_pos }, NULL,
            fparam, &progress);
        progress.inc_progress ();
      }
  int nok = 0;
  for (int y = 0; y < steps.y; y++)
    for (int x = 0; x < steps.x; x++)
      if (results[y * steps.x + x].success)
        nok++;
      else
        {
          progress.pause_stdout ();
          fprintf (stderr, "Failed to analyze sample %i %i:%s\n", x, y,
                   results[y * steps.x + x].err.c_str ());
          progress.resume_stdout ();
        }
  if (!nok)
    {
      progress.pause_stdout ();
      fprintf (stderr, "All points failed\n");
      exit (1);
    }
  progress.pause_stdout ();
  printf ("Badness\n");
  for (int y = 0; y < steps.y; y++)
    {
      for (int x = 0; x < steps.x; x++)
        if (results[y * steps.x + x].success)
          printf ("  %6.3f", results[y * steps.x + x].badness);
        else
          printf ("  ------");
      printf ("\n");
    }
  printf ("Uncertainity\n");
  for (int y = 0; y < steps.y; y++)
    {
      for (int x = 0; x < steps.x; x++)
        if (results[y * steps.x + x].success)
          printf ("  %6.3f", results[y * steps.x + x].uncertainty);
        else
          printf ("  ------");
      printf ("\n");
    }

  if (flags
      & (finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus | finetune_screen_blur
         | finetune_screen_channel_blurs | finetune_emulsion_blur))
    {
      histogram hist, emulsion_hist;
      rgb_histogram channel_hist;
      for (int y = 0; y < steps.y; y++)
        for (int x = 0; x < steps.x; x++)
          if (results[y * steps.x + x].success)
            {
              hist.pre_account (results[y * steps.x + x].screen_blur_radius);
              emulsion_hist.pre_account (
                  results[y * steps.x + x].emulsion_blur_radius);
              channel_hist.pre_account (
                  results[y * steps.x + x].screen_channel_blur_radius);
            }
      hist.finalize_range (65536);
      emulsion_hist.finalize_range (65536);
      channel_hist.finalize_range (65536);
      for (int y = 0; y < steps.y; y++)
        for (int x = 0; x < steps.x; x++)
          if (results[y * steps.x + x].success)
            {
              hist.account (results[y * steps.x + x].screen_blur_radius);
              emulsion_hist.account (
                  results[y * steps.x + x].emulsion_blur_radius);
              channel_hist.account (
                  results[y * steps.x + x].screen_channel_blur_radius);
            }
      hist.finalize ();
      emulsion_hist.finalize ();
      channel_hist.finalize ();
      if (flags & (finetune_scanner_mtf_sigma))
        {
          printf ("Detected scanner mtf sigma (pixels)\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf ("  %1.3f",
                          results[y * steps.x + x].scanner_mtf_sigma);
                else
                  printf ("  -----");
              printf ("\n");
            }
        }
      else if (flags & (finetune_scanner_mtf_defocus) && rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
        {
          printf ("Detected scanner mtf defocus (mm)\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf ("  %1.3f",
                          results[y * steps.x + x].scanner_mtf_defocus);
                else
                  printf ("  -----");
              printf ("\n");
            }
        }
      else if (flags & (finetune_scanner_mtf_defocus) && !rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
        {
          printf ("Detected scanner mtf blur diameter (pixels)\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf ("  %1.3f",
                          results[y * steps.x + x].scanner_mtf_blur_diameter);
                else
                  printf ("  -----");
              printf ("\n");
            }
        }
      else if (flags & finetune_screen_blur)
        {
          printf ("Detected screen blurs\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf ("  %6.3f",
                          results[y * steps.x + x].screen_blur_radius);
                else
                  printf ("  ------");
              printf ("\n");
            }
          printf ("Screen blur robust min %f, avg %f, max %f\n",
                  hist.find_min (0.1), hist.find_avg (0.1, 0.1),
                  hist.find_max (0.1));
        }
      else if (flags & finetune_scanner_mtf_channel_defocus)
        {
          printf ("Detected screen channel blurs\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf (
                      "  %6.3f,%6.3f,%6.3f",
                      results[y * steps.x + x].scanner_mtf_channel_defocus_or_blur.red,
                      results[y * steps.x + x].scanner_mtf_channel_defocus_or_blur.green,
                      results[y * steps.x + x].scanner_mtf_channel_defocus_or_blur.blue);
                else
                  printf ("  ------");
              printf ("\n");
            }
          printf ("Red screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).red,
                  channel_hist.find_avg (0.1, 0.1).red,
                  channel_hist.find_max (0.1).red);
          printf ("Green screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).green,
                  channel_hist.find_avg (0.1, 0.1).green,
                  channel_hist.find_max (0.1).green);
          printf ("Blue screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).blue,
                  channel_hist.find_avg (0.1, 0.1).blue,
                  channel_hist.find_max (0.1).blue);
        }
      else if (flags & finetune_screen_channel_blurs)
        {
          printf ("Detected screen channel blurs\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf (
                      "  %6.3f,%6.3f,%6.3f",
                      results[y * steps.x + x].screen_channel_blur_radius.red,
                      results[y * steps.x + x].screen_channel_blur_radius.green,
                      results[y * steps.x + x].screen_channel_blur_radius.blue);
                else
                  printf ("  ------");
              printf ("\n");
            }
          printf ("Red screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).red,
                  channel_hist.find_avg (0.1, 0.1).red,
                  channel_hist.find_max (0.1).red);
          printf ("Green screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).green,
                  channel_hist.find_avg (0.1, 0.1).green,
                  channel_hist.find_max (0.1).green);
          printf ("Blue screen blur robust min %f, avg %f, max %f\n",
                  channel_hist.find_min (0.1).blue,
                  channel_hist.find_avg (0.1, 0.1).blue,
                  channel_hist.find_max (0.1).blue);
        }
      else
        {
          printf ("Detected emulsion blurs\n");
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  printf ("  %6.3f",
                          results[y * steps.x + x].emulsion_blur_radius);
                else
                  printf ("  ------");
              printf ("\n");
            }
          printf ("Emulsion blur robust min %f, avg %f, max %f\n",
                  emulsion_hist.find_min (0.1),
                  emulsion_hist.find_avg (0.1, 0.1),
                  emulsion_hist.find_max (0.1));
        }
      if (screen_blur_tiff_name)
        {
          tiff_writer_params p;
          p.filename = screen_blur_tiff_name;
          p.width = steps.x;
          p.height = steps.y;
          p.depth = 16;
          const char *error;
          tiff_writer sharpness (p, &error);
          if (error)
            {
              progress.pause_stdout ();
              fprintf (stderr, "Cannot open tiff file %s: %s\n",
                       screen_blur_tiff_name, error);
              exit (1);
            }
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (!results[y * steps.x + x].success)
                  sharpness.put_pixel (x, 65535, 0, 0);
                else if (flags & (finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus))
                  {
                    int vr
                        = std::min (results[y * steps.x + x].scanner_mtf_sigma
                                        * 0.5 * 65535,
                                    (coord_t)65535);
		    double def;
		    if (rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
		      def = results[y * steps.x + x].scanner_mtf_defocus;
		    else
		      def = results[y * steps.x + x].scanner_mtf_blur_diameter;
                    int vg
                        = std::min (def * 65535, (coord_t)65535);
                    int vb = 0;
                    sharpness.put_pixel (x, vr, vg, vb);
                  }
                else if (flags & finetune_screen_blur)
                  {
                    int v = std::min (
                        results[y * steps.x + x].screen_blur_radius / 2 * 65535,
                        (coord_t)65535);
                    sharpness.put_pixel (x, v, v, v);
                  }
                else if (flags & finetune_screen_channel_blurs)
                  {
                    int vr = std::min (
                        results[y * steps.x + x].screen_channel_blur_radius.red
                            / 2 * 65535,
                        (luminosity_t)65535);
                    int vg
                        = std::min (results[y * steps.x + x]
                                            .screen_channel_blur_radius.green
                                        / 2 * 65535,
                                    (luminosity_t)65535);
                    int vb = std::min (
                        results[y * steps.x + x].screen_channel_blur_radius.blue
                            / 2 * 65535,
                        (luminosity_t)65535);
                    sharpness.put_pixel (x, vr, vg, vb);
                  }
                else
                  {
                    int v = std::min (
                        results[y * steps.x + x].emulsion_blur_radius / 2
                            * 65535,
                        (coord_t)65535);
                    sharpness.put_pixel (x, v, v, v);
                  }
              if (!sharpness.write_row ())
                {
                  progress.pause_stdout ();
                  fprintf (stderr, "Error writing tiff file %s\n", argv[2]);
                  exit (1);
                }
            }
        }
    }
  if (scan.has_rgb () && !(flags & finetune_bw))
    {
      for (int c = 0; c < 3; c++)
	{
	  const char *cname[3]={"red", "green", "blue"};
	  printf ("Detected screen %s\n", cname[c]);
	  for (int y = 0; y < steps.y; y++)
	    {
	      for (int x = 0; x < steps.x; x++)
		if (results[y * steps.x + x].success)
		  get_screen_channel (results[y * steps.x + x],c).print (stdout);
		else
		  printf ("  ------");
	      printf ("\n");
	    }
	    
	  if (screen_color_tiff_base)
	    {
	      tiff_writer_params p;
	      std::string name = (std::string)screen_color_tiff_base + "-" + cname[c] + ".tif";
	      p.filename = name.c_str ();
	      p.width = steps.x;
	      p.height = steps.y;
	      p.hdr = true;
	      p.depth = 32;
	      const char *error;
	      tiff_writer sharpness (p, &error);
	      if (error)
		{
		  progress.pause_stdout ();
		  fprintf (stderr, "Cannot open tiff file %s: %s\n",
			   screen_blur_tiff_name, error);
		  exit (1);
		}
	      for (int y = 0; y < steps.y; y++)
		{
		  for (int x = 0; x < steps.x; x++)
		    if (!results[y * steps.x + x].success)
		      sharpness.put_hdr_pixel (x, 1, 0, 0);
		    else
		      sharpness.put_hdr_pixel (
			  x, get_screen_channel (results[y * steps.x + x], c).red * 1,
			  get_screen_channel (results[y * steps.x + x], c).green * 1,
			  get_screen_channel (results[y * steps.x + x], c).blue * 1);
		  if (!sharpness.write_row ())
		    {
		      progress.pause_stdout ();
		      fprintf (stderr, "Error writing tiff file %s\n", argv[2]);
		      exit (1);
		    }
		}
	    }
	}
    }
  if (flags & finetune_fog)
    {
      printf ("Detected fog\n");
      for (int y = 0; y < steps.y; y++)
        {
          for (int x = 0; x < steps.x; x++)
            if (results[y * steps.x + x].success)
              results[y * steps.x + x].fog.print (stdout);
            else
              printf ("  ------");
          printf ("\n");
        }
      if (fog_tiff_name)
        {
          tiff_writer_params p;
          p.filename = fog_tiff_name;
          p.width = steps.x;
          p.height = steps.y;
	  p.hdr = true;
          p.depth = 32;
          const char *error;
          tiff_writer sharpness (p, &error);
          if (error)
            {
              progress.pause_stdout ();
              fprintf (stderr, "Cannot open tiff file %s: %s\n",
                       screen_blur_tiff_name, error);
              exit (1);
            }
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (!results[y * steps.x + x].success)
                  sharpness.put_hdr_pixel (x, 1, 0, 0);
                else
                  sharpness.put_hdr_pixel (
                      x, results[y * steps.x + x].fog.red * 1,
                      results[y * steps.x + x].fog.green * 1,
                      results[y * steps.x + x].fog.blue * 1);
              if (!sharpness.write_row ())
                {
                  progress.pause_stdout ();
                  fprintf (stderr, "Error writing tiff file %s\n", argv[2]);
                  exit (1);
                }
            }
        }
    }
  if ((flags & finetune_strips) && screen_with_varying_strips_p (param.type))
    {
      printf ("Detected screen strip widths (red, green)\n");
      histogram histr;
      histogram histg;
      for (int y = 0; y < steps.y; y++)
        for (int x = 0; x < steps.x; x++)
          if (results[y * steps.x + x].success)
            {
              histr.pre_account (
                  results[y * steps.x + x].red_strip_width);
              histg.pre_account (
                  results[y * steps.x + x].green_strip_width);
            }
      histr.finalize_range (65536);
      histg.finalize_range (65536);
      for (int y = 0; y < steps.y; y++)
        for (int x = 0; x < steps.x; x++)
          if (results[y * steps.x + x].success)
            {
              histr.account (results[y * steps.x + x].red_strip_width);
              histg.account (results[y * steps.x + x].green_strip_width);
            }
      histr.finalize ();
      histg.finalize ();
      printf ("Red strip width robust min %f, avg %f, max %f\n",
              histr.find_min (0.1), histr.find_avg (0.1, 0.1),
              histr.find_max (0.1));
      printf ("Green strip width robust min %f, avg %f, max %f\n",
              histg.find_min (0.1), histg.find_avg (0.1, 0.1),
              histg.find_max (0.1));
      for (int y = 0; y < steps.y; y++)
        {
          for (int x = 0; x < steps.x; x++)
            if (results[y * steps.x + x].success)
              printf ("  %.3f,%.3f",
                      results[y * steps.x + x].red_strip_width,
                      results[y * steps.x + x].green_strip_width);
            else
              printf ("  ------");
          printf ("\n");
        }
      if (strip_width_tiff_name)
        {
          tiff_writer_params p;
          p.filename = strip_width_tiff_name;
          p.width = steps.x;
          p.height = steps.y;
          p.depth = 16;
          const char *error;
          tiff_writer sharpness (p, &error);
          if (error)
            {
              progress.pause_stdout ();
              fprintf (stderr, "Cannot open tiff file %s: %s\n",
                       strip_width_tiff_name, error);
              exit (1);
            }
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (results[y * steps.x + x].success)
                  {
                    coord_t r = results[y * steps.x + x].red_strip_width;
                    coord_t g = results[y * steps.x + x].green_strip_width
                                * (1 - r);
                    coord_t b
                        = (1 - results[y * steps.x + x].green_strip_width)
                          * (1 - r);
                    sharpness.put_pixel (x, r * 65535, g * 65535, b * 65535);
                  }
                else
                  sharpness.put_pixel (x, 65535, 0, 0);
              if (!sharpness.write_row ())
                {
                  progress.pause_stdout ();
                  fprintf (stderr, "Error writing tiff file %s\n", argv[2]);
                  exit (1);
                }
            }
        }
    }
  if ((flags & finetune_position))
    {
      printf ("Detected position adjustment (in screen coordinates)\n");
      for (int y = 0; y < steps.y; y++)
        {
          for (int x = 0; x < steps.x; x++)
            if (results[y * steps.x + x].success)
              printf ("  %.3f,%.3f",
                      results[y * steps.x + x].screen_coord_adjust.x,
                      results[y * steps.x + x].screen_coord_adjust.y);
            else
              printf ("  ------");
          printf ("\n");
        }
      if (position_tiff_name)
        {
          tiff_writer_params p;
          p.filename = position_tiff_name;
          p.width = steps.x;
          p.height = steps.y;
          p.depth = 16;
          const char *error;
          tiff_writer sharpness (p, &error);
          if (error)
            {
              progress.pause_stdout ();
              fprintf (stderr, "Cannot open tiff file %s: %s\n",
                       position_tiff_name, error);
              exit (1);
            }
          for (int y = 0; y < steps.y; y++)
            {
              for (int x = 0; x < steps.x; x++)
                if (!results[y * steps.x + x].success)
                  sharpness.put_pixel (x, 65535, 0, 1);
                else
                  {
                    coord_t r = std::min (
                        fabs (results[y * steps.x + x].screen_coord_adjust.x
                              * 10),
                        (coord_t)1);
                    coord_t g = std::min (
                        fabs (results[y * steps.x + x].screen_coord_adjust.y
                              * 10),
                        (coord_t)1);
                    coord_t b = 0;
                    sharpness.put_pixel (x, r * 65535, g * 65535, b * 65535);
                  }
              if (!sharpness.write_row ())
                {
                  progress.pause_stdout ();
                  fprintf (stderr, "Error writing tiff file %s\n", argv[2]);
                  exit (1);
                }
            }
        }
    }
}

/* Implement "dump-patch-density" command with ARGC and ARGV.  */
int
dump_patch_density (int argc, char **argv)
{
  const char *error = NULL;
  subhelp = help_dump_patch_density;

  if (argc != 3)
    print_help ();
  verbose = 1;
  file_progress_info progress (stdout, verbose, verbose_tasks);
  image_data scan;
  if (!scan.load (argv[0], false, &error, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", argv[0], error);
      return 1;
    }

  FILE *in = fopen (argv[1], "rt");
  if (!in)
    {
      progress.pause_stdout ();
      perror (argv[1]);
      return 1;
    }

  scr_to_img_parameters param;
  render_parameters rparam;
  if (!load_csp (in, &param, NULL, &rparam, NULL, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Cannot load %s: %s\n", argv[1], error);
      return 1;
    }
  fclose (in);
  FILE *out = fopen (argv[2], "wt");
  if (!out)
    {
      progress.pause_stdout ();
      perror (argv[2]);
      return 1;
    }
  if (!dump_patch_density (out, scan, param, rparam, &progress))
    {
      progress.pause_stdout ();
      fprintf (stderr, "Saving of %s failed: %s\n", argv[1], error);
      return 1;
    }
  fclose (out);
  return 0;
}
static const char *save_project_filename;
static const char *load_project_filename;

/* Implement "stitch" command with ARGC and ARGV.  */
int
stitch (int argc, char **argv)
{
  std::vector<std::string> fnames;
  int ncols = 0;
  subhelp = help_stitch;
  detect_regular_screen_params dsparams;

  auto prj = std::make_unique<stitch_project> ();

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      float flt;
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (parse_detect_regular_screen_params (dsparams, true, argc, argv,
                                                   &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "report"))
        prj->params.report_filename = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "par"))
        prj->params.csp_filename = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "hugin-pto"))
        prj->params.hugin_pto_filename = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "out"))
        save_project_filename = str;
      else if (const char *str
               = arg_with_param (argc, argv, &i, "load-project"))
        load_project_filename = str;
      else if (arg == "--no-cpfind")
        prj->params.cpfind = 0;
      else if (arg == "--cpfind")
        prj->params.cpfind = 1;
      else if (arg == "--cpfind-verification")
        prj->params.cpfind = 2;
      else if (arg == "--load-registration")
        prj->params.load_registration = true;
      else if (arg == "--screen-tiles")
        prj->params.screen_tiles = true;
      else if (arg == "--known-screen-tiles")
        prj->params.known_screen_tiles = true;
      else if (arg == "--verbose")
        verbose = true;
      else if (arg == "--panorama-map")
        prj->params.panorama_map = true;
      else if (arg == "--reoptimize-colors")
        prj->params.reoptimize_colors = true;
      else if (arg == "--limit-directions")
        prj->params.limit_directions = true;
      else if (arg == "--no-limit-directions")
        prj->params.limit_directions = false;
      else if (parse_float_param (argc, argv, &i, "outer-tile-border", flt, 0,
                                  100))
        prj->params.outer_tile_border = flt;
      else if (parse_float_param (argc, argv, &i, "inner-tile-border", flt, 0,
                                  100))
        prj->params.inner_tile_border = flt;
      else if (parse_float_param (argc, argv, &i, "max-contrast", flt, 0, 100))
        prj->params.max_contrast = flt;
      else if (parse_float_param (argc, argv, &i, "max-overlap", flt, 0, 100))
        prj->params.max_overlap_percentage = atoi (argv[i]);
      else if (parse_int_param (argc, argv, &i, "ncols", ncols, 0,
                                stitching_params::max_dim))
        ;
      else if (parse_int_param (argc, argv, &i, "num-control-points",
                                prj->params.num_control_points, 0,
                                stitching_params::max_dim))
        ;
      else if (parse_float_param (argc, argv, &i, "scan-ppi", flt, 0, 100))
        prj->params.scan_xdpi = prj->params.scan_ydpi = flt;
      else if (parse_float_param (argc, argv, &i, "hfov", flt, 0, 100))
        prj->params.hfov = flt;
      else if (parse_float_param (argc, argv, &i, "max-avg-distance", flt, 0,
                                  100000))
        prj->params.max_avg_distance = flt;
      else if (parse_float_param (argc, argv, &i, "max-max-distance", flt, 0,
                                  100000))
        prj->params.max_max_distance = flt;
      else if (arg == "--geometry-info")
        prj->params.geometry_info = true;
      else if (arg == "--individual-geometry-info")
        prj->params.individual_geometry_info = true;
      else if (arg == "--outliers-info")
        prj->params.outliers_info = true;
      else if (arg == "--diffs")
        prj->params.diffs = true;
      else if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-')
        {
          fprintf (stderr, "Unknown parameter: %s\n", argv[i]);
          print_help (argv[i]);
          return 1;
        }
      else
        {
          std::string name = argv[i];
          fnames.push_back (name);
        }
    }
  if (!save_project_filename && !load_project_filename)
    {
      fprintf (stderr, "Output filename via --out is not specified\n");
      print_help ();
      return 1;
    }
  if (!load_project_filename)
    {
      if (!fnames.size ())
        {
          fprintf (stderr, "No files to stitch\n");
          print_help ();
          return 1;
        }
      if (fnames.size () == 1)
        {
          prj->params.width = 1;
          prj->params.height = 1;
        }
      if (ncols > 0)
        {
          prj->params.width = ncols;
          prj->params.height = fnames.size () / ncols;
        }
      else
        {
          int indexpos;
          if (fnames[0].length () != fnames[1].length ())
            {
              fprintf (stderr,
                       "Cannot determine organization of tiles in '%s'.  "
                       "Expect filenames of kind <name>yx<suffix>.tif\n",
                       fnames[0].c_str ());
              return 1;
            }
          for (indexpos = 0; indexpos < (int)fnames[0].length () - 2;
               indexpos++)
            if (fnames[0][indexpos] != fnames[1][indexpos]
                || (fnames[0][indexpos] == '1'
                    && fnames[0][indexpos + 1] != fnames[1][indexpos + 1]))
              break;
          if (fnames[0][indexpos] != '1' || fnames[0][indexpos + 1] != '1')
            {
              fprintf (stderr,
                       "Cannot determine organization of tiles in '%s'.  "
                       "Expect filenames of kind <name>yx<suffix>.tif\n",
                       fnames[0].c_str ());
              return 1;
            }
          int w;
          for (w = 1; w < (int)fnames.size (); w++)
            if (fnames[w][indexpos + 1] != '1' + w)
              break;
          prj->params.width = w;
          prj->params.height = fnames.size () / w;
          for (int y = 0; y < prj->params.height; y++)
            for (int x = 0; x < prj->params.width; x++)
              {
                int i = y * prj->params.width + x;
                if (fnames[i].length () != fnames[0].length ()
                    || fnames[i][indexpos] != '1' + y
                    || fnames[i][indexpos + 1] != '1' + x)
                  {
                    fprintf (stderr,
                             "Unexpected tile filename '%s' for tile %i %i\n",
                             fnames[i].c_str (), y + 1, x + 1);
                    return 1;
                  }
              }
        }
      if (prj->params.width * prj->params.height != (int)fnames.size ())
        {
          fprintf (stderr, "For %ix%i tiles I expect %i filenames, found %i\n",
                   prj->params.width, prj->params.height,
                   prj->params.width * prj->params.height,
                   (int)fnames.size ());
          return 1;
        }
      for (int y = 0; y < prj->params.height; y++)
        for (int x = 0; x < prj->params.width; x++)
          prj->images[y][x].filename = fnames[y * prj->params.width + x];
    }
  else
    {
      if (fnames.size ())
        {
          fprintf (stderr, "Unknown parameter %s\n", fnames[0].c_str ());
          return 1;
        }
    }

  {
    file_progress_info progress (stdout, verbose, verbose_tasks);
    if (!prj->stitch (&progress, &dsparams, load_project_filename))
      return 1;
  }
  if (save_project_filename)
    {
      FILE *f = fopen (save_project_filename, "wt");
      if (!f)
        {
          fprintf (stderr, "Error opening project file: %s\n",
                   save_project_filename);
          return 1;
        }
      if (!prj->save (f))
        {
          fprintf (stderr, "Error saving project file: %s\n",
                   save_project_filename);
          return 1;
        }
      fclose (f);
    }
  return 0;
}

/* Implement "mtf" command with ARGC and ARGV.  */
int
do_mtf (int argc, char **argv)
{
  const char *cspname = NULL, *error = NULL;
  const char *csvname = NULL;
  const char *psfname = NULL;
  const char *psfname2 = NULL;
  const char *quickmtfname = NULL;
  float sigma = 0, blur_diameter = 0, pixel_pitch = 0, wavelength = 0, f_stop = 0, defocus = 0, scan_dpi = 0;
  bool match = false;
  int flags = mtf_parameters::estimate_use_nmsimplex | mtf_parameters::estimate_use_multifit;

  render_parameters rparam;
  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      float flt;
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (arg == "--match")
	match = true;
      else if (arg == "--simplex")
	flags |= mtf_parameters::estimate_use_nmsimplex;
      else if (arg == "--no-simplex")
	flags &= ~mtf_parameters::estimate_use_nmsimplex;
      else if (arg == "--multifit")
	flags |= mtf_parameters::estimate_use_multifit;
      else if (arg == "--no-multifit")
	flags &= ~mtf_parameters::estimate_use_multifit;
      else if (arg == "--verbose-solving")
	flags |= mtf_parameters::estimate_verbose_solving;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-csv"))
        csvname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "load-quickmtf"))
        quickmtfname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-psf"))
        psfname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-matched-psf"))
        psfname2 = str;
      else if (parse_float_param (argc, argv, &i, "sigma", flt, 0, 10))
	sigma = flt;
      else if (parse_float_param (argc, argv, &i, "blur-diameter", flt, 0, 10))
	blur_diameter = flt;
      else if (parse_float_param (argc, argv, &i, "pixel-pitch", flt, 0, 1000))
	pixel_pitch = flt;
      else if (parse_float_param (argc, argv, &i, "wavelength", flt, 300, 2000))
	wavelength = flt;
      else if (parse_float_param (argc, argv, &i, "f-stop", flt, 0, 1000))
	f_stop = flt;
      else if (parse_float_param (argc, argv, &i, "defocus", flt, 0, 10))
	defocus = flt;
      else if (parse_float_param (argc, argv, &i, "dpi", flt, 0, 10))
	scan_dpi = flt;
      else
	{
	  if (!cspname)
	    cspname = argv[i];
	  else
	    print_help (argv[i]);
	}
    }
  if (verbose)
    flags |= mtf_parameters::estimate_verbose;
  if (!cspname && !quickmtfname && !sigma && !blur_diameter && !pixel_pitch)
    {
      fprintf (stderr, "No filename given and no MTF parameters specified\n");
      exit (-1);
    }
  if (cspname)
    {
      FILE *in = fopen (cspname, "rt");
      if (verbose)
	{
	  printf ("Loading color screen parameters: %s\n", cspname);
	}
      if (!in)
	{
	  perror (cspname);
	  return 1;
	}
      if (!load_csp (in, NULL, NULL, &rparam, NULL, &error))
	{
	  fprintf (stderr, "Cannot load %s: %s\n", cspname, error);
	  return 1;
	}
      fclose (in);
    }
  if (sigma)
    rparam.sharpen.scanner_mtf.sigma = sigma;
  if (blur_diameter)
    rparam.sharpen.scanner_mtf.blur_diameter = blur_diameter;
  if (pixel_pitch)
    rparam.sharpen.scanner_mtf.pixel_pitch = pixel_pitch;
  if (wavelength)
    rparam.sharpen.scanner_mtf.wavelength = wavelength;
  if (f_stop)
    rparam.sharpen.scanner_mtf.f_stop = f_stop;
  if (defocus)
    rparam.sharpen.scanner_mtf.defocus = defocus;
  if (scan_dpi)
    rparam.sharpen.scanner_mtf.scan_dpi = scan_dpi;
  if (quickmtfname)
    {
      FILE *in = fopen (quickmtfname, "rt");
      if (verbose)
	{
	  printf ("Loading quickmtf file: %s\n", quickmtfname);
	}
      if (!in)
	{
	  perror (cspname);
	  return 1;
	}
      fclose (in);
    }
  file_progress_info progress (stdout, verbose, verbose_tasks);
  if (match)
    {
      mtf_parameters estimated;
      if (verbose)
	{
          progress.pause_stdout ();
	  if (csvname)
	    printf ("Estimating parameters and saving CSV: %s\n", csvname);
	  else
	    printf ("Estimating parameters\n");
	  progress.resume_stdout ();
	}
      if (!rparam.sharpen.scanner_mtf.measurements.size ())
	{
          progress.pause_stdout ();
	  fprintf (stderr, "No measured MTF (scanner_mtf_point) data to match\n");
	  progress.resume_stdout ();
	  return 1;
	}
      double sqsum = estimated.estimate_parameters (rparam.sharpen.scanner_mtf, csvname, &progress, &error, flags);
      if (error)
	{
          progress.pause_stdout ();
	  fprintf (stderr, "Error estimating mtf: %s\n", error);
	  progress.resume_stdout ();
	  exit (1);
	}
      progress.pause_stdout ();
      if (verbose)
        printf ("Average error square: %f\n\n", sqsum / rparam.sharpen.scanner_mtf.measurements.size ());
      printf ("scanner_mtf_sigma_px: %f\n", estimated.sigma);
      printf ("scanner_mtf_blur_diameter_px: %f\n", estimated.blur_diameter);
      printf ("scanner_mtf_pixel_pitch_um: %f\n", estimated.pixel_pitch);
      printf ("scanner_mtf_sensor_fill_factor: %f\n", estimated.sensor_fill_factor);
      printf ("scanner_mtf_wavelength_nm: %f\n", estimated.wavelength);
      printf ("scanner_mtf_channel_wavelengths_nm: %f %f %f %f\n", estimated.wavelengths[0], estimated.wavelengths[1], estimated.wavelengths[2], estimated.wavelengths[3]);
      printf ("scanner_mtf_f_stop: %f\n", estimated.f_stop);
      printf ("scanner_mtf_defocus_mm: %g\n", estimated.defocus);
      printf ("scan_dpi: %f\n", estimated.scan_dpi);
      if (psfname2 && verbose)
	  printf ("Saving estimated PSF to tiff file: %s\n", psfname2);
      if (psfname2 && !estimated.save_psf (NULL, psfname2, &error))
	{
	  fprintf (stderr, "Matched PSF saving failed: %s\n", error);
	  progress.resume_stdout ();
	  exit (1);
	}
      progress.resume_stdout ();
    }
  else if (csvname)
    {
      if (verbose)
        {
          progress.pause_stdout ();
	  printf ("Saving CSV file: %s\n", csvname);
          progress.resume_stdout ();
	}
      if (!rparam.sharpen.scanner_mtf.write_table (csvname, &error))
	{
          progress.pause_stdout ();
	  fprintf (stderr, "CSV saving failed: %s\n", error);
	  exit (1);
	}
    }
  if (psfname && verbose)
  {
    progress.pause_stdout ();
    printf ("Saving PSF to tiff file: %s\n", psfname);
    progress.resume_stdout ();
  }
  if (psfname && !rparam.sharpen.scanner_mtf.save_psf (NULL, psfname, &error))
    {
      progress.pause_stdout ();
      fprintf (stderr, "PSF saving failed: %s\n", error);
      progress.resume_stdout ();
      exit (1);
    }
  return 0;
}

/* Implement "adjust-par" command with ARGC and ARGV.  */
int
do_adjust_par (int argc, char **argv)
{
  const char *cspname = NULL, *error = NULL, *outcspname = NULL;
  std::vector<const char *> csps;
  for (int i = 0; i < argc; i++)
    {
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "merge"))
        csps.push_back (str);
      else if (const char *str = arg_with_param (argc, argv, &i, "out"))
        outcspname = str;
      else
	{
	  if (!cspname)
	    cspname = argv[i];
	  else
	    print_help (argv[i]);
	}
    }
  if (!cspname)
    {
      fprintf (stderr, "No filename given\n");
      exit (-1);
    }
  scr_to_img_parameters param;
  render_parameters rparam;
  scr_detect_parameters dparam;
  struct solver_parameters solver_param;
  if (cspname)
    {
      FILE *in = fopen (cspname, "rt");
      if (verbose)
	{
	  printf ("Loading color screen parameters: %s\n", cspname);
	}
      if (!in)
	{
	  perror (cspname);
	  return 1;
	}
      if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
	{
	  fprintf (stderr, "Can not load %s: %s\n", cspname, error);
	  return 1;
	}
      fclose (in);
    }
  for (auto name: csps)
    {
      FILE *in = fopen (name, "rt");
      if (verbose)
	{
	  printf ("Merging in color screen parameters: %s\n", name);
	}
      if (!in)
	{
	  perror (name);
	  return 1;
	}
      if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
	{
	  fprintf (stderr, "Can not load %s: %s\n", cspname, error);
	  return 1;
	}
      fclose (in);
    }
  if (!outcspname)
    outcspname = cspname;
  if (verbose)
    {
      printf ("Saving color screen parameters: %s\n", outcspname);
    }
  FILE *out = fopen (outcspname, "wt");
  if (!out)
    {
      perror (outcspname);
      return 1;
    }
  if (!save_csp (out, &param, &dparam, &rparam, &solver_param))
    {
      fprintf (stderr, "saving failed\n");
      return 1;
    }
  fclose (out);
  return 0;
}

/* Implement "has-regular-screen" command with ARGC and ARGV.  */
int
do_has_regular_screen (int argc, char **argv)
{
  std::vector <char *> filenames;
  const char *repname = NULL;
  const char *save_matches = NULL;
  const char *save_misses = NULL;
  const char *save_errors = NULL;
  FILE *matches = NULL, *misses = NULL, *errors = NULL;
  has_regular_screen_params param;
  subhelp = help_has_regular_screen;
  bool must_match = false;

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      float flt;
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-tiles"))
        {
	  param.save_tiles = true;
	  param.tile_base = str;
        }
      else if (const char *str = arg_with_param (argc, argv, &i, "save-fft"))
        {
	  param.save_fft = true;
	  param.fft_base = str;
        }
      else if (parse_float_param (argc, argv, &i, "threshold", flt, 1, 1000))
	param.threshold = flt;
      else if (parse_float_param (argc, argv, &i, "tile-threshold", flt, 1, 100))
	param.threshold = flt / 100.0;
      else if (parse_float_param (argc, argv, &i, "min-period", flt, 2, 128))
	param.min_period = flt;
      else if (parse_float_param (argc, argv, &i, "max-period", flt, 2, 128))
	param.max_period = flt;
      else if (arg == "--must-match")
	must_match = true;
      else if (parse_float_param (argc, argv, &i, "gamma", flt, 0, 2))
	param.gamma = flt;
      else if (const char *str = arg_with_param (argc, argv, &i, "report"))
        repname = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-matches"))
        save_matches = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-misses"))
        save_misses = str;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-errors"))
        save_errors = str;
      else if (parse_int_param (argc, argv, &i, "xtiles", param.ntilesx, 1, 100000))
	;
      else if (parse_int_param (argc, argv, &i, "ytiles", param.ntilesy, 1, 100000))
	;
      else
	{
	  if (access(argv[i], F_OK) == 0)
	    filenames.push_back (argv[i]);
	  else
	    print_help (argv[i]);
	}
    }
  if (!filenames.size ())
    {
      fprintf (stderr, "No filename given\n");
      exit (-1);
    }
  if (repname)
    {
      param.report = fopen (repname, "wt");
      if (!param.report)
        {
	  perror (repname);
	  return -1;
        }
    }
  if (save_matches)
    {
      matches = fopen (save_matches, "wt");
      if (!matches)
        {
	  perror (save_matches);
	  return -1;
        }
    }
  if (save_misses)
    {
      misses = fopen (save_misses, "wt");
      if (!misses)
        {
	  perror (save_misses);
	  return -1;
        }
    }
  if (save_errors)
    {
      errors = fopen (save_errors, "wt");
      if (!errors)
        {
	  perror (save_errors);
	  return -1;
        }
    }
  if (verbose)
    {
      if (filenames.size () > 1)
        printf ("Analyzing %i images\n", (int)filenames.size ());
      if (save_matches)
        printf ("Saving filenames of scans with regular pattern to: %s\n", save_matches);
      if (save_misses)
        printf ("Saving filenames of scans with no regular to: %s\n", save_misses);
      if (save_errors)
        printf ("Saving filenames of scans where processing failed to: %s\n", save_errors);
      if (repname)
        printf ("Saving report to: %s\n", repname);
    }
  file_progress_info progress (stdout, verbose, verbose_tasks);
  bool found = false;
  bool error_found = false;
  if (filenames.size () > 1)
    progress.set_task ("analyzing files", filenames.size ());
  for (unsigned int i = 0; i < filenames.size (); i++)
    {
      image_data scan;
      int stack;
      if (filenames.size () > 1)
	stack = progress.push ();
      if (param.report)
	fprintf (param.report, "Analyzing %s\n", filenames[i]);
      const char *error = NULL;
      if (!scan.load (filenames[i], false, &error, &progress))
	{
	  if (errors)
	    {
	      fprintf (errors, "%s\n", filenames[i]);
	      fflush (errors);
	    }
	  progress.pause_stdout ();
	  fprintf (stderr, "Cannot load %s: %s\n", filenames[i], error);
	  progress.resume_stdout ();
	  error_found = true;
	  if (filenames.size () > 1)
	    {
	      progress.pop (stack);
	      progress.inc_progress ();
	    }
	  continue;
	}
      param.verbose = verbose;
      has_regular_screen_ret ret = has_regular_screen (scan, param, &progress);
      if (ret.found)
	{
	  if (matches)
	    {
	      fprintf (matches, "%s\n", filenames[i]);
	      fflush (matches);
	    }
	  if (param.report)
	    fprintf (param.report, "%s: regular pattern with period %.2f detected in %.2f%% of samples\n", filenames[i], ret.period, ret.perc);
	  progress.pause_stdout ();
	  printf ("%s: regular pattern with period %.2f detected in %.2f%% of samples\n", filenames[i], ret.period, ret.perc);
	  progress.resume_stdout ();
	  found = true;
	}
      else if (ret.error)
	{
	  if (errors)
	    {
	      fprintf (errors, "%s\n", filenames[i]);
	      fflush (errors);
	    }
	  if (param.report)
	    fprintf (param.report, "%s: detection failed (%s)\n", filenames[i], ret.error);
	  progress.pause_stdout ();
	  printf ("%s: detection failed (%s)\n", filenames[i], ret.error);
	  progress.resume_stdout ();
	  error_found = true;
	  if (must_match)
	    {
	      if (filenames.size () > 1)
		{
		  progress.pop (stack);
		  progress.inc_progress ();
		}
	      break;
	    }
	}
      else 
	{
	  if (misses)
	    {
	      fprintf (misses, "%s\n", filenames[i]);
	      fflush (matches);
	    }
	  if (must_match)
	    {
	      progress.pause_stdout ();
	      printf ("%s: not detected regular pattern\n", filenames[i]);
	      progress.resume_stdout ();
	      error_found = true;
	      if (filenames.size () > 1)
		{
		  progress.pop (stack);
		  progress.inc_progress ();
		}
	      break;
	    }
	}
      if (filenames.size () > 1)
	{
	  progress.pop (stack);
	  progress.inc_progress ();
	}
    }
  if (param.report)
    fclose (param.report);
  if (misses)
    fclose (misses);
  if (errors)
    fclose (errors);
  if (matches)
    fclose (matches);
  return error_found ? -1 : found ? 1 : 0;
}

/* Implement "slanted-edge" command with ARGC and ARGV.  */
int
do_slanted_edge (int argc, char **argv)
{
  const char *filename = NULL;
  const char *mtfname = NULL;
  const char *compare_mtf = NULL;
  float tolerance = 0.05;
  float gamma = 1.0;
  slanted_edge_parameters params;
  subhelp = help_slanted_edge;

  for (int i = 0; i < argc; i++)
    {
      std::string_view arg (argv[i]);
      float flt;
      if (parse_common_flags (argc, argv, &i))
        ;
      else if (parse_float_param (argc, argv, &i, "gamma", flt, 0.1, 10))
        gamma = flt;
      else if (const char *str = arg_with_param (argc, argv, &i, "save-mtf"))
        mtfname = str;
      else if (arg == "--compare-with")
        {
          if (i + 2 >= argc)
            {
              fprintf (stderr, "--compare-with requires two parameters: <file> <tolerance>\n");
              return 1;
            }
          compare_mtf = argv[++i];
          tolerance = atof (argv[++i]);
        }
      else if (argv[i][0] == '-')
        {
          print_help (argv[i]);
          return 1;
        }
      else
        filename = argv[i];
    }

  if (!filename)
    {
      fprintf (stderr, "No image file specified\n");
      return 1;
    }

  image_data img;
  const char *error = NULL;
  file_progress_info progress (stdout, verbose);
  if (!img.load (filename, false, &error, &progress))
    {
      fprintf (stderr, "Cannot load %s: %s\n", filename, error);
      return 1;
    }

  render_parameters rparam;
  rparam.gamma = gamma;

  slanted_edge_results res = slanted_edge_mtf (rparam, img, img.get_area(), params, &progress);
  if (!res.success)
    {
      fprintf (stderr, "Slanted edge analysis failed\n");
      return 1;
    }

  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Detected edge: (%.2f, %.2f) - (%.2f, %.2f)\n", 
              (double)res.edge_p1.x, (double)res.edge_p1.y, 
              (double)res.edge_p2.x, (double)res.edge_p2.y);
      progress.resume_stdout ();
    }

  if (rparam.sharpen.scanner_mtf.measurements.empty())
    {
      fprintf (stderr, "No MTF measurement produced\n");
      return 1;
    }

  auto &m = rparam.sharpen.scanner_mtf.measurements[0];

  if (verbose)
    {
      progress.pause_stdout ();
      printf ("Measured MTF:\n");
      for (size_t i = 0; i < m.size(); i += std::max((size_t)1, m.size() / 20))
        printf ("  Freq %.3f: %.3f%%\n", m.get_freq(i), (double)m.get_contrast(i));
      progress.resume_stdout ();
    }

  if (mtfname)
    {
      FILE *f = fopen (mtfname, "wt");
      if (!f)
        {
          perror (mtfname);
          return 1;
        }
      for (size_t i = 0; i < m.size(); i++)
        {
          double fq = m.get_freq(i);
          double c = m.get_contrast(i);
          fprintf (f, "%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n", fq, c, c, c, c);
        }
      fclose (f);
    }

  if (compare_mtf)
    {
      FILE *f = fopen (compare_mtf, "rt");
      if (!f)
        {
          perror (compare_mtf);
          return 1;
        }
      std::vector<std::pair<double, double>> ref_mtf;
      char line[1024];
      while (fgets (line, sizeof(line), f))
        {
          double fq, r, g, b, l;
          if (sscanf (line, "%lf %lf %lf %lf %lf", &fq, &r, &g, &b, &l) == 5)
            ref_mtf.push_back ({fq, l});
        }
      fclose (f);

      if (ref_mtf.empty())
        {
          fprintf (stderr, "Reference MTF file %s is empty or invalid\n", compare_mtf);
          return 1;
        }

      double max_err = 0;
      for (size_t i = 0; i < m.size(); i++)
        {
          double fq = m.get_freq(i);
          double c = m.get_contrast(i);
          
          // Interpolate ref_mtf
          if (fq < ref_mtf.front().first || fq > ref_mtf.back().first)
            continue;
            
          auto it = std::lower_bound(ref_mtf.begin(), ref_mtf.end(), std::make_pair(fq, 0.0),
                                     [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
                                       return a.first < b.first;
                                     });
          if (it == ref_mtf.begin()) continue;
          auto prev = std::prev(it);
          double t = (fq - prev->first) / (it->first - prev->first);
          double ref_c = prev->second * (1.0 - t) + it->second * t;
          
          double err = std::abs(c - ref_c);
          if (err > max_err) max_err = err;
        }
      
      if (verbose)
        printf("Max MTF error: %.3f%%\n", max_err);
        
      if (max_err > tolerance)
        {
          fprintf (stderr, "MTF comparison failed: max error %.3f%% > tolerance %.3f%%\n", max_err, tolerance);
          return 1;
        }
    }

  return 0;
}

/* Entry point for colorscreen.  */
int
main (int argc, char **argv)
{
  binname = argv[0];
  int ret = 0;
  argv++;
  argc -= 1;
  int i;
  for (i = 0; i < argc; i++)
    if (parse_common_flags (argc, argv, &i))
      ;
    else
      break;
  argv += i;
  argc -= i;

  if (argc == 0)
    {
      print_help ();
      return 0;
    }

  std::string_view cmd (argv[0]);
  
  struct command_t {
    std::string_view name;
    int (*func)(int, char **);
    std::string_view alias;
  };

  static const command_t commands[] = {
    {"render", [](int ac, char **av) { return render_cmd (ac, av); }, ""},
    {"autodetect", [](int ac, char **av) { return autodetect (ac, av); }, ""},
    {"analyze-backlight", [](int ac, char **av) { analyze_backlight (ac, av); return 0; }, ""},
    {"analyze-scanner-blur", [](int ac, char **av) { return (int)analyze_scanner_blur (ac, av); }, ""},
    {"finetune", [](int ac, char **av) { finetune (ac, av); return 0; }, ""},
    {"export-lcc", [](int ac, char **av) { export_lcc (ac, av); return 0; }, ""},
    {"dump-lcc", [](int ac, char **av) { return (int)dump_lcc (ac, av); }, ""},
    {"stitch", [](int ac, char **av) { return stitch (ac, av); }, ""},
    {"dump-patch-density", [](int ac, char **av) { return dump_patch_density (ac, av); }, ""},
    {"digital-laboratory", [](int ac, char **av) { digital_laboratory (ac, av); return 0; }, "lab"},
    {"read-chemcad-spectra", [](int ac, char **av) { read_chemcad (ac, av); return 0; }, ""},
    {"has-regular-screen", [](int ac, char **av) { return do_has_regular_screen (ac, av); }, ""},
    {"mtf", [](int ac, char **av) { return do_mtf (ac, av); }, ""},
    {"adjust-par", [](int ac, char **av) { return do_adjust_par (ac, av); }, ""},
    {"slanted-edge", [](int ac, char **av) { return do_slanted_edge (ac, av); }, ""}
  };

  for (const auto &c : commands)
    {
      if (cmd == c.name || (!c.alias.empty() && cmd == c.alias))
        {
          ret = c.func (argc - 1, argv + 1);
          return ret;
        }
    }

  print_help ();
  return 0;
}
