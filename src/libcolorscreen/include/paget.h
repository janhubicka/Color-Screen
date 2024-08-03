#ifndef PAGET_H
#define PAGET_H
#include "base.h"
namespace colorscreen
{
struct paget_geometry
{
  static constexpr const int red_width_scale = 1;
  static constexpr const int red_height_scale = 2;
  static constexpr const int green_width_scale = 1;
  static constexpr const int green_height_scale = 2;
  static constexpr const int blue_width_scale = 2;
  static constexpr const int blue_height_scale = 2;
  static constexpr const bool check_range = true; 

  /* Screen is 45 degree rotated bayer filster with blue and green exchanged.
     One tile of the screen is:

     G   R   g
       B   B
     R   G   r
       B   B 
     g   r   g

     With uppercase denoting what we record


    Normal coordinates
         G                   R                   g
     0.00,0.00           0.50,0.00           1.00,0.00
                   B                   B
               0.25,0.25           0.75,0.25
         R                   G                   r
     0.00,0.50           0.50,0.50           1.00,0.50
                   B                   B
               0.25,0.75           0.75,0.75
         g                   r                   g
     0.00,1.00           0.50,1.00           1.00,1.00

    Diagonal coordinates converts the coordinates to usual bayer filter:
  
         G                   R                   G
     0.00,0.00           0.50,0.50           1.00,1.00
                   B                   B
               0.00,0.50           0.50,1.00
         R                   G                   R
    -0.50,0.50           0.00,1.00           0.50,1.50
                   B                   B
               0.00,0.50           0.50,1.00
         g                   r                   g
    -1.00,1.00          -0.50,1.50           0.00,2.00

    All green elements diagonal coordinates [x,y] for some integers x,y.
    All red elements are either [x-0.5,y] or [x+0.5,y+0.5]  */

        

  /* Diagonal cooredinates have coordiate vectors (0.5,0.5) and (-0.5,0.5)  */
  inline static point_t to_diagonal_coordinates (point_t p)
  {
    return {p.x - p.y, p.x + p.y};
  }
  inline static point_t from_diagonal_coordinates (point_t p)
  {
    return {p.x + p.y, -p.x + p.y};
  }
  inline static int_point_t to_diagonal_coordinates (int_point_t p)
  {
    return {p.x - p.y, p.x + p.y};
  }
  inline static int_point_t from_diagonal_coordinates (int_point_t p)
  {
    return {p.x + p.y, -p.x + p.y};
  }

  /* Red and green are diagonal, so when doing interpolation we need to account that.  */
  inline static int_point_t offset_for_interpolation_red (int_point_t e, int_point_t off)
  {
    return offset_for_interpolation_green (e, off);
  }
  inline static int_point_t offset_for_interpolation_green (int_point_t e, int_point_t off)
  {
    /* Undo division by 2.  We know parity from y.  */
    e.x = e.x * 2 + (e.y & 1);
    /* Undo from_diagonal_coordinates.  */
    e.x = (e.x - e.y) / 2;
    e.y = e.y + e.x;
    /* Offset and convert back.  */
    e = e + off;
    e = from_diagonal_coordinates (e);
    e.x /= 2;
    return e;
  }
  inline static int_point_t offset_for_interpolation_blue (int_point_t e, int_point_t off)
  {
    return e + off;
  }

  static inline
  int_point_t red_scr_to_entry (point_t scr)
  {
    /* Adding 0.5 to screen coordinaes makes reds to appear as follows
         G                   R                   G
                         1.00,1.00          
                   B                   B
             
         R                   G                   R
     0.00,1.00                               1.00,1.00
                   B                   B
                                             
         g                   r                   g
                         0.00,1.00                    */
    return green_scr_to_entry ({scr.x + (coord_t)0.5, scr.y});
  }
  static inline
  int_point_t red_scr_to_entry (point_t scr, point_t *off)
  {
    return green_scr_to_entry ({scr.x + (coord_t)0.5, scr.y}, off);
  }
  static inline
  int_point_t green_scr_to_entry (point_t scr)
  {
    /* convert to diagonal coordinates; round to nearest integer and round back.  */
    point_t p = to_diagonal_coordinates (scr);
    int_point_t e = from_diagonal_coordinates ((int_point_t){nearest_int (p.x), nearest_int (p.y)});
    e.x /= 2;
    return e;
  }
  static inline
  int_point_t green_scr_to_entry (point_t scr, point_t *off)
  {
    /* convert to diagonal coordinates; round to nearest integer and round back.  */
    point_t p = to_diagonal_coordinates (scr);
    int xx, yy;
    off->x = my_modf (p.x, &xx);
    off->y = my_modf (p.y, &yy);
    int_point_t e = from_diagonal_coordinates ((int_point_t){xx,yy});
    e.x /= 2;
    return e;
  }
  static inline
  int_point_t blue_scr_to_entry (point_t scr)
  {
    return {nearest_int (2*(scr.x-(coord_t)0.25)), nearest_int (2*(scr.y-(coord_t)0.25))};
  }
  static inline
  int_point_t blue_scr_to_entry (point_t scr, point_t *off)
  {
#if 0
    off->x = 0;
    off->y = 0;
    return blue_scr_to_entry (scr);
#else
    int xx, yy;
    off->x = my_modf (2*(scr.x-(coord_t)0.25), &xx);
    off->y = my_modf (2*(scr.y-(coord_t)0.25), &yy);
    return {xx, yy};
#endif
  }
  static inline
  point_t red_entry_to_scr (int_point_t e)
  {
    if (!(e.y&1))
      return {e.x-(coord_t)0.5, (coord_t)(e.y / 2)};
    else
      return {(coord_t)e.x, (e.y / 2) + (coord_t)0.5};
  }
  static inline
  point_t green_entry_to_scr (int_point_t e)
  {
    if (!(e.y&1))
      return {(coord_t)e.x, (coord_t)(e.y / 2)};
    else
      return {e.x + (coord_t)0.5, (e.y / 2) + (coord_t)0.5};
  }
  static inline
  point_t blue_entry_to_scr (int_point_t e)
  {
    return {e.x * (coord_t)0.5 + (coord_t)0.25, e.y * (coord_t)0.5 + (coord_t)0.25};
  }
};
}
#endif
