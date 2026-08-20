#include <assert.h>
#include <algorithm>
#include <array>
#include <climits>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>


#include "include/colorscreen.h"
#include "include/imagedata.h"
#include "include/scr-to-img.h"
#include "include/finetune.h"
#include "include/scanner-blur-correction-parameters.h"
#include "include/color.h"
#include "include/matrix.h"
#include "include/mesh.h"
#include "screen.h"
#include "render.h"
#include "render-to-scr.h"
#include "simulate.h"
#include "include/spectrum-to-xyz.h"
#include "lru-cache.h"
#include "include/histogram.h"
#include "deconvolve.h"
#include "denoise.h"
#include "include/paget.h"
#include "include/dufaycolor.h"
#include "demosaic.h"
#include "analyze-base.h"
#include "finetune-int.h"
#include "gaussian-blur.h"
#include "nmsimplex.h"
#include "gsl-solver.h"


using namespace colorscreen;
namespace
{

/* Construct IEC-559 special values through their object representation.
   The clang buildbots use -ffast-math/-ffinite-math-only, under which spelling
   NaN or infinity as a floating constant is undefined and clang may discard
   or replace the value before a test can inspect it.  Volatile integer bits
   force the special value to be materialized at run time, which is also how
   non-finite values can enter the library from files and external data.  */
static luminosity_t
test_runtime_nan_luminosity ()
{
  static_assert (sizeof (luminosity_t) == sizeof (uint32_t));
  volatile uint32_t bits = UINT32_C (0x7fc00000);
  luminosity_t value;
  std::memcpy (&value, (const void *)&bits, sizeof (value));
  return value;
}

static coord_t
test_runtime_nan_coord ()
{
  static_assert (sizeof (coord_t) == sizeof (uint64_t));
  volatile uint64_t bits = UINT64_C (0x7ff8000000000000);
  coord_t value;
  std::memcpy (&value, (const void *)&bits, sizeof (value));
  return value;
}

static double
test_runtime_infinity ()
{
  volatile uint64_t bits = UINT64_C (0x7ff0000000000000);
  double value;
  std::memcpy (&value, (const void *)&bits, sizeof (value));
  return value;
}

static luminosity_t
test_runtime_infinity_luminosity ()
{
  static_assert (sizeof (luminosity_t) == sizeof (uint32_t));
  volatile uint32_t bits = UINT32_C (0x7f800000);
  luminosity_t value;
  std::memcpy (&value, (const void *)&bits, sizeof (value));
  return value;
}

/* Invalid problem used to exercise the GSL multifit allocation-failure path.
   GSL requires at least as many observations as fitted values.  */
class invalid_gsl_multifit_problem
{
public:
  double start[1] = { 0 };

  int num_values () const { return 1; }
  int num_observations () const { return 0; }
  double derivative_perturbation () const { return 1e-6; }
  bool verbose () const { return false; }
  void constrain (double *) {}
  int residuals (const double *, double *) { return GSL_SUCCESS; }
};

/* Verify the fast-math-safe quiet-NaN constructor, finite predicates, and the
   GSL failure sentinel that motivated the constructor.  */
bool
test_nonfinite_helpers ()
{
  volatile float volatile_float_nan = my_quiet_nan<float> ();
  volatile double volatile_double_nan = my_quiet_nan<double> ();
  float float_nan = volatile_float_nan;
  double double_nan = volatile_double_nan;
  uint32_t float_bits;
  uint64_t double_bits;
  std::memcpy (&float_bits, &float_nan, sizeof (float_bits));
  std::memcpy (&double_bits, &double_nan, sizeof (double_bits));

  if (my_isfinite (float_nan) || my_isfinite (double_nan)
      || float_bits != UINT32_C (0x7fc00000)
      || double_bits != UINT64_C (0x7ff8000000000000))
    {
      fprintf (stderr, "Fast-math-safe quiet-NaN construction failed\n");
      return false;
    }

  if (!my_isfinite ((float)1.25) || !my_isfinite ((double)-2.5))
    {
      fprintf (stderr, "Finite values were classified as non-finite\n");
      return false;
    }

  invalid_gsl_multifit_problem invalid_multifit;
  if (my_isfinite (gsl_multifit<double> (invalid_multifit)))
    {
      fprintf (stderr,
               "GSL multifit allocation failure did not return NaN\n");
      return false;
    }
  return true;
}

/* Zero-dimensional objective used to verify that SIMPLEX can evaluate a
   fully fixed model without constructing a degenerate simplex.  */
class zero_dimensional_simplex_problem
{
public:
  std::vector<double> start;
  int calls = 0;

  int num_values () const { return 0; }
  double epsilon () const { return 1e-6; }
  double scale () const { return 1; }
  bool verbose () const { return false; }
  void constrain (double *) {}
  double
  objfunc (double *)
  {
    calls++;
    return 3.25;
  }
};

bool
test_finetune_helpers ()
{
  const rgbdata fallback = { 0.01, 0.02, 0.03 };
  rgbdata dark
      = finetune_render_mix_dark ({ 0.2, 0.3, 0.5 }, 0.2, fallback);
  if (fabs (dark.red - 0.2) > 1e-6 || dark.red != dark.green
      || dark.red != dark.blue)
    {
      fprintf (stderr, "Unit-sum finetune mix-dark conversion failed\n");
      return false;
    }
  dark = finetune_render_mix_dark ({ 0.4, 0.6, 1.0 }, 0.2, fallback);
  if (fabs (dark.red - 0.1) > 1e-6 || dark.red != dark.green
      || dark.red != dark.blue)
    {
      fprintf (stderr, "Scaled finetune mix-dark conversion failed\n");
      return false;
    }
  dark = finetune_render_mix_dark ({ 1, -1, 0 }, 0.2, fallback);
  if (dark != fallback)
    {
      fprintf (stderr, "Zero-sum finetune mix-dark fallback failed\n");
      return false;
    }
  dark = finetune_render_mix_dark (
      { test_runtime_nan_luminosity (), 0, 0 }, 0.2,
      fallback);
  if (dark != fallback)
    {
      fprintf (stderr, "Non-finite finetune mix-dark fallback failed\n");
      return false;
    }

  std::vector<finetune_result> fit_results (5);
  fit_results[0].success = true;
  fit_results[0].uncertainty = 10;
  fit_results[1].success = true;
  fit_results[1].uncertainty = 4;
  fit_results[2].success = true;
  fit_results[2].uncertainty = 2;
  fit_results[3].success = false;
  fit_results[3].uncertainty = 0;
  fit_results[4].success = true;
  fit_results[4].uncertainty
      = test_runtime_nan_coord ();
  coord_t cutoff = -1;
  if (!finetune_retained_fit_score_cutoff (fit_results, 1, &cutoff)
      || cutoff != 10
      || !finetune_retained_fit_score_cutoff (fit_results, 0.5, &cutoff)
      || cutoff != 4
      || !finetune_retained_fit_score_cutoff (fit_results, 0, &cutoff)
      || cutoff != 2
      || finetune_retained_fit_score_cutoff (fit_results, -0.1, &cutoff)
      || finetune_retained_fit_score_cutoff (fit_results, 1.1, &cutoff))
    {
      fprintf (stderr, "Finetune fit-score retention cutoff failed\n");
      return false;
    }

  finetune_result quality_result;
  const luminosity_t minimum_contrast = 1 / (luminosity_t)1024;
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::solver_failure)
    {
      fprintf (stderr, "Failed finetune result was not classified\n");
      return false;
    }
  quality_result.success = true;
  quality_result.contrast
      = test_runtime_nan_luminosity ();
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::invalid_contrast)
    {
      fprintf (stderr, "Invalid finetune contrast was not classified\n");
      return false;
    }
  quality_result.contrast = minimum_contrast / 2;
  quality_result.uncertainty
      = test_runtime_nan_coord ();
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::invalid_fit_score)
    {
      fprintf (stderr,
               "Invalid finetune score was hidden by weak contrast\n");
      return false;
    }
  quality_result.uncertainty = 1;
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::low_contrast)
    {
      fprintf (stderr, "Weak finetune contrast was not rejected\n");
      return false;
    }
  quality_result.contrast = minimum_contrast;
  quality_result.uncertainty
      = test_runtime_nan_coord ();
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::invalid_fit_score)
    {
      fprintf (stderr, "Invalid finetune score was not classified\n");
      return false;
    }
  quality_result.uncertainty = 1;
  if (finetune_classify_result (quality_result, minimum_contrast)
      != finetune_result_quality::usable)
    {
      fprintf (stderr, "Identifiable finetune result was rejected\n");
      return false;
    }
  if (finetune_classify_result (quality_result, -1)
      != finetune_result_quality::invalid_contrast)
    {
      fprintf (stderr, "Invalid minimum contrast was accepted\n");
      return false;
    }

  if (finetune_flag_error (finetune_position)
      || finetune_flag_error (finetune_scanner_mtf_sigma
                              | finetune_scanner_mtf_defocus)
      || finetune_flag_error (finetune_scanner_mtf_sigma
                              | finetune_scanner_mtf_channel_defocus))
    {
      fprintf (stderr, "Valid finetune flag combination was rejected\n");
      return false;
    }
  if (!finetune_flag_error (finetune_coordinates
                            | finetune_guess_coordinates)
      || !finetune_flag_error (finetune_screen_blur
                               | finetune_screen_channel_blurs)
      || !finetune_flag_error (finetune_scanner_mtf_defocus
                               | finetune_scanner_mtf_channel_defocus)
      || !finetune_flag_error (finetune_screen_blur
                               | finetune_scanner_mtf_sigma))
    {
      fprintf (stderr, "Invalid finetune flag combination was accepted\n");
      return false;
    }

  finetune_focus_grid_interval interval;
  if (!finetune_focus_grid_interval_for_value ((coord_t)0.5, 2, 5,
                                                &interval)
      || interval.lower_index != 2 || interval.upper_index != 2
      || interval.lower != (coord_t)0.5
      || interval.upper != (coord_t)0.5 || interval.upper_weight != 0)
    {
      fprintf (stderr, "Exact nonlinear focus-grid node was not recognized\n");
      return false;
    }
  if (!finetune_focus_grid_interval_for_value ((coord_t)0.8125, 2, 5,
                                                &interval)
      || interval.lower_index != 2 || interval.upper_index != 3
      || fabs (interval.lower - 0.5) > 1e-12
      || fabs (interval.upper - 1.125) > 1e-12
      || fabs (interval.upper_weight - 0.5) > 1e-12)
    {
      fprintf (stderr, "Nonlinear focus-grid interpolation interval failed\n");
      return false;
    }
  if (!finetune_focus_grid_interval_for_value (-1, 2, 5, &interval)
      || interval.lower_index != 0 || interval.upper_index != 0
      || !finetune_focus_grid_interval_for_value (3, 2, 5, &interval)
      || interval.lower_index != 4 || interval.upper_index != 4
      || finetune_focus_grid_interval_for_value (0.5, 0, 5, &interval)
      || finetune_focus_grid_interval_for_value (0.5, 2, 1, &interval)
      || finetune_focus_grid_interval_for_value (0.5, 2, 65, &interval)
      || finetune_focus_grid_interval_for_value (0.5, 2, 5, nullptr))
    {
      fprintf (stderr, "Nonlinear focus-grid bounds validation failed\n");
      return false;
    }

  mtf_parameters start_mtf;
  start_mtf.model = mtf_model::empirical_fallback;
  start_mtf.blur_diameter = 4.75;
  if (finetune_initial_scanner_mtf_focus (start_mtf) != 0)
    {
      fprintf (stderr,
               "Empirical fallback did not preserve zero-blur initialization\n");
      return false;
    }
  start_mtf.model = mtf_model::physical_diffraction;
  start_mtf.scan_dpi = 4000;
  start_mtf.f_stop = 8;
  start_mtf.wavelength = 550;
  start_mtf.pixel_pitch = 3.76;
  start_mtf.defocus = 0.237;
  if (fabs (finetune_initial_scanner_mtf_focus (start_mtf) - 0.237)
      > 1e-12)
    {
      fprintf (stderr, "Physical focus warm start was lost\n");
      return false;
    }

  mtf_parameters focus_mtf;
  focus_mtf.model = mtf_model::physical_diffraction;
  focus_mtf.scan_dpi = 4000;
  focus_mtf.f_stop = 8;
  focus_mtf.wavelength = 550;
  focus_mtf.pixel_pitch = 3.76;
  focus_mtf.sensor_fill_factor = 1;
  coord_t useful_limit = 0;
  const coord_t screen_frequency = (coord_t)0.12;
  if (!finetune_useful_defocus_limit (focus_mtf, screen_frequency,
                                      (coord_t)0.05, 20, &useful_limit)
      || useful_limit <= 0 || useful_limit >= 20)
    {
      fprintf (stderr, "Useful physical-focus range was not detected\n");
      return false;
    }
  focus_mtf.defocus = useful_limit;
  if (fabs (focus_mtf.system_mtf (screen_frequency) - 0.05) > 1e-8)
    {
      fprintf (stderr,
               "Useful focus boundary does not match 5%% MTF: %.12g\n",
               focus_mtf.system_mtf (screen_frequency));
      return false;
    }
  focus_mtf.defocus = 0;
  const coord_t in_focus = focus_mtf.system_mtf (screen_frequency);
  if (in_focus >= 0.999
      || finetune_useful_defocus_limit (
          focus_mtf, screen_frequency, (in_focus + (coord_t)0.999) / 2,
          20, &useful_limit))
    {
      fprintf (stderr, "Unusable in-focus MTF was not rejected\n");
      return false;
    }

  mtf_parameters fallback_mtf;
  fallback_mtf.model = mtf_model::empirical_fallback;
  fallback_mtf.sigma = 0;
  fallback_mtf.sensor_fill_factor = 0;
  fallback_mtf.blur_diameter = 0;
  coord_t useful_blur_limit = 0;
  if (!finetune_useful_blur_diameter_limit (
          fallback_mtf, screen_frequency, (coord_t)0.05, 20,
          &useful_blur_limit)
      || useful_blur_limit <= 0 || useful_blur_limit >= 20)
    {
      fprintf (stderr, "Useful fallback-blur range was not detected\n");
      return false;
    }
  fallback_mtf.blur_diameter = useful_blur_limit;
  if (fabs (fallback_mtf.system_mtf (screen_frequency) - 0.05) > 1e-8)
    {
      fprintf (stderr,
               "Useful fallback boundary does not match 5%% MTF: %.12g\n",
               fallback_mtf.system_mtf (screen_frequency));
      return false;
    }
  fallback_mtf.blur_diameter = 0;
  if (finetune_useful_blur_diameter_limit (
          focus_mtf, screen_frequency, (coord_t)0.05, 20,
          &useful_blur_limit))
    {
      fprintf (stderr, "Physical model was accepted as fallback blur\n");
      return false;
    }

  zero_dimensional_simplex_problem zero;
  const double zero_result
      = simplex<double> (zero, nullptr, nullptr, false);
  if (zero_result != 3.25 || zero.calls != 1)
    {
      fprintf (stderr,
               "Zero-dimensional simplex failed: result %g, calls %i\n",
               zero_result, zero.calls);
      return false;
    }

  return true;
}

bool
test_finetune_focus_screen_cache ()
{
  std::array<sharpen_parameters, 3> sharpen;
  for (int c = 0; c < 3; c++)
    {
      sharpen[c].mode = sharpen_parameters::none;
      sharpen[c].scanner_mtf.model = mtf_model::empirical_fallback;
      sharpen[c].scanner_mtf.sensor_fill_factor = 0;
      sharpen[c].scanner_mtf.sigma = 0.8 + 0.4 * c;
      sharpen[c].scanner_mtf.blur_diameter = 0;
      sharpen[c].scanner_mtf_scale = (luminosity_t)0.012345;
    }
  sharpen_parameters *channels[3]
      = { &sharpen[0], &sharpen[1], &sharpen[2] };

  /* Capture transfer is active even when digital sharpening mode is NONE.
     Different channel MTFs must therefore not collapse to the first one.  */
  screen dot, filtered_dot;
  dot.initialize_dot ();
  if (!filtered_dot.initialize_with_sharpen_parameters (
          dot, channels, false, false))
    {
      fprintf (stderr, "Per-channel capture-MTF construction failed\n");
      return false;
    }
  double red_green_delta = 0;
  double green_blue_delta = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        red_green_delta
            += fabs (filtered_dot.mult[y][x][0]
                     - filtered_dot.mult[y][x][1]);
        green_blue_delta
            += fabs (filtered_dot.mult[y][x][1]
                     - filtered_dot.mult[y][x][2]);
      }
  if (red_green_delta < 1e-5 || green_blue_delta < 1e-5)
    {
      fprintf (stderr,
               "Per-channel capture MTFs were collapsed: deltas %g %g\n",
               red_green_delta, green_blue_delta);
      return false;
    }

  finetune_prune_screen_cache_for_test ();
  const coord_t red_width = (coord_t)0.314159;
  const coord_t green_width = (coord_t)0.271828;
  screen source, expected;
  source.initialize (Joly, red_width, green_width);
  screen_filter_profile expected_profile;
  if (!expected.initialize_with_sharpen_parameters (
          source, channels, false, false, &expected_profile))
    return false;

  bool cache_hit = true;
  screen_filter_profile miss_profile;
  std::shared_ptr<screen> first = finetune_get_cached_screen_for_test (
      Joly, red_width, green_width, false, sharpen, false, &cache_hit,
      &miss_profile);
  if (!first || cache_hit || miss_profile.mtf_precompute_calls
      || !miss_profile.empirical_focus_transfer_builds
      || !miss_profile.screen_forward_ffts
      || !miss_profile.screen_inverse_ffts)
    {
      fprintf (stderr, "Exact finetune focus-cache miss was not recorded\n");
      return false;
    }
  luminosity_t delta = 0;
  if (!expected.almost_equal_p (*first, &delta, (luminosity_t)1e-8))
    {
      fprintf (stderr,
               "Cached exact focus screen differs from direct build by %g\n",
               (double)delta);
      return false;
    }

  /* A different exact transfer must miss the final-screen cache but reuse the
     immutable source spectrum.  It should therefore perform inverse channel
     transforms without repeating any source forward FFT.  */
  std::array<sharpen_parameters, 3> changed_sharpen = sharpen;
  for (int c = 0; c < 3; c++)
    {
      changed_sharpen[c].scanner_mtf.sigma += (luminosity_t)0.173;
      /* Exercise a nonzero compact fallback diameter too.  This is the
         adaptive fallback path accelerated below, and historically it could
         take the wrapped-PSF implementation.  */
      changed_sharpen[c].scanner_mtf.blur_diameter = (luminosity_t)2.75;
    }
  sharpen_parameters *changed_channels[3]
      = { &changed_sharpen[0], &changed_sharpen[1], &changed_sharpen[2] };
  screen changed_expected;
  if (!changed_expected.initialize_with_sharpen_parameters (
          source, changed_channels, false, false))
    return false;
  cache_hit = true;
  screen_filter_profile source_hit_profile;
  std::shared_ptr<screen> changed
      = finetune_get_cached_screen_for_test (
          Joly, red_width, green_width, false, changed_sharpen, false,
          &cache_hit, &source_hit_profile);
  if (!changed || cache_hit || source_hit_profile.screen_forward_ffts
      || !source_hit_profile.screen_inverse_ffts)
    {
      fprintf (stderr,
               "Prepared finetune source spectrum was not reused\n");
      return false;
    }
  if (!changed_expected.almost_equal_p (
          *changed, &delta, (luminosity_t)1e-8))
    {
      fprintf (stderr,
               "Prepared-source focus screen differs from direct build by "
               "%g\n",
               (double)delta);
      return false;
    }

  cache_hit = false;
  screen_filter_profile hit_profile;
  std::shared_ptr<screen> second = finetune_get_cached_screen_for_test (
      Joly, red_width, green_width, false, sharpen, true, &cache_hit,
      &hit_profile);
  if (!second || !cache_hit || second.get () != first.get ()
      || hit_profile.mtf_precompute_calls
      || hit_profile.mtf_psf_precompute_calls
      || hit_profile.empirical_focus_transfer_builds
      || hit_profile.direct_transfer_builds
      || hit_profile.wrapped_psf_builds || hit_profile.kernel_forward_ffts
      || hit_profile.screen_forward_ffts
      || hit_profile.screen_inverse_ffts)
    {
      fprintf (stderr, "Exact finetune focus-cache hit was not reused\n");
      return false;
    }

  first.reset ();
  second.reset ();
  changed.reset ();
  finetune_prune_screen_cache_for_test ();

  /* The dense displacement approximation linearly combines two exact
     physical-defocus nodes.  Check a representative midpoint against an
     independently filtered screen at the requested focus.  */
  std::array<sharpen_parameters, 3> physical;
  for (int c = 0; c < 3; c++)
    {
      physical[c].mode = sharpen_parameters::none;
      physical[c].scanner_mtf.model = mtf_model::physical_diffraction;
      physical[c].scanner_mtf.scan_dpi = 4000;
      physical[c].scanner_mtf.f_stop = 8;
      physical[c].scanner_mtf.wavelength = 550;
      physical[c].scanner_mtf.pixel_pitch = 3.76;
      physical[c].scanner_mtf.sensor_fill_factor = 1;
      physical[c].scanner_mtf_scale = (luminosity_t)0.12;
    }
  /* The physical-focus transfer cache excludes DEFOCUS from its key and
     prepares only invariant terms.  Verify both the cache contract and the
     resulting signed transfer against the independent general MTF path.  */
  mtf_focus_transfer::prune_cache ();
  bool physical_transfer_cache_hit = true;
  std::shared_ptr<const mtf_focus_transfer> prepared_transfer
      = mtf_focus_transfer::get (physical[0].scanner_mtf,
                                 &physical_transfer_cache_hit);
  if (!prepared_transfer || physical_transfer_cache_hit)
    {
      fprintf (stderr, "Physical focus-transfer cache miss was not recorded\n");
      return false;
    }
  mtf_parameters comparison_parameters = physical[0].scanner_mtf;
  comparison_parameters.defocus = (coord_t)0.5;
  std::shared_ptr<const mtf_focus_transfer> reused_transfer
      = mtf_focus_transfer::get (comparison_parameters,
                                 &physical_transfer_cache_hit);
  if (!reused_transfer || !physical_transfer_cache_hit
      || reused_transfer.get () != prepared_transfer.get ())
    {
      fprintf (stderr, "Physical focus-transfer state was not reused\n");
      return false;
    }
  precomputed_function<double> prepared_table;
  if (!reused_transfer->precompute (comparison_parameters.defocus,
                                    prepared_table))
    return false;
  std::shared_ptr<mtf> comparison_mtf
      = mtf::get_mtf (comparison_parameters, nullptr);
  if (!comparison_mtf || !comparison_mtf->precompute (nullptr, false))
    return false;
  for (int i = 0; i <= 1000; i++)
    {
      const double frequency = i * 0.001;
      const double error
          = fabs (prepared_table.apply (frequency)
                  - comparison_mtf->get_transfer (frequency));
      if (error > 2e-13)
        {
          fprintf (stderr,
                   "Prepared physical transfer differs at %g by %.17g\n",
                   frequency, error);
          return false;
        }
    }

  /* Exercise the complete prepared-spectrum path on one Fourier harmonic.
     Its recovered amplitude must be the signed transfer coefficient itself,
     independent of the general sampled-PSF implementation.  */
  constexpr int focus_test_bin = 24;
  constexpr double focus_test_frequency
      = focus_test_bin / (double)screen::size;
  screen focus_test_source, focus_test_result;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        {
          focus_test_source.mult[y][x][c]
              = (luminosity_t)(0.5
                               + 0.2
                                     * std::cos (2 * M_PI * focus_test_bin * x
                                                 / screen::size));
          focus_test_source.add[y][x][c] = 0;
        }
  screen_filter_source focus_test_spectrum;
  if (!focus_test_source.prepare_filter_source (focus_test_spectrum))
    return false;
  std::array<sharpen_parameters, 3> focus_test_sharpen = physical;
  for (int c = 0; c < 3; c++)
    {
      focus_test_sharpen[c].scanner_mtf = comparison_parameters;
      focus_test_sharpen[c].scanner_mtf_scale
          = (luminosity_t)(1.0 / screen::size);
    }
  sharpen_parameters *focus_test_channels[3]
      = { &focus_test_sharpen[0], &focus_test_sharpen[1],
          &focus_test_sharpen[2] };
  screen_filter_profile focus_test_profile;
  if (!focus_test_result.initialize_with_sharpen_parameters (
          focus_test_spectrum, focus_test_channels, false, false,
          &focus_test_profile))
    return false;
  double focus_test_sum = 0;
  double focus_test_norm = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        const double cosine
            = std::cos (2 * M_PI * focus_test_bin * x / screen::size);
        focus_test_sum
            += ((double)focus_test_result.mult[y][x][0] - 0.5) * cosine;
        focus_test_norm += cosine * cosine;
      }
  const double focus_test_response
      = focus_test_sum / focus_test_norm / 0.2;
  const double focus_test_expected
      = comparison_mtf->get_transfer (focus_test_frequency);
  if (!(focus_test_expected < 0)
      || fabs (focus_test_response - focus_test_expected) > 2e-6
      || focus_test_profile.mtf_precompute_calls
      || focus_test_profile.mtf_psf_precompute_calls
      || focus_test_profile.physical_focus_cache_hits != 1
      || focus_test_profile.physical_focus_transfer_builds != 1
      || focus_test_profile.direct_transfer_builds != 1
      || focus_test_profile.wrapped_psf_builds
      || focus_test_profile.kernel_forward_ffts)
    {
      fprintf (stderr,
               "Prepared physical harmonic response differs: expected %g "
               "got %g\n",
               focus_test_expected, focus_test_response);
      return false;
    }

  coord_t max_defocus = 0;
  if (!finetune_useful_defocus_limit (
          physical[0].scanner_mtf, physical[0].scanner_mtf_scale,
          (coord_t)0.05, 20, &max_defocus))
    return false;
  constexpr int nodes = 33;
  constexpr int lower_index = 11;
  const coord_t lower_defocus
      = max_defocus * ((coord_t)lower_index / (nodes - 1))
        * ((coord_t)lower_index / (nodes - 1));
  const coord_t upper_defocus
      = max_defocus * ((coord_t)(lower_index + 1) / (nodes - 1))
        * ((coord_t)(lower_index + 1) / (nodes - 1));
  const coord_t target_defocus
      = (lower_defocus + upper_defocus) * (coord_t)0.5;
  finetune_focus_grid_interval physical_interval;
  if (!finetune_focus_grid_interval_for_value (
          target_defocus, max_defocus, nodes, &physical_interval)
      || physical_interval.lower_index != lower_index
      || physical_interval.upper_index != lower_index + 1
      || fabs (physical_interval.upper_weight - 0.5) > 1e-10)
    return false;

  std::array<sharpen_parameters, 3> lower_physical = physical;
  std::array<sharpen_parameters, 3> upper_physical = physical;
  std::array<sharpen_parameters, 3> target_physical = physical;
  for (int c = 0; c < 3; c++)
    {
      lower_physical[c].scanner_mtf.defocus = lower_defocus;
      upper_physical[c].scanner_mtf.defocus = upper_defocus;
      target_physical[c].scanner_mtf.defocus = target_defocus;
    }
  cache_hit = false;
  std::shared_ptr<screen> lower_screen
      = finetune_get_cached_screen_for_test (
          Dufay, (coord_t)0.45, (coord_t)0.35, false, lower_physical,
          false, &cache_hit);
  if (!lower_screen || cache_hit)
    return false;
  screen_filter_profile physical_source_hit_profile;
  std::shared_ptr<screen> upper_screen
      = finetune_get_cached_screen_for_test (
          Dufay, (coord_t)0.45, (coord_t)0.35, false, upper_physical,
          false, &cache_hit, &physical_source_hit_profile);
  if (!upper_screen || physical_source_hit_profile.screen_forward_ffts
      || physical_source_hit_profile.screen_inverse_ffts != 3
      || physical_source_hit_profile.mtf_precompute_calls
      || physical_source_hit_profile.mtf_psf_precompute_calls
      || physical_source_hit_profile.physical_focus_cache_hits != 1
      || physical_source_hit_profile.physical_focus_cache_misses
      || physical_source_hit_profile.physical_focus_transfer_builds != 1
      || physical_source_hit_profile.direct_transfer_builds != 1
      || physical_source_hit_profile.wrapped_psf_builds
      || physical_source_hit_profile.kernel_forward_ffts)
    {
      fprintf (stderr,
               "Prepared physical focus did not use the direct cached "
               "transfer path\n");
      return false;
    }

  /* The dense focus approximation materializes the interpolated screen on
     every new simplex state.  Verify that the flat SIMD-friendly helper is
     numerically equivalent to the historical three-level loop, including
     non-midpoint weights.  */
  constexpr luminosity_t interpolation_weight = (luminosity_t)0.371;
  const luminosity_t lower_weight = 1 - interpolation_weight;
  screen reference_interpolation, flat_interpolation;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        reference_interpolation.mult[y][x][c]
            = lower_screen->mult[y][x][c] * lower_weight
              + upper_screen->mult[y][x][c] * interpolation_weight;
  finetune_interpolate_screen_mult (
      flat_interpolation, *lower_screen, *upper_screen,
      interpolation_weight);
  double interpolation_delta = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        interpolation_delta
            = std::max (interpolation_delta,
                        fabs ((double)reference_interpolation.mult[y][x][c]
                              - flat_interpolation.mult[y][x][c]));
  if (interpolation_delta > 2e-7)
    {
      fprintf (stderr,
               "Flat focus-screen interpolation differs by %.12g\n",
               interpolation_delta);
      return false;
    }

  screen physical_source, exact_upper, exact_target;
  physical_source.initialize (Dufay, (coord_t)0.45, (coord_t)0.35);
  screen_filter_source prepared_physical_source;
  if (!physical_source.prepare_filter_source (prepared_physical_source))
    return false;
  sharpen_parameters *upper_channels[3]
      = { &upper_physical[0], &upper_physical[1], &upper_physical[2] };
  if (!exact_upper.initialize_with_sharpen_parameters (
          prepared_physical_source, upper_channels, false, false)
      || !exact_upper.almost_equal_p (
          *upper_screen, &delta, (luminosity_t)1e-8))
    {
      fprintf (stderr,
               "Prepared physical-defocus screen differs from direct build "
               "by %g\n",
               (double)delta);
      return false;
    }
  sharpen_parameters *target_channels[3]
      = { &target_physical[0], &target_physical[1], &target_physical[2] };
  if (!exact_target.initialize_with_sharpen_parameters (
          prepared_physical_source, target_channels, false, false))
    return false;
  long double squared_error = 0;
  double max_error = 0;
  size_t samples = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        {
          const double approximated
              = ((double)lower_screen->mult[y][x][c]
                 + (double)upper_screen->mult[y][x][c])
                * 0.5;
          const double error
              = approximated - exact_target.mult[y][x][c];
          squared_error += error * error;
          max_error = std::max (max_error, fabs (error));
          samples++;
        }
  const double rms_error = std::sqrt ((double)(squared_error / samples));
  if (max_error > 0.01 || rms_error > 0.002)
    {
      fprintf (stderr,
               "Interpolated physical focus screen is inaccurate: max %g, "
               "RMS %g\n",
               max_error, rms_error);
      return false;
    }

  lower_screen.reset ();
  upper_screen.reset ();
  finetune_prune_screen_cache_for_test ();

  /* Different focus states can be built concurrently by adaptive worker
     threads.  They must share one source-spectrum construction even though
     every final-screen key is distinct.  */
  constexpr int parallel_focus_states = 4;
  std::array<std::shared_ptr<screen>, parallel_focus_states> parallel_screens;
  std::array<screen_filter_profile, parallel_focus_states> parallel_profiles;
  std::array<bool, parallel_focus_states> parallel_cache_hits = {};
#pragma omp parallel for
  for (int i = 0; i < parallel_focus_states; i++)
    {
      std::array<sharpen_parameters, 3> state = physical;
      for (int c = 0; c < 3; c++)
        state[c].scanner_mtf.defocus = (coord_t)0.11 + (coord_t)0.07 * i;
      parallel_screens[i] = finetune_get_cached_screen_for_test (
          Dufay, (coord_t)0.45, (coord_t)0.35, false, state, false,
          &parallel_cache_hits[i], &parallel_profiles[i]);
    }
  uint64_t parallel_forward_ffts = 0;
  uint64_t parallel_inverse_ffts = 0;
  for (int i = 0; i < parallel_focus_states; i++)
    {
      if (!parallel_screens[i] || parallel_cache_hits[i])
        return false;
      parallel_forward_ffts += parallel_profiles[i].screen_forward_ffts;
      parallel_inverse_ffts += parallel_profiles[i].screen_inverse_ffts;
      parallel_screens[i].reset ();
    }
  if (parallel_forward_ffts != 3
      || parallel_inverse_ffts != 3 * parallel_focus_states)
    {
      fprintf (stderr,
               "Concurrent focus states used %llu source forward and %llu "
               "inverse FFTs\n",
               (unsigned long long)parallel_forward_ffts,
               (unsigned long long)parallel_inverse_ffts);
      return false;
    }
  finetune_prune_screen_cache_for_test ();
  return true;
}

bool
test_scanner_blur_correction_contract ()
{
  scanner_blur_correction_parameters empty;
  if (empty.save (nullptr))
    return false;
  const char *null_error = nullptr;
  if (empty.load (nullptr, &null_error) || !null_error
      || empty.load (nullptr, nullptr))
    return false;

  scanner_blur_correction_parameters correction;
  if (!correction.alloc (
          2, 2, scanner_blur_correction_parameters::blur_radius))
    return false;
  correction.set_correction (0, 0, 0.1);
  correction.set_correction (1, 0, 0.2);
  correction.set_correction (0, 1, 0.3);
  correction.set_correction (1, 1, 0.4);
  if (!correction.alloc_diagnostics ())
    return false;
  scanner_blur_correction_parameters::cell_diagnostics diagnostics
      = { 0.025, 0.4, 3, 4 };
  correction.set_diagnostics (1, 1, diagnostics);
  const scanner_blur_correction_parameters::cell_diagnostics *saved_diagnostics
      = correction.get_diagnostics (1, 1);
  if (!correction.has_diagnostics () || !saved_diagnostics
      || my_fabs (saved_diagnostics->robust_spread - 0.025) > 1e-6
      || my_fabs (saved_diagnostics->mean_contrast - 0.4) > 1e-6
      || saved_diagnostics->accepted_samples != 3
      || saved_diagnostics->total_samples != 4)
    return false;

  FILE *diagnostic_csv = tmpfile ();
  if (!diagnostic_csv || !correction.save_diagnostics (diagnostic_csv))
    {
      if (diagnostic_csv)
        fclose (diagnostic_csv);
      return false;
    }
  rewind (diagnostic_csv);
  char diagnostic_line[512];
  if (!fgets (diagnostic_line, sizeof (diagnostic_line), diagnostic_csv)
      || strcmp (diagnostic_line,
                 "x,y,correction_mode,correction,robust_spread,accepted_samples,"
                 "total_samples,accepted_fraction,mean_contrast\n"))
    {
      fclose (diagnostic_csv);
      return false;
    }
  bool found_diagnostic = false;
  while (fgets (diagnostic_line, sizeof (diagnostic_line), diagnostic_csv))
    {
      int x, y, accepted, total;
      char mode[32];
      double value, spread, fraction, contrast;
      if (sscanf (diagnostic_line, "%d,%d,%31[^,],%lf,%lf,%d,%d,%lf,%lf",
                  &x, &y, mode, &value, &spread, &accepted, &total, &fraction,
                  &contrast)
          != 9)
        {
          fclose (diagnostic_csv);
          return false;
        }
      if (x == 1 && y == 1)
        {
          found_diagnostic = true;
          if (strcmp (mode, "blur-radius")
              || my_fabs (value - 0.4) > 1e-6
              || my_fabs (spread - 0.025) > 1e-6 || accepted != 3
              || total != 4 || my_fabs (fraction - 0.75) > 1e-6
              || my_fabs (contrast - 0.4) > 1e-6)
            {
              fclose (diagnostic_csv);
              return false;
            }
        }
    }
  fclose (diagnostic_csv);
  if (!found_diagnostic)
    return false;
  const uint64_t original_id = correction.id;

  if (correction.alloc (0, 2,
                        scanner_blur_correction_parameters::blur_radius)
      || correction.alloc (
          2, 2, scanner_blur_correction_parameters::max_correction)
      || correction.alloc (
          INT_MAX, INT_MAX,
          scanner_blur_correction_parameters::blur_radius))
    return false;
  if (correction.id != original_id || correction.get_width () != 2
      || correction.get_height () != 2
      || correction.get_mode ()
             != scanner_blur_correction_parameters::blur_radius
      || my_fabs (correction.get_correction (1, 1) - 0.4) > 1e-6
      || !correction.has_diagnostics ())
    return false;

  FILE *malformed = tmpfile ();
  if (!malformed)
    return false;
  fprintf (malformed, "scanner_blur_correction_dimensions: 1 1\n"
                      "scanner_blur_correction_type: blur-radius\n"
                      "scanner_blur_correction_gaussian_blurs: 1 1 1\n"
                      "scanner_blur_correction_end\n");
  rewind (malformed);
  const char *error = nullptr;
  const bool malformed_loaded = correction.load (malformed, &error);
  fclose (malformed);
  if (malformed_loaded || !error || correction.id != original_id
      || correction.get_width () != 2 || correction.get_height () != 2
      || my_fabs (correction.get_correction (1, 1) - 0.4) > 1e-6
      || !correction.has_diagnostics ())
    return false;

  FILE *saved = tmpfile ();
  if (!saved || !correction.save (saved))
    {
      if (saved)
        fclose (saved);
      return false;
    }
  rewind (saved);
  scanner_blur_correction_parameters loaded;
  error = nullptr;
  const bool load_ok = loaded.load (saved, &error);
  fclose (saved);
  if (!load_ok || error || loaded.get_width () != 2
      || loaded.get_height () != 2
      || loaded.get_mode ()
             != scanner_blur_correction_parameters::blur_radius
      || my_fabs (loaded.get_correction (0, 0) - 0.1) > 1e-6
      || my_fabs (loaded.get_correction (1, 1) - 0.4) > 1e-6
      || loaded.has_diagnostics ())
    return false;

  const uint64_t before_realloc = loaded.id;
  if (!loaded.alloc (1, 3,
                     scanner_blur_correction_parameters::mtf_defocus)
      || loaded.id == before_realloc || loaded.get_width () != 1
      || loaded.get_height () != 3
      || loaded.get_mode ()
             != scanner_blur_correction_parameters::mtf_defocus
      || loaded.has_diagnostics ())
    return false;
  for (int y = 0; y < 3; y++)
    if (loaded.get_correction (0, y) != 0)
      return false;
  return true;
}

