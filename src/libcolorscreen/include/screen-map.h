#ifndef SCREEN_MAP_H
#define SCREEN_MAP_H
#include <limits>
#include "dllpublic.h"
#include "solver-parameters.h"
#include "paget.h"
class screen_map
{
public:
  DLL_PUBLIC screen_map (enum scr_type type1, int xshift1, int yshift1,
                         int width1, int height1);
  DLL_PUBLIC ~screen_map ();
  DLL_PUBLIC bool write_outliers_info (const char *filename, int width,
                                       int scale, int height, scr_to_img &map,
                                       const char **error,
                                       progress_info *progress) const;
  bool
  in_range_p (int x, int y) const
  {
    x += xshift;
    y += yshift;
    return (x >= 0 && x < width && y >= 0 && y < height);
  }
  bool
  known_p (int x, int y) const
  {
    if (!in_range_p (x, y))
      return false;
    x += xshift;
    y += yshift;
    return (map[y * width + x].x != 0);
  }
  /* We insert fake points to straighten areas without grid detected.  */
  bool
  known_and_not_fake_p (int x, int y) const
  {
    if (!in_range_p (x, y))
      return false;
    x += xshift;
    y += yshift;
    coord_t ix = map[y * width + x].x;
    coord_t iy = map[y * width + x].y;
    assert (xmin != xmax);
    return (map[y * width + x].x != 0 && ix >= xmin && ix <= xmax && iy >= ymin
            && iy <= ymax);
  }
  void
  set_coord (int xx, int yy, coord_t img_x, coord_t img_y)
  {
    if (colorscreen_checking)
      assert (in_range_p (xx, yy));
    int x = xx + xshift;
    int y = yy + yshift;
    if (img_x == 0)
      img_x = 0.00001;
    if (img_y == 0)
      img_y = 0.00001;
    map[y * width + x].x = img_x;
    map[y * width + x].y = img_y;
  }
  void
  safe_set_coord (int xx, int yy, coord_t img_x, coord_t img_y)
  {
    int x = xx + xshift;
    int y = yy + yshift;
    if (x < 0 || x >= width || y < 0 || y >= height)
      {
        grow (x < 0, x >= width, y < 0, y >= height);
        x = xx + xshift;
        y = yy + yshift;
        assert (x >= 0 && x < width && y >= 0 && y < height);
      }
    if (img_x == 0)
      img_x = 0.00001;
    if (img_y == 0)
      img_y = 0.00001;
    map[y * width + x].x = img_x;
    map[y * width + x].y = img_y;
  }
  void
  get_coord (int x, int y, coord_t *img_x, coord_t *img_y) const
  {
    if (colorscreen_checking)
      assert (in_range_p (x, y));
    x += xshift;
    y += yshift;
    *img_x = map[y * width + x].x;
    *img_y = map[y * width + x].y;
  }
  void
  get_screen_coord (int x, int y, coord_t *scr_x, coord_t *scr_y,
                    solver_parameters::point_color *color = NULL) const
  {
    if (type == Dufay)
      {
        *scr_x = x / 2.0;
        *scr_y = y;
        if (!color)
          return;
        *color = (x & 1) ? solver_parameters::blue : solver_parameters::green;
      }
    else
      {
        int_point_t p
            = paget_geometry::from_diagonal_coordinates (int_point_t{ x, y });
        *scr_x = p.x / 4.0;
        *scr_y = p.y / 4.0;
        if (!color)
          return;
        if (!(y & 1))
          *color
              = (x & 1) ? solver_parameters::blue : solver_parameters::green;
        else
          *color = (x & 1) ? solver_parameters::red : solver_parameters::blue;
      }
  }

  /* API used only by libcolorscreen.  */
  void get_solver_points_nearby (coord_t sx, coord_t sy, int n,
                                 solver_parameters &sparams) const;
  int check_consistency (FILE *out, coord_t coordinate1_x,
                         coord_t coordinate1_y, coord_t coordinate2_x,
                         coord_t coordinate2_y, coord_t tolerance) const;
  void add_solver_points (solver_parameters *sparam, int xgrid, int ygrid);
  bool grow (bool left, bool right, bool top, bool bottom);
  void get_known_range (int *xminr, int *yminr, int *xmaxr, int *ymaxr);
  void determine_solver_points (int patches_found, solver_parameters *sparam) const;

  enum scr_type type;
  int width, height, xshift, yshift;

private:
  int xmin, xmax, ymin, ymax;
  struct coord_entry
  {
    coord_t x, y;
  };
  coord_entry *map;
};
mesh *solver_mesh (scr_to_img_parameters *param, image_data &img_data,
                   solver_parameters &sparam2, screen_map &smap,
                   progress_info *progress);
#endif
