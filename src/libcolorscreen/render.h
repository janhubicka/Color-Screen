#ifndef RENDER_H
#define RENDER_H
#include <memory>
#include <math.h>
#include <assert.h>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "include/base.h"
#include "include/imagedata.h"
#include "include/color.h"
#include "include/progress-info.h"
#include "include/sensitivity.h"
#include "include/tone-curve.h"
#include "include/scr-to-img.h"
#include "include/render-parameters.h"
#include "include/render-type-parameters.h"
#include "include/stitch.h"
#include "out-color-adjustments.h"
#include "backlight-correction.h"
#include "mem-luminosity.h"
#include "lru-cache.h"
#include <vector>
#include "cubic-interpolate.h"

namespace colorscreen
{
class histogram;

/* Helper for downscaling template for color rendering.
   Account LUM * SCALE to DATA.  */
inline void
account_rgb_pixel (rgbdata *data, rgbdata lum, luminosity_t scale)
{
  data->red += lum.red * scale;
  data->green += lum.green * scale;
  data->blue += lum.blue * scale;
}

/* Helper for downscaling template for grayscale rendering.
   Account LUM * SCALE to DATA.  */
inline void
account_pixel (luminosity_t *data, luminosity_t lum, luminosity_t scale)
{
  *data += lum * scale;
}

/* Base class for rendering routines.  */
class render
{
public:
  /* Initialize renderer for image IMG using parameters RPARAM and
     outputting to range 0..DSTMAXVAL.  */
  render (const image_data &img, const render_parameters &rparam, int dstmaxval)
      : out_color (dstmaxval), m_img (img), m_params (rparam),
        m_gray_data_id (img.id), m_sharpened_data (nullptr),
        m_sharpened_data_holder (), m_maxval (img.data ? img.maxval : 65535),
        m_backlight_correction (), m_backlight_correction_id (0)
  {
  }

  /* Destroy renderer.  */
  virtual ~render () = default;

  /* Determine grayscale value at a given position in the image.  */
  pure_attr inline luminosity_t get_img_pixel (coord_t x, coord_t y) const;

  /* Determine grayscale value at a given position in the image without
     luminosity adjustments.  */
  pure_attr inline luminosity_t get_unadjusted_img_pixel (coord_t x, coord_t y) const;

  /* Determine RGB value at a given position in the image.  */
  inline void get_img_rgb_pixel (coord_t x, coord_t y, luminosity_t *r,
                                 luminosity_t *g, luminosity_t *b) const;

  /* Determine RGB value at a given position in the image without
     luminosity adjustments.  */
  inline void get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp,
                                            luminosity_t *r, luminosity_t *g,
                                            luminosity_t *b) const;

