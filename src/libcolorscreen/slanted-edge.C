/* Slanted edge MTF implementation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "include/colorscreen.h"
#include "include/mtf-parameters.h"
#include "fft.h"
#include "render.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace colorscreen {
namespace {

constexpr int default_slanted_edge_oversampling = 10;
constexpr int max_slanted_edge_oversampling = 64;
constexpr int max_slanted_edge_bins = 8 * 1024 * 1024;
/* Include the gradient tails of broad defocused edges in their weighted
   centroids.  Keep the secondary-peak guard narrower so a strong neighboring
   transition still disqualifies the scan line instead of biasing its
   centroid.  */
constexpr int edge_centroid_radius = 14;
constexpr int secondary_peak_guard = 12;
/* Scale-adaptive geometry is a fallback for edges rejected by the established
   compact path.  Derive every widened support from the measured gradient FWHM
   and cap pathological estimates before they can hide unrelated structure.  */
constexpr int max_adaptive_edge_centroid_radius = 96;
constexpr double max_adaptive_edge_width_fraction = 0.35;
constexpr double adaptive_centroid_fwhm_factor = 1.5;
constexpr double adaptive_secondary_guard_fwhm_factor = 0.5;
constexpr int adaptive_secondary_guard_padding = 2;
constexpr double adaptive_peak_offset_fwhm_factor = 0.10;
constexpr double adaptive_primary_lsf_fwhm_factor = 1.5;

/* These qualification limits deliberately favor rejecting a questionable ROI
   over manufacturing a plausible-looking curve from texture, multiple edges,
   or interpolated phase samples.  They were selected to retain the bundled
   Hurley infrared edge and the synthetic optical regressions with comfortable
   margins.  */
constexpr double max_secondary_peak_ratio = 0.75;
constexpr double min_local_gradient_coherence = 0.75;
constexpr double min_candidate_line_fraction = 0.65;
constexpr double min_dominant_polarity_fraction = 0.85;
constexpr double min_inlier_candidate_fraction = 0.80;
constexpr double max_edge_fit_rms = 0.50;
constexpr double max_edge_fit_p95 = 1.00;
constexpr double max_edge_angle_degrees = 15.0;
constexpr double min_edge_phase_motion = 0.75;
constexpr double min_plateau_snr = 8.0;
constexpr double min_esf_monotonicity = 0.50;
constexpr double min_primary_lsf_fraction = 0.45;
constexpr double max_lsf_peak_offset = 1.50;
constexpr double max_normalized_mtf = 1.50;

/* One qualified edge centroid measured on a row or column.  */
struct edge_sample
{
  double line_coordinate;
  double position;
  double signed_strength;
};

/* Geometric result of looking for one orientation of a straight edge.  */
struct edge_line_candidate
{
  bool valid = false;
  bool vertical = false;
  slanted_edge_failure failure = slanted_edge_failure_no_single_edge;
  std::string error;
  double slope = 0;
  double intercept = 0;
  double fit_rms = 0;
  double fit_p95 = 0;
  double angle_degrees = 0;
  int qualified_samples = 0;
  bool adaptive_scale = false;
  double gradient_fwhm = 0;
};

/* Return the reciprocal of sin (X) / X without losing accuracy near zero.  */
static inline double
inverse_sinc (double x)
{
  double x2 = x * x;
  if (x2 < 1.0e-12)
    return 1.0 + x2 / 6.0 + 7.0 * x2 * x2 / 360.0;
  return x / std::sin (x);
}

/* Interpolate empty bins in ESF_SUM/ESF_COUNT and store the complete ESF in
   ESF.  Measured bins retain their arithmetic mean; leading/trailing empty
   bins use the nearest measured plateau and interior gaps are linearly
   interpolated.  Return false when no measured bin is present.  */
static bool
interpolate_esf (const std::vector<double> &esf_sum,
                 const std::vector<int> &esf_count, std::vector<double> *esf)
{
  const int num_bins = (int)esf_count.size ();
  if (esf_sum.size () != esf_count.size () || !num_bins)
    return false;
  esf->assign (num_bins, 0);
  int first_valid = 0;
  while (first_valid < num_bins && !esf_count[first_valid])
    first_valid++;
  if (first_valid == num_bins)
    return false;

  const double first_value
      = esf_sum[first_valid] / esf_count[first_valid];
  for (int bin = 0; bin <= first_valid; bin++)
    (*esf)[bin] = first_value;

  int previous = first_valid;
  for (int bin = first_valid + 1; bin < num_bins; bin++)
    if (esf_count[bin])
      {
        const double left_value = esf_sum[previous] / esf_count[previous];
        const double right_value = esf_sum[bin] / esf_count[bin];
        for (int fill = previous; fill <= bin; fill++)
          {
            const double fraction
                = (double)(fill - previous) / (bin - previous);
            (*esf)[fill]
                = left_value + fraction * (right_value - left_value);
          }
        previous = bin;
      }
  const double last_value = esf_sum[previous] / esf_count[previous];
  for (int bin = previous; bin < num_bins; bin++)
    (*esf)[bin] = last_value;
  return true;
}

/* Compute the corrected normalized MTF from ESF.  PEAK_IDX is the qualified
   full-ROI LSF peak used to center finite-support windows, PARAMS specifies
   the window, OVERSAMPLING is the ESF sampling rate and N is the zero-padded
   FFT size.  Store samples from DC through sensor Nyquist in RESULT and
   return false if normalization or a corrected value is non-finite.  */
static bool
compute_mtf_curve (const std::vector<double> &esf, int peak_idx,
                   int support_half_bins,
                   const slanted_edge_parameters &params, int oversampling,
                   int N, std::vector<double> *result)
{
  const int num_bins = (int)esf.size ();
  if (num_bins < 2 || peak_idx < 0 || peak_idx >= num_bins || N < num_bins)
    return false;

  std::vector<double> lsf (num_bins, 0);
  lsf[0] = esf[1] - esf[0];
  for (int bin = 1; bin < num_bins - 1; bin++)
    lsf[bin] = (esf[bin + 1] - esf[bin - 1]) / 2.0;
  lsf[num_bins - 1] = esf[num_bins - 1] - esf[num_bins - 2];

  std::vector<double, fft_allocator<double>> in_vec (N, 0.0);
  for (int bin = 0; bin < num_bins; bin++)
    {
      const int offset = bin - peak_idx;
      double weight = 0;
      if (support_half_bins)
        {
          if (std::abs (offset) > support_half_bins)
            continue;
          const double cosine
              = std::cos (M_PI * offset / support_half_bins);
          if (params.window == slanted_edge_parameters::window_hann)
            weight = 0.5 * (1.0 + cosine);
          else if (params.window == slanted_edge_parameters::window_hamming)
            weight = 0.54 + 0.46 * cosine;
          else
            weight = 1;
        }
      else
        {
          /* Preserve the historical full-ROI window placement.  */
          const int window_index = offset + num_bins / 2;
          if (window_index < 0 || window_index >= num_bins)
            continue;
          if (params.window == slanted_edge_parameters::window_hann)
            weight = 0.5
                     * (1.0
                        - std::cos (2.0 * M_PI * window_index
                                    / (num_bins - 1)));
          else if (params.window == slanted_edge_parameters::window_hamming)
            weight = 0.54
                     - 0.46
                           * std::cos (2.0 * M_PI * window_index
                                       / (num_bins - 1));
          else
            weight = 1;
        }
      in_vec[bin] = lsf[bin] * weight;
    }

  auto out = fft_alloc_complex<double> (N / 2 + 1);
  fft_plan<double> plan
      = fft_plan_r2c_1d<double> (N, in_vec.data (), out.get ());
  plan.execute_r2c (in_vec.data (), out.get ());

  const double mtf_zero
      = std::hypot (out.get ()[0][0], out.get ()[0][1]);
  if (!my_isfinite (mtf_zero) || mtf_zero < 1.0e-9)
    return false;

  const int last_index
      = std::min (N / 2, (int)std::floor (0.5 * N / oversampling));
  result->resize (last_index + 1);
  for (int index = 0; index <= last_index; index++)
    {
      const double re = out.get ()[index][0];
      const double im = out.get ()[index][1];
      double value = std::hypot (re, im) / mtf_zero;
      if (index)
        {
          const double frequency = (double)index * oversampling / N;
          const double bin_argument = M_PI * frequency / oversampling;
          const double derivative_argument
              = 2.0 * M_PI * frequency / oversampling;
          value *= inverse_sinc (bin_argument)
                   * inverse_sinc (derivative_argument);
        }
      if (!my_isfinite (value) || value < 0)
        return false;
      (*result)[index] = value;
    }
  return true;
}

