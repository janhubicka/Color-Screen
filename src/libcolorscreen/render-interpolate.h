#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include "include/render-to-scr.h"
#include "include/screen.h"
#include "include/analyze-dufay.h"
#include "include/analyze-paget.h"
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval, bool screen_compensation, bool adjust_luminosity, bool unadjusted = false);
  ~render_interpolate ();
  bool precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress);
  rgbdata sample_pixel_final (coord_t x, coord_t y)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    return sample_pixel_scr (xx, yy);
  }
  void render_pixel_final (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.final_to_scr (x - m_final_xshift, y - m_final_yshift, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
  void render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
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
  void render_pixel_scr (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_scr (x, y);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
  void render_hdr_pixel_scr (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    rgbdata d = sample_pixel_scr (x, y);
    set_hdr_color (d.red, d.green, d.blue, r, g, b);
  }
  pure_attr flatten_attr rgbdata sample_pixel_scr (coord_t x, coord_t y);
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
class render_diff : public render_to_scr
{
public:
  render_diff (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval)
   : render_to_scr (param, img, rparam, dst_maxval),
     r1 (param, img, rparam, dst_maxval, false, false, false), r2 (param, img, rparam, dst_maxval, false, false, false)
  {
    r2.original_color (true);
  }
  ~render_diff ()
  {
  }
  bool
  precompute_img_range (int x1, int x2, int y1, int y2, progress_info *progress)
  {
    if (!render_to_scr::precompute (false, true, 0, 1, 0, 1, progress))
      return false;
    if (!r1.precompute_img_range (x1, x2, y1, y1, progress))
      return false;
    return r2.precompute_img_range (x1, x2, y1, y1, progress);
  }
  bool
  precompute_all (progress_info *progress)
  {
    if (!render_to_scr::precompute (false, true, 0, 1, 0, 1, progress))
      return false;
    if (!r1.precompute_all (progress))
      return false;
    return r2.precompute_all (progress);
  }
  rgbdata
  sample_pixel_scr (coord_t x, coord_t y)
  {
    rgbdata c1 = r1.sample_pixel_scr (x, y);
    rgbdata c2 = r2.sample_pixel_scr (x, y);
    return {0.25-4 * (c1.red - c2.red),
	    0.25-4 * (c1.green - c2.green),
	    0.25-4 * (c1.blue - c2.blue)};
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    rgbdata d = sample_pixel_scr (xx, yy);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
private:
  render_interpolate r1;
  render_interpolate r2;
};
#endif
