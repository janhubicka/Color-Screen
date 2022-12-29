#include <sys/time.h>
#include <bits/stdc++.h>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-dufay.h"
#include <tiffio.h>
#include "../libcolorscreen/icc-srgb.h"
#include "../libcolorscreen/render-interpolate.h"

namespace {
#define MAX_DIM 10
const int border = 20;
/* Seems that DT scans intends to overlap by 30%.  */
const int percentage = 25;
const bool stitched_file = false;

int stitch_width, stitch_height;
scr_to_img_parameters param;
render_parameters rparam;
render_parameters passthrough_rparam;
scr_detect_parameters dparam;
solver_parameters solver_param;

enum render_mode
{
  render_demosaiced,
  render_original
};

class stitch_image
{
  public:
  char *filename;
  std::string screen_filename;
  image_data *img;
  mesh *mesh_trans;
  scr_to_img_parameters param;
  scr_to_img scr_to_img_map;
  int xshift, yshift, width, height;
  int final_xshift, final_yshift;
  int final_width, final_height;
  analyze_dufay dufay;
  bitmap_2d *known_pixels;

  render_interpolate *render;
  render_img *render2;

  int xpos, ypos;
  bool analyzed;
  bool output;

  stitch_image ()
  : filename (NULL), img (NULL), mesh_trans (NULL), xshift (0), yshift (0), width (0), height (0), final_xshift (0), final_yshift (0), final_width (0), final_height (0), known_pixels (NULL), render (NULL), render2 (NULL), refcount (0)
  {
  }
  ~stitch_image ();
  void load_img (progress_info *);
  void release_img ();
  void analyze (int skiptop, int skipbottom, int skipleft, int skipright, progress_info *);
  void release_image_data (progress_info *);
  bitmap_2d *compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress);
  bool pixel_known_p (coord_t sx, coord_t sy);
  bool render_pixel (render_parameters & rparam, render_parameters &passthrough_rparam, coord_t sx, coord_t sy, render_mode mode, int *r, int *g, int *b, progress_info *p);
  bool write_tile (const char **error, scr_to_img &map, int xmin, int ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress);
private:
  static long current_time;
  static int nloaded;
  long lastused;
  int refcount;
};
long stitch_image::current_time;
int stitch_image::nloaded;

stitch_image images[MAX_DIM][MAX_DIM];

stitch_image::~stitch_image ()
{
  if (render)
    delete render;
  if (render2)
    delete render2;
  if (img)
    delete img;
  if (mesh_trans)
    delete mesh_trans;
  if (known_pixels)
    delete known_pixels;
}

void
stitch_image::release_image_data (progress_info *progress)
{
  progress->pause_stdout ();
  printf ("Releasing input tile %s\n", filename);
  progress->resume_stdout ();
  assert (!refcount && img);
  if (render)
    delete render;
  if (render2)
    delete render2;
  delete img;
  img = NULL;
  render = NULL;
  render2 = NULL;
  nloaded--;
}