/* Format FORMAT and its following printf-style arguments into a string.  */
static std::string
format_message (const char *format, ...)
{
  char buffer[1024];
  va_list args;
  va_start (args, format);
  int length = vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);
  if (length < 0)
    return "unknown slanted-edge error";
  return std::string (buffer,
                      std::min ((size_t)length, sizeof (buffer) - 1));
}

/* Mark RES as failed for REASON, discard every numerical output that callers
   could mistake for a valid measurement, and report MESSAGE when PROGRESS is
   nonnull.  */
static void
set_failure (slanted_edge_results *res, slanted_edge_failure reason,
             progress_info *progress, const std::string &message)
{
  res->success = false;
  res->failure = reason;
  res->error = message;
  res->edge_p1 = {0, 0};
  res->edge_p2 = {0, 0};
  res->edge_angle = 0;
  res->edge_fit_rms = 0;
  res->edge_contrast = 0;
  res->edge_snr = 0;
  res->phase_coverage = 0;
  res->edge_histogram.clear ();
  res->edge_histogram_origin = 0;
  res->edge_histogram_step = 0;
  if (progress)
    fprintf (stderr, "Slanted-edge failed: %s\n", message.c_str ());
}

/* Return the median of VALUES.  VALUES is copied because nth_element reorders
   it.  */
static double
median_value (std::vector<double> values)
{
  if (values.empty ())
    return 0;
  size_t middle = values.size () / 2;
  std::nth_element (values.begin (), values.begin () + middle, values.end ());
  double result = values[middle];
  if (!(values.size () & 1))
    {
      double lower
          = *std::max_element (values.begin (), values.begin () + middle);
      result = (lower + result) * 0.5;
    }
  return result;
}

/* Return FRACTION percentile of VALUES.  FRACTION must be in the interval
   zero through one.  */
static double
percentile_value (std::vector<double> values, double fraction)
{
  if (values.empty ())
    return 0;
  std::sort (values.begin (), values.end ());
  double position = fraction * (values.size () - 1);
  size_t lower = (size_t)std::floor (position);
  size_t upper = (size_t)std::ceil (position);
  double weight = position - lower;
  return values[lower] * (1 - weight) + values[upper] * weight;
}

/* Fit POSITION = SLOPE * LINE_COORDINATE + INTERCEPT to SAMPLES using centered
   long-double accumulation.  Store the result in SLOPE and INTERCEPT and
   return false when the line coordinates are degenerate.  */
static bool
least_squares_line (const std::vector<edge_sample> &samples, double *slope,
                    double *intercept)
{
  if (samples.size () < 2)
    return false;

  long double mean_coordinate = 0;
  long double mean_position = 0;
  for (const edge_sample &sample : samples)
    {
      mean_coordinate += sample.line_coordinate;
      mean_position += sample.position;
    }
  mean_coordinate /= samples.size ();
  mean_position /= samples.size ();

  long double sum_cc = 0;
  long double sum_cp = 0;
  for (const edge_sample &sample : samples)
    {
      long double dc = sample.line_coordinate - mean_coordinate;
      long double dp = sample.position - mean_position;
      sum_cc += dc * dc;
      sum_cp += dc * dp;
    }
  if (sum_cc <= std::numeric_limits<long double>::epsilon ())
    return false;

  *slope = (double)(sum_cp / sum_cc);
  *intercept = (double)(mean_position - *slope * mean_coordinate);
  return my_isfinite (*slope) && my_isfinite (*intercept);
}

/* Return true when SAMPLES span most of LINE_COUNT without a long unsupported
   gap.  Store a description in ERROR on failure.  */
static bool
edge_sample_coverage_p (const std::vector<edge_sample> &samples,
                        int line_count, std::string *error)
{
  if (samples.empty ())
    {
      *error = "no qualified edge centroids";
      return false;
    }

  std::vector<double> coordinates;
  coordinates.reserve (samples.size ());
  for (const edge_sample &sample : samples)
    coordinates.push_back (sample.line_coordinate);
  std::sort (coordinates.begin (), coordinates.end ());

  double required_span = 0.80 * (line_count - 1);
  double span = coordinates.back () - coordinates.front ();
  if (span < required_span)
    {
      *error = format_message (
          "the detected edge covers only %.1f of %d scan lines", span + 1,
          line_count);
      return false;
    }

  double max_gap = 0;
  for (size_t i = 1; i < coordinates.size (); i++)
    max_gap = std::max (max_gap, coordinates[i] - coordinates[i - 1]);
  double allowed_gap = std::max (4.0, std::ceil (0.10 * line_count));
  if (max_gap > allowed_gap)
    {
      *error = format_message (
          "the detected edge has an unsupported gap of %.0f scan lines",
          max_gap - 1);
      return false;
    }
  return true;
}

/* Estimate the full width at half maximum of the dominant gradient lobe in
   PIXELS for one edge orientation.  A short binomial low-pass suppresses
   pixel-phase and noise spikes before the width is measured.  Return zero when
   no usable scan-line gradients are available.  */
