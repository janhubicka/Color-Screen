#ifndef SOLVER_H
#define SOLVER_H
#include "scr-to-img.h"
#include "color.h"
#include "progress-info.h"
#include "scr-detect.h"
#include "imagedata.h"
#include "bitmap.h"
struct solver_parameters
{
  solver_parameters ()
  : npoints (0), point (NULL)
  {
  }
  ~solver_parameters ()
  {
    free (point);
  }
  enum point_color {red,green,blue,max_point_color};
  static const char *point_color_names[(int)max_point_color];
  struct point_location
  {
    coord_t x, y;
    solver_parameters::point_color color;
  };
  struct point_t
  {
    coord_t img_x, img_y;
    coord_t screen_x, screen_y;
    enum point_color color;

    void
    get_rgb (luminosity_t *r, luminosity_t *g, luminosity_t *b)
    {
      const luminosity_t colors [][3]={{1,0,0},{0,1,0},{0,0,1}};
      *r = colors[(int)color][0];
      *g = colors[(int)color][1];
      *b = colors[(int)color][2];
    }
  };

  int npoints;
  struct point_t *point;
  bool weighted;
  coord_t center_x, center_y;

  int
  add_point (coord_t img_x, coord_t img_y, coord_t screen_x, coord_t screen_y, enum point_color color)
  {
    npoints++;
    point = (point_t *)realloc ((void *)point, npoints * sizeof (point_t));
    point[npoints - 1].img_x = img_x;
    point[npoints - 1].img_y = img_y;
    point[npoints - 1].screen_x = screen_x;
    point[npoints - 1].screen_y = screen_y;
    point[npoints - 1].color = color;
    return npoints;
  }

  void
  remove_point (int n)
  {
    /* Just for fun keep the order as points were added.  */
    for (;n < npoints - 1; n++)
      point[n] = point[n+1];
#if 0
    if (n < 0 || n >= npoints)
      abort ();
    point[n] = point[npoints - 1];
#endif
    npoints--;
  }
  void
  remove_points ()
  {
    npoints = 0;
  }
  void
  dump (FILE *out)
  {
    for (int i =0; i < npoints; i++)
      {
	fprintf (out, "point %i img %f %f maps to scr %f %f color %i\n", i, point[i].img_x, point[i].img_y, point[i].screen_x, point[i].screen_y, (int)point[i].color);
      }
  }
  static point_location *get_point_locations (enum scr_type type, int *n);
};
coord_t solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress = NULL);
coord_t simple_solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress = NULL);
mesh *solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress = NULL);

/* Invormation about auto-detected screen.
   If mesh_trans is NULL the detection failed.  */
struct detected_screen
{
  mesh *mesh_trans;
  scr_to_img_parameters param;
  int xmin, ymin, xmax, ymax;
  int patches_found;
  coord_t pixel_size;

  int xshift, yshift;
  bitmap_2d *known_patches;
  struct screen_map *smap;
};

/* Parameters for reglar screen detection.  */
struct detect_regular_screen_params
{
  detect_regular_screen_params ()
  : min_screen_percentage (20), border_top (0), border_bottom (0), border_left (0), border_right (0),
    optimize_colors (false), slow_floodfill (false), return_known_patches (false), return_screen_map (false)
  {}
  coord_t min_screen_percentage;
  coord_t border_top, border_bottom, border_left, border_right;
  bool optimize_colors;
  bool slow_floodfill;
  bool return_known_patches;
  bool return_screen_map;
};
detected_screen detect_regular_screen (image_data &img, scr_detect_parameters &dparam, luminosity_t gamma, solver_parameters &sparam, detect_regular_screen_params *dsparams, progress_info *progress = NULL, FILE *report_file = NULL);
void optimize_screen_colors (scr_detect_parameters *param, luminosity_t gamma, color_t *reds, int nreds, color_t *greens, int ngreens, color_t *blues, int nblues, progress_info *progress = NULL, FILE *report = NULL);
void optimize_screen_colors (scr_detect_parameters *param, image_data *img, mesh *m, int xshift, int yshift, bitmap_2d *known_patches, luminosity_t gamma, progress_info *progress = NULL, FILE *report = NULL);
bool optimize_screen_colors (scr_detect_parameters *param, image_data *img, luminosity_t gamma, int x, int y, int width, int height, progress_info *progress = NULL, FILE *report = NULL);


#endif
