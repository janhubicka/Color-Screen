#include <sys/time.h>
#include <gsl/gsl_multifit.h>
#include <string>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-dufay.h"
#include <tiffio.h>
#include "../libcolorscreen/icc-srgb.h"
#include "../libcolorscreen/render-interpolate.h"

namespace {

struct stitching_params
{
  static const int max_dim = 10;

  bool demosaiced_tiles;
  bool predictive_tiles;
  bool orig_tiles;
  bool screen_tiles;
  bool known_screen_tiles;
  int cpfind;
  bool panorama_map;
  bool optimize_colors;
  bool reoptimize_colors;
  bool slow_floodfill;
  bool limit_directions;

  int outer_tile_border;
  int min_overlap_percentage;
  int max_overlap_percentage;
  luminosity_t max_contrast;
  luminosity_t orig_tile_gamma;

  int num_control_points;
  int min_screen_percentage;
  coord_t hfov;
  coord_t max_avg_distance;
  coord_t max_max_distance;

  int width, height;
  std::string filename[max_dim][max_dim];
  std::string csp_filename;
  std::string hugin_pto_filename;
  std::string report_filename;
  std::string stitched_filename;

  bool produce_stitched_file_p ()
  {
    return !stitched_filename.empty ();
  }

  stitching_params ()
  : demosaiced_tiles (false), predictive_tiles (false), orig_tiles (false), screen_tiles (false), known_screen_tiles (false),
    cpfind (true), panorama_map (false), optimize_colors (true), reoptimize_colors (false), slow_floodfill (false), limit_directions (true),
    outer_tile_border (30), min_overlap_percentage (10), max_overlap_percentage (65), max_contrast (-1), orig_tile_gamma (-1), num_control_points (100), min_screen_percentage (75), hfov (28.534),
    max_avg_distance (1), max_max_distance (10)
  {}
} stitching_params;

scr_to_img_parameters param;
render_parameters rparam;
render_parameters passthrough_rparam;
scr_detect_parameters dparam;
scr_to_img common_scr_to_img;
solver_parameters solver_param;
FILE *report_file;

bool initialized = false;
screen *my_screen;
coord_t pixel_size;

enum render_mode
{
  render_demosaiced,
  render_predictive,
  render_original
};

class stitch_image
{
  public:
  std::string filename;
  std::string screen_filename;
  std::string known_screen_filename;
  image_data *img;
  mesh *mesh_trans;
  scr_to_img_parameters param;
  /* scr_to_img map holding mesh_trans.  */
  scr_to_img scr_to_img_map;
  /* scr_to_img map holding detected parameters.  */
  scr_to_img basic_scr_to_img_map;
  int img_width, img_height;
  int xshift, yshift, width, height;
  int final_xshift, final_yshift;
  int final_width, final_height;
  analyze_dufay dufay;
  /* Screen patches that was detected by screen detection algorithm.  */
  bitmap_2d *screen_detected_patches;
  /* Known pixels used by stitching algorithm.  This is basically the image without borders.  */
  bitmap_2d *known_pixels;

  detected_screen detected;

  render_interpolate *render;
  render_img *render2;
  render_interpolate *render3;

  struct stitch_info {coord_t x,y;
    		      int sum;} *stitch_info;

  int xpos, ypos;
  bool analyzed;
  bool output;
  int gray_max;

  bool top, bottom, left, right;

  stitch_image ()
  : filename (""), img (NULL), mesh_trans (NULL), xshift (0), yshift (0), width (0), height (0), final_xshift (0), final_yshift (0), final_width (0), final_height (0), screen_detected_patches (NULL), known_pixels (NULL), render (NULL), render2 (NULL), render3 (NULL), stitch_info (NULL), refcount (0)
  {
  }
  ~stitch_image ();
  void load_img (progress_info *);
  void release_img ();
  void analyze (bool top_p, bool bottom_p, bool left_p, bool right_p, progress_info *);
  void release_image_data (progress_info *);
  bitmap_2d *compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress);
  void output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress = NULL);
  bool pixel_known_p (coord_t sx, coord_t sy);
  bool img_pixel_known_p (coord_t sx, coord_t sy);
  bool render_pixel (render_parameters & rparam, render_parameters &passthrough_rparam, coord_t sx, coord_t sy, render_mode mode, int *r, int *g, int *b, progress_info *p);
  bool write_tile (const char **error, scr_to_img &map, int xmin, int ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress);
  void compare_contrast_with (stitch_image &other, progress_info *progress);
  void write_stitch_info (progress_info *progress);
private:
  static long current_time;
  static int nloaded;
  long lastused;
  int refcount;
};
long stitch_image::current_time;
int stitch_image::nloaded;

stitch_image images[stitching_params::max_dim][stitching_params::max_dim];

stitch_image::~stitch_image ()
{
  delete render;
  delete render2;
  delete render3;
  delete img;
  delete mesh_trans;
  delete known_pixels;
  delete screen_detected_patches;
  delete stitch_info;
}

void
stitch_image::release_image_data (progress_info *progress)
{
  //progress->pause_stdout ();
  //printf ("Releasing input tile %s\n", filename.c_str ());
  //progress->resume_stdout ();
  assert (!refcount && img);
  delete img;
  img = NULL;
  delete render;
  render = NULL;
  delete render2;
  render2 = NULL;
  delete render3;
  render3 = NULL;
  nloaded--;
}

