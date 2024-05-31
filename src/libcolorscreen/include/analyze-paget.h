#ifndef ANALYZE_PAGET_H
#define ANALYZE_PAGET_H
#include "render-to-scr.h"
#include "analyze-base.h"
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
  rgbdata screen_tile_color (int x, int y)
  {
    return {(red (x, 2*y) + red (x, 2*y+1)) * 0.5, (green (x, 2*y) + green (x, 2*y+1)) * 0.5, (blue (2*x, 2*y) + blue (2*x +1, 2*y) + blue (2*x, 2*y+1) + blue (2*x+1, 2*y+1)) * 0.25};
  }
  void screen_tile_rgb_color (rgbdata &red, rgbdata &green, rgbdata &blue, int x, int y)
  {
	  abort ();
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
  /* Diagonal cooredinates have coordiate vectors (0.5,0.5) and (-0.5,0.5)  */
  inline static void to_diagonal_coordinates (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    *xx = x - y;
    *yy = x + y;
  }
  inline static void from_diagonal_coordinates (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    *xx = x + y;
    *yy = -x + y;
  }
  inline static void to_diagonal_coordinates (int x, int y, int *xx, int *yy)
  {
    *xx = x - y;
    *yy = x + y;
  }
  inline static void from_diagonal_coordinates (int x, int y, int *xx, int *yy)
  {
    *xx = x + y;
    *yy = -x + y;
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
  bool analyze(render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, const screen *screen, int width, int height, int xshift, int yshift, mode mode, luminosity_t collection_threshold, progress_info *progress = NULL);
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
  virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
  bool dump_patch_density (FILE *out);
private:
  bool flatten_attr analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_fast (render_to_scr *render,progress_info *progress);
};
#endif