static double
estimate_edge_gradient_fwhm (const std::vector<double> &pixels, int width,
                             int height, bool vertical)
{
  const int normal_size = vertical ? width : height;
  const int line_count = vertical ? height : width;
  if (normal_size < 7 || line_count < 1)
    return 0;

  auto pixel = [&] (int normal, int line)
    {
      return vertical ? pixels[(size_t)line * width + normal]
                      : pixels[(size_t)normal * width + line];
    };

  static constexpr int lowpass_weights[5] = { 1, 4, 6, 4, 1 };
  std::vector<double> gradient (normal_size - 2);
  std::vector<double> filtered (normal_size - 2);
  std::vector<double> widths;
  widths.reserve (line_count);

  for (int line = 0; line < line_count; line++)
    {
      for (int normal = 1; normal < normal_size - 1; normal++)
        gradient[normal - 1]
            = pixel (normal + 1, line) - pixel (normal - 1, line);

      for (int i = 0; i < (int)gradient.size (); i++)
        {
          /* Binomial 1,4,6,4,1 smoothing, with endpoint replication.  This is
             used only to estimate transition scale; the eventual centroid and
             every qualification statistic still use the original samples.  */
          double sum = 0;
          for (int k = -2; k <= 2; k++)
            {
              int j = std::clamp (i + k, 0, (int)gradient.size () - 1);
              sum += lowpass_weights[k + 2] * gradient[j];
            }
          filtered[i] = sum / 16.0;
        }

      int peak = 0;
      double peak_strength = 0;
      for (int i = 0; i < (int)filtered.size (); i++)
        if (std::abs (filtered[i]) > peak_strength)
          {
            peak_strength = std::abs (filtered[i]);
            peak = i;
          }
      if (!my_isfinite (peak_strength) || peak_strength <= 1.0e-12)
        continue;

      const bool positive = filtered[peak] > 0;
      const double half_strength = peak_strength * 0.5;
      int first = peak;
      int last = peak;
      while (first > 0
             && ((filtered[first - 1] > 0) == positive)
             && std::abs (filtered[first - 1]) >= half_strength)
        first--;
      while (last + 1 < (int)filtered.size ()
             && ((filtered[last + 1] > 0) == positive)
             && std::abs (filtered[last + 1]) >= half_strength)
        last++;
      widths.push_back (last - first + 1);
    }

  return median_value (widths);
}

/* Detect a mostly vertical or mostly horizontal straight edge in PIXELS using
   CENTROID_RADIUS for dominant-polarity centroid support and SECONDARY_GUARD
   for excluding the principal gradient lobe from the competing-edge search.
   WIDTH and HEIGHT describe the local ROI buffer.  */
static edge_line_candidate
detect_edge_line_with_support (const std::vector<double> &pixels, int width,
                               int height, bool vertical, int centroid_radius,
                               int secondary_guard)
{
  edge_line_candidate result;
  result.vertical = vertical;

  int normal_size = vertical ? width : height;
  int line_count = vertical ? height : width;
  int minimum_samples
      = std::max (16, (int)std::ceil (min_candidate_line_fraction
                                     * line_count));

  auto pixel = [&] (int normal, int line)
    {
      return vertical ? pixels[(size_t)line * width + normal]
                      : pixels[(size_t)normal * width + line];
    };

  std::vector<edge_sample> candidates;
  candidates.reserve (line_count);
  std::vector<double> gradient (normal_size - 2);

  for (int line = 0; line < line_count; line++)
    {
      int maximum_index = 0;
      double maximum_gradient = 0;
      for (int normal = 1; normal < normal_size - 1; normal++)
        {
          double value
              = pixel (normal + 1, line) - pixel (normal - 1, line);
          gradient[normal - 1] = value;
          if (std::abs (value) > maximum_gradient)
            {
              maximum_gradient = std::abs (value);
              maximum_index = normal - 1;
            }
        }
      if (!my_isfinite (maximum_gradient) || maximum_gradient <= 1.0e-12)
        continue;

      int maximum_position = maximum_index + 1;
      double secondary_gradient = 0;
      for (int normal = 1; normal < normal_size - 1; normal++)
        if (std::abs (normal - maximum_position) > secondary_guard)
          secondary_gradient
              = std::max (secondary_gradient,
                          std::abs (gradient[normal - 1]));

      int first = std::max (1, maximum_position - centroid_radius);
      int last
          = std::min (normal_size - 2,
                      maximum_position + centroid_radius);
      double local_absolute_sum = 0;
      double local_signed_sum = 0;
      double centroid_weight_sum = 0;
      double weighted_position = 0;
      const bool positive_peak = gradient[maximum_index] > 0;
      for (int normal = first; normal <= last; normal++)
        {
          double value = gradient[normal - 1];
          double weight = std::abs (value);
          local_absolute_sum += weight;
          local_signed_sum += value;

          /* Broad defocus needs a wide centroid aperture, but real edges can
             have weak ringing of the opposite polarity inside that aperture.
             Such a lobe belongs to the same transition for the coherence
             check, but using it as positive centroid mass shifts the fitted
             line as the lobe strength changes from one scan line to another.
             Accumulate the centroid only from gradients matching the dominant
             peak polarity while retaining all gradients for qualification.  */
          if ((value > 0) == positive_peak)
            {
              centroid_weight_sum += weight;
              weighted_position += weight * normal;
            }
        }
      if (!my_isfinite (local_absolute_sum)
          || !my_isfinite (centroid_weight_sum)
          || local_absolute_sum <= 1.0e-12
          || centroid_weight_sum <= 1.0e-12)
        continue;

      double secondary_ratio = secondary_gradient / maximum_gradient;
      double coherence = std::abs (local_signed_sum) / local_absolute_sum;
      if (secondary_ratio > max_secondary_peak_ratio
          || coherence < min_local_gradient_coherence
          || local_signed_sum == 0)
        continue;

      candidates.push_back (
          {(double)line, weighted_position / centroid_weight_sum,
           local_signed_sum});
    }

  result.qualified_samples = candidates.size ();
  if ((int)candidates.size () < minimum_samples)
    {
      result.error = format_message (
          "only %zu of %d scan lines contain one dominant %s edge",
          candidates.size (), line_count, vertical ? "vertical" : "horizontal");
      return result;
    }

  int positive = 0;
  for (const edge_sample &sample : candidates)
    positive += sample.signed_strength > 0;
  int negative = candidates.size () - positive;
  bool positive_polarity = positive >= negative;
  int dominant = std::max (positive, negative);
  double polarity_fraction = (double)dominant / candidates.size ();
  if (polarity_fraction < min_dominant_polarity_fraction)
    {
      result.error = format_message (
          "%s edge polarity is inconsistent across the ROI (%.1f%% agree)",
          vertical ? "vertical" : "horizontal",
          polarity_fraction * 100);
      return result;
    }

  std::vector<edge_sample> samples;
  samples.reserve (dominant);
  for (const edge_sample &sample : candidates)
    if ((sample.signed_strength > 0) == positive_polarity)
      samples.push_back (sample);

  if (!edge_sample_coverage_p (samples, line_count, &result.error))
    return result;

  /* Use medians in coordinate bands to initialize a deterministic Theil-Sen
     fit.  Isolated dust spots can otherwise pull an ordinary least-squares
     line far enough that residual clipping rejects the real edge.  */
  int band_count = std::clamp (line_count / 16, 4, 16);
  std::vector<std::vector<double>> band_coordinates (band_count);
  std::vector<std::vector<double>> band_positions (band_count);
  for (const edge_sample &sample : samples)
    {
      int band = std::min (
          band_count - 1,
          (int)(sample.line_coordinate * band_count / line_count));
      band_coordinates[band].push_back (sample.line_coordinate);
      band_positions[band].push_back (sample.position);
    }

  std::vector<double> representative_coordinates;
  std::vector<double> representative_positions;
  for (int band = 0; band < band_count; band++)
    if (!band_positions[band].empty ())
      {
        representative_coordinates.push_back (
            median_value (band_coordinates[band]));
        representative_positions.push_back (
            median_value (band_positions[band]));
      }

  std::vector<double> slopes;
  for (size_t i = 0; i < representative_positions.size (); i++)
    for (size_t j = i + 1; j < representative_positions.size (); j++)
      {
        double delta
            = representative_coordinates[j] - representative_coordinates[i];
        if (delta > 0)
          slopes.push_back (
              (representative_positions[j] - representative_positions[i])
              / delta);
      }
  if (slopes.empty ())
    {
      result.failure = slanted_edge_failure_nonlinear_edge;
      result.error = "edge centroids do not span enough independent rows";
      return result;
    }

  double slope = median_value (slopes);
  std::vector<double> intercepts;
  intercepts.reserve (samples.size ());
  for (const edge_sample &sample : samples)
    intercepts.push_back (
        sample.position - slope * sample.line_coordinate);
  double intercept = median_value (intercepts);

  std::vector<double> residuals;
  residuals.reserve (samples.size ());
  for (const edge_sample &sample : samples)
    residuals.push_back (
        sample.position - slope * sample.line_coordinate - intercept);
  intercept += median_value (residuals);

  std::vector<double> absolute_residuals;
  absolute_residuals.reserve (samples.size ());
  for (const edge_sample &sample : samples)
    absolute_residuals.push_back (
        std::abs (sample.position - slope * sample.line_coordinate
                  - intercept));
  double robust_sigma = 1.4826 * median_value (absolute_residuals);
  double inlier_limit = std::clamp (4 * robust_sigma, 0.50, 1.50);

  std::vector<edge_sample> inliers;
  inliers.reserve (samples.size ());
  for (const edge_sample &sample : samples)
    if (std::abs (sample.position - slope * sample.line_coordinate
                  - intercept)
        <= inlier_limit)
      inliers.push_back (sample);

  int minimum_inliers
      = std::max (16, (int)std::ceil (min_candidate_line_fraction
                                     * line_count));
  if ((int)inliers.size () < minimum_inliers
      || inliers.size ()
             < (size_t)std::ceil (min_inlier_candidate_fraction
                                 * samples.size ()))
    {
      result.failure = slanted_edge_failure_nonlinear_edge;
      result.error = format_message (
          "only %zu of %zu edge centroids lie on one straight line",
          inliers.size (), samples.size ());
      return result;
    }
  if (!edge_sample_coverage_p (inliers, line_count, &result.error))
    {
      result.failure = slanted_edge_failure_nonlinear_edge;
      return result;
    }

  if (!least_squares_line (inliers, &slope, &intercept))
    {
      result.failure = slanted_edge_failure_nonlinear_edge;
      result.error = "edge-line regression is numerically degenerate";
      return result;
    }

  absolute_residuals.clear ();
  long double squared_residual_sum = 0;
  for (const edge_sample &sample : inliers)
    {
      double residual
          = sample.position - slope * sample.line_coordinate - intercept;
      squared_residual_sum += (long double)residual * residual;
      absolute_residuals.push_back (std::abs (residual));
    }
  double fit_rms
      = std::sqrt ((double)(squared_residual_sum / inliers.size ()));
  double fit_p95 = percentile_value (absolute_residuals, 0.95);
  if (!my_isfinite (fit_rms) || !my_isfinite (fit_p95)
      || fit_rms > max_edge_fit_rms || fit_p95 > max_edge_fit_p95)
    {
      result.failure = slanted_edge_failure_nonlinear_edge;
      result.error = format_message (
          "edge is not straight enough (RMS %.3f px, 95th percentile %.3f px)",
          fit_rms, fit_p95);
      return result;
    }

  double angle = std::atan (std::abs (slope)) * 180.0 / M_PI;
  double phase_motion = std::abs (slope) * (line_count - 1);
  if (angle > max_edge_angle_degrees)
    {
      result.failure = slanted_edge_failure_unsuitable_angle;
      result.error = format_message (
          "%s edge angle %.2f degrees exceeds the supported %.0f degrees",
          vertical ? "vertical" : "horizontal", angle,
          max_edge_angle_degrees);
      return result;
    }
  if (phase_motion < min_edge_phase_motion)
    {
      result.failure = slanted_edge_failure_unsuitable_angle;
      result.error = format_message (
          "%s edge moves only %.3f pixel across the ROI; use a more slanted "
          "edge or a longer ROI",
          vertical ? "vertical" : "horizontal", phase_motion);
      return result;
    }

  result.valid = true;
  result.failure = slanted_edge_failure_none;
  result.slope = slope;
  result.intercept = intercept;
  result.fit_rms = fit_rms;
  result.fit_p95 = fit_p95;
  result.angle_degrees = angle;
  return result;
}

