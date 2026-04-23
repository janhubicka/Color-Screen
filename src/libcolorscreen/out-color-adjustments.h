#ifndef OUT_COLOR_ADJUSTMENTS_H
#define OUT_COLOR_ADJUSTMENTS_H
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/sensitivity.h"
#include "include/tone-curve.h"

namespace colorscreen
{
struct render_parameters;
class image_data;
class progress_info;

/* Convert color from process profile to final profile (either integer or HDR).
   This class handles color matrices, tone curves, and output gamma
   adjustments.  */
class out_color_adjustments
{
public:
  /* Initialize output color adjustments for image outputting to range
     0..MAXVAL.  */
  out_color_adjustments (int maxval) : m_dst_maxval (maxval) {}

  /* Precompute color transformation matrices and lookup tables for given
     RPARAM and IMG.  NORMALIZED_PATCHES and PATCH_PROPORTIONS control
     spectral data processing.  Report progress to PROGRESS.  Return false
     on failure.  */
  bool precompute (render_parameters &rparam, const image_data *img,
		   bool normalized_patches, rgbdata patch_proportions,
		   progress_info *progress = nullptr);

  /* Compute color in the final gamma and range 0..m_dst_maxval for values
     R, G, and B.  Store results into RR, GG, and BB.
     Fast version that is not always precise for dark colors and gamma > 1.5.  */
  inline void final_color (luminosity_t r, luminosity_t g, luminosity_t b,
			   int *rr, int *gg, int *bb) const;

  /* Same as above but using rgbdata and int_rgbdata structures.  */
  inline void
  final_color (rgbdata c, int_rgbdata &out) const
  {
    final_color (c.red, c.green, c.blue, &out.red, &out.green, &out.blue);
  }

  /* Compute color in the final gamma and range 0..m_dst_maxval for values
     R, G, and B.  Store results into RR, GG, and BB.
     Slow and precise version.  */
  inline void final_color_precise (luminosity_t r, luminosity_t g,
				   luminosity_t b, int *rr, int *gg,
				   int *bb) const;

  /* Same as above but using rgbdata and int_rgbdata structures.  */
  inline void
  final_color_precise (rgbdata c, int_rgbdata &out) const
  {
    final_color_precise (c.red, c.green, c.blue, &out.red, &out.green,
			 &out.blue);
  }

  /* Compute color in linear HDR space for values R, G, and B.  Store results
     into RR, GG, and BB.  */
  inline void linear_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b,
				luminosity_t *rr, luminosity_t *gg,
				luminosity_t *bb) const;

  /* Same as above but using rgbdata structures.  */
  inline void
  linear_hdr_color (rgbdata c, rgbdata &out) const
  {
    linear_hdr_color (c.red, c.green, c.blue, &out.red, &out.green, &out.blue);
  }

  /* Compute color in the final output gamma for values R, G, and B.  Store
     results into RR, GG, and BB.  */
  inline void hdr_final_color (luminosity_t r, luminosity_t g, luminosity_t b,
			       luminosity_t *rr, luminosity_t *gg,
			       luminosity_t *bb) const;

  /* Same as above but using rgbdata structures.  */
  inline void
  hdr_final_color (rgbdata c, rgbdata &out) const
  {
    hdr_final_color (c.red, c.green, c.blue, &out.red, &out.green, &out.blue);
  }

  static constexpr const size_t out_lookup_table_size = 65536 * 16;

  /* Color matrix.  For additive processes it converts process RGB to prophoto
     RGB. For subtractive processes it only applies transformations done in
     process RGB.
     TODO: Only exported because of single use in render-interpolate that
     should be rewritten.  */
  color_matrix m_color_matrix;

private:
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval = 255;

  /* Copied values from render_parameters. */
  luminosity_t m_output_gamma = (luminosity_t)-1.0;
  bool m_gamut_warning = false;

  /* Tone curve translation.  */
  std::unique_ptr<tone_curve> m_tone_curve = nullptr;

  /* For substractive processes it converts xyz to prophoto RGB applying
     corrections, like saturation control.  */
  color_matrix m_color_matrix2;

  /* For subtractive processes it converts dyes RGB to xyz.  */
  std::unique_ptr<spectrum_dyes_to_xyz> m_spectrum_dyes_to_xyz = nullptr;

  /* Translates back from linear space to output gamma.  */
  std::shared_ptr<precomputed_function<luminosity_t>> m_out_lookup_table
      = nullptr;
};

/* Compute color in linear HDR image for values R, G, B.  Store result
   in RR, GG, BB.  */
inline void
out_color_adjustments::linear_hdr_color (luminosity_t r, luminosity_t g,
					 luminosity_t b, luminosity_t *rr,
					 luminosity_t *gg,
					 luminosity_t *bb) const
{
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  if (m_spectrum_dyes_to_xyz)
    {
      if (m_spectrum_dyes_to_xyz->red_characteristic_curve)
	r = m_spectrum_dyes_to_xyz->red_characteristic_curve->apply (r);
      if (m_spectrum_dyes_to_xyz->green_characteristic_curve)
	g = m_spectrum_dyes_to_xyz->green_characteristic_curve->apply (g);
      if (m_spectrum_dyes_to_xyz->blue_characteristic_curve)
	b = m_spectrum_dyes_to_xyz->blue_characteristic_curve->apply (b);
      xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
      m_color_matrix2.apply_to_rgb (c.x, c.y, c.z, &r, &g, &b);
    }

  /* Apply DNG-style tone curve correction.  */
  if (m_tone_curve)
    {
      rgbdata c = { r, g, b };
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

/* Compute color in the final gamma for values R, G, B.  Store result
   in RR, GG, BB.  */
inline void
out_color_adjustments::hdr_final_color (luminosity_t r, luminosity_t g,
					luminosity_t b, luminosity_t *rr,
					luminosity_t *gg,
					luminosity_t *bb) const
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
out_color_adjustments::final_color (luminosity_t r, luminosity_t g,
				    luminosity_t b, int *rr, int *gg,
				    int *bb) const
{
  linear_hdr_color (r, g, b, &r, &g, &b);
  /* Show gamut warnings.  */
  if (m_gamut_warning && (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1))
    r = g = b = (luminosity_t)0.5;
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
out_color_adjustments::final_color_precise (luminosity_t r, luminosity_t g,
					    luminosity_t b, int *rr, int *gg,
					    int *bb) const
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
  *rr = fr * m_dst_maxval + (luminosity_t)0.5;
  *gg = fg * m_dst_maxval + (luminosity_t)0.5;
  *bb = fb * m_dst_maxval + (luminosity_t)0.5;
}
}
#endif
