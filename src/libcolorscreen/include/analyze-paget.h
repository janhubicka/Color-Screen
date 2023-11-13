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
  bool analyze(render_to_scr *render, image_data *img, scr_to_img *scr_to_img, screen *screen, int width, int height, int xshift, int yshift, bool precise, luminosity_t collection_threshold, progress_info *progress = NULL);
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
  virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
private:
  bool flatten_attr analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_fast (render_to_scr *render,progress_info *progress);
};
#endif
