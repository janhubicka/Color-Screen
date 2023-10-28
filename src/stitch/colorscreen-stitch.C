#include <sys/time.h>
#include <gsl/gsl_multifit.h>
#include <string>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-dufay.h"
#include "../libcolorscreen/include/stitch.h"
#include <tiffio.h>
#include "../libcolorscreen/icc-srgb.h"
#include "../libcolorscreen/render-interpolate.h"
#include "../libcolorscreen/screen-map.h"
#include "../libcolorscreen/include/tiff-writer.h"

namespace {
stitch_project prj;

/* Write one row.  */
static bool
write_row (TIFF * out, int y, uint16_t * outrow, const char **error, progress_info *progress)
{
  if (progress && progress->cancel_requested ())
    {
      free (outrow);
      TIFFClose (out);
      *error = "Cancelled";
      return false;
    }
  if (TIFFWriteScanline (out, outrow, y, 0) < 0)
    {
      free (outrow);
      TIFFClose (out);
      *error = "Write error";
      return false;
    }
   if (progress)
     progress->inc_progress ();
  return true;
}

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_tile_output_file (const char *outfname, 
		       int xoffset, int yoffset,
		       int outwidth, int outheight,
		       uint16_t ** outrow, const char **error,
		       void *icc_profile, uint32_t icc_profile_size,
		       stitch_image::render_mode mode,
		       progress_info *progress)
{
  TIFF *out = TIFFOpen (outfname, "wb");
  double dpi = 300;
  if (!out)
    {
      *error = "can not open output file";
      return NULL;
    }
  uint16_t extras[] = {EXTRASAMPLE_UNASSALPHA};
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, outwidth)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, outheight)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 4)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_EXTRASAMPLES, 1, extras)
      || !TIFFSetField (out, TIFFTAG_XRESOLUTION, dpi)
      || !TIFFSetField (out, TIFFTAG_YRESOLUTION, dpi)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, icc_profile && mode == stitch_image::render_original ? icc_profile_size : sRGB_icc_len, icc_profile && mode == stitch_image::render_original ? icc_profile : sRGB_icc))
    {
      *error = "write error";
      return NULL;
    }
  if (xoffset || yoffset || 1)
    {
      if (!TIFFSetField (out, TIFFTAG_XPOSITION, (double)(xoffset / dpi))
	  || !TIFFSetField (out, TIFFTAG_YPOSITION, (double)(yoffset / dpi))
	  || !TIFFSetField (out, TIFFTAG_PIXAR_IMAGEFULLWIDTH, (long)(outwidth + xoffset))
	  || !TIFFSetField (out, TIFFTAG_PIXAR_IMAGEFULLLENGTH, (long)(outheight + yoffset)))
	{
	  *error = "write error";
	  return NULL;
	}
    }
  *outrow = (uint16_t *) malloc (outwidth * 2 * 4);
  if (!outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving", outheight);
    }
  if (prj.report_file)
    fprintf (prj.report_file, "Rendering %s at offset %i,%i (%ix%i pixels)\n", outfname, xoffset, yoffset, outwidth, outheight);
  progress->pause_stdout ();
  printf ("Rendering %s at offset %i,%i (%ix%i pixels)\n", outfname, xoffset, yoffset, outwidth, outheight);
  progress->resume_stdout ();
  return out;
}


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
  printf ("  --stitched=filename.tif                     store stitched file (with no blending)\n");
  printf ("  --hugin-pto=filename.pto                    store project file for hugin\n");
  printf ("  --orig-tile-gamma=gamma                     gamma curve of the output tiles (by default it is set to one of input file)\n");
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
determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax)
{
  xmin = 0;
  ymin = 0;
  xmax = 0;
  ymax = 0;
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      if (prj.images[y][x].analyzed)
	{
	  coord_t x1,y1,x2,y2;
	  coord_t rxpos, rypos;
	  prj.common_scr_to_img.scr_to_final (prj.images[y][x].xpos, prj.images[y][x].ypos, &rxpos, &rypos);
	  x1 = -prj.images[y][x].final_xshift + rxpos;
	  y1 = -prj.images[y][x].final_yshift + rypos;
	  x2 = x1 + prj.images[y][x].final_width;
	  y2 = y1 + prj.images[y][x].final_height;

	  if (!y && !x)
	    {
	      xmin = xmax = x1;
	      ymin = ymax = y1;
	    }
	  xmin = std::min (xmin, (int)floor (x1));
	  xmax = std::max (xmax, (int)ceil (x1));
	  ymin = std::min (ymin, (int)floor (y1));
	  ymax = std::max (ymax, (int)ceil (y1));
	  xmin = std::min (xmin, (int)floor (x2));
	  xmax = std::max (xmax, (int)ceil (x2));
	  ymin = std::min (ymin, (int)floor (y2));
	  ymax = std::max (ymax, (int)ceil (y2));
	}
}

