#ifndef COLORSCREEN_H
#define COLORSCREEN_H
#include <memory>
#include <string>
#include "render-parameters.h"
#include "render-type-parameters.h"
#include "solver-parameters.h"
#include "detect-regular-screen-parameters.h"
#include "scr-detect-parameters.h"
namespace colorscreen
{
class scr_to_img;

struct render_to_file_params
{
  const char *filename;
  int depth;
  bool verbose;
  bool hdr;
  bool dng;
  coord_t scale;
  const void *icc_profile;
  int icc_profile_len;
  int antialias;
  coord_t xdpi, ydpi;

  enum output_geometry
  {
    /* Resulting file will use screen geometry (correct possible deformations
       of the scan).  */
    screen_geometry,
    /* Resulting file will use scan geometry.  */
    scan_geometry,
    /* Default value.  Use scan geoemtry for scr based rendering modes and scan
       for scr_detect.  */
    default_geometry,
    max_geometry,
  } geometry;

  static const DLL_PUBLIC char * const geometry_names[(int)max_geometry];


  /* Width and height of the output file.  It will be computed if set to 0.  */
  int width, height;
  bool tile;
  /* Specifies top left corner in coordinates.  */
  coord_t xstart, ystart;
  /* Size of single pixel.  If 0 default is computed using output mode and
     scale. */
  coord_t xstep, ystep;
  /* Pixel size used to determine antialiasing factor.  It needs to be same in
     whole stitch project. */
  coord_t pixel_size;
  /* Offset of a tile.  */
  int xoffset, yoffset;
  bool (*pixel_known_p) (void *data, coord_t x, coord_t y);
  void *pixel_known_p_data;
  /* Common map used for stitching project.  */
  scr_to_img *common_map;
  /* Position of rendered image in the project.  */
  coord_t xpos, ypos;
  render_to_file_params ()
      : filename (NULL), depth (16), verbose (false), hdr (false), dng (false),
        scale (1), icc_profile (NULL), icc_profile_len (0), antialias (0),
        xdpi (0), ydpi (0), geometry (default_geometry), width (0), height (0), tile (0), xstart (0),
        ystart (0), xstep (0), ystep (0), pixel_size (0)
  {
  }
};

struct tile_parameters
{
  uint8_t *pixels;
  int rowstride;
  int pixelbytes;
  int width;
  int height;
  point_t pos;
  coord_t step;
};

struct color_match
{
  xyz profiled;
  xyz target;
  luminosity_t deltaE;
};

struct has_regular_screen_params
{
  /* Minimum and maximum period of screen to look for.  */
  coord_t min_period;
  coord_t max_period;

  /* Save individual tiles and fft?  */
  bool save_tiles;
  bool save_fft;
  std::string tile_base;
  std::string fft_base;

  /* width and height.  */
  int ntilesx;
  int ntilesy;

  /* Input gamma.  */
  luminosity_t gamma;

  coord_t threshold;
  coord_t tiles_treshold;

  bool verbose;
  FILE *report;