void
stitch_image::load_img (progress_info *progress)
{
  refcount++;
  lastused = ++current_time;
  if (img)
    return;
  if (nloaded >= stitched_file ? stitch_width * 2 : 2)
    {
      int minx = -1, miny = -1;
      long minlast = 0;
      int nref = 0;


#if 0
      progress->pause_stdout ();
      for (int y = 0; y < stitch_height; y++)
      {
	for (int x = 0; x < stitch_width; x++)
	  printf (" %i:%5i", images[y][x].img != NULL, (int)images[y][x].lastused);
        printf ("\n");
      }
      progress->resume_stdout ();
#endif

      for (int y = 0; y < stitch_height; y++)
	for (int x = 0; x < stitch_width; x++)
	  if (images[y][x].refcount)
	    nref++;
	  else if (images[y][x].img
		   && (minx == -1 || images[y][x].lastused < minlast))
	    {
	      minx = x;
	      miny = y;
	      minlast = images[y][x].lastused;
	    }
      if (minx != -1)
	images[miny][minx].release_image_data (progress);
      else
	printf ("Too many (%i) images referenced\n", nref);
    }
  nloaded++;
  progress->pause_stdout ();
  printf ("Loading input tile %s (%i tiles in memory)\n", filename, nloaded);
  progress->resume_stdout ();
  img = new image_data;
  const char *error;
  if (!img->load (filename, &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", filename, error);
      exit (1);
    }
  if (!img->rgbdata)
    {
      progress->pause_stdout ();
      fprintf (stderr, "File %s is not having color channels\n", filename);
      exit (1);
    }
}

void
stitch_image::release_img ()
{
  refcount--;
}
bitmap_2d*
stitch_image::compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress)
{
  bitmap_2d *known_pixels = new bitmap_2d (width, height);
  if (!known_pixels)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Out of memory allocating known pixels bitmap for %s\n", filename);
      exit (1);
    }
  if (progress)
    progress->set_task ("determining known pixels", width * height);
  int xmin = img.width * skipleft / 100;
  int xmax = img.width * (100 - skipright) / 100;
  int ymin = img.height * skiptop / 100;
  int ymax = img.height * (100 - skipbottom) / 100;
  //printf ("Range: %i %i %i %i\n",xmin,xmax,ymin,ymax);
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
	{
	  coord_t x1, y1;
	  scr_to_img.to_img (x - xshift - 1, y - yshift - 1, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift + 2, y - yshift - 1, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift - 1, y - yshift + 2, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift + 2, y - yshift + 2, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  known_pixels->set_bit (x, y);
	}
      if (progress)
	progress->inc_progress ();
    }
  return known_pixels;
}

void
stitch_image::analyze (int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress)
{
  if (analyzed)
    return;
  //bitmap_2d *bitmap;
  load_img (progress);
  //mesh_trans = detect_solver_points (*img, dparam, solver_param, progress, &xshift, &yshift, &width, &height, &bitmap);
  mesh_trans = detect_solver_points (*img, dparam, solver_param, progress);
  if (!mesh_trans)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to analyze screen of %s\n", filename);
      exit (1);
    }
  render_parameters my_rparam;
  my_rparam.gamma = rparam.gamma;
  my_rparam.precise = true;
  my_rparam.gray_max = img->maxval;
  param.mesh_trans = mesh_trans;
  param.type = Dufay;
  render_to_scr render (param, *img, my_rparam, 256);
  render.precompute_all (true, progress);
  scr_to_img_map.set_parameters (param, *img);
  final_xshift = render.get_final_xshift ();
  final_yshift = render.get_final_yshift ();
  final_width = render.get_final_width ();
  final_height = render.get_final_height ();

  scr_to_img_map.get_range (img->width, img->height, &xshift, &yshift, &width, &height);
  dufay.analyze (&render, width, height, xshift, yshift, true, progress);
  dufay.set_known_pixels (compute_known_pixels (*img, scr_to_img_map, skiptop, skipbottom, skipleft, skipright, progress));
  screen_filename = (std::string)"screen"+(std::string)filename;
  known_pixels = compute_known_pixels (*img, scr_to_img_map, 0, 0, 0, 0, progress);
  const char *error;
  if (!dufay.write_screen (screen_filename.c_str (), NULL, &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", screen_filename.c_str (), error);
      exit (1);
    }
  //dufay.set_known_pixels (bitmap);
  analyzed = true;
  release_img ();
}

bool
stitch_image::pixel_known_p (coord_t sx, coord_t sy)
{
  int ax = floor (sx) + xshift - xpos;
  int ay = floor (sy) + yshift - ypos;
  if (ax < 0 || ay < 0 || ax >= width || ay >= height)
    return false;
  return known_pixels->test_bit (ax, ay);
}