  /* Sample square patch with center XC and YC and corner offsets.  */
  pure_attr luminosity_t
  sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2,
                     coord_t y2) const;

  /* Quickly determine grayscale value at a given position in the image.  */
  pure_attr inline luminosity_t fast_get_img_pixel (int x, int y) const;

  static const int num_color_models = render_parameters::color_model_max;

  /* Fetch lookup tables for image IMG and given GAMMA.  */
  static bool get_lookup_tables (std::shared_ptr<luminosity_t[]> *ret,
                                 luminosity_t gamma, const image_data *img,
                                 progress_info *progress = nullptr);

  /* Get sharpened grayscale value at index X, Y.  */
  pure_attr inline luminosity_t get_data (int x, int y) const;

  /* Get unadjusted sharpened grayscale value at index X, Y.  */
  pure_attr inline luminosity_t get_unadjusted_data (int x, int y) const;

  /* Adjust luminosity LUM considering infrared sensitivity.  */
  pure_attr inline luminosity_t adjust_luminosity_ir (luminosity_t lum) const;

  /* Get sharpened color values at index X, Y.  */
  pure_attr inline luminosity_t get_data_red (int x, int y) const;
  pure_attr inline luminosity_t get_data_green (int x, int y) const;
  pure_attr inline luminosity_t get_data_blue (int x, int y) const;

  /* Get linearized color values at index X, Y.  */
  pure_attr inline luminosity_t get_linearized_data_red (int x, int y) const;
  pure_attr inline luminosity_t get_linearized_data_green (int x, int y) const;
  pure_attr inline luminosity_t get_linearized_data_blue (int x, int y) const;

  /* Precompute all data needed for rendering.  */
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, bool normalized_patches,
                                  rgbdata patch_proportions,
                                  progress_info *progress);

  /* Get linearized RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_linearized_rgb_pixel (int x, int y) const
  {
    if (colorscreen_checking)
      assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
    rgbdata d = {m_rgb_lookup_table [0][m_img.rgbdata[y][x].r],
		 m_rgb_lookup_table [1][m_img.rgbdata[y][x].g],
		 m_rgb_lookup_table [2][m_img.rgbdata[y][x].b]};
    return d;
  }

  /* Get unadjusted RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_unadjusted_rgb_pixel (int x, int y) const
  {
    rgbdata d = get_linearized_rgb_pixel (x, y);
    if (m_backlight_correction)
      {
	d.red = m_backlight_correction->apply (d.red, x, y, backlight_correction_parameters::red, true);
	d.green = m_backlight_correction->apply (d.green, x, y, backlight_correction_parameters::green, true);
	d.blue = m_backlight_correction->apply (d.blue, x, y, backlight_correction_parameters::blue, true);
      }
    return d;
  }

  /* Adjust RGB value D based on dark point and exposure.  */
  pure_attr inline rgbdata
  adjust_rgb (rgbdata d) const
  {
    d.red = (d.red - m_params.dark_point) * m_params.scan_exposure;
    d.green = (d.green - m_params.dark_point) * m_params.scan_exposure;
    d.blue = (d.blue - m_params.dark_point) * m_params.scan_exposure;
    return d;
  }

  /* Get final RGB pixel value at index X, Y.  */
  pure_attr inline rgbdata
  get_rgb_pixel (int x, int y) const
  {
    return adjust_rgb (get_unadjusted_rgb_pixel (x, y));
  }

  /* Fetch matrix translating RGB values to XYZ.  */
  color_matrix get_rgb_to_xyz_matrix (bool normalized_patches,
                                      rgbdata patch_proportions)
  {
    return m_params.get_rgb_to_xyz_matrix (&m_img, normalized_patches,
                                           patch_proportions);
  }

  /* Placeholder for final range computation.  */
  void compute_final_range () {}

  /* Compute grayscale data for downscaled region.  */
  void get_gray_data (luminosity_t *graydata, coord_t x, coord_t y, int width,
                      int height, coord_t pixelsize, progress_info *progress);

  /* Return number of pixel computations considered profitable for OMP.  */
  size_t openmp_size ()
  {
    return 128 * 1024;
  }

  /* Fetch histogram for the current scan area.  */
  std::shared_ptr<histogram> get_image_layer_histogram (progress_info *progress = nullptr);

  /* Output color adjustments.  */
  out_color_adjustments out_color;

  /* Generic image downscaling routine using bilinear weights.  */
  template<typename D, typename T, T (D::*get_pixel) (int x, int y) const,
           void (*account_pixel) (T *, T, luminosity_t)>
  bool downscale (T *data, coord_t x, coord_t y, int width, int height,
                  coord_t pixelsize, progress_info *progress);

protected:
  /* Compute color data for downscaled region.  */
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width,
                       int height, coord_t pixelsize, progress_info *progress);

  /* Inner loop for image downscaling processing single line.  */
  template<typename T, typename D, T (D::*get_pixel) (int x, int y) const,
           void (*account_pixel) (T *, T, luminosity_t)>
  void process_line (T *data, int *pixelpos, luminosity_t *weights,
		     int xstart, int xend,
		     int width, int height,
		     int py, int yy,
		     bool y0, bool y1,
		     luminosity_t scale, luminosity_t yweight);

  /* Inner loop for image downscaling processing single pixel.  */
  template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
  inline always_inline_attr void
  process_pixel (T *data, int width, int height, int px, int py, bool x0,
                 bool x1, bool y0, bool y1, T val, luminosity_t scale,
                 luminosity_t xweight, luminosity_t yweight);

  /* Scanned image.  */
  const image_data &m_img;

  /* Rendering parameters.  */
  render_parameters m_params;

  /* ID of graydata computed.  */
  uint64_t m_gray_data_id;

  /* Sharpened data we render from.  */
  mem_luminosity_t *m_sharpened_data;

  /* Wrapping class to cause proper destruction.  */
  std::shared_ptr<class sharpened_data> m_sharpened_data_holder;

  /* Maximal value in M_IMG.  */
  int m_maxval;

  /* Translates input rgb channel values into normalized range.  */
  std::shared_ptr<luminosity_t[]> m_rgb_lookup_table[3];

  /* Backlight correction handler.  */
  std::shared_ptr<backlight_correction> m_backlight_correction;
  uint64_t m_backlight_correction_id;

  /* Film sensitivity handlers for simulated contact copies.  */
  std::unique_ptr <film_sensitivity> m_sensitivity;
  std::unique_ptr <hd_curve> m_sensitivity_hd_curve;
  std::unique_ptr <precomputed_function<luminosity_t>> m_adjust_luminosity;
};

