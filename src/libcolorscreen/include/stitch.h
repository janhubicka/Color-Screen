#ifndef STITCH_H
#define STITCH_H
#include <pthread.h>
#include <string>
#include "scr-to-img.h"
#include "render.h"
#include "solver.h"
#include "colorscreen.h"
struct tiff;
typedef struct tiff TIFF;

struct stitching_params
{
  static const int max_dim = 10;

  enum scr_type type;

  bool screen_tiles;
  bool known_screen_tiles;
  int cpfind;
  bool panorama_map;
  bool optimize_colors;
  bool reoptimize_colors;
  bool slow_floodfill;
  bool fast_floodfill;
  bool limit_directions;
  bool mesh_trans;
  bool geometry_info;
  bool individual_geometry_info;
  bool outliers_info;
  bool diffs;
  bool load_registration;

  int outer_tile_border;
  int inner_tile_border;
  int min_overlap_percentage;
  int max_overlap_percentage;
  int max_unknown_screen_range;
  luminosity_t max_contrast;
  luminosity_t min_patch_contrast;

  int num_control_points;
  int min_screen_percentage;
  coord_t hfov;
  coord_t max_avg_distance;
  coord_t max_max_distance;
  coord_t scan_xdpi, scan_ydpi;

  int width, height;
  /* Path to a directory where the stitch project is located.  */
  std::string path;
  std::string filename[max_dim][max_dim];
  std::string csp_filename;
  std::string hugin_pto_filename;
  std::string report_filename;


  stitching_params ()
  : type (Dufay), screen_tiles (false), known_screen_tiles (false),
    cpfind (true), panorama_map (false), optimize_colors (true), reoptimize_colors (false), slow_floodfill (true), fast_floodfill (true), limit_directions (false), mesh_trans (true),
    geometry_info (false), individual_geometry_info (false), outliers_info (false), diffs (false), load_registration (false),
    outer_tile_border (30), inner_tile_border (10), min_overlap_percentage (10), max_overlap_percentage (65), max_unknown_screen_range (100), max_contrast (-1), min_patch_contrast (-1), num_control_points (100), min_screen_percentage (75), hfov (28.534),
    max_avg_distance (2), max_max_distance (10), scan_xdpi (0), scan_ydpi (0), width (0), height (0), path("")
  {
  }
};

class stitch_project;
class analyze_base;

class stitch_image
{
  public:
  std::string filename;
  std::string screen_filename;
  std::string known_screen_filename;
  std::unique_ptr<image_data> img;
  std::unique_ptr<mesh> mesh_trans;
  scr_to_img_parameters param;
  /* scr_to_img map holding mesh_trans.  */
  scr_to_img scr_to_img_map;
  /* scr_to_img map holding detected parameters.  */
  scr_to_img basic_scr_to_img_map;
  int img_width, img_height;
  int xshift, yshift, width, height;
  int final_xshift, final_yshift;
  int final_width, final_height;
  std::unique_ptr <analyze_base> analyzer;
  /* Screen patches that was detected by screen detection algorithm.  */
  std::unique_ptr<bitmap_2d> screen_detected_patches;
  /* Known pixels used by stitching algorithm.  This is basically the image without borders.  */
  std::unique_ptr<bitmap_2d> known_pixels;

  detected_screen detected;

  /* Screen angle and ratio.  Used in Dufaycolor analysis since
     the Dufaycolor screens are printed with different ratios and angles
     of the horisontal and vertical lines.

     Computed at analysis time and used in final output.  */
  coord_t angle, ratio;

  struct stitch_info {coord_t x,y;
    		      int sum;} *stitch_info;

  /* Position in the stitched image.  Determined during analysis.  */
  coord_t xpos, ypos;
  bool analyzed;
  bool output;
  /* Gray max of the original data..  */
  int gray_max;

  bool top, bottom, left, right;

