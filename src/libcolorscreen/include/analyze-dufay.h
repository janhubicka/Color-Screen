#ifndef ANALYZE_DUFAY_H
#define ANALYZE_DUFAY_H
#include "render-to-scr.h"
#include "analyze-base.h"

struct dufay_geometry
{
  /* There is red strip, green and blue patch per screen organized as follows:
    
     GB
     RR

     We subdivide the red strip into two red patches which are shifted to appear
     half way between the green and blue squares.  This reduces banding if scanner
     influences red strip by neighbouring green or blue color.  */
  static constexpr const int red_width_scale = 2;
  static constexpr const int red_height_scale = 1;
  static constexpr const int green_width_scale = 1;
  static constexpr const int green_height_scale = 1;
  static constexpr const int blue_width_scale = 1;
  static constexpr const int blue_height_scale = 1;
  static constexpr const bool check_range = false;

  /* Used to compute grid for interpolation between neighbouring values.
     Everything is orthogonal, so no translation necessary  */
  inline static analyze_base::data_entry offset_for_interpolation_red (analyze_base::data_entry e, analyze_base::data_entry off)
  {
    return e + off;
  }
  inline static analyze_base::data_entry offset_for_interpolation_green (analyze_base::data_entry e, analyze_base::data_entry off)
  {
    return e + off;
  }
  inline static analyze_base::data_entry offset_for_interpolation_blue (analyze_base::data_entry e, analyze_base::data_entry off)
  {
    return e + off;
  }

  /* Convert screen coordinates to data entry, possibly with offset for interpolation.
     For performance reason do both.  */
  static inline
  analyze_base::data_entry red_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x * 2 - (coord_t)0.5), nearest_int (scr.y - (coord_t)0.5)};
  }
  static inline
  analyze_base::data_entry red_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x * 2 - (coord_t)0.5, &xx);
    off->y = my_modf (scr.y - (coord_t)0.5, &yy);
    return {xx, yy};
  }
  static inline
  analyze_base::data_entry green_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x), nearest_int (scr.y)};
  }
  static inline
  analyze_base::data_entry green_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x, &xx);
    off->y = my_modf (scr.y, &yy);
    return {xx, yy};
  }
  static inline
  analyze_base::data_entry blue_scr_to_entry (point_t scr)
  {
    return {nearest_int (scr.x - (coord_t)0.5), nearest_int (scr.y)};
  }
  static inline
  analyze_base::data_entry blue_scr_to_entry (point_t scr, point_t *off)
  {
    int xx, yy;
    off->x = my_modf (scr.x - (coord_t)0.5, &xx);
    off->y = my_modf (scr.y, &yy);
    return {xx, yy};
  }


  /* Reverse conversion: entry to screen coordinates.   */
  static inline
  point_t red_entry_to_scr (analyze_base::data_entry e)
  {
    return {e.x * (coord_t)0.5 + (coord_t)0.25, e.y + (coord_t)0.5};
  }
  static inline
  point_t green_entry_to_scr (analyze_base::data_entry e)
  {
    return {(coord_t)e.x, (coord_t)e.y};
  }
  static inline
  point_t blue_entry_to_scr (analyze_base::data_entry e)
  {
    return {e.x+ (coord_t)0.5, (coord_t)e.y};
  }
};

template class analyze_base_worker <dufay_geometry>;

class analyze_dufay : public analyze_base_worker <dufay_geometry>
{
public:
  analyze_dufay()
  /* We store two reds per X coordinate.  */
  : analyze_base_worker (1, 0, 0, 0, 0, 0)
  {
  }
  ~analyze_dufay()
  {
  }

  bool analyze_contrast (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, progress_info *progress = NULL);
  luminosity_t compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress);
private:
};
#endif
