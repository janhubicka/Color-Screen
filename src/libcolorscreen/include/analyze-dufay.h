#ifndef ANALYZE_DUFAY_H
#define ANALYZE_DUFAY_H
#include "render-to-scr.h"
#include "analyze-base.h"
class analyze_dufay : public analyze_base
{
public:
  analyze_dufay()
  /* We store two reds per X coordinate.  */
  : analyze_base (1, 0, 0, 0, 0, 0)
  {
  }
  ~analyze_dufay()
  {
  }
  luminosity_t &blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_blue [y * m_width + x];
    }
  luminosity_t &red (int x, int y)
    {
      x = std::min (std::max (x, 0), 2 * m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_red [y * m_width * 2 + x];
    }

  luminosity_t &green (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_green [y * m_width + x];
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
  rgbdata &rgb_blue (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_rgb_blue [y * m_width + x];
    }
  rgbdata &rgb_red (int x, int y)
    {
      x = std::min (std::max (x, 0), 2 * m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
      return m_rgb_red [y * m_width * 2 + x];
    }

  rgbdata &rgb_green (int x, int y)
    {
      x = std::min (std::max (x, 0), m_width - 1);
      y = std::min (std::max (y, 0), m_height - 1);
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
    return {(red (2*x, y) + red (2*x+1, y)) * 0.5, green (x, y), blue (x, y)};
  }
  void screen_tile_rgb_color (rgbdata &red, rgbdata &green, rgbdata &blue, int x, int y)
  {
    red = (rgb_red (2*x, y) + rgb_red (2*x+1, y)) * 0.5;
    green = rgb_green (x, y);
    blue = rgb_blue (x, y);
  }

  bool analyze(render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, screen const *screen, int width, int height, int xshift, int yshift, enum mode mode, luminosity_t collection_threshold, progress_info *progress = NULL);
  bool analyze_contrast (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, progress_info *progress = NULL);
  luminosity_t compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress);
private:
  bool flatten_attr analyze_precise (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_precise_rgb (scr_to_img *scr_to_img, render_to_scr *render, const screen *screen, luminosity_t collection_threshold, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_color (scr_to_img *scr_to_img, render_to_scr *render, luminosity_t *w_red, luminosity_t *w_green, luminosity_t *w_blue, int minx, int miny, int maxx, int maxy, progress_info *progress);
  bool flatten_attr analyze_fast (render_to_scr *render,progress_info *progress);
};
#endif
