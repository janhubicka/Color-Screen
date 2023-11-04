#include <sys/time.h>
#include <gsl/gsl_multifit.h>
#include <string>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-dufay.h"
#include "../libcolorscreen/include/stitch.h"
#include <tiffio.h>
#include "../libcolorscreen/render-interpolate.h"
#include "../libcolorscreen/screen-map.h"
#include "../libcolorscreen/include/tiff-writer.h"

extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;

namespace {
stitch_project *prj;
const char* save_project_filename;
const char* load_project_filename;

void
print_help (const char *filename)
{
  printf ("%s <parameters> <tiles> ....\n", filename);
  printf ("\n");
  printf ("Supported parameters:\n");
  printf (" input files:\n");
  printf ("  --csp=filename.par                          load given screen discovery and rendering parameters\n");
  printf ("  --ncols=n                                   number of columns of tiles\n");
  printf (" output files:\n");
  printf ("  --report=filename.txt                       store report about stitching operation to a file\n");
  printf ("  --save-project=filename.csprj               store analysis to a project file\n");
  printf ("  --load-project=filename.csprj               store analysis to a project file\n");
  printf ("  --stitched=filename.tif                     store stitched file (with no blending)\n");
  printf ("  --hugin-pto=filename.pto                    store project file for hugin\n");
  printf ("  --scan-ppi=scan-ppi                         PPI of input scan\n");
  printf ("  --orig-tile-gamma=gamma                     gamma curve of the output tiles (by default it is set to one of input file)\n");
  printf ("  --downscale=factor                          reduce size of predictive panorama\n");
  printf ("  --hdr                                       output predictive and interpolated panorama in hdr\n");
  printf (" tiles to ouptut:\n");
  printf ("  --demosaiced-tiles                          store demosaiced tiles (for later blending)\n");
  printf ("  --predictive-tiles                          store predictive tiles (for later blending)\n");
  printf ("  --screen-tiles                              store screen tiles (for verification)\n");
  printf ("  --known-screen-tiles                        store screen tiles where unanalyzed pixels are transparent\n");
  printf ("  --orig-tiles                                store geometrically corrected tiles (for later blending)\n");
  printf (" overlap detection:\n");
  printf ("  --no-cpfind                                 disable use of Hugin's cpfind to determine overlap\n");
  printf ("  --cpfind                                    enable use of Hugin's cpfind to determine overlap\n");
  printf ("  --cpfind-verification                       use cpfind to verify results of internal overlap detection\n");
  printf ("  --min-overlap=precentage                    minimal overlap\n");
  printf ("  --max-overlap=precentage                    maximal overlap\n");
  printf ("  --outer-tile-border=percentage              border to ignore in outer files\n");
  printf ("  --inner-tile-border=percentage              border to ignore in inner parts files\n");
  printf ("  --max-contrast=precentage                   report differences in contrast over this threshold\n");
  printf ("  --max-avg-distance=npixels                  maximal average distance of real screen patches to estimated ones via affine transform\n");
  printf ("  --max-max-distance=npixels                  maximal maximal distance of real screen patches to estimated ones via affine transform\n");
  printf ("  --geometry-info                             store info about goemetry mismatches to tiff files\n");
  printf ("  --individual-geometry-info                  store info about goemetry mismatches to tiff files; produce file for each pair\n");
  printf ("  --outlier-info                              store info about outliers\n");
  printf (" hugin output:\n");
  printf ("  --num-control-points=n                      number of control points for each pair of images\n");
  printf (" other:\n");
  printf ("  --panorama-map                              print panorama map in ascii-art\n");
  printf ("  --min-screen-precentage                     minimum portion of screen required to be recognized by screen detection\n");
  printf ("  --optimize-colors                           auto-optimize screen colors (default)\n");
  printf ("  --no-optimize-colors                        do not auto-optimize screen colors\n");
  printf ("  --reoptimize-colors                         auto-optimize screen colors after initial screen analysis\n");
  printf ("  --slow-floodfill                            use unly slower discovery of patches (by default both slow and fast methods are used)\n");
  printf ("  --fast-floodfill                            use unly faster discovery of patches (by default both slow and fast methods are used)\n");
  printf ("  --no-limit-directions                       do not limit overlap checking to expected directions\n");
  printf ("  --min-patch-contrast=contrast               specify minimal contrast accepted in patch discovery\n");
  printf ("  --diffs                                     produce diff files for each overlapping pair of tiles\n");
}

void
produce_hugin_pto_file (const char *name, progress_info *progress)
{
  FILE *f = fopen (name,"wt");
  if (!f)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Can not open %s\n", name);
      exit (1);
    }
  fprintf (f, "# hugin project file\n"
	   "#hugin_ptoversion 2\n"
	   //"p f2 w3000 h1500 v360  k0 E0 R0 n\"TIFF_m c:LZW r:CROP\"\n"
	   "p f0 w39972 h31684 v82  k0 E0 R0 S13031,39972,11939,28553 n\"TIFF_m c:LZW r:CROP\"\n"
	   "m i0\n");
  for (int y = 0; y < prj->params.height; y++)
    for (int x = 0; x < prj->params.width; x++)
     if (!y && !x)
       fprintf (f, "i w%i h%i f0 v%f Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n", prj->images[y][x].img_width, prj->images[y][x].img_height, prj->params.hfov, prj->images[y][x].filename.c_str ());
     else
       fprintf (f, "i w%i h%i f0 v=0 Ra=0 Rb=0 Rc=0 Rd=0 Re=0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy-0 Tpp0 j0 a=0 b=0 c=0 d=0 e=0 g=0 t=0 Va=1 Vb=0 Vc=0 Vd=0 Vx=0 Vy=0  Vm5 n\"%s\"\n", prj->images[y][x].img_width, prj->images[y][x].img_height, prj->images[y][x].filename.c_str ());
  fprintf (f, "# specify variables that should be optimized\n"
	   "v Ra0\n"
	   "v Rb0\n"
	   "v Rc0\n"
	   "v Rd0\n"
	   "v Re0\n"
	   "v Vb0\n"
	   "v Vc0\n"
	   "v Vd0\n");
  for (int i = 1; i < prj->params.width * prj->params.height; i++)
    fprintf (f, "v Ra%i\n"
	     "v Rb%i\n"
	     "v Rc%i\n"
	     "v Rd%i\n"
	     "v Re%i\n"
	     "v Eev%i\n"
	     "v Ra%i\n"
	     "v Rb%i\n"
	     "v Rc%i\n"
	     "v Rd%i\n"
	     "v Re%i\n"
	     "v r%i\n"
	     "v p%i\n"
	     "v y%i\n"
	     "v Vb%i\n"
	     "v Vc%i\n"
	     "v Vd%i\n",i,i,i,i,i,i,i,i,i,i,i,i,i,i,i,i,i);
#if 0
  for (int y = 0; y < prj->params.height; y++)
    for (int x = 0; x < prj->params.width; x++)
      {
	if (x >= 1)
	  images[y][x-1].output_common_points (f, images[y][x], y * prj->params.width + x - 1, y * prj->params.width + x);
	if (y >= 1)
	  images[y-1][x].output_common_points (f, images[y][x], (y - 1) * prj->params.width + x, y * prj->params.width + x);
      }
#endif
  for (int y = 0; y < prj->params.height; y++)
    for (int x = 0; x < prj->params.width; x++)
      for (int y2 = 0; y2 < prj->params.height; y2++)
        for (int x2 = 0; x2 < prj->params.width; x2++)
	  if ((x != x2 || y != y2) && (y < y2 || (y == y2 && x < x2)))
	    prj->images[y][x].output_common_points (f, prj->images[y2][x2], y * prj->params.width + x, y2 * prj->params.width + x2, false, progress);
  fclose (f);
}

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_output_file (const char *outfname, int outwidth, int outheight,
		  uint16_t ** outrow, const char **error,
		  void *icc_profile, uint32_t icc_profile_size,
		  progress_info *progress)
{
  TIFF *out = TIFFOpen (outfname, "wb8");
  if (!out)
    {
      *error = "can not open output file";
      return NULL;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, outheight)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, icc_profile ? icc_profile_size : sRGB_icc_len, icc_profile ? icc_profile : sRGB_icc))
    {
      *error = "write error";
      return NULL;
    }
  *outrow = (uint16_t *) malloc (outwidth * 2 * 3);
  if (!outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving tile", outheight);
    }
  if (prj->report_file)
    fprintf (prj->report_file, "Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->pause_stdout ();
  printf ("Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->resume_stdout ();
  return out;
}

void stitch (progress_info *progress)
{
  if (!prj->initialize ())
    exit (1);
  if (!load_project_filename)
    {
      progress->pause_stdout ();
      printf ("Stitching:\n");
      if (prj->report_file)
	fprintf (prj->report_file, "Stitching:\n");
      for (int y = 0; y < prj->params.height; y++)
	{
	  for (int x = 0; x < prj->params.width; x++)
	    {
	      printf ("  %s", prj->images[y][x].filename.c_str ());
	      if (prj->report_file)
		fprintf (prj->report_file, "  %s", prj->images[y][x].filename.c_str ());
	    }
	  printf("\n");
	  if (prj->report_file)
	    fprintf (prj->report_file, "\n");
	}
      progress->resume_stdout ();

      if (prj->params.csp_filename.length ())
	{
	  const char *cspname = prj->params.csp_filename.c_str ();
	  FILE *in = fopen (cspname, "rt");
	  progress->pause_stdout ();
	  printf ("Loading color screen parameters: %s\n", cspname);
	  progress->resume_stdout ();
	  if (!in)
	    {
	      perror (cspname);
	      exit (1);
	    }
	  const char *error;
	  if (!load_csp (in, &prj->param, &prj->dparam, &prj->rparam, &prj->solver_param, &error))
	    {
	      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
	      exit (1);
	    }
	  if (prj->param.mesh_trans)
	    {
	      delete prj->param.mesh_trans;
	      prj->param.mesh_trans = NULL;
	    }
	  fclose (in);
	  prj->solver_param.remove_points ();
	}

      if (prj->report_file)
	{
	  fprintf (prj->report_file, "Color screen parameters:\n");
	  save_csp (prj->report_file, &prj->param, &prj->dparam, &prj->rparam, &prj->solver_param);
	}
      if (!prj->analyze_images (progress))
	{
	  exit (1);
	}
      if (save_project_filename)
	{
	  FILE *f = fopen (save_project_filename, "wt");
	  if (!f)
	    {
	      fprintf (stderr, "Error opening project file: %s\n", save_project_filename);
	      exit (1);
	    }
	  if (!prj->save (f))
	    {
	      fprintf (stderr, "Error saving project file: %s\n", save_project_filename);
	      exit (1);
	    }
	  fclose (f);
	}
    }
  else
    {
      const char *error;
      FILE *f = fopen (load_project_filename, "rt");
      if (prj->params.csp_filename.length ())
	{
	  const char *cspname = prj->params.csp_filename.c_str ();
	  FILE *in = fopen (cspname, "rt");
	  progress->pause_stdout ();
	  printf ("Loading color screen parameters: %s\n", cspname);
	  progress->resume_stdout ();
	  if (!in)
	    {
	      perror (cspname);
	      exit (1);
	    }
	  const char *error;
	  if (!load_csp (in, &prj->param, &prj->dparam, &prj->rparam, &prj->solver_param, &error))
	    {
	      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
	      exit (1);
	    }
	  if (prj->param.mesh_trans)
	    {
	      delete prj->param.mesh_trans;
	      prj->param.mesh_trans = NULL;
	    }
	  fclose (in);
	  prj->solver_param.remove_points ();
	}
      if (!f)
	{
	  fprintf (stderr, "Error opening project file: %s\n", save_project_filename);
	  exit (1);
	}
      if (!prj->load (f, &error))
	{
	  fprintf (stderr, "Error loading project file: %s %s\n", load_project_filename, error);
	  exit (1);
	}
      fclose(f);
    }

  prj->determine_angle ();

  int xmin, ymin, xmax, ymax;
  prj->determine_viewport (xmin, xmax, ymin, ymax);
  if (prj->params.geometry_info)
    for (int y = 0; y < prj->params.height; y++)
      for (int x = 0; x < prj->params.width; x++)
	if (prj->images[y][x].stitch_info)
	  prj->images[y][x].write_stitch_info (progress);
  if (prj->params.individual_geometry_info)
    for (int y = 0; y < prj->params.height; y++)
      for (int x = 0; x < prj->params.width; x++)
	for (int yy = y; yy < prj->params.height; yy++)
	  for (int xx = (yy == y ? x : 0); xx < prj->params.width; xx++)
	    if (x != xx || y != yy)
	    {
	      prj->images[y][x].clear_stitch_info ();
	      if (prj->images[y][x].output_common_points (NULL, prj->images[yy][xx], 0, 0, true, progress))
		prj->images[y][x].write_stitch_info (progress, x, y, xx, yy);
	    }

  const coord_t xstep = prj->pixel_size, ystep = prj->pixel_size;
  const coord_t pred_xstep = prj->pixel_size * prj->params.downscale, pred_ystep = prj->pixel_size * prj->params.downscale;
  prj->passthrough_rparam.gray_max = prj->images[0][0].gray_max;
  if (prj->params.hugin_pto_filename.length ())
    produce_hugin_pto_file (prj->params.hugin_pto_filename.c_str (), progress);
  if (prj->params.diffs)
    for (int y = 0; y < prj->params.height; y++)
      for (int x = 0; x < prj->params.width; x++)
	for (int yy = y; yy < prj->params.height; yy++)
	  for (int xx = (yy == y ? x : 0); xx < prj->params.width; xx++)
	    if (x != xx || y != yy)
	      prj->images[y][x].diff (prj->images[yy][xx], progress);
  const char *error;
  if (prj->params.produce_stitched_file_p ())
    {
      TIFF *out;
      uint16_t *outrow;
      prj->images[0][0].load_img (progress);
      out =
	open_output_file (prj->params.stitched_filename.c_str (), (xmax-xmin) / xstep, (ymax-ymin) / ystep, &outrow, 
			  &error,
			  prj->images[0][0].img->icc_profile, prj->images[0][0].img->icc_profile_size,
			  progress);
      prj->images[0][0].release_img ();
      int j = 0;
      if (!out)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Can not open final stitch file %s: %s\n", prj->params.stitched_filename.c_str (), error);
	  exit (1);
	}
      for (coord_t y = ymin; j < (int)((ymax-ymin) / ystep); y+=ystep, j++)
	{
	  int i = 0;
	  bool set_p = false;
	  for (coord_t x = xmin; i < (int)((xmax-xmin) / xstep); x+=xstep, i++)
	    {
	      coord_t sx, sy;
	      int r = 0,g = 0,b = 0;
	      int ix = 0, iy = 0;
	      prj->common_scr_to_img.final_to_scr (x, y, &sx, &sy);
	      for (iy = 0 ; iy < prj->params.height; iy++)
		{
		  for (ix = 0 ; ix < prj->params.width; ix++)
		    if (prj->images[iy][ix].analyzed && prj->images[iy][ix].pixel_known_p (sx, sy))
		      break;
		  if (ix != prj->params.width)
		    break;
		}
	      if (iy != prj->params.height)
		{
		  if (prj->images[iy][ix].render_pixel (65535, sx,sy, stitch_image::render_original,&r,&g,&b, progress))
		    set_p = true;
		  if (!prj->images[iy][ix].output)
		    {
		      if ((prj->params.orig_tiles && !prj->images[iy][ix].write_tile (&error, prj->common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_original, progress))
			  || (prj->params.demosaiced_tiles && !prj->images[iy][ix].write_tile (&error, prj->common_scr_to_img, xmin, ymin, 1, 1, stitch_image::render_demosaiced, progress))
			  || (prj->params.predictive_tiles && !prj->images[iy][ix].write_tile (&error, prj->common_scr_to_img, xmin, ymin, pred_xstep, pred_ystep, stitch_image::render_predictive, progress)))
			{
			  fprintf (stderr, "Writting tile: %s\n", error);
			  exit (1);
			}
		      set_p = true;
		    }
		}
	      outrow[3 * i] = r;
	      outrow[3 * i + 1] = g;
	      outrow[3 * i + 2] = b;
	    }
	  if (set_p)
	    {
	      progress->set_task ("Rendering and saving", (ymax-ymin) / ystep);
	      progress->set_progress (j);
	    }
	  if (!stitch_image::write_row (out, j, outrow, &error, progress))
	    {
	      fprintf (stderr, "Writting failed: %s\n", error);
	      exit (1);
	    }
	}
      progress->set_task ("Closing output file", 1);

      TIFFClose (out);
      free (outrow);
      progress->set_task ("Releasing memory", 1);
    }
  else
    for (int y = 0; y < prj->params.height; y++)
      for (int x = 0; x < prj->params.width; x++)
      {
	coord_t demosaicedstep = prj->params.type == Dufay ? 0.5 : 0.25;
	if ((prj->params.orig_tiles && !prj->images[y][x].write_tile (&error, prj->common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_original, progress))
	    || (prj->params.demosaiced_tiles && !prj->images[y][x].write_tile (&error, prj->common_scr_to_img, xmin, ymin, demosaicedstep, demosaicedstep, stitch_image::render_demosaiced, progress))
	    || (prj->params.predictive_tiles && !prj->images[y][x].write_tile (&error, prj->common_scr_to_img, xmin, ymin, pred_xstep, pred_ystep, stitch_image::render_predictive, progress)))
	  {
	    fprintf (stderr, "Writting tile: %s\n", error);
	    exit (1);
	  }
      }
  for (int y = 0; y < prj->params.height; y++)
    for (int x = 0; x < prj->params.width; x++)
      if (prj->images[y][x].img)
	prj->images[y][x].release_image_data (progress);
  if (prj->report_file)
    fclose (prj->report_file);
}
}