void
stitch_image::load_img (progress_info *progress)
{
  refcount++;
  lastused = ++current_time;
  if (img)
    return;
  if (nloaded >= (stitching_params.produce_stitched_file_p ()
		  ? stitching_params.width * 2 : 1))
    {
      int minx = -1, miny = -1;
      long minlast = 0;
      int nref = 0;


#if 0
      progress->pause_stdout ();
      for (int y = 0; y < stitching_params.height; y++)
      {
	for (int x = 0; x < stitching_params.width; x++)
	  printf (" %i:%5i", images[y][x].img != NULL, (int)images[y][x].lastused);
        printf ("\n");
      }
      progress->resume_stdout ();
#endif

      for (int y = 0; y < stitching_params.height; y++)
	for (int x = 0; x < stitching_params.width; x++)
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
  printf ("Loading input tile %s (%i tiles in memory)\n", filename.c_str (), nloaded);
  progress->resume_stdout ();
  img = new image_data;
  const char *error;
  if (!img->load (filename.c_str (), &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", filename.c_str (), error);
      exit (1);
    }
  img_width = img->width;
  img_height = img->height;
  if (!img->rgbdata)
    {
      progress->pause_stdout ();
      fprintf (stderr, "File %s is not having color channels\n", filename.c_str ());
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
      fprintf (stderr, "Out of memory allocating known pixels bitmap for %s\n", filename.c_str ());
      exit (1);
    }
  if (progress)
    progress->set_task ("determining known pixels", width * height);
  int xmin = img.width * skipleft / 100;
  int xmax = img.width * (100 - skipright) / 100;
  int ymin = img.height * skiptop / 100;
  int ymax = img.height * (100 - skipbottom) / 100;
  //progress->pause_stdout ();
  //printf ("Skip: %i %i %i %i Range: %i %i %i %i\n", skiptop, skipbottom, skipleft, skipright,xmin,xmax,ymin,ymax);
  //progress->resume_stdout ();
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

/* Output common points to hugin pto file.  */
void
stitch_image::output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress)
{
  int n = 0;
  coord_t border = 5 / 100.0;
  const int range = 2;
  for (int y = -yshift; y < -yshift + height; y++)
    {
      int yy = y + ypos - other.ypos;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    int xx = x + xpos - other.xpos;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range (x + xshift, y + yshift, range)
		&& other.screen_detected_patches->test_range (xx + other.xshift, yy + other.yshift, range))
	    {
	      coord_t x1, y1, x2, y2;
	      mesh_trans->apply (x,y, &x1, &y1);
	      other.mesh_trans->apply (xx, yy, &x2, &y2);
	      if (x1 < img_width * border || x1 >= img_width * (1 - border) || y1 < img_height * border || y1 >= img_height * (1 - border)
	          || x2 < other.img_width * border || x2 >= other.img_width * (1 - border) || y2 < other.img_height * border || y2 >= other.img_height * (1 - border))
		continue;
	      n++;
	    }
	  }
    }
  if (!n)
    return;
  int step = std::max (n / stitching_params.num_control_points, 1);
  int npoints = n / step;
  int nfound = 0;
  gsl_matrix *X = NULL, *cov = NULL;
  gsl_vector *vy = NULL, *w = NULL, *c = NULL;
  if (collect_stitch_info)
    {
      X = gsl_matrix_alloc (npoints * 2, 6);
      vy = gsl_vector_alloc (npoints * 2);
      w = gsl_vector_alloc (npoints * 2);
      c = gsl_vector_alloc (6);
      cov = gsl_matrix_alloc (6, 6);
    }

  for (int y = -yshift, m = 0, next = 0; y < -yshift + height; y++)
    {
      int yy = y + ypos - other.ypos;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    int xx = x + xpos - other.xpos;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range (x + xshift, y + yshift, range)
		&& other.screen_detected_patches->test_range (xx + other.xshift, yy + other.yshift, range))
	      {
		coord_t x1, y1, x2, y2;
		mesh_trans->apply (x,y, &x1, &y1);
		other.mesh_trans->apply (xx, yy, &x2, &y2);
		if (x1 < img_width * border || x1 >= img_width * (1 - border) || y1 < img_height * border || y1 >= img_height * (1 - border)
		    || x2 < other.img_width * border || x2 >= other.img_width * (1 - border) || y2 < other.img_height * border || y2 >= other.img_height * (1 - border))
		  continue;
	        if (m++ == next)
		  {
		    next += step;
		    if (f)
		      fprintf (f,  "c n%i N%i x%f y%f X%f Y%f t0\n", n1, n2, x1, y1, x2, y2);

		    if (!collect_stitch_info || nfound >= npoints)
		      continue;

		    gsl_matrix_set (X, nfound * 2, 0, 1.0);
		    gsl_matrix_set (X, nfound * 2, 1, 0.0);
		    gsl_matrix_set (X, nfound * 2, 2, x1);
		    gsl_matrix_set (X, nfound * 2, 3, 0);
		    gsl_matrix_set (X, nfound * 2, 4, y1);
		    gsl_matrix_set (X, nfound * 2, 5, 0);

		    gsl_matrix_set (X, nfound * 2+1, 0, 0.0);
		    gsl_matrix_set (X, nfound * 2+1, 1, 1.0);
		    gsl_matrix_set (X, nfound * 2+1, 2, 0);
		    gsl_matrix_set (X, nfound * 2+1, 3, x1);
		    gsl_matrix_set (X, nfound * 2+1, 4, 0);
		    gsl_matrix_set (X, nfound * 2+1, 5, y1);

		    gsl_vector_set (vy, nfound * 2, x2);
		    gsl_vector_set (vy, nfound * 2 + 1, y2);
		    gsl_vector_set (w, nfound * 2, 1.0);
		    gsl_vector_set (w, nfound * 2 + 1, 1.0);
		    nfound++;
		  }
	      }
	  }
    }
  if (collect_stitch_info)
    {
      double chisq;
      if (nfound != npoints)
	abort ();
      gsl_multifit_linear_workspace * work
	= gsl_multifit_linear_alloc (npoints*2, 6);
      gsl_multifit_wlinear (X, w, vy, c, cov,
			    &chisq, work);
      gsl_multifit_linear_free (work);
      coord_t distsum = 0;
      coord_t maxdist = 0;
      if (!stitch_info)
	stitch_info = (struct stitch_info *)calloc ((img_width / 10 + 1) * (img_height / 10 + 1), sizeof (struct stitch_info));
      if (!other.stitch_info)
	other.stitch_info = (struct stitch_info *)calloc ((other.img_width / 10 + 1) * (other.img_height / 10 + 1), sizeof (struct stitch_info));
      npoints = 0;
#define C(i) (gsl_vector_get(c,(i)))
      for (int y = -yshift; y < -yshift + height; y++)
	{
	  int yy = y + ypos - other.ypos;
	  if (yy >= -other.yshift && yy < -other.yshift + other.height)
	    for (int x = -xshift; x < -xshift + width; x++)
	      {
		int xx = x + xpos - other.xpos;
		if (xx >= -other.xshift && xx < -other.xshift + other.width
		    && screen_detected_patches->test_bit (x + xshift, y + yshift)
		    && other.screen_detected_patches->test_bit (xx + other.xshift, yy + other.yshift))
		  {
		    coord_t x1, y1, x2, y2;
		    mesh_trans->apply (x,y, &x1, &y1);
		    other.mesh_trans->apply (xx, yy, &x2, &y2);
		    if (x1 < 0 || x1 >= img_width || y1 < 0 || y1 >= img_height
			|| x2 < 0 || x2 >= other.img_width || y2 < 0 || y2 > other.img_height)
		      continue;
		    coord_t px = C(0) + x1 * C(2) + y1 * C(4);
		    coord_t py = C(1) + x1 * C(3) + y1 * C(5);
		    coord_t dist = sqrt ((x2 - px) * (x2 - px) + (y2 - py) * (y2 - py));
		    distsum += dist;
		    maxdist = std::max (maxdist, dist);
		    assert ((((int)y1) / 10) * (img_width / 10 + 1) + ((int)x1) / 10 <= (img_width / 10 + 1) * (img_height / 10 + 1));
		    assert ((((int)y1) / 10) * (img_width / 10 + 1) + ((int)x1) / 10 >= 0);
		    struct stitch_info &info = stitch_info[(((int)y1) / 10) * (img_width / 10 + 1) + ((int)x1) / 10];
		    info.x += fabs(x2-px);
		    info.y += fabs(y2-py);
		    info.sum++;
		    assert ((((int)y2) / 10) * (other.img_width / 10 + 1) + ((int)x2) / 10 <= (other.img_width / 10 + 1) * (other.img_height / 10 + 1));
		    assert ((((int)y2) / 10) * (other.img_width / 10 + 1) + ((int)x2) / 10 >= 0);
		    struct stitch_info &info2 = other.stitch_info[(((int)y2) / 10) * (other.img_width / 10 + 1) + ((int)x2) / 10];
		    info2.x += fabs(x2-px);
		    info2.y += fabs(y2-py);
		    info2.sum++;
		    npoints++;
		  }
	      }
	}

      progress->pause_stdout ();
      printf ("Overlap of %s and %s avg distance %f max distance %f\n", filename.c_str (), other.filename.c_str (), distsum / npoints, maxdist);
      if (report_file)
        fprintf (report_file, "Overlap of %s and %s avg distance %f max distance %f\n", filename.c_str (), other.filename.c_str (), distsum / npoints, maxdist);
      progress->resume_stdout ();
      gsl_matrix_free (X);
      gsl_vector_free (vy);
      gsl_vector_free (w);
      gsl_vector_free (c);
      gsl_matrix_free (cov);
      if (distsum / npoints > stitching_params.max_avg_distance)
	{
	  progress->pause_stdout ();
	  printf ("Average distance out of tolerance (--max-avg-distnace parameter)\n");
	  if (report_file)
	    fprintf (report_file, "Average distance out of tolerance (--max-avg-distnace parameter)\n");
	  progress->resume_stdout ();
	  write_stitch_info (progress);
	  exit (1);
	}
      if (maxdist > stitching_params.max_max_distance)
	{
	  progress->pause_stdout ();
	  printf ("Maximal distance out of tolerance (--max-max-distnace parameter)\n");
	  if (report_file)
	    fprintf (report_file, "Maximal distance out of tolerance (--max-max-distnace parameter)\n");
	  progress->resume_stdout ();
	  write_stitch_info (progress);
	  exit (1);
	}
    }
}

