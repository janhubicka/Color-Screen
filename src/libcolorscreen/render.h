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
#include "backlight-correction.h"
#include "mem-luminosity.h"
namespace colorscreen
{
/* Helper for downscaling template for color rendering
   data += lum * scale.  */
inline void
account_rgb_pixel (rgbdata *data, rgbdata lum, luminosity_t scale)
{
  data->red += lum.red * scale;
  data->green += lum.green * scale;
  data->blue += lum.blue * scale;
}

/* Helper for downscaling template for grayscale rendering
   data += lum * scale.  */
inline void
account_pixel (luminosity_t *data, luminosity_t lum, luminosity_t scale)
{
  *data += lum * scale;
}

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class render
{
public:
  render (const image_data &img, const render_parameters &rparam, int dstmaxval)
  : m_img (img), m_params (rparam), m_gray_data_id (img.id), m_sharpened_data (NULL), m_sharpened_data_holder (NULL), m_maxval (img.data ? img.maxval : 65535), m_dst_maxval (dstmaxval),
    m_rgb_lookup_table {NULL, NULL, NULL}, m_out_lookup_table (NULL), m_spectrum_dyes_to_xyz (NULL), m_backlight_correction (NULL), m_backlight_correction_id (0), m_tone_curve ()
  {
    if (m_params.invert)
      {
	static synthetic_hd_curve c (10, safe_output_curve_params);
	m_params.output_curve = &c;
      }
    else
      m_params.output_curve = NULL;
  }
  ~render ();
  pure_attr inline luminosity_t get_img_pixel (coord_t x, coord_t y) const;
  pure_attr inline luminosity_t get_unadjusted_img_pixel (coord_t x, coord_t y) const;
  inline void get_img_rgb_pixel (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b) const;
  inline void get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b) const;
  pure_attr inline luminosity_t sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2) const;
  pure_attr inline luminosity_t fast_get_img_pixel (int x, int y) const;
    
  static const int num_color_models = render_parameters::color_model_max;
  static bool get_lookup_tables (luminosity_t **ret, luminosity_t gamma, const image_data *img, progress_info *progress = NULL);
  static void release_lookup_tables (luminosity_t **);
  inline void set_color (luminosity_t, luminosity_t, luminosity_t, int *, int *, int *) const;
  inline void set_color_precise (luminosity_t, luminosity_t, luminosity_t, int *, int *, int *) const;
  inline void set_linear_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *) const;
  inline void set_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *) const;
  pure_attr inline luminosity_t get_data (int x, int y) const;
  pure_attr inline luminosity_t get_unadjusted_data (int x, int y) const;
  pure_attr inline luminosity_t adjust_luminosity_ir (luminosity_t) const;
  pure_attr inline luminosity_t get_data_red (int x, int y) const;
  pure_attr inline luminosity_t get_data_green (int x, int y) const;
  pure_attr inline luminosity_t get_data_blue (int x, int y) const;
  pure_attr inline luminosity_t get_linearized_data_red (int x, int y) const;
  pure_attr inline luminosity_t get_linearized_data_green (int x, int y) const;
  pure_attr inline luminosity_t get_linearized_data_blue (int x, int y) const;
  DLL_PUBLIC bool precompute_all (bool grayscale_needed, bool normalized_patches, rgbdata patch_proportions, progress_info *progress);
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
  pure_attr inline rgbdata
  get_unadjusted_rgb_pixel (int x, int y) const
  {
    rgbdata d = get_linearized_rgb_pixel (x, y);
    if (m_backlight_correction)
      {
	d.red = m_backlight_correction->apply (d.red, x, y, backlight_correction_parameters::red, true);
	d.green = m_backlight_correction->apply (d.green, x, y, backlight_correction_parameters::green, true);
	d.blue = m_backlight_correction->apply (d.blue, x, y, backlight_correction_parameters::blue, true);
	/* TODO do inversion and film curves if requested.  */
      }
    return d;
  }
  pure_attr inline rgbdata
  adjust_rgb (rgbdata d) const
  {
    d.red = (d.red - m_params.dark_point) * m_params.scan_exposure;
    d.green = (d.green - m_params.dark_point) * m_params.scan_exposure;
    d.blue = (d.blue - m_params.dark_point) * m_params.scan_exposure;
    return d;
  }
  pure_attr inline rgbdata
  get_rgb_pixel (int x, int y) const
  {
    return adjust_rgb (get_unadjusted_rgb_pixel (x, y));
  }
  /* PATCH_PORTIONS describes how much percent of screen is occupied by red, green and blue
     patches respectively. It should have sum at most 1.
     
     If NORMALIZED_PATCHES is true, the rgbdata represents patch intensities regardless of their
     size (as in interpolated rendering) and the dye matrix channels needs to be scaled by
     PATCH_PROPORTIONS.  */
  color_matrix get_rgb_to_xyz_matrix (bool normalized_patches, rgbdata patch_proportions)
  {
    return m_params.get_rgb_to_xyz_matrix (&m_img, normalized_patches, patch_proportions);
  }
  /* Just a placeholder, it is needed only for render_to_scr.  */
  void
  compute_final_range ()
  {
  }
  void get_gray_data (luminosity_t *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);

  /* Return number of pixel computations that are considered
     profitable for openmp to paralelize.  */
  size_t openmp_size ()
  {
    return 128 * 1024;
  }

  static constexpr const size_t out_lookup_table_size = 65536 * 16;
