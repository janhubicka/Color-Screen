#ifndef SCREEN_MAP_H
#define SCREEN_MAP_H
#include <limits>
#include <memory>
#include "dllpublic.h"
#include "solver-parameters.h"
#include "paget.h"
#include "progress-info.h"
namespace colorscreen
{
class scr_to_img;
class image_data;
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
  in_range_p (int_point_t p) const
  {
    p.x += xshift;
    p.y += yshift;
    return (p.x >= 0 && p.x < width && p.y >= 0 && p.y < height);
  }
  bool
  known_p (int_point_t p) const
  {
    if (!in_range_p (p))
      return false;
    p.x += xshift;
    p.y += yshift;
    return (map[p.y * width + p.x].x != 0);
  }
  /* We insert fake points to straighten areas without grid detected.  */
  bool
  known_and_not_fake_p (int_point_t p) const
  {
    if (!in_range_p (p))
      return false;
    p.x += xshift;
    p.y += yshift;
    coord_t ix = map[p.y * width + p.x].x;
    coord_t iy = map[p.y * width + p.x].y;
    assert (xmin != xmax);
    return (map[p.y * width + p.x].x != 0 && ix >= xmin && ix <= xmax && iy >= ymin
            && iy <= ymax);
  }
  void
  set_coord (int_point_t e, point_t img)
  {
    if (colorscreen_checking)
      assert (in_range_p (e));
    int x = e.x + xshift;
    int y = e.y + yshift;
    if (img.x == 0)
      img.x = 0.00001;
    if (img.y == 0)
      img.y = 0.00001;
    map[y * width + x].x = img.x;
    map[y * width + x].y = img.y;
  }
  void
  safe_set_coord (int_point_t e, point_t img)
  {
    int x = e.x + xshift;
    int y = e.y + yshift;
    if (x < 0 || x >= width || y < 0 || y >= height)
      {
        grow (x < 0, x >= width, y < 0, y >= height);
        x = e.x + xshift;
        y = e.y + yshift;
        assert (x >= 0 && x < width && y >= 0 && y < height);
      }
    if (img.x == 0)
      img.x = 0.00001;
    if (img.y == 0)
      img.y = 0.00001;
    map[y * width + x].x = img.x;
    map[y * width + x].y = img.y;
  }
  inline pure_attr point_t
  get_coord (int_point_t e) const
  {
    if (colorscreen_checking)
      assert (in_range_p (e));
    e.x += xshift;
    e.y += yshift;
    return {(coord_t)map[e.y * width + e.x].x, (coord_t)map[e.y * width + e.x].y};
  }
  inline point_t
  get_screen_coord (int_point_t e, solver_parameters::point_color *color = NULL) const
  {
    point_t scr;
    if (type == Dufay)
      {
        scr.x = e.x / 2.0;
        scr.y = e.y;
        if (!color)
          return scr;
        *color = (e.x & 1) ? solver_parameters::blue : solver_parameters::green;
      }
    else
      {
        int_point_t p = paget_geometry::from_diagonal_coordinates (e);
        scr.x = p.x / 4.0;
        scr.y = p.y / 4.0;
        if (!color)
          return scr;
        if (!(e.y & 1))
          *color
              = (e.x & 1) ? solver_parameters::blue : solver_parameters::green;
        else
          *color = (e.x & 1) ? solver_parameters::red : solver_parameters::blue;
      }
    return scr;
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
std::unique_ptr<mesh> solver_mesh (scr_to_img_parameters *param,
                                   image_data &img_data,
                                   solver_parameters &sparam2,
                                   screen_map &smap, progress_info *progress);
}
#endif
