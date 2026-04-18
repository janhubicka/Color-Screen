#ifndef OUT_COLOR_ADJUSTMENTS_H
#define OUT_COLOR_ADJUSTMENTS_H
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/sensitivity.h"
#include "include/tone-curve.h"
#include "lru-cache.h"
namespace colorscreen
{
struct render_parameters;
class image_data;
class progress_info;

struct out_lookup_table_params
{
  int maxval;
  luminosity_t output_gamma;
  luminosity_t target_film_gamma;
  bool
  operator== (out_lookup_table_params &o)
  {
    return maxval == o.maxval && output_gamma == o.output_gamma
           && target_film_gamma == o.target_film_gamma;
  }
};

precomputed_function<luminosity_t> *
get_new_out_lookup_table (struct out_lookup_table_params &, progress_info *);

/* Convert color from process profile to final profile
   (either integer or HDR)  */
class out_color_adjustments
{
public:
  out_color_adjustments (int maxval) : m_dst_maxval (maxval) {}
  bool precompute (render_parameters &rparam, const image_data *m_img,
                   bool normalized_patches, rgbdata patch_proportions,
                   progress_info *progress = NULL);
  inline void final_color (luminosity_t, luminosity_t, luminosity_t, int *,
                           int *, int *) const;
  inline void final_color_precise (luminosity_t, luminosity_t, luminosity_t,
                                   int *, int *, int *) const;
  inline void linear_hdr_color (luminosity_t, luminosity_t, luminosity_t,
                                luminosity_t *, luminosity_t *,
                                luminosity_t *) const;
  inline void hdr_final_color (luminosity_t, luminosity_t, luminosity_t,
                               luminosity_t *, luminosity_t *,
                               luminosity_t *) const;

  static constexpr const size_t out_lookup_table_size = 65536 * 16;
  /* Color matrix.  For additvie processes it converts process RGB to prophoto
     RGB. For subtractive processes it only applies transformations does in
     process RGB.
     TODO: Only exported because of single use in render-interpolate that
     should be rewritten.  */
  color_matrix m_color_matrix;

private:
  typedef lru_cache<
      out_lookup_table_params, precomputed_function<luminosity_t>,
      precomputed_function<luminosity_t> *, get_new_out_lookup_table, 4>
      out_lookup_table_cache_t;

  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;

  /* Copied values from render_parameters. */
  luminosity_t m_target_film_gamma;
  luminosity_t m_output_gamma;
  bool m_gammut_warning;

  /* Tone curve translation.  */
  std::unique_ptr<tone_curve> m_tone_curve;

  /* For substractive processes it converts xyz to prophoto RGB applying
     corrections, like saturation control.  */
  color_matrix m_color_matrix2;

  /* For subtractive processes it converts dyes RGB to xyz.  */
  std::unique_ptr<spectrum_dyes_to_xyz> m_spectrum_dyes_to_xyz;

  /* Translates back to gamma 2.  */
  out_lookup_table_cache_t::cached_ptr m_out_lookup_table;
};
}
#endif