protected:
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);


  template<typename T, typename D, T (D::*get_pixel) (int x, int y) const, void (*account_pixel) (T *, T, luminosity_t)>
  void process_line (T *data, int *pixelpos, luminosity_t *weights,
		     int xstart, int xend,
		     int width, int height,
		     int py, int yy,
		     bool y0, bool y1,
		     luminosity_t scale, luminosity_t yweight);

  template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
  inline __attribute__ ((always_inline)) void process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T val, luminosity_t scale, luminosity_t xweight, luminosity_t yweight);

  template<typename D, typename T, T (D::*get_pixel) (int x, int y) const, void (*account_pixel) (T *, T, luminosity_t)>
  bool downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *);

  /* Scanned image.  */
  const image_data &m_img;
  /* Rendering parameters.
     Make local copy for performance reasons and also because render-tile releases rparam
     after constructing renderer.  */
  render_parameters m_params;
  /* ID of graydata computed.  */
  uint64_t m_gray_data_id;
  /* Sharpened data we render from.  */
  mem_luminosity_t *m_sharpened_data;
  /* Wrapping class to cause proper destruction.  */
  class sharpened_data *m_sharpened_data_holder;
  /* Maximal value in m_data.  */
  int m_maxval;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input rgb channel values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_rgb_lookup_table[3];
  /* Translates back to gamma 2.  */
  precomputed_function<luminosity_t> *m_out_lookup_table;
  /* Color matrix.  For additvie processes it converts process RGB to prophoto RGB.
     For subtractive processes it only applies transformations does in process RGB.  */
  color_matrix m_color_matrix;

  /* For subtractive processes it converts dyes RGB to xyz.  */
  spectrum_dyes_to_xyz *m_spectrum_dyes_to_xyz;

  /* For cubstractive processes it converts xyz to prophoto RGB applying
     corrections, like saturation control.  */
  color_matrix m_color_matrix2;

  backlight_correction *m_backlight_correction;
  uint64_t m_backlight_correction_id;

private:
  std::unique_ptr <tone_curve> m_tone_curve;
};

typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;

/* Cubic interpolation helper.  */

static inline luminosity_t const_attr __attribute__ ((always_inline))
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}
static inline vec_luminosity_t const_attr __attribute__ ((always_inline))
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

pure_attr inline luminosity_t always_inline_attr
render::get_unadjusted_data (int x, int y) const
{
  /* TODO do inversion and film curves if requested.  */
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_sharpened_data [y * m_img.width + x];
}

pure_attr inline luminosity_t always_inline_attr
render::adjust_luminosity_ir (luminosity_t lum) const
{
  lum = (lum - m_params.dark_point) * m_params.scan_exposure;
  if (m_params.film_gamma != 1)
    lum = invert_gamma (lum, m_params.film_gamma);
  return lum;
}

/* Get image data in normalized range 0...1.  */

pure_attr inline luminosity_t always_inline_attr
render::get_data (int x, int y) const
{
  return adjust_luminosity_ir (get_unadjusted_data (x, y));
}

/* Get same for rgb data.  */

pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_red (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [0][m_img.rgbdata[y][x].r];
  /* TODO do inversion and film curves if requested.  */
}

pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_green (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [1][m_img.rgbdata[y][x].g];
  /* TODO do inversion and film curves if requested.  */
}
pure_attr inline luminosity_t always_inline_attr
render::get_linearized_data_blue (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  return m_rgb_lookup_table [2][m_img.rgbdata[y][x].b];
  /* TODO do inversion and film curves if requested.  */
}

/* Get same for rgb data.  */

pure_attr inline luminosity_t
render::get_data_red (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [0][m_img.rgbdata[y][x].r];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::red, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

pure_attr inline luminosity_t
render::get_data_green (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [1][m_img.rgbdata[y][x].g];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::green, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

pure_attr inline luminosity_t
render::get_data_blue (int x, int y) const
{
  if (colorscreen_checking)
    assert (x >= 0 && x < m_img.width && y >= 0 && y < m_img.height);
  luminosity_t v = m_rgb_lookup_table [2][m_img.rgbdata[y][x].b];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::blue, true);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

/* Compute color in linear HDR image.  */
inline void
render::set_linear_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
{
#if 0
  r *= m_params.white_balance.red;
  g *= m_params.white_balance.green;
  b *= m_params.white_balance.blue;
  if (m_spectrum_dyes_to_xyz)
    {
      /* At the moment all conversions are linear.
         Simplify the codegen here.  */
      if (1)
	abort ();
      else
	{
	  if (m_params.presaturation != 1)
	    {
	      presaturation_matrix m (m_params.presaturation);
	      m.apply_to_rgb (r, g, b, &r, &g, &b);
	    }
	  struct xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
	  r = c.x;
	  g = c.y;
	  b = c.z;
	}
    }
#endif
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
  if (m_params.output_curve)
    {
      luminosity_t lum = r * rwght + g * gwght + b * bwght;
      luminosity_t lum2;
      lum2 = m_params.output_curve->apply (lum);
      if (lum != lum2)
	{
	  r *= lum2 / lum;
	  g *= lum2 / lum;
	  b *= lum2 / lum;
	}
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
inline void
render::set_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb) const
{
  luminosity_t r1, g1, b1;
  render::set_linear_hdr_color (r, g, b, &r1, &g1, &b1);
  if (m_params.target_film_gamma != 1)
    {
      r1 = apply_gamma (r1, m_params.target_film_gamma);
      g1 = apply_gamma (g1, m_params.target_film_gamma);
      b1 = apply_gamma (b1, m_params.target_film_gamma);
    }
  *rr = invert_gamma (r1, m_params.output_gamma);
  *gg = invert_gamma (g1, m_params.output_gamma);
  *bb = invert_gamma (b1, m_params.output_gamma);
}

/* Compute color in the final gamma and range 0...m_dst_maxval.
   Fast version that is not always precise for dark colors and gamma > 1.5  */
inline void
render::set_color (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb) const
{
  set_linear_hdr_color (r, g, b, &r, &g, &b);
  // Show gammut warnings
  if (m_params.gammut_warning && (r < 0 || r > 1 || g < 0 || g >1 || b < 0 || b > 1))
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

/* Compute color in the final gamma and range 0...m_dst_maxval.
   Slow version.  */
inline void
render::set_color_precise (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb) const
{
  if (m_params.gamma == 1)
    {
      set_color (r, g, b, rr, gg, bb);
      return;
    }
  luminosity_t fr, fg, fb;
  set_hdr_color (r, g, b, &fr, &fg, &fb);
  fr = std::clamp (fr, (luminosity_t)0.0, (luminosity_t)1.0);
  fg = std::clamp (fg, (luminosity_t)0.0, (luminosity_t)1.0);
  fb = std::clamp (fb, (luminosity_t)0.0, (luminosity_t)1.0);
  *rr = fr * m_maxval + 0.5;
  *gg = fg * m_maxval + 0.5;
  *bb = fb * m_maxval + 0.5;
}

#if 0
/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval
   combining color and luminosity information.  */

inline void
render::set_color_luminosity (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t l, int *rr, int *gg, int *bb)
{
  luminosity_t r1, g1, b1;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  m_color_matrix.apply_to_rgb (l, l, l, &r1, &g1, &b1);
  l = r1 * rwght + g1 * gwght + b1 * bwght;
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  l = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, l));
  luminosity_t gr = (r * rwght + g * gwght + b * bwght);
  if (gr <= 0.00001 || l <= 0.00001)
    r = g = b = l;
  else
    {
      gr = l / gr;
      r *= gr;
      g *= gr;
      b *= gr;
    }
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));

  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}
