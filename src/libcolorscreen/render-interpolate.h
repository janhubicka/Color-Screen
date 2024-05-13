#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include "include/render-to-scr.h"
#include "include/screen.h"
#include "include/analyze-dufay.h"
#include "include/analyze-paget.h"
#include "include/histogram.h"
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval);
  ~render_interpolate ();
  bool precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress);
  rgbdata sample_pixel_final (coord_t x, coord_t y)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    return sample_pixel_scr (xx, yy);
  }
  void set_render_type (render_type_parameters rtparam);
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
  pure_attr rgbdata sample_pixel_scr (coord_t x, coord_t y);
  rgbdata sample_pixel_img (int x, int y)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    return sample_pixel_scr (xx, yy);
  }
  void original_color (bool profiled)
  {
    if (m_img.rgbdata)
      {
	m_original_color = true;
	if (profiled)
	{
	  profile_matrix = m_params.get_profile_matrix (m_scr_to_img.patch_proportions ());
	  m_profiled = true;
	}
      }
  }
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);

  void collect_histogram (rgb_histogram &histogram, int xmin, int xmax, int ymin, int ymax, progress_info *progress = NULL);
  void collect_rgb_histograms (rgb_histogram &red_histogram, rgb_histogram &green_histogram, rgb_histogram &blue_histogram, int xmin, int xmax, int ymin, int ymax, progress_info *progress = NULL);
private:
  screen *m_screen;
  bool m_screen_compensation;
  bool m_adjust_luminosity;
  bool m_original_color;
  bool m_unadjusted;
  bool m_profiled;
  analyze_dufay *m_dufay;
  analyze_paget *m_paget;
  color_matrix profile_matrix;
};
#endif
