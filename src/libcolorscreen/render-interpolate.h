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
#include "analyze-strips.h"
#include "lru-cache.h"
#include "include/imagedata.h"
#include "include/scr-to-img-parameters.h"

namespace colorscreen
{

struct analyzer_params
{
  uint64_t img_id;
  uint64_t graydata_id;
  uint64_t simulated_screen_id;
  uint64_t screen_id;
  luminosity_t gamma;
  // int width, height, xshift, yshift;
  enum analyze_base::mode mode;
  luminosity_t collection_threshold;
  uint64_t mesh_trans_id;
  scr_to_img_parameters params;

  const image_data *img;
  const screen *scr;
  class render_to_scr *render;
  class scr_to_img *scr_to_img_map;
  struct simulated_screen *simulated_screen_ptr;

  bool
  operator== (const analyzer_params &o) const
  {
    if (mode != o.mode || mesh_trans_id != o.mesh_trans_id
	|| simulated_screen_id != o.simulated_screen_id
        || (!mesh_trans_id && params != o.params)
        || params.type != o.params.type)
      return false;
    if (mode == analyze_base::color || mode == analyze_base::precise_rgb)
      {
        if (img_id != o.img_id || gamma != o.gamma)
          return false;
      }
    else if (graydata_id != o.graydata_id)
      return false;
    if (mode == analyze_base::fast || mode == analyze_base::color)
      return true;
    return screen_id == o.screen_id
           && collection_threshold == o.collection_threshold;
  }
};

analyze_dufay * get_new_dufay_analysis (struct analyzer_params &, int, int, int, int, progress_info *);
analyze_paget * get_new_paget_analysis (struct analyzer_params &, int, int, int, int, progress_info *);
analyze_strips * get_new_strips_analysis (struct analyzer_params &, int, int, int, int, progress_info *);
typedef std::function <bool (coord_t, coord_t, rgbdata)> analyzer;
typedef std::function <bool (coord_t, coord_t, rgbdata, rgbdata, rgbdata)> rgb_analyzer;
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval);
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
  pure_attr inline rgbdata sample_pixel_img (coord_t x, coord_t y) const
  {
    point_t p = m_scr_to_img.to_scr ({(coord_t)x, (coord_t)y});
    return sample_pixel_scr (p.x, p.y);
  }
  pure_attr inline rgbdata fast_sample_pixel_img (int x, int y) const
  {
    point_t p = m_scr_to_img.to_scr ({x+(coord_t)0.5, y+(coord_t)0.5});
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
  typedef lru_tile_cache<analyzer_params, analyze_dufay, analyze_dufay *, get_new_dufay_analysis, 2> dufay_analyzer_cache_t;
  typedef lru_tile_cache<analyzer_params, analyze_paget, analyze_paget *, get_new_paget_analysis, 2> paget_analyzer_cache_t;
  typedef lru_tile_cache<analyzer_params, analyze_strips, analyze_strips *, get_new_strips_analysis, 2> strips_analyzer_cache_t;
private:
  rgbdata compensate_saturation_loss_scr (point_t p, rgbdata c) const;
  rgbdata compensate_saturation_loss_img (point_t p, rgbdata c) const;
  render_to_scr::screen_cache_t::cached_ptr m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;
  bool m_original_color;
  bool m_unadjusted;
  bool m_profiled;
  bool m_precise_rgb;

  dufay_analyzer_cache_t::cached_ptr m_dufay;
  paget_analyzer_cache_t::cached_ptr m_paget;
  strips_analyzer_cache_t::cached_ptr m_strips;
  color_matrix m_saturation_matrix;
  color_matrix profile_matrix;
  // Proportions after premutatoins we do to handle screen variants.
  rgbdata m_interpolation_proportions;
};
bool analyze_patches (analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int xmin, int ymin, int xmax, int ymax, progress_info *progress);
bool analyze_rgb_patches (rgb_analyzer analyze, const char *task, image_data &img, render_parameters &rparam, scr_to_img_parameters &param, bool screen, int xmin, int ymin, int xmax, int ymax, progress_info *progress);
}
#endif
