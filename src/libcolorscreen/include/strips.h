#ifndef STRIPS_H
#define STRIPS_H
#include "base.h"
namespace colorscreen
{
struct strips_geometry
{
  /* Assume strips with horisontal strips

     GGGGG
     BBBBB
     RRRRR

     This is the case of WarnerPowrie process.  Other strips can be adapted easily.
   */
  static constexpr const int red_width_scale = 1;
  static constexpr const int red_height_scale = 1;
  static constexpr const int green_width_scale = 1;
  static constexpr const int green_height_scale = 1;
  static constexpr const int blue_width_scale = 1;
  static constexpr const int blue_height_scale = 1;
  /* ??? we may pick previus screen cycle in X directly for example
     when red pixel has offset 0.1.  It would be more effective to simply enlarge the
     area.*/
  static constexpr const bool check_range = true;

  /* Used to compute grid for interpolation between neighbouring values.
     Everything is orthogonal, so no translation necessary  */
  inline static int_point_t offset_for_interpolation_red (int_point_t e, int_point_t off)
  {
    return e + off;
  }
  inline static int_point_t offset_for_interpolation_green (int_point_t e, int_point_t off)
  {
    return e + off;
  }
  inline static int_point_t offset_for_interpolation_blue (int_point_t e, int_point_t off)
  {
    return e + off;
  }

  /* Convert screen coordinates to data entry, possibly with offset for interpolation.
     For performance reason do both.
   
     Use 0.499999999 so 0 remains as 0.  This is important for analysis to not get out of
     range.  */
  static inline
  int_point_t red_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x - (2 / (coord_t)3)), nearest_int (scr.y)};
  }
  static inline
  int_point_t red_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x - (2 / (coord_t)3), &xx);
    off->y = my_modf (scr.y, &yy);
    return {xx, yy};
  }
  static inline
  int_point_t green_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x), nearest_int (scr.y)};
  }
  static inline
  int_point_t green_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x, &xx);
    off->y = my_modf (scr.y, &yy);
    return {xx, yy};
  }
  static inline
  int_point_t blue_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x - (1 / (coord_t)3)), nearest_int (scr.y)};
  }
  static inline
  int_point_t blue_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x - (1 / (coord_t)3), &xx);
    off->y = my_modf (scr.y, &yy);
    return {xx, yy};
  }


  /* Reverse conversion: entry to screen coordinates.   */
  static inline
  point_t red_entry_to_scr (int_point_t e)
  {
    return {(coord_t)e.x + 2 / (coord_t)3, (coord_t)e.y};
  }
  static inline
  point_t green_entry_to_scr (int_point_t e)
  {
    return {(coord_t)e.x, (coord_t)e.y};
  }
  static inline
  point_t blue_entry_to_scr (int_point_t e)
  {
    return {e.x + 1 / (coord_t)3, (coord_t)e.y};
  }
};
}
#endif