inline int
fast_rand16 (unsigned int *g_seed)
{
  *g_seed = (214013 * *g_seed + 2531011);
  return ((*g_seed) >> 16) & 0x7FFF;
}

/* Random number generator used by RANSAC.  It is re-initialized every time
   RANSAC is run so results are deterministic.  */
inline int
fast_rand32 (unsigned int *g_seed)
{
  return fast_rand16 (g_seed) | (fast_rand16 (g_seed) << 15);
}

void
test_matrix ()
{
  /* Simple unit test that inversion works. */
  matrix2x2<double> m1 (1, 2, 3, 4);
  matrix2x2<double> m2 (5, 6, 7, 8);
  matrix<double, 2> mm = m1 * m2;
  /* Test that operator() works and points to the right elements.  */
  assert (m1(0, 0) == 1 && m1(0, 1) == 2 && m1(1, 0) == 3 && m1(1, 1) == 4);
  assert (mm(0, 0) == 19 && mm(1, 1) == 50);
  assert (mm(0, 0) == 19 && mm(0, 1) == 22 && mm(1, 0) == 43 && mm(1, 1) == 50);
  mm = m1 * m1.invert ();
  assert (mm(0, 0) == 1 && mm(1, 0) == 0 && mm(0, 1) == 0 && mm(1, 1) == 1);
  matrix4x4<double> m;

  double xr, yr;
  for (int i = 0; i < 100; i++)
    {
      m.randomize ();
      double x = rand () % 100;
      double y = rand () % 100;
      point_t p = m.perspective_transform ({ (coord_t) x, (coord_t) y });
      p = m.inverse_perspective_transform (p);
      xr = p.x; yr = p.y;
      if (fabs (x - xr) > 0.1 || fabs (y - yr) > 0.1)
        {
          printf ("%f %f %f %f %f %f\n", xr, yr, x, y, fabs (x - xr),
                  fabs (y - yr));
          abort ();
        }
    }

  matrix4x4<double> mm1 (2, 0, 0, 1, 0, 2, 5, 0, 2, 0, 2, 0, 0, 0, 0, 2);
  matrix4x4<double> mm2 = mm1.invert ();
  matrix<double, 4> mm3 = mm1 * mm2;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (fabs (mm3(j, i) - (double)(i == j)) > 0.0001)
        {
          mm3.print (stderr);
          abort ();
        }
  matrix3x3<double> mm4 (2, 0, 0, 0, 2, 5, 2, 0, 2);
  matrix3x3<double> mm5 = mm4.invert ();
  matrix<double, 3> mm6 = mm4 * mm5;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (fabs (mm6(j, i) - (double)(i == j)) > 0.0001)
        {
          mm6.print (stderr);
          abort ();
        }
}
void
test_color ()
{
  xyz white = xyz::from_srgb (1, 1, 1);
  assert (fabs (white.x - 0.9505) < 0.0001 && fabs (white.y - 1) < 0.0001
          && fabs (white.z - 0.9505) < 1.0888);
  luminosity_t r, g, b;
  xyz_to_srgb (0.25, 0.40, 0.1, &r, &g, &b);
  assert (fabs (r - 0.4174) < 0.0001 && fabs (g - 0.7434) < 0.0001
          && fabs (b - 0.2152) < 1.0888);
}
bool
compare_scr_to_img (const char *test_name, scr_to_img_parameters & param,
		    scr_to_img_parameters & param2, solver_parameters *sparam,
		    image_data & img, bool keep0, bool lens_correction,
		    coord_t epsilon)
{
  scr_to_img map, map2;
  if (!map.set_parameters (param, img) || !map2.set_parameters (param2, img))
    {
      printf ("set-parameters failed\n");
      return false;
    }
  const int grid = 5;

  struct data
  {
    coord_t avg, max;
    point_t avgoffset;
  } data[grid][grid];

  point_t offset = { 0, 0 };
  point_t doffset = { 0, 0 };
  if (!keep0)
    {
      point_t imgp
	= { (coord_t) (img.width / 2), (coord_t) (img.height / 2) };
      point_t scr1 = map.to_scr (imgp);
      point_t scr2 = map2.to_scr (imgp);
      if (param.type == Finlay || param.type == Paget || param.type == Thames)
	{
	  int_point_t int_offset = ((scr1 - scr2) * 2).nearest ();
	  offset.x = int_offset.x * (coord_t) 0.5;
	  offset.y = int_offset.y * (coord_t) 0.5;
	}
      else
	{
	  int_point_t int_offset = (scr1 - scr2).nearest ();
	  offset.x = int_offset.x;
	  offset.y = int_offset.y;
	}
      doffset = scr1 - scr2;
    }

  for (int y = 0; y < grid; y++)
    for (int x = 0; x < grid; x++)
      {
	int xmin = x * img.width / grid;
	int xmax = (x + 1) * img.width / grid;
	int ymin = y * img.height / grid;
	int ymax = (y + 1) * img.height / grid;
	coord_t sum = 0, maxv = 0;
	point_t offavg = { 0, 0 };
	for (int y = ymin; y < ymax; y += 5)
	  for (int x = xmin; x < xmax; x += 5)
	    {
	      point_t imgp1 = { (coord_t) x, (coord_t) y };
	      // point_t scr1 = map.to_scr (imgp1);
	      point_t scr2 = map2.to_scr (imgp1) + offset;
	      point_t imgp2 = map.to_img (scr2);
	      coord_t dist = imgp1.dist_from (imgp2);
	      offavg += imgp2 - imgp1;
	      sum += dist;
	      if (maxv < dist)
		maxv = dist;
	    }
	coord_t scale = 1 / ((xmax - xmin - 1) * (coord_t) (ymax - ymin - 1));
	offavg *= scale;
	data[y][x].avg = sum * scale;
	data[y][x].max = maxv;
	data[y][x].avgoffset = offavg;
      }

  bool ok = true;
  printf ("\n%s test with scanner %s, process %s and tolerance %f\naverage "
	  "distances:",
	  test_name, scanner_type_names[(int) param.scanner_type].name,
	  scr_names[(int) param.type].name, epsilon);
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %5.4f%c", data[y][x].avg,
		  data[y][x].avg > epsilon ? '!' : ' ');
	  if (data[y][x].avg > epsilon)
	    ok = false;
	}
    }
  printf ("\nmax distances:");
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %5.4f%c", data[y][x].max,
		  data[y][x].max > epsilon ? '!' : ' ');
	  if (data[y][x].max > epsilon)
	    ok = false;
	}
    }
  printf ("\noffsets (offset in the center %f,%f compensated to %f,%f):",
	  doffset.x, doffset.y, offset.x, offset.y);
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %+5.4f,%+5.4f%c", data[y][x].avgoffset.x,
		  data[y][x].avgoffset.y,
		  data[y][x].avgoffset.length () > epsilon ? '!' : ' ');
	  if (data[y][x].avgoffset.length () > epsilon)
	    ok = false;
	}
    }

  printf ("\nCoordinate1 original: %f,%f solved: %f,%f dist:%f \n",
	  param.coordinate1.x, param.coordinate1.y, param2.coordinate1.x,
	  param2.coordinate1.y,
	  param.coordinate1.dist_from (param2.coordinate1));
  printf ("Coordinate2 original: %f,%f solved: %f,%f dist:%f \n",
	  param.coordinate2.x, param.coordinate2.y, param2.coordinate2.x,
	  param2.coordinate2.y,
	  param.coordinate2.dist_from (param2.coordinate2));
  printf ("tilts original: %f,%f solved: %f,%f\n", param.tilt_x, param.tilt_y,
	  param2.tilt_x, param2.tilt_y);
  if (lens_correction)
    {
      printf ("Lens center original: %f,%f solved: %f,%f dist:%f\n",
	      param.lens_correction.center.x, param.lens_correction.center.y,
	      param2.lens_correction.center.x,
	      param2.lens_correction.center.y,
	      param.lens_correction.center.dist_from (param2.lens_correction.
						      center));
      printf ("Lens correction coeeficients original: %f,%f,%f,%f solved: "
	      "%f,%f,%f,%f\n", param.lens_correction.kr[0],
	      param.lens_correction.kr[1], param.lens_correction.kr[2],
	      param.lens_correction.kr[3], param2.lens_correction.kr[0],
	      param2.lens_correction.kr[1], param2.lens_correction.kr[2],
	      param2.lens_correction.kr[3]);
    }

  if (param.scanner_type != param2.scanner_type)
    {
      printf ("Scanner type mismatch\n");
      ok = false;
    }
  if (!ok)
    {
      printf ("\nInput:\n");
      if (!save_csp (stdout, &param, NULL, NULL, sparam))
	{
	  /* Ignore failure.  */
	}
      printf ("\nSolution:\n");
      if (!save_csp (stdout, &param2, NULL, NULL, NULL))
	{
	  /* Ignore failure.  */
	}
    }
  fflush (stdout);
  return ok;
}

bool
do_test_homography (scr_to_img_parameters &param, int width, int height,
                    bool lens_correction, coord_t epsilon,
                    coord_t lens_center_distance = 0)
{
  scr_to_img map;
  image_data img;
  unsigned int g_seed = 0;
  if (!img.set_dimensions (width, height))
    return false;
  if (!map.set_parameters (param, img))
    {
      printf ("Set parameters failed\n");
      return false;
    }
  solver_parameters sparam;
  sparam.optimize_lens = lens_correction;
  sparam.lens_center_distance = lens_center_distance;
  int xstep = (width + 99) / 11;
  int ystep = (height + 99) / 10;
  for (int y = 0; y < height; y += ystep)
    for (int x = 0; x < width; x += xstep)
      {
        point_t img = { (coord_t)x, (coord_t)y };
        point_t scr = map.to_scr (img);
        /* Add 20% outliers */
        if (!lens_correction && !(fast_rand16 (&g_seed) % 4))
          {
            img.x += (fast_rand16 (&g_seed) % 16) - 8;
            img.y += (fast_rand16 (&g_seed) % 16) - 8;
          }
        sparam.add_point (img, scr, solver_parameters::red);
      }
  scr_to_img_parameters param2;
  param2.scanner_type = param.scanner_type;
  solver (&param2, img, sparam);
  return compare_scr_to_img (
      lens_correction ? "Lens correction" : "RANSAC homography", param, param2,
      &sparam, img, true, lens_correction, epsilon);
}
bool
test_homography (bool lens_correction, bool joly, coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = { (coord_t)300, (coord_t)300 };
  param.coordinate1 = { (coord_t)5, (coord_t)1.2 };
  param.coordinate2 = { (coord_t)-1.4, (coord_t)5.2 };
  param.tilt_x = 0.0001;
  param.tilt_y = 0.00001;
  if (joly)
    param.type = Joly;
  if (lens_correction)
    {
      param.lens_correction.center = { 0.4, 0.6 };
      param.lens_correction.kr[1] = 0.01;
      param.lens_correction.kr[2] = 0.03;
      param.lens_correction.kr[3] = 0.01;
      assert (param.lens_correction.normalize ());
    }
  for (int scanner = 0; scanner < max_scanner_type; scanner++)
    {
      param.scanner_type = (enum scanner_type)scanner;
      ok &= do_test_homography (param, 1024, 1024, lens_correction, epsilon);
    }

  /* An exactly distortion-free image still contains the perspective/tilt
     above, but it contains no information about the optical center.  The
     profiled lens Jacobian must therefore be rank deficient after the best
     homography is refitted.  Verify that lens optimization keeps the identity
     profile instead of using radial terms to imitate the perspective.  */
  if (lens_correction && !joly)
    {
      image_data img;
      if (!img.set_dimensions (1024, 1024))
        return false;
      scr_to_img_parameters truth = param;
      truth.scanner_type = fixed_lens;
      truth.lens_correction = lens_warp_correction_parameters ();
      scr_to_img truth_map;
      if (!truth_map.set_parameters (truth, img))
        return false;
      solver_parameters sparam;
      for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
          {
            point_t image = {(coord_t)(111 * x), (coord_t)(111 * y)};
            sparam.add_point (image, truth_map.to_scr (image),
                              solver_parameters::red);
          }
      lens_warp_correction_parameters retained_profile;
      retained_profile.center = {0.43, 0.57};
      retained_profile.kr[1] = 0.005;
      if (!retained_profile.normalize ())
        return false;
      scr_to_img_parameters fitted;
      fitted.scanner_type = fixed_lens;
      fitted.lens_correction = retained_profile;
      const coord_t objective = solver (&fitted, img, sparam);
      if (!(objective >= 0 && objective < 1e30)
          || !(fitted.lens_correction == retained_profile))
        {
          fprintf (stderr,
                   "Unidentifiable lens fit replaced the retained profile\n");
          ok = false;
        }
    }

  /* A scanner capture can be cropped far from the fixed lens optical axis.
     Verify that the explicit center-distance setting affects the actual
     nonlinear solver, rather than only the post-fit plausibility check.  */
  if (lens_correction && !joly)
    {
      point_t saved_center = param.lens_correction.center;
      param.scanner_type = fixed_lens;
      param.lens_correction.center = {1.8, 0.6};
      ok &= do_test_homography (param, 1024, 1024, true, epsilon, 4);
      param.lens_correction.center = saved_center;
    }

  /* Lens optimization is a variable-projection problem: only lens coordinates
     belong to the outer nonlinear fit, and every trial recomputes H*(L) from
     the correspondences.  An initial homography guess must therefore have no
     influence on the fitted lens.  Start one solve from deliberately absurd
     linear geometry and require the same lens solution as a default start.  */
  if (lens_correction && !joly)
    {
      image_data img;
      if (!img.set_dimensions (1024, 1024))
        return false;
      scr_to_img_parameters truth = param;
      truth.scanner_type = fixed_lens;
      scr_to_img truth_map;
      if (!truth_map.set_parameters (truth, img))
        return false;

      solver_parameters sparam;
      for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
          {
            point_t image = {(coord_t)(111 * x), (coord_t)(111 * y)};
            sparam.add_point (image, truth_map.to_scr (image),
                              solver_parameters::red);
          }

      scr_to_img_parameters ordinary, nonsense;
      ordinary.scanner_type = fixed_lens;
      nonsense.scanner_type = fixed_lens;
      nonsense.center = {123456, -654321};
      nonsense.coordinate1 = {-37, 91};
      nonsense.coordinate2 = {52, -113};
      nonsense.tilt_x = 0.0027;
      nonsense.tilt_y = -0.0024;
      const coord_t ordinary_objective = solver (&ordinary, img, sparam);
      const coord_t nonsense_objective = solver (&nonsense, img, sparam);
      if (!(ordinary_objective >= 0 && ordinary_objective < 1e30)
          || !(nonsense_objective >= 0 && nonsense_objective < 1e30)
          || !(ordinary.lens_correction == nonsense.lens_correction))
        {
          fprintf (stderr,
                   "Initial homography guess changed profiled lens fit\n");
          ok = false;
        }
    }
  return ok;
}

bool
do_test_discovery (scr_to_img_parameters &param, int width, int height, coord_t epsilon)
{
  image_data img;
  scr_detect_parameters dparam;
  render_parameters rparam;
  rparam.gamma = 1.0;
  rparam.screen_blur_radius = 1;
  rparam.sharpen.scanner_mtf_scale = 0;
  if (!render_screen (img, param, rparam, dparam, width, height))
    return false;
  detect_regular_screen_params dsparams;
  dsparams.min_screen_percentage=90;

  dsparams.scanner_type = param.scanner_type;
  dsparams.gamma = rparam.gamma;
  bool ok = true;
  /* TODO: Disable mesh testing for now; it is broken.  */
  for (int m = 0; m < /*param.type == Dufay ? 2 :*/ 1; m++)
    {
      solver_parameters sparam;
      dsparams.do_mesh = m;
      /* TODO: slow floodfill is broken.  */
      for (int alg = 0; alg < 2; alg++)
	{
	  dsparams.fast_floodfill = alg != 2;
	  dsparams.slow_floodfill = alg != 1;

	  /* Save some time with slow floodfill.  */
	  if (!dsparams.fast_floodfill && m)
	    continue;
	  /* Lens solving is slow with many points and this way we stress more mesh transformations.  */
	  if (m)
	    sparam.optimize_lens = sparam.optimize_tilt = false;
	  printf ("Mesh: %i, fast floodfill %i slow floodfill %i\n", dsparams.do_mesh, dsparams.fast_floodfill, dsparams.slow_floodfill);
	  auto detected
	      = detect_regular_screen (img, dparam, sparam, &dsparams, NULL, NULL);
	  if (!detected.success)
	    {
	      printf ("Screen discovery failed; saving screen to out.tif\n");
	      img.save_tiff ("out.tif");
	      return false;
	    }
	  ok &= compare_scr_to_img ("Screen discovery", param, detected.param, &sparam,
				    img, false, false, epsilon);
	  if (!ok)
	    {
	      printf ("Screen discovery out of tolerance; saving screen to out.tif\n");
	      img.save_tiff ("out.tif");
	      return false;
	    }
	}
    }
  return ok;
}

bool error_found = false;

void
report (const char *name, bool ok)
{
  static int testnum = 0;
  testnum++;
  printf ("%sok %i - %s\n", ok ? "" : "not ", testnum, name);
  if (!ok)
    error_found = true;
  fflush (stdout);
}
bool
test_discovery (coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = { (coord_t)300, (coord_t)300 };
  param.coordinate1 = { (coord_t)10, (coord_t)1.2 };
  param.coordinate2 = { (coord_t)-1.4, (coord_t)10 };
  param.tilt_x = 0.001;
  param.tilt_y = 0.0001;
  param.lens_correction.center = {0.3,0.7};
  param.lens_correction.kr[1] = -0.01;
  param.lens_correction.kr[2] = 0.02;
  param.lens_correction.kr[3] = 0.01;
  assert (param.lens_correction.normalize ());
  param.type = Finlay;
  param.scanner_type = fixed_lens;
  ok &= do_test_discovery (param, 1024, 1024, epsilon);
  param.type = Dufay;
  ok &= do_test_discovery (param, 1024, 1024, epsilon);
  return ok;
}

bool
test_screen_blur ()
{
  screen mstr;
  mstr.initialize (Paget);
  std::unique_ptr <screen> scr1 (new screen);
  std::unique_ptr <screen> scr2 (new screen);
  std::unique_ptr <screen> scr3 (new screen);

  for (int i = 0; i < 100; i++)
    {
      luminosity_t radius = i * screen::max_blur_radius / 100;
      scr1->initialize_with_blur (mstr, radius, screen::blur_fft);
      scr2->initialize_with_blur (mstr, radius, screen::blur_direct);
      luminosity_t delta;

      /* For very small blurs fft produces roundoff errors along sharp edges.  */
      if (!scr1->almost_equal_p (*scr2, &delta, i < 20 ? 0.006 : 1.0/2048))
        {
	  fprintf (stderr, "FFT Gaussian blur does not match direct version radius %f delta %f (step %i); see /tmp/scr-*.tif \n", radius, delta, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  scr2->save_tiff ("/tmp/scr-nofft.tif");
	  std::unique_ptr <screen> diff (new screen);
	  for (int y = 0; y < screen::size; y++)
	   for (int x = 0; x < screen::size; x++)
	     for (int c = 0; c < 3; c++)
		diff->mult[y][x][c] = 0.5 + (scr2->mult[y][x][c] - scr1->mult[y][x][c]);
	  diff->save_tiff ("/tmp/scr-diff.tif");
	  return false;
        }
      rgbdata rgbdelta;
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.001))
        {
	  fprintf (stderr, "FFT Gaussian blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
      scr3->initialize_with_blur (mstr, radius, screen::blur_fft2d);
      /* For very small blurs fft produces roundoff errors along sharp edges.  */
      if (!scr2->almost_equal_p (*scr3, &delta, i < 20 || i > 80 ? 0.006 : 1.0/1024))
        {
	  fprintf (stderr, "FFT Gaussian blur does not FFT2D version radius %f delta %f (step %i); see /tmp/scr-*.tif \n", radius, delta, i);
	  //scr1->save_tiff ("/tmp/scr-fft.tif");
	  scr2->save_tiff ("/tmp/scr-nofft.tif");
	  scr3->save_tiff ("/tmp/scr-fft2d.tif");
	  std::unique_ptr <screen> diff (new screen);
	  for (int y = 0; y < screen::size; y++)
	   for (int x = 0; x < screen::size; x++)
	     for (int c = 0; c < 3; c++)
		diff->mult[y][x][c] = 0.5 + (scr3->mult[y][x][c] - scr2->mult[y][x][c]);
	  diff->save_tiff ("/tmp/scr-diff.tif");
	  return false;
        }

      scr1->initialize_with_blur (mstr, radius, screen::blur_fft);
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.001))
        {
	  fprintf (stderr, "FFT mtffilter blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
    }
  return true;
}
bool
test_screen_sharpening ()
{
  std::unique_ptr <screen> scr (new screen);
  std::unique_ptr <screen> mstr (new screen);
  mstr->initialize (Paget);
  mstr->add[7][11][0] = (luminosity_t)0.125;
  mstr->add[7][11][1] = (luminosity_t)0.25;
  mstr->add[7][11][2] = (luminosity_t)0.5;

  sharpen_parameters sp;
  sp.scanner_mtf.f_stop = 8;
  sp.scanner_mtf.wavelength = 750;
  /* Pixel pitch 3.7/10 micrometers as requested.  */
  sp.scanner_mtf.pixel_pitch = 3.7  /*/ 10.0*/;
  sp.scanner_mtf.scan_dpi = 4000;
  sp.scanner_mtf_scale = 0.01;
  //sp.scanner_snr = 2000;
  
  sharpen_parameters *par[3] = {&sp, &sp, &sp};

  for (int m = 0; m < 3; m++)
    {
      if (m == 0)
	{
	  sp.mode = sharpen_parameters::wiener_deconvolution;
	  sp.richardson_lucy_iterations = 0;
	}
      else if (m == 1)
	{
	  sp.mode = sharpen_parameters::richardson_lucy_deconvolution;
	  sp.richardson_lucy_iterations = 5;
	}
      else
	{
	  sp.mode = sharpen_parameters::blur_deconvolution;
	  sp.richardson_lucy_iterations = 0;
	}

      for (int i = 0; i <= 100; i+=5)
	{
	  double defocus = 12.0*i/100.0; // 0 to 2mm in 5 steps
	  sp.scanner_mtf.defocus = defocus;
	  if (!scr->initialize_with_sharpen_parameters (*mstr, par, m != 2, true))
	    {
	      fprintf (stderr, "MTF screen filtering initialization failed\n");
	      return false;
	    }

	  for (int c = 0; c < 3; c++)
	    if (scr->add[7][11][c] != mstr->add[7][11][c])
	      {
		fprintf (stderr,
			 "MTF screen filtering did not preserve ADD channel %i\n",
			 c);
		return false;
	      }

	  /* Disable debug tiffs */
	  if (0)
	    {
	      char buf[256];
	      sprintf (buf, "/tmp/scr-sharpen-%s-defocus-%.1f.tif", 
		       m == 0 ? "wiener" : m == 1 ? "richardson-lucy" : "blur", defocus);
	      scr->save_tiff (buf);
	    }
	  rgbdata rgbdelta;
	  if (!scr->sum_almost_equal_p (*mstr, &rgbdelta, 0.001))
	    {
	      fprintf (stderr, "MTF %s defocus %f delta %f %f %f (step %i); see /tmp/scr-mtf.tif \n", m == 0 ? "wiener" : m == 1 ? "richardson-lucy" : "blur", defocus, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	      scr->save_tiff ("/tmp/scr-mtf.tif");
	      std::unique_ptr <screen> diff (new screen);
	      for (int y = 0; y < screen::size; y++)
	       for (int x = 0; x < screen::size; x++)
		 for (int c = 0; c < 3; c++)
		    diff->mult[y][x][c] = 0.5 + (scr->mult[y][x][c] - mstr->mult[y][x][c]);
	      diff->save_tiff ("/tmp/scr-diff.tif");
	      return false;
	    }
	}
    }
  /* Exercise the periodic SCREEN fast path with a synthetic cosine whose
     physical defocus OTF is negative.  A magnitude clamp here used to turn
     the expected contrast reversal into a positive response.  */
  screen signed_source, signed_result;
  constexpr int signed_bin = 24;
  constexpr double signed_frequency = signed_bin / (double)screen::size;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        {
          signed_source.mult[y][x][c]
              = (luminosity_t)(0.5
                               + 0.2
                                     * std::cos (2 * M_PI * signed_bin * x
                                                 / screen::size));
          signed_source.add[y][x][c] = 0;
        }

  sharpen_parameters signed_sp;
  signed_sp.mode = sharpen_parameters::blur_deconvolution;
  signed_sp.scanner_mtf.scan_dpi = 1887;
  signed_sp.scanner_mtf.f_stop = 8;
  signed_sp.scanner_mtf.wavelength = 750;
  signed_sp.scanner_mtf.pixel_pitch = 3.760;
  signed_sp.scanner_mtf.sensor_fill_factor = 0;
  signed_sp.scanner_mtf.defocus = 0.5;
  signed_sp.scanner_mtf_scale = 1.0 / screen::size;
  sharpen_parameters *signed_par[3] = {&signed_sp, &signed_sp, &signed_sp};
  if (!signed_result.initialize_with_sharpen_parameters (
          signed_source, signed_par, false, false))
    {
      fprintf (stderr, "Signed screen OTF initialization failed\n");
      return false;
    }

  double signed_sum = 0;
  double signed_norm = 0;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        const double cosine
            = std::cos (2 * M_PI * signed_bin * x / screen::size);
        signed_sum += ((double)signed_result.mult[y][x][0] - 0.5) * cosine;
        signed_norm += cosine * cosine;
      }
  const double signed_response = signed_sum / signed_norm / 0.2;
  const double expected_signed_response
      = signed_sp.scanner_mtf.system_otf (signed_frequency);
  if (!(expected_signed_response < -0.01 && signed_response < -0.01)
      || std::abs (signed_response - expected_signed_response) > 0.004)
    {
      fprintf (stderr,
               "Periodic screen filtering lost physical OTF sign: expected "
               "%g got %g\n",
               expected_signed_response, signed_response);
      return false;
    }

  return true;
}

/* Test finite screen-simulation storage, aperture ownership, pixel
   integration, cache identity, display normalization, coordinate ordering,
   and degenerate-screen handling.  */
bool
test_screen_simulation ()
{
  bool ok = true;

  /* A finite rendered image is part of the cache value.  Its dimensions and
     exact sharpening parameters must therefore be part of the cache key.  */
  screen source;
  source.empty ();
  simulated_screen_params first = {};
  first.screen_id = 17;
  first.width = 3;
  first.height = 2;
  first.scr = &source;
  simulated_screen_params second = first;
  second.width++;
  if (first == second)
    {
      fprintf (stderr, "Simulated-screen cache ignored image width\n");
      ok = false;
    }
  second = first;
  second.height++;
  if (first == second)
    {
      fprintf (stderr, "Simulated-screen cache ignored image height\n");
      ok = false;
    }
  second = first;
  second.sharpen.scanner_mtf_scale
      = first.sharpen.scanner_mtf_scale + (luminosity_t)0.0005;
  if (first == second)
    {
      fprintf (stderr,
               "Simulated-screen cache used approximate MTF-scale equality\n");
      ok = false;
    }
  second = first;
  second.sampling = screen_sampling::point_sample;
  if (first == second)
    {
      fprintf (stderr,
               "Simulated-screen cache ignored pixel-sampling policy\n");
      ok = false;
    }

  /* A measured MTF always contains the sensor aperture.  An analytical MTF
     contains it only when the fill-factor term is enabled.  No transfer can
     own the aperture unless it was actually applied to the periodic screen.  */
  sharpen_parameters transfer;
  transfer.scanner_mtf.sensor_fill_factor = 0;
  if (transfer.scanner_mtf.includes_sensor_aperture_p ()
      || screen_sampling_for_capture_transfer (transfer, true)
             != screen_sampling::integrate_pixel)
    {
      fprintf (stderr,
               "Aperture-exclusive analytical MTF selected point sampling\n");
      ok = false;
    }
  transfer.scanner_mtf.sensor_fill_factor = 1;
  if (!transfer.scanner_mtf.includes_sensor_aperture_p ()
      || screen_sampling_for_capture_transfer (transfer, true)
             != screen_sampling::point_sample
      || screen_sampling_for_capture_transfer (transfer, false)
             != screen_sampling::integrate_pixel)
    {
      fprintf (stderr,
               "Analytical sensor aperture has incorrect sampling ownership\n");
      ok = false;
    }
  /* Exercise the public screen-construction API as well as the policy helper.
     A valid deconvolution mode requests construction of the forward periodic
     capture screen even though ANTICIPATE_SHARPENING is false.  */
  sharpen_parameters applied_transfer;
  applied_transfer.mode = sharpen_parameters::wiener_deconvolution;
  applied_transfer.scanner_mtf_scale = (luminosity_t)0.01;
  applied_transfer.scanner_mtf.f_stop = 8;
  applied_transfer.scanner_mtf.wavelength = 750;
  applied_transfer.scanner_mtf.pixel_pitch = 3.760;
  applied_transfer.scanner_mtf.scan_dpi = 1887;
  applied_transfer.scanner_mtf.sensor_fill_factor = 1;
  screen_sampling actual_sampling = screen_sampling::integrate_pixel;
  std::shared_ptr<screen> actual_screen = render_to_scr::get_screen (
      Paget, false, false, applied_transfer, (coord_t)0, (coord_t)0,
      nullptr, nullptr, &actual_sampling);
  if (!actual_screen || actual_sampling != screen_sampling::point_sample)
    {
      fprintf (stderr,
               "Screen construction lost analytical aperture ownership\n");
      ok = false;
    }
  applied_transfer.scanner_mtf.sensor_fill_factor = 0;
  actual_screen = render_to_scr::get_screen (
      Paget, false, false, applied_transfer, (coord_t)0, (coord_t)0,
      nullptr, nullptr, &actual_sampling);
  if (!actual_screen || actual_sampling != screen_sampling::integrate_pixel)
    {
      fprintf (stderr,
               "Screen construction skipped aperture-exclusive integration\n");
      ok = false;
    }

  mtf_measurement measured;
  measured.add_value (0, 100);
  measured.add_value ((double)0.25, 80);
  measured.add_value ((double)0.5, 40);
  transfer.scanner_mtf.measurements.push_back (measured);
  transfer.scanner_mtf.measured_mtf_idx = 0;
  transfer.scanner_mtf.sensor_fill_factor = 0;
  if (!transfer.scanner_mtf.includes_sensor_aperture_p ()
      || screen_sampling_for_capture_transfer (transfer, true)
             != screen_sampling::point_sample)
    {
      fprintf (stderr,
               "Measured MTF did not retain sensor-aperture ownership\n");
      ok = false;
    }
  applied_transfer.scanner_mtf.measurements.clear ();
  applied_transfer.scanner_mtf.measurements.push_back (measured);
  applied_transfer.scanner_mtf.measured_mtf_idx = 0;
  applied_transfer.scanner_mtf.sensor_fill_factor = 0;
  actual_screen = render_to_scr::get_screen (
      Paget, false, false, applied_transfer, (coord_t)0, (coord_t)0,
      nullptr, nullptr, &actual_sampling);
  if (!actual_screen || actual_sampling != screen_sampling::point_sample)
    {
      fprintf (stderr,
               "Screen construction lost measured aperture ownership\n");
      ok = false;
    }

  /* Verify integration over the complete pixel footprint.  The screen is a
     one-dimensional sinusoid at 0.5 cycles per capture pixel.  Compare the
     five-point Gauss--Legendre result with a dense midpoint integral of the
     same periodic bilinear screen representation.  Also reproduce the old
     [-1/3,+1/3] equal-weight rule to ensure this test detects SIM-002.  */
  screen sinusoid;
  constexpr double sinusoid_phase = 0.23;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        const luminosity_t value
            = (luminosity_t)(0.5
                             + 0.4
                                   * std::cos (2.0 * M_PI * x / screen::size
                                               + sinusoid_phase));
        for (int c = 0; c < 3; c++)
          {
            sinusoid.mult[y][x][c] = value;
            sinusoid.add[y][x][c] = 0;
          }
      }
  scr_to_img_parameters sinusoid_parameters;
  sinusoid_parameters.type = Paget;
  sinusoid_parameters.coordinate1 = { 2, 0 };
  sinusoid_parameters.coordinate2 = { 0, 2 };
  scr_to_img sinusoid_map;
  if (!sinusoid_map.set_parameters (sinusoid_parameters, 16, 16))
    {
      fprintf (stderr, "Sinusoidal aperture-test mapping failed\n");
      ok = false;
    }
  else
    {
      constexpr int reference_steps = 65536;
      double reference = 0;
      for (int i = 0; i < reference_steps; i++)
        {
          const coord_t offset
              = ((coord_t)i + (coord_t)0.5) / reference_steps
                - (coord_t)0.5;
          reference += sinusoid
                           .interpolated_mult (sinusoid_map.to_scr (
                               { (coord_t)0.5 + offset, (coord_t)0.5 }))
                           .red;
        }
      reference /= reference_steps;

      const double integrated
          = antialias_screen (sinusoid, sinusoid_map, 0, 0).red;
      point_t center
          = sinusoid_map.to_scr ({ (coord_t)0.5, (coord_t)0.5 });
      point_t dx
          = (sinusoid_map.to_scr ({ (coord_t)1.5, (coord_t)0.5 })
             - center)
            * ((coord_t)1 / (coord_t)6);
      point_t dy
          = (sinusoid_map.to_scr ({ (coord_t)0.5, (coord_t)1.5 })
             - center)
            * ((coord_t)1 / (coord_t)6);
      double old_integral = 0;
      for (int yy = -2; yy <= 2; yy++)
        for (int xx = -2; xx <= 2; xx++)
          old_integral
              += sinusoid
                     .interpolated_mult (center + dx * (coord_t)xx
                                         + dy * (coord_t)yy)
                     .red;
      old_integral /= 25;

      if (fabs (integrated - reference) > 1e-5)
        {
          fprintf (stderr,
                   "Full-pixel quadrature mismatch: got %.9g, reference "
                   "%.9g\n",
                   integrated, reference);
          ok = false;
        }
      if (fabs (old_integral - reference) < 0.01)
        {
          fprintf (stderr,
                   "Aperture regression no longer distinguishes the old "
                   "partial-pixel rule\n");
          ok = false;
        }

      /* The finite renderer must dispatch the policy once outside its hot
         pixel loop.  Its first sample should match the corresponding direct
         sampling primitive exactly.  */
      simulated_screen_params sample_parameters = {};
      sample_parameters.screen_id = 29;
      sample_parameters.width = 4;
      sample_parameters.height = 4;
      sample_parameters.params = sinusoid_parameters;
      sample_parameters.scr = &sinusoid;
      sample_parameters.sampling = screen_sampling::point_sample;
      std::unique_ptr<simulated_screen> point_image
          = get_new_simulated_screen (sample_parameters, nullptr);
      sample_parameters.sampling = screen_sampling::integrate_pixel;
      std::unique_ptr<simulated_screen> integrated_image
          = get_new_simulated_screen (sample_parameters, nullptr);
      if (!point_image || !integrated_image)
        {
          fprintf (stderr, "Sampling-policy simulation failed\n");
          ok = false;
        }
      else
        {
          const rgbdata expected_point
              = noantialias_screen (sinusoid, sinusoid_map, 0, 0);
          const rgbdata expected_integrated
              = antialias_screen (sinusoid, sinusoid_map, 0, 0);
          if (!point_image->get_pixel (0, 0).almost_equal_p (
                  expected_point, (luminosity_t)1e-6)
              || !integrated_image->get_pixel (0, 0).almost_equal_p (
                  expected_integrated, (luminosity_t)1e-6))
            {
              fprintf (stderr,
                       "Finite screen ignored explicit sampling policy\n");
              ok = false;
            }
        }
    }

  /* Invalid dimensions or a missing periodic source must fail before an
     incomplete image can reach the cache.  */
  simulated_screen_params invalid = {};
  if (get_new_simulated_screen (invalid, nullptr))
    {
      fprintf (stderr, "Invalid simulated-screen request succeeded\n");
      ok = false;
    }

  /* Populate a non-square finite image with an affine RGB ramp.  Clamping at
     all four borders and bilinear interpolation have analytical results.  */
  simulated_screen finite (3, 2);
  for (int y = 0; y < 2; y++)
    for (int x = 0; x < 3; x++)
      finite.put_pixel (
          x, y,
          { (luminosity_t)(1 + x + 10 * y),
            (luminosity_t)(2 + 2 * x + 20 * y),
            (luminosity_t)(3 + 3 * x + 30 * y) });

  const struct
  {
    point_t point;
    rgbdata expected;
    const char *name;
  } interpolation_tests[] = {
    { { -10, (coord_t)0.5 }, finite.get_pixel (0, 0), "left border" },
    { { 10, (coord_t)0.5 }, finite.get_pixel (2, 0), "right border" },
    { { (coord_t)0.5, -10 }, finite.get_pixel (0, 0), "top border" },
    { { (coord_t)0.5, 10 }, finite.get_pixel (0, 1), "bottom border" },
    { { 1, 1 },
      (finite.get_pixel (0, 0) + finite.get_pixel (1, 0)
       + finite.get_pixel (0, 1) + finite.get_pixel (1, 1))
          * (luminosity_t)0.25,
      "boundary bilinear interpolation" }
  };
  for (const auto &test : interpolation_tests)
    {
      const rgbdata actual
          = finite.get_interpolated_pixel (test.point.x, test.point.y);
      if (!actual.almost_equal_p (test.expected, (luminosity_t)1e-6))
        {
          fprintf (stderr,
                   "Simulated-screen %s mismatch: got %g %g %g, expected "
                   "%g %g %g\n",
                   test.name, actual.red, actual.green, actual.blue,
                   test.expected.red, test.expected.green,
                   test.expected.blue);
          ok = false;
        }
    }

  /* Normalize each display channel independently.  The historical code used
     the red maximum for green and blue as well.  */
  screen display;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      for (int c = 0; c < 3; c++)
        {
          display.mult[y][x][c] = 0;
          display.add[y][x][c] = 0;
        }
  display.mult[0][0][0] = 1;
  display.mult[0][0][1] = (luminosity_t)0.5;
  display.mult[0][0][2] = (luminosity_t)0.25;
  std::unique_ptr<simple_image> normalized = display.get_image (true, 1);
  if (!normalized)
    {
      fprintf (stderr, "Normalized screen preview allocation failed\n");
      ok = false;
    }
  else
    {
      simple_image::rgb pixel = normalized->get_pixel (0, 0);
      if (pixel.red != 255 || pixel.green != 255 || pixel.blue != 255)
        {
          fprintf (stderr,
                   "Per-channel screen normalization failed: %i %i %i\n",
                   pixel.red, pixel.green, pixel.blue);
          ok = false;
        }
    }

  display.mult[0][0][0] = 0;
  display.mult[0][0][1] = 0;
  display.mult[0][0][2] = 0;
  normalized = display.get_image (true, 1);
  if (!normalized)
    {
      fprintf (stderr, "Zero-screen preview allocation failed\n");
      ok = false;
    }
  else
    {
      simple_image::rgb pixel = normalized->get_pixel (0, 0);
      if (pixel.red || pixel.green || pixel.blue)
        {
          fprintf (stderr, "Zero screen did not normalize to black\n");
          ok = false;
        }
    }

  rgbdata proportions = display.patch_proportions ();
  if (proportions != rgbdata (0, 0, 0))
    {
      fprintf (stderr, "Zero screen produced nonzero patch proportions\n");
      ok = false;
    }
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        display.mult[y][x][0] = 1;
        display.mult[y][x][1] = 2;
        display.mult[y][x][2] = 3;
      }
  proportions = display.patch_proportions ();
  const rgbdata expected_proportions
      = { (luminosity_t)(1.0 / 6.0), (luminosity_t)(2.0 / 6.0),
          (luminosity_t)(3.0 / 6.0) };
  if (!proportions.almost_equal_p (expected_proportions,
                                   (luminosity_t)1e-6))
    {
      fprintf (stderr,
               "Patch proportions mismatch: got %g %g %g, expected %g %g "
               "%g\n",
               proportions.red, proportions.green, proportions.blue,
               expected_proportions.red, expected_proportions.green,
               expected_proportions.blue);
      ok = false;
    }

  /* The simulated-screen branch of colour-loss estimation must read finite
     pixels as (X, Y).  Use a non-square image and an asymmetric vertical
     selection so exchanging the coordinates changes the result.  */
  simulated_screen collected (3, 2);
  for (int y = 0; y < 2; y++)
    for (int x = 0; x < 3; x++)
      collected.put_pixel (x, y, { (luminosity_t)0.2,
                                   (luminosity_t)0.2,
                                   (luminosity_t)0.2 });
  collected.put_pixel (1, 0, { 1, (luminosity_t)0.5,
                                (luminosity_t)0.25 });
  collected.put_pixel (1, 1, { (luminosity_t)0.5, 1,
                                (luminosity_t)0.75 });
  collected.put_pixel (0, 1, { (luminosity_t)0.9,
                                (luminosity_t)0.1,
                                (luminosity_t)0.4 });

  auto expected_collected_color = [&collected] (int channel)
    {
      double_rgbdata sum;
      double weight = 0;
      for (int y = 0; y < 2; y++)
        {
          const rgbdata value = collected.get_pixel (1, y);
          const double sample_weight = value[channel];
          weight += sample_weight;
          sum.red += (double)value.red * sample_weight;
          sum.green += (double)value.green * sample_weight;
          sum.blue += (double)value.blue * sample_weight;
        }
      return rgbdata ((luminosity_t)(sum.red / weight),
                      (luminosity_t)(sum.green / weight),
                      (luminosity_t)(sum.blue / weight));
    };
  const rgbdata expected_red = expected_collected_color (0);
  const rgbdata expected_green = expected_collected_color (1);
  const rgbdata expected_blue = expected_collected_color (2);
  rgbdata actual_red, actual_green, actual_blue;
  screen dummy_screen;
  dummy_screen.empty ();
  scr_to_img dummy_map;
  sharpen_parameters no_sharpening;
  if (!determine_color_loss (&actual_red, &actual_green, &actual_blue,
                             dummy_screen, dummy_screen, &collected,
                             screen_sampling::point_sample, 0, no_sharpening,
                             dummy_map,
                             {1, 0, 1, 2}))
    {
      fprintf (stderr, "Finite colour-loss simulation failed\n");
      ok = false;
    }
  else if (!actual_red.almost_equal_p (expected_red, (luminosity_t)1e-6)
           || !actual_green.almost_equal_p (expected_green,
                                            (luminosity_t)1e-6)
           || !actual_blue.almost_equal_p (expected_blue,
                                           (luminosity_t)1e-6))
    {
      fprintf (stderr,
               "Finite colour-loss simulation exchanged X and Y\n");
      ok = false;
    }

  if (fir_blur::gen_convolve_matrix (1, nullptr) != 0)
    {
      fprintf (stderr, "FIR kernel accepted a null output pointer\n");
      ok = false;
    }

  return ok;
}


