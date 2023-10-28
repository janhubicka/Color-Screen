#ifndef STITCH_H
#define STITCH_H
#include <string>
#include "scr-to-img.h"
#include "render.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
#include "solver.h"
#include "../libcolorscreen/render-interpolate.h"

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

  int outer_tile_border;
  int inner_tile_border;
  int min_overlap_percentage;
  int max_overlap_percentage;
  int max_unknown_screen_range;
  luminosity_t max_contrast;
  luminosity_t orig_tile_gamma;
  luminosity_t min_patch_contrast;

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
  : type (Dufay), demosaiced_tiles (false), predictive_tiles (false), orig_tiles (false), screen_tiles (false), known_screen_tiles (false),
    cpfind (true), panorama_map (false), optimize_colors (true), reoptimize_colors (false), slow_floodfill (true), fast_floodfill (false), limit_directions (false), mesh_trans (true),
    geometry_info (false), individual_geometry_info (false), outliers_info (false), diffs (false),
    outer_tile_border (30), inner_tile_border (2), min_overlap_percentage (10), max_overlap_percentage (65), max_unknown_screen_range (100), max_contrast (-1), orig_tile_gamma (-1), min_patch_contrast (-1), num_control_points (100), min_screen_percentage (75), hfov (28.534),
    max_avg_distance (2), max_max_distance (10)
  {}
};

class render_interpolate;
class stitch_project;

class stitch_image
{
  public:

  enum render_mode
  {
    render_demosaiced,
    render_predictive,
    render_original
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

  render_interpolate *render;
  render_img *render2;
  render_interpolate *render3;
  coord_t angle;
  coord_t ratio;

  struct stitch_info {coord_t x,y;
    		      int sum;} *stitch_info;

  coord_t xpos, ypos;
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
  void update_scr_to_final_parameters (coord_t ratio, coord_t anlge);
  void analyze (stitch_project *prj, bool top_p, bool bottom_p, bool left_p, bool right_p, coord_t k1, progress_info *);
  void release_image_data (progress_info *);
  bitmap_2d *compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress);
  int output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress = NULL);
  bool pixel_known_p (coord_t sx, coord_t sy);
  bool img_pixel_known_p (coord_t sx, coord_t sy);
  bool patch_detected_p (int sx, int sy);
  bool render_pixel (render_parameters & rparam, render_parameters &passthrough_rparam, coord_t sx, coord_t sy, render_mode mode, int *r, int *g, int *b, progress_info *p);
  bool write_tile (const char **error, scr_to_img &map, int xmin, int ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress);
  void compare_contrast_with (stitch_image &other, progress_info *progress);
  void write_stitch_info (progress_info *progress, int x = -1, int y = -1, int xx = -1, int yy = -1);
  void clear_stitch_info ();
  bool diff (stitch_image &other, progress_info *progress);

  inline analyze_base & get_analyzer ();
private:
  static long current_time;
  static int nloaded;
  long lastused;
  int refcount;
  stitch_project *m_prj;
};

class stitch_project
{
  public:
  struct stitching_params params;
  FILE *report_file;
  stitch_image images[stitching_params::max_dim][stitching_params::max_dim];
  scr_to_img_parameters param;
  render_parameters rparam;
  render_parameters passthrough_rparam;
  scr_to_img common_scr_to_img;
  scr_detect_parameters dparam;
  solver_parameters solver_param;
  coord_t pixel_size;
  private:
  screen *my_screen;
  int stitch_info_scale = 0;
  bool initialized;
  friend stitch_image;
  public:
  stitch_project () : params (), report_file (NULL), images(), param (), rparam (), passthrough_rparam (), common_scr_to_img (), dparam (), solver_param (), pixel_size (0), my_screen (NULL), stitch_info_scale (0), initialized (false)
  {}
  ~stitch_project ()
  {
  if (my_screen)
    render_to_scr::release_screen (my_screen);
  }
};


analyze_base & 
stitch_image::get_analyzer ()
{
  return m_prj->params.type == Dufay ? (analyze_base &)dufay : (analyze_base &)paget;
}

#endif