/* Get sharpened grayscale value at index X, Y without adjustments.  */
pure_attr inline luminosity_t always_inline_attr
render::get_unadjusted_data (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_sharpened_data [y * m_img.width + x];
}

/* Adjust luminosity LUM considering infrared sensitivity and dark point.  */
pure_attr inline luminosity_t always_inline_attr
render::adjust_luminosity_ir (luminosity_t lum) const
{
  if (m_adjust_luminosity)
    return m_adjust_luminosity->apply (lum);
  lum = (lum - m_params.dark_point) * m_params.scan_exposure;
  if (m_sensitivity)
    lum = m_sensitivity->apply (lum);
  return lum;
}

/* Get sharpened grayscale value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_data (int x, int y) const
{
  return adjust_luminosity_ir (get_unadjusted_data (x, y));
}

/* Get linearized red channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_red (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [0][m_img.rgbdata[y][x].r];
}

/* Get linearized green channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_green (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [1][m_img.rgbdata[y][x].g];
}

/* Get linearized blue channel value at index X, Y.  */
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_blue (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [2][m_img.rgbdata[y][x].b];
}

/* Get sharpened red channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_red (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [0][m_img.rgbdata[y][x].r];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::red, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Get sharpened green channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_green (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [1][m_img.rgbdata[y][x].g];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::green, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Get sharpened blue channel value at index X, Y.  */
pure_attr inline luminosity_t
render::get_data_blue (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [2][m_img.rgbdata[y][x].b];
  if (m_backlight_correction)
    v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::blue, true);
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  return v;
}

/* Compute color in linear HDR image for values R, G, B.  Store result in RR, GG, BB.  */
inline void
out_color_adjustments::linear_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b,
                                         luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
{
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  if (m_spectrum_dyes_to_xyz)
    {
      if (m_spectrum_dyes_to_xyz->red_characteristic_curve)
	r=m_spectrum_dyes_to_xyz->red_characteristic_curve->apply (r);
      if (m_spectrum_dyes_to_xyz->green_characteristic_curve)
	g=m_spectrum_dyes_to_xyz->green_characteristic_curve->apply (g);
      if (m_spectrum_dyes_to_xyz->blue_characteristic_curve)
	b=m_spectrum_dyes_to_xyz->blue_characteristic_curve->apply (b);
      xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
      m_color_matrix2.apply_to_rgb (c.x, c.y, c.z, &r, &g, &b);
    }

  /* Apply DNG-style tone curve correction.  */
  if (m_tone_curve)
    {
      rgbdata c = {r,g,b};
      assert (!m_tone_curve->is_linear ());
      c = m_tone_curve->apply_to_rgb (c);
      color_matrix cm;
      pro_photo_rgb_xyz_matrix m1;
      cm = m1 * cm;
      bradford_d50_to_d65_matrix m2;
      cm = m2 * cm;
      xyz_srgb_matrix m;
      cm = m * cm;
      cm.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green, &c.blue);
      *rr = c.red;
      *gg = c.green;
      *bb = c.blue;
      return;
    }

  *rr = r;
  *gg = g;
  *bb = b;
}