/* Fill FILTER tile zero with VALUE (X, Y), process it, and leave the result
   available through FILTER.  T is the deconvolution sample type and FUNCTION
   is the callable type of VALUE.  */
template <typename T, typename Function>
static void
fill_deconvolution_tile (deconvolution<T> &filter, Function value)
{
  filter.init (0);
  const int border = filter.get_border_size ();
  const int tile_size = filter.get_tile_size_with_borders ();
  for (int y = 0; y < tile_size; y++)
    for (int x = 0; x < tile_size; x++)
      filter.put_pixel (0, x, y, (T)value (x - border, y - border));
  filter.process_tile (0, nullptr);
}

/* Return root-mean-square error between processed FILTER and EXPECTED (X, Y),
   excluding a small interior margin.  T is FILTER's sample type and FUNCTION
   is the callable type of EXPECTED.  */
template <typename T, typename Function>
static double
deconvolution_tile_rmse (deconvolution<T> &filter, Function expected)
{
  const int border = filter.get_border_size ();
  const int basic_size = filter.get_basic_tile_size ();
  const int margin = std::min (8, basic_size / 8);
  long double squared_error = 0;
  size_t n = 0;
  for (int y = margin; y < basic_size - margin; y++)
    for (int x = margin; x < basic_size - margin; x++)
      {
        const double got = filter.get_pixel (0, x + border, y + border);
        if (!my_isfinite (got))
          return test_runtime_infinity ();
        const double error = got - expected (x, y);
        squared_error += error * error;
        n++;
      }
  return n ? std::sqrt ((double)(squared_error / n)) : test_runtime_infinity ();
}

/* Return the fitted horizontal cosine amplitude in FILTER at FREQUENCY after
   subtracting MEAN.  T is FILTER's sample type.  */
template <typename T>
static double
deconvolution_cosine_amplitude (const deconvolution<T> &filter,
                                double frequency, double mean)
{
  const int border = filter.get_border_size ();
  const int basic_size = filter.get_basic_tile_size ();
  const int margin = std::min (8, basic_size / 8);
  long double sum = 0;
  long double norm = 0;
  for (int y = margin; y < basic_size - margin; y++)
    for (int x = margin; x < basic_size - margin; x++)
      {
        const double sample
            = filter.get_pixel (0, x + border, y + border) - mean;
        const double cosine = std::cos (2 * M_PI * frequency * x);
        sum += sample * cosine;
        norm += cosine * cosine;
      }
  return norm ? (double)(sum / norm) : 0;
}

/* Return the fitted two-dimensional cosine amplitude in FILTER at
   (FREQUENCY_X, FREQUENCY_Y) after subtracting MEAN.  T is FILTER's sample
   type.  */
template <typename T>
static double
deconvolution_cosine_amplitude_2d (const deconvolution<T> &filter,
                                   double frequency_x, double frequency_y,
                                   double mean)
{
  const int border = filter.get_border_size ();
  const int basic_size = filter.get_basic_tile_size ();
  const int margin = std::min (8, basic_size / 8);
  long double sum = 0;
  long double norm = 0;
  for (int y = margin; y < basic_size - margin; y++)
    for (int x = margin; x < basic_size - margin; x++)
      {
        const double sample
            = filter.get_pixel (0, x + border, y + border) - mean;
        const double cosine
            = std::cos (2 * M_PI * (frequency_x * x + frequency_y * y));
        sum += sample * cosine;
        norm += cosine * cosine;
      }
  return norm ? (double)(sum / norm) : 0;
}

/* Build measured-MTF parameters from POINTS containing frequency and contrast
   percentage pairs.  */
static mtf_parameters
make_measured_mtf (const std::vector<std::pair<double, double>> &points)
{
  mtf_parameters parameters;
  mtf_measurement measurement;
  for (const auto &point : points)
    measurement.add_value (point.first, point.second);
  parameters.measurements.push_back (measurement);
  parameters.measured_mtf_idx = 0;
  return parameters;
}

/* Verify the physical diffraction model, its unit conversions and the
   optional broad-scatter residual on the Hurley capture geometry.  */
static bool
test_mtf_physical_model ()
{
  bool ok = true;
  mtf_parameters hurley;
  hurley.scan_dpi = 1887;
  hurley.f_stop = 8;
  hurley.wavelength = 750;
  hurley.pixel_pitch = 3.760;
  if (hurley.get_channel_wavelength (3) != 750)
    {
      fprintf (stderr, "Global narrow-band wavelength was not used for IR\n");
      ok = false;
    }

  const double expected_magnification = 0.27933543307086614;
  const double expected_effective_f_stop = 10.234683464566929;
  const double expected_cutoff = 0.48983765357177734;
  if (std::abs (hurley.magnification () - expected_magnification) > 1e-14
      || std::abs (hurley.effective_f_stop () - expected_effective_f_stop)
             > 1e-13
      || std::abs (hurley.nu (expected_cutoff) - 1) > 2e-14)
    {
      fprintf (stderr,
               "Hurley MTF unit conversion failed: m=%0.17g N=%0.17g "
               "nu(fc)=%0.17g\n",
               hurley.magnification (), hurley.effective_f_stop (),
               hurley.nu (expected_cutoff));
      ok = false;
    }

  const double half_cutoff = expected_cutoff * 0.5;
  const double expected_half_cutoff_mtf = 0.39100221895577064;
  if (std::abs (hurley.lens_diffraction_mtf (half_cutoff)
                - expected_half_cutoff_mtf)
          > 2e-13
      /* EXPECTED_CUTOFF is a rounded decimal reference.  Under -Ofast its
         independently evaluated ratio can land a few ulps below one, so test
         the physical zero rather than bitwise equality.  */
      || hurley.lens_diffraction_mtf (expected_cutoff) > 1e-20)
    {
      fprintf (stderr,
               "Circular-pupil diffraction MTF failed: half=%0.17g "
               "cutoff=%0.17g\n",
               hurley.lens_diffraction_mtf (half_cutoff),
               hurley.lens_diffraction_mtf (expected_cutoff));
      ok = false;
    }

  hurley.defocus = 0.17939247226072069;
  const double defocus_q10
      = hurley.lens_defocus_mtf (expected_cutoff * 0.1);
  const double defocus_q50
      = hurley.lens_defocus_mtf (expected_cutoff * 0.5);
  if (std::abs (defocus_q10 - 0.94938728640116945) > 3e-12
      || std::abs (defocus_q50 - 0.66524440281563219) > 3e-12)
    {
      fprintf (stderr,
               "Exact circular-pupil defocus failed: q=.1 %0.17g, "
               "q=.5 %0.17g\n",
               defocus_q10, defocus_q50);
      ok = false;
    }

  /* A sufficiently defocused known pupil has real OTF phase reversals.  MTF
     magnitudes used for fitting must stay positive, while the physical OTF
     supplied to forward blur and deconvolution must retain the sign.  */
  mtf_parameters reversed = hurley;
  reversed.defocus = 0.5;
  reversed.sensor_fill_factor = 0;
  reversed.sigma = 0;
  reversed.halo_fraction = 0;
  const double reversed_frequency = expected_cutoff * 0.4;
  const double reversed_defocus
      = reversed.lens_defocus_otf (reversed_frequency);
  if (std::abs (reversed_defocus + 0.09507351131732263) > 4e-12
      || reversed.lens_defocus_mtf (reversed_frequency) <= 0
      || std::abs (reversed.lens_defocus_mtf (reversed_frequency)
                   + reversed_defocus)
             > 4e-12
      || reversed.system_otf (reversed_frequency) >= 0
      || reversed.system_mtf (reversed_frequency) <= 0)
    {
      fprintf (stderr,
               "Signed physical OTF reversal failed: defocus=%0.17g "
               "system OTF=%0.17g MTF=%0.17g\n",
               reversed_defocus, reversed.system_otf (reversed_frequency),
               reversed.system_mtf (reversed_frequency));
      ok = false;
    }

  mtf reversed_mtf (reversed);
  if (!reversed_mtf.precompute (nullptr, false)
      || reversed_mtf.get_transfer (reversed_frequency) >= 0
      || std::abs (reversed_mtf.get_transfer (reversed_frequency)
                   - reversed.system_otf (reversed_frequency))
             > 0.002
      || reversed_mtf.get_mtf (reversed_frequency) <= 0)
    {
      fprintf (stderr,
               "Precomputed physical OTF lost the defocus sign: table=%g "
               "model=%g\n",
               reversed_mtf.get_transfer (reversed_frequency),
               reversed.system_otf (reversed_frequency));
      ok = false;
    }

  /* Component curves supplied to the GUI must expose both the nonnegative
     model MTF and the signed analytical OTF.  This lets the chart display
     measured magnitude and fitted phase reversal without changing the
     sharpening model or inventing phase for measured data.  */
  const mtf_parameters::computed_mtf reversed_curves
      = reversed.compute_curves (201);
  bool saw_negative_curve = false;
  if (reversed_curves.system_otf.size ()
          != reversed_curves.system_mtf.size ())
    {
      fprintf (stderr, "Signed OTF chart curve has the wrong size\n");
      ok = false;
    }
  else
    for (size_t i = 0; i < reversed_curves.system_otf.size (); i++)
      {
        if (reversed_curves.system_otf[i] < -0.001)
          saw_negative_curve = true;
        if (reversed_curves.system_mtf[i] < 0
            || std::abs (reversed_curves.system_mtf[i]
                         - std::abs (reversed_curves.system_otf[i]))
                   > 2e-13)
          {
            fprintf (stderr,
                     "GUI MTF/OTF curve magnitude mismatch at %zu: %g %g\n",
                     i, reversed_curves.system_otf[i],
                     reversed_curves.system_mtf[i]);
            ok = false;
            break;
          }
      }
  if (!saw_negative_curve)
    {
      fprintf (stderr, "Signed GUI OTF curve lost all negative lobes\n");
      ok = false;
    }

  /* A measured slanted-edge curve contains magnitude only.  Even if its
     samples match the magnitude of a reversed physical OTF, PRECOMPUTE must
     not invent a negative phase.  */
  mtf_parameters measured_reversal = make_measured_mtf (
      {{0, 100}, {reversed_frequency,
                  reversed.system_mtf (reversed_frequency) * 100},
       {0.5, 0}});
  mtf measured_reversal_mtf (measured_reversal);
  if (!measured_reversal_mtf.precompute (nullptr, false)
      || measured_reversal_mtf.get_mtf (reversed_frequency) < 0)
    {
      fprintf (stderr, "Measured MTF unexpectedly acquired a phase sign\n");
      ok = false;
    }

  hurley.halo_fraction = 0.16;
  hurley.halo_sigma = 5.2;
  if (hurley.halo_mtf (0) != 1
      || !(hurley.halo_mtf (0.02) < 1 && hurley.halo_mtf (0.02) > 0)
      || hurley.halo_mtf (0.5) > 1e-20)
    {
      fprintf (stderr,
               "Broad-scatter halo component failed: DC=%0.17g, "
               "f=.02 %0.17g, f=.5 %0.17g\n",
               hurley.halo_mtf (0), hurley.halo_mtf (0.02),
               hurley.halo_mtf (0.5));
      ok = false;
    }

  /* The broad halo is a separate positive PSF component.  It must be mixed
     with the signed compact core before MTF magnitude is taken.  Otherwise a
     defocus phase reversal is incorrectly turned positive before the halo is
     added, which moves or removes physical OTF zero crossings.  */
  mtf_parameters reversed_with_halo = reversed;
  reversed_with_halo.halo_fraction = 0.2;
  reversed_with_halo.halo_sigma = 5.0;
  const double core_otf
      = reversed_with_halo.lens_diffraction_otf (reversed_frequency)
        * reversed_with_halo.lens_defocus_otf (reversed_frequency)
        * std::exp (-2 * M_PI * M_PI * reversed_with_halo.sigma
                    * reversed_with_halo.sigma * reversed_frequency
                    * reversed_frequency);
  const double halo_component
      = reversed_with_halo.halo_mtf (reversed_frequency);
  const double mixed_otf
      = 0.8 * core_otf + 0.2 * halo_component;
  const double magnitude_before_mix
      = 0.8 * std::abs (core_otf) + 0.2 * halo_component;
  if (std::abs (reversed_with_halo.lens_otf (reversed_frequency) - mixed_otf)
          > 2e-13
      || std::abs (reversed_with_halo.lens_mtf (reversed_frequency)
                   - std::abs (mixed_otf))
             > 2e-13
      || std::abs (mixed_otf - magnitude_before_mix) < 1e-3)
    {
      fprintf (stderr,
               "Halo was not mixed with signed core before magnitude: "
               "core=%0.17g halo=%0.17g otf=%0.17g mtf=%0.17g\n",
               core_otf, halo_component,
               reversed_with_halo.lens_otf (reversed_frequency),
               reversed_with_halo.lens_mtf (reversed_frequency));
      ok = false;
    }

  /* A slanted-edge curve supplies only |OTF|.  Verify that fitting defocus to
     a synthetic curve which crosses a physical OTF zero recovers the signed
     model parameter instead of trying to fit the negative lobe directly to
     positive measured contrast.  */
  mtf_parameters reversal_fit_source = reversed_with_halo;
  reversal_fit_source.halo_fraction = 0.12;
  reversal_fit_source.halo_sigma = 5.0;
  mtf_measurement reversal_measurement;
  reversal_measurement.name = "synthetic phase-reversing physical MTF";
  reversal_measurement.wavelength = reversal_fit_source.wavelength;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      reversal_measurement.add_value (
          frequency, reversal_fit_source.system_mtf (frequency) * 100);
    }
  mtf_parameters reversal_fit_input = reversal_fit_source;
  reversal_fit_input.defocus = 0.35;
  reversal_fit_input.measurements = {reversal_measurement};
  mtf_estimation_options reversal_fit_options;
  reversal_fit_options.model = mtf_model::physical_diffraction;
  reversal_fit_options.optimize_defocus = true;
  mtf_parameters reversal_fit_result;
  const char *reversal_fit_error = nullptr;
  const double reversal_fit_objective
      = reversal_fit_result.estimate_parameters (
          reversal_fit_input, reversal_fit_options, nullptr, nullptr,
          &reversal_fit_error,
          mtf_parameters::estimate_use_nmsimplex
              | mtf_parameters::estimate_use_multifit);
  if (reversal_fit_error || reversal_fit_objective < 0
      || reversal_fit_objective > 1e-6
      || std::abs (reversal_fit_result.defocus
                   - reversal_fit_source.defocus)
             > 1e-4)
    {
      fprintf (stderr,
               "Magnitude-only fit across OTF reversal failed: defocus "
               "%0.17g expected %0.17g objective %g%s%s\n",
               reversal_fit_result.defocus, reversal_fit_source.defocus,
               reversal_fit_objective, reversal_fit_error ? ": " : "",
               reversal_fit_error ? reversal_fit_error : "");
      ok = false;
    }

  /* Per-frequency uncertainty should reduce the leverage of visibly noisy
     high-frequency samples without changing the total weight of a curve.
     Generate a physical MTF, bias its upper-frequency tail and mark that tail
     as uncertain.  The uncertainty-weighted fit must recover defocus more
     accurately than the same samples interpreted with historical uniform
     weights.  */
  mtf_parameters weighted_source = hurley;
  weighted_source.sigma = 0.45;
  weighted_source.defocus = 0.16;
  weighted_source.halo_fraction = 0.08;
  weighted_source.halo_sigma = 5.0;
  mtf_measurement weighted_measurement;
  weighted_measurement.name = "synthetic uncertainty-weighted MTF";
  weighted_measurement.wavelength = weighted_source.wavelength;
  mtf_measurement uniform_measurement = weighted_measurement;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      double contrast = weighted_source.system_mtf (frequency) * 100;
      const bool noisy_tail = frequency >= 0.28;
      if (noisy_tail)
        contrast += 4.0;
      weighted_measurement.add_value (frequency, contrast,
                                      noisy_tail ? 8.0 : 0.25);
      uniform_measurement.add_value (frequency, contrast);
    }
  mtf_estimation_options weighted_options;
  weighted_options.model = mtf_model::physical_diffraction;
  weighted_options.optimize_defocus = true;
  auto fit_defocus
      = [&] (const mtf_measurement &measurement, double *defocus)
        {
          mtf_parameters input = weighted_source;
          input.defocus = 0.08;
          input.measurements = {measurement};
          mtf_parameters result;
          const char *error = nullptr;
          const double objective = result.estimate_parameters (
              input, weighted_options, nullptr, nullptr, &error,
              mtf_parameters::estimate_use_nmsimplex
                  | mtf_parameters::estimate_use_multifit);
          if (error || objective < 0)
            return false;
          *defocus = result.defocus;
          return true;
        };
  double weighted_defocus = 0;
  double uniform_defocus = 0;
  if (!fit_defocus (weighted_measurement, &weighted_defocus)
      || !fit_defocus (uniform_measurement, &uniform_defocus)
      || std::abs (weighted_defocus - weighted_source.defocus)
             >= std::abs (uniform_defocus - weighted_source.defocus)
      || std::abs (weighted_defocus - weighted_source.defocus) > 0.01)
    {
      fprintf (stderr,
               "MTF uncertainty weighting failed: weighted defocus %.12g, "
               "uniform %.12g, expected %.12g\n",
               weighted_defocus, uniform_defocus, weighted_source.defocus);
      ok = false;
    }

  /* Repeated edges from one capture share physical defocus, so their absolute
     uncertainty is meaningful relative to one another.  Give a clean curve
     0.25-percentage-point uncertainty and a conflicting curve 5 percentage
     points: their raw residual weights are 4 and 0.2, a 20:1 ratio.  With the
     former per-curve normalization these constants both became unit weights and
     the fit was the same as the explicitly uniform two-curve fit.  Capture-wide
     normalization must instead keep the solution close to the precise curve.

     A mixed uncertainty-aware/legacy capture intentionally falls back to the
     per-curve rule rather than inventing uncertainty for the legacy curve.  */
  mtf_parameters conflicting_source = weighted_source;
  conflicting_source.defocus = 0.30;
  mtf_measurement precise_capture_measurement;
  precise_capture_measurement.name = "synthetic precise same-capture MTF";
  precise_capture_measurement.wavelength = weighted_source.wavelength;
  mtf_measurement uncertain_capture_measurement;
  uncertain_capture_measurement.name = "synthetic uncertain same-capture MTF";
  uncertain_capture_measurement.wavelength = weighted_source.wavelength;
  uncertain_capture_measurement.same_capture = true;
  mtf_measurement uniform_precise_capture_measurement;
  uniform_precise_capture_measurement.name
      = "synthetic uniform precise same-capture MTF";
  uniform_precise_capture_measurement.wavelength = weighted_source.wavelength;
  mtf_measurement uniform_conflicting_capture_measurement;
  uniform_conflicting_capture_measurement.name
      = "synthetic uniform conflicting same-capture MTF";
  uniform_conflicting_capture_measurement.wavelength
      = weighted_source.wavelength;
  uniform_conflicting_capture_measurement.same_capture = true;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      const double precise_contrast
          = weighted_source.system_mtf (frequency) * 100;
      const double conflicting_contrast
          = conflicting_source.system_mtf (frequency) * 100;
      precise_capture_measurement.add_value (frequency, precise_contrast, 0.25);
      uncertain_capture_measurement.add_value (frequency, conflicting_contrast,
                                                5.0);
      uniform_precise_capture_measurement.add_value (frequency,
                                                     precise_contrast);
      uniform_conflicting_capture_measurement.add_value (frequency,
                                                         conflicting_contrast);
    }
  auto fit_shared_defocus
      = [&] (const std::vector<mtf_measurement> &measurements,
             const std::vector<bool> &included, double *defocus)
        {
          mtf_parameters input = weighted_source;
          input.defocus = 0.08;
          input.measurements = measurements;
          mtf_estimation_options options = weighted_options;
          options.include_measurements = included;
          mtf_parameters result;
          const char *error = nullptr;
          const double objective = result.estimate_parameters (
              input, options, nullptr, nullptr, &error,
              mtf_parameters::estimate_use_nmsimplex
                  | mtf_parameters::estimate_use_multifit);
          if (error || objective < 0)
            return false;
          *defocus = result.defocus;
          return true;
        };
  double capture_weighted_defocus = 0;
  double capture_uniform_defocus = 0;
  double capture_mixed_defocus = 0;
  if (!fit_shared_defocus (
          {precise_capture_measurement, uncertain_capture_measurement}, {},
          &capture_weighted_defocus)
      || !fit_shared_defocus ({uniform_precise_capture_measurement,
                               uniform_conflicting_capture_measurement},
                              {}, &capture_uniform_defocus)
      || !fit_shared_defocus ({precise_capture_measurement,
                               uniform_conflicting_capture_measurement},
                              {}, &capture_mixed_defocus)
      || std::abs (capture_weighted_defocus - weighted_source.defocus) > 0.01
      || std::abs (capture_weighted_defocus - weighted_source.defocus)
             >= 0.25
                    * std::abs (capture_uniform_defocus
                                - weighted_source.defocus)
      || std::abs (capture_mixed_defocus - capture_uniform_defocus) > 1e-5)
    {
      fprintf (stderr,
               "Same-capture uncertainty weighting failed: weighted %.12g, "
               "uniform %.12g, mixed legacy %.12g, expected %.12g\n",
               capture_weighted_defocus, capture_uniform_defocus,
               capture_mixed_defocus, weighted_source.defocus);
      ok = false;
    }
  /* An excluded legacy curve is not part of the objective and therefore must
     not disable joint normalization of the two included uncertainty-aware
     curves in its capture group.  */
  mtf_measurement excluded_legacy = uniform_conflicting_capture_measurement;
  excluded_legacy.name = "synthetic excluded legacy same-capture MTF";
  double capture_with_excluded_legacy_defocus = 0;
  if (!fit_shared_defocus ({precise_capture_measurement,
                            uncertain_capture_measurement, excluded_legacy},
                           {true, true, false},
                           &capture_with_excluded_legacy_defocus)
      || std::abs (capture_with_excluded_legacy_defocus
                   - capture_weighted_defocus)
             > 1e-5)
    {
      fprintf (stderr,
               "Excluded legacy curve changed same-capture weighting: "
               "%.12g versus %.12g\n",
               capture_with_excluded_legacy_defocus,
               capture_weighted_defocus);
      ok = false;
    }
  /* The metadata-free fallback remains a separate model and must not silently
     enable diffraction.  */
  mtf_parameters fallback;
  fallback.sigma = 0.8;
  fallback.blur_diameter = 2.0;
  fallback.halo_fraction = 0.3;
  fallback.halo_sigma = 5.0;
  const double fallback_frequency = 0.1;
  const double gaussian
      = std::exp (-2 * M_PI * M_PI * fallback.sigma * fallback.sigma
                  * fallback_frequency * fallback_frequency);
  const double argument
      = M_PI * fallback_frequency * fallback.blur_diameter;
  /* Keep the reference calculation independent of the platform Bessel API.
     Production uses get_j1(), which selects the C++ special function, MSVCRT _j1 or
     libc j1 according to the available C/C++ runtime.  At this small argument
     the power series for J1 converges rapidly and gives a portable independent
     check of that compatibility path.  */
  const double argument_squared_quarter = argument * argument * 0.25;
  double j1_term = argument * 0.5;
  double j1_reference = j1_term;
  for (int k = 1; k < 12; k++)
    {
      j1_term *= -argument_squared_quarter / (k * (k + 1.0));
      j1_reference += j1_term;
    }
  const double disk = 2 * j1_reference / argument;
  if (fallback.simulate_diffraction_p ()
      || std::abs (fallback.lens_mtf (fallback_frequency)
                   - gaussian * std::abs (disk))
             > 2e-13)
    {
      fprintf (stderr, "Metadata-free fallback MTF changed unexpectedly\n");
      ok = false;
    }

  /* Supplied global wavelength metadata must remain fixed for both an
     unlabelled monochrome edge and an explicitly infrared edge.  Earlier the
     fitter treated 750 nm as unknown and moved it to its 1000 nm boundary.  */
  mtf_parameters source = hurley;
  source.sigma = 0.72;
  source.defocus = 0.12;
  source.halo_fraction = 0.15;
  source.halo_sigma = 5.0;
  mtf_measurement grayscale;
  grayscale.name = "synthetic Hurley grayscale";
  mtf_measurement infrared;
  infrared.name = "synthetic Hurley infrared";
  infrared.channel = 3;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      const double contrast = source.system_mtf (frequency) * 100;
      grayscale.add_value (frequency, contrast);
      infrared.add_value (frequency, contrast);
    }
  for (const mtf_measurement *measurement : {&grayscale, &infrared})
    {
      mtf_parameters input = source;
      input.measurements = {*measurement};
      mtf_parameters estimated;
      const char *error = nullptr;
      const double objective
          = estimated.estimate_parameters (input, nullptr, nullptr, &error, 0);
      if (error || objective < 0 || objective > 1e-8
          || estimated.wavelength != 750
          || estimated.wavelengths != input.wavelengths)
        {
          fprintf (stderr,
                   "Known 750 nm wavelength was not preserved: wavelength "
                   "%0.17g IR override %0.17g objective %g%s%s\n",
                   estimated.wavelength, estimated.wavelengths[3], objective,
                   error ? ": " : "", error ? error : "");
          ok = false;
        }
    }

  /* Preserve the old API's storage and sharing convention for unknown
     channel-labelled wavelengths.  Two red curves use one common fitted
     coordinate, and the result is written to the red channel override rather
     than rewriting the measurements themselves.  */
  mtf_parameters legacy_source = source;
  legacy_source.wavelength = 650;
  legacy_source.wavelengths = {0, 0, 0, 0};
  mtf_measurement legacy_red_first;
  legacy_red_first.name = "legacy shared red wavelength 1";
  legacy_red_first.channel = 0;
  mtf_measurement legacy_red_second;
  legacy_red_second.name = "legacy shared red wavelength 2";
  legacy_red_second.channel = 0;
  legacy_red_second.same_capture = true;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      const double contrast = legacy_source.system_mtf (frequency) * 100;
      legacy_red_first.add_value (frequency, contrast);
      legacy_red_second.add_value (frequency, contrast);
    }
  mtf_parameters legacy_input = legacy_source;
  legacy_input.wavelength = 0;
  legacy_input.measurements = {legacy_red_first, legacy_red_second};
  mtf_parameters legacy_result;
  const char *legacy_error = nullptr;
  const double legacy_objective = legacy_result.estimate_parameters (
      legacy_input, nullptr, nullptr, &legacy_error,
      mtf_parameters::estimate_use_nmsimplex
          | mtf_parameters::estimate_use_multifit);
  if (legacy_error || legacy_objective < 0 || legacy_objective > 1e-7
      || std::abs (legacy_result.wavelengths[0] - 650) > 1e-3
      || std::abs (legacy_result.wavelength - 650) > 1e-3
      || legacy_result.measurements[0].wavelength != 0
      || legacy_result.measurements[1].wavelength != 0)
    {
      fprintf (stderr,
               "Legacy shared channel wavelength changed: global %g red %g "
               "stored %g/%g objective %g%s%s\n",
               legacy_result.wavelength, legacy_result.wavelengths[0],
               legacy_result.measurements[0].wavelength,
               legacy_result.measurements[1].wavelength, legacy_objective,
               legacy_error ? ": " : "", legacy_error ? legacy_error : "");
      ok = false;
    }

  /* Explicit fit options separate values from optimization intent.  In
     particular, zero sigma and zero defocus are valid fixed values even when
     the selected measurement was generated with nonzero residual blur.  */
  mtf_parameters fixed_zero_input = source;
  fixed_zero_input.sigma = 0;
  fixed_zero_input.defocus = 0;
  fixed_zero_input.measurements = {grayscale};
  mtf_estimation_options fixed_zero_options;
  fixed_zero_options.model = mtf_model::physical_diffraction;
  fixed_zero_options.optimize_sigma = false;
  fixed_zero_options.optimize_defocus = false;
  mtf_parameters fixed_zero_result;
  const char *fixed_zero_error = nullptr;
  const double fixed_zero_objective = fixed_zero_result.estimate_parameters (
      fixed_zero_input, fixed_zero_options, nullptr, nullptr,
      &fixed_zero_error, 0);
  if (fixed_zero_error || fixed_zero_objective <= 0.1
      || fixed_zero_result.sigma != 0 || fixed_zero_result.defocus != 0)
    {
      fprintf (stderr,
               "Explicit fixed-zero fit failed: sigma %g defocus %g "
               "objective %g%s%s\n",
               fixed_zero_result.sigma, fixed_zero_result.defocus,
               fixed_zero_objective, fixed_zero_error ? ": " : "",
               fixed_zero_error ? fixed_zero_error : "");
      ok = false;
    }

  mtf_estimation_options free_core_options = fixed_zero_options;
  free_core_options.optimize_sigma = true;
  free_core_options.optimize_defocus = true;
  mtf_parameters free_core_result;
  const char *free_core_error = nullptr;
  const double free_core_objective = free_core_result.estimate_parameters (
      fixed_zero_input, free_core_options, nullptr, nullptr,
      &free_core_error,
      mtf_parameters::estimate_use_nmsimplex
          | mtf_parameters::estimate_use_multifit);
  if (free_core_error || free_core_objective < 0
      || free_core_objective >= fixed_zero_objective * 0.01)
    {
      fprintf (stderr,
               "Explicit free-core fit failed: objective %g versus fixed "
               "%g%s%s\n",
               free_core_objective, fixed_zero_objective,
               free_core_error ? ": " : "",
               free_core_error ? free_core_error : "");
      ok = false;
    }

  /* The empirical model remains an explicit fallback, and its zero-valued
     starting estimates are fitted only when their checkboxes request it.  */
  mtf_parameters optimized_fallback_source;
  optimized_fallback_source.model = mtf_model::empirical_fallback;
  optimized_fallback_source.sigma = 0.65;
  optimized_fallback_source.blur_diameter = 2.1;
  mtf_measurement optimized_fallback_measurement;
  optimized_fallback_measurement.name = "synthetic empirical fallback";
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      optimized_fallback_measurement.add_value (
          frequency, optimized_fallback_source.system_mtf (frequency) * 100);
    }
  mtf_parameters optimized_fallback_input = optimized_fallback_source;
  optimized_fallback_input.sigma = 0;
  optimized_fallback_input.blur_diameter = 0;
  optimized_fallback_input.measurements = {optimized_fallback_measurement};
  mtf_estimation_options optimized_fallback_options;
  optimized_fallback_options.model = mtf_model::empirical_fallback;
  optimized_fallback_options.optimize_sigma = true;
  optimized_fallback_options.optimize_blur_diameter = true;
  mtf_parameters optimized_fallback_result;
  const char *optimized_fallback_error = nullptr;
  const double optimized_fallback_objective
      = optimized_fallback_result.estimate_parameters (
          optimized_fallback_input, optimized_fallback_options, nullptr,
          nullptr, &optimized_fallback_error,
          mtf_parameters::estimate_use_nmsimplex
              | mtf_parameters::estimate_use_multifit);
  if (optimized_fallback_error || optimized_fallback_objective < 0
      || optimized_fallback_objective > 1e-7
      || optimized_fallback_result.model != mtf_model::empirical_fallback)
    {
      fprintf (stderr,
               "Explicit empirical fallback fit failed: sigma %g blur %g "
               "objective %g%s%s\n",
               optimized_fallback_result.sigma,
               optimized_fallback_result.blur_diameter,
               optimized_fallback_objective,
               optimized_fallback_error ? ": " : "",
               optimized_fallback_error ? optimized_fallback_error : "");
      ok = false;
    }

  /* A positive per-measurement wavelength is authoritative even when global
     and channel fallbacks disagree.  An explicit fit also selects the fitted
     analytical model instead of leaving the source measurement active.  */
  mtf_parameters authoritative_input = source;
  authoritative_input.wavelength = 640;
  authoritative_input.wavelengths[3] = 850;
  mtf_measurement authoritative_measurement = infrared;
  authoritative_measurement.wavelength = 750;
  authoritative_input.measurements = {authoritative_measurement};
  authoritative_input.measured_mtf_idx = 0;
  mtf_parameters authoritative_result;
  const char *authoritative_error = nullptr;
  const double authoritative_objective
      = authoritative_result.estimate_parameters (
          authoritative_input, fixed_zero_options, nullptr, nullptr,
          &authoritative_error, 0);
  if (authoritative_error || authoritative_objective < 0
      || authoritative_objective > 1e-8
      || authoritative_result.wavelength != 750
      || authoritative_result.measurements[0].wavelength != 750
      || authoritative_result.model != mtf_model::physical_diffraction
      || authoritative_result.measured_mtf_idx != -1)
    {
      fprintf (stderr,
               "Authoritative measurement wavelength or model activation "
               "failed: wavelength %g stored %g model %i selected %i "
               "objective %g%s%s\n",
               authoritative_result.wavelength,
               authoritative_result.measurements[0].wavelength,
               (int)authoritative_result.model,
               authoritative_result.measured_mtf_idx,
               authoritative_objective, authoritative_error ? ": " : "",
               authoritative_error ? authoritative_error : "");
      ok = false;
    }

  /* Excluded measurements do not constrain validation.  This permits users
     to retain an unfinished or legacy curve in the project while fitting a
     well-described subset.  */
  mtf_parameters selected_input = authoritative_input;
  mtf_measurement incomplete_measurement;
  incomplete_measurement.name = "excluded incomplete curve";
  incomplete_measurement.add_value (0, 100);
  selected_input.measurements.push_back (incomplete_measurement);
  mtf_estimation_options selected_options = fixed_zero_options;
  selected_options.include_measurements = {true, false};
  selected_options.optimize_measurement_wavelengths = {false, true};
  const char *selected_error = nullptr;
  if (!mtf_parameters::validate_estimation_options (
          selected_input, selected_options, &selected_error))
    {
      fprintf (stderr, "Excluded MTF curve affected validation: %s\n",
               selected_error ? selected_error : "unknown error");
      ok = false;
    }

  mtf_estimation_options invalid_model_options = fixed_zero_options;
  invalid_model_options.model = (mtf_model)99;
  if (mtf_parameters::validate_estimation_options (
          authoritative_input, invalid_model_options, &selected_error))
    {
      fprintf (stderr, "Invalid explicit MTF model was accepted\n");
      ok = false;
    }

  /* Front ends pass index-aligned vectors to the library.  Reject malformed
     selections before a solver can silently include or optimize the wrong
     curve.  */
  mtf_estimation_options malformed_selection = fixed_zero_options;
  malformed_selection.include_measurements = {true, false};
  if (mtf_parameters::validate_estimation_options (
          authoritative_input, malformed_selection, &selected_error))
    {
      fprintf (stderr, "Wrong-sized MTF inclusion vector was accepted\n");
      ok = false;
    }
  malformed_selection = fixed_zero_options;
  malformed_selection.optimize_measurement_wavelengths = {false, false};
  if (mtf_parameters::validate_estimation_options (
          authoritative_input, malformed_selection, &selected_error))
    {
      fprintf (stderr, "Wrong-sized MTF wavelength vector was accepted\n");
      ok = false;
    }

  /* Diffraction cutoff constrains approximately wavelength times f-number.
     With no fixed wavelength, fitting both scales is underdetermined and must
     be rejected rather than producing an arbitrary boundary solution.  */
  mtf_estimation_options unanchored_options = fixed_zero_options;
  unanchored_options.optimize_f_stop = true;
  unanchored_options.optimize_measurement_wavelengths = {true};
  if (mtf_parameters::validate_estimation_options (
          authoritative_input, unanchored_options, &selected_error))
    {
      fprintf (stderr, "Unanchored f-number/wavelength fit was accepted\n");
      ok = false;
    }

  /* The empirical model remains an explicit backup even when unrelated
     physical metadata happens to be present in the parameter object.  */
  mtf_parameters fallback_source = fallback;
  fallback_source.pixel_pitch = 3.760;
  fallback_source.scan_dpi = 1887;
  fallback_source.f_stop = 8;
  fallback_source.wavelength = 750;
  fallback_source.model = mtf_model::empirical_fallback;
  mtf_measurement fallback_measurement;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      fallback_measurement.add_value (
          frequency, fallback_source.system_mtf (frequency) * 100);
    }
  fallback_source.measurements = {fallback_measurement};
  fallback_source.measured_mtf_idx = 0;
  mtf_estimation_options fallback_options;
  fallback_options.model = mtf_model::empirical_fallback;
  mtf_parameters fallback_result;
  const char *fallback_error = nullptr;
  const double fallback_objective = fallback_result.estimate_parameters (
      fallback_source, fallback_options, nullptr, nullptr, &fallback_error, 0);
  if (fallback_error || fallback_objective < 0 || fallback_objective > 1e-8
      || fallback_result.model != mtf_model::empirical_fallback
      || fallback_result.simulate_diffraction_p ()
      || fallback_result.measured_mtf_idx != -1)
    {
      fprintf (stderr,
               "Explicit empirical fallback failed: model %i diffraction %i "
               "selected %i objective %g%s%s\n",
               (int)fallback_result.model,
               fallback_result.simulate_diffraction_p (),
               fallback_result.measured_mtf_idx, fallback_objective,
               fallback_error ? ": " : "",
               fallback_error ? fallback_error : "");
      ok = false;
    }

  /* Invalid fit-capable metadata must either be optimized or rejected.  The
     physical geometry itself remains mandatory and is never inferred from a
     single radial MTF curve.  */
  mtf_parameters invalid_metadata = source;
  invalid_metadata.measurements = {grayscale};
  invalid_metadata.f_stop = 0;
  const char *validation_error = nullptr;
  if (mtf_parameters::validate_estimation_options (
          invalid_metadata, fixed_zero_options, &validation_error))
    {
      fprintf (stderr, "Missing fixed f-number was accepted\n");
      ok = false;
    }
  mtf_estimation_options fitted_f_stop_options = fixed_zero_options;
  fitted_f_stop_options.optimize_f_stop = true;
  if (!mtf_parameters::validate_estimation_options (
          invalid_metadata, fitted_f_stop_options, &validation_error))
    {
      fprintf (stderr, "Optimized missing f-number was rejected: %s\n",
               validation_error ? validation_error : "unknown error");
      ok = false;
    }

  invalid_metadata = source;
  invalid_metadata.wavelength = 0;
  invalid_metadata.wavelengths = {0, 0, 0, 0};
  invalid_metadata.measurements = {grayscale};
  if (mtf_parameters::validate_estimation_options (
          invalid_metadata, fixed_zero_options, &validation_error))
    {
      fprintf (stderr, "Missing fixed measurement wavelength was accepted\n");
      ok = false;
    }
  mtf_estimation_options fitted_wavelength_options = fixed_zero_options;
  fitted_wavelength_options.optimize_measurement_wavelengths = {true};
  if (!mtf_parameters::validate_estimation_options (
          invalid_metadata, fitted_wavelength_options, &validation_error))
    {
      fprintf (stderr, "Optimized missing wavelength was rejected: %s\n",
               validation_error ? validation_error : "unknown error");
      ok = false;
    }

  /* Fit only the optional halo against a noiseless physical curve.  Core
     diffraction, defocus, residual Gaussian blur and sensor aperture remain
     fixed by supplied metadata.  */
  mtf_parameters halo_input = source;
  halo_input.halo_fraction = 0;
  halo_input.halo_sigma = 0;
  halo_input.measurements = {grayscale};
  mtf_parameters halo_estimated;
  const char *halo_error = nullptr;
  const int halo_flags = mtf_parameters::estimate_use_nmsimplex
                         | mtf_parameters::estimate_use_multifit
                         | mtf_parameters::estimate_halo;
  const double halo_objective = halo_estimated.estimate_parameters (
      halo_input, nullptr, nullptr, &halo_error, halo_flags);
  if (halo_error || halo_objective < 0 || halo_objective > 1e-8
      || std::abs (halo_estimated.halo_fraction - source.halo_fraction) > 2e-4
      || std::abs (halo_estimated.halo_sigma - source.halo_sigma) > 0.01)
    {
      fprintf (stderr,
               "Synthetic halo fit failed: fraction %0.17g sigma %0.17g "
               "objective %g%s%s\n",
               halo_estimated.halo_fraction, halo_estimated.halo_sigma,
               halo_objective, halo_error ? ": " : "",
               halo_error ? halo_error : "");
      ok = false;
    }

  /* Mirror the GUI's default physical fit on a curve which contains no broad
     halo.  Making the two halo parameters free by default must permit an exact
     zero-energy result rather than forcing the optional component into an
     otherwise complete diffraction, defocus and compact-blur model.  */
  mtf_parameters no_halo_source = source;
  no_halo_source.halo_fraction = 0;
  no_halo_source.halo_sigma = 0;
  mtf_measurement no_halo_measurement;
  no_halo_measurement.name = "synthetic halo-free physical MTF";
  no_halo_measurement.wavelength = no_halo_source.wavelength;
  for (int i = 0; i <= 100; i++)
    {
      const double frequency = i / 200.0;
      no_halo_measurement.add_value (
          frequency, no_halo_source.system_mtf (frequency) * 100);
    }
  mtf_parameters default_fit_input = no_halo_source;
  default_fit_input.measurements = {no_halo_measurement};
  mtf_estimation_options default_fit_options;
  default_fit_options.model = mtf_model::physical_diffraction;
  default_fit_options.optimize_sigma = true;
  default_fit_options.optimize_defocus = true;
  default_fit_options.optimize_halo_fraction = true;
  default_fit_options.optimize_halo_sigma = true;
  mtf_parameters default_fit_result;
  const char *default_fit_error = nullptr;
  const double default_fit_objective
      = default_fit_result.estimate_parameters (
          default_fit_input, default_fit_options, nullptr, nullptr,
          &default_fit_error,
          mtf_parameters::estimate_use_nmsimplex
              | mtf_parameters::estimate_use_multifit);
  if (default_fit_error || default_fit_objective < 0
      || default_fit_objective > 1e-8
      || default_fit_result.halo_fraction > 1e-6
      || std::abs (default_fit_result.sigma - no_halo_source.sigma) > 1e-5
      || std::abs (default_fit_result.defocus - no_halo_source.defocus)
             > 1e-5)
    {
      fprintf (stderr,
               "Default halo-enabled fit invented a halo: fraction %0.17g "
               "radius %0.17g sigma %0.17g defocus %0.17g objective %g%s%s\n",
               default_fit_result.halo_fraction,
               default_fit_result.halo_sigma, default_fit_result.sigma,
               default_fit_result.defocus, default_fit_objective,
               default_fit_error ? ": " : "",
               default_fit_error ? default_fit_error : "");
      ok = false;
    }

  /* Contrast samples are retained as double; this catches an accidental
     reintroduction of float storage in long measured curves.  */
  mtf_measurement precision_measurement;
  const double precise_contrast = 99.123456789012345;
  const double precise_uncertainty = 0.12345678901234567;
  precision_measurement.add_value (0.123456789012345, precise_contrast,
                                   precise_uncertainty);
  if (precision_measurement.get_contrast (0) != precise_contrast
      || precision_measurement.get_uncertainty (0) != precise_uncertainty)
    {
      fprintf (stderr,
               "Measured MTF contrast or uncertainty lost double precision\n");
      ok = false;
    }

  /* Project files must round-trip the explicit model and authoritative
     per-measurement metadata without losing double precision.  */
  render_parameters saved_render;
  mtf_parameters &saved_mtf = saved_render.sharpen.scanner_mtf;
  saved_mtf = source;
  saved_mtf.model = mtf_model::physical_diffraction;
  saved_mtf.wavelength = 750.12345678901238;
  saved_mtf.wavelengths
      = {620.12345678901238, 540.23456789012345, 460.34567890123457,
         750.45678901234567};
  saved_render.sharpen.supersample = 5;
  saved_render.sharpen.resampling
      = sharpen_parameters::lanczos8_resampling;
  saved_mtf.measured_mtf_idx = 0;
  mtf_measurement saved_measurement;
  saved_measurement.channel = 3;
  saved_measurement.wavelength = 750.56789012345678;
  saved_measurement.same_capture = false;
  saved_measurement.name = "quoted \"IR\" measurement \\ path";
  saved_measurement.add_value (0.012345678901234568,
                               99.123456789012351,
                               0.12345678901234567);
  saved_measurement.add_value (0.23456789012345678,
                               54.234567890123456,
                               0.98765432109876539);
  saved_measurement.add_value (0.5, 4.3456789012345679,
                               2.3456789012345679);
  saved_mtf.measurements = {saved_measurement};

  FILE *project = tmpfile ();
  render_parameters loaded_render;
  const char *project_error = nullptr;
  const bool project_saved
      = project && save_csp (project, nullptr, nullptr, &saved_render, nullptr);
  const bool project_loaded
      = project_saved && !fseek (project, 0, SEEK_SET)
        && load_csp (project, nullptr, nullptr, &loaded_render, nullptr,
                     &project_error);
  if (project)
    fclose (project);
  if (!project_loaded
      || !loaded_render.sharpen.scanner_mtf.equal_p (saved_mtf)
      || loaded_render.sharpen.supersample
             != saved_render.sharpen.supersample
      || loaded_render.sharpen.resampling
             != saved_render.sharpen.resampling)
    {
      fprintf (stderr, "MTF project-file round trip failed%s%s\n",
               project_error ? ": " : "",
               project_error ? project_error : "");
      ok = false;
    }

  /* A project written before the kernel selector existed must retain the old
     hard-coded Lanczos-8 rendering.  New parameter objects still default to
     Lanczos 3, but silently changing an old project's output is undesirable.  */
  FILE *legacy_project = tmpfile ();
  render_parameters legacy_render;
  const char *legacy_kernel_error = nullptr;
  const char legacy_text[]
      = "screen_alignment_version: 1\n"
        "deconvolution_supersample: 2\n"
        "screen_alignment_end\n";
  const bool legacy_loaded
      = legacy_project
        && fwrite (legacy_text, 1, sizeof (legacy_text) - 1, legacy_project)
               == sizeof (legacy_text) - 1
        && !fseek (legacy_project, 0, SEEK_SET)
        && load_csp (legacy_project, nullptr, nullptr, &legacy_render,
                     nullptr, &legacy_kernel_error);
  if (legacy_project)
    fclose (legacy_project);
  if (!legacy_loaded
      || legacy_render.sharpen.resampling
             != sharpen_parameters::lanczos8_resampling)
    {
      fprintf (stderr, "Legacy deconvolution kernel compatibility failed%s%s\n",
               legacy_kernel_error ? ": " : "",
               legacy_kernel_error ? legacy_kernel_error : "");
      ok = false;
    }

  /* Project files written before uncertainty estimates were added contain
     two-value scanner_mtf_point records.  They must continue to load with
     zero uncertainty, which selects the historical uniformly weighted fit.  */
  FILE *legacy_mtf_project = tmpfile ();
  render_parameters legacy_mtf_render;
  const char *legacy_mtf_error = nullptr;
  const char legacy_mtf_text[]
      = "screen_alignment_version: 1\n"
        "scanner_mtf_measurement: 0\n"
        "scanner_mtf_point: 0 100\n"
        "scanner_mtf_point: 0.5 10\n"
        "screen_alignment_end\n";
  const bool legacy_mtf_loaded
      = legacy_mtf_project
        && fwrite (legacy_mtf_text, 1, sizeof (legacy_mtf_text) - 1,
                   legacy_mtf_project)
               == sizeof (legacy_mtf_text) - 1
        && !fseek (legacy_mtf_project, 0, SEEK_SET)
        && load_csp (legacy_mtf_project, nullptr, nullptr,
                     &legacy_mtf_render, nullptr, &legacy_mtf_error);
  if (legacy_mtf_project)
    fclose (legacy_mtf_project);
  if (!legacy_mtf_loaded
      || legacy_mtf_render.sharpen.scanner_mtf.measurements.size () != 1
      || legacy_mtf_render.sharpen.scanner_mtf.measurements[0].size () != 2
      || legacy_mtf_render.sharpen.scanner_mtf.measurements[0]
                 .get_uncertainty (0)
             != 0
      || legacy_mtf_render.sharpen.scanner_mtf.measurements[0]
                 .get_uncertainty (1)
             != 0)
    {
      fprintf (stderr, "Legacy two-column MTF compatibility failed%s%s\n",
               legacy_mtf_error ? ": " : "",
               legacy_mtf_error ? legacy_mtf_error : "");
      ok = false;
    }

  return ok;
}