void
stitch_image::analyze (bool top_p, bool bottom_p, bool left_p, bool right_p, progress_info *progress)
{
  if (analyzed)
    return;
  top = top_p;
  bottom = bottom_p;
  left = left_p;
  right = right_p;
  if (report_file)
    fprintf (report_file, "\n\nAnalysis of %s\n", filename.c_str ());
  //bitmap_2d *bitmap;
  load_img (progress);
  //mesh_trans = detect_solver_points (*img, dparam, solver_param, progress, &xshift, &yshift, &width, &height, &bitmap);
#if 0
  if (stitching_params.optimize_colors)
  {
    if (!optimize_screen_colors (&dparam, img, rparam.gamma, img->width / 2 - 500, img->height /2 - 500, 1000, 1000,  progress, report_file))
      {
	progress->pause_stdout ();
	fprintf (stderr, "Failed analyze screen colors of %s\n", filename.c_str ());
	exit (1);
      }
  }
#endif
  detect_regular_screen_params dsparams;
  dsparams.min_screen_percentage = stitching_params.min_screen_percentage;
  int skiptop = top ? stitching_params.outer_tile_border : 2;
  int skipbottom = bottom ? stitching_params.outer_tile_border : 2;
  int skipleft = left ? stitching_params.outer_tile_border : 2;
  int skipright = right ? stitching_params.outer_tile_border : 2;
  dsparams.border_top = skiptop;
  dsparams.border_bottom = skipbottom;
  dsparams.border_left = skipleft;
  dsparams.border_right = skipright;
  dsparams.optimize_colors = stitching_params.optimize_colors;
  dsparams.slow_floodfill = stitching_params.slow_floodfill;
  dsparams.return_known_patches = true;
  detected = detect_regular_screen (*img, dparam, rparam.gamma, solver_param, &dsparams, progress, report_file);
  mesh_trans = detected.mesh_trans;
  if (!mesh_trans)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to analyze screen of %s\n", filename.c_str ());
      exit (1);
    }
  if (stitching_params.reoptimize_colors)
    {
      scr_detect_parameters optimized_dparam = dparam;
      optimize_screen_colors (&optimized_dparam, img, mesh_trans, detected.xshift, detected.yshift, detected.known_patches, rparam.gamma, progress, report_file);
      delete mesh_trans;
      delete detected.known_patches;
      detected = detect_regular_screen (*img, optimized_dparam, rparam.gamma, solver_param, &dsparams, progress, report_file);
      mesh_trans = detected.mesh_trans;
      if (!mesh_trans)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Failed to analyze screen of %s after optimizing screen colors. Probably a bug\n", filename.c_str ());
	  exit (1);
	}
    }


  gray_max = img->maxval;
  render_parameters my_rparam;
  my_rparam.gamma = rparam.gamma;
  my_rparam.precise = true;
  my_rparam.gray_max = img->maxval;
  my_rparam.screen_blur_radius = 0.5;
  my_rparam.mix_red = 0;
  my_rparam.mix_green = 0;
  my_rparam.mix_blue = 1;
  param.mesh_trans = mesh_trans;
  param.type = Dufay;
  render_to_scr render (param, *img, my_rparam, 256);
  render.precompute_all (true, progress);
  if (!initialized)
    {
      initialized = true;
      pixel_size = detected.pixel_size;
      my_screen = render_to_scr::get_screen (Dufay, false, detected.pixel_size * my_rparam.screen_blur_radius, progress);
    }
  scr_to_img_map.set_parameters (param, *img);
  basic_scr_to_img_map.set_parameters (detected.param, *img);
  final_xshift = render.get_final_xshift ();
  final_yshift = render.get_final_yshift ();
  final_width = render.get_final_width ();
  final_height = render.get_final_height ();

  scr_to_img_map.get_range (img->width, img->height, &xshift, &yshift, &width, &height);
  screen_detected_patches = new bitmap_2d (width, height);
  for (int y = 0; y < height; y++)
    if (y - yshift +  detected.yshift > 0 && y - yshift +  detected.yshift < detected.known_patches->height)
      for (int x = 0; x < width; x++)
        if (x - xshift +  detected.xshift > 0 && x - xshift +  detected.xshift < detected.known_patches->width
	    && detected.known_patches->test_bit (x - xshift + detected.xshift, y - yshift +  detected.yshift))
           screen_detected_patches->set_bit (x, y);
  delete detected.known_patches;
  detected.known_patches = NULL;
  dufay.analyze (&render, img, &scr_to_img_map, my_screen, width, height, xshift, yshift, true, 0.7, progress);
  if (stitching_params.max_contrast >= 0)
    dufay.analyze_contrast (&render, img, &scr_to_img_map, progress);
  dufay.set_known_pixels (compute_known_pixels (*img, scr_to_img_map, skiptop, skipbottom, skipleft, skipright, progress) /*screen_detected_patches*/);
  screen_filename = (std::string)"screen"+(std::string)filename;
  known_screen_filename = (std::string)"known_screen"+(std::string)filename;
  known_pixels = compute_known_pixels (*img, scr_to_img_map, 0, 0, 0, 0, progress);
  const char *error;
  if (stitching_params.screen_tiles && !dufay.write_screen (screen_filename.c_str (), NULL, &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", screen_filename.c_str (), error);
      exit (1);
    }
  if (stitching_params.known_screen_tiles && !dufay.write_screen (known_screen_filename.c_str (), screen_detected_patches, &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", known_screen_filename.c_str (), error);
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
  return known_pixels->test_range (ax, ay, 2);
}
bool
stitch_image::img_pixel_known_p (coord_t sx, coord_t sy)
{
  coord_t ix, iy;
  scr_to_img_map.to_img (sx - xpos, sy - ypos, &ix, &iy);
  return ix >= (left ? 0 : img->width * 0.02)
	 && iy >= (top ? 0 : img->height * 0.02)
	 && ix <= (right ? img->width : img->width * 0.98)
	 && iy <= (bottom ? img->height : img->height * 0.98);
}

bool
stitch_image::render_pixel (render_parameters & my_rparam, render_parameters &passthrough_rparam, coord_t sx, coord_t sy, render_mode mode, int *r, int *g, int *b, progress_info *progress)
{
  bool loaded = false;
  switch (mode)
    {
     case render_demosaiced:
      if (!render)
	{
	  load_img (progress);
	  render = new render_interpolate (param, *img, my_rparam, 65535, false, false);
	  render->precompute_all (progress);
	  release_img ();
	  loaded = true;
	}
      else
	lastused = ++current_time;
      //assert (pixel_known_p (sx, sy));
      render->render_pixel_scr (sx - xpos, sy - ypos, r, g, b);
      break;
     case render_original:
      if (!render2)
	{
	  load_img (progress);
	  render2 = new render_img (param, *img, passthrough_rparam, 65535);
	  render2->set_color_display ();
	  render2->precompute_all (progress);
	  release_img ();
	  loaded = true;
	}
      else
	lastused = ++current_time;
      //assert (pixel_known_p (sx, sy));
      render2->render_pixel (sx - xpos, sy - ypos, r, g, b);
      break;
     case render_predictive:
      if (!render3)
	{
	  load_img (progress);
	  render3 = new render_interpolate (param, *img, my_rparam, 65535, true, false);
	  render3->precompute_all (progress);
	  release_img ();
	  loaded = true;
	}
      else
	lastused = ++current_time;
      //assert (pixel_known_p (sx, sy));
      render3->render_pixel_scr (sx - xpos, sy - ypos, r, g, b);
      break;
    }
  return loaded;
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

void
stitch_image::compare_contrast_with (stitch_image &other, progress_info *progress)
{
  int x1, y1, x2, y2;
  int xs = other.xpos - xpos;
  int ys = other.ypos - ypos;
  if (stitching_params.max_contrast < 0)
    return;
  luminosity_t ratio = dufay.compare_contrast (other.dufay, xs, ys, &x1, &y1, &x2, &y2, scr_to_img_map, other.scr_to_img_map, progress);
  if (ratio < 0)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Failed to compare contrast ratio of %s and %s\n", filename.c_str (), other.filename.c_str ());
      if (progress)
	progress->resume_stdout ();
      return;
    }
  if (report_file)
    fprintf (report_file, "Contrast difference of %s and %s: %f%%\n", filename.c_str (), other.filename.c_str (), (ratio - 1) * 100);
  if ((ratio - 1) * 100 < stitching_params.max_contrast)
    return;
  if (progress)
    progress->pause_stdout ();
  printf ("Out of threshold contrast difference of %s and %s: %f%%\n", filename.c_str (), other.filename.c_str (), (ratio - 1) * 100);
  if (progress)
    progress->resume_stdout ();

  int range = 400;
  //TODO
  char buf[4096];
  sprintf (buf, "contrast-%03i-%s-%s",(int)((ratio -1) * 100 + 0.5), filename.c_str(), other.filename.c_str());

  load_img (progress);
  other.load_img (progress);
  progress->pause_stdout ();
  printf ("Saving contrast diff %s\n", buf);
  progress->resume_stdout ();

  TIFF *out = TIFFOpen (buf, "wb");
  double dpi = 300;
  if (!out)
    {
      //*error = "can not open output file";
      return;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, range*3)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, range)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_XRESOLUTION, dpi)
      || !TIFFSetField (out, TIFFTAG_YRESOLUTION, dpi)
      || (img->icc_profile && !TIFFSetField (out, TIFFTAG_ICCPROFILE, img->icc_profile_size, img->icc_profile)))
    {
      //*error = "write error";
      return;
    }
  uint16_t *outrow = (uint16_t *) malloc (range * 6 * 3);
  if (!outrow)
    {
      //*error = "Out of memory allocating output buffer";
      return;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving contrast info", range);
    }
  for (int y = 0; y < range; y++)
    {
      for (int x =0 ; x < range; x++)
        {
	  int yy = y + y1 - range / 2;
	  int xx = x + x1 - range / 2;
	  if (yy >= 0 && yy < img->height && xx >= 0 && xx <= img->width)
	    {
	      outrow[x*3] = img->rgbdata[yy][xx].r;
	      outrow[x*3+1] = img->rgbdata[yy][xx].g;
	      outrow[x*3+2] = img->rgbdata[yy][xx].b;
	      if (x < range/2)
		{
		  outrow[range * 6 + x*3] = img->rgbdata[yy][xx].r;
		  outrow[range * 6 + x*3+1] = img->rgbdata[yy][xx].g;
		  outrow[range * 6 + x*3+2] = img->rgbdata[yy][xx].b;
		}
	    }
	  else
	    {
	      outrow[x*3] = 0;
	      outrow[x*3+1] = 0;
	      outrow[x*3+2] = 0;
	      if (x < range / 2)
		{
		  outrow[range * 6 + x*3] = 0;
		  outrow[range * 6 + x*3+1] = 0;
		  outrow[range * 6 + x*3+2] = 0;
		}
	    }
	  yy = y + y2 - range / 2;
	  xx = x + x2 - range / 2;
	  if (yy >= 0 && yy < other.img->height && xx >= 0 && xx <= other.img->width)
	    {
	      outrow[range * 3 + x*3] = other.img->rgbdata[yy][xx].r;
	      outrow[range * 3 + x*3+1] = other.img->rgbdata[yy][xx].g;
	      outrow[range * 3 + x*3+2] = other.img->rgbdata[yy][xx].b;
	      if (x >= range/2)
		{
		  outrow[range * 6 + x*3] = other.img->rgbdata[yy][xx].r;
		  outrow[range * 6 + x*3+1] = other.img->rgbdata[yy][xx].g;
		  outrow[range * 6 + x*3+2] = other.img->rgbdata[yy][xx].b;
		}
	    }
	  else
	    {
	      outrow[range * 3 + x*3] = 0;
	      outrow[range * 3 + x*3+1] = 0;
	      outrow[range * 3 + x*3+2] = 0;
	      if (x >= range/2)
		{
		  outrow[range * 6 + x*3] = 0;
		  outrow[range * 6 + x*3+1] = 0;
		  outrow[range * 6 + x*3+2] = 0;
		}
	    }
        }
      const char *error;
      if (!write_row (out, y, outrow, &error, progress))
        {
	  free (outrow);
	  TIFFClose (out);
	  return;
        }
    }
  release_img ();
  other.release_img ();
  free (outrow);
  TIFFClose (out);
}
void
stitch_image::write_stitch_info (progress_info *progress)
{
  if (progress)
    progress->pause_stdout ();
  printf ("Writting geometry info for %s\n", filename.c_str ());
  if (progress)
    progress->resume_stdout ();

  std::string prefix = "geometry-";
  TIFF *out = TIFFOpen ((prefix + filename).c_str (), "wb");
  if (!out)
    {
      //*error = "can not open output file";
      return;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img_width / 10 + 1)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, img_height / 10 + 1)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB))
    {
      //*error = "write error";
      return;
    }
  uint16_t *outrow = (uint16_t *) malloc ((img_width / 10 + 1) * 2 * 3);
  if (!outrow)
    {
      //*error = "Out of memory allocating output buffer";
      return;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving geometry info", img_height / 10 + 1);
    }
  for (int y = 0; y < img_height / 10 + 1; y++)
    {
      for (int x =0 ; x < img_width / 10 + 1; x++)
        {
	  struct stitch_info &i = stitch_info[y * (img_width / 10 + 1) + x];
	  if (!i.sum)
	    {
	      outrow[x*3] = 0;
	      outrow[x*3+1] = 0;
	      outrow[x*3+2] = 65535;
	    }
	  else
	    {
	      outrow[x*3] = std::min ((i.x / i.sum) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+1] = std::min ((i.y / i.sum) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+2] = 0;
	    }
        }
      const char *error;
      if (!write_row (out, y, outrow, &error, progress))
        {
	  free (outrow);
	  TIFFClose (out);
	  return;
        }
    }
  free (outrow);
  TIFFClose (out);
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
  if (!outrow)
    {
      *error = "Out of memory allocating output buffer";
      return NULL;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving", outheight);
    }
  if (report_file)
    fprintf (report_file, "Rendering %s at offset %i,%i width dimension %ix%i\n", outfname, xoffset, yoffset, outwidth, outheight);
  progress->pause_stdout ();
  printf ("Rendering %s at offset %i,%i width dimension %ix%i\n", outfname, xoffset, yoffset, outwidth, outheight);
  progress->resume_stdout ();
  return out;
}

