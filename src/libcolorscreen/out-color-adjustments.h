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
  nodiscard_attr bool precompute (render_parameters &rparam,
				  const image_data *img,
				  bool normalized_patches,
				  rgbdata patch_proportions,
				  progress_info *progress = nullptr);

  /* Compute color in the final gamma and range 0..m_dst_maxval for values
     in C.
     Fast version that is not always precise for dark colors and gamma > 1.5.  */
  pure_attr inline int_rgbdata final_color (rgbdata c) const noexcept;

  /* Compute color in the final gamma and range 0..m_dst_maxval for values
     in C.
     Slow and precise version.  */
  pure_attr inline int_rgbdata final_color_precise (rgbdata c) const noexcept;

  /* Compute color in linear HDR space for values in C.  */
  pure_attr inline rgbdata linear_hdr_color (rgbdata c) const noexcept;

  /* Compute color in the final output gamma for values in C.  */
  pure_attr inline rgbdata hdr_final_color (rgbdata c) const noexcept;

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

/* Compute color in linear HDR image for values in C.  */
inline rgbdata
out_color_adjustments::linear_hdr_color (rgbdata c) const noexcept
{
  luminosity_t r = c.red, g = c.green, b = c.blue;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  if (m_spectrum_dyes_to_xyz)
    {
      if (m_spectrum_dyes_to_xyz->red_characteristic_curve)
	r = m_spectrum_dyes_to_xyz->red_characteristic_curve->apply (r);
      if (m_spectrum_dyes_to_xyz->green_characteristic_curve)
	g = m_spectrum_dyes_to_xyz->green_characteristic_curve->apply (g);
      if (m_spectrum_dyes_to_xyz->blue_characteristic_curve)
	b = m_spectrum_dyes_to_xyz->blue_characteristic_curve->apply (b);
      xyz c_xyz = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
      m_color_matrix2.apply_to_rgb (c_xyz.x, c_xyz.y, c_xyz.z, &r, &g, &b);
    }

  /* Apply DNG-style tone curve correction.  */
  if (m_tone_curve)
    {
      rgbdata c_tc = { r, g, b };
      assert (!m_tone_curve->is_linear ());
      c_tc = m_tone_curve->apply_to_rgb (c_tc);
      color_matrix cm;
      pro_photo_rgb_xyz_matrix m1;
      cm = m1 * cm;
      bradford_d50_to_d65_matrix m2;
      cm = m2 * cm;
      xyz_srgb_matrix m;
      cm = m * cm;
      cm.apply_to_rgb (c_tc.red, c_tc.green, c_tc.blue, &c_tc.red, &c_tc.green,
		       &c_tc.blue);
      return c_tc;
    }

  return { r, g, b };
}

/* Compute color in the final gamma for values in C.  */
inline rgbdata
out_color_adjustments::hdr_final_color (rgbdata c) const noexcept
{
  rgbdata linear = linear_hdr_color (c);
  return { invert_gamma (linear.red, m_output_gamma),
           invert_gamma (linear.green, m_output_gamma),
           invert_gamma (linear.blue, m_output_gamma) };
}

/* Compute color in the final gamma and range 0..m_dst_maxval for values in C.
   Fast version that is not always precise for dark colors and gamma > 1.5.  */
inline int_rgbdata
out_color_adjustments::final_color (rgbdata c) const noexcept
{
  rgbdata linear = linear_hdr_color (c);
  /* Show gamut warnings.  */
  if (m_gamut_warning
      && (linear.red < 0 || linear.red > 1 || linear.green < 0
	  || linear.green > 1 || linear.blue < 0 || linear.blue > 1))
    {
      linear.red = linear.green = linear.blue = (luminosity_t)0.5;
    }
  else
    {
      linear.red = std::clamp (linear.red, (luminosity_t)0.0, (luminosity_t)1.0);
      linear.green
	  = std::clamp (linear.green, (luminosity_t)0.0, (luminosity_t)1.0);
      linear.blue
	  = std::clamp (linear.blue, (luminosity_t)0.0, (luminosity_t)1.0);
    }
  return { (int)m_out_lookup_table->apply (linear.red),
           (int)m_out_lookup_table->apply (linear.green),
           (int)m_out_lookup_table->apply (linear.blue) };
}

/* Compute color in the final gamma and range 0..m_dst_maxval for values in C.
   Slow version.  */
inline int_rgbdata
out_color_adjustments::final_color_precise (rgbdata c) const noexcept
{
  if (m_output_gamma == 1)
    {
      return final_color (c);
    }
  rgbdata frgb = hdr_final_color (c);
  frgb.red = std::clamp (frgb.red, (luminosity_t)0.0, (luminosity_t)1.0);
  frgb.green = std::clamp (frgb.green, (luminosity_t)0.0, (luminosity_t)1.0);
  frgb.blue = std::clamp (frgb.blue, (luminosity_t)0.0, (luminosity_t)1.0);
  return { (int)(frgb.red * m_dst_maxval + (luminosity_t)0.5),
           (int)(frgb.green * m_dst_maxval + (luminosity_t)0.5),
           (int)(frgb.blue * m_dst_maxval + (luminosity_t)0.5) };
}
}
#endif