void
print_panorama_map (FILE *out)
{
  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);
  fprintf (out, "Viewport range %i %i %i %i\n", xmin, xmax, ymin, ymax);
  for (int y = 0; y < 20; y++)
    {
      for (int x = 0; x < 40; x++)
	{
	  coord_t fx = xmin + (xmax - xmin) * x / 40;
	  coord_t fy = ymin + (ymax - ymin) * y / 20;
	  coord_t sx, sy;
	  int ix = 0, iy = 0;
	  prj.common_scr_to_img.final_to_scr (fx, fy, &sx, &sy);
	  for (iy = 0 ; iy < prj.params.height; iy++)
	    {
	      for (ix = 0 ; ix < prj.params.width; ix++)
		if (prj.images[iy][ix].analyzed && prj.images[iy][ix].pixel_known_p (sx, sy))
		  break;
	      if (ix != prj.params.width)
		break;
	    }

#if 0
	  if (iy == prj.params.height)
	    fprintf (out, "   ");
	  else
	    fprintf (out, " %i%i",iy+1,ix+1);
#endif
	  if (iy == prj.params.height)
	    fprintf (out, " ");
	  else
	    fprintf (out, "%c",'a'+ix+iy*prj.params.width);
	}
      fprintf (out, "\n");
    }
}

