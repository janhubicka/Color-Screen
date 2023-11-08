#ifndef COLORSCREEN_H
#define COLORSCREEN_H
#include "render-fast.h"
#include "render-scr-detect.h"
#include "solver.h"

/* Supported output modes.  */
enum output_mode
{
  corrected,
  corrected_color,
  realistic,
  preview_grid,
  color_preview_grid,
  interpolated,
  predictive,
  combined,
  /* detect_adjusted must be first detect mode.  */
  detect_adjusted,
  detect_realistic,
  detect_nearest,
  detect_nearest_scaled,
  detect_relax,
  output_mode_max
};


struct render_to_file_params
{
  output_mode mode;
  const char *filename;
  int depth;
  bool verbose;
  bool hdr;
  coord_t scale;
  void *icc_profile;
  int icc_profile_len;
  int antialias;
  coord_t xdpi, ydpi;

  /* Width and height of the output file.  It will be computed if set to 0.  */
  int width, height;
  bool tile;
  /* Specifies top left corner in coordinates.  */
  coord_t xstart, ystart;
  /* Size of single pixel.  If 0 default is computed using output mode and scale. */
  coord_t xstep, ystep;
  /* Pixel size used to determine antialiasing factor.  It needs to be same in whole stitch project. */
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
  : mode (interpolated), filename (NULL), depth(16), verbose (false), hdr (false), scale (1), icc_profile (NULL), icc_profile_len (0), antialias (0), xdpi (0), ydpi (0), width (0), height (0), tile (0), xstart (0), ystart (0), xstep (0), ystep (0), pixel_size (0)
  {}

  struct output_mode_property
  {
    const char *name;
    bool require_rgb;
  };
  const static struct output_mode_property output_mode_properties[output_mode_max];
};
DLL_PUBLIC bool render_to_file(image_data &scan, scr_to_img_parameters &param, scr_detect_parameters &dparam, render_parameters rparam,
			render_to_file_params rfarams, progress_info *progress, const char **error);
DLL_PUBLIC bool complete_rendered_file_parameters (scr_to_img_parameters & param, image_data &scan, render_to_file_params *p);
DLL_PUBLIC bool complete_rendered_file_parameters (scr_to_img_parameters * param, image_data *scan, stitch_project *stitch, render_to_file_params *p);
#endif