/* Verify interpolation of measured data and the complete measured-MTF blur /
   deconvolution path.  In particular, exercise every supersampling phase: the
   old code read before the Lanczos table at factors 3 and 4.  */
static bool
test_mtf_deconvolution ()
{
  bool ok = true;

  /* A three-point irregular table used to be silently treated as equidistant.  */
  mtf_parameters irregular_parameters
      = make_measured_mtf ({{0, 100}, {0.4, 40}, {0.5, 0}});
  mtf irregular (irregular_parameters);
  if (!irregular.precompute (nullptr, false)
      || std::abs (irregular.get_mtf (0.2) - 0.7) > 0.002
      || std::abs (irregular.get_mtf (0.4) - 0.4) > 0.002)
    {
      fprintf (stderr, "Irregular measured MTF interpolation failed\n");
      ok = false;
    }

  /* MTF is relative to DC; normalize a non-100-percent measured DC value.  */
  mtf_parameters normalized_parameters
      = make_measured_mtf ({{0, 80}, {0.25, 40}, {0.5, 0}});
  mtf normalized (normalized_parameters);
  if (!normalized.precompute (nullptr, false)
      || std::abs (normalized.get_mtf (0) - 1) > 0.000001
      || std::abs (normalized.get_mtf (0.25) - 0.5) > 0.002)
    {
      fprintf (stderr, "Measured MTF DC normalization failed\n");
      ok = false;
    }

  /* Corrections to a measured MTF and supersampling affect cached output.  */
  mtf_parameters corrected_parameters = irregular_parameters;
  corrected_parameters.sigma = 0.25;
  if (corrected_parameters == irregular_parameters)
    {
      fprintf (stderr, "Measured MTF cache key ignores sigma\n");
      ok = false;
    }
  sharpen_parameters sharpen1, sharpen2;
  sharpen1.mode = sharpen_parameters::blur_deconvolution;
  sharpen1.scanner_mtf = irregular_parameters;
  sharpen1.scanner_mtf_scale = 1;
  sharpen1.supersample = 2;
  sharpen2 = sharpen1;
  sharpen2.supersample = 3;
  if (sharpen1 == sharpen2)
    {
      fprintf (stderr, "Deconvolution cache key ignores supersampling\n");
      ok = false;
    }
  sharpen2 = sharpen1;
  sharpen2.resampling = sharpen_parameters::lanczos8_resampling;
  if (sharpen1 == sharpen2)
    {
      fprintf (stderr, "Deconvolution cache key ignores resampling kernel\n");
      ok = false;
    }
  sharpen1.supersample = 1;
  sharpen2 = sharpen1;
  sharpen2.resampling = sharpen_parameters::lanczos8_resampling;
  if (!(sharpen1 == sharpen2))
    {
      fprintf (stderr,
               "Inactive resampling kernel unnecessarily invalidates cache\n");
      ok = false;
    }

  /* Repeated reflection must remain in bounds even when the requested optical
     border is wider than a small image.  */
  if (reflect_deconvolution_coordinate (-1, 4) != 1
      || reflect_deconvolution_coordinate (4, 4) != 3
      || reflect_deconvolution_coordinate (-7, 4) != 0
      || reflect_deconvolution_coordinate (8, 4) != 1)
    {
      fprintf (stderr, "Repeated deconvolution reflection failed\n");
      ok = false;
    }

  /* An axial slanted-edge table reaches 0.5 cycles/pixel, whereas a 2-D image
     has valid diagonal frequencies out to sqrt (2) / 2.  The curve must taper
     conservatively over that interval instead of dropping to zero one table
     step after 0.5.  */
  std::vector<std::pair<double, double>> dense_nyquist_points;
  for (int i = 0; i <= 100; i++)
    dense_nyquist_points.push_back ({i / 200.0, 100});
  mtf_parameters diagonal_parameters
      = make_measured_mtf (dense_nyquist_points);
  mtf diagonal_mtf (diagonal_parameters);
  constexpr double diagonal_frequency_x = 0.4;
  constexpr double diagonal_frequency_y = 0.4;
  const double diagonal_frequency
      = std::hypot (diagonal_frequency_x, diagonal_frequency_y);
  const double diagonal_nyquist = std::sqrt (0.5);
  const double expected_diagonal_tail
      = (diagonal_nyquist - diagonal_frequency)
        / (diagonal_nyquist - 0.5);
  if (!diagonal_mtf.precompute (nullptr, false)
      || std::abs (diagonal_mtf.get_mtf (diagonal_frequency)
                   - expected_diagonal_tail)
             > 0.003)
    {
      fprintf (stderr, "Measured MTF diagonal-Nyquist tail failed\n");
      ok = false;
    }
  else
    {
      auto diagonal_signal = [] (int x, int y) {
        return 0.5
               + 0.2
                     * std::cos (2 * M_PI
                                 * (diagonal_frequency_x * x
                                    + diagonal_frequency_y * y));
      };
      deconvolution<double> diagonal_filter (
          &diagonal_mtf, 1, 1000, 0, 1,
          deconvolution<double>::blur, 0, 2,
          sharpen_parameters::lanczos8_resampling);
      fill_deconvolution_tile (diagonal_filter, diagonal_signal);
      const double diagonal_response
          = deconvolution_cosine_amplitude_2d (
                diagonal_filter, diagonal_frequency_x,
                diagonal_frequency_y, 0.5)
            / 0.2;
      deconvolution<double> fast_diagonal_filter (
          &diagonal_mtf, 1, 1000, 0, 1,
          deconvolution<double>::blur, 0, 2,
          sharpen_parameters::lanczos3_resampling);
      fill_deconvolution_tile (fast_diagonal_filter, diagonal_signal);
      const double fast_diagonal_response
          = deconvolution_cosine_amplitude_2d (
                fast_diagonal_filter, diagonal_frequency_x,
                diagonal_frequency_y, 0.5)
            / 0.2;
      if (!(diagonal_response > 0.55 && diagonal_response < 0.75
            && fast_diagonal_response > 0.35
            && diagonal_response > fast_diagonal_response + 0.04))
        {
          fprintf (stderr,
                   "Resampling-kernel diagonal responses are unexpected "
                   "(Lanczos 3 %g, Lanczos 8 %g)\n",
                   fast_diagonal_response, diagonal_response);
          ok = false;
        }
    }

  /* Forward blur of a known physical OTF must also preserve a phase reversal
     through the actual FFT filtering path.  Use an exactly periodic cosine so
     the sign of the recovered coefficient is unambiguous.  */
  mtf_parameters signed_blur_parameters;
  signed_blur_parameters.scan_dpi = 1887;
  signed_blur_parameters.f_stop = 8;
  signed_blur_parameters.wavelength = 750;
  signed_blur_parameters.pixel_pitch = 3.760;
  signed_blur_parameters.sensor_fill_factor = 0;
  signed_blur_parameters.defocus = 0.5;
  mtf signed_blur_mtf (signed_blur_parameters);
  constexpr double signed_frequency = 0.1875;
  const double expected_signed_transfer
      = signed_blur_parameters.system_otf (signed_frequency);
  auto signed_signal = [] (int x, int) {
    return 0.5 + 0.2 * std::cos (2 * M_PI * signed_frequency * x);
  };
  deconvolution<double> signed_filter (
      &signed_blur_mtf, 1, 1000, 0, 1, deconvolution<double>::blur, 0, 1,
      sharpen_parameters::lanczos3_resampling);
  fill_deconvolution_tile (signed_filter, signed_signal);
  const double signed_response
      = deconvolution_cosine_amplitude_2d (signed_filter, signed_frequency, 0,
                                           0.5)
        / 0.2;
  if (!(expected_signed_transfer < -0.01 && signed_response < -0.01)
      || std::abs (signed_response - expected_signed_transfer) > 0.02)
    {
      fprintf (stderr,
               "Physical OTF sign was lost by forward filtering: expected "
               "%g got %g\n",
               expected_signed_transfer, signed_response);
      ok = false;
    }

  /* The test signal is entirely inside the flat passband.  Blur mode must
     therefore be an identity operation apart from tiny resampling error.  */
  mtf_parameters flat_parameters
      = make_measured_mtf ({{0, 100}, {0.25, 100}, {0.5, 0}});
  mtf flat (flat_parameters);
  auto smooth_signal = [] (int x, int y) {
    return 0.5 + 0.17 * std::sin (2 * M_PI * 0.03125 * x)
           + 0.11 * std::cos (2 * M_PI * 0.046875 * y);
  };
  for (enum sharpen_parameters::resampling_kernel kernel
       : {sharpen_parameters::lanczos3_resampling,
          sharpen_parameters::lanczos8_resampling})
    for (int supersample : {1, 2, 3, 4, 5, 8, 16})
      {
        deconvolution<double> filter (&flat, 1, 1000, 0, 1,
                                      deconvolution<double>::blur, 0,
                                      supersample, kernel);
        fill_deconvolution_tile (filter, smooth_signal);
        const double error = deconvolution_tile_rmse (filter, smooth_signal);
        if (!(error < 0.00005))
          {
            fprintf (stderr,
                     "Identity MTF failed for %s at supersampling %i "
                     "(RMSE %g)\n",
                     sharpen_parameters::resampling_kernel_names[(int)kernel]
                         .name,
                     supersample, error);
            ok = false;
          }
      }

  if (sharpen_parameters ().resampling
      != sharpen_parameters::lanczos3_resampling)
    {
      fprintf (stderr, "Fast resampling kernel is not the default\n");
      ok = false;
    }

  /* A nonpositive SNR must not divide by zero or create NaNs.  The public
     parameter path disables this mode, while the low-level class is hardened
     to an identity filter as well.  */
  deconvolution<double> zero_snr (&flat, 1, 0, 0, 1,
                                  deconvolution<double>::sharpen, 0, 2);
  fill_deconvolution_tile (zero_snr, smooth_signal);
  const double zero_snr_error
      = deconvolution_tile_rmse (zero_snr, smooth_signal);
  /* Even supersampling returns to the original pixel center through the common
     bicubic midpoint reconstruction.  Lanczos 3 therefore has a small but
     bounded round-trip error even when the Fourier transfer is identity.  */
  if (!(zero_snr_error < 0.00007))
    {
      fprintf (stderr, "Zero-SNR Wiener filter is not finite identity (%g)\n",
               zero_snr_error);
      ok = false;
    }
  sharpen_parameters disabled_wiener;
  disabled_wiener.mode = sharpen_parameters::wiener_deconvolution;
  disabled_wiener.scanner_mtf_scale = 1;
  disabled_wiener.scanner_snr = 0;
  if (disabled_wiener.get_mode () != sharpen_parameters::none)
    {
      fprintf (stderr, "Public zero-SNR Wiener mode was not disabled\n");
      ok = false;
    }
  disabled_wiener.scanner_snr = test_runtime_infinity_luminosity ();
  if (disabled_wiener.get_mode () != sharpen_parameters::none)
    {
      fprintf (stderr, "Public nonfinite-SNR Wiener mode was not disabled\n");
      ok = false;
    }

  /* Zero Richardson-Lucy iterations likewise mean no processing, both in the
     public parameter layer and in the low-level worker.  */
  sharpen_parameters disabled_richardson_lucy;
  disabled_richardson_lucy.mode
      = sharpen_parameters::richardson_lucy_deconvolution;
  disabled_richardson_lucy.scanner_mtf_scale = 1;
  disabled_richardson_lucy.richardson_lucy_iterations = 0;
  if (disabled_richardson_lucy.get_mode () != sharpen_parameters::none)
    {
      fprintf (stderr,
               "Public zero-iteration Richardson-Lucy mode was not disabled\n");
      ok = false;
    }
  deconvolution<double> zero_iteration_richardson_lucy (
      &flat, 1, 1000, 0, 1,
      deconvolution<double>::richardson_lucy_sharpen, 0, 2);
  fill_deconvolution_tile (zero_iteration_richardson_lucy, smooth_signal);
  const double zero_iteration_error = deconvolution_tile_rmse (
      zero_iteration_richardson_lucy, smooth_signal);
  if (!(zero_iteration_error < 0.00007))
    {
      fprintf (stderr,
               "Zero-iteration Richardson-Lucy is not identity (%g)\n",
               zero_iteration_error);
      ok = false;
    }

  /* Build a sampled Gaussian MTF like one obtained by a slanted-edge
     measurement, then verify both forward blur and inverse filters against its
     analytical response at one spatial frequency.  */
  constexpr double sigma = 1.15;
  constexpr double frequency = 0.0625;
  constexpr double amplitude = 0.2;
  constexpr double mean = 0.5;
  constexpr double snr = 2000;
  std::vector<std::pair<double, double>> gaussian_points;
  for (int i = 0; i <= 150; i++)
    {
      const double f = i / 100.0;
      const double response
          = std::exp (-2 * M_PI * M_PI * sigma * sigma * f * f);
      gaussian_points.push_back ({f, response * 100});
    }
  mtf_parameters gaussian_parameters = make_measured_mtf (gaussian_points);
  mtf gaussian (gaussian_parameters);
  const double expected_response
      = std::exp (-2 * M_PI * M_PI * sigma * sigma * frequency * frequency);
  auto original_signal = [] (int x, int) {
    return mean + amplitude * std::cos (2 * M_PI * frequency * x);
  };
  auto blurred_signal = [expected_response] (int x, int) {
    return mean + amplitude * expected_response
                      * std::cos (2 * M_PI * frequency * x);
  };

  deconvolution<double> blur (&gaussian, 1, snr, 0, 1,
                              deconvolution<double>::blur, 0, 2);
  fill_deconvolution_tile (blur, original_signal);
  const double measured_response
      = deconvolution_cosine_amplitude (blur, frequency, mean) / amplitude;

  deconvolution<double> wiener (&gaussian, 1, snr, 0, 1,
                                deconvolution<double>::sharpen, 0, 2);
  fill_deconvolution_tile (wiener, blurred_signal);
  const double wiener_response
      = deconvolution_cosine_amplitude (wiener, frequency, mean) / amplitude;

  deconvolution<double> richardson_lucy (
      &gaussian, 1, snr, 0, 1,
      deconvolution<double>::richardson_lucy_sharpen, 25, 2);
  fill_deconvolution_tile (richardson_lucy, blurred_signal);
  const double richardson_lucy_response
      = deconvolution_cosine_amplitude (richardson_lucy, frequency, mean)
        / amplitude;

  if (std::abs (measured_response - expected_response) > 0.015
      || std::abs (wiener_response - 1) > 0.025
      || std::abs (richardson_lucy_response - 1) > 0.04)
    {
      fprintf (stderr,
               "Measured-MTF deconvolution failed: expected blur %g, got %g; "
               "Wiener %g; Richardson-Lucy %g\n",
               expected_response, measured_response, wiener_response,
               richardson_lucy_response);
      ok = false;
    }

  /* Exercise the production single-precision path with an MTF broad enough to
     require a 4096 by 4096 FFT at SCALE.  FFTW stores the normalized DC
     coefficient as 1 / N^2; at N = 4096 this is smaller than float epsilon.
     The coefficient must nevertheless be recognized as a valid unit-DC MTF,
     rather than causing the complete optical kernel to be replaced by an
     identity filter.  Build the table from the Phase One / macro-lens model so
     this test also follows the measured-MTF input path used after calibration.  */
  mtf_parameters phase_one_model_parameters;
  phase_one_model_parameters.f_stop = 8;
  phase_one_model_parameters.scan_dpi = 4000;
  phase_one_model_parameters.pixel_pitch = 3.76;
  phase_one_model_parameters.wavelength = 750;
  mtf phase_one_model (phase_one_model_parameters);
  if (!phase_one_model.precompute (nullptr, false))
    {
      fprintf (stderr, "Phase One reference MTF precomputation failed\n");
      ok = false;
    }
  else
    {
      std::vector<std::pair<double, double>> phase_one_points;
      for (int i = 0; i <= 200; i++)
        {
          const double f = i / 200.0;
          phase_one_points.push_back ({f, phase_one_model.get_mtf (f) * 100});
        }
      mtf_parameters phase_one_measured_parameters
          = make_measured_mtf (phase_one_points);
      mtf phase_one_measured (phase_one_measured_parameters);
      constexpr double phase_one_scale = 16;
      constexpr double phase_one_frequency = 0.1;
      constexpr double high_resolution_frequency
          = phase_one_frequency / phase_one_scale;
      const double phase_one_expected
          = phase_one_model.get_mtf (phase_one_frequency);
      auto phase_one_signal = [] (int x, int) {
        return mean
               + amplitude
                     * std::cos (2 * M_PI * high_resolution_frequency * x);
      };
      deconvolution<float> phase_one_blur (
          &phase_one_measured, phase_one_scale, snr, 0, 1,
          deconvolution<float>::blur, 0, 1);
      if (phase_one_blur.get_tile_size_with_borders () < 4096)
        {
          fprintf (stderr,
                   "Single-precision large-FFT regression uses only %i pixels\n",
                   phase_one_blur.get_tile_size_with_borders ());
          ok = false;
        }
      else
        {
          fill_deconvolution_tile (phase_one_blur, phase_one_signal);
          const double phase_one_response
              = deconvolution_cosine_amplitude (
                    phase_one_blur, high_resolution_frequency, mean)
                / amplitude;
          if (std::abs (phase_one_response - phase_one_expected) > 0.015)
            {
              fprintf (stderr,
                       "Single-precision 4096-FFT MTF failed: expected %g, "
                       "got %g\n",
                       phase_one_expected, phase_one_response);
              ok = false;
            }
        }
    }

  /* The spatial PSF reconstructed from the same Gaussian MTF should have
     the expected radial Gaussian shape.  This also exercises PSF support
     estimation and the precompute_psf locking path.  */
  const char *psf_error = nullptr;
  const double expected_psf_ratio
      = std::exp (-1 / (2 * sigma * sigma));
  if (!gaussian.precompute_psf (nullptr, false, nullptr, &psf_error)
      || psf_error || gaussian.psf_radius (1) <= 0
      || !my_isfinite (gaussian.get_psf (0))
      || gaussian.get_psf (0) <= 0
      || std::abs (gaussian.get_psf (1) / gaussian.get_psf (0)
                   - expected_psf_ratio)
             > 0.02)
    {
      fprintf (stderr, "Measured-MTF PSF reconstruction failed%s%s\n",
               psf_error ? ": " : "", psf_error ? psf_error : "");
      ok = false;
    }

  /* SNR is a scalar regularization control.  At a suitably conservative value
     it must suppress high-frequency contamination while restoring the blurred
     low-frequency signal.  */
  auto noisy_blurred_signal = [expected_response] (int x, int y) {
    return mean + amplitude * expected_response
                      * std::cos (2 * M_PI * frequency * x)
           + 0.0035 * std::cos (2 * M_PI * 0.3125 * x + 0.7)
           + 0.0035
                 * std::cos (2 * M_PI * (0.28125 * x + 0.21875 * y));
  };
  deconvolution<double> regularized_wiener (
      &gaussian, 1, 10, 0, 1, deconvolution<double>::sharpen, 0, 2);
  fill_deconvolution_tile (regularized_wiener, noisy_blurred_signal);
  const int regularized_border = regularized_wiener.get_border_size ();
  const int regularized_size = regularized_wiener.get_basic_tile_size ();
  const int regularized_margin = std::min (8, regularized_size / 8);
  long double noisy_error_squared = 0;
  long double restored_error_squared = 0;
  size_t regularized_samples = 0;
  for (int y = regularized_margin;
       y < regularized_size - regularized_margin; y++)
    for (int x = regularized_margin;
         x < regularized_size - regularized_margin; x++)
      {
        const double expected = original_signal (x, y);
        const double noisy_error = noisy_blurred_signal (x, y) - expected;
        const double restored_error
            = regularized_wiener.get_pixel (
                  0, x + regularized_border, y + regularized_border)
              - expected;
        noisy_error_squared += noisy_error * noisy_error;
        restored_error_squared += restored_error * restored_error;
        regularized_samples++;
      }
  const double noisy_error
      = std::sqrt ((double)(noisy_error_squared / regularized_samples));
  const double regularized_error
      = std::sqrt ((double)(restored_error_squared / regularized_samples));
  if (!my_isfinite (regularized_error)
      || regularized_error >= noisy_error * 0.6)
    {
      fprintf (stderr,
               "Regularized Wiener filtering failed: input RMSE %g, "
               "output RMSE %g\n",
               noisy_error, regularized_error);
      ok = false;
    }

  return ok;
}