  DLL_PUBLIC stitch_image ();
  DLL_PUBLIC_EXP ~stitch_image ();
  bool init_loader (const char **error, progress_info *progress);
  bool load_img (const char **error, progress_info *);
  bool load_part (int *permille, const char **error, progress_info *progress);
  void release_img ();
  void update_scr_to_final_parameters (coord_t ratio, coord_t anlge);
  bool analyze (stitch_project *prj, bool top_p, bool bottom_p, bool left_p, bool right_p, lens_warp_correction_parameters &lens_correction, progress_info *);
  void release_image_data (progress_info *);
  bitmap_2d *compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress);
  int output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress = NULL);
  bool pixel_known_p (coord_t sx, coord_t sy);
  bool img_pixel_known_p (coord_t sx, coord_t sy);
  bool patch_detected_p (int sx, int sy);
  bool write_tile (render_parameters rparam, int stitch_xmin, int stitch_ymin, render_to_file_params rfparams, render_type_parameters &rtparam, const char **error, progress_info *progress);
  void compare_contrast_with (stitch_image &other, progress_info *progress);
  void write_stitch_info (progress_info *progress, int x = -1, int y = -1, int xx = -1, int yy = -1);
  void clear_stitch_info ();
  bool diff (stitch_image &other, progress_info *progress);
  bool save (FILE *f);
  bool load (stitch_project *, FILE *f, const char **error);

  inline analyze_base & get_analyzer ();
  static bool write_row (TIFF * out, int y, uint16_t * outrow, const char **error, progress_info *progress);
  bool pixel_maybe_in_range_p (coord_t sx, coord_t sy)
  {
    coord_t ax = sx + xshift - xpos;
    if (ax < 0 || ax >= width)
      return false;
    coord_t ay = sy + yshift - ypos;
    if (ay < 0 || ay >= height)
      return false;
    return scr_to_img_map.to_img_in_mesh_range (sx - xpos, sy - ypos);
  }
  void img_to_common_scr (coord_t ix, coord_t iy, coord_t *sx, coord_t *sy)
  {
    coord_t xx, yy;
    scr_to_img_map.to_scr (ix, iy, &xx, &yy);
    *sx = xx + xpos;
    *sy = yy + ypos;
  }
  void img_scr_to_common_scr (coord_t tsx, coord_t tsy, coord_t *sx, coord_t *sy)
  {
    *sx = tsx + xpos;
    *sy = tsy + ypos;
  }
  void common_scr_to_img_scr (coord_t sx, coord_t sy, coord_t *isx, coord_t *isy)
  {
    *isx = sx - xpos;
    *isy = sy - ypos;
  }
  void common_scr_to_img (coord_t sx, coord_t sy, coord_t *ix, coord_t *iy)
  {
    scr_to_img_map.to_img (sx - xpos, sy - ypos, ix, iy);
  }
  struct common_sample
  {
    coord_t x1, y1, x2, y2;
    luminosity_t channel1[4];
    luminosity_t channel2[4];
    luminosity_t weight;
  };
  typedef std::vector<common_sample> common_samples;
  common_samples find_common_points (stitch_image &other, int outerborder, int innerborder, render_parameters &rparams, progress_info *progress, const char **error);
private:
  bool render_pixel (int maxval, coord_t sx, coord_t sy, int *r, int *g, int *b, progress_info *p);
  static uint64_t current_time;
  static int nloaded;
  uint64_t lastused;
  int refcount;
  stitch_project *m_prj;
};