/* Compute color in the final gamma for values R, G, B.  Store result in RR, GG, BB.  */
inline void
out_color_adjustments::hdr_final_color (luminosity_t r, luminosity_t g, luminosity_t b,
                                        luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
{
  luminosity_t r1, g1, b1;
  linear_hdr_color (r, g, b, &r1, &g1, &b1);
  *rr = invert_gamma (r1, m_output_gamma);
  *gg = invert_gamma (g1, m_output_gamma);
  *bb = invert_gamma (b1, m_output_gamma);
}

/* Compute color in the final gamma and range 0..m_dst_maxval for values R, G, B.
   Fast version that is not always precise for dark colors and gamma > 1.5.  */
inline void
out_color_adjustments::final_color (luminosity_t r, luminosity_t g, luminosity_t b,
                                    int *rr, int *gg, int *bb) const
{
  linear_hdr_color (r, g, b, &r, &g, &b);
  /* Show gammut warnings.  */
  if (m_gammut_warning && (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1))
    r = g = b = 0.5;
  else
    {
      r = std::clamp (r, (luminosity_t)0.0, (luminosity_t)1.0);
      g = std::clamp (g, (luminosity_t)0.0, (luminosity_t)1.0);
      b = std::clamp (b, (luminosity_t)0.0, (luminosity_t)1.0);
    }
  *rr = m_out_lookup_table->apply (r);
  *gg = m_out_lookup_table->apply (g);
  *bb = m_out_lookup_table->apply (b);
}

/* Compute color in the final gamma and range 0..m_dst_maxval for values R, G, B.
   Slow version.  */
inline void
out_color_adjustments::final_color_precise (luminosity_t r, luminosity_t g, luminosity_t b,
                                            int *rr, int *gg, int *bb) const
{
  if (m_output_gamma == 1)
    {
      final_color (r, g, b, rr, gg, bb);
      return;
    }
  luminosity_t fr, fg, fb;
  hdr_final_color (r, g, b, &fr, &fg, &fb);
  fr = std::clamp (fr, (luminosity_t)0.0, (luminosity_t)1.0);
  fg = std::clamp (fg, (luminosity_t)0.0, (luminosity_t)1.0);
  fb = std::clamp (fb, (luminosity_t)0.0, (luminosity_t)1.0);
  *rr = fr * m_dst_maxval + 0.5;
  *gg = fg * m_dst_maxval + 0.5;
  *bb = fb * m_dst_maxval + 0.5;
}

/* Quickly determine grayscale value at a given position in the image.  */
pure_attr inline luminosity_t
render::fast_get_img_pixel (int x, int y) const
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Determine grayscale value at position XP, YP in the image using bicubic
   interpolation without adjustments.  */
pure_attr inline luminosity_t
render::get_unadjusted_img_pixel (coord_t xp, coord_t yp) const
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      vec_luminosity_t v1 = {get_unadjusted_data (sx-1, sy-1), get_unadjusted_data (sx, sy-1), get_unadjusted_data (sx+1, sy-1), get_unadjusted_data (sx+2, sy-1)};
      vec_luminosity_t v2 = {get_unadjusted_data (sx-1, sy-0), get_unadjusted_data (sx, sy-0), get_unadjusted_data (sx+1, sy-0), get_unadjusted_data (sx+2, sy-0)};
      vec_luminosity_t v3 = {get_unadjusted_data (sx-1, sy+1), get_unadjusted_data (sx, sy+1), get_unadjusted_data (sx+1, sy+1), get_unadjusted_data (sx+2, sy+1)};
      vec_luminosity_t v4 = {get_unadjusted_data (sx-1, sy+2), get_unadjusted_data (sx, sy+2), get_unadjusted_data (sx+1, sy+2), get_unadjusted_data (sx+2, sy+2)};
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      return cubic_interpolate (v[0], v[1], v[2], v[3], rx);
    }
  return 0;
}

/* Determine grayscale value at position XP, YP in the image using bicubic
   interpolation.  */
pure_attr inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp) const
{
  return adjust_luminosity_ir (get_unadjusted_img_pixel (xp, yp));
}

/* Determine RGB value at position XP, YP in the image using bicubic interpolation
   without adjustments.  */
