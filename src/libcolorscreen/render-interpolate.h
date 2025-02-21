#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include <functional>
#include "include/histogram.h"
#include "solver.h"
#include "render-to-scr.h"
#include "screen.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
#include "analyze-strips.h"
namespace colorscreen
{
typedef std::function <bool (coord_t, coord_t, rgbdata)> analyzer;
typedef std::function <bool (coord_t, coord_t, rgbdata, rgbdata, rgbdata)> rgb_analyzer;
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval);
  ~render_interpolate ();
  bool precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress);
  pure_attr inline rgbdata sample_pixel_final (coord_t x, coord_t y) const
  {
    point_t p = m_scr_to_img.final_to_scr ({x - get_final_xshift (), y - get_final_yshift ()});
    return sample_pixel_scr (p.x, p.y);
  }
  void set_render_type (render_type_parameters rtparam);
  void set_precise_rgb ()
  {
    m_precise_rgb = true;
  }
  void set_unadjusted ()
  {
    m_unadjusted = true;
  }
  void set_predictive ()
  {
    m_unadjusted = true;
  }
  void set_adjust_luminosity ()
  {
    m_unadjusted = true;
  }
  bool precompute_all (progress_info *progress)
  {
    int xshift, yshift, width, height;
    m_scr_to_img.get_range (0, 0, m_img.width, m_img.height, &xshift, &yshift, &width, &height);
    return precompute (-xshift, -yshift, -xshift + width, -yshift + height, progress);
  }
  bool precompute_img_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2, progress_info *progress)
  {
    int xshift, yshift, width, height;
    m_scr_to_img.get_range (x1, y1, x2, y2, &xshift, &yshift, &width, &height);
    return precompute (-xshift, -yshift, -xshift + width, -yshift + height, progress);
  }
  pure_attr rgbdata sample_pixel_scr (coord_t x, coord_t y) const;
  pure_attr inline rgbdata sample_pixel_img (int x, int y) const
  {
    point_t p = m_scr_to_img.to_scr ({(coord_t)x, (coord_t)y});
    return sample_pixel_scr (p.x, p.y);
  }
  void original_color (bool profiled)
  {
    if (m_img.rgbdata)
      {
	m_original_color = true;
	if (profiled)
	{
	  profile_matrix = m_params.get_profile_matrix (m_scr_to_img.patch_proportions (&m_params));
	  m_profiled = true;
	}
      }
  }
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);

  bool analyze_patches (analyzer, const char *, bool screen, int xmin, int xmax, int ymin, int ymax, progress_info *progress = NULL);
  bool analyze_rgb_patches (rgb_analyzer, const char *, bool screen, int xmin, int xmax, int ymin, int ymax, progress_info *progress = NULL);

  bool dump_patch_density (FILE *);
  //bool finetune (render_parameters &rparam, solver_parameters::point_t &point, int x, int y, progress_info *progress);
  //void collect_rgb_histograms (rgb_histogram &red_histogram, rgb_histogram &green_histogram, rgb_histogram &blue_histogram, int xmin, int xmax, int ymin, int ymax, progress_info *progress = NULL);
private:
  rgbdata compensate_saturation_loss_scr (point_t p, rgbdata c) const;
  rgbdata compensate_saturation_loss_img (point_t p, rgbdata c) const;
  screen *m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;
  bool m_original_color;
  bool m_unadjusted;
  bool m_profiled;
  bool m_precise_rgb;
  analyze_dufay *m_dufay;
  analyze_paget *m_paget;
  analyze_strips *m_strips;
  color_matrix m_saturation_matrix;
  color_matrix profile_matrix;
};
bool analyze_patches (analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int xmin, int ymin, int xmax, int ymax, progress_info *progress);
bool analyze_rgb_patches (rgb_analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int xmin, int ymin, int xmax, int ymax, progress_info *progress);
}
#endif