bool
stitch_image::render_pixel (render_parameters & my_rparam, render_parameters &passthrough_rparam, coord_t sx, coord_t sy, render_mode mode, int *r, int *g, int *b, progress_info *progress)
{
  bool loaded = false;
  if (mode == render_demosaiced ? !render : !render2)
    {
      load_img (progress);
      if (mode == render_demosaiced)
	{
	  render = new render_interpolate (param, *img, my_rparam, 65535, false, false);
	  render->precompute_all (progress);
	}
      else
	{
          render2 = new render_img (param, *img, passthrough_rparam, 65535);
          render2->set_color_display ();
          render2->precompute_all (progress);
	}
      release_img ();
      loaded = true;
    }
  else
    lastused = ++current_time;
  assert (pixel_known_p (sx, sy));
  if (mode == render_demosaiced)
    render->render_pixel_scr (sx - xpos, sy - ypos, r, g, b);
  else
    render2->render_pixel (sx - xpos, sy - ypos, r, g, b);
  /**r = 65535;*/
  return loaded;
}

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_tile_output_file (const char *outfname, 
		       int xoffset, int yoffset,
		       int outwidth, int outheight,
		       uint16_t ** outrow, bool verbose, const char **error,
		       void *icc_profile, uint32_t icc_profile_size,
		       enum render_mode mode,
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
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, icc_profile && mode == render_original ? icc_profile_size : sRGB_icc_len, icc_profile && mode == render_original ? icc_profile : sRGB_icc))
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
  if (!*outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving", outheight);
    }
  if (verbose)
    {
      progress->pause_stdout ();
      printf ("Rendering %s at offset %i,%i in resolution %ix%i\n", outfname, xoffset, yoffset, outwidth,
	      outheight);
      progress->resume_stdout ();
    }
  return out;
}

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

bool
stitch_image::write_tile (const char **error, scr_to_img &map, int stitch_xmin, int stitch_ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress)
{
  std::string fname=filename;
  std::string prefix= mode == render_demosaiced ? "demosaicedtile-" : "tile-";
  uint16_t *outrow;
  coord_t final_xpos, final_ypos;
  map.scr_to_final (xpos, ypos, &final_xpos, &final_ypos);
  int xmin = floor ((final_xpos - final_xshift) / xstep) * xstep;
  int ymin = floor ((final_ypos - final_yshift) / ystep) * ystep;

  load_img (progress);
  TIFF *out = open_tile_output_file ((prefix+fname).c_str(), (xmin - stitch_xmin) / xstep, (ymin - stitch_ymin) / ystep, final_width / xstep, final_height / ystep, &outrow, true, error, img->icc_profile, img->icc_profile_size, mode, progress);
  if (!out)
    {
      release_img ();
      return false;
    }
  int j = 0;
  for (coord_t y = ymin; j < final_height / ystep; y+=ystep, j++)
    {
      int i = 0;
      for (coord_t x = xmin; i < final_width / xstep; x+=xstep, i++)
	{
	  coord_t sx, sy;
	  int r = 0,g = 0,b = 0;
	  map.final_to_scr (x, y, &sx, &sy);
	  //printf ("%f %f\n",x,y);
	  if (pixel_known_p (sx, sy))
	    {
	      render_pixel (rparam, passthrough_rparam, sx, sy, mode,&r,&g,&b, progress);
	      outrow[4 * i] = r;
	      outrow[4 * i + 1] = g;
	      outrow[4 * i + 2] = b;
	      outrow[4 * i + 3] = 65535;
	    }
	  else
	    {
	      outrow[4 * i] = 0;
	      outrow[4 * i + 1] = 0;
	      outrow[4 * i + 2] = 0;
	      outrow[4 * i + 3] = 0;
	    }
	}
      if (!write_row (out, j, outrow, error, progress))
	{
	  *error = "Writting failed";
	  TIFFClose (out);
	  free (outrow);
	  release_img ();
	  return false;
	}
    }
  progress->set_task ("Closing tile output file", 1);
  TIFFClose (out);
  free (outrow);
  output = true;
  release_img ();
  return true;
}

void
print_help (const char *filename)
{
  printf ("%s output.tif parameters.par <xdim> <ydim> imag11.tif img12.tif ....\n", filename);
}