class stitch_project
{
public:
  struct stitching_params params;
  FILE *report_file;
  stitch_image images[stitching_params::max_dim][stitching_params::max_dim];
  /* Expected to by initialized by user and used during analysis stage only
     to pass lens correction.  */
  scr_to_img_parameters param;
  /* Rendering parameters. Expected to be initialized by user.  */
  render_parameters rparam;
  /* Used to output final image. Initialized by determine_angle.  */
  scr_to_img common_scr_to_img;
  /* Screen detection parameters used at analysis stage only.  */
  scr_detect_parameters dparam;
  /* Solver parameters used to analyze images.  Needed in analysis stage only.  */
  solver_parameters solver_param;
  /* Used to determine output file size.  */
  coord_t pixel_size;
  DLL_PUBLIC stitch_project ();
  DLL_PUBLIC ~stitch_project ();
  bool initialize();
  void determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax);
  void determine_angle ();
  DLL_PUBLIC bool save (FILE *f);
  DLL_PUBLIC bool load (FILE *f, const char **error);
  std::string adjusted_filename (std::string filename, std::string suffix, std::string extension, int x = -1, int y = -1);
  std::string add_path (std::string name);
  void set_path_by_filename (std::string name);
  void keep_all_images ()
  {
    release_images = false;
  }
  bool
  tile_for_scr (render_parameters *rparams, coord_t sx, coord_t sy, int *x, int *y, bool only_loaded)
  {
#if 0
    /* Lookup tile to use. */
    int ix = 0, iy;
    for (iy = 0 ; iy < params.height; iy++)
      {
	for (ix = 0 ; ix < params.width; ix++)
	  if ((!only_loaded || images[iy][ix].img)
	      && (!rparams || rparams->get_tile_adjustment (this, ix, iy).enabled)
	      && images[iy][ix].pixel_known_p (sx, sy))
	    break;
	if (ix != params.width)
	  break;
      }
    if (iy == params.height)
      return false;
    *x = ix;
    *y = iy;
    return true;
#else
    bool found = false;
    int bx = 0, by = 0;
    int bdist = 0;
    /* Lookup tile to use. */
    int ix = 0, iy;
    for (iy = 0 ; iy < params.height; iy++)
      {
	for (ix = 0 ; ix < params.width; ix++)
	  if ((!only_loaded || images[iy][ix].img)
	      && (!rparams || rparams->get_tile_adjustment (this, ix, iy).enabled)
	      && images[iy][ix].pixel_maybe_in_range_p (sx, sy))
	    {
	      coord_t xx, yy;
	      /* Compute image coordinates.  */
	      images[iy][ix].common_scr_to_img (sx, sy, &xx, &yy);
	      /* Shortest distance from the edge.  */
	      if (xx < 0 || xx >= images[iy][ix].img_width || yy < 0 || yy >= images[iy][ix].img_height)
		continue;
	      int dd = std::min (std::min ((int)xx, images[iy][ix].img_width - (int)xx),
				 std::min ((int)yy, images[iy][ix].img_height - (int)yy));
	      /* Try to minimize distances to edges.  */
	      if (dd>0 && (!found || dd > bdist))
	        {
		  bx = ix;
		  by = iy;
		  bdist = dd;
		  found = true;
	        }
	    }
      }
    if (!found)
      return false;
    *x = bx;
    *y = by;
    return true;
#endif
  }
  bool write_tiles (render_parameters rparam, struct render_to_file_params *rfparams, struct render_type_parameters &rtparam, int n, progress_info * progress, const char **error);
  enum optimize_tile_adjustments_flags
  {
    OPTIMIZE_BACKLIGHT_BLACK = 1,
    OPTIMIZE_EXPOSURE = 2,
    OPTIMIZE_DARK_POINT = 4,
    VERBOSE = 8,
    OPTIMIZE_ALL = -1 & ~VERBOSE
  };
  DLL_PUBLIC bool optimize_tile_adjustments (render_parameters *rparams, int flags, const char **rerror, progress_info *info = NULL);
  void set_dpi (coord_t new_xdpi, coord_t new_ydpi)
  {
    params.scan_xdpi = new_xdpi;
    params.scan_ydpi = new_ydpi;
    for (int iy = 0 ; iy < params.height; iy++)
      for (int ix = 0 ; ix < params.width; ix++)
	if (images[iy][ix].img)
	  images[iy][ix].img->set_dpi (new_xdpi, new_ydpi);
  }

  struct tile_range
  {
    int tile_x, tile_y;
    coord_t xmin, ymin, xmax, ymax;
  };
  std::vector <tile_range> find_ranges (coord_t xmin, coord_t xmax, coord_t ymin, coord_t ymax, bool only_loaded, bool screen_ranges);
  DLL_PUBLIC bool stitch (progress_info *progress, const char *load_project_filename);
private:
  bool analyze_images (progress_info *progress);
  void produce_hugin_pto_file (const char *name, progress_info *progress);
  /* Passed from initialize to analyze_angle to determine scr param.
     TODO: Localize to analyze_angle.  */
  scr_to_img_parameters scr_param;
  bool analyze (int x, int y, progress_info *);
  void print_panorama_map (FILE *out);
  void print_status (FILE *out);
  /* Screen used to collect patch density at analysis stage.  */
  screen *my_screen;
  int stitch_info_scale = 0;
  bool release_images;
  coord_t rotation_adjustment;
  friend stitch_image;

  struct overlap
  {
    int x1, y1, x2, y2;
    stitch_image::common_samples samples;
    luminosity_t add, mul, weight;
  };
  double solve_equations (render_parameters *in_rparams, std::vector <overlap> &overlaps, int flags, progress_info *progress, bool finished, const char **error);
};


analyze_base & 
stitch_image::get_analyzer ()
{
  return *analyzer;
}

#endif