bool
stitch_image::write_tile (const char **error, scr_to_img &map, int stitch_xmin, int stitch_ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress)
{
  std::string prefix;
  uint16_t *outrow;
  coord_t final_xpos, final_ypos;
  map.scr_to_final (xpos, ypos, &final_xpos, &final_ypos);
  int xmin = floor ((final_xpos - final_xshift) / xstep) * xstep;
  int ymin = floor ((final_ypos - final_yshift) / ystep) * ystep;

  switch(mode)
  {
  case render_demosaiced:
    prefix = "demosaicedtile-";
    break;
  case render_original:
    prefix = "tile-";
    break;
  case render_predictive:
    prefix = "predictivetile-";
    break;
  }

  load_img (progress);
  TIFF *out = open_tile_output_file ((prefix+filename).c_str(), (xmin - stitch_xmin) / xstep, (ymin - stitch_ymin) / ystep, final_width / xstep, final_height / ystep, &outrow, error, img->icc_profile, img->icc_profile_size, mode, progress);
  if (!out)
    {
      release_img ();
      return false;
    }
  int j = 0;
  for (coord_t y = ymin; j < (int)(final_height / ystep); y+=ystep, j++)
    {
      int i = 0;
      for (coord_t x = xmin; i < (int)(final_width / xstep); x+=xstep, i++)
	{
	  coord_t sx, sy;
	  int r = 0,g = 0,b = 0;
	  map.final_to_scr (x, y, &sx, &sy);
	  if (mode == render_original /*&& 0*/? img_pixel_known_p (sx, sy) : pixel_known_p (sx, sy))
	    {
	      if (render_pixel (rparam, passthrough_rparam, sx, sy, mode,&r,&g,&b, progress)
		  && progress)
		{
		  progress->set_task ("Rendering and saving tile", (final_height / ystep));
		  progress->set_progress (j);
		}
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
  printf ("  --max-contrast=precentage                   report differences in contrast over this threshold\n");
  printf ("  --max-avx-dstance=npixels                   maximal average distance of real screen patches to estimated ones via affine transform\n");
  printf ("  --max-max-dstance=npixels                   maximal maximal distance of real screen patches to estimated ones via affine transform\n");
  printf (" hugin output:\n");
  printf ("  --num-control-points=n                      number of control points for each pair of images\n");
  printf (" other:\n");
  printf ("  --panorama-map                              print panorama map in ascii-art\n");
  printf ("  --min-screen-precentage                     minimum portion of screen required to be recognized by screen detection\n");
  printf ("  --optimize-colors                           auto-optimize screen colors (default)\n");
  printf ("  --no-optimize-colors                        do not auto-optimize screen colors\n");
  printf ("  --reoptimize-colors                         auto-optimize screen colors after initial screen analysis\n");
  printf ("  --slow-floodfill                            use slower but hopefully more precise discovery of patches\n");
  printf ("  --no-limit-directions                       do not limit overlap checking to expected directions\n");
}

void
determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax)
{
  xmin = 0;
  ymin = 0;
  xmax = 0;
  ymax = 0;
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      if (images[y][x].analyzed)
	{
	  coord_t x1,y1,x2,y2;
	  coord_t rxpos, rypos;
	  common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rxpos, &rypos);
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
	  common_scr_to_img.final_to_scr (fx, fy, &sx, &sy);
	  for (iy = 0 ; iy < stitching_params.height; iy++)
	    {
	      for (ix = 0 ; ix < stitching_params.width; ix++)
		if (images[iy][ix].analyzed && images[iy][ix].pixel_known_p (sx, sy))
		  break;
	      if (ix != stitching_params.width)
		break;
	    }

#if 0
	  if (iy == stitching_params.height)
	    fprintf (out, "   ");
	  else
	    fprintf (out, " %i%i",iy+1,ix+1);
#endif
	  if (iy == stitching_params.height)
	    fprintf (out, " ");
	  else
	    fprintf (out, "%c",'a'+ix+iy*stitching_params.width);
	}
      fprintf (out, "\n");
    }
}