void
determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax)
{
  scr_to_img_parameters scr_param;
  image_data data;
  scr_param.type = Dufay;
  data.width=1000;
  data.height=1000;
  scr_to_img map;
  map.set_parameters (scr_param, data);
  xmin = 0;
  ymin = 0;
  xmax = 0;
  ymax = 0;
  for (int y = 0; y < stitch_height; y++)
    for (int x = 0; x < stitch_width; x++)
      if (images[y][x].analyzed)
	{
	  coord_t x1,y1,x2,y2;
	  coord_t rxpos, rypos;
	  map.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rxpos, &rypos);
	  x1 = -images[y][x].final_xshift + rxpos;
	  y1 = -images[y][x].final_yshift + rypos;
	  x2 = x1 + images[y][x].final_width;
	  y2 = y1 + images[y][x].final_height;

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
print_status ()
{
  scr_to_img_parameters scr_param;
  image_data data;
  scr_param.type = Dufay;
  data.width=1000;
  data.height=1000;
  scr_to_img map;
  map.set_parameters (scr_param, data);

  for (int y = 0; y < stitch_height; y++)
    {
      if (y)
	{
	  coord_t rx, ry;
	  map.scr_to_final (images[y-1][0].xpos, images[y-1][0].ypos, &rx, &ry);
	  coord_t rx2, ry2;
	  map.scr_to_final (images[y][0].xpos, images[y][0].ypos, &rx2, &ry2);
	  rx -= images[y-1][0].xshift;
	  ry -= images[y-1][0].yshift;
	  rx2 -= images[y][0].xshift;
	  ry2 -= images[y][0].yshift;
	  printf (" down %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	}
      else printf ("                  ");
      for (int x = 1; x < stitch_width; x++)
      {
	coord_t rx, ry;
	map.scr_to_final (images[y][x-1].xpos, images[y][x-1].ypos, &rx, &ry);
	coord_t rx2, ry2;
	map.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx2, &ry2);
	rx -= images[y][x-1].xshift;
	ry -= images[y][x-1].yshift;
	rx2 -= images[y][x].xshift;
	ry2 -= images[y][x].yshift;
	printf (" right %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	//printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      printf ("\n");
    }
  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
      {
	coord_t rx, ry;
	map.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx, &ry);
	printf ("  %-5i,%-5i  rotated:%-5i,%-5i ", images[y][x].xpos, images[y][x].ypos, (int)rx,(int)ry);
      }
      printf ("\n");
    }
  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);
  printf ("Viewport range %i %i %i %i\n", xmin, xmax, ymin, ymax);
  for (int y = 0; y < 20; y++)
    {
      for (int x = 0; x < 20; x++)
	{
	  coord_t fx = xmin + (xmax - xmin) * x / 20;
	  coord_t fy = ymin + (ymax - ymin) * y / 20;
	  coord_t sx, sy;
	  int ix = 0, iy = 0;
	  map.final_to_scr (fx, fy, &sx, &sy);
	  //printf ("%f %f %f %f\n",fx,fy,sx,sy);
	  for (iy = 0 ; iy < stitch_height; iy++)
	    {
	      for (ix = 0 ; ix < stitch_width; ix++)
		if (images[iy][ix].analyzed && images[iy][ix].pixel_known_p (sx, sy))
		  break;
	      if (ix != stitch_width)
		break;
	    }

	  if (iy == stitch_height)
	    printf ("   ");
	  else
	    printf (" %i%i",iy+1,ix+1);
	}
      printf ("\n");
    }
}

void
analyze (int x, int y, progress_info *progress)
{
  images[y][x].analyze (!y ? border : 0, y == stitch_height - 1 ? border : 0, !x ? border : 0, x == stitch_width - 1 ? border : 0, progress);
}

void
determine_positions (progress_info *progress)
{
  for (int y = 0; y < stitch_height; y++)
    {
      if (!y)
	{
	  images[0][0].xpos = 0;
	  images[0][0].ypos = 0;
	}
      else
	{
	  int xs;
	  int ys;
	  analyze (0, y-1, progress);
	  analyze (0, y, progress);
	  if (!images[y-1][0].dufay.find_best_match (percentage, images[y][0].dufay, images[y-1][0].screen_filename.c_str (), images[y][0].screen_filename.c_str(), &xs, &ys, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y-1][0].filename, images[y][0].filename);
	      exit (1);
	    }
	  images[y][0].xpos = images[y-1][0].xpos + xs;
	  images[y][0].ypos = images[y-1][0].ypos + ys;
	  progress->pause_stdout ();
	  print_status ();
	  progress->resume_stdout ();
	}
      for (int x = 0; x < stitch_width - 1; x++)
	{
	  int xs;
	  int ys;
	  analyze (x, y, progress);
	  analyze (x + 1,y, progress);
	  if (!images[y][x].dufay.find_best_match (percentage, images[y][x+1].dufay, images[y][x].screen_filename.c_str (), images[y][x+1].screen_filename.c_str(), &xs, &ys, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename, images[y][x + 1].filename);
	      exit (1);
	    }
	  images[y][x+1].xpos = images[y][x].xpos + xs;
	  images[y][x+1].ypos = images[y][x].ypos + ys;
	  progress->pause_stdout ();
	  print_status ();
	  progress->resume_stdout ();
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!images[y-1][x+1].dufay.find_best_match (percentage, images[y][x+1].dufay, images[y-1][x+1].screen_filename.c_str (), images[y][x+1].screen_filename.c_str(), &xs, &ys, progress))
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename, images[y][x + 1].filename);
		  exit (1);
		}
	      if (images[y][x+1].xpos != images[y-1][x+1].xpos + xs
		  || images[y][x+1].ypos != images[y-1][x+1].ypos + ys)
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Stitching mismatch in %s: %i,%i is not equal to %i,%i\n", images[y][x + 1].filename, images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		  exit (1);
		}

	    }
	}
    }
}