void
print_status (FILE *out)
{
  for (int y = 0; y < prj.params.height; y++)
    {
      if (y)
	{
	  coord_t rx, ry;
	  prj.common_scr_to_img.scr_to_final (prj.images[y-1][0].xpos, prj.images[y-1][0].ypos, &rx, &ry);
	  coord_t rx2, ry2;
	  prj.common_scr_to_img.scr_to_final (prj.images[y][0].xpos, prj.images[y][0].ypos, &rx2, &ry2);
	  rx -= prj.images[y-1][0].xshift;
	  ry -= prj.images[y-1][0].yshift;
	  rx2 -= prj.images[y][0].xshift;
	  ry2 -= prj.images[y][0].yshift;
	  fprintf (out, " down %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	}
      else fprintf (out, "                  ");
      for (int x = 1; x < prj.params.width; x++)
      {
	coord_t rx, ry;
	prj.common_scr_to_img.scr_to_final (prj.images[y][x-1].xpos, prj.images[y][x-1].ypos, &rx, &ry);
	coord_t rx2, ry2;
	prj.common_scr_to_img.scr_to_final (prj.images[y][x].xpos, prj.images[y][x].ypos, &rx2, &ry2);
	rx -= prj.images[y][x-1].xshift;
	ry -= prj.images[y][x-1].yshift;
	rx2 -= prj.images[y][x].xshift;
	ry2 -= prj.images[y][x].yshift;
	fprintf (out, " right %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	//printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      fprintf (out, "\n");
    }
  for (int y = 0; y < prj.params.height; y++)
    {
      for (int x = 0; x < prj.params.width; x++)
      {
	coord_t rx, ry;
	prj.common_scr_to_img.scr_to_final (prj.images[y][x].xpos, prj.images[y][x].ypos, &rx, &ry);
	fprintf (out, "  %-5f,%-5f  rotated:%-5f,%-5f ", prj.images[y][x].xpos, prj.images[y][x].ypos, rx,ry);
      }
      fprintf (out, "\n");
    }
  print_panorama_map (out);
}

void
analyze (int x, int y, progress_info *progress)
{
  prj.images[y][x].analyze (&prj, !y, y == prj.params.height - 1, !x, x == prj.params.width - 1, prj.param.k1, progress);
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
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
     if (!y && !x)
       fprintf (f, "i w%i h%i f0 v%f Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n", prj.images[y][x].img_width, prj.images[y][x].img_height, prj.params.hfov, prj.images[y][x].filename.c_str ());
     else
       fprintf (f, "i w%i h%i f0 v=0 Ra=0 Rb=0 Rc=0 Rd=0 Re=0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy-0 Tpp0 j0 a=0 b=0 c=0 d=0 e=0 g=0 t=0 Va=1 Vb=0 Vc=0 Vd=0 Vx=0 Vy=0  Vm5 n\"%s\"\n", prj.images[y][x].img_width, prj.images[y][x].img_height, prj.images[y][x].filename.c_str ());
  fprintf (f, "# specify variables that should be optimized\n"
	   "v Ra0\n"
	   "v Rb0\n"
	   "v Rc0\n"
	   "v Rd0\n"
	   "v Re0\n"
	   "v Vb0\n"
	   "v Vc0\n"
	   "v Vd0\n");
  for (int i = 1; i < prj.params.width * prj.params.height; i++)
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
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      {
	if (x >= 1)
	  images[y][x-1].output_common_points (f, images[y][x], y * prj.params.width + x - 1, y * prj.params.width + x);
	if (y >= 1)
	  images[y-1][x].output_common_points (f, images[y][x], (y - 1) * prj.params.width + x, y * prj.params.width + x);
      }
#endif
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      for (int y2 = 0; y2 < prj.params.height; y2++)
        for (int x2 = 0; x2 < prj.params.width; x2++)
	  if ((x != x2 || y != y2) && (y < y2 || (y == y2 && x < x2)))
	    prj.images[y][x].output_common_points (f, prj.images[y2][x2], y * prj.params.width + x, y2 * prj.params.width + x2, false, progress);
  fclose (f);
}

void
determine_positions (progress_info *progress)
{
  if (prj.params.width == 1 && prj.params.height == 1)
    {
      analyze (0, 0, progress);
      return;
    }
  for (int y = 0; y < prj.params.height; y++)
    {
      if (!y)
	{
	  prj.images[0][0].xpos = 0;
	  prj.images[0][0].ypos = 0;
	}
      else
	{
	  coord_t xs;
	  coord_t ys;
	  analyze (0, y-1, progress);
	  analyze (0, y, progress);
	  if (!prj.images[y-1][0].get_analyzer().find_best_match (prj.params.min_overlap_percentage, prj.params.max_overlap_percentage, prj.images[y][0].get_analyzer(), prj.params.cpfind, &xs, &ys, prj.params.limit_directions ? 1 : -1, prj.images[y-1][0].basic_scr_to_img_map, prj.images[y][0].basic_scr_to_img_map, prj.report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", prj.images[y-1][0].filename.c_str (), prj.images[y][0].filename.c_str ());
	      exit (1);
	    }
	  prj.images[y][0].xpos = prj.images[y-1][0].xpos + xs;
	  prj.images[y][0].ypos = prj.images[y-1][0].ypos + ys;
	  prj.images[y-1][0].compare_contrast_with (prj.images[y][0], progress);
	  if (prj.params.geometry_info || prj.params.individual_geometry_info)
	    prj.images[y-1][0].output_common_points (NULL, prj.images[y][0], 0, 0, true, progress);
	  if (prj.params.width)
	    {
	      prj.images[y-1][1].compare_contrast_with (prj.images[y][0], progress);
	      if (prj.params.geometry_info || prj.params.individual_geometry_info)
	        prj.images[y-1][1].output_common_points (NULL, prj.images[y][0], 0, 0, true, progress);
	    }
	  if (prj.params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	}
      for (int x = 0; x < prj.params.width - 1; x++)
	{
	  coord_t xs;
	  coord_t ys;
	  analyze (x, y, progress);
	  analyze (x + 1,y, progress);
	  if (!prj.images[y][x].get_analyzer().find_best_match (prj.params.min_overlap_percentage, prj.params.max_overlap_percentage, prj.images[y][x+1].get_analyzer(), prj.params.cpfind, &xs, &ys, prj.params.limit_directions ? 0 : -1, prj.images[y][x].basic_scr_to_img_map, prj.images[y][x+1].basic_scr_to_img_map, prj.report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", prj.images[y][x].filename.c_str (), prj.images[y][x + 1].filename.c_str ());
	      if (prj.report_file)
		print_status (prj.report_file);
	      exit (1);
	    }
	  prj.images[y][x+1].xpos = prj.images[y][x].xpos + xs;
	  prj.images[y][x+1].ypos = prj.images[y][x].ypos + ys;
	  if (prj.params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!prj.images[y-1][x+1].get_analyzer().find_best_match (prj.params.min_overlap_percentage, prj.params.max_overlap_percentage, prj.images[y][x+1].get_analyzer(), prj.params.cpfind, &xs, &ys, prj.params.limit_directions ? 1 : -1, prj.images[y-1][x+1].basic_scr_to_img_map, prj.images[y][x+1].basic_scr_to_img_map, prj.report_file, progress))
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n", prj.images[y][x].filename.c_str (), prj.images[y][x + 1].filename.c_str ());
		  if (prj.report_file)
		    print_status (prj.report_file);
		  exit (1);
		}
	      if (prj.images[y][x+1].xpos != prj.images[y-1][x+1].xpos + xs
		  || prj.images[y][x+1].ypos != prj.images[y-1][x+1].ypos + ys)
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n", prj.images[y][x + 1].filename.c_str (), prj.images[y][x+1].xpos, prj.images[y][x+1].ypos, prj.images[y-1][x+1].xpos + xs, prj.images[y-1][x+1].ypos + ys);
		  if (prj.report_file)
		  {
		    fprintf (prj.report_file, "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n", prj.images[y][x + 1].filename.c_str (), prj.images[y][x+1].xpos, prj.images[y][x+1].ypos, prj.images[y-1][x+1].xpos + xs, prj.images[y-1][x+1].ypos + ys);
		    print_status (prj.report_file);
		  }
		  exit (1);
		}

	    }
	  if (y)
	    {
	      prj.images[y-1][x+1].compare_contrast_with (prj.images[y][x], progress);
	      if (prj.params.geometry_info || prj.params.individual_geometry_info)
	        prj.images[y-1][x+1].output_common_points (NULL, prj.images[y][x], 0, 0, true, progress);
	      prj.images[y-1][x+1].compare_contrast_with (prj.images[y][x+1], progress);
	      if (prj.params.geometry_info || prj.params.individual_geometry_info)
	        prj.images[y-1][x+1].output_common_points (NULL, prj.images[y][x+1], 0, 0, true, progress);
	      if (x + 2 < prj.params.width)
	      {
	         prj.images[y-1][x+1].compare_contrast_with (prj.images[y][x+2], progress);
		 if (prj.params.geometry_info || prj.params.individual_geometry_info)
		   prj.images[y-1][x+1].output_common_points (NULL, prj.images[y][x+2], 0, 0, true, progress);
	      }
	    }
	  prj.images[y][x].compare_contrast_with (prj.images[y][x+1], progress);
	 if (prj.params.geometry_info || prj.params.individual_geometry_info)
            prj.images[y][x].output_common_points (NULL, prj.images[y][x+1], 0, 0, true, progress);
	  if (prj.report_file)
	    fflush (prj.report_file);
	}
    }
  if (prj.report_file)
    print_status (prj.report_file);
  if (prj.params.panorama_map)
    {
      progress->pause_stdout ();
      print_panorama_map (stdout);
      progress->resume_stdout ();
    }
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
  if (prj.report_file)
    fprintf (prj.report_file, "Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->pause_stdout ();
  printf ("Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->resume_stdout ();
  return out;
}

void stitch (progress_info *progress)
{
  prj.passthrough_rparam.gamma = prj.rparam.gamma;
  if (prj.params.orig_tile_gamma > 0)
    prj.passthrough_rparam.output_gamma = prj.params.orig_tile_gamma;
  else
    prj.passthrough_rparam.output_gamma = prj.rparam.gamma;
  const char *error;

  scr_to_img_parameters scr_param;
  image_data data;
  scr_param.type = prj.params.type;
  data.width=1000;
  data.height=1000;
  prj.common_scr_to_img.set_parameters (scr_param, data);

  if ((prj.params.width == 1 || prj.params.height == 1) && prj.params.outer_tile_border > 40)
    {
      fprintf (stderr, "Outer tile border is too large for single row or column stitching\n");
      exit (1);
    }
  if (prj.params.outer_tile_border > 80)
    {
      fprintf (stderr, "Outer tile border is too large\n");
      exit (1);
    }
  if (prj.params.report_filename.length ())
    {
      prj.report_file = fopen (prj.params.report_filename.c_str (), "wt");
      if (!prj.report_file)
	{
	  fprintf (stderr, "Can not open report file: %s\n", prj.params.report_filename.c_str ());
	  exit (1);
	}
    }
  progress->pause_stdout ();
  printf ("Stitching:\n");
  if (prj.report_file)
    fprintf (prj.report_file, "Stitching:\n");
  for (int y = 0; y < prj.params.height; y++)
    {
      for (int x = 0; x < prj.params.width; x++)
	{
	  printf ("  %s", prj.images[y][x].filename.c_str ());
	  if (prj.report_file)
	    fprintf (prj.report_file, "  %s", prj.images[y][x].filename.c_str ());
	}
      printf("\n");
      if (prj.report_file)
        fprintf (prj.report_file, "\n");
    }
  progress->resume_stdout ();

  if (prj.params.csp_filename.length ())
    {
      const char *cspname = prj.params.csp_filename.c_str ();
      FILE *in = fopen (cspname, "rt");
      progress->pause_stdout ();
      printf ("Loading color screen parameters: %s\n", cspname);
      progress->resume_stdout ();
      if (!in)
	{
	  perror (cspname);
	  exit (1);
	}
      if (!load_csp (in, &prj.param, &prj.dparam, &prj.rparam, &prj.solver_param, &error))
	{
	  fprintf (stderr, "Can not load %s: %s\n", cspname, error);
	  exit (1);
	}
      fclose (in);
      prj.solver_param.remove_points ();
    }

  if (prj.report_file)
    {
      fprintf (prj.report_file, "Color screen parameters:\n");
      save_csp (prj.report_file, &prj.param, &prj.dparam, &prj.rparam, &prj.solver_param);
    }
  determine_positions (progress);

  std::vector<coord_t> angles;
  std::vector<coord_t> ratios;
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      {
	angles.push_back (prj.images[y][x].angle);
	ratios.push_back (prj.images[y][x].ratio);
      }
  sort(angles.begin(), angles.end());
  sort(ratios.begin(), ratios.end());
  int cap = (angles.size () + 3) / 4;
  int imin = cap;
  int imax = angles.size() - 1 - cap;
  if (imin > imax)
    imin = imax = angles.size () / 2;
  coord_t avgangle = 0;
  coord_t avgratio = 0;
  for (int i = imin; i <= imax; i++)
    {
      avgangle += angles[i];
      avgratio += ratios[i];
    }
  avgangle /= imax - imin + 1;
  avgratio /= imax - imin + 1;
  scr_param.final_angle = avgangle;
  scr_param.final_ratio = avgratio;
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      prj.images[y][x].update_scr_to_final_parameters (avgratio, avgangle);

  progress->pause_stdout ();
  printf ("Final angle %f ratio %f\n", scr_param.final_angle, scr_param.final_ratio);
  if (prj.report_file)
    fprintf (prj.report_file, "Final angle %f ratio %f\n", scr_param.final_angle, scr_param.final_ratio);
  progress->resume_stdout ();
  prj.common_scr_to_img.set_parameters (scr_param, data);



  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);
  if (prj.params.geometry_info)
    for (int y = 0; y < prj.params.height; y++)
      for (int x = 0; x < prj.params.width; x++)
	if (prj.images[y][x].stitch_info)
	  prj.images[y][x].write_stitch_info (progress);
  if (prj.params.individual_geometry_info)
    for (int y = 0; y < prj.params.height; y++)
      for (int x = 0; x < prj.params.width; x++)
	for (int yy = y; yy < prj.params.height; yy++)
	  for (int xx = (yy == y ? x : 0); xx < prj.params.width; xx++)
	    if (x != xx || y != yy)
	    {
	      prj.images[y][x].clear_stitch_info ();
	      if (prj.images[y][x].output_common_points (NULL, prj.images[yy][xx], 0, 0, true, progress))
		prj.images[y][x].write_stitch_info (progress, x, y, xx, yy);
	    }

  const coord_t xstep = prj.pixel_size, ystep = prj.pixel_size;
  prj.passthrough_rparam.gray_max = prj.images[0][0].gray_max;
  if (prj.params.hugin_pto_filename.length ())
    produce_hugin_pto_file (prj.params.hugin_pto_filename.c_str (), progress);
  if (prj.params.diffs)
    for (int y = 0; y < prj.params.height; y++)
      for (int x = 0; x < prj.params.width; x++)
	for (int yy = y; yy < prj.params.height; yy++)
	  for (int xx = (yy == y ? x : 0); xx < prj.params.width; xx++)
	    if (x != xx || y != yy)
	      prj.images[y][x].diff (prj.images[yy][xx], progress);
  if (prj.params.produce_stitched_file_p ())
    {
      TIFF *out;
      uint16_t *outrow;
      prj.images[0][0].load_img (progress);
      out =
	open_output_file (prj.params.stitched_filename.c_str (), (xmax-xmin) / xstep, (ymax-ymin) / ystep, &outrow, 
			  &error,
			  prj.images[0][0].img->icc_profile, prj.images[0][0].img->icc_profile_size,
			  progress);
      prj.images[0][0].release_img ();
      int j = 0;
      if (!out)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Can not open final stitch file %s: %s\n", prj.params.stitched_filename.c_str (), error);
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
	      prj.common_scr_to_img.final_to_scr (x, y, &sx, &sy);
	      for (iy = 0 ; iy < prj.params.height; iy++)
		{
		  for (ix = 0 ; ix < prj.params.width; ix++)
		    if (prj.images[iy][ix].analyzed && prj.images[iy][ix].pixel_known_p (sx, sy))
		      break;
		  if (ix != prj.params.width)
		    break;
		}
	      if (iy != prj.params.height)
		{
		  if (prj.images[iy][ix].render_pixel (prj.rparam, prj.passthrough_rparam, sx,sy, stitch_image::render_original,&r,&g,&b, progress))
		    set_p = true;
		  if (!prj.images[iy][ix].output)
		    {
		      if ((prj.params.orig_tiles && !prj.images[iy][ix].write_tile (&error, prj.common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_original, progress))
			  || (prj.params.demosaiced_tiles && !prj.images[iy][ix].write_tile (&error, prj.common_scr_to_img, xmin, ymin, 1, 1, stitch_image::render_demosaiced, progress))
			  || (prj.params.predictive_tiles && !prj.images[iy][ix].write_tile (&error, prj.common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_predictive, progress)))
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
	  if (!write_row (out, j, outrow, &error, progress))
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
    for (int y = 0; y < prj.params.height; y++)
      for (int x = 0; x < prj.params.width; x++)
	if ((prj.params.orig_tiles && !prj.images[y][x].write_tile (&error, prj.common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_original, progress))
	    || (prj.params.demosaiced_tiles && !prj.images[y][x].write_tile (&error, prj.common_scr_to_img, xmin, ymin, 1, 1, stitch_image::render_demosaiced, progress))
	    || (prj.params.predictive_tiles && !prj.images[y][x].write_tile (&error, prj.common_scr_to_img, xmin, ymin, xstep, ystep, stitch_image::render_predictive, progress)))
	  {
	    fprintf (stderr, "Writting tile: %s\n", error);
	    exit (1);
	  }
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      if (prj.images[y][x].img)
	prj.images[y][x].release_image_data (progress);
  if (prj.report_file)
    fclose (prj.report_file);
}
}

int
main (int argc, char **argv)
{
  file_progress_info progress (stdout);
  std::vector<std::string> fnames;
  int ncols = 0;

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
	  prj.params.report_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--report=", strlen ("--report=")))
	{
	  prj.params.report_filename = argv[i] + strlen ("--report=");
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
	  prj.params.csp_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--csp=", strlen ("--csp=")))
	{
	  prj.params.csp_filename = argv[i] + strlen ("--csp=");
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
	  prj.params.hugin_pto_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--hugin-pto=", strlen ("--hugin-pto=")))
	{
	  prj.params.hugin_pto_filename = argv[i] + strlen ("--hugin-pto=");
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
	  prj.params.stitched_filename = argv[i];
	  continue;
	}
      if (!strcmp (argv[i], "--no-cpfind"))
	{
	  prj.params.cpfind = 0;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind"))
	{
	  prj.params.cpfind = 1;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind-verification"))
	{
	  prj.params.cpfind = 2;
	  continue;
	}
      if (!strncmp (argv[i], "--stitched=", strlen ("--stitched=")))
	{
	  prj.params.stitched_filename = argv[i] + strlen ("--stitched=");
	  continue;
	}
      if (!strcmp (argv[i], "--demosaiced-tiles"))
	{
	  prj.params.demosaiced_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--predictive-tiles"))
	{
	  prj.params.predictive_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--orig-tiles"))
	{
	  prj.params.orig_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--screen-tiles"))
	{
	  prj.params.screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--known-screen-tiles"))
	{
	  prj.params.known_screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--panorama-map"))
	{
	  prj.params.panorama_map = true;
	  continue;
	}
      if (!strcmp (argv[i], "--optimize-colors"))
	{
	  prj.params.optimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-optimize-colors"))
	{
	  prj.params.optimize_colors = false;
	  continue;
	}
      if (!strcmp (argv[i], "--reoptimize-colors"))
	{
	  prj.params.reoptimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-limit-directions"))
	{
	  prj.params.limit_directions = false;
	  continue;
	}
      if (!strcmp (argv[i], "--slow-floodfill"))
	{
	  prj.params.slow_floodfill = true;
	  continue;
	}
      if (!strcmp (argv[i], "--fast-floodfill"))
	{
	  prj.params.fast_floodfill = true;
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
	  prj.params.outer_tile_border = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--outer-tile-border=", strlen ("--outer-tile-border=")))
	{
	  prj.params.outer_tile_border = atoi (argv[i] + strlen ("--outer-tile-border="));
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
	  prj.params.inner_tile_border = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--inner-tile-border=", strlen ("--inner-tile-border=")))
	{
	  prj.params.inner_tile_border = atoi (argv[i] + strlen ("--inner-tile-border="));
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
	  prj.params.max_contrast = atoi (argv[i]);
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
	  prj.params.max_unknown_screen_range = atoi (argv[i]);
	  continue;
	}
      if (!strcmp (argv[i], "--max-unknown-screen-range="))
	{
	  prj.params.max_unknown_screen_range = atoi (argv[i] + strlen("--max-unknown-screen-range="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-contrast=", strlen ("--max-contrast=")))
	{
	  prj.params.max_contrast = atoi (argv[i] + strlen ("--max-contrast="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-overlap=", strlen ("--min-overlap=")))
	{
	  prj.params.min_overlap_percentage = atoi (argv[i] + strlen ("--min-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-overlap=", strlen ("--max-overlap=")))
	{
	  prj.params.max_overlap_percentage = atoi (argv[i] + strlen ("--max-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--ncols=", strlen ("--ncols=")))
	{
	  ncols = atoi (argv[i] + strlen ("--ncols="));
	  continue;
	}
      if (!strncmp (argv[i], "--num-control-points=", strlen ("--num-control-points=")))
	{
	  prj.params.num_control_points = atoi (argv[i] + strlen ("--num-control-points="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  prj.params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--orig-tile-gamma=", strlen ("--orig-tile-gamma=")))
	{
	  prj.params.orig_tile_gamma = atof (argv[i] + strlen ("--orig-tile-gamma="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  prj.params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--hfov=", strlen ("--hfov=")))
	{
	  prj.params.hfov = atof (argv[i] + strlen ("--hfov="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-avg-distance=", strlen ("--max-avg-distance=")))
	{
	  prj.params.max_avg_distance = atof (argv[i] + strlen ("--max-avg-distance="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-max-distance=", strlen ("--max-max-distance=")))
	{
	  prj.params.max_max_distance = atof (argv[i] + strlen ("--max-max-distance="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-patch-contrast=", strlen ("--max-max-distance=")))
	{
	  prj.params.min_patch_contrast = atof (argv[i] + strlen ("--min-patch-contrast="));
	  continue;
	}
      if (!strncmp (argv[i], "--screen-type=", strlen ("--screen-type=")))
	{
	  const char * t = argv[i] + strlen ("--screen-type=");
	  if (!strcmp (t, "Paget"))
	    prj.params.type = Paget;
	  else if (!strcmp (t, "Dufay"))
	    prj.params.type = Dufay;
	  else if (!strcmp (t, "Finlay"))
	    prj.params.type = Finlay;
	  else
	    {
	      fprintf (stderr, "Unknown or unsupported screen type: %s\n", t);
	      exit (1);
	    }
	  continue;
	}
      if (!strcmp (argv[i], "--mesh"))
	{
	  prj.params.mesh_trans = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-mesh"))
	{
	  prj.params.mesh_trans = false;
	  continue;
	}
      if (!strncmp (argv[i], "--geometry-info", strlen ("--geometry-info")))
	{
	  prj.params.geometry_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--individual-geometry-info", strlen ("--individual-geometry-info")))
	{
	  prj.params.individual_geometry_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--outliers-info", strlen ("--outliers-info")))
	{
	  prj.params.outliers_info = true;
	  continue;
	}
      if (!strncmp (argv[i], "--diffs", strlen ("--diffs")))
	{
	  prj.params.diffs = true;
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
  if (!fnames.size ())
    {
      fprintf (stderr, "No files to stitch\n");
      print_help (argv[0]);
      exit (1);
    }
  if (fnames.size () == 1)
    {
      prj.params.width = 1;
      prj.params.height = 1;
   
   }
  if (ncols > 0)
    {
      prj.params.width = ncols;
      prj.params.height = fnames.size () / ncols;
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
      prj.params.width = w;
      prj.params.height = fnames.size () / w;
      for (int y = 0; y < prj.params.height; y++)
        for (int x = 0; x < prj.params.width; x++)
	  {
	    int i = y * prj.params.width + x;
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
  if (prj.params.width * prj.params.height != (int)fnames.size ())
    {
      fprintf (stderr, "For %ix%i tiles I expect %i filenames, found %i\n", prj.params.width, prj.params.height, prj.params.width * prj.params.height, (int)fnames.size ());
      print_help (argv[0]);
      exit (1);
    }
  for (int y = 0; y < prj.params.height; y++)
    for (int x = 0; x < prj.params.width; x++)
      prj.images[y][x].filename = fnames[y * prj.params.width + x];

  stitch (&progress);


  return 0;
}