void
print_status (FILE *out)
{
  for (int y = 0; y < stitching_params.height; y++)
    {
      if (y)
	{
	  coord_t rx, ry;
	  common_scr_to_img.scr_to_final (images[y-1][0].xpos, images[y-1][0].ypos, &rx, &ry);
	  coord_t rx2, ry2;
	  common_scr_to_img.scr_to_final (images[y][0].xpos, images[y][0].ypos, &rx2, &ry2);
	  rx -= images[y-1][0].xshift;
	  ry -= images[y-1][0].yshift;
	  rx2 -= images[y][0].xshift;
	  ry2 -= images[y][0].yshift;
	  fprintf (out, " down %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	}
      else fprintf (out, "                  ");
      for (int x = 1; x < stitching_params.width; x++)
      {
	coord_t rx, ry;
	common_scr_to_img.scr_to_final (images[y][x-1].xpos, images[y][x-1].ypos, &rx, &ry);
	coord_t rx2, ry2;
	common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx2, &ry2);
	rx -= images[y][x-1].xshift;
	ry -= images[y][x-1].yshift;
	rx2 -= images[y][x].xshift;
	ry2 -= images[y][x].yshift;
	fprintf (out, " right %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	//printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      fprintf (out, "\n");
    }
  for (int y = 0; y < stitching_params.height; y++)
    {
      for (int x = 0; x < stitching_params.width; x++)
      {
	coord_t rx, ry;
	common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx, &ry);
	fprintf (out, "  %-5i,%-5i  rotated:%-5i,%-5i ", images[y][x].xpos, images[y][x].ypos, (int)rx,(int)ry);
      }
      fprintf (out, "\n");
    }
  print_panorama_map (out);
}

void
analyze (int x, int y, progress_info *progress)
{
  images[y][x].analyze (!y, y == stitching_params.height - 1, !x, x == stitching_params.width - 1, progress);
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
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
     if (!y && !x)
       fprintf (f, "i w%i h%i f0 v%f Ra0 Rb0 Rc0 Rd0 Re0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy0 Tpp0 j0 a0 b0 c0 d0 e0 g0 t0 Va1 Vb0 Vc0 Vd0 Vx0 Vy0  Vm5 n\"%s\"\n", images[y][x].img_width, images[y][x].img_height, stitching_params.hfov, images[y][x].filename.c_str ());
     else
       fprintf (f, "i w%i h%i f0 v=0 Ra=0 Rb=0 Rc=0 Rd=0 Re=0 Eev0 Er1 Eb1 r0 p0 y0 TrX0 TrY0 TrZ0 Tpy-0 Tpp0 j0 a=0 b=0 c=0 d=0 e=0 g=0 t=0 Va=1 Vb=0 Vc=0 Vd=0 Vx=0 Vy=0  Vm5 n\"%s\"\n", images[y][x].img_width, images[y][x].img_height, images[y][x].filename.c_str ());
  fprintf (f, "# specify variables that should be optimized\n"
	   "v Ra0\n"
	   "v Rb0\n"
	   "v Rc0\n"
	   "v Rd0\n"
	   "v Re0\n"
	   "v Vb0\n"
	   "v Vc0\n"
	   "v Vd0\n");
  for (int i = 1; i < stitching_params.width * stitching_params.height; i++)
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
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      {
	if (x >= 1)
	  images[y][x-1].output_common_points (f, images[y][x], y * stitching_params.width + x - 1, y * stitching_params.width + x);
	if (y >= 1)
	  images[y-1][x].output_common_points (f, images[y][x], (y - 1) * stitching_params.width + x, y * stitching_params.width + x);
      }
#endif
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      for (int y2 = 0; y2 < stitching_params.height; y2++)
        for (int x2 = 0; x2 < stitching_params.width; x2++)
	  if ((x != x2 || y != y2) && (y < y2 || (y == y2 && x < x2)))
	    images[y][x].output_common_points (f, images[y2][x2], y * stitching_params.width + x, y2 * stitching_params.width + x2, false, progress);
  fclose (f);
}

void
determine_positions (progress_info *progress)
{
  if (stitching_params.width == 1 && stitching_params.height == 1)
    {
      analyze (0, 0, progress);
      return;
    }
  for (int y = 0; y < stitching_params.height; y++)
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
	  if (!images[y-1][0].dufay.find_best_match (stitching_params.min_overlap_percentage, stitching_params.max_overlap_percentage, images[y][0].dufay, stitching_params.cpfind, &xs, &ys, stitching_params.limit_directions ? 1 : -1, images[y-1][0].basic_scr_to_img_map, images[y][0].basic_scr_to_img_map, report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y-1][0].filename.c_str (), images[y][0].filename.c_str ());
	      exit (1);
	    }
	  images[y][0].xpos = images[y-1][0].xpos + xs;
	  images[y][0].ypos = images[y-1][0].ypos + ys;
	  images[y-1][0].compare_contrast_with (images[y][0], progress);
	  images[y-1][0].output_common_points (NULL, images[y][0], 0, 0, true, progress);
	  if (stitching_params.width)
	    {
	      images[y-1][1].compare_contrast_with (images[y][0], progress);
	      images[y-1][1].output_common_points (NULL, images[y][0], 0, 0, true, progress);
	    }
	  if (stitching_params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	}
      for (int x = 0; x < stitching_params.width - 1; x++)
	{
	  int xs;
	  int ys;
	  analyze (x, y, progress);
	  analyze (x + 1,y, progress);
	  if (!images[y][x].dufay.find_best_match (stitching_params.min_overlap_percentage, stitching_params.max_overlap_percentage, images[y][x+1].dufay, stitching_params.cpfind, &xs, &ys, stitching_params.limit_directions ? 0 : -1, images[y][x].basic_scr_to_img_map, images[y][x+1].basic_scr_to_img_map, report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename.c_str (), images[y][x + 1].filename.c_str ());
	      if (report_file)
		print_status (report_file);
	      exit (1);
	    }
	  images[y][x+1].xpos = images[y][x].xpos + xs;
	  images[y][x+1].ypos = images[y][x].ypos + ys;
	  if (stitching_params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!images[y-1][x+1].dufay.find_best_match (stitching_params.min_overlap_percentage, stitching_params.max_overlap_percentage, images[y][x+1].dufay, stitching_params.cpfind, &xs, &ys, stitching_params.limit_directions ? 1 : -1, images[y-1][x+1].basic_scr_to_img_map, images[y][x+1].basic_scr_to_img_map, report_file, progress))
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename.c_str (), images[y][x + 1].filename.c_str ());
		  if (report_file)
		    print_status (report_file);
		  exit (1);
		}
	      if (images[y][x+1].xpos != images[y-1][x+1].xpos + xs
		  || images[y][x+1].ypos != images[y-1][x+1].ypos + ys)
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Stitching mismatch in %s: %i,%i is not equal to %i,%i\n", images[y][x + 1].filename.c_str (), images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		  if (report_file)
		  {
		    fprintf (report_file, "Stitching mismatch in %s: %i,%i is not equal to %i,%i\n", images[y][x + 1].filename.c_str (), images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		    print_status (report_file);
		  }
		  exit (1);
		}

	    }
	  if (y)
	    {
	      images[y-1][x+1].compare_contrast_with (images[y][x], progress);
	      images[y-1][x+1].output_common_points (NULL, images[y][x], 0, 0, true, progress);
	      images[y-1][x+1].compare_contrast_with (images[y][x+1], progress);
	      images[y-1][x+1].output_common_points (NULL, images[y][x+1], 0, 0, true, progress);
	      if (x + 2 < stitching_params.width)
	      {
	         images[y-1][x+1].compare_contrast_with (images[y][x+2], progress);
	         images[y-1][x+1].output_common_points (NULL, images[y][x+2], 0, 0, true, progress);
	      }
	    }
	  images[y][x].compare_contrast_with (images[y][x+1], progress);
          images[y][x].output_common_points (NULL, images[y][x+1], 0, 0, true, progress);
	  if (report_file)
	    fflush (report_file);
	}
    }
  if (report_file)
    print_status (report_file);
  if (stitching_params.panorama_map)
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
  if (report_file)
    fprintf (report_file, "Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->pause_stdout ();
  printf ("Rendering %s in resolution %ix%i\n", outfname, outwidth, outheight);
  progress->resume_stdout ();
  return out;
}