int
main (int argc, char **argv)
{
  file_progress_info progress (stdout);
  std::vector<std::string> fnames;
  int ncols = 0;

  prj = new stitch_project ();

  for (int i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "--report"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing report filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.report_filename = argv[i];
	  continue;
	}
      if (!strcmp (argv[i], "--save-project"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing report filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  save_project_filename = argv[i];
	  continue;
	}
      if (!strcmp (argv[i], "--load-project"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing report filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  load_project_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--report=", strlen ("--report=")))
	{
	  prj->params.report_filename = argv[i] + strlen ("--report=");
	  continue;
	}
      if (!strcmp (argv[i], "--csp"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing csp filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.csp_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--csp=", strlen ("--csp=")))
	{
	  prj->params.csp_filename = argv[i] + strlen ("--csp=");
	  continue;
	}
      if (!strcmp (argv[i], "--hugin-pto"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing hugin-pto filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.hugin_pto_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--hugin-pto=", strlen ("--hugin-pto=")))
	{
	  prj->params.hugin_pto_filename = argv[i] + strlen ("--hugin-pto=");
	  continue;
	}
      if (!strcmp (argv[i], "--stitched"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing stitched filename\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.stitched_filename = argv[i];
	  continue;
	}
      if (!strcmp (argv[i], "--no-cpfind"))
	{
	  prj->params.cpfind = 0;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind"))
	{
	  prj->params.cpfind = 1;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind-verification"))
	{
	  prj->params.cpfind = 2;
	  continue;
	}
      if (!strncmp (argv[i], "--stitched=", strlen ("--stitched=")))
	{
	  prj->params.stitched_filename = argv[i] + strlen ("--stitched=");
	  continue;
	}
      if (!strcmp (argv[i], "--demosaiced-tiles"))
	{
	  prj->params.demosaiced_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--predictive-tiles"))
	{
	  prj->params.predictive_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--orig-tiles"))
	{
	  prj->params.orig_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--hdr"))
	{
	  prj->params.hdr = true;
	  continue;
	}
      if (!strncmp (argv[i], "--downscale=", strlen ("--downscale=")))
	{
	  prj->params.downscale = std::max (atoi (argv[i] + strlen ("--downscale=")), 1);
	  continue;
	}
      if (!strcmp (argv[i], "--screen-tiles"))
	{
	  prj->params.screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--known-screen-tiles"))
	{
	  prj->params.known_screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--panorama-map"))
	{
	  prj->params.panorama_map = true;
	  continue;
	}
      if (!strcmp (argv[i], "--optimize-colors"))
	{
	  prj->params.optimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-optimize-colors"))
	{
	  prj->params.optimize_colors = false;
	  continue;
	}
      if (!strcmp (argv[i], "--reoptimize-colors"))
	{
	  prj->params.reoptimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-limit-directions"))
	{
	  prj->params.limit_directions = false;
	  continue;
	}
      if (!strcmp (argv[i], "--slow-floodfill"))
	{
	  prj->params.slow_floodfill = true;
	  continue;
	}
      if (!strcmp (argv[i], "--fast-floodfill"))
	{
	  prj->params.fast_floodfill = true;
	  continue;
	}
      if (!strcmp (argv[i], "--outer-tile-border"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing tile border percentage\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.outer_tile_border = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--outer-tile-border=", strlen ("--outer-tile-border=")))
	{
	  prj->params.outer_tile_border = atoi (argv[i] + strlen ("--outer-tile-border="));
	  continue;
	}
      if (!strcmp (argv[i], "--inner-tile-border"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing tile border percentage\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.inner_tile_border = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--inner-tile-border=", strlen ("--inner-tile-border=")))
	{
	  prj->params.inner_tile_border = atoi (argv[i] + strlen ("--inner-tile-border="));
	  continue;
	}
      if (!strcmp (argv[i], "--max-contrast"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing max contrast\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.max_contrast = atoi (argv[i]);
	  continue;
	}
      if (!strcmp (argv[i], "--max-unknown-screen-range"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (stderr, "Missing unknown screen range\n");
	      print_help (argv[0]);
	      exit (1);
	    }
	  i++;
	  prj->params.max_unknown_screen_range = atoi (argv[i]);
	  continue;
	}
      if (!strcmp (argv[i], "--max-unknown-screen-range="))
	{
	  prj->params.max_unknown_screen_range = atoi (argv[i] + strlen("--max-unknown-screen-range="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-contrast=", strlen ("--max-contrast=")))
	{
	  prj->params.max_contrast = atoi (argv[i] + strlen ("--max-contrast="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-overlap=", strlen ("--min-overlap=")))
	{
	  prj->params.min_overlap_percentage = atoi (argv[i] + strlen ("--min-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-overlap=", strlen ("--max-overlap=")))
	{
	  prj->params.max_overlap_percentage = atoi (argv[i] + strlen ("--max-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--ncols=", strlen ("--ncols=")))
	{
	  ncols = atoi (argv[i] + strlen ("--ncols="));
	  continue;
	}
      if (!strncmp (argv[i], "--num-control-points=", strlen ("--num-control-points=")))
	{
	  prj->params.num_control_points = atoi (argv[i] + strlen ("--num-control-points="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  prj->params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--orig-tile-gamma=", strlen ("--orig-tile-gamma=")))
	{
	  prj->params.orig_tile_gamma = atof (argv[i] + strlen ("--orig-tile-gamma="));
	  continue;
	}
      if (!strncmp (argv[i], "--scan-ppi=", strlen ("--scan-ppi=")))
	{
	  prj->params.scan_dpi = atof (argv[i] + strlen ("--scan-ppi="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  prj->params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--hfov=", strlen ("--hfov=")))
	{
	  prj->params.hfov = atof (argv[i] + strlen ("--hfov="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-avg-distance=", strlen ("--max-avg-distance=")))
	{
	  prj->params.max_avg_distance = atof (argv[i] + strlen ("--max-avg-distance="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-max-distance=", strlen ("--max-max-distance=")))
	{
	  prj->params.max_max_distance = atof (argv[i] + strlen ("--max-max-distance="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-patch-contrast=", strlen ("--max-max-distance=")))
	{
	  prj->params.min_patch_contrast = atof (argv[i] + strlen ("--min-patch-contrast="));
	  continue;
	}
      if (!strncmp (argv[i], "--screen-type=", strlen ("--screen-type=")))
	{
	  const char * t = argv[i] + strlen ("--screen-type=");
	  if (!strcmp (t, "Paget"))
	    prj->params.type = Paget;
	  else if (!strcmp (t, "Dufay"))
	    prj->params.type = Dufay;
	  else if (!strcmp (t, "Finlay"))
	    prj->params.type = Finlay;
	  else
	    {
	      fprintf (stderr, "Unknown or unsupported screen type: %s\n", t);
	      exit (1);
	    }
	  continue;
	}
      if (!strcmp (argv[i], "--mesh"))
	{
	  prj->params.mesh_trans = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-mesh"))
	{
	  prj->params.mesh_trans = false;
	  continue;
	}
      if (!strncmp (argv[i], "--geometry-info", strlen ("--geometry-info")))
	{
	  prj->params.geometry_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--individual-geometry-info", strlen ("--individual-geometry-info")))
	{
	  prj->params.individual_geometry_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--outliers-info", strlen ("--outliers-info")))
	{
	  prj->params.outliers_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--diffs", strlen ("--diffs")))
	{
	  prj->params.diffs = true;
	  continue;
	}
      if (!strncmp (argv[i], "--", 2))
	{
	  fprintf (stderr, "Unknown parameter: %s\n", argv[i]);
	  print_help (argv[0]);
	  exit (1);
	}
      std::string name = argv[i];
      fnames.push_back (name);
    }
  if (!load_project_filename)
    {
      if (!fnames.size ())
	{
	  fprintf (stderr, "No files to stitch\n");
	  print_help (argv[0]);
	  exit (1);
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
	      fprintf (stderr, "Can not determine organization of tiles in '%s'.  Expect filenames of kind <name>yx<suffix>.tif\n", fnames[0].c_str ());
	      print_help (argv[0]);
	      exit (1);
	    }
	  for (indexpos = 0; indexpos < (int)fnames[0].length () - 2; indexpos++)
	    if (fnames[0][indexpos] != fnames[1][indexpos]
		|| (fnames[0][indexpos] == '1' && fnames[0][indexpos + 1] != fnames[1][indexpos + 1]))
	      break;
	  if (fnames[0][indexpos] != '1' || fnames[0][indexpos + 1] != '1')
	    {
	      fprintf (stderr, "Can not determine organization of tiles in '%s'.  Expect filenames of kind <name>yx<suffix>.tif\n", fnames[0].c_str ());
	      print_help (argv[0]);
	      exit (1);
	    }
	  int w;
	  for (w = 1; w < (int)fnames.size (); w++)
	    if (fnames[w][indexpos+1] != '1' + w)
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
		    fprintf (stderr, "Unexpected tile filename '%s' for tile %i %i\n", fnames[i].c_str (), y + 1, x + 1);
		    print_help (argv[0]);
		    exit (1);
		  }
	      }
	}
      if (prj->params.width * prj->params.height != (int)fnames.size ())
	{
	  fprintf (stderr, "For %ix%i tiles I expect %i filenames, found %i\n", prj->params.width, prj->params.height, prj->params.width * prj->params.height, (int)fnames.size ());
	  print_help (argv[0]);
	  exit (1);
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
	  print_help (argv[0]);
	  exit (1);
	}
    }

  stitch (&progress);
  delete prj;


  return 0;
}
