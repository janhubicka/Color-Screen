#ifndef ANALYZE_PAGET_H
#define ANALYZE_PAGET_H
#include "render-to-scr.h"
#include "analyze-base.h"
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
  inline static analyze_base::data_entry to_diagonal_coordinates (analyze_base::data_entry p)
  {
    return {p.x - p.y, p.x + p.y};
  }
  inline static analyze_base::data_entry from_diagonal_coordinates (analyze_base::data_entry p)
  {
    return {p.x + p.y, -p.x + p.y};
  }

  /* Red and green are diagonal, so when doing interpolation we need to account that.  */
  inline static analyze_base::data_entry offset_for_interpolation_red (analyze_base::data_entry e, analyze_base::data_entry off)
  {
    return offset_for_interpolation_green (e, off);
  }
  inline static analyze_base::data_entry offset_for_interpolation_green (analyze_base::data_entry e, analyze_base::data_entry off)
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
  inline static analyze_base::data_entry offset_for_interpolation_blue (analyze_base::data_entry e, analyze_base::data_entry off)
  {
    return e + off;
  }

  static inline
  analyze_base::data_entry red_scr_to_entry (point_t scr)
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
  analyze_base::data_entry red_scr_to_entry (point_t scr, point_t *off)
  {
    return green_scr_to_entry ({scr.x + (coord_t)0.5, scr.y}, off);
  }
  static inline
  analyze_base::data_entry green_scr_to_entry (point_t scr)
  {
    /* convert to diagonal coordinates; round to nearest integer and round back.  */
    point_t p = to_diagonal_coordinates (scr);
    analyze_base::data_entry e = from_diagonal_coordinates ((analyze_base::data_entry){nearest_int (p.x), nearest_int (p.y)});
    e.x /= 2;
    return e;
  }
  static inline
  analyze_base::data_entry green_scr_to_entry (point_t scr, point_t *off)
  {
#if 1
    /* convert to diagonal coordinates; round to nearest integer and round back.  */
    point_t p = to_diagonal_coordinates (scr);
    int xx, yy;
    off->x = my_modf (p.x, &xx);
    off->y = my_modf (p.y, &yy);
    analyze_base::data_entry e = from_diagonal_coordinates ((analyze_base::data_entry){xx,yy});
    e.x /= 2;
    return e;
#else
    off->x = 0;
    off->y = 0;
    return green_scr_to_entry (scr);
#endif
  }
  static inline
  analyze_base::data_entry blue_scr_to_entry (point_t scr)
  {
    return {nearest_int (2*(scr.x-(coord_t)0.25)), nearest_int (2*(scr.y-(coord_t)0.25))};
  }
  static inline
  analyze_base::data_entry blue_scr_to_entry (point_t scr, point_t *off)
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
  point_t red_entry_to_scr (analyze_base::data_entry e)
  {
    if (!(e.y&1))
      return {e.x-(coord_t)0.5, (coord_t)(e.y / 2)};
    else
      return {(coord_t)e.x, (e.y / 2) + (coord_t)0.5};
  }
  static inline
  point_t green_entry_to_scr (analyze_base::data_entry e)
  {
    if (!(e.y&1))
      return {(coord_t)e.x, (coord_t)(e.y / 2)};
    else
      return {e.x + (coord_t)0.5, (e.y / 2) + (coord_t)0.5};
  }
  static inline
  point_t blue_entry_to_scr (analyze_base::data_entry e)
  {
    return {e.x * (coord_t)0.5 + (coord_t)0.25, e.y * (coord_t)0.5 + (coord_t)0.25};
  }
};
class analyze_paget : public analyze_base
{
public:
  /* Two blues per X coordinate.  */
  analyze_paget()
  : analyze_base (0, 1, 0, 1, 1, 1)
  {
  }
  ~analyze_paget()
  {
  }
  luminosity_t &blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width * 2 - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_blue [y * m_width * 2 + x];
    }
  luminosity_t &red (int x, int y)
    { 
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_red [y * m_width + x];
    }
  luminosity_t &green (int x, int y) 
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_green [y * m_width + x];
    }
  rgbdata &rgb_blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width * 2 - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_rgb_blue [y * m_width * 2 + x];
    }
  rgbdata &rgb_red (int x, int y)
    { 
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_rgb_red [y * m_width + x];
    }
  rgbdata &rgb_green (int x, int y) 
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height * 2 - 1);
      return m_rgb_green [y * m_width + x];
    }
  inline void rgb_red_atomic_add (int x, int y, rgbdata val)
  {
    rgbdata &addr = rgb_red(x, y);
#pragma omp atomic
    addr.red += val.red;
#pragma omp atomic
    addr.green += val.green;
#pragma omp atomic
    addr.blue += val.blue;
  }
  inline void rgb_green_atomic_add (int x, int y, rgbdata val)
  {
    rgbdata &addr = rgb_green(x, y);
#pragma omp atomic
    addr.red += val.red;
#pragma omp atomic
    addr.green += val.green;
#pragma omp atomic
    addr.blue += val.blue;
  }
  inline void rgb_blue_atomic_add (int x, int y, rgbdata val)
  {
    rgbdata &addr = rgb_blue(x, y);
#pragma omp atomic
    addr.red += val.red;
#pragma omp atomic
    addr.green += val.green;
#pragma omp atomic
    addr.blue += val.blue;
  }
  rgbdata screen_tile_color (int x, int y)
  {
    return {(red (x, 2*y) + red (x, 2*y+1)) * 0.5, (green (x, 2*y) + green (x, 2*y+1)) * 0.5, (blue (2*x, 2*y) + blue (2*x +1, 2*y) + blue (2*x, 2*y+1) + blue (2*x+1, 2*y+1)) * 0.25};
  }
  void screen_tile_rgb_color (rgbdata &red, rgbdata &green, rgbdata &blue, int x, int y)
  {
     red = (rgb_red (x, 2*y) + rgb_red (x, 2*y+1)) * 0.5;
     green = (rgb_green (x, 2*y) + rgb_green (x, 2*y+1)) * 0.5;
     blue = (rgb_blue (2*x, 2*y) + rgb_blue (2*x +1, 2*y) + rgb_blue (2*x, 2*y+1) + rgb_blue (2*x+1, 2*y+1)) * 0.25;
#if 0
    red = 
    green = rgb_green (x, y);
    blue = rgb_blue (x, y);
#endif
  }
  inline void red_atomic_add (int x, int y, luminosity_t val)
  {
    luminosity_t &addr = red(x, y);
#pragma omp atomic
    addr += val;
  }
  inline void green_atomic_add (int x, int y, luminosity_t val)
  {
    luminosity_t &addr = green(x, y);
#pragma omp atomic
    addr += val;
  }
  inline void blue_atomic_add (int x, int y, luminosity_t val)
  {
    luminosity_t &addr = blue(x, y);
#pragma omp atomic
    addr += val;
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t &diag_green (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return green (xx / 2, yy);
  }
  /* Green pixel in diagonal coordinates.  */
  luminosity_t &diag_red (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return red (xx / 2, yy);
  }
  inline pure_attr rgbdata
  bicubic_interpolate (point_t scr)
  {
    return bicubic_interpolate2<paget_geometry> (scr);
  }
  bool analyze(render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, const screen *screen, int width, int height, int xshift, int yshift, mode mode, luminosity_t collection_threshold, progress_info *progress = NULL);
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
  virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
  bool dump_patch_density (FILE *out);
private:
  bool flatten_attr analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_fast (render_to_scr *render,progress_info *progress);
};
#endif
