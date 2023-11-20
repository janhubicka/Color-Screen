#ifndef STITCH_H
#define STITCH_H
#include <pthread.h>
#include <string>
#include "scr-to-img.h"
#include "render.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
#include "solver.h"
#include "colorscreen.h"
struct tiff;
typedef struct tiff TIFF;

struct stitching_params
{
  static const int max_dim = 10;

  enum scr_type type;

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
  bool fast_floodfill;
  bool limit_directions;
  bool mesh_trans;
  bool geometry_info;
  bool individual_geometry_info;
  bool outliers_info;
  bool diffs;
  bool hdr;

  int outer_tile_border;
  int inner_tile_border;
  int min_overlap_percentage;
  int max_overlap_percentage;
  int max_unknown_screen_range;
  int downscale;
  luminosity_t max_contrast;
  luminosity_t orig_tile_gamma;
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
  : type (Dufay), demosaiced_tiles (false), predictive_tiles (false), orig_tiles (false), screen_tiles (false), known_screen_tiles (false),
    cpfind (true), panorama_map (false), optimize_colors (true), reoptimize_colors (false), slow_floodfill (true), fast_floodfill (false), limit_directions (false), mesh_trans (true),
    geometry_info (false), individual_geometry_info (false), outliers_info (false), diffs (false), hdr (false),
    outer_tile_border (30), inner_tile_border (10), min_overlap_percentage (10), max_overlap_percentage (65), max_unknown_screen_range (100), downscale (1), max_contrast (-1), orig_tile_gamma (-1), min_patch_contrast (-1), num_control_points (100), min_screen_percentage (75), hfov (28.534),
    max_avg_distance (2), max_max_distance (10), scan_xdpi (0), scan_ydpi (0), width (0), height (0), path("")
  {
  }
};

class render_interpolate;
class render_fast;
class stitch_project;

class stitch_image
{
  public:

  enum render_mode
  {
    render_demosaiced,
    render_predictive,
    render_original,
    render_max
  };
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
  analyze_paget paget;
  /* Screen patches that was detected by screen detection algorithm.  */
  bitmap_2d *screen_detected_patches;
  /* Known pixels used by stitching algorithm.  This is basically the image without borders.  */
  bitmap_2d *known_pixels;

  detected_screen detected;

  render_img *render2;
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
  DLL_PUBLIC ~stitch_image ();
  bool init_loader (const char **error, progress_info *progress);
  bool load_img (const char **error, progress_info *);
  bool load_part (int *permille, const char **error, progress_info *progress);
  void release_img ();
  void update_scr_to_final_parameters (coord_t ratio, coord_t anlge);
  bool analyze (stitch_project *prj, bool top_p, bool bottom_p, bool left_p, bool right_p, coord_t k1, progress_info *);
  void release_image_data (progress_info *);
  bitmap_2d *compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress);
  int output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress = NULL);
  bool pixel_known_p (coord_t sx, coord_t sy);
  bool img_pixel_known_p (coord_t sx, coord_t sy);
  bool patch_detected_p (int sx, int sy);
  bool write_tile (render_parameters rparam, int stitch_xmin, int stitch_ymin, render_to_file_params rfparams, const char **error, progress_info *progress);
  bool write_tile_old (const char **error, scr_to_img &map, int xmin, int ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress);
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
  void common_scr_to_img_scr (coord_t sx, coord_t sy, coord_t *isx, coord_t *isy)
  {
    *isx = sx - xpos;
    *isy = sy - ypos;
  }
  void common_scr_to_img (coord_t sx, coord_t sy, coord_t *ix, coord_t *iy)
  {
    scr_to_img_map.to_img (sx - xpos, sy - ypos, ix, iy);
  }
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
     to pass k1.  */
  scr_to_img_parameters param;
  /* Rendering parameters. Expected to be initialized by user.  */
  render_parameters rparam;
  /* Rendering parameters used to output original tiles.  Initialized 
     in initialize () call.  */
  render_parameters passthrough_rparam;
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
  bool analyze_images (progress_info *progress);
  void determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax);
  void determine_angle ();
  bool save (FILE *f);
  bool load (FILE *f, const char **error);
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
  bool write_tiles (render_parameters rparam, struct render_to_file_params *rfparams, int n, progress_info * progress, const char **error);
  DLL_PUBLIC bool optimize_tile_adjustments (render_parameters *rparams, const char **rerror, progress_info *info = NULL);
  void set_dpi (coord_t new_xdpi, coord_t new_ydpi)
  {
    params.scan_xdpi = new_xdpi;
    params.scan_ydpi = new_ydpi;
    for (int iy = 0 ; iy < params.height; iy++)
      for (int ix = 0 ; ix < params.width; ix++)
	if (images[iy][ix].img)
	  images[iy][ix].img->set_dpi (new_xdpi, new_ydpi);

  }

  struct ratio 
    {
      luminosity_t ratio, val1, val2, weight;
      bool operator < (const struct ratio &other) const
      {
	return ratio < other.ratio;
      }
    };
  struct ratios
  {
    std::vector<ratio> channel[4];
  };
private:
  /* Passed from initialize to analyze_angle to determine scr param.
     TODO: Localize to analyze_angle.  */
  scr_to_img_parameters scr_param;
  bool analyze (int x, int y, progress_info *);
  void print_panorama_map (FILE *out);
  void print_status (FILE *out);
  /* Screen used to collect patch density at analysis stage.  */
  screen *my_screen;
  int stitch_info_scale = 0;
  coord_t xdpi[(int)stitch_image::render_max];
  coord_t ydpi[(int)stitch_image::render_max];
  bool release_images;
  coord_t rotation_adjustment;
  friend stitch_image;

  struct ratios find_common_points (int x, int y, int ix, int iy, int outerborder, int innerborder, render_parameters &rparams, render &render1, progress_info *progress, const char **error);
};


analyze_base & 
stitch_image::get_analyzer ()
{
  return m_prj->params.type == Dufay ? (analyze_base &)dufay : (analyze_base &)paget;
}

#endif