/* Detect one edge orientation.  Preserve the established compact detector for
   every ROI it already accepts.  If it rejects the geometry, estimate the
   transition width and retry only when that width calls for a larger principal
   lobe.  This keeps sharp-edge behavior unchanged while preventing a single
   very defocused edge from being mistaken for several competing edges merely
   because its gradient remains strong outside the fixed guard.  */
static edge_line_candidate
detect_edge_line (const std::vector<double> &pixels, int width, int height,
                  bool vertical)
{
  edge_line_candidate compact
      = detect_edge_line_with_support (pixels, width, height, vertical,
                                       edge_centroid_radius,
                                       secondary_peak_guard);
  if (compact.valid)
    return compact;

  const double fwhm
      = estimate_edge_gradient_fwhm (pixels, width, height, vertical);
  const int normal_size = vertical ? width : height;
  if (!my_isfinite (fwhm) || fwhm <= 0
      || fwhm > max_adaptive_edge_width_fraction * normal_size)
    return compact;

  const int maximum_radius
      = std::max (edge_centroid_radius,
                  std::min (max_adaptive_edge_centroid_radius,
                            std::max (1, (normal_size - 4) / 2)));
  const int centroid_radius
      = std::clamp (
          (int)std::ceil (adaptive_centroid_fwhm_factor * fwhm),
          edge_centroid_radius, maximum_radius);
  const int secondary_guard
      = std::clamp (
          (int)std::ceil (adaptive_secondary_guard_fwhm_factor * fwhm)
              + adaptive_secondary_guard_padding,
          secondary_peak_guard, maximum_radius);

  if (centroid_radius == edge_centroid_radius
      && secondary_guard == secondary_peak_guard)
    return compact;

  edge_line_candidate adaptive
      = detect_edge_line_with_support (pixels, width, height, vertical,
                                       centroid_radius, secondary_guard);
  adaptive.adaptive_scale = true;
  adaptive.gradient_fwhm = fwhm;
  if (adaptive.valid
      || adaptive.qualified_samples > compact.qualified_samples)
    return adaptive;
  return compact;
}

/* Return a box-filtered copy of INPUT using an edge-replicated WINDOW-sample
   aperture.  */
static std::vector<double>
box_filter (const std::vector<double> &input, int window)
{
  if (input.empty ())
    return {};
  window = std::clamp (window, 1, (int)input.size ());
  int left_radius = window / 2;
  int right_radius = window - left_radius - 1;

  std::vector<double> prefix (input.size () + 1, 0);
  for (size_t i = 0; i < input.size (); i++)
    prefix[i + 1] = prefix[i] + input[i];

  std::vector<double> result (input.size ());
  for (int i = 0; i < (int)input.size (); i++)
    {
      int first = i - left_radius;
      int last = i + right_radius;
      int inside_first = std::max (0, first);
      int inside_last = std::min ((int)input.size () - 1, last);
      double sum = prefix[inside_last + 1] - prefix[inside_first];
      if (first < 0)
        sum += -first * input.front ();
      if (last >= (int)input.size ())
        sum += (last - (int)input.size () + 1) * input.back ();
      result[i] = sum / window;
    }
  return result;
}