/* Start writting output file to OUTFNAME with dimensions OUTWIDTHxOUTHEIGHT.
   File will be 16bit RGB TIFF.
   Allocate output buffer to hold single row to OUTROW.  */
static TIFF *
open_output_file (const char *outfname, int outwidth, int outheight,
		  uint16_t ** outrow, bool verbose, const char **error,
		  void *icc_profile, uint32_t icc_profile_size,
		  progress_info *progress)
{
  TIFF *out = TIFFOpen (outfname, "wb");
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
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, sRGB_icc_len, sRGB_icc)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, icc_profile ? icc_profile_size : sRGB_icc_len, icc_profile ? icc_profile : sRGB_icc))
    {
      *error = "write error";
      return NULL;
    }
  *outrow = (uint16_t *) malloc (outwidth * 2 * 3);
  if (!*outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving tile", outheight);
    }
  if (verbose)
    {
      progress->pause_stdout ();
      printf ("Rendering %s in resolution %ix%i\n", outfname, outwidth,
	      outheight);
      progress->resume_stdout ();
    }
  return out;
}
}

int
main (int argc, char **argv)
{
  char *outfname;
  const char *error;
  const char *cspname;

  if (argc < 5)
  {
    print_help (argv[0]);
    exit(1);
  }
  outfname = argv[1];
  cspname = argv[2];
  /* Load color screen and rendering parameters.  */
  FILE *in = fopen (cspname, "rt");
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
  passthrough_rparam.gamma = rparam.gamma;
  passthrough_rparam.output_gamma = rparam.gamma;

  stitch_width = atoi(argv[3]);
  if (stitch_width <= 0 || stitch_width > MAX_DIM)
    {
      fprintf (stderr, "Invalid stich width %s\n", argv[3]);
      print_help (argv[0]);
      exit(1);
    }
  stitch_height = atoi(argv[4]);
  if (stitch_height <= 0 || stitch_height > MAX_DIM)
    {
      fprintf (stderr, "Invalid stich height %s\n", argv[4]);
      print_help (argv[0]);
      exit(1);
    }
  if (argc != 5 + stitch_width * stitch_height)
    {
      fprintf (stderr, "Expected %i parameters, have %i\n", 5 + stitch_width * stitch_height, argc);
      print_help (argv[0]);
      exit(1);
    }
  for (int y = 0, n = 5; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
	{
	  images[y][x].filename = argv[n++];
	  printf ("   %s", images[y][x].filename);
	}
      printf ("\n");
    }
  file_progress_info progress (stdout);
#if 0
  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
	images[y][x].analyze (&progress);
    }