/* Internal unit test for the precomputed_function class.  */
static bool
test_precomputed_function ()
{
  bool ok = true;
  /* Test functional constructor with x^2.  */
  precomputed_function<double> f (0, 10, 101, [] (double x) { return x * x; });

  for (double x = 0; x <= 10; x += 0.5)
    {
      double val = f.apply (x);
      if (std::abs (val - x * x) > 0.01)
        {
          printf ("FAILED: precomputed_function x^2 mismatch at %f: "
                  "expected %f, got %f\n",
                  (double)x, (double)(x * x), (double)val);
          ok = false;
        }
    }

  /* Test move constructor.  */
  precomputed_function<double> f2 = std::move (f);
  if (std::abs (f2.apply (5.0) - 25.0) > 0.01)
    {
      printf ("FAILED: precomputed_function move constructor failed!\n");
      ok = false;
    }

  /* Test monotonicity and inverse.  */
  if (std::abs (f2.invert (49.0) - 7.0) > 0.01)
    {
      printf ("FAILED: precomputed_function inverse mismatch: "
              "expected 7.0, got %f\n",
              (double)f2.invert (49.0));
      ok = false;
    }

  return ok;
}

/* Internal unit test for the histogram parallel collection.  */
static bool
test_histogram_parallel ()
{
  printf ("Testing histogram parallel collection...\n");
  histogram h;
  const int n = 1000000;

  /* Stage 1: Parallel range.  */
#pragma omp parallel for reduction(histogram_range : h)
  for (int i = 0; i < n; i++)
    h.pre_account ((luminosity_t)(i % 1000));

  if (h.find_min (0) != 0 || h.find_max (0) != 999)
    {
      printf ("FAILED: Parallel range mismatch! Min: %f Max: %f\n",
              (double)h.find_min (0), (double)h.find_max (0));
      return false;
    }

  h.finalize_range (1000);

  /* Stage 2: Parallel entries.  */
#pragma omp parallel for reduction(histogram_entries : h)
  for (int i = 0; i < n; i++)
    h.account ((luminosity_t)(i % 1000));

  h.finalize ();

  if (h.num_samples () != n)
    {
      printf ("FAILED: Total count mismatch! Expected %i, got %i\n", n,
              h.num_samples ());
      return false;
    }

  for (int i = 0; i < 1000; i++)
    if (h.entry (i) != 1000)
      {
        printf ("FAILED: Entry %i count mismatch! Expected 1000, got %llu\n", i,
                (unsigned long long)h.entry (i));
        return false;
      }

  return true;
}

bool
test_richards_curve ()
{
  bool ok = true;
  // Test direct curve
  luminosity_t A = -5;
  luminosity_t K = 5;
  luminosity_t B = 1.2;
  luminosity_t M = 0;
  luminosity_t v = 0.8;
  
  hd_curve_parameters params_direct = richards_to_hd_curve_parameters({A, K, B, M, v, false});
  richards_hd_curve curve_direct(1000, params_direct);
  
  for (int i = 5; i < 95; i++)
    {
      luminosity_t X = params_direct.minx + i * (params_direct.maxx - params_direct.minx) / 100.0;
      luminosity_t expected = A + (K - A) / std::pow(1.0 + std::exp(-B * (X - M)), 1.0 / v);
      luminosity_t actual = curve_direct.apply(X);
      if (std::abs(expected - actual) > 0.3)
        {
          printf ("Direct Richards curve mismatch at x=%f: expected %f, got %f\n", X, expected, actual);
          ok = false;
        }
    }

  // Test inverse curve
  luminosity_t Ax = -6;
  luminosity_t Kx = 6;
  luminosity_t Bx = 1.0;
  luminosity_t Mx = 0;
  luminosity_t vx = 1.0;
  
  hd_curve_parameters params_inverse = richards_to_hd_curve_parameters({Ax, Kx, Bx, Mx, vx, true});
  // Test inverse curve with more points for better logit resolution
  richards_hd_curve curve_inverse(10000, params_inverse);
  
  for (int i = 48; i < 52; i++)
    {
      luminosity_t Y = params_inverse.miny + i * (params_inverse.maxy - params_inverse.miny) / 100.0;
      luminosity_t expected_X = Ax + (Kx - Ax) / std::pow(1.0 + std::exp(-Bx * (Y - Mx)), 1.0 / vx);
      
      // Probing Inverse Richards Curve (X as function of Y)
      luminosity_t actual_Y = curve_inverse.apply(expected_X);
      if (std::abs(Y - actual_Y) > 1.0)
        {
          printf ("Inverse Richards curve mismatch at X=%f: expected Y=%f, got Y=%f\n", expected_X, Y, actual_Y);
          ok = false;
        }
    }
  return ok;
}

bool
test_richards_symmetry ()
{
  bool ok = true;
  hd_curve_parameters p;
  p.minx = -2.274010; p.miny = 3.400111;
  p.linear1x = -1.341965; p.linear1y = 1.402846;
  p.linear2x = -0.789100; p.linear2y = 0.927726;
  p.maxx = -0.437047; p.maxy = -0.003900;
  
  // 1. Toggling inverse should match swapping X and Y
  hd_curve_parameters p_swapped(p.miny, p.minx, p.linear1y, p.linear1x, p.linear2y, p.linear2x, p.maxy, p.maxx);
  
  auto r1 = hd_to_richards_curve_parameters(p); // detected direct (if gamma is low) or inverse
  auto r2 = hd_to_richards_curve_parameters(p_swapped);
  
  // They should have same B, M, v but A, K swapped and is_inverse toggled
  if (std::abs(r1.B - r2.B) > 1e-4 || std::abs(r1.v - r2.v) > 1e-4)
    {
       printf("Richards Symmetry FAIL: B1=%f B2=%f, v1=%f v2=%f\n", r1.B, r2.B, r1.v, r2.v);
       ok = false;
    }
    
  // 2. richards_to_hd should also be symmetric
  auto p1 = richards_to_hd_curve_parameters({-2, 2, 1.5, 0, 0.5, true});
  auto p2 = richards_to_hd_curve_parameters({-2, 2, 1.5, 0, 0.5, false});
  
  // p1.miny should be p2.minx, etc.
  if (std::abs(p1.miny - p2.minx) > 1e-4 || std::abs(p1.maxx - p2.maxy) > 1e-4)
    {
       printf("Richards Reverse Symmetry FAIL: p1.miny=%f p2.minx=%f\n", p1.miny, p2.minx);
       ok = false;
    }
    
  return ok;
}

bool
test_richards_reversibility ()
{
  bool ok = true;
  /* Test points: A, K, B, M, v, is_inverse */
  struct richards_curve_parameters test_params[] = {
    {0.0, 1.0, 1.5, 0.5, 1.0, false},
    {0.1, 2.5, 2.0, -1.0, 0.8, false},
    {-0.5, 3.0, 0.5, 2.0, 1.5, false},
    {0.0, 4.0, 1.0, 2.0, 1.0, true},
    {1.0, 5.0, 0.7, 3.0, 1.2, true},
    /* User case: Negative slope curve (requires negative B in inverse mode) */
    {-2.274, -0.437, -3.986, 0.997, 2.647, true},
    /* User case: Negative slope curve in direct mode (requires x-swapping) */
    {3.4, 0.0, 1.7, 1.0, 1.0, false}
  };

  for (auto &p : test_params)
    {
       hd_curve_parameters hdp = richards_to_hd_curve_parameters(p);
       richards_curve_parameters rp = hd_to_richards_curve_parameters(hdp);
       
       /* We use heuristic to determine v, so it is not recovered perfectly.  */
       if (std::abs(rp.A - p.A) > 1e-4 || std::abs(rp.K - p.K) > 1e-4 ||
           std::abs(rp.B - p.B) > 0.2 || std::abs(rp.M - p.M) > 0.1 ||
           std::abs(rp.v - p.v) > 0.2 || rp.is_inverse != p.is_inverse)
         {
            printf ("Richards reversibility failed for %s mode!\n", p.is_inverse ? "inverse" : "direct");
            printf ("Expected: A=%f, K=%f, B=%f, M=%f, v=%f\n", p.A, p.K, p.B, p.M, p.v);
            printf ("Got:      A=%f, K=%f, B=%f, M=%f, v=%f\n", rp.A, rp.K, rp.B, rp.M, rp.v);
            ok = false;
         }
    }
  return ok;
}

bool
test_richards_functional_inverse ()
{
  bool ok = true;
  /* Test that Richards_inv(Richards_dir(X)) == X (functionally)
     Note: In our implementation, richards_hd_curve(p, true) maps LogE -> Density 
     using the inverted formula. So curve_inv.apply(f_dir(y)) should be y. */
     
  luminosity_t A = 0.5, K = 3.5, B = 1.2, M = 1.0, v = 0.8;
  richards_curve_parameters rp_dir(A, K, B, M, v, false);
  richards_curve_parameters rp_inv(A, K, B, M, v, true);
  
  richards_hd_curve curve_dir(1000, rp_dir);
  richards_hd_curve curve_inv(1000, rp_inv);
  
  // Probing: density y -> exposure x=Richards(y) -> recovered_y = curve_inv.apply(x)
  for (int i = 20; i < 80; i++)
    {
      luminosity_t y = -5.0 + i * 10.0 / 100.0;
      // Step 1: Calculate LogE from Density using Direct Richards formula
      luminosity_t x = A + (K - A) / std::pow(1.0 + std::exp(-B * (y - M)), 1.0 / v);
      
      // Step 2: Use Inverted H&D curve to map LogE back to Density
      luminosity_t recovered_y = curve_inv.apply(x);
      
      if (std::abs(y - recovered_y) > 0.1)
        {
          printf ("Richards functional inverse failed at y=%f: x=%f, recovered_y=%f\n", y, x, recovered_y);
          ok = false;
        }
    }
    
  return ok;
}

bool
test_hd_reversibility ()
{
  bool ok = true;
  /* Test that HD -> Richards -> HD' -> Richards is stable.
     Starting with user provided sample parameters. */
  hd_curve_parameters hurley = {
      -2.745997, 3.133772,
      -1.930210, 2.190697,
      -0.970248, 1.208836,
      -0.299072, -0.399532
  };
  
  // Step 1: Fit Richards model to original H&D
  richards_curve_parameters rp1 = hd_to_richards_curve_parameters(hurley);

  /* Check that the Richards model actually passes through the original knots. 
     Our analytic solver in hd_to_richards is designed to be exact for knots. */
  luminosity_t y1_fit = richards_hd_curve::eval_richards(rp1, hurley.linear1x);
  luminosity_t y2_fit = richards_hd_curve::eval_richards(rp1, hurley.linear2x);
  
  if (std::abs(y1_fit - hurley.linear1y) > 1e-3 || std::abs(y2_fit - hurley.linear2y) > 1e-3)
    {
       printf ("Richards fit fidelity failed for Hurley parameters!\n");
       printf ("L1: expected %f, got %f\n", hurley.linear1y, y1_fit);
       printf ("L2: expected %f, got %f\n", hurley.linear2y, y2_fit);
       ok = false;
    }
  
  // Step 2: Generate new H&D points from that Richards model
  hd_curve_parameters hdp_prime = richards_to_hd_curve_parameters(rp1);
  
  // They represent the same curve. Check that the reconstructed knots are on the original model.
  luminosity_t y1_prime_fit = richards_hd_curve::eval_richards(rp1, hdp_prime.linear1x);
  luminosity_t y2_prime_fit = richards_hd_curve::eval_richards(rp1, hdp_prime.linear2x);

  if (std::abs(y1_prime_fit - hdp_prime.linear1y) > 1e-4 || 
      std::abs(y2_prime_fit - hdp_prime.linear2y) > 1e-4)
    {
      printf ("Reconstructed H&D points are not on the Richards sigmoid!\n");
      ok = false;
    }

  return ok;
}

bool
test_hd_incremental_update ()
{
  bool ok = true;
  /* Test that adjusting Richards parameters via geometric transformations 
     actually produces a valid and consistent new model. */
  hd_curve_parameters hurley = {
      -2.745997, 3.133772,
      -1.930210, 2.190697,
      -0.970248, 1.208836,
      -0.299072, -0.399532
  };
  
  auto rp1 = hd_to_richards_curve_parameters(hurley);
  
  // 1. Test M adjustment (Translation)
  {
    auto rp_new = rp1;
    rp_new.M += 0.5;
    auto hurley_new = hurley;
    hurley_new.adjust_M(rp1.M, rp_new.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental M adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.M - rp_new.M) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental M adjustment: parameter drift! M: expected %f got %f, B: expected %f got %f\n",
		rp_new.M, rp_fit.M, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  // 2. Test B adjustment (Scaling)
  {
    auto rp_new = rp1;
    rp_new.B *= 1.2;
    auto hurley_new = hurley;
    hurley_new.adjust_B(rp1.B, rp_new.B, rp1.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental B adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.B - rp_new.B) > 0.05 || std::abs(rp_fit.M - rp_new.M) > 1e-3)
      {
	printf ("Incremental B adjustment: parameter drift! B: expected %f got %f, M: expected %f got %f\n",
		rp_new.B, rp_fit.B, rp_new.M, rp_fit.M);
	ok = false;
      }
  }

  // 3. Test v adjustment (Non-linear)
  {
    auto rp_new = rp1;
    rp_new.v *= 0.8;
    auto hurley_new = hurley;
    hurley_new.adjust_v(rp1.v, rp_new.v, rp1.B, rp1.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental v adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.v - rp_new.v) > 0.1 || std::abs(rp_fit.B - rp_new.B) > 0.1 || std::abs(rp_fit.M - rp_new.M) > 0.05)
      {
	printf ("Incremental v adjustment: parameter drift! v: exp %f got %f, B: exp %f got %f, M: exp %f got %f\n",
		rp_new.v, rp_fit.v, rp_new.B, rp_fit.B, rp_new.M, rp_fit.M);
	ok = false;
      }
  }

  // 4. Test A adjustment (Lower asymptote)
  {
    auto rp_new = rp1;
    rp_new.A -= 0.2;
    auto hurley_new = hurley;
    hurley_new.adjust_A(rp1.A, rp_new.A, rp1.K);

    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental A adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.A - rp_new.A) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental A adjustment: parameter drift! A: exp %f got %f, B: exp %f got %f\n",
		rp_new.A, rp_fit.A, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  // 5. Test K adjustment (Upper asymptote)
  {
    auto rp_new = rp1;
    rp_new.K += 0.3;
    auto hurley_new = hurley;
    hurley_new.adjust_K(rp1.K, rp_new.K, rp1.A);

    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental K adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.K - rp_new.K) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental K adjustment: parameter drift! K: exp %f got %f, B: exp %f got %f\n",
		rp_new.K, rp_fit.K, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  return ok;
}

bool
test_hd_validity ()
{
  bool ok = true;
  // S-shape Direct
  hd_curve_parameters p1 (-3, 0, -2, 1, 2, 3, 3, 4);
  if (!p1.is_valid_for_richards_curve())
    {
      printf("H&D validity failed for valid direct curve\n");
      ok = false;
    }
  
  // Non-monotonic X (X-loop)
  hd_curve_parameters p2 (-3, 0, 1, 1, -2, 3, 3, 4);
  if (p2.is_valid_for_richards_curve())
    {
      printf("H&D validity failed: accepted non-monotonic X\n");
      ok = false;
    }
    
  // Non-monotonic Y (Y-loop)
  hd_curve_parameters p3 (-3, 0, -2, 3, 2, 1, 3, 4);
  if (p3.is_valid_for_richards_curve())
    {
      printf("H&D validity failed: accepted non-monotonic Y\n");
      ok = false;
    }

  return ok;
}

bool
test_hd_sorting ()
{
  bool ok = true;
  // Decreasing X
  hd_curve_parameters p1 (5, 0, 4, 1, 1, 3, 0, 4);
  p1.sort_by_x();
  if (p1.minx != 0 || p1.maxx != 5 || p1.linear1x != 1 || p1.linear2x != 4)
    {
       printf("H&D sorting failed to reverse decreasing X\n");
       ok = false;
    }
  return ok;
}

bool
test_custom_tone_curve ()
{
  bool ok = true;
  // Test default points
  tone_curve c1 (tone_curve::tone_curve_custom);
  if (fabs (c1.apply (0.21764) - 0.46303) > 0.001)
    {
      printf ("Default custom tone curve mismatch at 0.21764: got %f, expected 0.46303\n", c1.apply (0.21764));
      ok = false;
    }

  // Test explicit points
  std::vector<point_t> cp = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
  tone_curve c2 (cp);
  for (float x = 0; x <= 1.0; x += 0.1)
    {
      if (fabs (c2.apply (x) - x) > 0.001)
	{
	  printf ("Linear custom tone curve mismatch at %f: got %f, expected %f\n", x, c2.apply (x), x);
	  ok = false;
	}
    }

  // Test non-linear interpolation
  std::vector<point_t> cp3 = {{0.0, 0.0}, {0.5, 0.25}, {1.0, 1.0}};
  tone_curve c3 (cp3);
  if (fabs (c3.apply (0.5) - 0.25) > 0.001)
    {
      printf ("Non-linear custom tone curve mismatch at 0.5: got %f, expected 0.25\n", c3.apply (0.5));
      ok = false;
    }

  return ok;
}

int
test_render_linearity ()
{
  render_parameters rparam;
  image_data img;
  if (!img.set_dimensions (65536, 1, true, false))
    return false;
  for (int i = 0; i < 65536; i++)
    img.put_rgb_pixel (i, 0, {(image_data::gray)i, (image_data::gray)i, (image_data::gray)i});
  bool ok = true;
  luminosity_t gammas[] = {-1, 1, 1.8, 2.2, 2.8};

  /* sRGB and linear gamma should be handled perfectly.
     Gammas about 1.5 are steep enough so initial segment has too large
     gradient for out_lookup_table_size, so only check larger values.  */
  int mins[] = {0, 0, 40, 220, 1000};
  for (unsigned gamma_idx = 0; gamma_idx < sizeof (gammas) / sizeof (luminosity_t); gamma_idx ++)
    {
      luminosity_t gamma = gammas[gamma_idx];
      rparam.gamma = gamma;
      rparam.output_gamma = gamma;
      rparam.output_profile = render_parameters::output_profile_original;
      render ren (img, rparam, 65535);
      if (!ren.precompute_all (PRECOMPUTE_IMAGE_LAYER, {1, 1, 1}, NULL))
	return false;
      for (int i = 0; i < 65535; i++)
	{
	  int r, g, b;
	  luminosity_t linear = apply_gamma (i / (luminosity_t)65535, gamma);

	  /* Gamma should be invertible.  */
	  int gg = (int)(invert_gamma (linear, gamma) * 65535 + 0.5);
	  if (gg != i)
	    {
	      printf ("Gamma is non-invertible at gamma %f: %i becomes %i\n", gamma, i, gg);
	      ok = false;
	    }
	  /* Check that linearization works as expected.  */
	  if (fabs (linear - ren.get_data_red ({(int)i, 0})) > 1.0/655350)
	    {
	      printf ("Bad linearization of %i: %f should be %f\n", i, linear, ren.get_data_red ({(int)i, 0}));
	      ok = false;
	    }
	  /* Now out_lookup_table is applied.  */
	  int_rgbdata out_int = ren.out_color.final_color ({ren.get_data_red ({(int)i,0}), ren.get_data_green ({(int)i,0}), ren.get_data_blue ({(int)i,0})});
	  r = out_int.red; g = out_int.green; b = out_int.blue;
	  if (i > mins[gamma_idx] && (r != i || g != i || b != i))
	    {
	      printf ("Render is non-linear at gamma %f linear: %i becomes %i %i %i (with table)\n",
		      gamma, i, r, g, b);
	      ok = false;
	    }
	  luminosity_t hr,hg,hb;
	  rgbdata out_hdr = ren.out_color.hdr_final_color ({ren.get_data_red ({(int)i,0}), ren.get_data_green ({(int)i,0}), ren.get_data_blue ({(int)i,0})});
	  hr = out_hdr.red; hg = out_hdr.green; hb = out_hdr.blue;
	  int rr = hr * 65535 + 0.5;
	  gg = hg * 65535 + 0.5;
	  int bb = hb * 65535 + 0.5;
	  if (rr != i || gg != i || bb != i)
	    {
	      printf ("Render is non-linear at gamma %f linear: %i becomes %i %i %i (with hdr)\n",
		      gamma, i, r, g, b);
	      ok = false;
	    }
	}
    }
  return ok;
}

struct test_params
{
  int x;
  bool
  operator== (const test_params &other) const
  {
    return x == other.x;
  }
};

std::atomic<int> get_new_calls;
std::atomic<int> get_new_fast_calls;

std::unique_ptr<int>
get_new_test (test_params &p, progress_info *)
{
  get_new_calls++;
  std::this_thread::sleep_for (std::chrono::milliseconds (100));
  return std::make_unique<int> (p.x * 2);
}

std::unique_ptr<int>
get_new_test_fast (test_params &p, progress_info *)
{
  get_new_fast_calls++;
  return std::make_unique<int> (p.x * 2);
}

bool
test_lru_cache_concurrency ()
{
  lru_cache<test_params, int, get_new_test, 10> cache ("test_cache");
  const int num_threads = 10;
  std::vector<std::thread> threads;
  std::vector<std::shared_ptr<int>> results (num_threads);
  test_params p = { 42 };
  get_new_calls = 0;

  for (int i = 0; i < num_threads; ++i)
    {
      threads.emplace_back ([&, i] () { results[i] = cache.get (p, NULL); });
    }

  for (auto &t : threads)
    t.join ();

  bool ok = true;
  if (get_new_calls != 1)
    {
      printf ("LRU concurrency test FAIL: get_new called %d times (expected 1)\n",
	      (int)get_new_calls);
      ok = false;
    }
  for (int i = 0; i < num_threads; ++i)
    {
      if (!results[i] || *results[i] != 84)
	{
	  printf ("LRU concurrency test FAIL: thread %d got wrong result\n", i);
	  ok = false;
	}
    }

  /* Verify true least-recently-used eviction.  The former comparison selected
     the newest free entry and therefore behaved as an MRU cache.  */
  lru_cache<test_params, int, get_new_test_fast, 2> eviction_cache (
      "test_eviction_cache");
  get_new_fast_calls = 0;
  test_params p1 = { 1 }, p2 = { 2 }, p3 = { 3 };
  bool hit = false;
  std::shared_ptr<int> value = eviction_cache.get (p1, nullptr, nullptr, &hit);
  if (!value || hit)
    ok = false;
  value.reset ();
  value = eviction_cache.get (p2, nullptr, nullptr, &hit);
  if (!value || hit)
    ok = false;
  value.reset ();
  value = eviction_cache.get (p1, nullptr, nullptr, &hit);
  if (!value || !hit)
    ok = false;
  value.reset ();
  value = eviction_cache.get (p3, nullptr, nullptr, &hit);
  if (!value || hit)
    ok = false;
  value.reset ();
  value = eviction_cache.get (p1, nullptr, nullptr, &hit);
  if (!value || !hit)
    ok = false;
  value.reset ();
  value = eviction_cache.get (p2, nullptr, nullptr, &hit);
  if (!value || hit || get_new_fast_calls != 4)
    {
      printf ("LRU eviction test FAIL: hit %i, builds %i\n", hit,
              (int)get_new_fast_calls);
      ok = false;
    }
  return ok;
}

