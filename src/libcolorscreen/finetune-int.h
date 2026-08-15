#ifndef FINETUNE_INT_H
#define FINETUNE_INT_H
#include <array>
#include <memory>
#include <vector>
#include "include/finetune.h"
#include "simulate.h"
namespace colorscreen {

/* Return an explanatory string when FLAGS select mutually incompatible
   FINETUNE models, or null when the combination is supported.  */
const char *finetune_flag_error (uint64_t flags);

/* Convert the scalar dark term fitted after RGB mixing to the neutral RGB
   dark value stored by render_parameters.  FALLBACK is returned when the
   conversion is undefined or any fitted value is non-finite.  */
rgbdata finetune_render_mix_dark (rgbdata weights, luminosity_t scalar_dark,
                                  rgbdata fallback);

/* Return the scalar scanner-MTF coordinate used to initialize a local
   FINETUNE fit.  Physical defocus and measured-MTF residual blur keep the
   caller's current estimate.  The metadata-free empirical circular-blur
   fallback intentionally starts at zero: its blur/color objective is
   multimodal and warm-starting from an adaptive coarse average can move
   otherwise equivalent local fits into a high-blur color-compensation basin. */
coord_t finetune_initial_scanner_mtf_focus (const mtf_parameters &params);

/* One interval of the nonlinear scalar blur/focus grid used by the dense
   displacement-analysis approximation.  The scalar coordinate is physical
   image-plane defocus for the diffraction model and compact blur diameter for
   the metadata-free empirical fallback.  UPPER_WEIGHT is zero at LOWER and
   one at UPPER.  */
struct finetune_focus_grid_interval
{
  int lower_index = 0;
  int upper_index = 0;
  coord_t lower = 0;
  coord_t upper = 0;
  coord_t upper_weight = 0;
};

/* Return the quadratically spaced grid interval containing VALUE in
   [0,MAX_VALUE].  NODES includes both endpoints.  */
bool finetune_focus_grid_interval_for_value (
    coord_t value, coord_t max_value, int nodes,
    finetune_focus_grid_interval *interval);

/* Materialize in DST the multiplicative transmission between exact
   focus-grid screens LOWER and UPPER, giving UPPER the weight UPPER_WEIGHT.
   Keep the loop flat: the former three-level 128x128x3 loop left a
   three-element innermost dimension that GCC did not reliably vectorize.
   ADD is independent of optical filtering and remains the caller's
   responsibility.  DST must not alias either source.  */
inline void
finetune_interpolate_screen_mult (screen &dst, const screen &lower,
                                  const screen &upper,
                                  luminosity_t upper_weight)
{
  assert (&dst != &lower && &dst != &upper);
  constexpr int values = screen::size * screen::size * 3;
  luminosity_t *d = &dst.mult[0][0][0];
  const luminosity_t *l = &lower.mult[0][0][0];
  const luminosity_t *u = &upper.mult[0][0][0];
  const luminosity_t lower_weight = 1 - upper_weight;
#pragma omp simd
  for (int i = 0; i < values; i++)
    d[i] = l[i] * lower_weight + u[i] * upper_weight;
}

/* Find the first nonnegative physical defocus at which PARAMS' system MTF at
   PIXEL_FREQUENCY drops to MINIMUM_MTF.  Search no farther than HARD_MAX and
   store HARD_MAX when no crossing occurs.  The in-focus response must exceed
   MINIMUM_MTF.  */
bool finetune_useful_defocus_limit (mtf_parameters params,
                                    coord_t pixel_frequency,
                                    coord_t minimum_mtf, coord_t hard_max,
                                    coord_t *limit);

/* Find the first nonnegative compact fallback blur diameter at which PARAMS'
   system MTF at PIXEL_FREQUENCY drops to MINIMUM_MTF.  Search no farther than
   HARD_MAX and store HARD_MAX when no crossing occurs.  PARAMS must select
   the metadata-free analytical fallback rather than measured MTF data.  */
bool finetune_useful_blur_diameter_limit (mtf_parameters params,
                                          coord_t pixel_frequency,
                                          coord_t minimum_mtf,
                                          coord_t hard_max,
                                          coord_t *limit);

/* Find the fit-quality cutoff that retains RETAIN_RATIO of the most reliable
   successful RESULTS.  Failed and non-finite results are ignored.  */
bool finetune_retained_fit_score_cutoff (
    const std::vector<finetune_result> &results, coord_t retain_ratio,
    coord_t *cutoff);

/* Classification of one completed FINETUNE result for adaptive analysis.
   LOW_CONTRAST is distinct from numerical failure: the optimizer found a
   finite solution, but the fitted additive-screen modulation is too weak to
   constrain position, blur, or focus reliably.  */
enum class finetune_result_quality
{
  usable,
  solver_failure,
  invalid_contrast,
  low_contrast,
  invalid_fit_score
};

/* Classify RESULT using MIN_CONTRAST as the smallest usable fitted
   positional colour contrast.  MIN_CONTRAST must be finite and nonnegative.  */
finetune_result_quality finetune_classify_result (
    const finetune_result &result, luminosity_t min_contrast);

/* Internal exact focus-screen cache access used by regression tests.  Normal
   finetune callers reach this cache through FINETUNE itself.  */
std::shared_ptr<screen> finetune_get_cached_screen_for_test (
    scr_type type, coord_t red_strip_width, coord_t green_strip_width,
    bool anticipate_sharpening,
    const std::array<sharpen_parameters, 3> &sharpen, bool parallel,
    bool *cache_hit, screen_filter_profile *filter_profile = nullptr);
void finetune_prune_screen_cache_for_test ();

/* Simulate collection of SCR through COLLECTION_SCR and return the average
   colors assigned to its red, green, and blue collecting patches in RET_RED,
   RET_GREEN, and RET_BLUE.  SIMULATED_SCREEN supplies a previously rendered
   finite capture when nonnull.  Otherwise SAMPLING, SHARPEN, and MAP describe
   the capture path to evaluate over AREA.  THRESHOLD excludes weak collecting
   patches.  Return false when one of the three collecting colors has no usable
   samples or when filtering fails.  */
bool determine_color_loss (rgbdata *ret_red, rgbdata *ret_green,
                           rgbdata *ret_blue, screen &scr,
                           screen &collection_scr,
                           simulated_screen *simulated_screen,
                           screen_sampling sampling, luminosity_t threshold,
                           const sharpen_parameters &sharpen, scr_to_img &map,
                           int_image_area area);
}
#endif