  has_regular_screen_params ()
  : min_period (2.01), max_period (30), save_tiles (false), save_fft (false), tile_base ("tile"), fft_base ("fft"), ntilesx (9), ntilesy (9), gamma (0), threshold (1.1), tiles_treshold (0.15), verbose (false), report (NULL)
  {
  }
};
struct has_regular_screen_ret
{
  bool found;
  const char *error;
  coord_t period;
  coord_t perc;
};
DLL_PUBLIC struct has_regular_screen_ret has_regular_screen (image_data &scan, const has_regular_screen_params &params, progress_info *progress = NULL);
DLL_PUBLIC bool save_csp (FILE *f, scr_to_img_parameters *param,
                          scr_detect_parameters *dparam,
                          render_parameters *rparam,
                          solver_parameters *sparam);
DLL_PUBLIC bool load_csp (FILE *f, scr_to_img_parameters *param,
                          scr_detect_parameters *dparam,
                          render_parameters *rparam, solver_parameters *sparam,
                          const char **error);
DLL_PUBLIC bool render_to_file (image_data &scan, scr_to_img_parameters &param,
                                scr_detect_parameters &dparam,
                                render_parameters &rparam,
                                render_to_file_params rfarams,
                                render_type_parameters &rtparam,
                                progress_info *progress, const char **error);
DLL_PUBLIC bool render_tile (image_data &scan, scr_to_img_parameters &param,
                             scr_detect_parameters &dparam,
                             render_parameters &rparam,
                             render_type_parameters &rtparam,
                             tile_parameters &tile,
                             progress_info *progress = NULL);
enum render_screen_tile_type
{
  original_screen,
  blured_screen,
  sharpened_screen,
  backlight_screen,
  detail_screen,
  full_screen,
  coorected_backlight_screen,
  coorected_detail_screen,
  coorected_full_screen
};
DLL_PUBLIC bool render_screen_tile (tile_parameters &tile,
				    scr_type type,
				    const render_parameters &rparam,
				    coord_t pixel_size,
				    enum render_screen_tile_type,
				    progress_info *p);
DLL_PUBLIC bool complete_rendered_file_parameters
				  (render_type_parameters &rtparams,
                                   scr_to_img_parameters &param,
                                   image_data &scan, render_to_file_params *p);
DLL_PUBLIC bool complete_rendered_file_parameters (
    render_type_parameters *rtparams, scr_to_img_parameters *param,
    image_data *scan, stitch_project *stitch, render_to_file_params *p);
DLL_PUBLIC rgbdata get_linearized_pixel (const image_data &img,
                                         render_parameters &rparam, int x,
                                         int y, int range = 4,
                                         progress_info *progress = NULL);
DLL_PUBLIC bool dump_patch_density (FILE *out, image_data &scan,
                                    scr_to_img_parameters &param,
                                    render_parameters &rparam,
                                    progress_info *progress = NULL);
DLL_PUBLIC bool render_preview (image_data &scan, scr_to_img_parameters &param,
                                render_parameters &rparams,
                                unsigned char *pixels, int width, int height,
                                int rowstride, progress_info *progress = NULL);
DLL_PUBLIC rgbdata analyze_color_proportions (
    scr_detect_parameters param, render_parameters &rparam, image_data &img,
    scr_to_img_parameters *map_param, int xmin, int ymin, int xmax, int ymax,
    progress_info *p = NULL);

DLL_PUBLIC coord_t solver (scr_to_img_parameters *param, image_data &img_data,
                           solver_parameters &sparam,
                           progress_info *progress = NULL);
DLL_PUBLIC std::unique_ptr<mesh> solver_mesh (scr_to_img_parameters *param,
                                              image_data &img_data,
                                              solver_parameters &sparam,
                                              progress_info *progress = NULL);
DLL_PUBLIC detected_screen detect_regular_screen (
    image_data &img, scr_detect_parameters &dparam,
    solver_parameters &sparam,
    detect_regular_screen_params *dsparams,
    progress_info *progress = NULL,
    FILE *report_file = NULL);
DLL_PUBLIC color_matrix determine_color_matrix (
    rgbdata *colors, xyz *targets, rgbdata *rgbtargets, int n, xyz white,
    int dark_point_elts = 0, std::vector<color_match> *report = NULL,
    render *r = NULL, rgbdata proportions = { 1, 1, 1 },
    progress_info *progress = NULL);
DLL_PUBLIC bool optimize_color_model_colors (scr_to_img_parameters *param,
                                             image_data &img,
                                             render_parameters &rparam,
                                             std::vector<point_t> &points,
                                             std::vector<color_match> *report,
                                             progress_info *progress);
DLL_PUBLIC bool compare_deltae (image_data &img, scr_to_img_parameters &param1, render_parameters &rparam1, scr_to_img_parameters &param2, render_parameters &rparam2, const char *cmpname, double *, double *, progress_info *progress = NULL);
}
#endif