#endif

/* Determine grayscale value at a given position in the image.  */

pure_attr inline luminosity_t
render::fast_get_img_pixel (int x, int y) const
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

pure_attr inline pure_attr luminosity_t
render::get_unadjusted_img_pixel (coord_t xp, coord_t yp) const
{
  luminosity_t val;

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  //int sx = xp, sy = yp;
  //luminosity_t rx = xp - sx, ry = yp - sy;
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
      val = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
      return val;
    }
    return 0;
  return val;
}

pure_attr inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp) const
{
  return adjust_luminosity_ir (get_unadjusted_img_pixel (xp, yp));
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline flatten_attr void
render::get_unadjusted_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b) const
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
      return;
    }
}
inline flatten_attr void
render::get_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b) const
{
  get_unadjusted_img_rgb_pixel (xp, yp, r, g, b);
  *r = (*r - m_params.dark_point) * m_params.scan_exposure;
  *g = (*g - m_params.dark_point) * m_params.scan_exposure;
  *b = (*b - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
}

/* Sample square patch with center xc and yc and x1/y1, x2/y2 determining a coordinates
   of top left and top right corner.  */

pure_attr luminosity_t
render::sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2) const
{
  luminosity_t acc = 0, weights = 0;
  int xmin = std::max ((int)(std::min (std::min (std::min (xc - x1, xc + x1), xc - x2), xc + x2) - 0.5), 0);
  int xmax = std::min ((int)ceil (std::max(std::max (std::max (xc - x1, xc + x1), xc - x2), xc + x2) + 0.5), m_img.width - 1);
  /* If the resolution is too small, just sample given point.  */
  if (xmax-xmin < 2)
    return get_img_pixel (xc, yc);
  /* For bigger resolution we can sample few points in the square.  */
  if (xmax-xmin < 6)
    {
      /* Maybe this will give more reproducible results, but it is very slow.  */
      int samples = (sqrt (x1 * x1 + y1 * y1) + 0.5) * 2;
      luminosity_t rec = 1.0 / samples;
      if (!samples)
	return get_img_pixel (xc, yc);
      for (int y = -samples ; y <= samples; y++)
	for (int x = -samples ; x <= samples; x++)
	  {
	    luminosity_t w = 1 + (samples - abs (x) - abs (y));
	    if (w < 0)
	      continue;
	    acc += w * get_img_pixel (xc + (x1 * x + x2 * y) * rec, yc + (y1 * x + y2 * y) * rec);
	    weights += w;
	  }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      int ymin = std::max ((int)(std::min (std::min (std::min (yc - y1, yc + y1), yc - y2), yc + y2) - 0.5), 0);
      int ymax = std::min ((int)ceil (std::max(std::max (std::max (yc - y1, yc + y1), yc - y2), yc + y2) + 0.5), m_img.height - 1);
      matrix2x2<coord_t> base (x1, x2,
			      y1, y2);
      matrix2x2<coord_t> inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
	{
	  for (int x = xmin ; x <= xmax; x++)
	    {
	      coord_t cx = x+0.5 -xc;
	      coord_t cy = y+0.5 -yc;
	      coord_t ccx, ccy;
	      inv.apply_to_vector (cx, cy, &ccx, &ccy);
	      luminosity_t w = fabs (ccx) + fabs (ccy);

	      //if (w < 1)
		//printf ("%.1f ",w);
	      //else
		//printf ("    ",w);
	      if (w < 1)
		{
		  w = (1 - w);
		  acc += w * get_data (x, y);
		  weights += w;
		}
	    }
	    //printf ("\n");
	 }
    }
  if (weights)
    return acc / weights;
  return 0;
}

