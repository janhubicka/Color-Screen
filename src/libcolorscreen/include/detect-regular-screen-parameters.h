#ifndef DETECT_REGULAR_SCREEN_PARAMETERS_H
#define DETECT_REGULAR_SCREEN_PARAMETERS_H
#include "scr-to-img-parameters.h"
namespace colorscreen
{
class bitmap_2d;
/* Parameters for reglar screen detection.  */
struct detect_regular_screen_params
{
  DLL_PUBLIC_EXP
  detect_regular_screen_params ()
      : scr_type (max_scr_type), scanner_type (max_scanner_type), gamma (-2), 
	min_screen_percentage (20), border_top (50), border_bottom (50),
        border_left (50), border_right (50), top (false), bottom (false),
        left (false), right (false), lens_correction (),
        min_patch_contrast (2), max_unknown_screen_range (10000),
        optimize_colors (true), slow_floodfill (true), fast_floodfill (true),
        return_known_patches (false), return_screen_map (false), do_mesh (true)
  {
  }

  /* Screen type.  */
  enum scr_type scr_type;
  /* Scanner type.  */
  enum scanner_type scanner_type;
  /* Gamma of the scan.  */
  luminosity_t gamma;
  coord_t min_screen_percentage;
  coord_t border_top, border_bottom, border_left, border_right;
  /* True if this is part of an tile that is on top, bottom, left or right.  */
  bool top, bottom, left, right;
  lens_warp_correction_parameters lens_correction;
  luminosity_t min_patch_contrast;
  int max_unknown_screen_range;
  bool optimize_colors;
  bool slow_floodfill;
  bool fast_floodfill;
  bool return_known_patches;
  bool return_screen_map;
  bool do_mesh;
};
/* Invormation about auto-detected screen.
   If mesh_trans is NULL the detection failed.  */
struct detected_screen
{
  bool success;
  mesh *mesh_trans;
  scr_to_img_parameters param;
  int xmin, ymin, xmax, ymax;
  int patches_found;
  coord_t pixel_size;

  /* xshift and yshift of known patches.
     We can not use smap shifts because known_patches is in screen
     coordinates, while smap is, on Finlay and Paget, in diagonal
     coordinates.  */
  int xshift, yshift;
  bitmap_2d *known_patches;

  class screen_map *smap;
};
}
#endif