#endif
  determine_positions (&progress);

  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);

  const coord_t xstep = 0.2, ystep = 0.2;
  /* We need ICC profile.  */
  images[0][0].load_img (&progress);
  passthrough_rparam.gray_max = images[0][0].img->maxval;
  images[0][0].release_img ();
  scr_to_img_parameters scr_param;
  image_data data;
  scr_param.type = Dufay;
  data.width=1000;
  data.height=1000;
  scr_to_img map;
  map.set_parameters (scr_param, data);
  if (stitched_file)
    {
      TIFF *out;
      uint16_t *outrow;
      out =
	open_output_file (outfname, (xmax-xmin) / xstep, (ymax-ymin) / ystep, &outrow, true,
			  &error,
			  images[0][0].img->icc_profile, images[0][0].img->icc_profile_size,
			  &progress);
	int j = 0;
      if (!out)
	{
	  progress.pause_stdout ();
	  fprintf (stderr, "Can not open final stitch file %s: %f\n", outfname, error);
	  exit (1);
	}
      for (coord_t y = ymin; j < (ymax-ymin) / ystep; y+=ystep, j++)
	{
	  int i = 0;
	  bool set_p = false;
	  for (coord_t x = xmin; i < (xmax-xmin) / xstep; x+=xstep, i++)
	    {
	      coord_t sx, sy;
	      int r = 0,g = 0,b = 0;
	      int ix = 0, iy = 0;
	      map.final_to_scr (x, y, &sx, &sy);
	      for (iy = 0 ; iy < stitch_height; iy++)
		{
		  for (ix = 0 ; ix < stitch_width; ix++)
		    if (images[iy][ix].analyzed && images[iy][ix].pixel_known_p (sx, sy))
		      break;
		  if (ix != stitch_width)
		    break;
		}
	      if (iy != stitch_height)
		{
		  if (images[iy][ix].render_pixel (rparam, passthrough_rparam, sx,sy, render_original,&r,&g,&b, &progress))
		    set_p = true;
		  if (!images[iy][ix].output)
		    {
		      if (!images[iy][ix].write_tile (&error, map, xmin, ymin, xstep, ystep, render_original, &progress)
			  || !images[iy][ix].write_tile (&error, map, xmin, ymin, 1, 1, render_demosaiced, &progress))
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
	      progress.set_task ("Rendering and saving", (ymax-ymin) / ystep);
	      progress.set_progress (j);
	    }
	  if (!write_row (out, j, outrow, &error, &progress))
	    {
	      fprintf (stderr, "Writting failed: %s\n", error);
	      exit (1);
	    }
	}
      progress.set_task ("Closing output file", 1);

      TIFFClose (out);
      free (outrow);
      progress.set_task ("Releasing memory", 1);
    }
  else
    for (int y = 0; y < stitch_height; y++)
      for (int x = 0; x < stitch_width; x++)
	if (!images[y][x].write_tile (&error, map, xmin, ymin, xstep, ystep, render_original, &progress)
	    || !images[y][x].write_tile (&error, map, xmin, ymin, 1, 1, render_demosaiced, &progress))
	  {
	    fprintf (stderr, "Writting tile: %s\n", error);
	    exit (1);
	  }
  for (int y = 0; y < stitch_height; y++)
    for (int x = 0; x < stitch_width; x++)
      if (images[y][x].img)
	images[y][x].release_image_data (&progress);


  return 0;
}