inline flatten_attr void
render::get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r,
                                      luminosity_t *g, luminosity_t *b) const
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      luminosity_t rr, gg, bb;
      rr = cubic_interpolate (cubic_interpolate (get_linearized_data_red ( sx-1, sy-1), get_linearized_data_red (sx-1, sy), get_linearized_data_red (sx-1, sy+1), get_linearized_data_red (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx-0, sy-1), get_linearized_data_red (sx-0, sy), get_linearized_data_red (sx-0, sy+1), get_linearized_data_red (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx+1, sy-1), get_linearized_data_red (sx+1, sy), get_linearized_data_red (sx+1, sy+1), get_linearized_data_red (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_red ( sx+2, sy-1), get_linearized_data_red (sx+2, sy), get_linearized_data_red (sx+2, sy+1), get_linearized_data_red (sx+2, sy+2), ry),
			      rx);
      gg = cubic_interpolate (cubic_interpolate (get_linearized_data_green ( sx-1, sy-1), get_linearized_data_green (sx-1, sy), get_linearized_data_green (sx-1, sy+1), get_linearized_data_green (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx-0, sy-1), get_linearized_data_green (sx-0, sy), get_linearized_data_green (sx-0, sy+1), get_linearized_data_green (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx+1, sy-1), get_linearized_data_green (sx+1, sy), get_linearized_data_green (sx+1, sy+1), get_linearized_data_green (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_green ( sx+2, sy-1), get_linearized_data_green (sx+2, sy), get_linearized_data_green (sx+2, sy+1), get_linearized_data_green (sx+2, sy+2), ry),
			      rx);
      bb = cubic_interpolate (cubic_interpolate (get_linearized_data_blue ( sx-1, sy-1), get_linearized_data_blue (sx-1, sy), get_linearized_data_blue (sx-1, sy+1), get_linearized_data_blue (sx-1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx-0, sy-1), get_linearized_data_blue (sx-0, sy), get_linearized_data_blue (sx-0, sy+1), get_linearized_data_blue (sx-0, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx+1, sy-1), get_linearized_data_blue (sx+1, sy), get_linearized_data_blue (sx+1, sy+1), get_linearized_data_blue (sx+1, sy+2), ry),
			      cubic_interpolate (get_linearized_data_blue ( sx+2, sy-1), get_linearized_data_blue (sx+2, sy), get_linearized_data_blue (sx+2, sy+1), get_linearized_data_blue (sx+2, sy+2), ry),
			      rx);
      if (m_backlight_correction)
	{
	  rr = m_backlight_correction->apply (rr, xp, yp, backlight_correction_parameters::red, true);
	  gg = m_backlight_correction->apply (gg, xp, yp, backlight_correction_parameters::green, true);
	  bb = m_backlight_correction->apply (bb, xp, yp, backlight_correction_parameters::blue, true);
	}
      *r = rr;
      *g = gg;
      *b = bb;
    }
  else
    {
      *r = 0;
      *g = 0;
      *b = 0;
    }
}

/* Determine RGB value at position XP, YP in the image using bicubic interpolation.  */
inline flatten_attr void
render::get_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b) const
{
  get_unadjusted_img_rgb_pixel (xp, yp, r, g, b);
  *r = (*r - m_params.dark_point) * m_params.scan_exposure;
  *g = (*g - m_params.dark_point) * m_params.scan_exposure;
  *b = (*b - m_params.dark_point) * m_params.scan_exposure;
}

/* Inner loop for image downscaling processing single pixel PX, PY in result DATA
   from source pixel VAL.  */
template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
inline void always_inline_attr
render::process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T pixel, luminosity_t scale, luminosity_t xweight, luminosity_t yweight)
{
  if (colorscreen_checking)
    {
      assert (px >= (x0?0:-1) && px < (x1 ? width - 1 : width));
      assert (py >= (y0?0:-1) && py < (y1 ? height - 1: height));
    }
  if (x0)
    {
      if (y0)
	account_pixel (data + px + py * width, pixel, scale * (1 - yweight) * (1 - xweight));
      if (y1)
	account_pixel (data + px + (py + 1) * width, pixel, scale * yweight * (1 - xweight));
    }
  if (x1)
    {
      if (y0)
        account_pixel (data + px + (py * width) + 1, pixel, scale * (1 - yweight) * xweight);
      if (y1)
	account_pixel (data + px + (py + 1) * width + 1, pixel, scale * yweight * xweight);
    }
}