/* Return robust median and Gaussian-equivalent MAD estimates for VALUES in
   MEDIAN and SIGMA.  */
static void
robust_location_scale (const std::vector<double> &values, double *median,
                       double *sigma)
{
  *median = median_value (values);
  std::vector<double> deviations;
  deviations.reserve (values.size ());
  for (double value : values)
    deviations.push_back (std::abs (value - *median));
  *sigma = 1.4826 * median_value (deviations);
}

} // anonymous namespace


/* Measure the spatial-frequency response of one slanted edge in ROI using
   PARAMS and return the qualified curve in the result.  RPARAM is read-only:
   callers may measure several channels first and commit them atomically.  */
slanted_edge_results
slanted_edge_mtf (const render_parameters &rparam, const image_data &img,
                  int_image_area roi, const slanted_edge_parameters &params,
                  progress_info *progress)
{
  slanted_edge_results res;

  int oversampling = params.oversampling > 0
                     ? params.oversampling
                     : default_slanted_edge_oversampling;
  if (oversampling < 2 || oversampling > max_slanted_edge_oversampling)
    {
      set_failure (
          &res, slanted_edge_failure_invalid_parameters, progress,
          format_message ("oversampling %d is outside the supported range "
                          "2..%d",
                          oversampling, max_slanted_edge_oversampling));
      return res;
    }
  if (params.channel < -1 || params.channel > 3)
    {
      set_failure (&res, slanted_edge_failure_invalid_parameters, progress,
                   "MTF measurement channel must be image layer, red, green, "
                   "blue, or infrared");
      return res;
    }
  if (params.channel >= 0 && params.channel <= 2 && !img.has_rgb ())
    {
      set_failure (&res, slanted_edge_failure_invalid_parameters, progress,
                   "requested RGB MTF channel is not present in the image");
      return res;
    }
  if (params.channel == 3 && !img.has_grayscale_or_ir ())
    {
      set_failure (&res, slanted_edge_failure_invalid_parameters, progress,
                   "requested infrared MTF channel is not present in the image");
      return res;
    }
  if (!my_isfinite (params.lsf_half_width) || params.lsf_half_width < 0)
    {
      set_failure (
          &res, slanted_edge_failure_invalid_parameters, progress,
          "LSF half-width must be finite and nonnegative");
      return res;
    }

  roi = roi.intersect (int_image_area (0, 0, img.width, img.height));
  if (roi.empty_p () || roi.width < 24 || roi.height < 24)
    {
      set_failure (
          &res, slanted_edge_failure_invalid_roi, progress,
          "ROI must contain at least 24 by 24 image pixels");
      return res;
    }

  double required_margin
      = std::max (4.0, params.lsf_half_width > 0
                           ? params.lsf_half_width + 1.0
                           : 4.0);
  if (roi.width < 2 * required_margin + 5
      || roi.height < 2 * required_margin + 5)
    {
      set_failure (
          &res, slanted_edge_failure_invalid_roi, progress,
          format_message (
              "ROI is too narrow for %.1f-pixel plateaus on both sides",
              required_margin));
      return res;
    }

  /* Measure the input image, not an image already modified by unsharp masking
     or deconvolution.  Preserve linearization and backlight correction while
     disabling sharpening only in the private render used for measurement.  */
  render_parameters measurement_rparam = rparam;
  measurement_rparam.sharpen.mode = sharpen_parameters::none;
  /* CHANNEL 3 explicitly requests the native grayscale/IR plane even when
     normal rendering is configured to synthesize the image layer from RGB.  */
  if (params.channel == 3)
    measurement_rparam.ignore_infrared = false;
  render r (img, measurement_rparam, 65535);
  const int precompute_flags
      = params.channel < 0 || params.channel == 3 ? PRECOMPUTE_IMAGE_LAYER
                                                  : PRECOMPUTE_NONE;
  if (!r.precompute_all (precompute_flags, {1, 1, 1}, progress))
    {
      set_failure (&res, slanted_edge_failure_precomputation, progress,
                   "image precomputation failed");
      return res;
    }

  size_t pixel_count = (size_t)roi.width * roi.height;
  if (roi.height && pixel_count / roi.height != (size_t)roi.width)
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "ROI pixel count overflows address space");
      return res;
    }

  std::vector<double> pixels;
  try
    {
      pixels.resize (pixel_count);
    }
  catch (const std::bad_alloc &)
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "not enough memory to analyze the selected ROI");
      return res;
    }

  for (int y = 0; y < roi.height; y++)
    for (int x = 0; x < roi.width; x++)
      {
        const int_point_t pos = {roi.x + x, roi.y + y};
        double value;
        if (params.channel < 0 || params.channel == 3)
          value = r.get_unadjusted_data (pos);
        else
          {
            const rgbdata rgb = r.get_unadjusted_rgb_pixel (pos);
            value = params.channel == 0
                        ? rgb.red
                        : params.channel == 1 ? rgb.green : rgb.blue;
          }
        if (!my_isfinite (value))
          {
            set_failure (
                &res, slanted_edge_failure_invalid_numerics, progress,
                "ROI contains a non-finite rendered pixel");
            return res;
          }
        pixels[(size_t)y * roi.width + x] = value;
      }

  long double sum_gx = 0;
  long double sum_gy = 0;
  for (int y = 1; y < roi.height - 1; y++)
    for (int x = 1; x < roi.width - 1; x++)
      {
        sum_gx += std::abs (
            pixels[(size_t)y * roi.width + x + 1]
            - pixels[(size_t)y * roi.width + x - 1]);
        sum_gy += std::abs (
            pixels[(size_t)(y + 1) * roi.width + x]
            - pixels[(size_t)(y - 1) * roi.width + x]);
      }

  edge_line_candidate vertical
      = detect_edge_line (pixels, roi.width, roi.height, true);
  edge_line_candidate horizontal
      = detect_edge_line (pixels, roi.width, roi.height, false);

  if (progress)
    fprintf (stderr,
             "Slanted-edge: gradient sums gx=%.6Lg, gy=%.6Lg; vertical "
             "candidate %s, horizontal candidate %s\n",
             sum_gx, sum_gy, vertical.valid ? "valid" : "rejected",
             horizontal.valid ? "valid" : "rejected");

  edge_line_candidate edge;
  if (vertical.valid && horizontal.valid)
    {
      set_failure (
          &res, slanted_edge_failure_no_single_edge, progress,
          "ROI contains strong straight edges in both directions; select one "
          "isolated edge");
      return res;
    }
  if (vertical.valid)
    edge = vertical;
  else if (horizontal.valid)
    edge = horizontal;
  else
    {
      const edge_line_candidate &best
          = vertical.qualified_samples >= horizontal.qualified_samples
                ? vertical
                : horizontal;
      slanted_edge_failure reason
          = best.failure == slanted_edge_failure_none
                ? slanted_edge_failure_no_single_edge
                : best.failure;
      set_failure (
          &res, reason, progress,
          format_message (
              "no usable single straight edge was found (vertical: %s; "
              "horizontal: %s)",
              vertical.error.c_str (), horizontal.error.c_str ()));
      return res;
    }

  int normal_size = edge.vertical ? roi.width : roi.height;
  int line_count = edge.vertical ? roi.height : roi.width;
  double first_position = edge.intercept;
  double last_position
      = edge.slope * (line_count - 1) + edge.intercept;
  double minimum_position = std::min (first_position, last_position);
  double maximum_position = std::max (first_position, last_position);
  if (minimum_position < required_margin
      || maximum_position > normal_size - 1 - required_margin)
    {
      set_failure (
          &res, slanted_edge_failure_edge_near_boundary, progress,
          format_message (
              "detected edge approaches within %.2f pixels of an ROI boundary; "
              "leave plateaus of at least %.1f pixels on both sides",
              std::min (minimum_position,
                        normal_size - 1 - maximum_position),
              required_margin));
      return res;
    }

  res.edge_angle = edge.angle_degrees;
  res.edge_fit_rms = edge.fit_rms;
  if (progress)
    fprintf (stderr,
             "Slanted-edge: selected %s edge, angle %.3f degrees, fit RMS "
             "%.4f pixels, p95 %.4f pixels\n",
             edge.vertical ? "vertical" : "horizontal", edge.angle_degrees,
             edge.fit_rms, edge.fit_p95);

  double a;
  double b;
  double c;
  if (edge.vertical)
    {
      a = 1.0;
      b = -edge.slope;
      c = -edge.intercept;
    }
  else
    {
      a = -edge.slope;
      b = 1.0;
      c = -edge.intercept;
    }
  double norm = std::hypot (a, b);
  if (!my_isfinite (norm) || norm <= 0)
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "fitted edge has an invalid normal vector");
      return res;
    }
  a /= norm;
  b /= norm;
  c /= norm;

  auto distance = [&] (int x, int y) { return a * x + b * y + c; };
  double corner_distances[4]
      = {distance (0, 0), distance (roi.width - 1, 0),
         distance (0, roi.height - 1),
         distance (roi.width - 1, roi.height - 1)};
  double min_d
      = *std::min_element (corner_distances, corner_distances + 4);
  double max_d
      = *std::max_element (corner_distances, corner_distances + 4);

  double num_bins_d = std::ceil ((max_d - min_d) * oversampling) + 1.0;
  if (!my_isfinite (num_bins_d) || num_bins_d < 4
      || num_bins_d > max_slanted_edge_bins)
    {
      set_failure (
          &res, slanted_edge_failure_invalid_numerics, progress,
          format_message ("invalid or excessive ESF size %.0f", num_bins_d));
      return res;
    }
  int num_bins = (int)num_bins_d;

  std::vector<double> esf_sum;
  std::vector<int> esf_count;
  try
    {
      esf_sum.assign (num_bins, 0);
      esf_count.assign (num_bins, 0);
    }
  catch (const std::bad_alloc &)
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "not enough memory for the supersampled ESF");
      return res;
    }

  for (int y = 0; y < roi.height; y++)
    for (int x = 0; x < roi.width; x++)
      {
        double d = distance (x, y);
        int bin = (int)std::llround ((d - min_d) * oversampling);
        if (bin >= 0 && bin < num_bins)
          {
            esf_sum[bin] += pixels[(size_t)y * roi.width + x];
            esf_count[bin]++;
          }
      }

  int occupied_bins = 0;
  int longest_empty_run = 0;
  int current_empty_run = 0;
  std::vector<bool> occupied_phases (oversampling, false);
  for (int bin = 0; bin < num_bins; bin++)
    if (esf_count[bin])
      {
        occupied_bins++;
        occupied_phases[bin % oversampling] = true;
        current_empty_run = 0;
      }
    else
      {
        current_empty_run++;
        longest_empty_run
            = std::max (longest_empty_run, current_empty_run);
      }
  int occupied_phase_count
      = std::count (occupied_phases.begin (), occupied_phases.end (), true);
  double occupied_fraction = (double)occupied_bins / num_bins;
  res.phase_coverage = occupied_fraction;
  int minimum_phases
      = std::max (2, (int)std::ceil (0.70 * oversampling));
  int maximum_empty_run = std::max (2, oversampling / 2);
  if (occupied_phase_count < minimum_phases || occupied_fraction < 0.65
      || longest_empty_run > maximum_empty_run)
    {
      set_failure (
          &res, slanted_edge_failure_phase_coverage, progress,
          format_message (
              "insufficient subpixel phase coverage: %d/%d phases, %.1f%% "
              "populated ESF bins, longest empty run %d",
              occupied_phase_count, oversampling, occupied_fraction * 100,
              longest_empty_run));
      return res;
    }

  std::vector<double> esf;
  if (!interpolate_esf (esf_sum, esf_count, &esf))
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "ESF contains no measured samples");
      return res;
    }

  /* Estimate plateau noise directly from pixels well away from the fitted
     transition.  Limit the vectors to a deterministic sample so a mistakenly
     huge ROI cannot consume excessive memory merely for robust statistics.  */
  double span = max_d - min_d;
  double negative_limit = min_d + 0.25 * span;
  double positive_limit = max_d - 0.25 * span;
  size_t sample_stride = std::max ((size_t)1, pixel_count / 131072);
  std::vector<double> negative_plateau;
  std::vector<double> positive_plateau;
  negative_plateau.reserve (
      std::min ((size_t)65536, pixel_count / 4 + 1));
  positive_plateau.reserve (
      std::min ((size_t)65536, pixel_count / 4 + 1));
  size_t linear_index = 0;
  for (int y = 0; y < roi.height; y++)
    for (int x = 0; x < roi.width; x++, linear_index++)
      if (!(linear_index % sample_stride))
        {
          double d = distance (x, y);
          double value = pixels[(size_t)y * roi.width + x];
          if (d <= negative_limit)
            negative_plateau.push_back (value);
          else if (d >= positive_limit)
            positive_plateau.push_back (value);
        }

  if (negative_plateau.size () < 64 || positive_plateau.size () < 64)
    {
      set_failure (&res, slanted_edge_failure_invalid_roi, progress,
                   "ROI does not contain enough pixels on both edge plateaus");
      return res;
    }

  double negative_level;
  double positive_level;
  double negative_sigma;
  double positive_sigma;
  robust_location_scale (negative_plateau, &negative_level, &negative_sigma);
  robust_location_scale (positive_plateau, &positive_level, &positive_sigma);
  double contrast = std::abs (positive_level - negative_level);
  double noise = std::hypot (negative_sigma, positive_sigma);
  double snr = contrast / std::max (noise, 1.0e-12);
  res.edge_contrast = contrast;
  res.edge_snr = snr;
  if (!my_isfinite (contrast) || !my_isfinite (snr)
      || contrast < 1.0e-4 || snr < min_plateau_snr)
    {
      set_failure (
          &res, slanted_edge_failure_low_contrast, progress,
          format_message (
              "edge plateaus have contrast %.6g and SNR %.2f; require at "
              "least 0.0001 contrast and SNR %.0f",
              contrast, snr, min_plateau_snr));
      return res;
    }

  /* Smooth only a validation copy of the ESF.  Two one-pixel box passes
     suppress bin-to-bin phase noise while preserving broad optical wings.
     The measured MTF below continues to use the original ESF.  */
  std::vector<double> qualified_esf
      = box_filter (box_filter (esf, oversampling), oversampling);
  int plateau_bins
      = std::min (num_bins / 4, std::max (oversampling, num_bins / 10));
  double left_level = 0;
  double right_level = 0;
  for (int bin = 0; bin < plateau_bins; bin++)
    {
      left_level += qualified_esf[bin];
      right_level += qualified_esf[num_bins - 1 - bin];
    }
  left_level /= plateau_bins;
  right_level /= plateau_bins;
  double qualified_contrast = std::abs (right_level - left_level);
  double total_variation = 0;
  for (int bin = 1; bin < num_bins; bin++)
    total_variation
        += std::abs (qualified_esf[bin] - qualified_esf[bin - 1]);
  double monotonicity
      = qualified_contrast / std::max (total_variation, 1.0e-300);

  std::vector<double> qualified_lsf (num_bins, 0);
  qualified_lsf[0] = qualified_esf[1] - qualified_esf[0];
  for (int bin = 1; bin < num_bins - 1; bin++)
    qualified_lsf[bin]
        = (qualified_esf[bin + 1] - qualified_esf[bin - 1]) / 2.0;
  qualified_lsf[num_bins - 1]
      = qualified_esf[num_bins - 1] - qualified_esf[num_bins - 2];

  /* The geometric line fit already locates the intended edge at signed
     distance zero.  Search the validation LSF only near that position.  A
     distant dust mark, illumination boundary, or noisy ROI endpoint must not
     replace the fitted edge merely because its local derivative is larger.

     For a broad transition the LSF maximum is intrinsically flat: binning and
     phase noise can move the highest sampled bin by more than the sharp-edge
     1.5-pixel tolerance even when the derivative centroid is stable.  Scale
     that tolerance only for a localized broad lobe, and keep the historical
     value for sharp edges and illumination ramps.  */
  const double edge_gradient_fwhm
      = edge.adaptive_scale ? edge.gradient_fwhm : 0;
  double peak_offset_limit = max_lsf_peak_offset;
  if (edge.adaptive_scale && my_isfinite (edge_gradient_fwhm)
      && edge_gradient_fwhm > 0)
    peak_offset_limit
        = std::max (peak_offset_limit,
                    adaptive_peak_offset_fwhm_factor * edge_gradient_fwhm);

  const int expected_peak_idx
      = (int)std::llround (-min_d * oversampling);
  const int peak_search_radius
      = std::max (1, (int)std::ceil (peak_offset_limit * oversampling));
  const int peak_search_begin
      = std::max (0, expected_peak_idx - peak_search_radius);
  const int peak_search_end
      = std::min (num_bins - 1, expected_peak_idx + peak_search_radius);
  int peak_idx = std::clamp (expected_peak_idx, 0, num_bins - 1);
  double peak_magnitude = 0;
  double total_lsf_energy = 0;
  for (int bin = 0; bin < num_bins; bin++)
    {
      double magnitude = std::abs (qualified_lsf[bin]);
      total_lsf_energy += magnitude;
      if (bin >= peak_search_begin && bin <= peak_search_end
          && magnitude > peak_magnitude)
        {
          peak_magnitude = magnitude;
          peak_idx = bin;
        }
    }
  /* A six-pixel primary LSF window is a useful rejection guard for sharp
     edges, but it must not define the maximum measurable blur.  The adaptive
     geometry path already carries its measured lobe width.  A moderately broad
     edge can still pass compact geometry and fail only this energy check, so
     retry the energy window (and only the energy window) with a localized FWHM
     estimate if the historical six-pixel test fails.  This leaves geometry,
     peak selection, and the measured curve unchanged for compact-valid edges.  */
  int primary_radius_pixels = 6;
  if (edge.adaptive_scale && my_isfinite (edge_gradient_fwhm)
      && edge_gradient_fwhm > 0)
    primary_radius_pixels
        = std::max (primary_radius_pixels,
                    (int)std::ceil (adaptive_primary_lsf_fwhm_factor
                                    * edge_gradient_fwhm));

  auto primary_lsf_fraction = [&] (int radius_pixels)
    {
      const int radius = radius_pixels * oversampling;
      double energy = 0;
      for (int bin = std::max (0, peak_idx - radius);
           bin <= std::min (num_bins - 1, peak_idx + radius); bin++)
        energy += std::abs (qualified_lsf[bin]);
      return energy / std::max (total_lsf_energy, 1.0e-300);
    };

  double primary_fraction = primary_lsf_fraction (primary_radius_pixels);
  if (!edge.adaptive_scale
      && primary_fraction < min_primary_lsf_fraction)
    {
      const double validation_fwhm
          = estimate_edge_gradient_fwhm (pixels, roi.width, roi.height,
                                         edge.vertical);
      if (my_isfinite (validation_fwhm) && validation_fwhm > 0
          && validation_fwhm
                 <= max_adaptive_edge_width_fraction * normal_size)
        {
          const int broad_radius_pixels
              = std::max (primary_radius_pixels,
                          (int)std::ceil (adaptive_primary_lsf_fwhm_factor
                                          * validation_fwhm));
          if (broad_radius_pixels > primary_radius_pixels)
            primary_fraction = primary_lsf_fraction (broad_radius_pixels);
        }
    }
  double peak_offset = min_d + (double)peak_idx / oversampling;

  if (!my_isfinite (monotonicity) || !my_isfinite (primary_fraction)
      || !my_isfinite (peak_offset)
      || monotonicity < min_esf_monotonicity
      || primary_fraction < min_primary_lsf_fraction
      || std::abs (peak_offset) > peak_offset_limit)
    {
      set_failure (
          &res, slanted_edge_failure_unstable_esf, progress,
          format_message (
              "ESF is not one stable transition (monotonicity %.3f, primary "
              "LSF fraction %.3f, peak offset %.3f px)",
              monotonicity, primary_fraction, peak_offset));
      return res;
    }

  if (progress)
    fprintf (stderr,
             "Slanted-edge: contrast %.6g, plateau SNR %.2f, phase coverage "
             "%.1f%%, ESF monotonicity %.3f, primary LSF %.3f\n",
             contrast, snr, occupied_fraction * 100, monotonicity,
             primary_fraction);

  double min_esf = *std::min_element (esf.begin (), esf.end ());
  double max_esf = *std::max_element (esf.begin (), esf.end ());
  if (progress)
    fprintf (stderr,
             "Slanted-edge: ESF range [%.6g, %.6g], origin %.4f, step %.4f\n",
             min_esf, max_esf, min_d, 1.0 / oversampling);

  int N = 1;
  while (N < num_bins)
    {
      if (N > INT_MAX / 2)
        {
          set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                       "FFT length overflows integer range");
          return res;
        }
      N <<= 1;
    }
  if (N > INT_MAX / 2)
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "zero-padded FFT length overflows integer range");
      return res;
    }
  N <<= 1;

  int support_half_bins = 0;
  if (params.lsf_half_width > 0)
    {
      support_half_bins
          = std::max (1, (int)std::llround (params.lsf_half_width
                                           * oversampling));
      if (peak_idx - support_half_bins < 0
          || peak_idx + support_half_bins >= num_bins)
        {
          set_failure (
              &res, slanted_edge_failure_edge_near_boundary, progress,
              format_message (
                  "requested %.6g-pixel LSF half-width does not fit in the "
                  "qualified edge ROI",
                  params.lsf_half_width));
          return res;
        }
    }

  if (progress)
    fprintf (stderr,
             "Slanted-edge: num_bins=%d, oversampling=%d, FFT=%d, LSF support "
             "%s, half-width %.6g pixels, window %s\n",
             num_bins, oversampling, N,
             support_half_bins ? "finite" : "full ROI",
             params.lsf_half_width,
             params.window == slanted_edge_parameters::window_hann
                 ? "Hann"
                 : params.window == slanted_edge_parameters::window_hamming
                       ? "Hamming"
                       : "rectangular");

  std::vector<double> mtf;
  if (!compute_mtf_curve (esf, peak_idx, support_half_bins, params,
                          oversampling, N, &mtf))
    {
      set_failure (&res, slanted_edge_failure_invalid_numerics, progress,
                   "MTF normalization or correction is invalid");
      return res;
    }

  /* Estimate frequency-dependent random uncertainty by recomputing the MTF
     from four interleaved groups of scan lines.  The edge geometry, window
     and FFT grid remain fixed, so the scatter measures image noise and
     sensitivity to subpixel phase coverage rather than changing the reported
     MTF estimator.  Each group contains about one quarter of the samples;
     divide the between-group standard deviation by sqrt(4) to estimate the
     standard error of the full-ROI curve.  Small ROIs keep uncertainty zero,
     which deliberately requests the historical uniform fit.  */
  constexpr int uncertainty_subsets = 4;
  std::vector<std::vector<double>> subset_mtfs;
  const int uncertainty_line_count = edge.vertical ? roi.height : roi.width;
  if (uncertainty_line_count >= 32)
    for (int subset = 0; subset < uncertainty_subsets; subset++)
      {
        std::vector<double> subset_sum (num_bins, 0);
        std::vector<int> subset_count (num_bins, 0);
        for (int y = 0; y < roi.height; y++)
          for (int x = 0; x < roi.width; x++)
            {
              const int line = edge.vertical ? y : x;
              if (line % uncertainty_subsets != subset)
                continue;
              const double d = distance (x, y);
              const int bin
                  = (int)std::llround ((d - min_d) * oversampling);
              if (bin >= 0 && bin < num_bins)
                {
                  subset_sum[bin]
                      += pixels[(size_t)y * roi.width + x];
                  subset_count[bin]++;
                }
            }

        int occupied_subset_bins = 0;
        std::vector<bool> subset_phases (oversampling, false);
        for (int bin = 0; bin < num_bins; bin++)
          if (subset_count[bin])
            {
              occupied_subset_bins++;
              subset_phases[bin % oversampling] = true;
            }
        const int subset_phase_count
            = std::count (subset_phases.begin (), subset_phases.end (), true);
        if (subset_phase_count < 2
            || occupied_subset_bins < std::max (4, num_bins / 10))
          continue;

        std::vector<double> subset_esf;
        std::vector<double> subset_mtf;
        if (interpolate_esf (subset_sum, subset_count, &subset_esf)
            && compute_mtf_curve (subset_esf, peak_idx, support_half_bins,
                                  params, oversampling, N, &subset_mtf)
            && subset_mtf.size () == mtf.size ())
          subset_mtfs.push_back (std::move (subset_mtf));
      }

  std::vector<double> mtf_uncertainty (mtf.size (), 0);
  if (subset_mtfs.size () >= 3)
    {
      for (size_t i = 0; i < mtf.size (); i++)
        {
          long double mean = 0;
          for (const std::vector<double> &curve : subset_mtfs)
            mean += curve[i];
          mean /= subset_mtfs.size ();
          long double sum_squared = 0;
          for (const std::vector<double> &curve : subset_mtfs)
            {
              const long double delta = curve[i] - mean;
              sum_squared += delta * delta;
            }
          const long double subset_variance
              = sum_squared / (subset_mtfs.size () - 1);
          mtf_uncertainty[i]
              = my_sqrt ((double)(subset_variance / subset_mtfs.size ()));
        }

      /* Adjacent zero-padded FFT bins are strongly correlated.  Smooth the
         noisy four-way variance estimate over +/-0.01 cycles/pixel using an
         RMS average; this changes only fit weights, never the measured MTF.  */
      const int smoothing_radius
          = std::max (1, (int)std::ceil (0.01 * N / oversampling));
      std::vector<double> smoothed (mtf_uncertainty.size (), 0);
      for (size_t i = 0; i < mtf_uncertainty.size (); i++)
        {
          const int first
              = std::max (0, (int)i - smoothing_radius);
          const int last
              = std::min ((int)mtf_uncertainty.size () - 1,
                          (int)i + smoothing_radius);
          long double sum_squared = 0;
          for (int j = first; j <= last; j++)
            sum_squared += (long double)mtf_uncertainty[j]
                           * mtf_uncertainty[j];
          smoothed[i]
              = my_sqrt ((double)(sum_squared / (last - first + 1)));
        }
      mtf_uncertainty.swap (smoothed);
    }

  mtf_measurement measurement;
  measurement.name
      = params.name.empty () ? "Slanted edge MTF" : params.name;
  measurement.channel = params.channel;
  measurement.image_layer = params.channel < 0;
  measurement.wavelength = params.wavelength;
  measurement.same_capture = params.same_capture;

  double largest_mtf = 0;
  for (size_t index = 0; index < mtf.size (); index++)
    {
      const double frequency = (double)index * oversampling / N;
      const double mtf_value = mtf[index];
      if (index)
        largest_mtf = std::max (largest_mtf, mtf_value);
      measurement.add_value (frequency, mtf_value * 100.0,
                             mtf_uncertainty[index] * 100.0);
    }

  if (progress && subset_mtfs.size () >= 3)
    {
      std::vector<double> positive_uncertainties;
      for (double uncertainty : mtf_uncertainty)
        if (uncertainty > 0 && my_isfinite (uncertainty))
          positive_uncertainties.push_back (uncertainty * 100.0);
      if (!positive_uncertainties.empty ())
        fprintf (stderr,
                 "Slanted-edge: uncertainty estimated from %zu interleaved "
                 "subsets, median 1-sigma %.3f percentage points\n",
                 subset_mtfs.size (),
                 median_value (std::move (positive_uncertainties)));
    }

  if (largest_mtf > max_normalized_mtf)
    {
      set_failure (
          &res, slanted_edge_failure_nonphysical_mtf, progress,
          format_message (
              "normalized MTF reaches %.1f%%; the ROI is dominated by noise, "
              "texture, or multiple transitions",
              largest_mtf * 100));
      return res;
    }

  if (edge.vertical)
    {
      res.edge_p1
          = {(coord_t)(roi.x + edge.intercept), (coord_t)roi.y};
      res.edge_p2
          = {(coord_t)(roi.x
                       + edge.slope * (roi.height - 1) + edge.intercept),
             (coord_t)(roi.y + roi.height - 1)};
    }
  else
    {
      res.edge_p1
          = {(coord_t)roi.x, (coord_t)(roi.y + edge.intercept)};
      res.edge_p2
          = {(coord_t)(roi.x + roi.width - 1),
             (coord_t)(roi.y
                       + edge.slope * (roi.width - 1) + edge.intercept)};
    }

  res.edge_histogram_origin = min_d;
  res.edge_histogram_step = 1.0 / oversampling;
  res.edge_histogram.reserve (esf.size ());
  for (double value : esf)
    res.edge_histogram.push_back ((luminosity_t)value);

  res.measurement = std::move (measurement);
  res.success = true;
  res.failure = slanted_edge_failure_none;
  res.error.clear ();
  return res;
}

}