/* test_spectrum_dyes_to_xyz performs unit tests for the spectrum_dyes_to_xyz class.  */
bool
test_spectrum_dyes_to_xyz ()
{
  spectrum_dyes_to_xyz dyes;
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5000);
  dyes.set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
  xyz wp;
  
  bool ok = true;
  // Test Illuminant A (Incandescent)
  {
    spectrum_dyes_to_xyz dyesA;
    dyesA.set_backlight (spectrum_dyes_to_xyz::il_A, 2856);
    wp = dyesA.whitepoint_xyz ();
    if (fabs (wp.x - 1.0985) > 0.001 || fabs (wp.z - 0.3558) > 0.001)
      {
	printf ("FAILED: Illuminant A whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant C (Average Daylight)
  {
    spectrum_dyes_to_xyz dyesC;
    dyesC.set_backlight (spectrum_dyes_to_xyz::il_C, 6774);
    wp = dyesC.whitepoint_xyz ();
    if (fabs (wp.x - 0.9807) > 0.001 || fabs (wp.z - 1.1822) > 0.001)
      {
	printf ("FAILED: Illuminant C whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant D65
  {
    spectrum_dyes_to_xyz dyesD65;
    dyesD65.set_backlight (spectrum_dyes_to_xyz::il_D, 6504);
    wp = dyesD65.whitepoint_xyz ();
    if (fabs (wp.x - 0.9505) > 0.002 || fabs (wp.z - 1.0891) > 0.002)
      {
	printf ("FAILED: Illuminant D65 whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant Equal Energy (E)
  {
    spectrum_dyes_to_xyz dyesE;
    dyesE.set_backlight (spectrum_dyes_to_xyz::il_equal_energy);
    wp = dyesE.whitepoint_xyz ();
    if (fabs (wp.x - 1.0) > 0.001 || fabs (wp.y - 1.0) > 0.001 || fabs (wp.z - 1.0) > 0.001)
      {
	printf ("FAILED: Illuminant E whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }
  if (!ok)
    return false;

  // Test normalization
  dyes.normalize_brightness ();
  xyz wp_norm = dyes.dyes_rgb_to_xyz (1, 1, 1);
  if (fabs (wp_norm.y - 1.0) > 0.0001)
    {
      printf ("FAILED: Normalized brightness Y should be 1.0, got %f\n", wp_norm.y);
      return false;
    }

  // Test that characteristic curve setting doesn't affect dyes_rgb_to_xyz linearity.
  // The characteristic curve is applied in film_rgb_response, not dyes_rgb_to_xyz.
  dyes.set_characteristic_curve (spectrum_dyes_to_xyz::input_curve);
  if (!dyes.is_linear ())
    {
       printf ("FAILED: dyes_rgb_to_xyz should remain linear even with characteristic curve set\n");
       return false;
    }

  // Test matrix generation
  color_matrix m = dyes.xyz_matrix ();
  if (fabs (m (3, 3) - 1.0) > 0.0001)
    {
       printf ("FAILED: Matrix diagonal should be 1.0, got %f\n", m (3, 3));
       return false;
    }

  return true;
}

/* test_whitepoint_constants verifies that the spectral path whitepoints computed for various
   standard illuminants match the hardcoded xyz constants in color.h.  */
bool
test_whitepoint_constants ()
{
  bool ok = true;
  spectrum_dyes_to_xyz dyes;
  xyz wp;
  
  auto compare_wp = [&] (const char *name, xyz spec_wp, xyz const_wp, luminosity_t eps = 0.002) {
    if (!spec_wp.almost_equal_p (const_wp, eps))
      {
	printf ("FAILED: %s whitepoint mismatch!\n", name);
	printf ("  Spectral: (%f, %f, %f)\n", spec_wp.x, spec_wp.y, spec_wp.z);
	printf ("  Constant: (%f, %f, %f)\n", const_wp.x, const_wp.y, const_wp.z);
	printf ("  Diff:     (%f, %f, %f)\n", spec_wp.x - const_wp.x, spec_wp.y - const_wp.y, spec_wp.z - const_wp.z);
	ok = false;
      }
  };

  // 1. Illuminant A (Incandescent) - 2856K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_A, 2856);
  compare_wp ("Illuminant A", dyes.whitepoint_xyz (), il_A_white);

  // 2. Illuminant B (Direct Sunlight) - 4874K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_B, 4874);
  compare_wp ("Illuminant B", dyes.whitepoint_xyz (), il_B_white);

  // 3. Illuminant C (Average Daylight) - 6774K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_C, 6774);
  compare_wp ("Illuminant C", dyes.whitepoint_xyz (), il_C_white);

  // 4. Illuminant D50 - 5003K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5003);
  compare_wp ("Illuminant D50", dyes.whitepoint_xyz (), d50_white);

  // 5. Illuminant D55 - 5503K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5503);
  compare_wp ("Illuminant D55", dyes.whitepoint_xyz (), d55_white);

  // 6. Illuminant D65 - 6504K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 6504);
  compare_wp ("Illuminant D65", dyes.whitepoint_xyz (), d65_white);

  // 7. sRGB Whitepoint (D65)
  // sRGB standard specifically uses D65.
  compare_wp ("sRGB/D65", dyes.whitepoint_xyz (), srgb_white);

  return ok;
}

/* Independently evaluate the radial-only DNG WarpRectilinear equations for P
   in a WIDTH by HEIGHT full image.  CENTER is the normalized DNG optical
   center and KR contains kr0 ... kr3.

   Keep this implementation separate from lens_warp_correction helpers.  It
   follows the radial equations plus the rectangle-bound convention used by
   Adobe DNG SDK dng_filter_warp, and provides a secondary check alongside the
   frozen output of the executed Adobe reference implementation.  */
point_t
dng_warp_rectilinear_reference (point_t p, int width, int height,
                                point_t center, const coord_t kr[4])
{
  const coord_t x0 = 0;
  const coord_t y0 = 0;
  const coord_t x1 = width;
  const coord_t y1 = height;
  const coord_t cx = x0 + center.x * (x1 - x0);
  const coord_t cy = y0 + center.y * (y1 - y0);
  const coord_t mx = std::max (my_fabs (x0 - cx), my_fabs (x1 - cx));
  const coord_t my = std::max (my_fabs (y0 - cy), my_fabs (y1 - cy));
  const coord_t m = my_sqrt (mx * mx + my * my);
  const coord_t dx = (p.x - cx) / m;
  const coord_t dy = (p.y - cy) / m;
  const coord_t rsq = std::min ((coord_t)1, dx * dx + dy * dy);
  const coord_t f
      = kr[0] + rsq * (kr[1] + rsq * (kr[2] + rsq * kr[3]));
  return { cx + m * f * dx, cy + m * f * dy };
}

/* Verify the implemented radial, single-coefficient-set subset of DNG
   WarpRectilinear.  In addition to the independent calculation above, this
   test contains coordinates emitted by Adobe DNG SDK 1.7.1 Build 2652.  */
bool
test_lens_warp ()
{
  bool ok = true;
  struct test_case {
    const char *name;
    coord_t kr[4];
    point_t center;
  } cases[] = {
    { "Synthetic Barrel", { 1.0, 0.05, 0.02, 0.01 }, { 0.5, 0.5 } },
    //{ "Nikon Coolscan 9000ED", { 0.99508, 0.0245411, -0.0521967, 0.0325757 }, { 0.560586, 0.482547 } },
    { "Near-identity DNG profile", { 0.999787, 0.000025, -0.000025, 0.000006 }, { 0.500029, 0.499863 } }
  };

  for (const auto& tc : cases)
    {
      lens_warp_correction_parameters p;
      for (int i = 0; i < 4; i++) p.kr[i] = tc.kr[i];
      p.center = tc.center;

      if (!p.is_monotone ())
        {
          printf ("FAILED: is_monotone should be true for %s!\n", tc.name);
          ok = false;
        }

      lens_warp_correction lw;
      lw.set_parameters (p);

      /* Use a 0..1000 square for the low-level radial helper.  */
      point_t img_center = { tc.center.x * 1000, tc.center.y * 1000 };
      if (!lw.precompute (img_center, {0, 0}, {1000, 0},
                          {1000, 1000}, {0, 1000}))
	return false;
      if (!lw.precompute_inverse ())
	return false;

      /* Verify center is fixed.  */
      point_t c_scan = lw.corrected_to_scan (img_center);
      if (!img_center.almost_eq (c_scan, 1e-6))
        {
          printf ("FAILED: %s center should be fixed point!\n", tc.name);
          ok = false;
        }

      /* Verify round-trip accuracy on a grid.  */
      for (int y = 0; y <= 1000; y += 250)
        for (int x = 0; x <= 1000; x += 250)
          {
            point_t orig = { (coord_t) x, (coord_t) y };
            point_t scan = lw.corrected_to_scan (orig);
            /* The inverse lookup is defined for source pixels in the image.
               Positive distortion may request a source position outside the
               source rectangle at an output corner; do not use such a point
               as an inverse-domain round-trip test.  */
            const coord_t max_dist
                = std::max ({img_center.dist_from ({0, 0}),
                             img_center.dist_from ({1000, 0}),
                             img_center.dist_from ({1000, 1000}),
                             img_center.dist_from ({0, 1000})});
            if (scan.dist_from (img_center) > max_dist)
              continue;
            point_t corrected = lw.scan_to_corrected (scan);
            if (!orig.almost_eq (corrected, 0.01))
              {
                printf ("FAILED: %s roundtrip mismatch at (%i,%i)\n", tc.name, x, y);
                ok = false;
              }
          }
    }

  /* Broken parameters: non-monotone.  */
  lens_warp_correction_parameters p2;
  p2.kr[0] = 1.0;
  p2.kr[1] = -1.0;
  p2.kr[2] = 0.0;
  p2.kr[3] = 0.0;
  /* Derivative is f(x) = 1 - 3x. For x > 1/3, f(x) < 0.  */
  if (p2.is_monotone ())
    {
      printf ("FAILED: is_monotone should be false for p2!\n");
      ok = false;
    }
  lens_warp_correction broken;
  broken.set_parameters (p2);
  if (broken.precompute ({500, 500}, {0, 0}, {1000, 0},
                         {1000, 1000}, {0, 1000}))
    {
      fprintf (stderr, "Non-monotone DNG radial warp was accepted\n");
      ok = false;
    }

  /* Synthetic direct evaluation of the DNG radial polynomial.  The DNG
     specification gives the equations but not this numerical example.
     Using rectangle endpoints 0..1000 gives center (500,500) and makes a
     corner define r=1.
     Parameters: kr = [1.0, 0.05, -0.02, 0.005]
     Point (600, 700) -> Delta (100, 200), r^2 = (100^2+200^2)/(500^2+500^2) = 0.1.
     Distortion ratio f(0.1) = 1.0 + 0.05(0.1) - 0.02(0.01) + 0.005(0.001) = 1.004805.
     Expected point: (500 + 100*1.004805, 500 + 200*1.004805) = (600.4805, 700.961).  */
  {
    lens_warp_correction_parameters p_ref;
    p_ref.kr[0] = 1.0; p_ref.kr[1] = 0.05; p_ref.kr[2] = -0.02; p_ref.kr[3] = 0.005;
    p_ref.center = { 0.5, 0.5 };
    lens_warp_correction lw_ref;
    lw_ref.set_parameters (p_ref);
    if (!lw_ref.precompute ({ 500, 500 }, { 0, 0 }, { 1000, 0 }, { 1000, 1000 }, { 0, 1000 }))
      return false;
    
    point_t p_in = { 600, 700 };
    point_t p_out = lw_ref.corrected_to_scan (p_in);
    point_t p_expected = { 600.4805, 700.9610 };
    if (!p_out.almost_eq (p_expected, 1e-4))
      {
        printf ("FAILED: synthetic DNG radial example mismatch!\n");
        printf ("Expected (%f, %f), got (%f, %f)\n", p_expected.x, p_expected.y, p_out.x, p_out.y);
        ok = false;
      }
  }

  /* Cross-implementation conformance fixture.  The EXPECTED values below were
     emitted by the actual Adobe DNG SDK 1.7.1 Build 2652 (2026-07-14), not
     calculated by Color-Screen.  The reference workflow downloaded Adobe's
     official dng_sdk_1_7_1_2652_20260714.zip, verified SHA-256
       73499b47f4683e12120a234bd0946f02e52ab2ff9834bcbd0e9f8ab4f923360e
     and verified dng_lens_correction.cpp SHA-256
       89112619dce4a205761dc9e3b0c641d6c1d99911829a18c181a7229af4e8521f.
     A wrapper compiled in Adobe's dng_lens_correction.cpp instantiated the
     real dng_filter_warp and called GetSrcPixelPosition().  See
     doc/lens-correction-review.md for the complete reproduction procedure.  */
  {
    struct adobe_dng_fixture
    {
      const char *name;
      int width, height;
      point_t center;
      coord_t kr[4];
      point_t output;
      point_t expected_source;
    };
    const adobe_dng_fixture fixtures[] = {
      { "classic", 1000, 1000, {0.5, 0.5},
        {1, 0.05, -0.02, 0.005}, {600, 700},
        {600.48050000000001, 700.96100000000001} },
      { "offcenter_tl", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {0, 0},
        {-0.98894095820949701, -2.0174429911056109} },
      { "offcenter_tr", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {1000, 0},
        {1013.8325672342966, -8.4398480753109197} },
      { "offcenter_bl", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {0, 700},
        {0.59777057668648581, 699.40196978270342} },
      { "offcenter_br", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {1000, 700},
        {1011.2046333360396, 703.35264195316779} },
      { "offcenter_mid1", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {123, 456},
        {123.78924343023824, 456.10061510483405} },
      { "offcenter_mid2", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {827, 51},
        {834.12407645072631, 46.002032462044667} },
      { "offcenter_mid3", 1001, 701, {0.23, 0.67},
        {0.992, 0.045, -0.028, 0.009}, {230, 469},
        {230.00183999362491, 469.00535998142914} },
      { "coolscan_tl", 4000, 3000, {0.560586, 0.482547},
        {0.99508, 0.0245411, -0.0521967, 0.0325757}, {0, 0},
        {1.5160594222929831, 0.97875695171978805} },
      { "coolscan_br", 4000, 3000, {0.560586, 0.482547},
        {0.99508, 0.0245411, -0.0521967, 0.0325757}, {3999, 2999},
        {3995.2350617838392, 2995.6750628546024} },
      { "coolscan_mid", 4000, 3000, {0.560586, 0.482547},
        {0.99508, 0.0245411, -0.0521967, 0.0325757}, {317, 2411},
        {320.87650466339437, 2409.0603644564176} },
      { "edge_ratio_below_one", 1000, 800, {0.41, 0.62},
        {0.9, 0, 0, 0}, {901, 87},
        {851.90000000000009, 127.89999999999998} }
    };

    for (const adobe_dng_fixture &fixture : fixtures)
      {
        scr_to_img_parameters p;
        p.lens_correction.center = fixture.center;
        for (int i = 0; i < 4; i++)
          p.lens_correction.kr[i] = fixture.kr[i];
        scr_to_img map;
        if (!map.set_parameters_for_early_correction (
                p, fixture.width, fixture.height))
          return false;
        const point_t actual
            = map.inverse_early_correction (fixture.output);
        const point_t equation = dng_warp_rectilinear_reference (
            fixture.output, fixture.width, fixture.height, fixture.center,
            fixture.kr);
        if (!actual.almost_eq (fixture.expected_source, 1e-9)
            || !equation.almost_eq (fixture.expected_source, 1e-9))
          {
            fprintf (stderr,
                     "Adobe DNG SDK fixture %s mismatch: expected "
                     "%.17g,%.17g got %.17g,%.17g; equation %.17g,%.17g\n",
                     fixture.name, fixture.expected_source.x,
                     fixture.expected_source.y, actual.x, actual.y, equation.x,
                     equation.y);
            ok = false;
          }
      }
  }

  /* The direct inverse used by lens_solver and the lookup-table inverse must
     have the same domain.  f(1)<1 requires a corrected radius greater than m
     to invert source pixels near radius m.  */
  {
    lens_warp_correction_parameters p;
    p.kr[0] = 0.9;
    lens_warp_correction lw;
    lw.set_parameters (p);
    if (!lw.precompute ({500, 500}, {0, 0}, {1000, 0},
                        {1000, 1000}, {0, 1000})
        || !lw.precompute_inverse ())
      return false;
    const point_t source_points[] = {{0, 0}, {1000, 0}, {125, 625}};
    for (point_t source : source_points)
      {
        point_t direct = lw.nonprecomputed_scan_to_corrected (source);
        point_t cached = lw.scan_to_corrected (source);
        if (!direct.almost_eq (cached, 0.002)
            || !lw.corrected_to_scan (direct).almost_eq (source, 0.001))
          {
            fprintf (stderr, "Direct and cached lens inverses disagree\n");
            ok = false;
          }
      }
  }

  /* The moving-lens extension removes the movement-axis coordinate before
     applying the radial model.  The optical center on that discarded axis is
     therefore zero in the reduced lens coordinate system.  */
  {
    scr_to_img_parameters p;
    p.scanner_type = lens_move_horizontally;
    p.lens_correction.center = {0.5, 0.61};
    p.lens_correction.kr[0] = 0.9;
    scr_to_img map;
    if (!map.set_parameters_for_early_correction (p, 1000, 600))
      return false;
    point_t center_line = {123, 0.61 * 600};
    if (!map.inverse_early_correction (center_line).almost_eq (center_line,
                                                               1e-10))
      {
        fprintf (stderr, "Moving-lens discarded axis has a spurious center\n");
        ok = false;
      }
  }

  /* Automatic lens fitting needs global coverage, not merely enough points.
     The local cloud has two remote outliers, which the central-90% span must
     ignore.  Exactly MIN_LENS_POINTS broadly distributed points must qualify.  */
  {
    solver_parameters local, global, horizontal, vertical;
    for (int i = 0; i < 98; i++)
      {
        coord_t x = 450 + (i % 10) * 5;
        coord_t y = 450 + (i / 10) * 5;
        local.add_point ({x, y}, {x, y}, solver_parameters::red);
      }
    local.add_point ({0, 0}, {0, 0}, solver_parameters::red);
    local.add_point ({999, 999}, {999, 999}, solver_parameters::red);

    for (int y = 0; y < 10; y++)
      for (int x = 0; x < 10; x++)
        {
          coord_t broad_x = (coord_t)(111 * x);
          coord_t broad_y = (coord_t)(111 * y);
          coord_t local_x = 450 + 5 * x;
          coord_t local_y = 450 + 5 * y;
          global.add_point ({broad_x, broad_y}, {broad_x, broad_y},
                            solver_parameters::red);
          horizontal.add_point ({local_x, broad_y}, {local_x, broad_y},
                                solver_parameters::red);
          vertical.add_point ({broad_x, local_y}, {broad_x, local_y},
                              solver_parameters::red);
        }

    if (local.lens_optimization_sufficient (Paget, 1000, 1000, fixed_lens)
        || !global.lens_optimization_sufficient (Paget, 1000, 1000,
                                                 fixed_lens)
        || !horizontal.lens_optimization_sufficient (
               Paget, 1000, 1000, lens_move_horizontally)
        || horizontal.lens_optimization_sufficient (
               Paget, 1000, 1000, lens_move_vertically)
        || !vertical.lens_optimization_sufficient (
               Paget, 1000, 1000, lens_move_vertically)
        || vertical.lens_optimization_sufficient (
               Paget, 1000, 1000, lens_move_horizontally))
      {
        fprintf (stderr, "Lens-solver spatial coverage gate is wrong\n");
        ok = false;
      }
  }

  /* The automatic-fit envelope is much narrower than DNG validity.  Current
     synthetic and Coolscan profiles are accepted after solver normalization,
     while a still-monotone extreme profile from the coefficient search box is
     rejected.  */
  {
    lens_warp_correction_parameters synthetic;
    synthetic.center = {0.4, 0.6};
    synthetic.kr[1] = 0.01;
    synthetic.kr[2] = 0.03;
    synthetic.kr[3] = 0.01;
    lens_warp_correction_parameters coolscan;
    coolscan.center = {0.560586, 0.482547};
    coolscan.kr[0] = 0.99508;
    coolscan.kr[1] = 0.0245411;
    coolscan.kr[2] = -0.0521967;
    coolscan.kr[3] = 0.0325757;
    lens_warp_correction_parameters extreme;
    extreme.center = {0.5, 0.5};
    extreme.kr[1] = -0.15;
    extreme.kr[2] = -0.05;
    extreme.kr[3] = -0.01;
    if (!synthetic.normalize () || !coolscan.normalize ()
        || !extreme.normalize ())
      return false;
    lens_warp_correction_parameters off_image = synthetic;
    off_image.center = {2.0, 0.5};
    lens_warp_correction_parameters centered_camera = synthetic;
    centered_camera.center = {0.25, 0.75};
    lens_warp_correction_parameters outside_centered_camera = synthetic;
    outside_centered_camera.center = {0.24, 0.75};
    lens_warp_correction_parameters discarded_axis = synthetic;
    discarded_axis.center = {100.0, 0.5};
    if (!solver_parameters::lens_candidate_reasonable_p (synthetic,
                                                          fixed_lens)
        || !solver_parameters::lens_candidate_reasonable_p (coolscan,
                                                             fixed_lens)
        || solver_parameters::lens_candidate_reasonable_p (extreme,
                                                            fixed_lens)
        /* Auto resolves to distance 2, i.e. center coordinates -0.5..1.5.  */
        || solver_parameters::lens_candidate_reasonable_p (off_image,
                                                            fixed_lens)
        /* Distance 4 permits -1.5..2.5 on each fitted axis.  */
        || !solver_parameters::lens_candidate_reasonable_p (off_image,
                                                             fixed_lens, 4)
        /* Distance 0.5 is the centered-camera preset: both fitted center
           coordinates must remain in the central half of the capture.  */
        || !solver_parameters::lens_candidate_reasonable_p (
               centered_camera, fixed_lens, 0.5)
        || solver_parameters::lens_candidate_reasonable_p (
               outside_centered_camera, fixed_lens, 0.5)
        /* Distance 1 means that every fitted center coordinate stays in the
           image.  */
        || solver_parameters::lens_candidate_reasonable_p (
               lens_warp_correction_parameters{off_image}, fixed_lens, 1)
        /* The movement-axis coordinate is deliberately discarded.  */
        || !solver_parameters::lens_candidate_reasonable_p (
               discarded_axis, lens_move_horizontally, 1)
        || solver_parameters::lens_candidate_reasonable_p (synthetic,
                                                            fixed_lens, -1))
      {
        fprintf (stderr, "Lens-solver deformation/center envelope is wrong\n");
        ok = false;
      }

    /* Solver configuration must round-trip, while old project files which do
       not contain the new keyword keep the automatic value zero.  */
    solver_parameters saved_solver;
    saved_solver.lens_center_distance = 3.25;
    FILE *project = tmpfile ();
    solver_parameters loaded_solver;
    const char *project_error = nullptr;
    const bool project_saved
        = project && save_csp (project, nullptr, nullptr, nullptr,
                               &saved_solver);
    const bool project_loaded
        = project_saved && !fseek (project, 0, SEEK_SET)
          && load_csp (project, nullptr, nullptr, nullptr, &loaded_solver,
                       &project_error);
    if (project)
      fclose (project);
    if (!project_loaded || project_error
        || loaded_solver.lens_center_distance != 3.25)
      {
        fprintf (stderr, "Lens-center distance project round trip failed%s%s\n",
                 project_error ? ": " : "",
                 project_error ? project_error : "");
        ok = false;
      }
    if (solver_parameters ().lens_center_distance != 0
        || solver_parameters::effective_lens_center_distance (0) != 2)
      {
        fprintf (stderr, "Automatic lens-center distance default changed\n");
        ok = false;
      }
  }

  /* Solver-only limits must not invalidate an existing profile.  With local
     points automatic fitting is disabled, so even a DNG-valid profile outside
     the automatic envelope remains installed while the homography is fitted.  */
  {
    image_data img;
    if (!img.set_dimensions (1000, 1000))
      return false;
    scr_to_img_parameters truth;
    truth.type = Paget;
    truth.scanner_type = fixed_lens;
    truth.lens_correction.center = {0.5, 0.5};
    truth.lens_correction.kr[1] = -0.15;
    truth.lens_correction.kr[2] = -0.05;
    truth.lens_correction.kr[3] = -0.01;
    if (!truth.lens_correction.normalize ())
      return false;
    scr_to_img map;
    if (!map.set_parameters (truth, img))
      return false;
    solver_parameters sparam;
    for (int y = 0; y < 10; y++)
      for (int x = 0; x < 10; x++)
        {
          point_t image = {(coord_t)(450 + 5 * x),
                           (coord_t)(450 + 5 * y)};
          sparam.add_point (image, map.to_scr (image), solver_parameters::red);
        }
    scr_to_img_parameters fitted;
    fitted.type = Paget;
    fitted.scanner_type = fixed_lens;
    fitted.lens_correction = truth.lens_correction;
    solver (&fitted, img, sparam);
    if (!(fitted.lens_correction == truth.lens_correction))
      {
        fprintf (stderr,
                 "Insufficient coverage modified an existing lens profile\n");
        ok = false;
      }
  }

  return ok;
}

/* Test the simulated photographic darkroom process.
   This verifies the symmetry of the 'apply' and 'unapply' functions
   in film_sensitivity, modeling the chain from scanned transmittance
   to print transmittance and back.  */
bool
test_darkroom ()
{
  bool ok = true;
  luminosity_t xs[] = { 1.5, 2.0, 2.5, 3.0, 3.5 };
  luminosity_t ys[] = { 0.1, 0.5, 1.5, 2.0, 2.2 };
  hd_curve paper (xs, ys, 5);

  /* Preflash=0.5, Exposure=100, Boost=1.2.  */
  film_sensitivity sens (&paper, 0.5, 100.0, 1.2);
  sens.precompute ();

  /* Test symmetry for values within the paper's dynamic range.  */
  luminosity_t test_vals[] = { 0.4, 0.6, 0.8 };
  for (luminosity_t v : test_vals)
    {
      luminosity_t t = sens.apply (v);
      luminosity_t v_inv = sens.unapply (t);
      if (fabs (v - v_inv) > 1e-4)
        {
          printf ("FAILED: Darkroom symmetry mismatch for V=%f! Expected %f, got %f\n",
                  v, v, v_inv);
          ok = false;
        }
    }

  /* Test that preflash affects the output (fog lifting).
     Preflash adds exposure, which increases density and DECREASES transmittance.  */
  film_sensitivity sens_no_pre (&paper, 0.0, 100.0, 1.2);
  sens_no_pre.precompute ();
  if (sens.apply (0.5) >= sens_no_pre.apply (0.5))
    {
      printf ("FAILED: Preflash should increase density (decrease transmittance)!\n");
      ok = false;
    }

  return ok;
}
static bool
test_get_src_range ()
{
  bool ok = true;
  /* Create a simple 2x2 cell mesh (3x3 points).  */
  int width = 3;
  int height = 3;
  coord_t xshift = 0;
  coord_t yshift = 0;
  coord_t xstep = 10.0;
  coord_t ystep = 10.0;

  std::unique_ptr<mesh> m (new mesh (xshift, yshift, xstep, ystep, width, height));
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      m->set_point ({(int64_t)x, (int64_t)y}, {(coord_t)(x * xstep), (coord_t)(y * ystep)});

  /* Input area in source coordinates: [5, 15] x [5, 15].
     This covers parts of all 4 cells.
     The corners of the area are at (5,5), (15,5), (5,15), (15,15).
     The target coordinates should be the same as source for this mesh.
     So result range should be [5, 15] x [5, 15].  */
  image_area area_in (5.0, 5.0, 10.0, 10.0);
  image_area result = m->get_src_range (area_in);

  if (fabs (result.x - 5.0) > 0.001 || fabs (result.y - 5.0) > 0.001
      || fabs (result.width - 10.0) > 0.001 || fabs (result.height - 10.0) > 0.001)
    {
      printf ("FAILED: get_src_range clipping failed: got [%f, %f] %fx%f, expected [5, 5] 10x10\n",
              result.x, result.y, result.width, result.height);
      ok = false;
    }

  /* Test with area entirely inside one cell.  */
  image_area area_in2 (2.0, 2.0, 1.0, 1.0);
  image_area result2 = m->get_src_range (area_in2);
  if (fabs (result2.x - 2.0) > 0.001 || fabs (result2.y - 2.0) > 0.001
      || fabs (result2.width - 1.0) > 0.001 || fabs (result2.height - 1.0) > 0.001)
    {
      printf ("FAILED: get_src_range inner clipping failed: got [%f, %f] %fx%f, expected [2, 2] 1x1\n",
              result2.x, result2.y, result2.width, result2.height);
      ok = false;
    }

  return ok;
}

static bool
test_mesh_inversion ()
{
  bool ok = true;
  /* Create a mesh with non-trivial warp.  */
  int width = 10;
  int height = 10;
  coord_t xshift = 0;
  coord_t yshift = 0;
  coord_t xstep = 10.0;
  coord_t ystep = 10.0;
  
  std::unique_ptr<mesh> m (new mesh(xshift, yshift, xstep, ystep, width, height));
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
         /* Add some non-linear distortion. */
         coord_t target_x = x * xstep + std::sin(y * 0.5) * 1.0;
         coord_t target_y = y * ystep + std::cos(x * 0.5) * 1.0;
         m->set_point({(int64_t)x, (int64_t)y}, {target_x, target_y});
      }
      
  /* Precompute inverse to use m->invert(ip).  */
  m->precompute_inverse();
  
  /* Get the cached inverse mesh covering the bounding box. */
  std::shared_ptr<mesh> inv_m = m->compute_inverse();
  
  /* Test inversion precision by verifying roundtrip. */
  for (int y = 1; y < height - 2; y++)
    for (int x = 1; x < width - 2; x++)
      {
         point_t src = {(coord_t)(x * xstep + xstep / 2.0), (coord_t)(y * ystep + ystep / 2.0)};
         point_t target = m->apply(src);
         point_t expected_src = m->invert(target);
         point_t recovered_src = inv_m->apply(target);
         
         if (src.dist_from(expected_src) > 0.05)
           {
             printf("FAILED: m->invert error too large at %f, %f: recovered %f, %f\n", 
                 src.x, src.y, expected_src.x, expected_src.y);
             ok = false;
           }

         if (src.dist_from(recovered_src) > 1.0)
           {
             printf("FAILED: inv_m->apply roundtrip error too large at %f, %f: recovered %f, %f\n", 
                 src.x, src.y, recovered_src.x, recovered_src.y);
             ok = false;
           }
      }
      
  /* Test optional area caching. */
  int_optional_image_area area;
  area.set = true;
  area.x = 20;
  area.y = 20;
  area.width = 40;
  area.height = 40;
  
  std::shared_ptr<mesh> inv_m_area = m->compute_inverse(area);
  
  point_t target = {40, 40};
  point_t recovered_src_area = inv_m_area->apply(target);
  point_t expected_src = m->invert(target);
  
  if (recovered_src_area.dist_from(expected_src) > 0.5)
    {
      printf("FAILED: Mesh inversion area mismatch at %f, %f: expected %f, %f, got %f, %f\n", 
          target.x, target.y, expected_src.x, expected_src.y, recovered_src_area.x, recovered_src_area.y);
      ok = false;
    }
  
  return ok;
}
bool
test_cow_points ()
{
  solver_parameters sp1;
  sp1.add_point ({1, 1}, {2, 2}, solver_parameters::red);

  solver_parameters sp2 = sp1;
  /* They should share the same data.  */
  if (sp1.points.raw_data () != sp2.points.raw_data ())
    {
      printf ("FAILED: sp1 and sp2 do not share points array after copy\n");
      return false;
    }

  /* Modifying sp2 should trigger COW.  */
  sp2.add_point ({3, 3}, {4, 4}, solver_parameters::blue);
  if (sp1.points.raw_data () == sp2.points.raw_data ())
    {
      printf ("FAILED: sp1 and sp2 still share points array after modification\n");
      return false;
    }

  if (sp1.n_points () != 1 || sp2.n_points () != 2)
    {
      printf ("FAILED: points count mismatch after COW\n");
      return false;
    }

  return true;
}
bool
test_image_area ()
{
  bool ok = true;
  /* Test int_image_area (exclusive).  */
  int_image_area ia_int (0, 0, 1, 1); // [0, 1) x [0, 1)
  if (ia_int.empty_p ())
    {
      printf ("FAILED: int_image_area incorrectly reported as empty\n");
      ok = false;
    }
  if (!ia_int.contains_p (int_point_t{0, 0}))
    {
      printf ("FAILED: int_image_area does not contain its origin\n");
      ok = false;
    }
  if (ia_int.contains_p (int_point_t{1, 0}))
    {
      printf ("FAILED: int_image_area incorrectly contains exclusive upper bound\n");
      ok = false;
    }

  /* Test image_area (inclusive).  */
  image_area ia_fp (0.0, 0.0, 1.0, 1.0); // [0, 1] x [0, 1]
  if (ia_fp.empty_p ())
    {
      printf ("FAILED: image_area incorrectly reported as empty\n");
      ok = false;
    }
  if (!ia_fp.contains_p (point_t{0.0, 0.0}))
    {
      printf ("FAILED: image_area does not contain its origin\n");
      ok = false;
    }
  if (!ia_fp.contains_p (point_t{1.0, 1.0}))
    {
      printf ("FAILED: image_area does not contain inclusive upper bound\n");
      ok = false;
    }

  /* Test conversion and rounding.  */
  image_area fp_area (0.1, 0.2, 0.9, 0.8); // [0.1, 1.0] x [0.2, 1.0]
  int_image_area int_area (fp_area);
  if (int_area.x != 0 || int_area.y != 0 || int_area.width != 2 || int_area.height != 2)
    {
      printf ("FAILED: conversion from image_area to int_image_area failed rounding requirements\n");
      ok = false;
    }

  return ok;
}

/* Return normalized grayscale pixel P from IMG.  The unnamed width and opaque
   callback parameters are unused by this image_data accessor.  */
static luminosity_t
get_test_gray (image_data *img, int_point_t p, int, void *)
{
  return (luminosity_t)img->get_pixel (p.x, p.y) / 65535.0f;
}

/* Verify channel-specific scanner sharpening, precompute flags and original
   RGB rendering.  */
static bool
test_channel_sharpening ()
{
  constexpr int width = 96;
  constexpr int height = 64;
  image_data img;
  if (!img.set_dimensions (width, height, true, false))
    return false;
  img.maxval = 65535;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      {
        const image_data::gray value = x < width / 2 ? 10000 : 50000;
        img.put_rgb_pixel (x, y, { value, value, value });
      }

  render_parameters params;
  params.gamma = 1;
  params.ignore_infrared = true;
  params.mix_dark = { 0, 0, 0 };
  params.mix_red = 0.2;
  params.mix_green = 0.3;
  params.mix_blue = 0.5;
  params.sharpen.mode = sharpen_parameters::wiener_deconvolution;
  params.sharpen.scanner_snr = 200;
  params.sharpen.supersample = 1;
  params.sharpen.scanner_mtf.wavelengths = { 650, 540, 470, 850 };

  const double contrast[3][7]
      = { { 100, 100, 100, 100, 100, 100, 100 },
          { 100, 96, 88, 72, 55, 42, 35 },
          { 100, 90, 72, 52, 36, 24, 18 } };
  for (int channel = 0; channel < 3; ++channel)
    {
      mtf_measurement measurement;
      measurement.channel = channel;
      measurement.wavelength = 600 - 70 * channel;
      measurement.same_capture = channel != 0;
      measurement.name
          = channel == 0 ? "red" : channel == 1 ? "green" : "blue";
      for (int i = 0; i < 7; ++i)
        measurement.add_value (i / 12.0, contrast[channel][i]);
      params.sharpen.scanner_mtf.measurements.push_back (
          std::move (measurement));
    }
  params.sharpen.scanner_mtf.measured_mtf_idx = 0;

  for (int channel = 0; channel < 3; ++channel)
    {
      const sharpen_parameters channel_sharpen
          = params.get_sharpen_parameters_for_channel (channel);
      if (channel_sharpen.scanner_mtf.measured_mtf_idx != channel
          || channel_sharpen.scanner_mtf.wavelength
                 != params.sharpen.scanner_mtf.wavelengths[channel])
        {
          fprintf (stderr,
                   "Scanner sharpening did not select channel %d metadata\n",
                   channel);
          return false;
        }
    }
  const sharpen_parameters ir_sharpen
      = params.get_sharpen_parameters_for_channel (3);
  if (ir_sharpen.scanner_mtf.measured_mtf_idx != -1
      || ir_sharpen.scanner_mtf.wavelength != 850)
    {
      fprintf (stderr,
               "Missing IR measurement did not fall back to the IR model\n");
      return false;
    }

  /* An explicitly selected image-layer measurement retains the historical
     behavior of supplying one transfer curve to all native channels.  */
  render_parameters image_layer_params = params;
  mtf_measurement image_layer_measurement;
  image_layer_measurement.channel = -1;
  image_layer_measurement.name = "image layer";
  for (int i = 0; i < 7; ++i)
    image_layer_measurement.add_value (i / 12.0, 100 - 8 * i);
  image_layer_params.sharpen.scanner_mtf.measurements.push_back (
      std::move (image_layer_measurement));
  const int image_layer_index
      = image_layer_params.sharpen.scanner_mtf.measurements.size () - 1;
  image_layer_params.sharpen.scanner_mtf.measured_mtf_idx
      = image_layer_index;
  if (image_layer_params.get_sharpen_parameters_for_channel (0)
              .scanner_mtf.measured_mtf_idx
          != image_layer_index
      || image_layer_params.get_sharpen_parameters_for_channel (2)
                 .scanner_mtf.measured_mtf_idx
             != image_layer_index)
    {
      fprintf (stderr,
               "Image-layer MTF no longer applies to every RGB channel\n");
      return false;
    }

  render r (img, params, 65535);
  if (!r.precompute_all (PRECOMPUTE_IMAGE_LAYER | PRECOMPUTE_RGB_IMAGE,
                         { 1, 1, 1 }, nullptr))
    {
      fprintf (stderr, "Per-channel sharpening precomputation failed\n");
      return false;
    }

  const int_point_t p = { width / 2 - 1, height / 2 };
  const double red = r.get_linearized_data_red (p);
  const double green = r.get_linearized_data_green (p);
  const double blue = r.get_linearized_data_blue (p);
  if (fabs (red - green) < 1e-5 && fabs (green - blue) < 1e-5)
    {
      fprintf (stderr,
               "Different same-capture channel MTFs produced identical RGB "
               "sharpening\n");
      return false;
    }

  const double expected_mix
      = red * params.mix_red + green * params.mix_green
        + blue * params.mix_blue;
  const double actual_mix = r.get_unadjusted_data (p);
  const double tolerance = 0.002 * std::max (1.0, fabs (expected_mix));
  if (fabs (actual_mix - expected_mix) > tolerance)
    {
      fprintf (stderr,
               "Image layer was not mixed from sharpened RGB channels: "
               "expected %.12g got %.12g\n",
               expected_mix, actual_mix);
      return false;
    }

  /* Original-capture rendering must request the RGB image rather than merely
     lookup tables, so it displays the same scanner-sharpened channels.  */
  scr_to_img_parameters map_parameters;
  map_parameters.type = Paget;
  map_parameters.center = { width / 2.0, height / 2.0 };
  map_parameters.coordinate1 = { 4, 0 };
  map_parameters.coordinate2 = { 0, 4 };
  render_img original (map_parameters, img, params, 65535);
  render_type_parameters render_type;
  render_type.type = render_type_original;
  original.set_render_type (render_type);
  if (!original.precompute_all (nullptr))
    {
      fprintf (stderr, "Original RGB renderer precomputation failed\n");
      return false;
    }
  const rgbdata original_pixel
      = original.sample_pixel_img ({ (coord_t)p.x, (coord_t)p.y });
  const rgbdata direct_pixel = r.get_rgb_pixel (p);
  if (fabs (original_pixel.red - direct_pixel.red) > tolerance
      || fabs (original_pixel.green - direct_pixel.green) > tolerance
      || fabs (original_pixel.blue - direct_pixel.blue) > tolerance)
    {
      fprintf (stderr,
               "Original renderer did not use scanner-sharpened RGB image\n");
      return false;
    }

  return true;
}

/* Verify the slanted-edge MTF estimator against realistic optical blurs
   generated at high resolution and subsequently integrated to sensor pixels.  */
static bool
test_slanted_edge_mtf ()
{
  bool verbose = false;
  if (verbose)
    printf("Testing slanted edge MTF (realistic anti-aliased model)...\n");
  for (int disp = 0 ; disp < 10; disp++)
    {
      int w = 128, h = 128;
      int scale = 16;
      int w_hi = w * scale, h_hi = h * scale;
      
      image_data img_hi;
      if (!img_hi.set_dimensions(w_hi, h_hi, false, true))
	return false;
      img_hi.maxval = 65535;
      
      double angle = 5.0 * M_PI / 180.0;
      double cos_a = std::cos(angle);
      double sin_a = std::sin(angle);
      for (int y = 0; y < h_hi; y++)
	for (int x = 0; x < w_hi; x++)
	  {
	    double d = (x - w_hi/2.0) * cos_a + (y - h_hi/2.0) * sin_a;
	    img_hi.put_pixel(x, y, d > 0 ? 10000 : 5000);
	  }
	  
      // Setup blur parameters for 16x resolution
      sharpen_parameters sp_hi;
      sp_hi.mode = sharpen_parameters::blur_deconvolution;
      sp_hi.scanner_mtf.f_stop = 8;
      sp_hi.scanner_mtf.scan_dpi = 4000; 
      sp_hi.scanner_mtf.defocus = 0.01 * disp; // 10 microns displacement
      sp_hi.scanner_mtf.pixel_pitch = 3.76;
      sp_hi.scanner_mtf.wavelength = 750; // IR lifht
      sp_hi.scanner_mtf_scale = scale;
      sp_hi.supersample = 1;

      std::vector<float> blurred_hi(w_hi * h_hi);
      
      if (!deconvolve<luminosity_t, float, image_data *, void *, get_test_gray, float>(
	    blurred_hi.data(), &img_hi, nullptr, w_hi, h_hi, sp_hi, nullptr, true))
	{
	  printf("Blurring failed\n");
	  return false;
	}

      image_data blurred;
      if (!blurred.set_dimensions(w, h, true, true))
	return false;
      blurred.maxval = 65535;
      
      // Downscale by averaging 16x16 blocks
      for (int y = 0; y < h; y++)
	for (int x = 0; x < w; x++)
	  {
	    double sum = 0;
	    for (int dy = 0; dy < scale; dy++)
	      for (int dx = 0; dx < scale; dx++)
		sum += blurred_hi[(y * scale + dy) * w_hi + (x * scale + dx)];
	    uint16_t val = (uint16_t)std::clamp(sum / (scale * scale) * 65535.0, 0.0, 65535.0);
	    blurred.put_pixel(x, y, val);
	    blurred.put_rgb_pixel(x, y, {val, val, val});
	  }
	
      // Analyze edge
      render_parameters rparam;
      rparam.gamma = 1.0;
      
      slanted_edge_parameters params;
      params.wavelength = 750;
      params.channel = 3;
      params.name = "synthetic infrared edge";
      slanted_edge_results res = slanted_edge_mtf(rparam, blurred, blurred.get_area(), params, nullptr);
      
      if (!res.success)
	{
	  printf("Slanted edge detection failed\n");
	  return false;
	}
	
      if (verbose)
        printf("Edge found: (%f,%f) - (%f,%f)\n", (double)res.edge_p1.x, (double)res.edge_p1.y, (double)res.edge_p2.x, (double)res.edge_p2.y);
      
      // Verify MTF
      if (!rparam.sharpen.scanner_mtf.measurements.empty())
	{
	  printf("Slanted-edge measurement unexpectedly modified render parameters\n");
	  return false;
	}
	
      auto &measurement = res.measurement;
      if (!measurement.size())
	{
	  printf("No MTF measurement generated\n");
	  return false;
	}
      if (measurement.wavelength != params.wavelength
          || measurement.channel != params.channel
          || measurement.name != params.name)
	{
	  printf ("Slanted-edge measurement metadata was not preserved\n");
	  return false;
	}
      if (verbose)
        printf("MTF size: %zu\n", measurement.size());
      
      if (measurement.size() < 10)
	{
	  printf("MTF too small\n");
	  return false;
	}

      bool has_uncertainty = false;
      for (size_t i = 0; i < measurement.size (); i++)
        {
          const double uncertainty = measurement.get_uncertainty (i);
          if (!my_isfinite (uncertainty) || uncertainty < 0)
            {
              printf ("Invalid slanted-edge uncertainty\n");
              return false;
            }
          has_uncertainty |= uncertainty > 0;
        }
      if (!has_uncertainty)
        {
          printf ("Slanted edge did not estimate MTF uncertainty\n");
          return false;
        }

      mtf m (sp_hi.scanner_mtf);
      if (! m.precompute (NULL))
	{
	  printf("MTF precomputation\n");
	  return false;
	}
	
      bool ok = true;
      for (size_t i = 0; i < measurement.size () && ok; i++)
	if (fabs (measurement.get_contrast (i) - m.get_mtf (measurement.get_freq (i)) * 100) > 5)
	  ok = false;
      if (!ok)
	{
	  for (size_t i = 0; i < measurement.size (); i++)
	     printf ("Freq %.3f %f.1%% should be %f.1%%, diff %f.1%%\n", measurement.get_freq (i),
		     measurement.get_contrast (i), m.get_mtf (measurement.get_freq (i)) * 100,
		     fabs (measurement.get_contrast (i) - m.get_mtf (measurement.get_freq (i)) * 100));
	  blurred.save_tiff("slanted_edge_test_realistic.tif");
	  printf("Saved test image to slanted_edge_test_realistic.tif\n");
	  return false;
	}
#if 0
      // Check some MTF values (should be in percentage 0..100)
      printf("Freq 0.1: %f%% should be %f%%, Freq 0.4: %f%% should be %f%%\n", 
	     (double)measurement.get_contrast(measurement.size() / 10), 
	     (double)m.get_mtf (measurement.get_freq (measurement.size() / 10)) * 100.0,
	     (double)measurement.get_contrast(4 * measurement.size() / 10),
	     (double)m.get_mtf (measurement.get_freq (4 * measurement.size() / 10)) * 100.0);
#endif
	     
      if (measurement.get_contrast(measurement.size() / 10) < measurement.get_contrast(4 * measurement.size() / 10))
	{
	  printf("MTF is not decreasing!\n");
	  return false;
	}

      if (measurement.get_contrast(1) < 50.0)
	{
	  printf("MTF values seem too low (expecting percentages 0..100)\n");
	  return false;
	}
    }
    
  /* Invalid ROIs must be rejected rather than converted into plausible but
     unrelated MTF curves.  DESCRIPTION identifies the adversarial geometry
     and PIXEL_VALUE supplies one deterministic 16-bit grayscale sample.  */
  auto expect_rejected
      = [] (const char *description, auto pixel_value)
        {
          constexpr int width = 192;
          constexpr int height = 192;
          image_data test_image;
          if (!test_image.set_dimensions (width, height, true, true))
            return false;
          test_image.maxval = 65535;
          for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
              {
                uint16_t value = pixel_value (x, y);
                test_image.put_pixel (x, y, value);
                test_image.put_rgb_pixel (x, y, {value, value, value});
              }

          render_parameters test_parameters;
          test_parameters.gamma = 1.0;
          slanted_edge_parameters edge_parameters;
          edge_parameters.wavelength = 750;
          slanted_edge_results result
              = slanted_edge_mtf (test_parameters, test_image,
                                  test_image.get_area (), edge_parameters,
                                  nullptr);
          if (result.success
              || result.failure == slanted_edge_failure_none
              || result.error.empty ()
              || !result.edge_histogram.empty ()
              || !test_parameters.sharpen.scanner_mtf.measurements.empty ())
            {
              printf ("FAILED: invalid slanted-edge ROI accepted: %s",
                      description);
              if (!result.error.empty ())
                printf (" (%s)", result.error.c_str ());
              printf ("\n");
              return false;
            }
          return true;
        };

  if (!expect_rejected (
          "edge parallel to the pixel grid",
          [] (int x, int) -> uint16_t
            { return x < 96 ? 10000 : 50000; })
      || !expect_rejected (
          "periodic stripe pattern with many competing edges",
          [] (int x, int) -> uint16_t
            { return ((x / 8) & 1) ? 50000 : 10000; })
      || !expect_rejected (
          "smooth illumination gradient without an isolated transition",
          [] (int x, int) -> uint16_t
            { return 5000 + (uint16_t)(50000.0 * x / 191.0); })
      || !expect_rejected (
          "two parallel slanted transitions",
          [] (int x, int y) -> uint16_t
            {
              double slope = std::tan (5.0 * M_PI / 180.0);
              double first_edge = 55.0 + slope * y;
              double second_edge = 120.0 + slope * y;
              return x >= first_edge && x < second_edge ? 50000 : 10000;
            })
      || !expect_rejected (
          "curved transition that cannot be represented by one line",
          [] (int x, int y) -> uint16_t
            {
              double edge_x
                  = 96.0 + 8.0 * std::sin (2.0 * M_PI * y / 64.0);
              return x < edge_x ? 10000 : 50000;
            })
      || !expect_rejected (
          "deterministic random texture without an edge",
          [] (int x, int y) -> uint16_t
            {
              unsigned int value
                  = (unsigned int)x * 0x9e3779b9U
                    ^ (unsigned int)y * 0x85ebca6bU;
              value ^= value >> 16;
              value *= 0x7feb352dU;
              value ^= value >> 15;
              return (uint16_t)(5000 + value % 55001);
            })
      || !expect_rejected (
          "low-contrast edge buried in deterministic noise",
          [] (int x, int y) -> uint16_t
            {
              unsigned int value
                  = (unsigned int)x * 0x9e3779b9U
                    ^ (unsigned int)y * 0x85ebca6bU;
              value ^= value >> 16;
              value *= 0x7feb352dU;
              value ^= value >> 15;
              int noise = (int)(value % 8001) - 4000;
              double edge_x
                  = 88.0 + std::tan (5.0 * M_PI / 180.0) * y;
              int sample = (x < edge_x ? 30000 : 32000) + noise;
              return (uint16_t)std::clamp (sample, 0, 65535);
            })
      || !expect_rejected (
          "slanted edge too close to an ROI boundary",
          [] (int x, int y) -> uint16_t
            {
              double edge_x = 4.5 + std::tan (5.0 * M_PI / 180.0) * y;
              return x < edge_x ? 10000 : 50000;
            }))
    return false;

  return true;
}

/* Verify that independent slanted edges from one real Phase One capture are
   accepted consistently and lead the physical MTF solver to the same basin.
   The ten ROIs sample horizontal and vertical edges at five field positions
   in a 2089 PPI, f/8, 750 nm capture with 3.760 um sensor pixels.  This test
   intentionally checks reproducibility statistics rather than exact fitted
   values: real field curvature and target variation are allowed, while a
   random ROI result or optimizer jump must fail.  */
static bool
test_real_mtf_reproducibility ()
{
  struct fit_result
  {
    double sigma;
    double defocus;
    double halo_fraction;
    double halo_sigma;
    double objective;
  };

  static constexpr const char *edge_files[] = {
    "ON_558_001_004_ISA-bottomlefth.tif",
    "ON_558_001_004_ISA-bottomleftv.tif",
    "ON_558_001_004_ISA-bottomrighth.tif",
    "ON_558_001_004_ISA-bottomrightv.tif",
    "ON_558_001_004_ISA-centerh.tif",
    "ON_558_001_004_ISA-centerv.tif",
    "ON_558_001_004_ISA-toplefth.tif",
    "ON_558_001_004_ISA-topleftv.tif",
    "ON_558_001_004_ISA-toprighth.tif",
    "ON_558_001_004_ISA-toprightv.tif"
  };

  /* Return the source-tree path of test file FILENAME.  Automake supplies
     TOP_SRCDIR during make check; the fallback is convenient when running the
     binary manually from build-qt/testsuite.  */
  auto test_path = [] (const char *filename)
    {
      const char *top_srcdir = getenv ("top_srcdir");
      if (!top_srcdir || !*top_srcdir)
        top_srcdir = "../..";
      return std::string (top_srcdir) + "/testsuite/" + filename;
    };

  /* Fit MEASUREMENT from START and store the physical-model result in OUT.
     The capture metadata is fixed; only residual sigma, defocus and the broad
     halo are allowed to move.  */
  auto fit_measurement
      = [] (const mtf_measurement &measurement, const fit_result &start,
            fit_result *out)
        {
          mtf_parameters input;
          input.model = mtf_model::physical_diffraction;
          input.scan_dpi = 2089;
          input.pixel_pitch = 3.760;
          input.f_stop = 8;
          input.wavelength = 750;
          input.sensor_fill_factor = 1;
          input.sigma = start.sigma;
          input.defocus = start.defocus;
          input.halo_fraction = start.halo_fraction;
          input.halo_sigma = start.halo_sigma;
          input.measurements = {measurement};

          mtf_estimation_options options;
          options.model = mtf_model::physical_diffraction;
          options.optimize_sigma = true;
          options.optimize_defocus = true;
          options.optimize_halo_fraction = true;
          options.optimize_halo_sigma = true;

          mtf_parameters result;
          const char *error = nullptr;
          const double objective = result.estimate_parameters (
              input, options, nullptr, nullptr, &error,
              mtf_parameters::estimate_use_nmsimplex
                  | mtf_parameters::estimate_use_multifit);
          if (error || objective < 0 || !my_isfinite (objective)
              || !my_isfinite (result.sigma)
              || !my_isfinite (result.defocus)
              || !my_isfinite (result.halo_fraction)
              || !my_isfinite (result.halo_sigma))
            {
              fprintf (stderr, "Real MTF fit failed%s%s\n",
                       error ? ": " : "", error ? error : "");
              return false;
            }
          *out = {result.sigma, result.defocus, result.halo_fraction,
                  result.halo_sigma, objective};
          return true;
        };

  /* Return the sample standard deviation of VALUES.  */
  auto standard_deviation = [] (const std::vector<double> &values)
    {
      double mean = 0;
      for (double value : values)
        mean += value;
      mean /= values.size ();
      double sum = 0;
      for (double value : values)
        {
          const double delta = value - mean;
          sum += delta * delta;
        }
      return std::sqrt (sum / (values.size () - 1));
    };

  const fit_result default_start = {0.65, 0.16, 0.12, 8.0, 0};
  std::vector<fit_result> fits;
  std::vector<mtf_measurement> measurements;
  fits.reserve (sizeof (edge_files) / sizeof (edge_files[0]));
  measurements.reserve (sizeof (edge_files) / sizeof (edge_files[0]));

  for (const char *filename : edge_files)
    {
      const std::string path = test_path (filename);
      image_data image;
      const char *error = nullptr;
      if (!image.load (path.c_str (), false, &error, nullptr))
        {
          fprintf (stderr, "Cannot load real slanted-edge fixture %s: %s\n",
                   path.c_str (), error ? error : "unknown error");
          return false;
        }

      render_parameters parameters;
      parameters.gamma = 1.0;
      slanted_edge_parameters edge_parameters;
      edge_parameters.wavelength = 750;
      edge_parameters.channel = 3;
      edge_parameters.name = filename;
      const slanted_edge_results edge
          = slanted_edge_mtf (parameters, image, image.get_area (),
                              edge_parameters, nullptr);
      if (!edge.success || !edge.measurement.size ()
          || !parameters.sharpen.scanner_mtf.measurements.empty ())
        {
          fprintf (stderr, "Real slanted edge %s was rejected: %s\n",
                   filename,
                   edge.error.empty () ? "no measurement produced"
                                       : edge.error.c_str ());
          return false;
        }
      if (edge.edge_angle < 4.5 || edge.edge_angle > 5.2
          || edge.edge_fit_rms > 0.25 || edge.edge_snr < 20
          || edge.phase_coverage < 0.95)
        {
          fprintf (stderr,
                   "Unexpected geometry/quality for %s: angle %.6g, RMS "
                   "%.6g, SNR %.6g, coverage %.6g\n",
                   filename, edge.edge_angle, edge.edge_fit_rms, edge.edge_snr,
                   edge.phase_coverage);
          return false;
        }

      const mtf_measurement &measurement = edge.measurement;
      bool has_uncertainty = false;
      for (size_t i = 0; i < measurement.size (); i++)
        if (measurement.get_uncertainty (i) > 0)
          has_uncertainty = true;
      if (!has_uncertainty)
        {
          fprintf (stderr, "Real slanted edge %s has no uncertainty data\n",
                   filename);
          return false;
        }

      fit_result fitted;
      if (!fit_measurement (measurement, default_start, &fitted))
        return false;
      if (fitted.sigma < 0.3 || fitted.sigma > 1.2
          || fitted.defocus < 0.1 || fitted.defocus > 0.35
          || fitted.halo_fraction < 0 || fitted.halo_fraction > 0.35
          || fitted.halo_sigma <= 0 || fitted.halo_sigma > 64)
        {
          fprintf (stderr,
                   "Unreasonable physical fit for %s: sigma %.6g, defocus "
                   "%.6g, halo %.6g at %.6g px\n",
                   filename, fitted.sigma, fitted.defocus,
                   fitted.halo_fraction, fitted.halo_sigma);
          return false;
        }
      measurements.push_back (measurement);
      fits.push_back (fitted);
    }

  std::vector<double> sigma_values;
  std::vector<double> defocus_values;
  std::vector<double> halo_fraction_values;
  std::vector<double> halo_sigma_values;
  for (const fit_result &fit : fits)
    {
      sigma_values.push_back (fit.sigma);
      defocus_values.push_back (fit.defocus);
      halo_fraction_values.push_back (fit.halo_fraction);
      halo_sigma_values.push_back (fit.halo_sigma);
    }

  const double sigma_sd = standard_deviation (sigma_values);
  const double defocus_sd = standard_deviation (defocus_values);
  const double halo_fraction_sd = standard_deviation (halo_fraction_values);
  const double halo_sigma_sd = standard_deviation (halo_sigma_values);
  if (sigma_sd > 0.12 || defocus_sd > 0.04 || halo_fraction_sd > 0.03
      || halo_sigma_sd > 8.0)
    {
      fprintf (stderr,
               "Real-edge physical fits are not reproducible: sigma SD %.6g, "
               "defocus SD %.6g, halo-fraction SD %.6g, halo-radius SD %.6g\n",
               sigma_sd, defocus_sd, halo_fraction_sd, halo_sigma_sd);
      return false;
    }

  /* Files are ordered as horizontal/vertical pairs at the same field
     position.  Defocus and halo energy should normally agree more closely
     within a pair than the compact residual sigma, which can absorb modest
     directional blur.  One bottom-right pair is intentionally given a wider
     defocus allowance because the real capture shows measurable field/orientation
     variation there.  */
  std::vector<double> paired_defocus_differences;
  for (size_t i = 0; i < fits.size (); i += 2)
    {
      const double defocus_difference
          = std::abs (fits[i].defocus - fits[i + 1].defocus);
      const double halo_difference
          = std::abs (fits[i].halo_fraction - fits[i + 1].halo_fraction);
      paired_defocus_differences.push_back (defocus_difference);
      if (defocus_difference > 0.10 || halo_difference > 0.02)
        {
          fprintf (stderr,
                   "Horizontal/vertical fit disagreement for %s/%s: defocus "
                   "%.6g, halo fraction %.6g\n",
                   edge_files[i], edge_files[i + 1], defocus_difference,
                   halo_difference);
          return false;
        }
    }
  std::sort (paired_defocus_differences.begin (),
             paired_defocus_differences.end ());
  if (paired_defocus_differences[paired_defocus_differences.size () / 2]
      > 0.02)
    {
      fprintf (stderr,
               "Median horizontal/vertical defocus disagreement is %.6g mm\n",
               paired_defocus_differences[paired_defocus_differences.size ()
                                          / 2]);
      return false;
    }

  /* Refit one representative real curve from deliberately different starting
     points.  Solver convergence should be much tighter than the genuine
     edge-to-edge variation above.  */
  static constexpr fit_result alternate_starts[] = {
    {0.25, 0.05, 0.03, 4.0, 0},
    {1.10, 0.45, 0.30, 30.0, 0}
  };
  const size_t representative = 4; /* Center horizontal edge.  */
  for (const fit_result &start : alternate_starts)
    {
      fit_result refit;
      if (!fit_measurement (measurements[representative], start, &refit))
        return false;
      const fit_result &reference = fits[representative];
      if (std::abs (refit.sigma - reference.sigma) > 0.01
          || std::abs (refit.defocus - reference.defocus) > 0.005
          || std::abs (refit.halo_fraction - reference.halo_fraction) > 0.005
          || std::abs (refit.halo_sigma - reference.halo_sigma) > 0.2)
        {
          fprintf (stderr,
                   "Real MTF solver depends on its initial guess: sigma "
                   "%.6g/%.6g, defocus %.6g/%.6g, halo %.6g/%.6g, radius "
                   "%.6g/%.6g\n",
                   refit.sigma, reference.sigma, refit.defocus,
                   reference.defocus, refit.halo_fraction,
                   reference.halo_fraction, refit.halo_sigma,
                   reference.halo_sigma);
          return false;
        }
    }

  /* The circles fixture deliberately contains several curved transitions and
     is not a valid slanted-edge ROI.  It must be rejected rather than yielding
     a random MTF curve.  */
  {
    const std::string path = test_path ("ON_558_001_004_ISA-circles.tif");
    image_data image;
    const char *error = nullptr;
    if (!image.load (path.c_str (), false, &error, nullptr))
      {
        fprintf (stderr, "Cannot load real circles fixture: %s\n",
                 error ? error : "unknown error");
        return false;
      }
    render_parameters parameters;
    parameters.gamma = 1.0;
    slanted_edge_parameters edge_parameters;
    edge_parameters.wavelength = 750;
    const slanted_edge_results edge
        = slanted_edge_mtf (parameters, image, image.get_area (),
                            edge_parameters, nullptr);
    if (edge.success || edge.failure == slanted_edge_failure_none
        || !parameters.sharpen.scanner_mtf.measurements.empty ())
      {
        fprintf (stderr,
                 "Real circles fixture was incorrectly accepted as one edge\n");
        return false;
      }
  }

  return true;
}

static bool
test_denoise ()
{
  const int width = 64;
  const int height = 64;
  std::vector<float> original (width * height);
  std::vector<float> noisy (width * height);

  unsigned int seed = 123;
  /* Create a gradient with zero-mean additive noise.  */
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        float val = (float)x / width;
        original[y * width + x] = val;
        float noise
            = ((int)(fast_rand16 (&seed) % 201) - 100) / 2000.0f;
        noisy[y * width + x] = val + noise;
      }

  auto get_float_pixel = [] (const std::vector<float> &data, int_point_t p,
                             int width, void *) -> float
  { return data[p.y * width + p.x]; };

  denoise_parameters::denoise_mode modes[] = {
    denoise_parameters::bilateral,
    denoise_parameters::nl_means,
    denoise_parameters::nl_fast
  };

  /* Selecting NL-means should enable it with useful defaults.  */
  {
    denoise_parameters params;
    params.mode = denoise_parameters::nl_fast;
    if (params.get_mode () != denoise_parameters::nl_fast)
      {
        fprintf (stderr, "Default NL-means parameters disable denoising\n");
        return false;
      }
    params.search_radius = 0;
    if (params.get_mode () != denoise_parameters::none)
      {
        fprintf (stderr, "Invalid NL-means search radius was accepted\n");
        return false;
      }
  }

  /* NL-means noise normalization is opt-in.  With a zero variance floor the
     historical raw squared-distance metric must be exact.  Once enabled, the
     same absolute difference is less surprising at higher signal when the
     variance slope predicts more noise there.  */
  {
    denoise_parameters params;
    params.mode = denoise_parameters::nl_means;
    params.strength = 1.0f;
    const float raw = (0.2f - 0.1f) * (0.2f - 0.1f);
    params.noise_variance_floor = 0;
    params.noise_variance_slope = 0.1f; /* Inactive without a floor.  */
    if (denoise_nl_square_distance (0.1f, 0.2f, params) != raw)
      {
        fprintf (stderr, "Inactive noise model changed NL-means distance\n");
        return false;
      }

    params.noise_variance_floor = 0.01f;
    const float low = denoise_nl_square_distance (0.1f, 0.2f, params);
    const float high = denoise_nl_square_distance (0.7f, 0.8f, params);
    if (fabsf (low - raw / 0.05f) > 2e-6f
        || fabsf (high - raw / 0.17f) > 2e-6f || !(high < low))
      {
        fprintf (stderr,
                 "Signal-dependent noise normalization is wrong: low %g high %g\n",
                 low, high);
        return false;
      }

    params.noise_variance_floor = -0.01f;
    if (params.get_mode () != denoise_parameters::none)
      {
        fprintf (stderr, "Negative noise variance floor was accepted\n");
        return false;
      }
  }

  /* Estimate a floor-plus-slope noise model from second differences.  The
     underlying test image is a linear gradient, so its deterministic signal
     cancels exactly.  Weak-support outliers must not bias the estimate.  */
  {
    const int w = 96, h = 72;
    const luminosity_t expected_floor = (luminosity_t)0.0004;
    const luminosity_t expected_slope = (luminosity_t)0.0012;
    std::vector<luminosity_t> data ((size_t)w * h);
    std::vector<luminosity_t> support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x51c0ffeeU;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          luminosity_t signal = (luminosity_t)0.08
              + (luminosity_t)0.72 * x / (w - 1)
              + (luminosity_t)0.04 * y / (h - 1);
          luminosity_t variance = expected_floor + expected_slope * signal;
          data[(size_t)y * w + x] = signal + std::sqrt (variance) * gaussian ();
          if ((x + 17 * y) % 113 == 0)
            {
              support[(size_t)y * w + x] = 0;
              data[(size_t)y * w + x] += 5;
            }
        }
    auto identity_to_scr = [] (int_point_t p) -> point_t
      { return {(coord_t)p.x, (coord_t)p.y}; };
    denoise_noise_estimate estimate = estimate_screen_noise_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        identity_to_scr);
    if (!estimate.valid_p ()
        || fabs (estimate.variance_floor - expected_floor) > expected_floor * 0.45
        || fabs (estimate.variance_slope - expected_slope) > expected_slope * 0.45
        || estimate.support_threshold < (luminosity_t)0.45
        || estimate.support_threshold > (luminosity_t)0.55)
      {
        fprintf (stderr,
                 "Noise estimator mismatch: floor %g slope %g support %g "
                 "observations %zu bins %d error %g\n",
                 (double)estimate.variance_floor,
                 (double)estimate.variance_slope,
                 (double)estimate.support_threshold, estimate.observations,
                 estimate.bins, (double)estimate.relative_fit_error);
        return false;
      }

    denoise_noise_scale_estimate scales = estimate_screen_noise_scale_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        identity_to_scr);
    if (!scales.valid_p ()
        || scales.spacing2_variance_ratio < (luminosity_t)0.55
        || scales.spacing2_variance_ratio > (luminosity_t)1.8)
      {
        fprintf (stderr,
                 "Two-scale noise estimator mismatch: ratio %g paired %zu "
                 "near error %g far error %g\n",
                 (double)scales.spacing2_variance_ratio,
                 scales.paired_observations,
                 (double)scales.spacing1.relative_fit_error,
                 (double)scales.spacing2.relative_fit_error);
        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            identity_to_scr);
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.5
        || three.spacing2_variance_ratio > (luminosity_t)2
        || three.spacing3_variance_ratio < (luminosity_t)0.4
        || three.spacing3_variance_ratio > (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "Three-scale noise estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }
  }

  /* Precise-RGB estimation pools the three scanner components but must
     recover the same shared model used by vector NL-means.  */
  {
    const int w = 80, h = 60;
    const luminosity_t expected_floor = (luminosity_t)0.0003;
    const luminosity_t expected_slope = (luminosity_t)0.0009;
    std::vector<rgbdata> data ((size_t)w * h);
    std::vector<luminosity_t> support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x2468ace0U;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          rgbdata signal = {
            (luminosity_t)0.08 + (luminosity_t)0.65 * x / (w - 1),
            (luminosity_t)0.15 + (luminosity_t)0.55 * y / (h - 1),
            (luminosity_t)0.12
                + (luminosity_t)0.35 * x / (w - 1)
                + (luminosity_t)0.25 * y / (h - 1)};
          rgbdata v;
          for (int k = 0; k < 3; k++)
            {
              luminosity_t variance
                  = expected_floor + expected_slope * signal[k];
              v[k] = signal[k] + std::sqrt (variance) * gaussian ();
            }
          data[(size_t)y * w + x] = v;
          if ((11 * x + 7 * y) % 149 == 0)
            {
              support[(size_t)y * w + x] = 0;
              data[(size_t)y * w + x] += rgbdata{4, 3, 5};
            }
        }
    denoise_noise_estimate estimate = estimate_screen_rgb_noise_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        [] (int_point_t p) { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!estimate.valid_p ()
        || fabs (estimate.variance_floor - expected_floor) > expected_floor * 0.5
        || fabs (estimate.variance_slope - expected_slope) > expected_slope * 0.5)
      {
        fprintf (stderr,
                 "RGB noise estimator mismatch: floor %g slope %g obs %zu "
                 "error %g\n",
                 (double)estimate.variance_floor,
                 (double)estimate.variance_slope, estimate.observations,
                 (double)estimate.relative_fit_error);
        return false;
      }

    denoise_noise_scale_estimate scales
        = estimate_screen_rgb_noise_scale_model (
            w, h,
            [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!scales.valid_p ()
        || scales.spacing2_variance_ratio < (luminosity_t)0.55
        || scales.spacing2_variance_ratio > (luminosity_t)1.8)
      {
        fprintf (stderr,
                 "RGB two-scale estimator mismatch: ratio %g paired %zu\n",
                 (double)scales.spacing2_variance_ratio,
                 scales.paired_observations);
        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_rgb_noise_three_scale_model (
            w, h,
            [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.5
        || three.spacing2_variance_ratio > (luminosity_t)2
        || three.spacing3_variance_ratio < (luminosity_t)0.4
        || three.spacing3_variance_ratio > (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "RGB three-scale estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }

    denoise_noise_domain_comparison domains
        = estimate_screen_rgb_noise_domain_comparison (
            w, h,
            [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!domains.valid_p () || !domains.density_valid_p ()
        || !domains.variance_stabilized_valid_p ())
      {
        fprintf (stderr,
                 "RGB domain comparison invalid: linear %d density %d VST %d\n",
                 (int)domains.valid_p (), (int)domains.density_valid_p (),
                 (int)domains.variance_stabilized_valid_p ());
        return false;
      }
  }

  /* Packed screen geometries must only use equally spaced physical triples.
     In particular Paget red rows are horizontally regular but their vertical
     array columns zig-zag by half a screen coordinate.  */
  {
    const int w = 80, h = 64;
    std::vector<luminosity_t> data ((size_t)w * h), support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x13572468U;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          point_t p = paget_geometry::red_entry_to_scr ({x, y});
          luminosity_t signal = (luminosity_t)0.2
              + (luminosity_t)0.004 * p.x + (luminosity_t)0.002 * p.y;
          luminosity_t variance = (luminosity_t)0.0007;
          data[(size_t)y * w + x] = signal + std::sqrt (variance) * gaussian ();
        }
    denoise_noise_estimate estimate = estimate_screen_noise_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        [] (int_point_t p) { return paget_geometry::red_entry_to_scr (p); });
    if (!estimate.valid_p ()
        || fabs (estimate.variance_floor - (luminosity_t)0.0007)
               > (luminosity_t)0.00035)
      {
        fprintf (stderr,
                 "Paget noise estimator mismatch: floor %g slope %g obs %zu\n",
                 (double)estimate.variance_floor,
                 (double)estimate.variance_slope, estimate.observations);
        return false;
      }

    denoise_noise_scale_estimate scales = estimate_screen_noise_scale_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        [] (int_point_t p) { return paget_geometry::red_entry_to_scr (p); });
    if (!scales.valid_p ()
        || scales.spacing2_variance_ratio < (luminosity_t)0.4
        || scales.spacing2_variance_ratio > (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "Paget two-scale estimator mismatch: ratio %g paired %zu\n",
                 (double)scales.spacing2_variance_ratio,
                 scales.paired_observations);
        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p) { return paget_geometry::red_entry_to_scr (p); });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.35
        || three.spacing2_variance_ratio > (luminosity_t)2.7
        || three.spacing3_variance_ratio < (luminosity_t)0.25
        || three.spacing3_variance_ratio > (luminosity_t)3.5)
      {
        fprintf (stderr,
                 "Paget three-scale estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }
  }

  /* A smooth quadratic signal is deliberately not noise.  Its second
     difference grows with spacing squared, so the paired scale diagnostic
     must expose a variance ratio well above the noise-only value of one.  */
  {
    const int w = 56, h = 48;
    const luminosity_t curvature = (luminosity_t)0.0005;
    const luminosity_t noise_variance = (luminosity_t)0.0000001;
    std::vector<luminosity_t> data ((size_t)w * h), support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x10293847U;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          luminosity_t dx = x - (w - 1) * (luminosity_t)0.5;
          luminosity_t dy = y - (h - 1) * (luminosity_t)0.5;
          luminosity_t signal
              = (luminosity_t)0.2 + curvature * (dx * dx + dy * dy);
          data[(size_t)y * w + x]
              = signal + std::sqrt (noise_variance) * gaussian ();
        }
    denoise_noise_scale_estimate scales = estimate_screen_noise_scale_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y) { return support[(size_t)y * w + x]; },
        [] (int_point_t p) { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!scales.valid_p ()
        || scales.spacing2_variance_ratio < (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "Two-scale estimator failed to detect curvature: ratio %g "
                 "paired %zu\n",
                 (double)scales.spacing2_variance_ratio,
                 scales.paired_observations);
        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)2.5
        || three.spacing3_variance_ratio < (luminosity_t)8
        || !three.scale_fit_valid_p ()
        || three.scale_growth_exponent < (luminosity_t)2.5
        || three.scale_growth_exponent > (luminosity_t)5.5
        || three.extrapolated_scale_invariant_fraction > (luminosity_t)0.5)
      {
        fprintf (stderr,
                 "Three-scale curvature fit mismatch: ratios %g %g exponent %g "
                 "noise fraction %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 (double)three.scale_growth_exponent,
                 (double)three.extrapolated_scale_invariant_fraction,
                 three.paired_observations);
        return false;
      }
  }


  /* A multiplicative/log-normal noise process on an exponential transmission
     field is intentionally awkward in linear intensity but becomes an
     additive, nearly homoscedastic process on a locally linear density field.
     The domain comparison should expose that improvement rather than merely
     producing three mathematically valid transforms.  */
  {
    const int w = 96, h = 72;
    std::vector<luminosity_t> data ((size_t)w * h), support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x17d0a11U;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          luminosity_t log_signal = (luminosity_t)-2.2
              + (luminosity_t)0.018 * x + (luminosity_t)0.006 * y;
          luminosity_t signal = std::exp (log_signal);
          data[(size_t)y * w + x]
              = signal * std::exp ((luminosity_t)0.075 * gaussian ());
        }
    denoise_noise_domain_comparison domains
        = estimate_screen_noise_domain_comparison (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t q)
            { return point_t{(coord_t)q.x, (coord_t)q.y}; });
    if (!domains.valid_p () || !domains.density_valid_p ()
        || domains.density.spacing2_variance_ratio < (luminosity_t)0.7
        || domains.density.spacing2_variance_ratio > (luminosity_t)1.4
        || domains.density.spacing3_variance_ratio < (luminosity_t)0.65
        || domains.density.spacing3_variance_ratio > (luminosity_t)1.5
        || domains.density.spacing1.relative_fit_error
               >= domains.linear.spacing1.relative_fit_error * (luminosity_t)0.5)
      {
        fprintf (stderr,
                 "Density-domain diagnostic mismatch: linear ratios %g %g "
                 "error %g; density ratios %g %g error %g\n",
                 (double)domains.linear.spacing2_variance_ratio,
                 (double)domains.linear.spacing3_variance_ratio,
                 (double)domains.linear.spacing1.relative_fit_error,
                 (double)domains.density.spacing2_variance_ratio,
                 (double)domains.density.spacing3_variance_ratio,
                 (double)domains.density.spacing1.relative_fit_error);
        return false;
      }
  }

  /* The generalized square-root transform is derived from the same
     floor-plus-slope variance law used by normalized NLM.  On synthetic data
     generated from that law it should substantially reduce the relative
     signal dependence of the spacing-1 variance fit.  */
  {
    const int w = 96, h = 72;
    const luminosity_t floor = (luminosity_t)0.00025;
    const luminosity_t slope = (luminosity_t)0.0020;
    std::vector<luminosity_t> data ((size_t)w * h), support ((size_t)w * h, 1);
    unsigned int noise_seed = 0x57ab1e42U;
    auto gaussian = [&] () -> luminosity_t
    {
      luminosity_t u1
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      luminosity_t u2
          = ((luminosity_t)fast_rand16 (&noise_seed) + 1) / (luminosity_t)32768;
      return std::sqrt ((luminosity_t)-2 * std::log (u1))
             * std::cos ((luminosity_t)6.2831853071795864769 * u2);
    };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          luminosity_t signal = (luminosity_t)0.08
              + (luminosity_t)0.68 * x / (w - 1)
              + (luminosity_t)0.03 * y / (h - 1);
          luminosity_t variance = floor + slope * signal;
          data[(size_t)y * w + x]
              = signal + std::sqrt (variance) * gaussian ();
        }
    denoise_noise_domain_comparison domains
        = estimate_screen_noise_domain_comparison (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t q)
            { return point_t{(coord_t)q.x, (coord_t)q.y}; });
    const luminosity_t linear_relative_slope
        = domains.linear.spacing1.variance_slope
          / domains.linear.spacing1.variance_floor;
    const luminosity_t vst_relative_slope
        = domains.variance_stabilized.spacing1.variance_slope
          / domains.variance_stabilized.spacing1.variance_floor;
    if (!domains.valid_p () || !domains.variance_stabilized_valid_p ()
        || !(linear_relative_slope > (luminosity_t)1)
        || vst_relative_slope > linear_relative_slope * (luminosity_t)0.25
        || vst_relative_slope > (luminosity_t)0.5)
      {
        fprintf (stderr,
                 "Variance-stabilized diagnostic mismatch: linear floor %g "
                 "slope %g rel %g; VST floor %g slope %g rel %g\n",
                 (double)domains.linear.spacing1.variance_floor,
                 (double)domains.linear.spacing1.variance_slope,
                 (double)linear_relative_slope,
                 (double)domains.variance_stabilized.spacing1.variance_floor,
                 (double)domains.variance_stabilized.spacing1.variance_slope,
                 (double)vst_relative_slope);
        return false;
      }
  }

  /* A bilateral filter needs a border matching its spatial kernel, not the
     unrelated NL-means patch and search radii.  Sigma 2 has support radius 6
     with the three-sigma truncation used by process_bilateral().  */
  {
    denoise_parameters params;
    params.mode = denoise_parameters::bilateral;
    params.bilateral_sigma_s = 2.0f;
    params.bilateral_sigma_r = 0.1f;
    params.patch_radius = 1;
    params.search_radius = 3;
    denoising<float> bilateral (params, 1);
    if (bilateral.get_border_size () != 6)
      {
        fprintf (stderr,
                 "Bilateral denoising border is %i instead of kernel radius 6\n",
                 bilateral.get_border_size ());
        return false;
      }
  }

  for (auto mode : modes)
    {
      std::vector<float> denoised (width * height);
      denoise_parameters params;
      params.mode = mode;
      params.strength = 0.1f;
      params.patch_radius = 1;
      params.search_radius = 3;
      params.bilateral_sigma_s = 2.0f;
      params.bilateral_sigma_r = 0.1f;

      if (!denoise<float, float, const std::vector<float> &, void *,
                   get_float_pixel, float> (denoised.data (), noisy, NULL,
                                             width, height, params, NULL,
                                             false))
        return false;

      /* Calculate MSE for noisy and denoised images.  */
      double mse_noisy = 0;
      double mse_denoised = 0;
      for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
          {
            double diff_noisy
                = noisy[y * width + x] - original[y * width + x];
            double diff_denoised
                = denoised[y * width + x] - original[y * width + x];
            mse_noisy += diff_noisy * diff_noisy;
            mse_denoised += diff_denoised * diff_denoised;
          }

      if (mse_denoised >= mse_noisy)
        {
          fprintf (stderr,
                   "Denoising mode %i failed to reduce MSE: noisy %f, denoised %f\n",
                   (int)mode, mse_noisy, mse_denoised);
          return false;
        }
    }

  /* The reference and integral-image NL-means implementations must implement
     the same patch-distance normalization.  */
  {
    std::vector<float> reference (width * height), fast (width * height);
    denoise_parameters params;
    params.strength = 0.1f;
    params.patch_radius = 2;
    params.search_radius = 3;
    params.mode = denoise_parameters::nl_means;
    if (!denoise<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y, float val) { reference[y * width + x] = val; },
            params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y, float val) { fast[y * width + x] = val; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < fast.size (); i++)
      if (fabs (fast[i] - reference[i]) > 2e-6)
        {
          fprintf (stderr,
                   "Fast/reference NL-means differ at %zu: %.9g versus %.9g\n",
                   i, fast[i], reference[i]);
          return false;
        }
  }

  /* Integral-image and reference NL-means must also agree when patch
     differences are normalized by the optional signal-dependent variance
     model.  */
  {
    std::vector<float> reference (width * height), fast (width * height);
    denoise_parameters params;
    params.strength = 0.9f;
    params.patch_radius = 2;
    params.search_radius = 3;
    params.noise_variance_floor = 0.0004f;
    params.noise_variance_slope = 0.0015f;
    params.mode = denoise_parameters::nl_means;
    if (!denoise<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y, float val) { reference[y * width + x] = val; },
            params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y, float val) { fast[y * width + x] = val; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < fast.size (); i++)
      if (fabs (fast[i] - reference[i]) > 3e-6)
        {
          fprintf (stderr,
                   "Noise-normalized fast/reference NL-means differ at %zu: "
                   "%.9g versus %.9g\n",
                   i, fast[i], reference[i]);
          return false;
        }
  }

  /* Collection support of one must be exactly the historical unweighted
     confidence.  This is important because well-sampled screen patches should
     not change merely because analyze-* now retains its collection weights.  */
  {
    std::vector<float> unweighted (width * height), weighted (width * height);
    denoise_parameters params;
    params.mode = denoise_parameters::nl_fast;
    params.strength = 0.1f;
    params.patch_radius = 2;
    params.search_radius = 3;
    if (!denoise<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y, float val) { unweighted[y * width + x] = val; },
            params, NULL, false)
        || !denoise_with_support<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [] (int, int) { return 1.0f; },
            [&] (int x, int y, float val) { weighted[y * width + x] = val; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < weighted.size (); i++)
      if (weighted[i] != unweighted[i])
        {
          fprintf (stderr,
                   "Unit-support denoising changed historical output at %zu: "
                   "%.9g versus %.9g\n",
                   i, weighted[i], unweighted[i]);
          return false;
        }
  }

  /* A screen sample inferred from essentially no actual scanner area must not
     have the same authority as a reliable observation.  With a small range
     sigma, ordinary bilateral filtering preserves the isolated bright sample;
     zero collection support lets its reliable neighbours reconstruct it.  */
  {
    const int w = 15, h = 15, cx = w / 2, cy = h / 2;
    std::vector<float> input ((size_t)w * h, 0.2f);
    std::vector<float> ordinary ((size_t)w * h), weighted ((size_t)w * h);
    std::vector<float> support ((size_t)w * h, 1.0f);
    input[(size_t)cy * w + cx] = 0.8f;
    support[(size_t)cy * w + cx] = 0.0f;
    denoise_parameters params;
    params.mode = denoise_parameters::bilateral;
    params.bilateral_sigma_s = 2.0f;
    params.bilateral_sigma_r = 0.05f;
    if (!denoise<float> (
            w, h, [&] (int x, int y) { return input[(size_t)y * w + x]; },
            [&] (int x, int y, float val) { ordinary[(size_t)y * w + x] = val; },
            params, NULL, false)
        || !denoise_with_support<float> (
            w, h, [&] (int x, int y) { return input[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [&] (int x, int y, float val) { weighted[(size_t)y * w + x] = val; },
            params, NULL, false))
      return false;
    const float ordinary_center = ordinary[(size_t)cy * w + cx];
    const float weighted_center = weighted[(size_t)cy * w + cx];
    if (ordinary_center < 0.7f || fabsf (weighted_center - 0.2f) > 1e-5f)
      {
        fprintf (stderr,
                 "Collection support did not suppress unreliable sample: "
                 "ordinary %.9g weighted %.9g\n",
                 ordinary_center, weighted_center);
        return false;
      }
  }

  /* Fast and reference NLM must remain equivalent with spatially varying
     collection support, including sub-pixel and zero-support samples.  */
  {
    std::vector<float> support (width * height), reference (width * height),
        fast (width * height);
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++)
        {
          const int k = (x + 3 * y) % 7;
          support[(size_t)y * width + x]
              = k == 0 ? 0.0f : k == 1 ? 0.2f : k == 2 ? 0.5f
                                                   : k == 3 ? 0.8f
                                                            : 1.0f + 0.25f * (k - 4);
        }
    denoise_parameters params;
    params.strength = 0.1f;
    params.patch_radius = 2;
    params.search_radius = 3;
    params.mode = denoise_parameters::nl_means;
    if (!denoise_with_support<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y) { return support[(size_t)y * width + x]; },
            [&] (int x, int y, float val) { reference[y * width + x] = val; },
            params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise_with_support<float> (
            width, height, [&] (int x, int y) { return noisy[y * width + x]; },
            [&] (int x, int y) { return support[(size_t)y * width + x]; },
            [&] (int x, int y, float val) { fast[y * width + x] = val; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < fast.size (); i++)
      if (fabs (fast[i] - reference[i]) > 3e-6)
        {
          fprintf (stderr,
                   "Confidence-aware fast/reference NL-means differ at %zu: "
                   "%.9g versus %.9g\n",
                   i, fast[i], reference[i]);
          return false;
        }
  }

  /* Screen-domain radii are expressed in common screen coordinates.  Check
     both geometry mappings themselves and the reference filter's compatibility
     with the historical unit rectangular lattice.  */
  {
    /* Paget red/green are packed by row.  Their mappings must remain exact for
       negative logical entries used by reflected denoising borders.  */
    for (int y = -5; y <= 5; y++)
      for (int x = -5; x <= 5; x++)
        {
          int_point_t e = { x, y };
          if (paget_geometry::red_scr_to_entry (
                  paget_geometry::red_entry_to_scr (e)) != e
              || paget_geometry::green_scr_to_entry (
                     paget_geometry::green_entry_to_scr (e)) != e
              || paget_geometry::blue_scr_to_entry (
                     paget_geometry::blue_entry_to_scr (e)) != e
              || dufay_geometry::red_scr_to_entry (
                     dufay_geometry::red_entry_to_scr (e)) != e)
            {
              fprintf (stderr,
                       "Screen sample entry/screen mapping is not reversible at %i,%i\n",
                       x, y);
              return false;
            }
        }

    /* Dufay red is sampled at half-screen-cell horizontal spacing: a physical
       radius-one square therefore contains five by three red samples.  Paget
       blue has half-cell spacing on both axes and therefore contains 5x5.  */
    auto count_square = [] (auto entry_to_scr, int rx, int ry)
    {
      int count = 0;
      point_t center = entry_to_scr (int_point_t{ 0, 0 });
      for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++)
          {
            point_t p = entry_to_scr (int_point_t{ x, y });
            if (my_fabs (p.x - center.x) <= 1.000000001
                && my_fabs (p.y - center.y) <= 1.000000001)
              count++;
          }
      return count;
    };
    if (count_square (dufay_geometry::red_entry_to_scr, 6, 4) != 15
        || count_square (paget_geometry::blue_entry_to_scr, 6, 6) != 25)
      {
        fprintf (stderr, "Screen-coordinate denoise neighbourhood has wrong density\n");
        return false;
      }

    /* The same packed Paget red array offset points in opposite horizontal
       directions on alternating phases.  This is why NLM must map physical
       patch displacements rather than simply reuse raw array offsets.  */
    point_t p00 = paget_geometry::red_entry_to_scr ({ 0, 0 });
    point_t p01 = paget_geometry::red_entry_to_scr ({ 0, 1 });
    point_t p02 = paget_geometry::red_entry_to_scr ({ 0, 2 });
    point_t d0 = p01 - p00;
    point_t d1 = p02 - p01;
    if (!d0.almost_eq ({ 0.5, 0.5 }, 1e-12)
        || !d1.almost_eq ({ -0.5, 0.5 }, 1e-12))
      {
        fprintf (stderr, "Paget packed-row physical phase is wrong\n");
        return false;
      }

    /* On a unit rectangular sample lattice the geometry-aware reference
       implementation must reproduce the historical reference NLM and
       bilateral filters.  */
    auto identity_to_scr = [] (int_point_t e) -> point_t
      { return { (coord_t)e.x, (coord_t)e.y }; };
    auto identity_to_entry = [] (point_t p) -> int_point_t
      { return { nearest_int (p.x), nearest_int (p.y) }; };
    for (denoise_parameters::denoise_mode mode :
         { denoise_parameters::bilateral, denoise_parameters::nl_means })
      {
        std::vector<float> ordinary (width * height), screen_ref (width * height);
        denoise_parameters params;
        params.mode = mode;
        params.strength = 0.1f;
        params.patch_radius = 2;
        params.search_radius = 3;
        params.bilateral_sigma_s = 1.5f;
        params.bilateral_sigma_r = 0.1f;
        if (!denoise<float> (
                width, height,
                [&] (int x, int y) { return noisy[(size_t)y * width + x]; },
                [&] (int x, int y, float v)
                { ordinary[(size_t)y * width + x] = v; },
                params, NULL, false)
            || !denoise_screen<float> (
                width, height,
                [&] (int x, int y) { return noisy[(size_t)y * width + x]; },
                [&] (int x, int y, float v)
                { screen_ref[(size_t)y * width + x] = v; },
                identity_to_scr, identity_to_entry, 1, 1, params, NULL, false))
          return false;
        for (size_t i = 0; i < ordinary.size (); i++)
          if (fabs (ordinary[i] - screen_ref[i]) > 3e-6)
            {
              fprintf (stderr,
                       "Unit-lattice geometry denoiser differs at %zu: %.9g vs %.9g\n",
                       i, ordinary[i], screen_ref[i]);
              return false;
            }
      }

    /* Geometry NL_FAST intentionally falls back to the reference path until
       a valid acceleration exists for packed/skewed lattices.  */
    std::vector<float> ref (width * height), fast (width * height);
    denoise_parameters params;
    params.strength = 0.9f;
    params.patch_radius = 2;
    params.search_radius = 3;
    params.noise_variance_floor = 0.0004f;
    params.noise_variance_slope = 0.0015f;
    params.mode = denoise_parameters::nl_means;
    if (!denoise_screen<float> (
            width, height,
            [&] (int x, int y) { return noisy[(size_t)y * width + x]; },
            [&] (int x, int y, float v) { ref[(size_t)y * width + x] = v; },
            paget_geometry::red_entry_to_scr,
            [] (point_t p) { return paget_geometry::red_scr_to_entry (p); },
            1, 2, params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise_screen<float> (
            width, height,
            [&] (int x, int y) { return noisy[(size_t)y * width + x]; },
            [&] (int x, int y, float v) { fast[(size_t)y * width + x] = v; },
            paget_geometry::red_entry_to_scr,
            [] (point_t p) { return paget_geometry::red_scr_to_entry (p); },
            1, 2, params, NULL, false))
      return false;
    for (size_t i = 0; i < ref.size (); i++)
      if (ref[i] != fast[i])
        {
          fprintf (stderr, "Geometry NL_FAST did not use reference semantics\n");
          return false;
        }
  }

  /* Precise-RGB pre-demosaic filtering must use one RGB-vector weight for
     all components.  If every input sample lies on one chromaticity ray,
     common weighting preserves that ray; three independent scalar filters do
     not in general because each component has a different distance scale.  */
  {
    const int vw = 13, vh = 11;
    const rgbdata ray = { (luminosity_t)0.2, (luminosity_t)0.5,
                          (luminosity_t)0.9 };
    std::vector<rgbdata> input ((size_t)vw * vh), out ((size_t)vw * vh),
        fast ((size_t)vw * vh);
    std::vector<luminosity_t> support ((size_t)vw * vh, 1);
    for (int y = 0; y < vh; y++)
      for (int x = 0; x < vw; x++)
        {
          luminosity_t a = (luminosity_t)(0.3 + 0.03 * ((3 * x + 5 * y) % 9));
          input[(size_t)y * vw + x] = ray * a;
        }
    support[(size_t)5 * vw + 6] = 0;
    input[(size_t)5 * vw + 6] = ray * (luminosity_t)1.7;

    auto identity_to_scr = [] (int_point_t e) -> point_t
      { return { (coord_t)e.x, (coord_t)e.y }; };
    auto identity_to_entry = [] (point_t p) -> int_point_t
      { return { nearest_int (p.x), nearest_int (p.y) }; };

    for (denoise_parameters::denoise_mode mode :
         { denoise_parameters::bilateral, denoise_parameters::nl_means })
      {
        denoise_parameters params;
        params.mode = mode;
        params.strength = 0.035f;
        params.patch_radius = 1;
        params.search_radius = 2;
        params.bilateral_sigma_s = 1.5f;
        params.bilateral_sigma_r = 0.08f;
        if (!denoise_screen_rgb_with_support (
                vw, vh,
                [&] (int x, int y) { return input[(size_t)y * vw + x]; },
                [&] (int x, int y) { return support[(size_t)y * vw + x]; },
                [&] (int x, int y, rgbdata v) { out[(size_t)y * vw + x] = v; },
                identity_to_scr, identity_to_entry, 1, 1, params, NULL, false))
          return false;
        for (const rgbdata &v : out)
          if (v.red > 1e-8
              && (fabs (v.green / v.red - ray.green / ray.red) > 3e-5
                  || fabs (v.blue / v.red - ray.blue / ray.red) > 3e-5))
            {
              fprintf (stderr,
                       "Screen RGB-vector denoising changed chromaticity: %g %g %g\n",
                       v.red, v.green, v.blue);
              return false;
            }
      }

    /* Geometry NL_FAST has the same exact reference semantics as NL_MEANS.  */
    denoise_parameters params;
    params.mode = denoise_parameters::nl_means;
    params.strength = 0.9f;
    params.patch_radius = 1;
    params.search_radius = 2;
    params.noise_variance_floor = 0.0005f;
    params.noise_variance_slope = 0.001f;
    if (!denoise_screen_rgb_with_support (
            vw, vh, [&] (int x, int y) { return input[(size_t)y * vw + x]; },
            [&] (int x, int y) { return support[(size_t)y * vw + x]; },
            [&] (int x, int y, rgbdata v) { out[(size_t)y * vw + x] = v; },
            paget_geometry::red_entry_to_scr,
            [] (point_t p) { return paget_geometry::red_scr_to_entry (p); },
            1, 2, params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise_screen_rgb_with_support (
            vw, vh, [&] (int x, int y) { return input[(size_t)y * vw + x]; },
            [&] (int x, int y) { return support[(size_t)y * vw + x]; },
            [&] (int x, int y, rgbdata v) { fast[(size_t)y * vw + x] = v; },
            paget_geometry::red_entry_to_scr,
            [] (point_t p) { return paget_geometry::red_scr_to_entry (p); },
            1, 2, params, NULL, false))
      return false;
    for (size_t i = 0; i < out.size (); i++)
      if (out[i].red != fast[i].red || out[i].green != fast[i].green
          || out[i].blue != fast[i].blue)
        {
          fprintf (stderr, "Screen RGB-vector NL_FAST differs from reference\n");
          return false;
        }
  }

  /* analyze_base must actually pass retained collection support to the
     pre-demosaic denoiser.  This synthetic analyzer represents a sub-pixel
     central red patch surrounded by reliable samples.  */
  {
    class support_test_analyzer : public analyze_base_worker<dufay_geometry>
    {
    public:
      support_test_analyzer () : analyze_base_worker (0, 0, 0, 0, 0, 0)
      {
        m_area = { 0, 0, 15, 15 };
        const size_t n = (size_t)m_area.width * m_area.height;
        m_red = std::make_unique<luminosity_t[]> (n);
        m_red_support = std::make_unique<luminosity_t[]> (n);
        std::fill (m_red.get (), m_red.get () + n, (luminosity_t)0.2);
        std::fill (m_red_support.get (), m_red_support.get () + n,
                   (luminosity_t)1);
        const size_t center = (size_t)7 * m_area.width + 7;
        m_red[center] = (luminosity_t)0.8;
        m_red_support[center] = 0;
      }
    } analyzer;
    denoise_parameters params;
    params.mode = denoise_parameters::bilateral;
    params.bilateral_sigma_s = 2;
    params.bilateral_sigma_r = 0.05f;
    if (analyzer.red_collection_support (7, 7) != 0
        || analyzer.red_collection_support (6, 7) != 1
        || !analyzer.denoise_red (params, NULL)
        || fabs (analyzer.red (7, 7) - 0.2f) > 1e-5)
      {
        fprintf (stderr,
                 "Analyzer did not preserve/use collection support in denoising\n");
        return false;
      }
  }

  /* The public functor interface is used in-place by analyze_base.  Tiled
     denoising must still read every border from the original image, not from
     an already processed neighbouring tile.  */
  {
    const int w = 300, h = 180;
    std::vector<float> source ((size_t)w * h), out ((size_t)w * h);
    std::vector<float> inplace;
    unsigned int s = 17;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          float noise
              = ((int)(fast_rand16 (&s) % 201) - 100) / 2500.0f;
          source[(size_t)y * w + x]
              = (x < w / 2 ? 0.2f : 0.8f) + noise
                + 0.03f * sinf (0.17f * x + 0.11f * y);
        }
    inplace = source;
    denoise_parameters params;
    params.mode = denoise_parameters::nl_fast;
    params.strength = 0.1f;
    params.patch_radius = 1;
    params.search_radius = 3;
    if (!denoise<float> (
            w, h, [&] (int x, int y) { return source[(size_t)y * w + x]; },
            [&] (int x, int y, float val) { out[(size_t)y * w + x] = val; },
            params, NULL, true)
        || !denoise<float> (
            w, h, [&] (int x, int y) { return inplace[(size_t)y * w + x]; },
            [&] (int x, int y, float val)
            { inplace[(size_t)y * w + x] = val; },
            params, NULL, true))
      return false;
    for (size_t i = 0; i < out.size (); i++)
      if (out[i] != inplace[i])
        {
          fprintf (stderr,
                   "In-place denoising differs from immutable-input result at %zu\n",
                   i);
          return false;
        }
  }

  /* Image-boundary extension must not prefer the left/top side over the
     right/bottom side.  */
  {
    const int w = 40, h = 17;
    std::vector<float> a ((size_t)w * h), flipped ((size_t)w * h);
    std::vector<float> da ((size_t)w * h), df ((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        a[(size_t)y * w + x]
            = 0.1f + 0.015f * x + 0.07f * sinf (0.3f * x + 0.2f * y);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        flipped[(size_t)y * w + x] = a[(size_t)y * w + (w - 1 - x)];
    denoise_parameters params;
    params.mode = denoise_parameters::bilateral;
    params.bilateral_sigma_s = 2;
    params.bilateral_sigma_r = 0.1f;
    if (!denoise<float> (
            w, h, [&] (int x, int y) { return a[(size_t)y * w + x]; },
            [&] (int x, int y, float val) { da[(size_t)y * w + x] = val; },
            params, NULL, false)
        || !denoise<float> (
            w, h,
            [&] (int x, int y) { return flipped[(size_t)y * w + x]; },
            [&] (int x, int y, float val) { df[(size_t)y * w + x] = val; },
            params, NULL, false))
      return false;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (fabs (da[(size_t)y * w + x]
                  - df[(size_t)y * w + (w - 1 - x)])
            > 2e-6)
          {
            fprintf (stderr, "Denoising boundary reflection is asymmetric\n");
            return false;
          }
  }

  /* Precise-RGB screen data use channel-specific rectangular sample grids.
     Averaging a tile must iterate HEIGHT_SCALE vertically and WIDTH_SCALE
     horizontally; swapping them reads the wrong Dufay red samples.  */
  {
    class test_dufay_rgb_analyzer : public analyze_base_worker<dufay_geometry>
    {
    public:
      test_dufay_rgb_analyzer () : analyze_base_worker (1, 0, 0, 0, 0, 0)
      {
        m_area = { 0, 0, 1, 1 };
        m_rgb_red = std::make_unique<rgbdata[]> (2);
        m_rgb_green = std::make_unique<rgbdata[]> (1);
        m_rgb_blue = std::make_unique<rgbdata[]> (1);
        m_rgb_red[0] = { 1, 2, 3 };
        m_rgb_red[1] = { 3, 4, 5 };
        m_rgb_green[0] = { 6, 7, 8 };
        m_rgb_blue[0] = { 9, 10, 11 };
      }
    } analyzer;
    rgbdata red, green, blue;
    analyzer.screen_tile_rgb_color (red, green, blue, 0, 0);
    if (red != rgbdata{ 2, 3, 4 } || green != rgbdata{ 6, 7, 8 }
        || blue != rgbdata{ 9, 10, 11 })
      {
        fprintf (stderr, "Precise-RGB screen tile averaging uses wrong grid axes\n");
        return false;
      }
  }

  /* Post-demosaic denoising must use one RGB-vector weight rather than
     filtering each channel independently.  A field whose colors lie on a
     fixed chromaticity ray must therefore remain on that ray after filtering.  */
  {
    const int w = 9, h = 5;
    std::vector<rgbdata> in ((size_t)w * h), out ((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        {
          luminosity_t v = (luminosity_t)(0.08 * x + 0.025 * y);
          if (x == 4 && y == 2)
            v += (luminosity_t)0.22;
          in[(size_t)y * w + x] = { v, 2 * v, 3 * v };
        }
    denoise_parameters params;
    params.mode = denoise_parameters::bilateral;
    params.bilateral_sigma_s = 1.5f;
    params.bilateral_sigma_r = 0.18f;
    if (!denoise_rgb_vector (
            w, h,
            [&] (int x, int y) { return in[(size_t)y * w + x]; },
            [&] (int x, int y, rgbdata c) { out[(size_t)y * w + x] = c; },
            params, NULL, false))
      return false;
    for (const rgbdata &c : out)
      if (fabs (c.green - 2 * c.red) > 2e-6
          || fabs (c.blue - 3 * c.red) > 3e-6)
        {
          fprintf (stderr,
                   "Post-demosaic vector denoising changed chromaticity\n");
          return false;
        }
  }

  /* The vector reference and integral-image NL-means implementations must
     implement the same RGB patch metric.  */
  {
    const int w = 11, h = 8;
    std::vector<rgbdata> in ((size_t)w * h), slow ((size_t)w * h), fast ((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        in[(size_t)y * w + x]
            = { (luminosity_t)(0.03 * x + 0.011 * y),
                (luminosity_t)(0.02 * y + 0.017 * x),
                (luminosity_t)(0.013 * (x + y) + (x == 5 ? 0.08 : 0)) };
    denoise_parameters params;
    params.patch_radius = 1;
    params.search_radius = 2;
    params.strength = 0.12f;
    params.mode = denoise_parameters::nl_means;
    if (!denoise_rgb_vector (
            w, h, [&] (int x, int y) { return in[(size_t)y * w + x]; },
            [&] (int x, int y, rgbdata c) { slow[(size_t)y * w + x] = c; },
            params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise_rgb_vector (
            w, h, [&] (int x, int y) { return in[(size_t)y * w + x]; },
            [&] (int x, int y, rgbdata c) { fast[(size_t)y * w + x] = c; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < slow.size (); i++)
      for (int c = 0; c < 3; c++)
        if (fabs (slow[i][c] - fast[i][c]) > 3e-5)
          {
            fprintf (stderr,
                     "Vector NL-means implementations disagree: %g vs %g\n",
                     (double)slow[i][c], (double)fast[i][c]);
            return false;
          }

    params.noise_variance_floor = 0.0004f;
    params.noise_variance_slope = 0.0015f;
    params.strength = 0.9f;
    params.mode = denoise_parameters::nl_means;
    if (!denoise_rgb_vector (
            w, h, [&] (int x, int y) { return in[(size_t)y * w + x]; },
            [&] (int x, int y, rgbdata c) { slow[(size_t)y * w + x] = c; },
            params, NULL, false))
      return false;
    params.mode = denoise_parameters::nl_fast;
    if (!denoise_rgb_vector (
            w, h, [&] (int x, int y) { return in[(size_t)y * w + x]; },
            [&] (int x, int y, rgbdata c) { fast[(size_t)y * w + x] = c; },
            params, NULL, false))
      return false;
    for (size_t i = 0; i < slow.size (); i++)
      for (int c = 0; c < 3; c++)
        if (fabs (slow[i][c] - fast[i][c]) > 4e-5)
          {
            fprintf (stderr,
                     "Noise-normalized vector NLM implementations disagree: "
                     "%g vs %g\n",
                     (double)slow[i][c], (double)fast[i][c]);
            return false;
          }
  }

  /* Both denoising stages are part of the rendering state and must survive a
     CSP save/load round trip exactly, including parameters inactive in the
     currently selected mode.  */
  {
    render_parameters saved, loaded;
    saved.screen_denoise.mode = denoise_parameters::nl_fast;
    saved.screen_denoise.strength = 0.073f;
    saved.screen_denoise.noise_variance_floor = 0.00031f;
    saved.screen_denoise.noise_variance_slope = 0.0017f;
    saved.screen_denoise.patch_radius = 3;
    saved.screen_denoise.search_radius = 9;
    saved.screen_denoise.bilateral_sigma_s = 1.75f;
    saved.screen_denoise.bilateral_sigma_r = 0.034f;
    saved.demosaiced_denoise.mode = denoise_parameters::bilateral;
    saved.demosaiced_denoise.strength = 0.231f;
    saved.demosaiced_denoise.noise_variance_floor = 0.00052f;
    saved.demosaiced_denoise.noise_variance_slope = 0.0021f;
    saved.demosaiced_denoise.patch_radius = 4;
    saved.demosaiced_denoise.search_radius = 11;
    saved.demosaiced_denoise.bilateral_sigma_s = 2.25f;
    saved.demosaiced_denoise.bilateral_sigma_r = 0.047f;
    FILE *f = tmpfile ();
    const char *error = NULL;
    if (!f || !save_csp (f, NULL, NULL, &saved, NULL) || fseek (f, 0, SEEK_SET)
        || !load_csp (f, NULL, NULL, &loaded, NULL, &error))
      {
        fprintf (stderr, "Denoising CSP round trip failed: %s\n",
                 error ? error : "I/O error");
        if (f)
          fclose (f);
        return false;
      }
    fclose (f);
    if (!saved.screen_denoise.equal_p (loaded.screen_denoise)
        || !saved.demosaiced_denoise.equal_p (loaded.demosaiced_denoise))
      {
        fprintf (stderr, "Denoising parameters were not preserved\n");
        return false;
      }
  }

  /* denoise_parameters::operator== is deliberately cache/output equivalence:
     changing a parameter ignored by the current mode must not invalidate a
     cached image.  render_parameters::operator== has different semantics and
     is an exact GUI/state comparison, so the same edit must make rendering
     parameters structurally different.  */
  {
    render_parameters a, b;
    a.screen_denoise.mode = denoise_parameters::none;
    b = a;
    b.screen_denoise.strength = a.screen_denoise.strength + 0.125f;
    if (!(a.screen_denoise == b.screen_denoise))
      {
        fprintf (stderr,
                 "Inactive denoise parameter invalidates cache equivalence\n");
        return false;
      }
    if (a.screen_denoise.equal_p (b.screen_denoise))
      {
        fprintf (stderr,
                 "Denoise structural comparison ignored stored parameter\n");
        return false;
      }
    if (a == b)
      {
        fprintf (stderr,
                 "Render structural comparison used denoise cache equivalence\n");
        return false;
      }
    b = a;
    a.screen_denoise.mode = denoise_parameters::nl_means;
    b = a;
    b.screen_denoise.noise_variance_slope = 0.01f;
    if (!(a.screen_denoise == b.screen_denoise))
      {
        fprintf (stderr,
                 "Inactive noise-variance slope invalidates cache equivalence\n");
        return false;
      }
    a.screen_denoise.noise_variance_floor = 0.001f;
    b = a;
    b.screen_denoise.noise_variance_slope = 0.01f;
    if (a.screen_denoise == b.screen_denoise)
      {
        fprintf (stderr,
                 "Active noise-variance slope ignored by cache equivalence\n");
        return false;
      }

    b = a;
    b.demosaiced_denoise.bilateral_sigma_r
        = a.demosaiced_denoise.bilateral_sigma_r + 0.01f;
    if (a == b)
      {
        fprintf (stderr,
                 "Render comparison ignored post-demosaic denoise state\n");
        return false;
      }
  }

  return true;
}
/* Unit test for Dufaycolor RCD demosaicing.  */
template <typename GEOMETRY>
class fake_analyze
{
public:
  int_image_area m_area;
  fake_analyze (int w, int h) : m_area ({ 0, 0, w, h }) {}

  int_image_area
  demosaiced_area () const
  {
    return m_area;
  }

  bool
  populate_demosaiced_data (std::vector<rgbdata> &data, render *r,
                            int_image_area area, progress_info *progress)
  {
    for (int y = 0; y < m_area.height; y++)
      for (int x = 0; x < m_area.width; x++)
        {
          int color = GEOMETRY::demosaic_entry_color (x, y);
          if (color != base_geometry::none)
            {
              rgbdata expected;
              if (y < m_area.height / 3)
                {
                  int tx = x / 32;
                  int ty = y / 32;
                  expected.red = (luminosity_t)((tx * 17) % 256) / 255.0;
                  expected.green = (luminosity_t)((ty * 23) % 256) / 255.0;
                  expected.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;
                }
              else if (y < 2 * m_area.height / 3)
                {
                  /* 45-degree rotated squares.  */
                  int tx = (x + y) / 64;
                  int ty = (x - y + m_area.width) / 64;
                  expected.red = (luminosity_t)((tx * 31) % 256) / 255.0;
                  expected.green = (luminosity_t)((ty * 37) % 256) / 255.0;
                  expected.blue = (luminosity_t)(((tx + ty) * 41) % 256) / 255.0;
                }
              else
                {
                  /* Smooth gradients.  */
                  expected.red = (luminosity_t)x / (m_area.width - 1);
                  expected.green = (luminosity_t)y / (m_area.height - 1);
                  expected.blue = (luminosity_t)(x + y)
                                  / (m_area.width + m_area.height - 2);
                }

              if (color == base_geometry::red)
                data[y * m_area.width + x].red = expected.red;
              else if (color == base_geometry::green)
                data[y * m_area.width + x].green = expected.green;
              else if (color == base_geometry::blue)
                data[y * m_area.width + x].blue = expected.blue;
            }
        }
    return true;
  }
};

template <typename GEOMETRY, typename DEMOSAICER>
bool
test_demosaic_loop (fake_analyze<GEOMETRY> &fake, DEMOSAICER &demosaicer,
                    render_parameters::screen_demosaic_t alg, const char *alg_name)
{
  if (!demosaicer.demosaic (&fake, NULL, alg, denoise_parameters (), NULL))
    {
      printf ("Demosaic %s failed to run\n", alg_name);
      return false;
    }

  int width = fake.m_area.width;
  int height = fake.m_area.height;
  bool ok = true;
  for (int y = 20; y < height - 20; y++)
    for (int x = 20; x < width - 20; x++)
      {
        rgbdata expected;
	const char *section;
        if (y < height / 3)
          {
            int tx = x / 32;
            int ty = y / 32;
	    section = "squares";
	    /* Ignore borders.  */
	    if (x - tx * 32 < 3 || x - tx * 32 >29
		|| y - ty * 32 < 3 || y - ty * 32 >29)
	      continue;
            expected.red = (luminosity_t)((tx * 17) % 256) / 255.0;
            expected.green = (luminosity_t)((ty * 23) % 256) / 255.0;
            expected.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;
          }
        else if (y < 2 * height / 3)
          {
            int tx = (x + y) / 64;
            int ty = (x - y + width) / 64;
	    /* Ignore borders.  */
	    section = "diagonal squares";
	    if ((x+y) - tx * 64 < 3 || x + y - tx * 64 > 61
		|| (x - y + width) - ty * 64 < 3 || (x - y + width) - ty * 32 > 61)
	      continue;
            expected.red = (luminosity_t)((tx * 31) % 256) / 255.0;
            expected.green = (luminosity_t)((ty * 37) % 256) / 255.0;
            expected.blue = (luminosity_t)(((tx + ty) * 41) % 256) / 255.0;
          }
        else
          {
	    if (y < 2 * height / 3 + 3)
	      continue;
	    section = "gradient";
            expected.red = (luminosity_t)x / (width - 1);
            expected.green = (luminosity_t)y / (height - 1);
            expected.blue = (luminosity_t)(x + y) / (width + height - 2);
          }

        bool skip = false;
        if (abs (y - height / 3) < 20 || abs (y - 2 * height / 3) < 20)
          skip = true;

        rgbdata actual = demosaicer.demosaiced_data (x, y);
        if (!skip && (fabs (actual.red - expected.red) > 0.05
                      || fabs (actual.green - expected.green) > 0.05
                      || fabs (actual.blue - expected.blue) > 0.05))
          {
            printf ("Demosaic %s, section %s mismatch at (%i, %i): expected (%f, %f, %f), got (%f, %f, %f)\n",
                    alg_name, section, x, y, expected.red, expected.green, expected.blue,
                    actual.red, actual.green, actual.blue);
            ok = false;
            break;
          }
      }
  return ok;
}

bool
test_demosaic_paget ()
{
  int w = 512, h = 768;
  fake_analyze<paget_geometry> fake (w, h);
  demosaic_paget_base<fake_analyze<paget_geometry>> demosaicer;
  bool ok = true;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::hamilton_adams_demosaic, "Paget Hamilton-Adams"))
    demosaicer.save_tiff ("paget_ha_test.tiff", NULL);
  else ok = false;
  
  if (test_demosaic_loop (fake, demosaicer, render_parameters::ahd_demosaic, "Paget AHD"))
    demosaicer.save_tiff ("paget_ahd_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::amaze_demosaic, "Paget AMaZE"))
    demosaicer.save_tiff ("paget_amaze_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Paget RCD"))
    demosaicer.save_tiff ("paget_rcd_test.tiff", NULL);
  else ok = false;

  if (test_demosaic_loop (fake, demosaicer, render_parameters::lmmse_demosaic, "Paget LMMSE"))
    demosaicer.save_tiff ("paget_lmmse_test.tiff", NULL);
  else ok = false;

  return ok;
}

bool
test_demosaic_dufay ()
{
  int w = 512, h = 768;
  fake_analyze<dufay_geometry> fake (w, h);
  demosaic_dufay_base<fake_analyze<dufay_geometry>> demosaicer;
  
  bool ok = test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Dufay RCD");
  demosaicer.save_tiff ("dufay_rcd_test.tiff", NULL);

  /* The independently configured post-demosaic stage must actually operate on
     the completed color field.  */
  demosaic_dufay_base<fake_analyze<dufay_geometry>> filtered;
  denoise_parameters post;
  post.mode = denoise_parameters::bilateral;
  post.bilateral_sigma_s = 1.5f;
  post.bilateral_sigma_r = 0.5f;
  if (!filtered.demosaic (&fake, NULL, render_parameters::rcd_demosaic, post, NULL))
    {
      fprintf (stderr, "Post-demosaic Dufay denoising failed\n");
      return false;
    }
  bool changed = false;
  for (int y = 20; y < h - 20 && !changed; y++)
    for (int x = 20; x < w - 20; x++)
      {
        rgbdata a = demosaicer.fast_demosaiced_data (x, y);
        rgbdata b = filtered.fast_demosaiced_data (x, y);
        if (fabs (a.red - b.red) + fabs (a.green - b.green)
                + fabs (a.blue - b.blue)
            > 1e-5)
          {
            changed = true;
            break;
          }
      }
  if (!changed)
    {
      fprintf (stderr, "Post-demosaic Dufay denoising had no effect\n");
      ok = false;
    }
  return ok;
}

bool
test_demosaic ()
{
  bool ok = true;
  if (!test_demosaic_paget ())
    ok = false;
  if (!test_demosaic_dufay ())
    ok = false;
  return ok;
}
}





int
main (int argc, char **argv)
{
  struct test_entry
  {
    const char *name;
    const char *description;
    bool (*func) ();
  };

  test_entry tests[] = {
    { "matrix", "matrix tests", [] () { test_matrix (); return true; } },
    { "color", "color tests", [] () { test_color (); return true; } },
    { "nonfinite_helpers", "fast-math non-finite and GSL failure tests",
      [] () { return test_nonfinite_helpers (); } },
    { "finetune_helpers", "finetune and Nelder-Mead contract tests",
      [] () { return test_finetune_helpers (); } },
    { "finetune_focus_cache", "exact finetune focus-screen cache tests",
      [] () { return test_finetune_focus_screen_cache (); } },
    { "scanner_blur_correction", "scanner blur correction table tests",
      [] () { return test_scanner_blur_correction_contract (); } },
    { "linearity", "render linearity tests", [] () { return (bool)test_render_linearity (); } },
    { "blur", "screen blur tests", [] () { return test_screen_blur (); } },
    { "sharpening", "screen sharpening tests", [] () { return test_screen_sharpening (); } },
    { "screen_simulation", "screen simulation tests",
      [] () { return test_screen_simulation (); } },
    { "mtf_model", "physical MTF model tests",
      [] () { return test_mtf_physical_model (); } },
    { "mtf_deconvolution", "measured MTF deconvolution tests",
      [] () { return test_mtf_deconvolution (); } },
    { "homography", "homography tests", [] () { return (bool)test_homography (false, false, 0.000001); } },
    { "warp", "lens warp tests", [] () { return test_lens_warp (); } },
    { "lens_correction", "lens correction tests", [] () { return (bool)test_homography (true, false, 0.15); } },
    { "1d_homography", "1d homography and lens correction tests", [] () { return (bool)test_homography (true, true, 0.15); } },
    { "discovery", "screen discovery tests", [] () { return (bool)test_discovery (1.8); } },
    { "precomputed", "precomputed function tests", [] () { return test_precomputed_function (); } },
    { "histogram", "histogram parallel tests", [] () { return test_histogram_parallel (); } },
    { "richards", "richards curve tests", [] () { return test_richards_curve (); } },
    { "richards_symmetry", "richards symmetry tests", [] () { return test_richards_symmetry (); } },
    { "richards_reversibility", "richards reversibility tests", [] () { return test_richards_reversibility (); } },
    { "richards_functional_inverse", "richards functional inverse tests", [] () { return test_richards_functional_inverse (); } },
    { "hd_reversibility", "hd reversibility tests", [] () { return test_hd_reversibility (); } },
    { "hd_incremental", "hd incremental update tests", [] () { return test_hd_incremental_update (); } },
    { "hd_validity", "hd validity tests", [] () { return test_hd_validity (); } },
    { "hd_sorting", "hd sorting tests", [] () { return test_hd_sorting (); } },
    { "tone_curve", "custom tone curve tests", [] () { return test_custom_tone_curve (); } },
    { "lru_cache", "lru cache concurrency tests", [] () { return test_lru_cache_concurrency (); } },
    { "spectrum", "spectrum to xyz tests", [] () { return test_spectrum_dyes_to_xyz (); } },
    { "whitepoint", "whitepoint consistency tests", [] () { return test_whitepoint_constants (); } },
    { "darkroom", "darkroom simulation tests", [] () { return test_darkroom (); } },
    { "mesh_src_range", "mesh get_src_range tests", [] () { return test_get_src_range (); } },
    { "mesh_inversion", "mesh inversion tests", [] () { return test_mesh_inversion (); } },
    { "cow_points", "cow points tests", [] () { return test_cow_points (); } },
    { "image_area", "image area tests", [] () { return test_image_area (); } },
    { "channel_sharpening", "per-channel scanner sharpening tests",
      [] () { return test_channel_sharpening (); } },
    { "slanted_edge", "slanted edge MTF tests", [] () { return test_slanted_edge_mtf (); } },
    { "real_mtf_reproducibility", "real MTF reproducibility tests",
      [] () { return test_real_mtf_reproducibility (); } },
    { "denoising", "denoising tests", [] () { return test_denoise (); } },
    { "demosaic", "dufay and paget demosaicing tests", [] () { return test_demosaic (); } },
    { NULL, NULL, NULL }
  };

  int num_to_run = 0;
  for (int i = 0; tests[i].name; i++)
    {
      bool run = (argc == 1);
      for (int j = 1; j < argc; j++)
        if (strcmp (argv[j], tests[i].name) == 0)
          run = true;
      if (run)
        num_to_run++;
    }

  printf ("1..%i\n", num_to_run);

  for (int i = 0; tests[i].name; i++)
    {
      bool run = (argc == 1);
      for (int j = 1; j < argc; j++)
        if (strcmp (argv[j], tests[i].name) == 0)
          run = true;
      if (run)
        report (tests[i].description, tests[i].func ());
    }

  return error_found;
}