void stitch (progress_info *progress)
{
  passthrough_rparam.gamma = rparam.gamma;
  if (stitching_params.orig_tile_gamma > 0)
    passthrough_rparam.output_gamma = stitching_params.orig_tile_gamma;
  else
    passthrough_rparam.output_gamma = rparam.gamma;
  const char *error;

  {
    scr_to_img_parameters scr_param;
    image_data data;
    scr_param.type = Dufay;
    data.width=1000;
    data.height=1000;
    common_scr_to_img.set_parameters (scr_param, data);
  }

  if ((stitching_params.width == 1 || stitching_params.height == 1) && stitching_params.outer_tile_border > 40)
    {
      fprintf (stderr, "Outer tile border is too large for single row or column stitching\n");
      exit (1);
    }
  if (stitching_params.outer_tile_border > 80)
    {
      fprintf (stderr, "Outer tile border is too large\n");
      exit (1);
    }
  if (stitching_params.report_filename.length ())
    {
      report_file = fopen (stitching_params.report_filename.c_str (), "wt");
      if (!report_file)
	{
	  fprintf (stderr, "Can not open report file: %s\n", stitching_params.report_filename.c_str ());
	  exit (1);
	}
    }
  progress->pause_stdout ();
  printf ("Stitching:\n");
  if (report_file)
    fprintf (report_file, "Stitching:\n");
  for (int y = 0; y < stitching_params.height; y++)
    {
      for (int x = 0; x < stitching_params.width; x++)
	{
	  printf ("  %s", images[y][x].filename.c_str ());
	  if (report_file)
	    fprintf (report_file, "  %s", images[y][x].filename.c_str ());
	}
      printf("\n");
      if (report_file)
        fprintf (report_file, "\n");
    }
  progress->resume_stdout ();

  if (stitching_params.csp_filename.length ())
    {
      const char *cspname = stitching_params.csp_filename.c_str ();
      FILE *in = fopen (cspname, "rt");
      progress->pause_stdout ();
      printf ("Loading color screen parameters: %s\n", cspname);
      progress->resume_stdout ();
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
      solver_param.remove_points ();
    }

  if (report_file)
    {
      fprintf (report_file, "Color screen parameters:\n");
      save_csp (report_file, &param, &dparam, &rparam, &solver_param);
    }
  determine_positions (progress);

  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      images[y][x].write_stitch_info (progress);

  const coord_t xstep = pixel_size, ystep = pixel_size;
  passthrough_rparam.gray_max = images[0][0].gray_max;
  if (stitching_params.hugin_pto_filename.length ())
    produce_hugin_pto_file (stitching_params.hugin_pto_filename.c_str (), progress);
  if (stitching_params.produce_stitched_file_p ())
    {
      TIFF *out;
      uint16_t *outrow;
      images[0][0].load_img (progress);
      out =
	open_output_file (stitching_params.stitched_filename.c_str (), (xmax-xmin) / xstep, (ymax-ymin) / ystep, &outrow, 
			  &error,
			  images[0][0].img->icc_profile, images[0][0].img->icc_profile_size,
			  progress);
      images[0][0].release_img ();
      int j = 0;
      if (!out)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Can not open final stitch file %s: %s\n", stitching_params.stitched_filename.c_str (), error);
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
	      common_scr_to_img.final_to_scr (x, y, &sx, &sy);
	      for (iy = 0 ; iy < stitching_params.height; iy++)
		{
		  for (ix = 0 ; ix < stitching_params.width; ix++)
		    if (images[iy][ix].analyzed && images[iy][ix].pixel_known_p (sx, sy))
		      break;
		  if (ix != stitching_params.width)
		    break;
		}
	      if (iy != stitching_params.height)
		{
		  if (images[iy][ix].render_pixel (rparam, passthrough_rparam, sx,sy, render_original,&r,&g,&b, progress))
		    set_p = true;
		  if (!images[iy][ix].output)
		    {
		      if ((stitching_params.orig_tiles && !images[iy][ix].write_tile (&error, common_scr_to_img, xmin, ymin, xstep, ystep, render_original, progress))
			  || (stitching_params.demosaiced_tiles && !images[iy][ix].write_tile (&error, common_scr_to_img, xmin, ymin, 1, 1, render_demosaiced, progress))
			  || (stitching_params.predictive_tiles && !images[iy][ix].write_tile (&error, common_scr_to_img, xmin, ymin, xstep, ystep, render_predictive, progress)))
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
    for (int y = 0; y < stitching_params.height; y++)
      for (int x = 0; x < stitching_params.width; x++)
	if ((stitching_params.orig_tiles && !images[y][x].write_tile (&error, common_scr_to_img, xmin, ymin, xstep, ystep, render_original, progress))
	    || (stitching_params.demosaiced_tiles && !images[y][x].write_tile (&error, common_scr_to_img, xmin, ymin, 1, 1, render_demosaiced, progress))
	    || (stitching_params.predictive_tiles && !images[y][x].write_tile (&error, common_scr_to_img, xmin, ymin, xstep, ystep, render_predictive, progress)))
	  {
	    fprintf (stderr, "Writting tile: %s\n", error);
	    exit (1);
	  }
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      if (images[y][x].img)
	images[y][x].release_image_data (progress);
  if (my_screen)
    render_to_scr::release_screen (my_screen);
  if (report_file)
    fclose (report_file);
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
	  stitching_params.report_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--report=", strlen ("--report=")))
	{
	  stitching_params.report_filename = argv[i] + strlen ("--report=");
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
	  stitching_params.csp_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--csp=", strlen ("--csp=")))
	{
	  stitching_params.csp_filename = argv[i] + strlen ("--csp=");
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
	  stitching_params.hugin_pto_filename = argv[i];
	  continue;
	}
      if (!strncmp (argv[i], "--hugin-pto=", strlen ("--hugin-pto=")))
	{
	  stitching_params.hugin_pto_filename = argv[i] + strlen ("--hugin-pto=");
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
	  stitching_params.stitched_filename = argv[i];
	  continue;
	}
      if (!strcmp (argv[i], "--no-cpfind"))
	{
	  stitching_params.cpfind = 0;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind"))
	{
	  stitching_params.cpfind = 1;
	  continue;
	}
      if (!strcmp (argv[i], "--cpfind-verification"))
	{
	  stitching_params.cpfind = 2;
	  continue;
	}
      if (!strncmp (argv[i], "--stitched=", strlen ("--stitched=")))
	{
	  stitching_params.stitched_filename = argv[i] + strlen ("--stitched=");
	  continue;
	}
      if (!strcmp (argv[i], "--demosaiced-tiles"))
	{
	  stitching_params.demosaiced_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--predictive-tiles"))
	{
	  stitching_params.predictive_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--orig-tiles"))
	{
	  stitching_params.orig_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--screen-tiles"))
	{
	  stitching_params.screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--known-screen-tiles"))
	{
	  stitching_params.known_screen_tiles = true;
	  continue;
	}
      if (!strcmp (argv[i], "--panorama-map"))
	{
	  stitching_params.panorama_map = true;
	  continue;
	}
      if (!strcmp (argv[i], "--optimize-colors"))
	{
	  stitching_params.optimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-optimize-colors"))
	{
	  stitching_params.optimize_colors = false;
	  continue;
	}
      if (!strcmp (argv[i], "--reoptimize-colors"))
	{
	  stitching_params.reoptimize_colors = true;
	  continue;
	}
      if (!strcmp (argv[i], "--no-limit-directions"))
	{
	  stitching_params.limit_directions = false;
	  continue;
	}
      if (!strcmp (argv[i], "--slow-floodfill"))
	{
	  stitching_params.slow_floodfill = true;
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
	  stitching_params.outer_tile_border = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--outer-tile-border=", strlen ("--outer-tile-border=")))
	{
	  stitching_params.outer_tile_border = atoi (argv[i] + strlen ("--outer-tile-border="));
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
	  stitching_params.max_contrast = atoi (argv[i]);
	  continue;
	}
      if (!strncmp (argv[i], "--max-contrast=", strlen ("--max-contrast=")))
	{
	  stitching_params.max_contrast = atoi (argv[i] + strlen ("--max-contrast="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-overlap=", strlen ("--min-overlap=")))
	{
	  stitching_params.min_overlap_percentage = atoi (argv[i] + strlen ("--min-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-overlap=", strlen ("--max-overlap=")))
	{
	  stitching_params.max_overlap_percentage = atoi (argv[i] + strlen ("--max-overlap="));
	  continue;
	}
      if (!strncmp (argv[i], "--ncols=", strlen ("--ncols=")))
	{
	  ncols = atoi (argv[i] + strlen ("--ncols="));
	  continue;
	}
      if (!strncmp (argv[i], "--num-control-points=", strlen ("--num-control-points=")))
	{
	  stitching_params.num_control_points = atoi (argv[i] + strlen ("--num-control-points="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  stitching_params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--orig-tile-gamma=", strlen ("--orig-tile-gamma=")))
	{
	  stitching_params.orig_tile_gamma = atof (argv[i] + strlen ("--orig-tile-gamma="));
	  continue;
	}
      if (!strncmp (argv[i], "--min-screen-percentage=", strlen ("--min-screen-percentage=")))
	{
	  stitching_params.min_screen_percentage = atoi (argv[i] + strlen ("--min-screen-percentage="));
	  continue;
	}
      if (!strncmp (argv[i], "--hfov=", strlen ("--hfove=")))
	{
	  stitching_params.hfov = atof (argv[i] + strlen ("--hfov="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-avg-distance=", strlen ("--max-avg-distancee=")))
	{
	  stitching_params.max_avg_distance = atof (argv[i] + strlen ("--max-avg-distance="));
	  continue;
	}
      if (!strncmp (argv[i], "--max-max-distance=", strlen ("--max-max-distancee=")))
	{
	  stitching_params.max_max_distance = atof (argv[i] + strlen ("--max-max-distance="));
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
      stitching_params.width = 1;
      stitching_params.height = 1;
   
   }
  if (ncols > 0)
    {
      stitching_params.width = ncols;
      stitching_params.height = fnames.size () / ncols;
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
      stitching_params.width = w;
      stitching_params.height = fnames.size () / w;
      for (int y = 0; y < stitching_params.height; y++)
        for (int x = 0; x < stitching_params.width; x++)
	  {
	    int i = y * stitching_params.width + x;
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
  if (stitching_params.width * stitching_params.height != (int)fnames.size ())
    {
      fprintf (stderr, "For %ix%i tiles I expect %i filenames, found %i\n", stitching_params.width, stitching_params.height, stitching_params.width * stitching_params.height, (int)fnames.size ());
      print_help (argv[0]);
      exit (1);
    }
  for (int y = 0; y < stitching_params.height; y++)
    for (int x = 0; x < stitching_params.width; x++)
      images[y][x].filename = fnames[y * stitching_params.width + x];

  stitch (&progress);


  return 0;
}