/* Inner loop for image downscaling processing single line.  */
template<typename T, typename D, T (D::*get_pixel) (int x, int y) const,
         void (*account_pixel) (T *, T, luminosity_t)>
inline void
render::process_line (T *data, int *pixelpos, luminosity_t *weights,
                      int xstart, int xend,
                      int width, int height,
                      int py, int yy,
                      bool y0, bool y1,
                      luminosity_t scale, luminosity_t yweight)
{
  int px = xstart;
  int xx = pixelpos[px];
  int stop;
  if (yy < 0 || yy >= m_img.height || xx >= m_img.width)
    return;
  if (px >= 0 && xx >= 0)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T, account_pixel> (data, width, height, px - 1, py, false, true, y0, y1, pixel, scale, weights[px], yweight);
    }
  xx++;
  if (xx < 0)
    xx = 0;
  stop = pixelpos[px + 1];
  for (; xx < stop; xx++)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T, account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
    }
  px++;
  while (px <= xend)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T, account_pixel> (data, width, height, px - 1, py, true, true, y0, y1, pixel, scale, weights[px], yweight);
      stop = pixelpos[px + 1];
      xx++;
      for (; xx < stop; xx++)
        {
          T pixel = (((D *)this)->*get_pixel) (xx, yy);
          process_pixel<T, account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
        }
      px++;
    }
  if (xx < m_img.width)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T, account_pixel> (data, width, height, px - 1, py, true, false, y0, y1, pixel, scale, weights[px], yweight);
    }
}

/* Template for parallelized downscaling of image.  */
template<typename D, typename T, T (D::*get_pixel) (int x, int y) const,
         void (*account_pixel) (T *, T, luminosity_t)>
bool
render::downscale (T *data, coord_t x, coord_t y, int width, int height,
                   coord_t pixelsize, progress_info *progress)
{
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  memset (data, 0, sizeof (T) * width * height);

  if (pxstart > pxend)
    return true;

  if (progress)
    {
      int pystart = std::max (0, (int)(-y / pixelsize));
      int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
      progress->set_task ("downscaling", pyend - pystart + 1);
    }

  /* Precompute scaling weights for X coordinate.  */
  std::vector<int> pixelpos (width + 2);
  std::vector<luminosity_t> weights (width + 2);

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

#pragma omp parallel shared(progress, data, pixelsize, width, height, pixelpos, x, y, pxstart, pxend, weights) default(none)
  {
    luminosity_t scale = 1 / (pixelsize * pixelsize);
    int pystart = std::max (0, (int)(-y / pixelsize));
    int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
#ifdef _OPENMP
    int tn = omp_get_thread_num ();
    int threads = omp_get_max_threads ();
#else
    int tn = 0;
    int threads = 1;
#endif
    int ystart = pystart + (pyend + 1 - pystart) * tn / threads;
    int yend = pystart + (pyend + 1 - pystart) * (tn + 1) / threads - 1;

    int py = ystart;
    int yy = ypixelpos (py);
    int stop;

    if (ystart > yend)
      goto end;
    if (py >= 0 && yy >= 0)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos.data (), weights.data (), pxstart, pxend, width, height, py - 1, yy, false, true, scale, weight (py));
    yy++;
    stop = std::min (ypixelpos (py + 1), m_img.height);
    for (; yy < stop; yy++)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos.data (), weights.data (), pxstart, pxend, width, height, py, yy, true, false, scale, 0);
    py++;

    while (py <= yend && (!progress || !progress->cancel_requested ()))
      {
	process_line<T, D, get_pixel, account_pixel> (data, pixelpos.data (), weights.data (), pxstart, pxend, width, height, py - 1, yy, true, true, scale, weight (py));
	stop = std::min (ypixelpos (py + 1), m_img.height);
	yy++;
	for (; yy < stop; yy++)
	  process_line<T, D, get_pixel, account_pixel> (data, pixelpos.data (), weights.data (), pxstart, pxend, width, height, py, yy, true, false, scale, 0);
	py++;
	if (progress)
	  progress->inc_progress ();
      }
    if (yy < m_img.height)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos.data (), weights.data (), pxstart, pxend, width, height, py - 1, yy, true, false, scale, weight (py));
    end:;
  }

#undef ypixelpos
#undef weight
  return !progress || !progress->cancelled ();
}
}
#endif