/* Helper for downscaling template.
   PIXEL is a pixel obtained from source image.  Account PIXEL*SCALE
   to DATA at coordinates (px,py), (px,py+1), (py+1, px) and (px+1,py+1)
   and distribute its value according to XWEIGHT and YWEIHT (here 0,0 means
   that pixel is accounted only to px,py.

   x0,x1,y0,y1 is used to disable updating for certain rows and columns to void
   accessing out of range data. 
  
   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
inline void __attribute__ ((always_inline))
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

/* Helper for downscaling template.  Process line (in range XSTART..XEND) if input image with
   coordinate YY and account it (scaled by SCALE) to line of DATA with coordinate PY and PY+1.
   PY gets 1-yweight of the data, while py+1 get yweight of data. 
   PIXELPOS and WEIGHTS are precoputed scaling data for for x coordinate.

   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, typename D, T (D::*get_pixel) (int x, int y) const, void (*account_pixel) (T *, T, luminosity_t)>
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
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, false, true, y0, y1, pixel, scale, weights[px], yweight);
    }
  xx++;
  if (xx < 0)
    xx = 0;
  stop = pixelpos[px + 1];
  for (; xx < stop; xx++)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
    }
  px++;
  while (px <= xend)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, true, y0, y1, pixel, scale, weights[px], yweight);
      stop = pixelpos[px + 1];
      xx++;
      for (; xx < stop; xx++)
	{
	  T pixel = (((D *)this)->*get_pixel) (xx, yy);
	  process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
	}
      px++;
    }
   if (xx < m_img.width)
     {
       T pixel = (((D *)this)->*get_pixel) (xx, yy);
       process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, false, y0, y1, pixel, scale, weights[px], yweight);
     }
}

/* Template for paralelized downscaling of image.
   GET_PIXEL is used to access input image which is of type T and ACCOUNT_PIXEL is used to account
   pixels to given position of DATA.
 
   DATA is an output pixmap with dimensions WIDTH*HEIGHT.
   pixelsize if size of output pixel inside of input image.
   X,Y are coordinates of the top left corner of the output image in the input image.  */

template<typename D, typename T, T (D::*get_pixel) (int x, int y) const, void (*account_pixel) (T *, T, luminosity_t)>
bool
render::downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  memset ((void *)data, 0, sizeof (T) * width * height);

  if (pxstart > pxend)
    return true;

  if (progress)
    {
      int pystart = std::max (0, (int)(-y / pixelsize));
      int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
      progress->set_task ("downscaling", pyend - pystart + 1);
    }

  /* Precompute to which column of output image given colon of input image shold be accounted to.  */
  int *pixelpos = (int *)malloc (sizeof (int) * (width + 1));
  luminosity_t *weights = (luminosity_t *)malloc (sizeof (luminosity_t) * (width + 1));

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

  size_t size = width * (size_t)height * (pixelsize * pixelsize);
  bool openmp = size > openmp_size ();

#pragma omp parallel shared(progress,data,pixelsize,width,height,pixelpos,x,y,pxstart,pxend,weights,openmp) default (none) if (openmp)
  {
    luminosity_t scale = 1 / (pixelsize * pixelsize);
    int pystart = std::max (0, (int)(-y / pixelsize));
    int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
#ifdef _OPENMP
    int tn = openmp ? omp_get_thread_num () : 0;
    int threads = openmp ? omp_get_max_threads () : 1;
#else
    int tn = 0;
    int threads = 1;
#endif
    int ystart = pystart + (pyend + 1 - pystart) * tn / threads;
    int yend = pystart + (pyend + 1 - pystart) * (tn + 1) / threads - 1;

    int py = ystart;
    int yy = ypixelpos(py);
    int stop;

    if (ystart > yend)
      goto end;
    if (py >= 0 && yy >= 0)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, false, true, scale, weight(py));
    yy++;
    stop = std::min (ypixelpos(py + 1), m_img.height);
    for (; yy < stop; yy++)
      {
	process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
      }
    py++;
    if (progress)
      progress->inc_progress ();
    while (py <= yend && (!progress || !progress->cancel_requested ()))
      {
        process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, true, scale, weight (py));
	stop = std::min (ypixelpos(py + 1), m_img.height);
	yy++;
	for (; yy < stop; yy++)
	  {
	    process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
	  }
	py++;
	if (progress)
	  progress->inc_progress ();
      }
     if (yy < m_img.height)
       process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, false, scale, weight (py));
     end:;
  }

#undef ypixelpos
#undef weight
  free (pixelpos);
  free (weights);
  return !progress || !progress->cancelled ();
}

}
#endif
