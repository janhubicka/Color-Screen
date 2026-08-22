/* Parameter finetuning.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <functional>
#include <limits>
#include <memory>
#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include "bitmap.h"
#include "deconvolve.h"
#include "icc.h"
#include "include/colorscreen.h"
#include "include/dufaycolor.h"
#include "finetune-int.h"
#include "include/histogram.h"
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "nmsimplex.h"
#include "render-interpolate.h"
#include "sharpen.h"
#include <gsl/gsl_multifit.h>
namespace colorscreen
{

/* Validate combinations of FINETUNE_FLAGS before model setup can silently
   disable or ignore one of the requested parameters.  */
const char *
finetune_flag_error (uint64_t flags)
{
  const uint64_t legacy_blur
      = flags & (finetune_screen_blur | finetune_screen_channel_blurs);
  const uint64_t scanner_mtf
      = flags & (finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus
                 | finetune_scanner_mtf_channel_defocus);

  if ((flags & finetune_coordinates) && (flags & finetune_guess_coordinates))
    return "coordinate refinement and coordinate discovery are mutually "
           "exclusive";
  if ((flags & finetune_screen_blur)
      && (flags & finetune_screen_channel_blurs))
    return "scalar and per-channel legacy screen blur are mutually exclusive";
  if ((flags & finetune_scanner_mtf_defocus)
      && (flags & finetune_scanner_mtf_channel_defocus))
    return "scalar and per-channel scanner MTF defocus are mutually "
           "exclusive";
  if (legacy_blur && scanner_mtf)
    return "legacy screen blur and scanner MTF optimization cannot be "
           "combined";
  if ((flags & finetune_uniform_image_layer) && (flags & finetune_bw))
    return "uniform image-layer multi-tile fitting requires RGB input";
  if ((flags & finetune_uniform_image_layer)
      && (flags & finetune_simulate_infrared))
    return "uniform image-layer fitting and simulated infrared are mutually "
           "exclusive";
  return nullptr;
}

/* Convert a post-mix scalar dark term to an equivalent neutral pre-mix RGB
   dark value.  */
rgbdata
finetune_render_mix_dark (rgbdata weights, luminosity_t scalar_dark,
                          rgbdata fallback)
{
  if (!my_isfinite (weights.red) || !my_isfinite (weights.green)
      || !my_isfinite (weights.blue) || !my_isfinite (scalar_dark))
    return fallback;

  const luminosity_t sum = weights.red + weights.green + weights.blue;
  const luminosity_t scale = my_fabs (weights.red) + my_fabs (weights.green)
                             + my_fabs (weights.blue);
  const luminosity_t threshold
      = std::numeric_limits<luminosity_t>::epsilon ()
        * std::max ((luminosity_t)1, scale) * 16;
  if (!my_isfinite (sum) || my_fabs (sum) <= threshold)
    return fallback;

  const luminosity_t neutral_dark = scalar_dark / sum;
  if (!my_isfinite (neutral_dark))
    return fallback;
  return { neutral_dark, neutral_dark, neutral_dark };
}

/* Return the stable historical start for the active scalar scanner-MTF
   coordinate.  Physical defocus benefits from the adaptive coarse estimate.
   Measured MTF curves use BLUR_DIAMETER as an explicit residual correction
   and likewise retain the caller's estimate.  The metadata-free empirical
   circular-blur model is intentionally different: because its screen colors
   are variable-projected, blur and primary saturation can compensate one
   another and produce widely separated minima.  Starting at zero preserves
   the pre-warm-start basin selection used by this fallback model.  */
coord_t
finetune_initial_scanner_mtf_focus (const mtf_parameters &params)
{
  coord_t value = 0;
  if (params.simulate_diffraction_p ())
    value = params.defocus;
  else if (params.use_measured_mtf ())
    value = params.blur_diameter;
  if (!my_isfinite (value) || value < 0)
    return 0;
  return value;
}


/* Return a quadratically spaced scalar blur/focus-grid interval.  Computing
   node values from integer indexes makes cache keys bit-identical in every
   solver that uses the same range.  */
bool
finetune_focus_grid_interval_for_value (
    coord_t value, coord_t max_value, int nodes,
    finetune_focus_grid_interval *interval)
{
  if (!interval || !my_isfinite (value) || !my_isfinite (max_value)
      || max_value <= 0 || nodes < 2 || nodes > 64)
    return false;

  value = std::clamp (value, (coord_t)0, max_value);
  const coord_t last = nodes - 1;
  const coord_t scaled = std::sqrt (value / max_value) * last;
  int lower_index = (int)std::floor (scaled);
  if (lower_index < 0)
    lower_index = 0;
  if (lower_index >= nodes - 1)
    lower_index = nodes - 1;
  int upper_index = std::min (lower_index + 1, nodes - 1);

  const auto node_value = [=] (int index) {
    const coord_t t = index / last;
    return max_value * t * t;
  };
  const coord_t lower = node_value (lower_index);
  const coord_t upper = node_value (upper_index);
  coord_t upper_weight = 0;
  if (upper > lower)
    upper_weight = (value - lower) / (upper - lower);
  upper_weight = std::clamp (upper_weight, (coord_t)0, (coord_t)1);

  /* Roundoff in SQRT may place a value computed from an exact node just above
     or below the corresponding integer.  Collapse numerically exact endpoint
     cases so they require only one cache lookup.  */
  const coord_t tolerance
      = std::numeric_limits<coord_t>::epsilon ()
        * std::max ((coord_t)1, max_value) * 32;
  if (my_fabs (value - lower) <= tolerance)
    {
      upper_index = lower_index;
      upper_weight = 0;
    }
  else if (my_fabs (value - upper) <= tolerance)
    {
      lower_index = upper_index;
      upper_weight = 0;
    }

  interval->lower_index = lower_index;
  interval->upper_index = upper_index;
  interval->lower = node_value (lower_index);
  interval->upper = node_value (upper_index);
  interval->upper_weight = upper_weight;
  return true;
}

/* Find the first useful scalar blur/focus boundary at the process-screen
   frequency.  SET_VALUE changes the one varying model coordinate.  The
   quadratic scan is deliberately dense near the sharp end and is followed by
   bisection.  This is cheap compared with constructing even one filtered
   periodic screen and avoids stepping over the first low-contrast interval
   before later OTF lobes.  */
template<typename SetValue>
static bool
finetune_useful_scalar_limit (mtf_parameters params,
                              coord_t pixel_frequency,
                              coord_t minimum_mtf, coord_t hard_max,
                              SetValue set_value, coord_t *limit)
{
  if (!limit || !my_isfinite (pixel_frequency) || pixel_frequency <= 0
      || !my_isfinite (minimum_mtf) || minimum_mtf <= 0
      || minimum_mtf >= 1 || !my_isfinite (hard_max) || hard_max <= 0)
    return false;

  set_value (params, 0);
  const coord_t in_focus = params.system_mtf (pixel_frequency);
  if (!my_isfinite (in_focus) || in_focus <= minimum_mtf)
    return false;

  constexpr int samples = 4096;
  coord_t previous_value = 0;
  for (int i = 1; i <= samples; i++)
    {
      const coord_t t = (coord_t)i / samples;
      const coord_t value = hard_max * t * t;
      set_value (params, value);
      const coord_t current_mtf = params.system_mtf (pixel_frequency);
      if (!my_isfinite (current_mtf))
        return false;
      if (current_mtf <= minimum_mtf)
        {
          coord_t low = previous_value;
          coord_t high = value;
          /* SYSTEM_MTF is not assumed globally monotone: bisection remains
             inside the first sampled crossing bracket.  */
          for (int iteration = 0; iteration < 60; iteration++)
            {
              const coord_t middle = (low + high) * (coord_t)0.5;
              set_value (params, middle);
              const coord_t middle_mtf = params.system_mtf (pixel_frequency);
              if (!my_isfinite (middle_mtf))
                return false;
              if (middle_mtf > minimum_mtf)
                low = middle;
              else
                high = middle;
            }
          *limit = high;
          return true;
        }
      previous_value = value;
    }
  *limit = hard_max;
  return true;
}

/* Find the first useful physical-defocus boundary.  */
bool
finetune_useful_defocus_limit (mtf_parameters params,
                               coord_t pixel_frequency,
                               coord_t minimum_mtf, coord_t hard_max,
                               coord_t *limit)
{
  if (!params.simulate_diffraction_p ())
    return false;
  return finetune_useful_scalar_limit (
      params, pixel_frequency, minimum_mtf, hard_max,
      [] (mtf_parameters &p, coord_t value) { p.defocus = value; }, limit);
}

/* Find the first useful metadata-free compact-blur boundary.  */
bool
finetune_useful_blur_diameter_limit (mtf_parameters params,
                                     coord_t pixel_frequency,
                                     coord_t minimum_mtf, coord_t hard_max,
                                     coord_t *limit)
{
  if (params.simulate_diffraction_p () || params.use_measured_mtf ())
    return false;
  return finetune_useful_scalar_limit (
      params, pixel_frequency, minimum_mtf, hard_max,
      [] (mtf_parameters &p, coord_t value) { p.blur_diameter = value; },
      limit);
}

/* Classify one completed fit for use by adaptive blur/focus reduction.  Do
   this after FINETUNE has evaluated the final exact screen, so CONTRAST and
   UNCERTAINTY describe the result that would be stored.  */
finetune_result_quality
finetune_classify_result (const finetune_result &result,
                          luminosity_t min_contrast)
{
  if (!result.success)
    return finetune_result_quality::solver_failure;
  if (!my_isfinite (result.contrast) || result.contrast < 0
      || !my_isfinite (min_contrast) || min_contrast < 0)
    return finetune_result_quality::invalid_contrast;
  if (!my_isfinite (result.uncertainty) || result.uncertainty < 0
      || result.uncertainty >= std::numeric_limits<coord_t>::max ())
    return finetune_result_quality::invalid_fit_score;
  if (result.contrast < min_contrast)
    return finetune_result_quality::low_contrast;
  return finetune_result_quality::usable;
}

/* Return true when C can be used by the focus-area detector.  */
static inline bool
finite_focus_area_color_p (rgbdata c)
{
  return my_isfinite (c.red) && my_isfinite (c.green)
         && my_isfinite (c.blue);
}

/* Return RMS magnitude of RGB vector C.  */
static inline coord_t
focus_area_color_magnitude (rgbdata c)
{
  return my_sqrt (((coord_t)c.red * c.red + (coord_t)c.green * c.green
                   + (coord_t)c.blue * c.blue)
                  / (coord_t)3);
}

/* Return true when PARAMETERS form a safe focus-area detector setup.  */
static bool
valid_focus_analysis_area_parameters_p (
    const focus_analysis_area_parameters &parameters)
{
  return my_isfinite (parameters.sample_step) && parameters.sample_step > 0
         && my_isfinite (parameters.window_scale)
         && parameters.window_scale >= 1
         && parameters.candidate_stride >= 1
         && my_isfinite (parameters.max_relative_rms)
         && parameters.max_relative_rms >= 0
         && my_isfinite (parameters.max_relative_gradient)
         && parameters.max_relative_gradient >= 0
         && my_isfinite (parameters.min_signal) && parameters.min_signal > 0
         && my_isfinite (parameters.minimum_separation)
         && parameters.minimum_separation >= 0
         && parameters.max_candidates >= 0;
}

/* Score a regularly sampled interpolated RGB grid.  A shallow plane is fit
   independently to all three channels using centred X/Y coordinates.  Because
   the window is rectangular, the constant, X and Y basis vectors are mutually
   orthogonal and the residual can be computed directly without a matrix solve.
   This tolerates gentle fading/illumination gradients while still rejecting
   texture, edges and stronger gradients.  */
bool
finetune_find_focus_areas_in_grid (
    const rgbdata *data, const unsigned char *valid, int width, int height,
    point_t origin, coord_t step, int window_width, int window_height,
    const focus_analysis_area_parameters &parameters,
    std::vector<focus_analysis_area> *areas)
{
  if (!areas || !data || width <= 0 || height <= 0
      || (int64_t)width * height > INT_MAX
      || !valid_focus_analysis_area_parameters_p (parameters)
      || !my_isfinite (origin.x) || !my_isfinite (origin.y)
      || !my_isfinite (step) || step <= 0 || window_width < 3
      || window_height < 3 || !(window_width & 1) || !(window_height & 1))
    return false;

  areas->clear ();
  if (window_width > width || window_height > height)
    return true;

  const int hx = window_width / 2;
  const int hy = window_height / 2;
  const int n = window_width * window_height;
  double sxx = 0, syy = 0;
  for (int x = -hx; x <= hx; x++)
    sxx += (double)x * x * step * step * window_height;
  for (int y = -hy; y <= hy; y++)
    syy += (double)y * y * step * step * window_width;
  if (!(sxx > 0) || !(syy > 0))
    return false;

  std::vector<focus_analysis_area> candidates;
  const int stride = parameters.candidate_stride;
  for (int cy = hy; cy + hy < height; cy += stride)
    for (int cx = hx; cx + hx < width; cx += stride)
      {
        double sr = 0, sg = 0, sb = 0, sq = 0;
        double sxr = 0, sxg = 0, sxb = 0;
        double syr = 0, syg = 0, syb = 0;
        bool usable = true;
        for (int y = -hy; y <= hy && usable; y++)
          for (int x = -hx; x <= hx; x++)
            {
              const size_t i = (size_t)(cy + y) * width + cx + x;
              if ((valid && !valid[i]) || !finite_focus_area_color_p (data[i]))
                {
                  usable = false;
                  break;
                }
              const rgbdata c = data[i];
              const double ux = x * step;
              const double uy = y * step;
              sr += c.red;
              sg += c.green;
              sb += c.blue;
              sq += (double)c.red * c.red + (double)c.green * c.green
                    + (double)c.blue * c.blue;
              sxr += ux * c.red;
              sxg += ux * c.green;
              sxb += ux * c.blue;
              syr += uy * c.red;
              syg += uy * c.green;
              syb += uy * c.blue;
            }
        if (!usable)
          continue;

        const rgbdata mean
            = { (luminosity_t)(sr / n), (luminosity_t)(sg / n),
                (luminosity_t)(sb / n) };
        const coord_t magnitude = focus_area_color_magnitude (mean);
        if (!my_isfinite (magnitude) || magnitude < parameters.min_signal)
          continue;

        double sse = sq - (sr * sr + sg * sg + sb * sb) / n
                     - (sxr * sxr + sxg * sxg + sxb * sxb) / sxx
                     - (syr * syr + syg * syg + syb * syb) / syy;
        /* Roundoff can make exact planes slightly negative.  A substantially
           negative value indicates invalid arithmetic instead.  */
        const double roundoff
            = std::numeric_limits<double>::epsilon ()
              * std::max (1.0, sq) * 128;
        if (sse < -roundoff || !my_isfinite (sse))
          continue;
        if (sse < 0)
          sse = 0;
        const coord_t relative_rms
            = my_sqrt (sse / ((double)n * 3)) / magnitude;

        const double bx2 = (sxr * sxr + sxg * sxg + sxb * sxb)
                           / (sxx * sxx);
        const double by2 = (syr * syr + syg * syg + syb * syb)
                           / (syy * syy);
        const double dx = (window_width - 1) * step;
        const double dy = (window_height - 1) * step;
        const coord_t relative_gradient
            = my_sqrt ((bx2 * dx * dx + by2 * dy * dy) / 3) / magnitude;
        if (!my_isfinite (relative_rms) || !my_isfinite (relative_gradient)
            || relative_rms > parameters.max_relative_rms
            || relative_gradient > parameters.max_relative_gradient)
          continue;

        focus_analysis_area area;
        area.screen_center
            = { origin.x + cx * step, origin.y + cy * step };
        area.screen_area
            = { area.screen_center.x - window_width * step * (coord_t)0.5,
                area.screen_center.y - window_height * step * (coord_t)0.5,
                window_width * step, window_height * step };
        area.mean_color = mean;
        area.relative_rms = relative_rms;
        area.relative_gradient = relative_gradient;
        area.uniformity_score
            = relative_rms + relative_gradient * (coord_t)0.25;
        candidates.push_back (area);
      }

  std::stable_sort (
      candidates.begin (), candidates.end (),
      [] (const focus_analysis_area &a, const focus_analysis_area &b)
      { return a.uniformity_score < b.uniformity_score; });

  areas->reserve (parameters.max_candidates
                      ? std::min ((size_t)parameters.max_candidates,
                                  candidates.size ())
                      : candidates.size ());
  for (const focus_analysis_area &candidate : candidates)
    {
      bool overlap = false;
      if (parameters.minimum_separation > 0)
        for (const focus_analysis_area &accepted : *areas)
          {
            const coord_t xscale
                = parameters.minimum_separation * candidate.screen_area.width;
            const coord_t yscale
                = parameters.minimum_separation * candidate.screen_area.height;
            const coord_t dx
                = (candidate.screen_center.x - accepted.screen_center.x)
                  / xscale;
            const coord_t dy
                = (candidate.screen_center.y - accepted.screen_center.y)
                  / yscale;
            if (dx * dx + dy * dy < 1)
              {
                overlap = true;
                break;
              }
          }
      if (!overlap)
        areas->push_back (candidate);
      if (parameters.max_candidates
          && (int)areas->size () >= parameters.max_candidates)
        break;
    }
  return true;
}

/* Return a conservative image-space bounding box for screen-space AREA.
   Sampling the corners, edge midpoints and centre also covers ordinary smooth
   mesh/lens curvature without making the overlay depend on a dense polygon.  */
static int_image_area
focus_area_image_bounds (const scr_to_img &map, const image_area &area,
                         const int_image_area &limit)
{
  coord_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  bool first = true;
  for (int yi = 0; yi < 3; yi++)
    for (int xi = 0; xi < 3; xi++)
      {
        point_t p = { area.x + area.width * xi * (coord_t)0.5,
                      area.y + area.height * yi * (coord_t)0.5 };
        if (!map.to_img_in_mesh_range (p))
          continue;
        p = map.to_img (p);
        if (!my_isfinite (p.x) || !my_isfinite (p.y))
          continue;
        if (first)
          xmin = xmax = p.x, ymin = ymax = p.y, first = false;
        else
          {
            xmin = std::min (xmin, p.x);
            xmax = std::max (xmax, p.x);
            ymin = std::min (ymin, p.y);
            ymax = std::max (ymax, p.y);
          }
      }
  if (first)
    return {};
  int_image_area ret (my_floor (xmin), my_floor (ymin),
                      my_ceil (xmax) - my_floor (xmin) + 1,
                      my_ceil (ymax) - my_floor (ymin) + 1);
  return ret.intersect (limit);
}

/* Locate solid-colour focus-analysis candidates in the interpolated
   reconstruction.  Scanner sharpening and an already-installed adaptive blur
   correction are disabled for this inexpensive discovery pass so candidate
   classification does not depend circularly on the focus model being fitted.
   Screen-domain denoising/demosaicing and the current colour reconstruction
   remain active.  */
bool
find_focus_analysis_areas (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img, int_image_area search_area,
    const finetune_parameters &fparams,
    const focus_analysis_area_parameters &parameters,
    std::vector<focus_analysis_area> *areas, progress_info *progress)
{
  if (!areas || !img.has_rgb () || param.type == Random
      || !valid_focus_analysis_area_parameters_p (parameters)
      || fparams.range < 0 || finetune_flag_error (fparams.flags)
      || (fparams.flags & (finetune_coordinates | finetune_guess_coordinates)))
    return false;
  areas->clear ();

  const int_image_area image_limit
      = rparam.get_image_area (img.width, img.height);
  if (search_area.empty_p ())
    search_area = image_limit;
  else
    search_area = search_area.intersect (image_limit);
  if (search_area.empty_p ())
    return true;

  scr_to_img map;
  if (!map.set_parameters (param, img))
    return false;
  const image_area screen_range = map.get_range (image_area (search_area));
  if (screen_range.empty_p ())
    return true;

  coord_t solver_xrange = fparams.range
                              ? fparams.range
                              : ((fparams.flags
                                  & (finetune_no_normalize | finetune_bw
                                     | finetune_uniform_image_layer))
                                     ? (coord_t)1
                                     : (coord_t)2);
  coord_t solver_yrange = solver_xrange;
  if (screen_with_vertical_strips_p (param.type))
    solver_yrange *= 3;
  const int half_x = std::max (2, (int)my_ceil (
                                      solver_xrange * parameters.window_scale
                                      / parameters.sample_step));
  const int half_y = std::max (2, (int)my_ceil (
                                      solver_yrange * parameters.window_scale
                                      / parameters.sample_step));
  const int window_width = 2 * half_x + 1;
  const int window_height = 2 * half_y + 1;

  const coord_t step = parameters.sample_step;
  const coord_t x0 = my_ceil (screen_range.x / step) * step;
  const coord_t y0 = my_ceil (screen_range.y / step) * step;
  const coord_t x1
      = my_floor ((screen_range.x + screen_range.width) / step) * step;
  const coord_t y1
      = my_floor ((screen_range.y + screen_range.height) / step) * step;
  if (x1 < x0 || y1 < y0)
    return true;
  const int64_t width64
      = (int64_t)my_floor ((x1 - x0) / step + (coord_t)0.5) + 1;
  const int64_t height64
      = (int64_t)my_floor ((y1 - y0) / step + (coord_t)0.5) + 1;
  if (width64 <= 0 || height64 <= 0 || width64 > INT_MAX
      || height64 > INT_MAX || width64 * height64 > INT_MAX)
    return false;
  const int width = width64;
  const int height = height64;
  if (window_width > width || window_height > height)
    return true;

  render_parameters analysis_rparam = rparam;
  analysis_rparam.sharpen.mode = sharpen_parameters::none;
  analysis_rparam.scanner_blur_correction = nullptr;
  render_interpolate renderer (param, img, analysis_rparam, 256);
  renderer.set_unadjusted ();
  if (!renderer.precompute_img_range (search_area, progress))
    return false;
  if (progress && progress->cancel_requested ())
    return false;

  std::vector<rgbdata> data ((size_t)width * height);
  std::vector<unsigned char> valid ((size_t)width * height, 0);
  for (int y = 0; y < height; y++)
    {
      if (progress && progress->cancel_requested ())
        return false;
      for (int x = 0; x < width; x++)
        {
          const point_t scr = { x0 + x * step, y0 + y * step };
          if (!map.to_img_in_mesh_range (scr))
            continue;
          const point_t ip = map.to_img (scr);
          if (!my_isfinite (ip.x) || !my_isfinite (ip.y)
              || ip.x < search_area.x || ip.y < search_area.y
              || ip.x >= search_area.x + search_area.width
              || ip.y >= search_area.y + search_area.height)
            continue;
          const rgbdata c = renderer.sample_pixel_scr (scr);
          if (!finite_focus_area_color_p (c))
            continue;
          const size_t i = (size_t)y * width + x;
          data[i] = c;
          valid[i] = 1;
        }
    }

  std::vector<focus_analysis_area> found;
  if (!finetune_find_focus_areas_in_grid (
          data.data (), valid.data (), width, height, { x0, y0 }, step,
          window_width, window_height, parameters, &found))
    return false;

  areas->reserve (found.size ());
  for (focus_analysis_area &area : found)
    {
      if (!map.to_img_in_mesh_range (area.screen_center))
        continue;
      area.image_center = map.to_img (area.screen_center);
      area.image_bounds
          = focus_area_image_bounds (map, area.screen_area, search_area);
      if (!finite_focus_area_color_p (area.mean_color)
          || !my_isfinite (area.image_center.x)
          || !my_isfinite (area.image_center.y) || area.image_bounds.empty_p ())
        continue;
      areas->push_back (area);
    }
  return true;
}

/* Determinant of a symmetric 3x3 matrix stored in G.  */
static double
focus_area_gram_determinant (const double g[3][3])
{
  return g[0][0] * (g[1][1] * g[2][2] - g[1][2] * g[2][1])
         - g[0][1] * (g[1][0] * g[2][2] - g[1][2] * g[2][0])
         + g[0][2] * (g[1][0] * g[2][1] - g[1][1] * g[2][0]);
}

/* Add outer product C*C^T to G.  */
static void
focus_area_add_outer_product (double g[3][3], const double c[3])
{
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 3; x++)
      g[y][x] += c[y] * c[x];
}

/* Select a high-quality, colour-diverse subset for the joint multi-tile fit.
   Individual solver residual is normalized by the independently reconstructed
   area signal; fitted contrast is only an information gate and never appears
   in the quality denominator, avoiding the known high-saturation reward.  */
bool
select_focus_analysis_areas (
    const std::vector<focus_analysis_area> &areas,
    const std::vector<finetune_result> &fits,
    const focus_analysis_selection_parameters &parameters,
    std::vector<size_t> *indices, coord_t *color_condition)
{
  if (!indices || areas.size () != fits.size () || parameters.max_areas < 1
      || parameters.max_areas > 8
      || !my_isfinite (parameters.fit_retain_ratio)
      || parameters.fit_retain_ratio < 0 || parameters.fit_retain_ratio > 1
      || !my_isfinite (parameters.min_contrast) || parameters.min_contrast < 0
      || !my_isfinite (parameters.min_signal) || parameters.min_signal <= 0)
    return false;
  indices->clear ();
  if (color_condition)
    *color_condition = 0;

  struct candidate
  {
    size_t index;
    double quality;
    double chroma[3];
    double neutrality;
  };
  std::vector<candidate> usable;
  usable.reserve (areas.size ());
  for (size_t i = 0; i < areas.size (); i++)
    {
      if (finetune_classify_result (fits[i], parameters.min_contrast)
              != finetune_result_quality::usable
          || !my_isfinite (fits[i].badness) || fits[i].badness < 0
          || fits[i].badness >= std::numeric_limits<coord_t>::max ()
          || !finite_focus_area_color_p (areas[i].mean_color))
        continue;
      const double r = std::max ((double)areas[i].mean_color.red, 0.0);
      const double g = std::max ((double)areas[i].mean_color.green, 0.0);
      const double b = std::max ((double)areas[i].mean_color.blue, 0.0);
      const double signal = std::sqrt ((r * r + g * g + b * b) / 3);
      const double sum = r + g + b;
      if (!my_isfinite (signal) || signal < parameters.min_signal
          || !my_isfinite (sum) || !(sum > 0))
        continue;
      candidate c;
      c.index = i;
      c.quality = fits[i].badness / signal;
      c.chroma[0] = r / sum;
      c.chroma[1] = g / sum;
      c.chroma[2] = b / sum;
      c.neutrality = std::min ({ c.chroma[0], c.chroma[1], c.chroma[2] });
      if (my_isfinite (c.quality))
        usable.push_back (c);
    }
  if (usable.empty ())
    return true;

  std::stable_sort (usable.begin (), usable.end (),
                    [] (const candidate &a, const candidate &b)
                    { return a.quality < b.quality; });
  size_t pool_size
      = (size_t)my_ceil (usable.size () * parameters.fit_retain_ratio);
  pool_size = std::max (pool_size,
                        std::min (usable.size (),
                                  (size_t)parameters.max_areas));
  pool_size = std::min (pool_size, usable.size ());
  usable.resize (pool_size);

  /* The first tile fixes the scale gauge of tile-primary intensities.  Prefer
     a neutral-ish member of the already high-quality pool so later tiles do
     not need extreme relative coefficients merely because the anchor was a
     nearly pure primary.  */
  size_t anchor = 0;
  for (size_t i = 1; i < usable.size (); i++)
    if (usable[i].neutrality > usable[anchor].neutrality + 1e-12
        || (fabs (usable[i].neutrality - usable[anchor].neutrality) <= 1e-12
            && usable[i].quality < usable[anchor].quality))
      anchor = i;

  double gram[3][3] = {};
  std::vector<unsigned char> selected (usable.size (), 0);
  auto accept = [&] (size_t i)
  {
    indices->push_back (usable[i].index);
    focus_area_add_outer_product (gram, usable[i].chroma);
    selected[i] = 1;
  };
  accept (anchor);

  while ((int)indices->size () < parameters.max_areas
         && indices->size () < usable.size ())
    {
      size_t best = usable.size ();
      double best_det = -1;
      for (size_t i = 0; i < usable.size (); i++)
        if (!selected[i])
          {
            double trial[3][3];
            memcpy (trial, gram, sizeof (trial));
            focus_area_add_outer_product (trial, usable[i].chroma);
            /* A tiny isotropic prior makes the D-optimal score meaningful for
               the first two selected rows without materially changing later
               conditioning.  */
            for (int c = 0; c < 3; c++)
              trial[c][c] += 1e-8;
            const double det = focus_area_gram_determinant (trial);
            if (best == usable.size () || det > best_det + 1e-18
                || (fabs (det - best_det) <= 1e-18
                    && usable[i].quality < usable[best].quality))
              best = i, best_det = det;
          }
      if (best == usable.size ())
        break;
      accept (best);
    }

  if (color_condition && indices->size () >= 3)
    {
      const double trace = gram[0][0] + gram[1][1] + gram[2][2];
      double det = focus_area_gram_determinant (gram);
      if (det < 0 && det > -1e-14)
        det = 0;
      if (trace > 0 && det > 0 && my_isfinite (det))
        *color_condition
            = std::clamp ((coord_t)(27 * det / (trace * trace * trace)),
                          (coord_t)0, (coord_t)1);
    }
  return true;
}

namespace
{
struct gsl_work_deleter
{
  void
  operator() (gsl_multifit_linear_workspace *p)
  {
    gsl_multifit_linear_free (p);
  }
};
struct gsl_matrix_deleter
{
  void
  operator() (gsl_matrix *p)
  {
    gsl_matrix_free (p);
  }
};
struct gsl_vector_deleter
{
  void
  operator() (gsl_vector *p)
  {
    gsl_vector_free (p);
  }
};

/* Thread-safe accumulator shared by every solver candidate created by one
   FINETUNE call.  The public result contains an ordinary snapshot.  */
class finetune_profile_accumulator
{
public:
  std::atomic_uint64_t simplex_runs{0};
  std::atomic_uint64_t simplex_iterations{0};
  std::atomic_uint64_t simplex_evaluations{0};
  std::atomic_uint64_t objective_evaluations{0};
  std::atomic_uint64_t screen_init_calls{0};
  std::atomic_uint64_t screen_state_reuses{0};
  std::atomic_uint64_t fixed_screen_cache_hits{0};
  std::atomic_uint64_t fixed_screen_cache_misses{0};
  std::atomic_uint64_t focus_screen_cache_hits{0};
  std::atomic_uint64_t focus_screen_cache_misses{0};
  std::atomic_uint64_t focus_screen_local_node_hits{0};
  std::atomic_uint64_t focus_screen_local_node_misses{0};
  std::atomic_uint64_t focus_source_cache_hits{0};
  std::atomic_uint64_t focus_source_cache_misses{0};
  std::atomic_uint64_t focus_screen_interpolations{0};
  std::atomic_uint64_t focus_screen_exact_node_uses{0};
  std::atomic_uint64_t focus_screen_final_exact_builds{0};
  std::atomic_uint64_t exact_screen_builds{0};
  std::atomic_uint64_t mtf_precompute_calls{0};
  std::atomic_uint64_t mtf_psf_precompute_calls{0};
  std::atomic_uint64_t physical_focus_cache_hits{0};
  std::atomic_uint64_t physical_focus_cache_misses{0};
  std::atomic_uint64_t physical_focus_transfer_builds{0};
  std::atomic_uint64_t empirical_focus_transfer_builds{0};
  std::atomic_uint64_t direct_transfer_builds{0};
  std::atomic_uint64_t wrapped_psf_builds{0};
  std::atomic_uint64_t kernel_forward_ffts{0};
  std::atomic_uint64_t screen_forward_ffts{0};
  std::atomic_uint64_t screen_inverse_ffts{0};
  std::atomic_uint64_t objective_nanoseconds{0};
  std::atomic_uint64_t screen_filter_nanoseconds{0};
  std::atomic_uint64_t screen_cache_nanoseconds{0};
  std::atomic_uint64_t screen_interpolation_nanoseconds{0};
  std::atomic_uint64_t screen_simulation_nanoseconds{0};
  std::atomic_uint64_t color_estimation_nanoseconds{0};
  std::atomic_uint64_t residual_nanoseconds{0};

  void
  add_filter_profile (const screen_filter_profile &p)
  {
    mtf_precompute_calls.fetch_add (p.mtf_precompute_calls,
                                    std::memory_order_relaxed);
    mtf_psf_precompute_calls.fetch_add (p.mtf_psf_precompute_calls,
                                        std::memory_order_relaxed);
    physical_focus_cache_hits.fetch_add (p.physical_focus_cache_hits,
                                         std::memory_order_relaxed);
    physical_focus_cache_misses.fetch_add (p.physical_focus_cache_misses,
                                           std::memory_order_relaxed);
    physical_focus_transfer_builds.fetch_add (
        p.physical_focus_transfer_builds, std::memory_order_relaxed);
    empirical_focus_transfer_builds.fetch_add (
        p.empirical_focus_transfer_builds, std::memory_order_relaxed);
    direct_transfer_builds.fetch_add (p.direct_transfer_builds,
                                      std::memory_order_relaxed);
    wrapped_psf_builds.fetch_add (p.wrapped_psf_builds,
                                  std::memory_order_relaxed);
    kernel_forward_ffts.fetch_add (p.kernel_forward_ffts,
                                   std::memory_order_relaxed);
    screen_forward_ffts.fetch_add (p.screen_forward_ffts,
                                   std::memory_order_relaxed);
    screen_inverse_ffts.fetch_add (p.screen_inverse_ffts,
                                   std::memory_order_relaxed);
  }

  finetune_profile
  snapshot () const
  {
    finetune_profile ret;
#define COPY_PROFILE_FIELD(FIELD)                                             \
  ret.FIELD = FIELD.load (std::memory_order_relaxed)
    COPY_PROFILE_FIELD (simplex_runs);
    COPY_PROFILE_FIELD (simplex_iterations);
    COPY_PROFILE_FIELD (simplex_evaluations);
    COPY_PROFILE_FIELD (objective_evaluations);
    COPY_PROFILE_FIELD (screen_init_calls);
    COPY_PROFILE_FIELD (screen_state_reuses);
    COPY_PROFILE_FIELD (fixed_screen_cache_hits);
    COPY_PROFILE_FIELD (fixed_screen_cache_misses);
    COPY_PROFILE_FIELD (focus_screen_cache_hits);
    COPY_PROFILE_FIELD (focus_screen_cache_misses);
    COPY_PROFILE_FIELD (focus_screen_local_node_hits);
    COPY_PROFILE_FIELD (focus_screen_local_node_misses);
    COPY_PROFILE_FIELD (focus_source_cache_hits);
    COPY_PROFILE_FIELD (focus_source_cache_misses);
    COPY_PROFILE_FIELD (focus_screen_interpolations);
    COPY_PROFILE_FIELD (focus_screen_exact_node_uses);
    COPY_PROFILE_FIELD (focus_screen_final_exact_builds);
    COPY_PROFILE_FIELD (exact_screen_builds);
    COPY_PROFILE_FIELD (mtf_precompute_calls);
    COPY_PROFILE_FIELD (mtf_psf_precompute_calls);
    COPY_PROFILE_FIELD (physical_focus_cache_hits);
    COPY_PROFILE_FIELD (physical_focus_cache_misses);
    COPY_PROFILE_FIELD (physical_focus_transfer_builds);
    COPY_PROFILE_FIELD (empirical_focus_transfer_builds);
    COPY_PROFILE_FIELD (direct_transfer_builds);
    COPY_PROFILE_FIELD (wrapped_psf_builds);
    COPY_PROFILE_FIELD (kernel_forward_ffts);
    COPY_PROFILE_FIELD (screen_forward_ffts);
    COPY_PROFILE_FIELD (screen_inverse_ffts);
    COPY_PROFILE_FIELD (objective_nanoseconds);
    COPY_PROFILE_FIELD (screen_filter_nanoseconds);
    COPY_PROFILE_FIELD (screen_cache_nanoseconds);
    COPY_PROFILE_FIELD (screen_interpolation_nanoseconds);
    COPY_PROFILE_FIELD (screen_simulation_nanoseconds);
    COPY_PROFILE_FIELD (color_estimation_nanoseconds);
    COPY_PROFILE_FIELD (residual_nanoseconds);
#undef COPY_PROFILE_FIELD
    return ret;
  }
};

enum class finetune_profile_timer_kind
{
  objective,
  screen_filter,
  screen_cache,
  screen_interpolation,
  screen_simulation,
  color_estimation,
  residual
};

/* Charge elapsed steady-clock time to PROFILE on every exit path.  */
class finetune_profile_timer
{
public:
  finetune_profile_timer (finetune_profile_accumulator *profile,
                          finetune_profile_timer_kind kind)
      : m_profile (profile), m_kind (kind),
        m_start (profile ? clock::now () : clock::time_point ())
  {
  }

  ~finetune_profile_timer ()
  {
    if (!m_profile)
      return;
    const uint64_t elapsed
        = std::chrono::duration_cast<std::chrono::nanoseconds> (clock::now ()
                                                                - m_start)
              .count ();
    std::atomic_uint64_t *counter = nullptr;
    switch (m_kind)
      {
      case finetune_profile_timer_kind::objective:
        counter = &m_profile->objective_nanoseconds;
        break;
      case finetune_profile_timer_kind::screen_filter:
        counter = &m_profile->screen_filter_nanoseconds;
        break;
      case finetune_profile_timer_kind::screen_cache:
        counter = &m_profile->screen_cache_nanoseconds;
        break;
      case finetune_profile_timer_kind::screen_interpolation:
        counter = &m_profile->screen_interpolation_nanoseconds;
        break;
      case finetune_profile_timer_kind::screen_simulation:
        counter = &m_profile->screen_simulation_nanoseconds;
        break;
      case finetune_profile_timer_kind::color_estimation:
        counter = &m_profile->color_estimation_nanoseconds;
        break;
      case finetune_profile_timer_kind::residual:
        counter = &m_profile->residual_nanoseconds;
        break;
      }
    counter->fetch_add (elapsed, std::memory_order_relaxed);
  }

private:
  using clock = std::chrono::steady_clock;
  finetune_profile_accumulator *m_profile;
  finetune_profile_timer_kind m_kind;
  clock::time_point m_start;
};

/* Compare the subset of SHARPEN that changes the periodic filtered screen.
   SHARPEN_PARAMETERS::OPERATOR== intentionally ignores the capture MTF in
   mode NONE, so compare the capture scale/model explicitly as the renderer's
   ordinary screen cache does.  */
static bool
same_filtered_screen_parameters_p (const sharpen_parameters &a,
                                   const sharpen_parameters &b,
                                   bool anticipate_sharpening)
{
  return a.scanner_mtf_scale == b.scanner_mtf_scale
         && (!a.scanner_mtf_scale || a.scanner_mtf == b.scanner_mtf)
         && (!anticipate_sharpening || a == b);
}

/* Key for immutable source spectra used by fixed-geometry scalar-defocus
   fits.  The spectra depend only on the process screen, not on focus or the
   capture transfer.  PROFILE affects construction only and is excluded from
   equality.  */
struct finetune_screen_source_cache_params
{
  scr_type type = Joly;
  coord_t red_strip_width = 0;
  coord_t green_strip_width = 0;
  screen_filter_profile *filter_profile = nullptr;

  bool
  operator== (const finetune_screen_source_cache_params &o) const
  {
    return type == o.type
           && (!screen_with_varying_strips_p (type)
               || (red_strip_width == o.red_strip_width
                   && green_strip_width == o.green_strip_width));
  }
};

std::unique_ptr<screen_filter_source>
get_new_finetune_screen_source (finetune_screen_source_cache_params &p,
                                progress_info *progress)
{
  (void)progress;
  screen_filter_profile *filter_profile = p.filter_profile;
  p.filter_profile = nullptr;
  screen source;
  source.initialize (p.type, p.red_strip_width, p.green_strip_width);
  auto prepared = std::make_unique<screen_filter_source> ();
  if (!source.prepare_filter_source (*prepared, filter_profile))
    return nullptr;
  return prepared;
}

/* A prepared RGB source spectrum occupies about 0.6 MiB.  Fixed scalar-focus
   fitting normally uses one entry; eight entries keep accidental geometry
   variation bounded without turning this exact cache into a strip-width
   approximation.  */
using finetune_screen_source_cache_t
    = lru_cache<finetune_screen_source_cache_params, screen_filter_source,
                get_new_finetune_screen_source, 8>;
static finetune_screen_source_cache_t finetune_screen_source_cache (
    "finetune focus source spectrum");

static std::shared_ptr<screen_filter_source>
get_cached_finetune_screen_source (
    scr_type type, coord_t red_strip_width, coord_t green_strip_width,
    bool *cache_hit, screen_filter_profile *filter_profile)
{
  finetune_screen_source_cache_params params;
  params.type = type;
  params.red_strip_width = red_strip_width;
  params.green_strip_width = green_strip_width;
  params.filter_profile = filter_profile;
  return finetune_screen_source_cache.get (
      params, nullptr, nullptr, cache_hit);
}

/* Construction-only details returned by a final-screen cache miss.  They do
   not participate in final-screen cache equality.  */
struct finetune_screen_build_info
{
  bool source_cache_lookup = false;
  bool source_cache_hit = false;
};

/* Key for exact focus-dependent periodic screens used only by FINETUNE.  It
   is separate from the renderer cache so the optimizer cannot evict normal
   display/render entries with its transient focus nodes.  PARALLEL,
   REUSE_SOURCE_SPECTRUM, PROFILE and BUILD_INFO affect construction only and
   deliberately do not participate in equality.  */
struct finetune_screen_cache_params
{
  scr_type type = Joly;
  coord_t red_strip_width = 0;
  coord_t green_strip_width = 0;
  bool anticipate_sharpening = false;
  bool parallel = false;
  bool reuse_source_spectrum = false;
  std::array<sharpen_parameters, 3> sharpen = {};
  screen_filter_profile *filter_profile = nullptr;
  finetune_screen_build_info *build_info = nullptr;

  bool
  operator== (const finetune_screen_cache_params &o) const
  {
    if (type != o.type
        || anticipate_sharpening != o.anticipate_sharpening
        || (screen_with_varying_strips_p (type)
            && (red_strip_width != o.red_strip_width
                || green_strip_width != o.green_strip_width)))
      return false;
    for (int c = 0; c < 3; c++)
      if (!same_filtered_screen_parameters_p (
              sharpen[c], o.sharpen[c], anticipate_sharpening))
        return false;
    return true;
  }
};

std::unique_ptr<screen>
get_new_finetune_screen (finetune_screen_cache_params &p,
                         progress_info *progress)
{
  (void)progress;
  screen_filter_profile *filter_profile = p.filter_profile;
  finetune_screen_build_info *build_info = p.build_info;
  /* The cache entry retains PARAMS after this call.  Do not retain a pointer
     to the requesting solver's stack-local profile structure.  */
  p.filter_profile = nullptr;
  p.build_info = nullptr;
  auto filtered = std::make_unique<screen> ();
  sharpen_parameters *channels[3]
      = { &p.sharpen[0], &p.sharpen[1], &p.sharpen[2] };
  bool prepared_source_supported = p.reuse_source_spectrum;
  if (p.anticipate_sharpening)
    for (int c = 0; c < 3; c++)
      if (p.sharpen[c].get_mode ()
          == sharpen_parameters::richardson_lucy_deconvolution)
        prepared_source_supported = false;
  if (prepared_source_supported)
    {
      bool source_cache_hit = false;
      std::shared_ptr<screen_filter_source> source
          = get_cached_finetune_screen_source (
              p.type, p.red_strip_width, p.green_strip_width,
              &source_cache_hit, filter_profile);
      if (build_info)
        {
          build_info->source_cache_lookup = true;
          build_info->source_cache_hit = source_cache_hit;
        }
      if (!source
          || !filtered->initialize_with_sharpen_parameters (
              *source, channels, p.anticipate_sharpening, p.parallel,
              filter_profile))
        return nullptr;
    }
  else
    {
      screen source;
      source.initialize (p.type, p.red_strip_width, p.green_strip_width);
      if (!filtered->initialize_with_sharpen_parameters (
              source, channels, p.anticipate_sharpening, p.parallel,
              filter_profile))
        return nullptr;
    }
  return filtered;
}

/* A screen occupies roughly 384 KiB with the current 128x128 float storage;
   64 entries therefore bound the nominal payload near 24 MiB.  Entries still
   referenced by active solvers are not evicted by LRU_CACHE.  */
using finetune_screen_cache_t
    = lru_cache<finetune_screen_cache_params, screen,
                get_new_finetune_screen, 64>;
static finetune_screen_cache_t finetune_screen_cache ("finetune focus screen");

static std::shared_ptr<screen>
get_cached_finetune_screen (
    scr_type type, coord_t red_strip_width, coord_t green_strip_width,
    bool anticipate_sharpening,
    const std::array<sharpen_parameters, 3> &sharpen, bool parallel,
    bool reuse_source_spectrum, bool *cache_hit,
    screen_filter_profile *filter_profile,
    finetune_screen_build_info *build_info = nullptr)
{
  finetune_screen_cache_params params;
  params.type = type;
  params.red_strip_width = red_strip_width;
  params.green_strip_width = green_strip_width;
  params.anticipate_sharpening = anticipate_sharpening;
  params.parallel = parallel;
  params.reuse_source_spectrum = reuse_source_spectrum;
  params.sharpen = sharpen;
  params.filter_profile = filter_profile;
  params.build_info = build_info;
  return finetune_screen_cache.get (params, nullptr, nullptr, cache_hit);
}

/* Translate center to given coordinates (x,y).  */
class translation_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize translation to CENTER.  */
  translation_3x3matrix (point_t center)
  {
    (*this)(0, 2) = center.x;
    (*this)(1, 2) = center.y;
  }
};

/* Rotate by given angle.  */
class rotation_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize rotation by ROTATION degrees.  */
  rotation_3x3matrix (coord_t rotation)
  {
    rotation *= (coord_t)M_PI / 180;
    const coord_t s = std::sin (rotation);
    const coord_t c = std::cos (rotation);
    (*this)(0, 0) = c; (*this)(0, 1) = -s;
    (*this)(1, 0) = s; (*this)(1, 1) = c;
    (*this)(2, 2) = 1; 
  }
};

/* Scale both image axes by the same factor.  */
class scale_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize uniform scaling by SCALE.  */
  scale_3x3matrix (coord_t scale)
  {
    (*this)(0, 0) = scale;
    (*this)(1, 1) = scale;
  }
};

/* Return contrast which is useful for registration.  */
luminosity_t
get_positional_color_contrast (scr_type type, rgbdata c, bool robust)
{
  /* Robust mode is used to discover coordinates and screen type completely.
     Here we insist on a difference between all three channels.  */
  if (robust)
    {
      return std::min ({my_fabs (c.red - c.green), my_fabs (c.red - c.blue),
		        my_fabs (c.blue - c.green)});
    }
  /* In Paget like screens any difference is good since each color
     forms a grid.
     For screen with strips we only determine one direction and that one
     is also always good.  */
  if (paget_like_screen_p (type)
      || screen_with_vertical_strips_p (type))
    {
      luminosity_t mmin = std::min ({c.red, c.green, c.blue});
      luminosity_t mmax = std::max ({c.red, c.green, c.blue});
      return mmax - mmin;
    }
  /* Dufaycolor has green and blue squares, red strips.
     We need contrast between the squares to determine position in both
     directions.  */
  if (dufay_like_screen_p (type))
    {
      if (type == Dufay)
	;
      else if (type == DioptichromeB)
	std::swap (c.red, c.green);
      else if (type == ImprovedDioptichromeB || type == Omnicolore)
	std::swap (c.red, c.blue);
      else
	abort ();
      luminosity_t mmin = std::min (c.blue, c.green);
      luminosity_t mmax = std::max (c.blue, c.green);
      return mmax - mmin;
    }
  abort ();
}

/* Callback used for sharpening.  Fetch data from buffer R at point P.
   WIDTH and HEIGHT are dimensions of the buffer.  */
rgbdata
getdata_helper (rgbdata *r, int_point_t p, int width, int height)
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < width && p.y >= 0 && p.y < height);
  return r[p.y * width + p.x];
}

/* Sign of angle used for mesh transform.  Compute sign of triangle formed
   by P1, P2 and P3.  */
static coord_t
sign (point_t p1, point_t p2, point_t p3)
{
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

/* Intersect two vectors starting at [X1, Y1] with direction [DX1, DY1]
   and [X2, Y2] with direction [DX2, DY2].  Store result in [A, B].  */
void
intersect_vectors (coord_t x1, coord_t y1, coord_t dx1, coord_t dy1,
                   coord_t x2, coord_t y2, coord_t dx2, coord_t dy2,
                   coord_t *a, coord_t *b)
{
  matrix2x2<coord_t> m (dx1, -dx2, dy1, -dy2);
  m = m.invert ();
  m.apply_to_vector (x2 - x1, y2 - y1, a, b);
}

/* Clamp value V to range [MIN, MAX].  */
inline void
to_range (coord_t &v, coord_t min, coord_t max)
{
  /* std::clamp leaves NaNs unchanged because both comparisons are false.
     Comparison-based clamping also preserves the useful distinction between
     positive and negative infinity.  Do not let one invalid simplex
     coordinate poison screen-cache keys and subsequent evaluations.  */
  if (!(v >= min))
    v = min;
  else if (!(v <= max))
    v = max;
}

/* Return true for a usable contrast-scaled FINETUNE score.  The largest
   representable value is reserved as a propagation sentinel for failed
   screen construction and non-finite objectives.  */
inline bool
valid_fit_score_p (coord_t score)
{
  return my_isfinite (score) && score >= 0
         && score < std::numeric_limits<coord_t>::max ();
}

/* Return a safe positive divisor for diagnostic-image normalization.  */
inline luminosity_t
diagnostic_normalization (luminosity_t maximum)
{
  return my_isfinite (maximum) && maximum > 0 ? maximum : 1;
}

/* V is in range 0...1 expand it to MINV...MAXV.
   Finetuning works well if values are generally kept
   in range 0...1.  */
inline coord_t
expand_range (coord_t v, coord_t minv, coord_t maxv)
{
  return minv + v * (maxv - minv);
}

/* V is in range MINV...MAXV shrink it to 0...1.  */
inline coord_t
shrink_range (coord_t v, coord_t minv, coord_t maxv)
{
  return (v - minv) * ((coord_t)1 / (maxv - minv));
}

/* Solver used to find parameters of simulated scan (position of the grid,
   color of individual patches, lens blur ...) to match given scan tile
   (described tile and tile_pos) as well as possible.
   It is possible to match either in BW or RGB and choose set of parameters
   to optimize for.  */
class finetune_solver
{
public:
  static constexpr const int max_tiles = 8;

  /* Data we need for each tile.  */
  class tile_data
  {
  public:
    std::shared_ptr<bitmap_2d> outliers;
    /* Tile positions */
    int txmin, tymin;

    /* Tile colors collected from the scan for faster access.
       Empty for BW mode.  */
    std::vector<rgbdata> color;
    /* Sharpened tile.  */
    rgbdata *sharpened_color = nullptr;
    /* Memory buffer for sharpened tile when it is not alias of color.  */
    std::vector<rgbdata> sharpened_color_buffer;
    /* Black and white tile.
       Empty for color mode.  */
    std::vector<luminosity_t> bw;
    /* Tile position  */
    std::vector<point_t> pos;
    /* If we do not finetune offsets, fix one. Usually 0,0.  */
    point_t fixed_offset = { -10, -10 }, fixed_emulsion_offset = { -10, -10 };
    /* Screen merging emulsion and unblurred screen.  */
    std::unique_ptr<screen> merged_scr;
    /* Blurred screen used to render the simulated scan.  Exact cached focus
       nodes may be shared by many local solvers; dynamically merged/emulsion
       screens use a private instance.  */
    std::shared_ptr<screen> scr;

    tile_data () {}
    tile_data (const tile_data &) = delete;
    tile_data (tile_data &&) = default;
    tile_data &operator= (const tile_data &) = delete;
    tile_data &operator= (tile_data &&) = default;
    ~tile_data () = default;

  protected:
    friend finetune_solver;

    /* Remember last settings, so we do not recompute screens uselessly.  */
    rgbdata last_emulsion_intensities = { -1, -1, -1 };
    point_t last_emulsion_offset = { -10, -10 };
    int last_screen_revision = -1;
    point_t last_simulated_offset = { -100, -100 };

    /* Simulation of screen.  */
    std::vector<rgbdata> simulated_screen;
  };
  tile_data tiles[max_tiles];

private:
  /* Matrix for scaling & rotation */
  matrix3x3<coord_t> transformation;

  /* Least squares solver for optimizing parameters that behaves linearly.  */
  std::unique_ptr<gsl_multifit_linear_workspace, gsl_work_deleter> gsl_work;
  std::unique_ptr<gsl_matrix, gsl_matrix_deleter> gsl_X;
  std::unique_ptr<gsl_vector, gsl_vector_deleter> gsl_y[3];
  std::unique_ptr<gsl_vector, gsl_vector_deleter> gsl_c;
  std::unique_ptr<gsl_matrix, gsl_matrix_deleter> gsl_cov;
  bool least_squares_initialized;

  /* we ignore some outliers to get more realistic result.  */
  int noutliers = 0;

  /* Indexes into optimized values array to fetch individual parameters  */
  int coordinate_index;
  int fog_index;
  int color_index;
  int emulsion_intensity_index;
  int emulsion_offset_index;
  int emulsion_blur_index;
  int sharpen_index;
  int mtf_sigma_index;
  int mtf_defocus_index;
  int screen_index;
  int strips_index;
  int mix_weights_index;
  int mix_dark_index;
  /* Number of values needed.  */
  int n_values;
  int border;

  rgbdata fog_range;
  luminosity_t maxgray;
  luminosity_t mingray;
  luminosity_t min_nonone_clen;

  /* Global tracking of shared screen parameters.  */
  int screen_revision;
  rgbdata last_blur;
  luminosity_t last_scanner_mtf_sigma;
  rgbdata last_scanner_mtf_defocus;
  luminosity_t last_emulsion_blur;
  coord_t last_width, last_height;
  finetune_profile_accumulator *profile = nullptr;
  /* Dense displacement analysis may approximate scalar physical defocus from
     cached exact nodes.  The coarse prepass and the final objective remain
     exact.  */
  bool interpolate_scanner_mtf_defocus = false;
  coord_t scanner_mtf_defocus_interpolation_max = 0;
  int scanner_mtf_defocus_interpolation_nodes = 0;
  bool force_exact_scanner_mtf_defocus = false;
  /* Avoid traversing and locking the global linked-list LRU for focus nodes
     already acquired by this simplex.  Weak ownership preserves the global
     cache's bounded eviction behaviour.  */
  std::array<std::weak_ptr<screen>, 64> focus_screen_nodes;
public:
  /* Unblurred screen.  */
  std::shared_ptr<screen> original_scr;
  /* Screen with emulsion.  */
  std::shared_ptr<screen> emulsion_scr;
  sharpen_parameters render_sharpen_params;

  finetune_solver () {}

  void
  set_profile (finetune_profile_accumulator *p)
  {
    profile = p;
  }

  /* Optional hook consumed by NMSIMPLEX.H.  */
  void
  record_simplex_profile (int evaluations, int iterations)
  {
    if (!profile)
      return;
    profile->simplex_runs.fetch_add (1, std::memory_order_relaxed);
    profile->simplex_evaluations.fetch_add (evaluations,
                                            std::memory_order_relaxed);
    profile->simplex_iterations.fetch_add (iterations,
                                           std::memory_order_relaxed);
  }
  int n_tiles;
  /* All tiles have same width and height.  */
  int twidth, theight;
  int simulated_screen_border;
  int simulated_screen_width;
  int simulated_screen_height;

  std::vector<coord_t> start;
  /* True if openMP parallelism is desired.  */
  bool parallel;

  /* Legacy, currently unused scanner-MTF pointer.  Keep it until the solver
     construction cleanup tracked as FT-027 removes the remaining implicit
     initialization state.  */
  mtf *fixed_scanner_mtf = nullptr;

  /* Screen blur and strip widths. */
  coord_t fixed_blur, fixed_red_width, fixed_green_width, fixed_emulsion_blur;

  /* Range of position adjustment.  Dufay screen has squares about size of 0.5
     screen coordinates.  adjusting within -0.2 to 0.2 makes sure we do not
     exchange green for blue.  */
  constexpr static const coord_t dufay_range = 0.2;
  /* Paget range is smaller since there are more squares per screen period.
     Especially blue elements are small  */
  constexpr static const coord_t paget_range = 0.1;
  /* Screens with strips have strips with offset 1/3.
     Shifting too far may make us to mix up the strips.
     Be sure we do not move more than -1/6 to 1/6  */
  constexpr static const coord_t strips_range = (coord_t)1 / 6.0;

  coord_t pixel_size;
  scr_type type;

  /* 1 if we refine the coordinate system; 2 if we discover it completely.  */
  int optimize_coordinates;
  /* True if tile is already sharpened.  */
  bool tile_sharpened;
  /* Try to adjust position of center of the patches (+- range)  */
  bool optimize_position;
  /* Try adjusting coordinate1 (rotation/scale)  */
  bool optimize_coordinate1;
  /* Try to optimize scanner mtf sigma (gaussian blur) (otherwise fixed_mtf is
   * used, if any).  */
  bool optimize_scanner_mtf_sigma;
  /* Try to optimize screen blur attribute (otherwise fixed_defocus is used, if
   * any).  */
  bool optimize_scanner_mtf_defocus;
  /* Same but per-channel.  */
  bool optimize_scanner_mtf_channel_defocus;
  /* Try to optimize screen blur attribute (otherwise fixed_blur is used).  */
  bool optimize_screen_blur;
  /* Try to optimize screen blur independently in each channel.  */
  bool optimize_screen_channel_blurs;
  /* Try to optimize strip widths for dufay and strip screens
     (otherwise fixed_width, fixed_height is used).  */
  bool optimize_strips;
  /* Try to optimize dark point.  */
  bool optimize_fog;
  /* Try to optimize digital sharpening radius and amount.  */
  bool optimize_sharpening;
  /* Try to optimize blur caused by the historical film emulsion.  Unless the
     full emulsion-intensity model is active, capture/screen blur is fixed.  */
  bool optimize_emulsion_blur;
  /* Optimize colors using least squares method.
     Probably useful only for debugging and better to be true.  */
  bool least_squares;
  /* Determine color using data collection same as used by
     analyze_base_worker.  */
  bool data_collection;
  /* Normalize colors for simulation to uniform intensity.  This is useful
     in RGB simulation to eliminate underlying silver image (which works as
     neutral density filter) of the input scan is linear.  */
  bool normalize;
  /* Use simulated infrared channel when rendering.  */
  bool simulate_infrared;
  /* True if mixing weights should be optimized.  */
  bool optimize_mix_weights;
  /* True if mixing dark (in addition to fog) should be optimized.
     This is more an experiment.  Theoretically fog should be good way
     to avoid this  */
  bool optimize_mix_dark;
  /* True if we are using simulated infrared as source of tiles's bw.  */
  bool bw_is_simulated_infrared;

  /* True if per-tile uniform image-layer intensities should be finetuned.
     The historical name below is retained internally because the same
     machinery is also used by the emulsion-blur experiment.  */
  bool optimize_uniform_image_layer;
  bool optimize_emulsion_intensities;
  bool optimize_emulsion_offset;
  bool fog_by_least_squares;

  /* Threshold for data collection.  */
  luminosity_t collection_threshold;

  /* Scale for coordinate guess.  */
  coord_t min_scale, max_scale;
  coord_t min_rotate, max_rotate;

  /* Contrast determined.  */
  luminosity_t contrast;

  /* Optimized values of red, green, blue for RGB simulation
     and optimized intensities for BW simulation.
     Initialized by objfunc and can be reused after it is computed
     since get_colors is expensive.  */
  double_rgbdata last_red, last_green, last_blue, last_color;
  double_rgbdata last_fog;

  finetune_solver (const finetune_solver &) = delete;
  finetune_solver (finetune_solver &&) = default;
  finetune_solver &operator= (const finetune_solver &) = delete;
  finetune_solver &operator= (finetune_solver &&) = default;

  ~finetune_solver () = default;

  /* Return number of values to optimize non-linearly.  */
  int
  num_values () const
  {
    return n_values;
  }
  constexpr static const coord_t rgbscale = 1;

  /* Epsilon used by nonlinear solver.  */
  coord_t
  epsilon () const
  {
    /* the objective function computes average difference.
       1/65536 seems to be way too small epsilon.  */
    return (coord_t)1.0 / 10000;
  }

  /* Scale of original simplex.  */
  coord_t
  scale () const
  {
    return 0.1;
  }

  /* Should nonlinear solver output info?  */
  bool
  verbose () const
  {
    return false;
  }

  /* How many samples we work with.
     We ignore outliers and when sharpening also some border  */
  int
  sample_points () const
  {
    return (twidth - 2 * border) * (theight - 2 * border) * n_tiles
           - noutliers;
  }

  /* Return correction to the scr-to-img map for TILEID.
     Values are in vector V.  */
  point_t
  get_offset (coord_t *v, int tileid) const
  {
    if (!optimize_position)
      return tiles[tileid].fixed_offset;
    /* Increase search range when we guess coordinates completely.  */
    if (optimize_coordinates == 1)
      {
	luminosity_t range = 0.5;
        return { v[2 * tileid] * range, v[2 * tileid + 1] * range};
      }
    /* Screens with two-dimensional structure needs two offsets.  */
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        return { v[2 * tileid] * range, v[2 * tileid + 1] * range };
      }
    /* Screens with one-dimensional structure needs just one.  */
    else
      return { v[tileid] * strips_range, 0 };
  }

  /* Set correction to the scr-to-img-map OFF for TILEID.
     Store values in vector V.  */
  void
  set_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_position)
      {
        tiles[tileid].fixed_offset = off;
        return;
      }
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        v[2 * tileid] = off.x / range;
        v[2 * tileid + 1] = off.y / range;
      }
    else
      v[tileid] = off.x / strips_range;
  }

  /* Return displacement between the image layer and emulsion for TILEID.
     Values are in vector V.  */
  point_t
  get_emulsion_offset (coord_t *v, int tileid)
  {
    if (!optimize_emulsion_offset)
      return tiles[tileid].fixed_emulsion_offset;
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        /* Reduce range if is not removable and can only be
           adjusted by angle of scanner.  On the other hand
           increase the range for non-removable screens since there is
           lesser change to make off-by-one error when we have color of the
           patch.  */
        if (integrated_screen_p (type))
          range /= 2;
        else
          range *= 2;
        return { v[emulsion_offset_index + 2 * tileid] * range,
                 v[emulsion_offset_index + 2 * tileid + 1] * range };
      }
    else
      return { v[emulsion_offset_index + tileid] * (strips_range * (coord_t)2), 0 };
  }
  /* Set displacement between the image layer and emulsion OFF for TILEID.
     Store values in vector V.  */
  void
  set_emulsion_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_emulsion_offset)
      {
        tiles[tileid].fixed_emulsion_offset = off;
        return;
      }
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        /* Reduce range if is not removable and can only be
           adjusted by angle of scanner.  */
        if (integrated_screen_p (type))
          range /= 2;
        else
          range *= 2;
        v[emulsion_offset_index + 2 * tileid] = off.x / range;
        v[emulsion_offset_index + 2 * tileid + 1] = off.y / range;
      }
    else
      v[emulsion_offset_index + tileid] = off.x / (2 * strips_range);
  }

  /* Return current emulsion blur radius from vector V.  Value is in screen
     size. This does not use pixel_blur since physical emulsion is not
     dependent on scanner parameters.  */
  coord_t
  get_emulsion_blur_radius (coord_t *v)
  {
    if (!optimize_emulsion_blur)
      return fixed_emulsion_blur;
    /* screen::max_blur_radius allows so large blurs that the resulting
       process would be next to useless. Also the minimal blur is non-zero.  */
    return v[emulsion_blur_index] * (screen::max_blur_radius * (coord_t)0.2 - (coord_t)0.03)
           + (coord_t)0.03;
  }

  /* Set current emulsion blur radius BLUR.
     Store values in vector V.  */
  void
  set_emulsion_blur_radius (coord_t *v, coord_t blur)
  {
    if (!optimize_emulsion_blur)
      fixed_emulsion_blur = blur;
    else
      v[emulsion_blur_index]
          = (blur - (coord_t)0.03) / (screen::max_blur_radius * (coord_t)0.2 - (coord_t)0.03);
  }

  /* Return pixel blur for value V.  Do pixel blurs in the range 0.3 ...
     screen::max_blur_radius / pixel_size. Value is in pixels, not screen size.
     Scans are always bit blurred at pixel level, so 0.3 should be reasonable
     minima.  */
  coord_t
  pixel_blur (coord_t v)
  {
    return expand_range (v, (coord_t)0.3, screen::max_blur_radius / pixel_size);
  }
  /* Inverse of pixel_blur.  */
  coord_t
  rev_pixel_blur (coord_t v)
  {
    return shrink_range (v, (coord_t)0.3, screen::max_blur_radius / pixel_size);
  }

  /* Return sigma of screen from vector V. */
  coord_t
  get_scanner_mtf_sigma (coord_t *v)
  {
    if (!optimize_scanner_mtf_sigma)
      return render_sharpen_params.scanner_mtf.sigma;
    return v[mtf_sigma_index];
  }

  /* Return blur radius of screen from vector V. */
  coord_t
  get_scanner_mtf_defocus (coord_t *v)
  {
    if (!optimize_scanner_mtf_defocus && !optimize_scanner_mtf_channel_defocus)
      return render_sharpen_params.scanner_mtf.simulate_diffraction_p ()
                 ? render_sharpen_params.scanner_mtf.defocus
                 : render_sharpen_params.scanner_mtf.blur_diameter;
    if (optimize_scanner_mtf_channel_defocus)
      return (v[mtf_defocus_index] + v[mtf_defocus_index + 1]
              + v[mtf_defocus_index + 2])
             * ((coord_t)1 / (coord_t)3);
    return v[mtf_defocus_index];
  }
  /* Return blur radius of screen for individual channels from vector V. */
  rgbdata
  get_scanner_mtf_channel_defocus (coord_t *v)
  {
    if (!optimize_scanner_mtf_defocus && !optimize_scanner_mtf_channel_defocus)
      {
        auto r = render_sharpen_params.scanner_mtf.simulate_diffraction_p ()
                     ? render_sharpen_params.scanner_mtf.defocus
                     : render_sharpen_params.scanner_mtf.blur_diameter;
        return { (luminosity_t)r, (luminosity_t)r, (luminosity_t)r };
      }
    if (!optimize_scanner_mtf_channel_defocus)
      {
        luminosity_t b = v[mtf_defocus_index];
        return { b, b, b };
      }
    return { (luminosity_t)v[mtf_defocus_index],
             (luminosity_t)v[mtf_defocus_index + 1],
             (luminosity_t)v[mtf_defocus_index + 2] };
  }

  /* Return blur radius of screen from vector V. */
  coord_t
  get_blur_radius (coord_t *v)
  {
    if (optimize_screen_channel_blurs)
      return pixel_blur (
          (v[screen_index] + v[screen_index + 1] + v[screen_index + 2])
          * ((coord_t)1 / (coord_t)3));
    if (!optimize_screen_blur)
      return fixed_blur;
    return pixel_blur (v[screen_index]);
  }
  /* Set blur radius BLUR.
     Store values in vector V.  */
  void
  set_blur_radius (coord_t *v, coord_t blur)
  {
    if (optimize_screen_channel_blurs)
      abort ();
    if (!optimize_screen_blur)
      fixed_blur = blur;
    else
      v[screen_index] = rev_pixel_blur (blur);
    return;
  }

  /* Same as get_blur_radius but when optimizing blurs of individual
     channels from vector V.  */
  rgbdata
  get_channel_blur_radius (coord_t *v)
  {
    if (!optimize_screen_blur && !optimize_screen_channel_blurs)
      return { (luminosity_t)fixed_blur, (luminosity_t)fixed_blur,
               (luminosity_t)fixed_blur };
    if (!optimize_screen_channel_blurs)
      {
        coord_t b = pixel_blur (v[screen_index]);
        return { (luminosity_t)b, (luminosity_t)b, (luminosity_t)b };
      }
    return { (luminosity_t)pixel_blur (v[screen_index]),
             (luminosity_t)pixel_blur (v[screen_index + 1]),
             (luminosity_t)pixel_blur (v[screen_index + 2]) };
  }

  /* Return red strip width from vector V.  */
  coord_t
  get_red_strip_width (coord_t *v)
  {
    if (!optimize_strips)
      return fixed_red_width;
    return v[strips_index];
  }
  /* Return green strip width from vector V.  */
  coord_t
  get_green_strip_width (coord_t *v)
  {
    if (!optimize_strips)
      return fixed_green_width;
    return v[strips_index + 1];
  }
  /* Set red strip width W.
     Store values in vector V.  */
  void
  set_red_strip_width (coord_t *v, coord_t w)
  {
    if (!optimize_strips)
      fixed_red_width = w;
    else
      v[strips_index] = w;
  }
  /* Set green strip width W.
     Store values in vector V.  */
  void
  set_green_strip_width (coord_t *v, coord_t w)
  {
    if (!optimize_strips)
      fixed_green_width = w;
    else
      v[strips_index + 1] = w;
  }

  /* Scale of coordinate system for V.  */
  coord_t
  get_scale (coord_t *v) const
  {
    if (!optimize_coordinates)
      return 1;
    if (optimize_coordinates == 1)
      return v[coordinate_index] * 0.3 + 1;
    return (1 + v[coordinate_index]) * 0.5 * (max_scale - min_scale) + min_scale;
  }

  /* Rotation of coordinate system for V.  */
  coord_t
  get_rotation (coord_t *v) const
  {
    if (!optimize_coordinates)
      return 0;
    if (optimize_coordinates == 1)
      return v[coordinate_index + 1] * 25;
    return (1 + v[coordinate_index + 1]) * 0.5 * (max_rotate - min_rotate) + min_rotate;
  }

  /* Get screen coordinates of a given pixel P of a given tile TILEID.
     Values are in vector V.  */
  pure_attr point_t
  get_pos (coord_t *v, int tileid, int_point_t p) const
  {
    if (optimize_coordinates == 2)
      return transformation.apply ({(coord_t)p.x, (coord_t)p.y});
    if (optimize_coordinates)
      return transformation.apply (tiles[tileid].pos[p.y * twidth + p.x]);
    return tiles[tileid].pos[p.y * twidth + p.x] + get_offset (v, tileid);
  }

  /* Return true if we consider outliers.  */
  bool
  has_outliers ()
  {
    return noutliers;
  }

  /* Output values in vector V.  */
  void
  print_values (coord_t *v)
  {
    printf ("\n\nOptimizing %i values:", num_values ());
    if (optimize_coordinates == 1)
      printf (" coordinates");
    if (optimize_coordinates == 2)
      printf (" guess_coordinates");
    if (optimize_position)
      printf (" position");
    if (optimize_scanner_mtf_sigma)
      printf (" scanner_mtf_sigma");
    if (optimize_scanner_mtf_defocus)
      printf (" scanner_mtf_defocus");
    if (optimize_scanner_mtf_channel_defocus)
      printf (" scanner_mtf_channel_defocus");
    if (optimize_screen_blur)
      printf (" screen_blur");
    if (optimize_screen_channel_blurs)
      printf (" screen_channel_blur");
    if (optimize_strips)
      printf (" strips");
    if (optimize_fog)
      printf (" fog");
    if (optimize_sharpening)
      printf (" sharpening");
    if (optimize_emulsion_blur)
      printf (" emulsion_blur");
    if (optimize_emulsion_intensities)
      printf (" emulsion_intensities");
    if (optimize_emulsion_offset)
      printf (" emulsion_offset");
    if (optimize_mix_weights)
      printf (" mix_weights");
    if (optimize_mix_dark)
      printf (" mix_dark");
    printf ("\n");
    if (least_squares)
      printf (" Estimating color using least squares %s\n",
              fog_by_least_squares ? "(including fog)" : "");
    if (data_collection)
      printf (" Estimating color using data collection with threshold %f\n",
              collection_threshold);
    if (normalize)
      printf (" Normalizing colors to eliminate image layer\n");
    if (simulate_infrared)
      printf (" Simulating infrared scan to determine image layer\n");
    if (bw_is_simulated_infrared)
      printf (" Image layer is simulated from RGB scan\n");
    printf ("\n");
    if (sharpen_index >= 0)
      printf ("sharpen radius %f and amount %f\n", get_sharpen_radius (v),
              get_sharpen_amount (v));
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        if (optimize_position)
          {
            point_t p = get_offset (v, tileid);
            printf ("Screen offset %f %f (in pixels %f %f)\n", p.x, p.y,
                    p.x / pixel_size, p.y / pixel_size);
          }
	if (optimize_coordinates)
	  printf ("Scale %f rotation %f degrees\n", get_scale (v), get_rotation (v));
        if (optimize_emulsion_offset)
          {
            point_t p = get_emulsion_offset (v, tileid);
            printf ("Emulsion offset %f %f (%f %f in pixels; relative to "
                    "screen)\n",
                    p.x, p.y, p.x / pixel_size, p.y / pixel_size);
          }
      }
    if (optimize_emulsion_blur)
      printf ("Emulsion blur %f (%f pixels)\n", get_emulsion_blur_radius (v),
              get_emulsion_blur_radius (v) / pixel_size);
    if (optimize_screen_blur)
      printf ("Screen blur %f (pixel size %f, scaled %f)\n",
              get_blur_radius (v), pixel_size,
              get_blur_radius (v) * pixel_size);
    if (optimize_scanner_mtf_sigma)
      printf ("Scanner mtf sigma %f px\n", get_scanner_mtf_sigma (v));
    if (optimize_scanner_mtf_defocus)
      {
        if (!render_sharpen_params.scanner_mtf.simulate_diffraction_p ())
          printf ("Scanner mtf blur diameter %f px\n",
                  get_scanner_mtf_defocus (v));
        else
          printf ("Scanner mtf defocus %f mm\n", get_scanner_mtf_defocus (v));
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        rgbdata b = get_scanner_mtf_channel_defocus (v);
        if (!render_sharpen_params.scanner_mtf.simulate_diffraction_p ())
          printf ("Scanner mtf blur diameter %f px (red) %f px (green) %f px "
                  "(blue)\n",
                  b.red, b.green, b.blue);
        else
          printf (
              "Scanner mtf defocus %f mm (red) %f mm (green) %f mm (blue)\n",
              b.red, b.green, b.blue);
      }
    if (optimize_screen_channel_blurs)
      {
        rgbdata b = get_channel_blur_radius (v);
        printf ("Red screen blur %f (pixel size %f, scaled %f)\n", b.red,
                pixel_size, b.red * pixel_size);
        printf ("Green screen blur %f (pixel size %f, scaled %f)\n", b.green,
                pixel_size, b.green * pixel_size);
        printf ("Blue screen blur %f (pixel size %f, scaled %f)\n", b.blue,
                pixel_size, b.blue * pixel_size);
      }
    if (optimize_strips)
      {
        printf ("Red strip width: %f\n", get_red_strip_width (v));
        printf ("Green strip width: %f\n", get_green_strip_width (v));
      }
    if (!tiles[0].color.empty ())
      {
        double_rgbdata red, green, blue;
        get_colors (v, &red, &green, &blue);

        printf ("Red :");
        red.print (stdout);
        printf ("Normalized red :");
        luminosity_t sum = red.red + red.green + red.blue;
        (red / sum).print (stdout);

        printf ("Green :");
        green.print (stdout);
        printf ("Normalized green :");
        sum = green.red + green.green + green.blue;
        (green / sum).print (stdout);

        printf ("Blue :");
        blue.print (stdout);
        printf ("Normalized blue :");
        sum = blue.red + blue.green + blue.blue;
        (blue / sum).print (stdout);
        if (optimize_fog)
          {
            printf ("Fog ");
            get_fog (v).print (stdout);
            printf ("Fog range ");
            fog_range.print (stdout);
          }
        for (int tileid = 0; tileid < n_tiles; tileid++)
          if (optimize_emulsion_intensities)
            {
              printf ("Emulsion intensities tile %i ", tileid);
              get_emulsion_intensities (v, tileid).print (stdout);
            }
        if (!normalize)
          {
            printf ("Mix weights ");
            get_mix_weights (v).print (stdout);
            if (optimize_mix_dark)
              {
                printf ("Mix dark: %f\n", get_mix_dark (v));
              }
          }
      }
    if (!tiles[0].bw.empty ())
      {
        printf ("Max gray %f\n", maxgray);
        rgbdata color = bw_get_color (v);
        printf ("Intensities :");
        color.print (stdout);
        printf ("Normalized :");
        luminosity_t sum = color.red + color.green + color.blue;
        (color / sum).print (stdout);
      }
  }

  /* Constrain values in vector V to reasonable range.  */
  void
  constrain (coord_t *v)
  {
    if (optimize_coordinates)
      {
        to_range (v[coordinate_index], -1, 1);
        to_range (v[coordinate_index + 1], -1, 1);
      }
    /* x and y adjustments.  */
    if (optimize_position)
      {
        /* Two dimensional screens has two coordinates.  */
        if (!screen_with_vertical_strips_p (type))
          for (int tileid = 0; tileid < n_tiles; tileid++)
            {
              to_range (v[tileid * 2 + 0], -1, 1);
              to_range (v[tileid * 2 + 1], -1, 1);
            }
        /* One dimensional screens just one.  */
        else
          for (int tileid = 0; tileid < n_tiles; tileid++)
            to_range (v[tileid], -1, 1);
      }
    if (optimize_emulsion_offset)
      {
        /* Two dimensional screens has two coordinates.  */
        if (!screen_with_vertical_strips_p (type))
          for (int tileid = 0; tileid < n_tiles; tileid++)
            {
              to_range (v[emulsion_offset_index + 2 * tileid + 0], -1, 1);
              to_range (v[emulsion_offset_index + 2 * tileid + 1], -1, 1);
            }
        /* One dimensional screens just one.  */
        else
          for (int tileid = 0; tileid < n_tiles; tileid++)
            to_range (v[emulsion_offset_index + tileid], -1, 1);
      }
    if (fog_index >= 0)
      {
        assert (!colorscreen_checking || optimize_fog);
        to_range (v[fog_index + 0], (coord_t)-0.1 / (coord_t)fog_range.red, (coord_t)1);
        to_range (v[fog_index + 1], (coord_t)-0.1 / (coord_t)fog_range.green, (coord_t)1);
        to_range (v[fog_index + 2], (coord_t)-0.1 / (coord_t)fog_range.blue, (coord_t)1);
      }
    if (!tiles[0].bw.empty () && !least_squares && !data_collection)
      {
        /* If infrared channel is simulated, negative values may be possible
           and it is kind of hard to constrain to reasonable bounds.
           Still allow values somewhat out of range to account for possible
           over-exposure or cropping  */
        if (!bw_is_simulated_infrared)
          {
            to_range (v[color_index + 0], (coord_t)-0.1, (coord_t)1.1);
            to_range (v[color_index + 1], (coord_t)-0.1, (coord_t)1.1);
            to_range (v[color_index + 2], (coord_t)-0.1, (coord_t)1.1);
          }
      }
    if (sharpen_index >= 0)
      {
        to_range (v[sharpen_index], (coord_t)0, (coord_t)5);        // radius
        to_range (v[sharpen_index + 1], (coord_t)0, (coord_t)1000); // amount
      }
    if (optimize_emulsion_blur)
      to_range (v[emulsion_blur_index], (coord_t)0, (coord_t)1);
    if (optimize_emulsion_intensities)
      for (int i = 0; i < n_tiles * 3 - 1; i++)
        /* First 2 values are normalized and make only sense in range 0..1
           Rest of values are relative to the first two and may be large if
           first patch is dark.  */
        to_range (v[emulsion_intensity_index + i], (coord_t)0, i < 3 ? (coord_t)1 : (coord_t)100);
    if (optimize_screen_blur)
      to_range (v[screen_index], (coord_t)0, (coord_t)1);
    if (optimize_scanner_mtf_sigma)
      to_range (v[mtf_sigma_index], (coord_t)0, (coord_t)20);
    if (optimize_scanner_mtf_defocus)
      to_range (v[mtf_defocus_index], (coord_t)0,
                interpolate_scanner_mtf_defocus
                    ? scanner_mtf_defocus_interpolation_max
                    : (coord_t)20);
    if (optimize_scanner_mtf_channel_defocus)
      {
        to_range (v[mtf_defocus_index], (coord_t)0, (coord_t)20);
        to_range (v[mtf_defocus_index + 1], (coord_t)0, (coord_t)20);
        to_range (v[mtf_defocus_index + 2], (coord_t)0, (coord_t)20);
      }
    if (optimize_screen_channel_blurs)
      {
        /* Screen blur radius.  */
        to_range (v[screen_index + 0], (coord_t)0, (coord_t)1);
        to_range (v[screen_index + 1], (coord_t)0, (coord_t)1);
        to_range (v[screen_index + 2], (coord_t)0, (coord_t)1);
      }
    if (optimize_strips)
      {
        /* strip widths.  */
        if (type == Dufay)
          {
            /* Dufay screen is
               RR
               BG

               Red strip width is more narrow in Dufay screens
               so overall coverage of colors is equal.  */
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.6);
            /* Green strip width approx 0.5 in Dufay screens.  */
            to_range (v[strips_index + 1], (coord_t)0.3, (coord_t)0.7);
          }
        /* Dioptichrome screens come in various combinations
           and strip widths.  */
        else if (dufay_like_screen_p (type))
          {
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.7);
            to_range (v[strips_index + 1], (coord_t)0.1, (coord_t)0.7);
          }
        /* Widths of red, green and blue strip needs to sub to 1
           when we have screens with vertical strips.
           Constrain the to min width of 0.1.

           TODO: Warner powrie screens seems to have 4 strips with
           green second tiny green strip between red and blue.  */
        else if (screen_with_vertical_strips_p (type))
          {
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.7);
            to_range (v[strips_index + 1], (coord_t)0.1, (coord_t)0.9 - v[strips_index + 0]);
          }
        else
          abort ();
      }
  }

  /* Free data used by least squares solver.  */
  void
  free_least_squares ()
  {
    gsl_work.reset ();
    gsl_X.reset ();
    gsl_y[0].reset ();
    gsl_y[1].reset ();
    gsl_y[2].reset ();
    gsl_c.reset ();
    gsl_cov.reset ();
    least_squares_initialized = false;
  }

  /* Allocate least square solver.  */
  void
  alloc_least_squares ()
  {
    int matrixw;

    /* In color mode we produce 3 equations.  In easiest case

       ss is simulated screen (in screen colors after blur)
       red is screen's red color
       green is screen's green color
       blue is screen's blue color

       ss.red * red.red     + ss.green * green.red   + ss.blue * blue.red   =
       tile.red ss.green * red.green + ss.green * green.green + ss.blue *
       blue.green = tile.green ss.blue * red.blue   + ss.blue * green.blue   +
       ss.blue * blue.blue  = tile.blue

       So there are 3 independent equations with 3 variables (red.*, green.*,
       blue.*) each. Tile can be normalized or after fog applied.  If we apply
       fog, then we need to compute RHS each time, otherwise RHS is invariant
       and computed once.

       If fog is optimized we do:

       ss.red * red.red     + ss.green * green.red   + ss.blue * blue.red   +
       fog.red   = tile.red ss.green * red.green + ss.green * green.green +
       ss.blue * blue.green + fog.green = tile.green ss.blue * red.blue   +
       ss.blue * green.blue   + ss.blue * blue.blue  + fog.blue  = tile.blue

       In this case there are 3 independent equations with 4 variables. We also
       add

       fog.red   = 0;
       fog.green = 0;
       fog.blue  = 0;

       Now if infrared is simulated we first we use non-linear solver to guess
       mix_weights and fog. Image layer (simulated infrared) intensity is
       computed by:

       c = tile.red * mix_weights.red + tile.green * mix_weights.green +
       tile.blue * mix_weights.blue

       We know that screen colors should have all the same intensity when
       scalled by the formula above. So we have:

       red.red   * mix_weights.red + red.green   * mix_weights.green + red.blue
       * mix_weights.blue = cst green.red * mix_weights.red + green.green *
       mix_weights.green + green.blue * mix_weights.blue = cst blue.red  *
       mix_weights.red + blue.green  * mix_weights.green + blue.blue  *
       mix_weights.blue = cst

       (We put cst=1.) Consequently:

       red.blue = (cst - red.red * mix_weights.red - red.green *
       mix_weights.green) / mix_weights.blue green.blue = (cst - green.red *
       mix_weights.red - green.green * mix_weights.green) / mix_weights.blue
       blue.blue = (cst - blue.red * mix_weights.red - blue.green *
       mix_weights.green) / mix_weights.blue

       We want to optimize:

       ss.red * c * red.red + ss.green * c * green.red + ss.blue * c * blue.red
       = tile.red - fog.red ss.green * c * red.green + ss.green * c *
       green.green + ss.blue * c * blue.green = tile.green - fog.green ss.blue
       * c * red.blue + ss.green * c * green.blue + ss.blue * c * blue.blue =
       tile.blue - fog.blue

       There are 6 unknowns: red.red, red.green, green.red, green.green,
       blue.red and blue.green. Variables red.blue/green.blue/blue.blue are
       substituted by the identity above. We no longer can treat individual
       channels by independent equations.  */

    if (!tiles[0].color.empty ())
      {
        if (!simulate_infrared)
          {
            matrixw = 3;
            if (fog_by_least_squares)
              matrixw++;
          }
        else
          {
            matrixw = 6;
            if (fog_by_least_squares)
              matrixw += 3;
          }
      }
    /* In BW mode we guess intensity.  */
    else
      matrixw = 1;
    int matrixh = sample_points () + (fog_by_least_squares != 0);
    if (simulate_infrared)
      matrixh *= 3;
    gsl_work.reset (gsl_multifit_linear_alloc (matrixh, matrixw));
    gsl_X.reset (gsl_matrix_alloc (matrixh, matrixw));
    gsl_y[0].reset (gsl_vector_alloc (matrixh));
    least_squares_initialized = false;
    if (!tiles[0].color.empty () && !simulate_infrared)
      {
        gsl_y[1].reset (gsl_vector_alloc (matrixh));
        gsl_y[2].reset (gsl_vector_alloc (matrixh));
      }
    gsl_c.reset (gsl_vector_alloc (matrixw));
    gsl_cov.reset (gsl_matrix_alloc (matrixw, matrixw));
  }

  /* Initialize least square solver using values in vector V.
     Only those values which do not change during optimization are computed.
     Rest is done later.

     In most settings we do not use least squares here and base everything on
     data collection.  */
  void
  init_least_squares (coord_t *v)
  {
    last_fog = { 0, 0, 0 };

    /* In color we solve 3 independent equations for red, green and blue
       channel. Initialize RHS sides which are invariant.  */
    if (!tiles[0].color.empty () && !simulate_infrared)
      {
        int e = 0;

        /* RHS side of all equations should expect the tile's color.  */
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata d = fog_by_least_squares
                                  ? get_pixel_nofog (tileid, { x, y })
                                  : get_pixel (v, tileid, { x, y });
                  gsl_vector_set (gsl_y[0].get (), e, d.red);
                  gsl_vector_set (gsl_y[1].get (), e, d.green);
                  gsl_vector_set (gsl_y[2].get (), e, d.blue);
                  e++;
                }
        /* We want fog to be 0.  */
        if (fog_by_least_squares)
          {
            gsl_vector_set (gsl_y[0].get (), e, 0);
            gsl_vector_set (gsl_y[1].get (), e, 0);
            gsl_vector_set (gsl_y[2].get (), e, 0);
            e++;
          }
        if (e != (int)gsl_y[0]->size)
          abort ();
      }
    /* In infrared simulation we set everything later.  */
    else if (!tiles[0].color.empty () && simulate_infrared)
      ;
    /* In BW mode there is only one equation to compute.  */
    else
      {
        int e = 0;
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  gsl_vector_set (gsl_y[0].get (), e,
                                  bw_get_pixel (tileid, { x, y })
                                      / (2 * maxgray));
                  e++;
                }
        if (e != (int)gsl_y[0]->size)
          abort ();
      }
    least_squares_initialized = true;
  }

  /* Used to set up optimization of tile TILEID
     with left corner (CUR_TXMIN, CUR_TYMIN) and screen-to-image map MAP.
     if BW is true, ignore color data.  RENDER is used to fetch data.  */
  bool
  init_tile (int tileid, int cur_txmin, int cur_tymin, bool bw,
             scr_to_img &map, render &render)
  {
    tiles[tileid].txmin = cur_txmin;
    tiles[tileid].tymin = cur_tymin;
    type = map.get_type ();
    if (!bw)
      tiles[tileid].color.resize (twidth * theight);
    else
      tiles[tileid].bw.resize (twidth * theight);

    tiles[tileid].pos.resize (twidth * theight);
    if ((tiles[tileid].color.empty () && tiles[tileid].bw.empty ())
        || tiles[tileid].pos.empty ())
      return false;
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  tiles[tileid].pos[y * twidth + x]
	      = map.to_scr ({ cur_txmin + x + (coord_t)0.5, cur_tymin + y + (coord_t)0.5 });
	  if (!tiles[tileid].color.empty ())
	    tiles[tileid].color[y * twidth + x]
		= render.get_unadjusted_rgb_pixel (
		    { x + cur_txmin, y + cur_tymin });
	  if (!tiles[tileid].bw.empty ())
	    tiles[tileid].bw[y * twidth + x] = render.get_unadjusted_data (
		{ x + cur_txmin, y + cur_tymin });
	}
    return true;
  }

  /* This is hack used for coordinate discovery experiment.
     We should avoid need to copy everything from main solver. */
  bool
  copy_tile (int tileid, finetune_solver &other)
  {
    tiles[tileid].txmin = other.tiles[tileid].txmin;
    tiles[tileid].tymin = other.tiles[tileid].tymin;
    type = other.type;
    tiles[tileid].pos = other.tiles[tileid].pos;
    tiles[tileid].color = other.tiles[tileid].color;
    tiles[tileid].bw = other.tiles[tileid].bw;
    return true;
  }

  /* Init solver with FPARAMS, BLUR_RADIUS, RED_STRIP_WIDTH,
     GREEN_STRIP_WIDTH.
     If SIM_INFRARED is true, simulate infrared channel.
     IS_TILE_SHARPENED indicates if tile is already sharpened.
     RESULTS are previous results if any.  */
  void
  init (const finetune_parameters &fparams, coord_t blur_radius,
        coord_t red_strip_width,
        coord_t green_strip_width, bool sim_infrared, bool is_tile_sharpened,
        const std::vector<finetune_result> *results)
  {
    const uint64_t flags = fparams.flags;
    /* Keep disabled-parameter indexes harmless.  Most accessors are guarded
       by optimization flags, but explicit sentinels make accidental future
       use deterministic instead of indexing through an uninitialized value.  */
    coordinate_index = fog_index = color_index = emulsion_intensity_index
        = emulsion_offset_index = emulsion_blur_index = sharpen_index
        = mtf_sigma_index = mtf_defocus_index = screen_index = strips_index
        = mix_weights_index = mix_dark_index = -1;
    bw_is_simulated_infrared = sim_infrared;

    /* First decide on what to optimize.  */
    tile_sharpened = is_tile_sharpened;
    if (flags & finetune_guess_coordinates)
      optimize_coordinates = 2;
    else if (flags & finetune_coordinates)
      optimize_coordinates = 1;
    else
      optimize_coordinates = 0;
    optimize_position = flags & finetune_position;
    optimize_coordinate1 = flags & finetune_coordinates;
    optimize_screen_blur = flags & finetune_screen_blur;
    optimize_scanner_mtf_sigma = flags & finetune_scanner_mtf_sigma;
    optimize_scanner_mtf_defocus = flags & finetune_scanner_mtf_defocus;
    optimize_scanner_mtf_channel_defocus
        = flags & finetune_scanner_mtf_channel_defocus;
    interpolate_scanner_mtf_defocus
        = fparams.interpolate_scanner_mtf_defocus;
    scanner_mtf_defocus_interpolation_max
        = fparams.scanner_mtf_defocus_interpolation_max;
    scanner_mtf_defocus_interpolation_nodes
        = fparams.scanner_mtf_defocus_interpolation_nodes;
    force_exact_scanner_mtf_defocus = false;
    for (std::weak_ptr<screen> &node : focus_screen_nodes)
      node.reset ();
    optimize_screen_channel_blurs = flags & finetune_screen_channel_blurs;
    optimize_emulsion_blur = flags & finetune_emulsion_blur;
    optimize_uniform_image_layer
        = (flags & finetune_uniform_image_layer) && !tiles[0].color.empty ();
    optimize_strips
        = (flags & finetune_strips) && screen_with_varying_strips_p (type);
    /* Strips needs to be optimized only for some screens, like Dufay, Joly or
     * Powrie.  */
    if (!screen_with_varying_strips_p (type))
      optimize_strips = false;
    /* For one tile the effect of fog can always be simulated by adjusting the
       colors of screen. If multiple tiles (and colors) are sampled we can try
       to estimate it.  */
    optimize_fog = (flags & finetune_fog) && !tiles[0].color.empty () /*&& n_tiles > 1*/;
    /* Colors can be determined either by data collection, least squares
       or using nonlinear solver.  Data collection is fastest, but only works
       if threshold and blurs are meaningful.  Second two should be equivalent
       with least squares being faster and more robust (avoiding local
       minima).

       We later use at most one of least squares and data collection.  Mode
       that does not make sense is turned off.  */
    least_squares = !(flags & finetune_no_least_squares);
    data_collection = !(flags & finetune_no_data_collection);
    simulate_infrared = (flags & finetune_simulate_infrared) && !tiles[0].color.empty ();
    optimize_sharpening
        = (flags & finetune_sharpening) && !tiles[0].color.empty ();
    optimize_mix_weights = false;
    optimize_mix_dark = false;
    optimize_emulsion_offset = false;
    /* TODO; We probably can sharpen and normalize.  */
    normalize = !(flags & finetune_no_normalize) && !optimize_sharpening
                && !tiles[0].color.empty ();
    /* Uniform-colour multi-tile fitting models the image layer explicitly.
       Per-pixel RGB normalization would divide out the very tile-dependent
       primary intensities being fitted, and data collection cannot separate
       one shared screen response from several tile intensity vectors.  Do
       this before the normalization/emulsion-blur compatibility check below
       so an explicitly requested emulsion blur is not disabled merely because
       normalization would otherwise have been the default.  */
    if (optimize_uniform_image_layer)
      data_collection = normalize = false;
    /* Normalization turns every color of every pixel to have sum of 1.
       This simplifies the optimization since it effectively removes
       the image layer and we can more easily estimate screen position and
       blur.  However this removal is not precise since it can not account for
       scan sharpness.  It is not useful to optimize emulsion blur.
       */
    if (!tiles[0].color.empty () && normalize)
      optimize_emulsion_blur = false;
    /* In infrared simulation we try to estimate the image layer.
       We want to be extra precise, so do not use data collection.
       Emulsion blur so far really has only chance to work on areas of
       solid saturated color.  */
    if (simulate_infrared)
      {
        data_collection = normalize = optimize_emulsion_blur = false;
        if (least_squares)
          optimize_mix_weights = optimize_mix_dark = true;
      }
    /* The historical emulsion-blur experiment also needs per-tile primary
       intensities and a local emulsion offset.  Uniform-image-layer focus
       fitting reuses only the intensity part: screen primaries and capture
       transfer stay shared while local offsets remain fixed.  */
    const bool coupled_emulsion_fit
        = !tiles[0].color.empty () && optimize_emulsion_blur
          && (optimize_screen_blur || optimize_screen_channel_blurs
              || optimize_scanner_mtf_sigma || optimize_scanner_mtf_defocus
              || optimize_scanner_mtf_channel_defocus);
    optimize_emulsion_intensities
        = optimize_uniform_image_layer || coupled_emulsion_fit;
    optimize_emulsion_offset = coupled_emulsion_fit;
    if (optimize_emulsion_intensities)
      data_collection = false;

    /* To optimize emulsion blur we can either assume that screen blur
       is already known and use it or try to simulate everything including
       intensities.  */
    if (optimize_emulsion_blur && !optimize_emulsion_intensities)
      optimize_screen_blur = optimize_screen_channel_blurs
          = optimize_scanner_mtf_sigma = optimize_scanner_mtf_defocus
          = optimize_scanner_mtf_channel_defocus = false;
    /* When simulating infrared fog needs to be subtracted before applying
       mixing weights. This makes equations non-linear.  */
    fog_by_least_squares
        = (optimize_fog && !normalize && least_squares) && !simulate_infrared;
    // fog_by_least_squares = 0;

    /* Data collection is faster, so if available prefer it over least
       squares.  */
    if (data_collection)
      least_squares = false;

    /* Next determine values to optimize.  */

    n_values = 0;
    /* Position needs 1 or 2 values per tile depending on if screen
       is 2d or 1d.  */
    if (optimize_position)
      n_values += (1 + !screen_with_vertical_strips_p (type)) * n_tiles;

    if (optimize_coordinates)
      {
        coordinate_index = n_values;
        n_values += 2;
      }
    else
      coordinate_index = -1;

    /* When optimizing sharpening, be ready for borders of the tile to not be
       right. Also allocate the memory buffer.  */
    if (optimize_sharpening)
      {
        sharpen_index = n_values;
        border = 10;
        n_values += 2;
        for (int i = 0; i < n_tiles; i++)
          tiles[i].sharpened_color_buffer.resize (twidth * theight);
        /* Determine minimum meaningful sharpening radius.  */
        min_nonone_clen = 0.3;
      }
    else
      {
        sharpen_index = -1;
        border = 0;
        for (int i = 0; i < n_tiles; i++)
          tiles[i].sharpened_color = tiles[i].color.data ();
      }

    /* When not doing data collection or least squares, we need to optimize
     * colors.  */
    if (!least_squares && !data_collection)
      {
        color_index = n_values;
        /* 3*3 values for color.
           3 intensities for B&W  */
        if (!tiles[0].color.empty ())
          n_values += 9;
        else
          n_values += 3;
      }
    else
      color_index = -1;

    /* Fog is RGB value.  We can not determine it in BW, since it can not
       be separated from color.  */
    if (optimize_fog && !fog_by_least_squares)
      {
        fog_index = n_values;
        n_values += 3;
      }
    else
      fog_index = -1;

    /* If we do not use least squares, mixing weights are 2 values.
       Last value is complement of the other two since we optimize
       screen colors freely.

       If we use least squares then we need all 3 values since screen
       colors are normalized to have sum of (1,1,1) so we save some
       variables.  */
    if (optimize_mix_weights)
      {
        mix_weights_index = n_values;
        n_values += 2 + (least_squares != 0);
      }
    else
      mix_weights_index = -1;

    if (optimize_mix_dark)
      {
        mix_dark_index = n_values;
        n_values++;
      }
    else
      mix_dark_index = -1;

    /* Try to guess the intensity of the emulsion below each primary color.
       Used when trying to determine emulsion blur.
       This must be per-tile since every tile is assumed to have different
       color (but uniform in each tile).  */
    if (optimize_emulsion_intensities)
      {
        emulsion_intensity_index = n_values;
        n_values += 3 * n_tiles - 1;
      }
    else
      emulsion_intensity_index = -1;
    if (optimize_emulsion_offset)
      {
        emulsion_offset_index = n_values;
        n_values += (1 + !screen_with_vertical_strips_p (type)) * n_tiles;
      }
    else
      emulsion_offset_index = -1;
    if (optimize_emulsion_blur)
      {
        emulsion_blur_index = n_values;
        n_values += 1;
      }
    else
      emulsion_blur_index = -1;

    /* Screen index has different meanings depending on how well
       we want to estimate the blur.  */
    if (optimize_screen_channel_blurs)
      {
        screen_index = n_values;
        optimize_screen_blur = false;
        n_values += 3;
        assert (!optimize_screen_blur);
      }
    else if (optimize_screen_blur)
      {
        screen_index = n_values;
        n_values++;
      }
    else
      screen_index = -1;

    if (optimize_scanner_mtf_sigma)
      {
        mtf_sigma_index = n_values;
        n_values++;
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        mtf_defocus_index = n_values;
        optimize_scanner_mtf_defocus = false;
        n_values += 3;
      }
    else if (optimize_scanner_mtf_defocus)
      {
        mtf_defocus_index = n_values;
        n_values++;
      }

    if (optimize_strips)
      {
        strips_index = n_values;
        n_values += 2;
      }
    else
      strips_index = -1;

    /* We know number of values to optimize; allocate them and get initial
       values.  */
    start.resize (n_values, 0);
    /* Poison values, so we know we initialized them all.  */
    if (colorscreen_checking)
      {
        for (int i = 0; i < n_values; i++)
          start[i] = INT_MAX;
        for (int i = 0; i < n_values; i++)
          assert (start[i] == INT_MAX);
      }

    /* Allocate also memory for all simulations.  */
    original_scr = std::make_shared<screen> ();
    if (optimize_emulsion_blur)
      emulsion_scr = std::make_shared<screen> ();
    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        tiles[tileid].merged_scr = std::make_unique<screen> ();
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].scr = std::make_shared<screen> ();

    /* Set up cached values.   */
    screen_revision = 0;
    last_blur = { -1, -1, -1 };
    last_scanner_mtf_sigma = -1;
    last_scanner_mtf_defocus = { -1, -1, -1 };
    last_emulsion_blur = -1;
    last_width = -1;
    last_height = -1;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        tiles[tileid].last_emulsion_intensities = { -1, -1, -1 };
        tiles[tileid].last_emulsion_offset = { -100, -100 };
        tiles[tileid].last_screen_revision = -1;
        tiles[tileid].last_simulated_offset = { -100, -100 };
      }
    last_fog = { 0, 0, 0 };

    /* If we are not reusing older results, offset should be 0
       since we assume scr-to-img map to be meaningful.  */
    if (!results)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          set_offset (start.data (), tileid, { 0, 0 });
          set_emulsion_offset (start.data (), tileid, { 0, 0 });
        }
    else
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          set_offset (start.data (), tileid, (*results)[tileid].screen_coord_adjust);
          set_emulsion_offset (start.data (), tileid,
                               (*results)[tileid].emulsion_coord_adjust);
        }
    if (optimize_coordinates)
      {
	if (optimize_coordinates == 2)
        /* scale = (min_scale + max_scale)/2 */
          start [coordinate_index] = 0;
	else
        /* scale = 1 */
          start [coordinate_index] = 0;
	/* rotation = (min_rotation + max_rotation)/2  */
        start [coordinate_index + 1] = 0;
      }

    /* Always start from scratch with sharpening; otherwise the optimizer tends
       to pick up very large values.  */
    if (sharpen_index >= 0)
      {
        start[sharpen_index] = 0;
        start[sharpen_index + 1] = 0;
      }

    /* Start with color being red, green and blue. */
    if (color_index >= 0)
      {
        if (!tiles[0].color.empty ())
          {
            start[color_index] = finetune_solver::rgbscale;
            start[color_index + 1] = 0;
            start[color_index + 2] = 0;

            start[color_index + 3] = 0;
            start[color_index + 4] = finetune_solver::rgbscale;
            start[color_index + 5] = 0;

            start[color_index + 6] = 0;
            start[color_index + 7] = 0;
            start[color_index + 8] = finetune_solver::rgbscale;
          }
        else
          {
            start[color_index] = 0;
            start[color_index + 1] = 0;
            start[color_index + 2] = 0;
          }
      }

    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < 3 * n_tiles - 1; tileid++)
        start[emulsion_intensity_index + tileid] = (coord_t)1 / (coord_t)3;
    /* Starting from small blur seems to work better, since other parameters
       are then more relevant.  Sane scanner lens blurs are close to Nyquist
       frequency.  */
    if (optimize_emulsion_blur)
      {
        coord_t blur = (coord_t)0.03;
        if (results)
          {
            histogram hist;
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.pre_account ((*results)[tileid].emulsion_blur_radius);
            hist.finalize_range (65535);
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.account ((*results)[tileid].emulsion_blur_radius);
            hist.finalize ();
            blur = hist.find_avg ((coord_t)0.1);
          }
        set_emulsion_blur_radius (start.data (), blur);
        if (my_fabs (get_emulsion_blur_radius (start.data ()) - blur) > (coord_t)0.01)
          {
            printf ("Emulsion blur %f %f\n", get_emulsion_blur_radius (start.data ()),
                    blur);
            abort ();
          }
      }
    /* Avoid valgrind warnings on undefined values.  We will not really
       use the value, but we will read it to set up last value tracking  */
    else
      set_emulsion_blur_radius (start.data (), -1);
    if (optimize_screen_channel_blurs)
      {
        coord_t initial_blur = (coord_t)0.3;
        if ((flags & finetune_use_screen_blur) && my_isfinite (blur_radius))
          initial_blur = blur_radius;
        coord_t initial_value = rev_pixel_blur (initial_blur);
        to_range (initial_value, (coord_t)0, (coord_t)1);
        start[screen_index] = start[screen_index + 1]
            = start[screen_index + 2] = initial_value;
      }
    else
      {
        /* Optimizations seem to work better when it starts from small blur. */
        if (optimize_screen_blur && !results)
          {
            if (!(flags & finetune_use_screen_blur))
              blur_radius = (coord_t)0.3;
          }
        if (results)
          {
            histogram hist;
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.pre_account ((*results)[tileid].screen_blur_radius);
            hist.finalize_range (65535);
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.account ((*results)[tileid].screen_blur_radius);
            hist.finalize ();
            blur_radius = hist.find_avg ((coord_t)0.1);
          }
        set_blur_radius (start.data (), blur_radius);
        if (my_fabs (get_blur_radius (start.data ()) - blur_radius) > (coord_t)0.01)
          {
            printf ("Screen blur %f %f\n", get_blur_radius (start.data ()),
                    blur_radius);
            abort ();
          }
      }
    /* Start MTF optimization from the current rendering parameters.  The
       adaptive-focus prepass stores its robust global estimate there before
       constructing the dense-grid solvers.  Starting every solver at zero
       discarded that information and forced many expensive periodic-screen
       rebuilds while the simplex travelled back to the known neighbourhood.  */
    if (optimize_scanner_mtf_sigma)
      {
        coord_t sigma = render_sharpen_params.scanner_mtf.sigma;
        if (!my_isfinite (sigma) || sigma < 0)
          sigma = 0;
        start[mtf_sigma_index] = sigma;
        to_range (start[mtf_sigma_index], (coord_t)0, (coord_t)20);
      }
    /* Physical and measured-MTF fits can reuse the caller's current estimate.
       Keep the empirical fallback on its historical zero-blur boundary; see
       FINETUNE_INITIAL_SCANNER_MTF_FOCUS for the basin-selection rationale.  */
    coord_t defocus
        = finetune_initial_scanner_mtf_focus (render_sharpen_params.scanner_mtf);
    if (optimize_scanner_mtf_defocus)
      {
        start[mtf_defocus_index] = defocus;
        to_range (start[mtf_defocus_index], (coord_t)0,
                  interpolate_scanner_mtf_defocus
                      ? scanner_mtf_defocus_interpolation_max
                      : (coord_t)20);
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        start[mtf_defocus_index] = defocus;
        start[mtf_defocus_index + 1] = defocus;
        start[mtf_defocus_index + 2] = defocus;
        to_range (start[mtf_defocus_index], (coord_t)0, (coord_t)20);
        to_range (start[mtf_defocus_index + 1], (coord_t)0, (coord_t)20);
        to_range (start[mtf_defocus_index + 2], (coord_t)0, (coord_t)20);
      }
    if (flags & finetune_use_strip_widths)
      {
        set_red_strip_width (start.data (), red_strip_width);
        set_green_strip_width (start.data (), green_strip_width);
      }
    /* Default Dufaycolor strip widths.  */
    else if (type == Dufay)
      {
        set_red_strip_width (start.data (), dufaycolor::red_strip_width);
        set_green_strip_width (start.data (), dufaycolor::green_strip_width);
      }
    /* Dioptichromes seem to be printed with strips of equal widths.  */
    else if (dufay_like_screen_p (type))
      {
        set_red_strip_width (start.data (), (coord_t)0.5);
        set_green_strip_width (start.data (), (coord_t)0.5);
      }
    /* Joly and Warner-Powrie should be approx 1/3 each.  */
    else
      {
        set_red_strip_width (start.data (), (coord_t)1.0 / (coord_t)3);
        set_green_strip_width (start.data (), (coord_t)1.0 / (coord_t)3);
      }
    if (fog_index >= 0)
      {
        start[fog_index + 0] = 0;
        start[fog_index + 1] = 0;
        start[fog_index + 2] = 0;
      }
    if (mix_weights_index >= 0)
      {
        start[mix_weights_index + 0] = (coord_t)1.0 / (coord_t)3;
        start[mix_weights_index + 1] = (coord_t)1.0 / (coord_t)3;
        if (least_squares)
          start[mix_weights_index + 2] = (coord_t)1.0 / (coord_t)3;
      }
    if (mix_dark_index >= 0)
      start[mix_dark_index] = 0;

    /* Verify that everything is set up.  */
    if (colorscreen_checking)
      for (int i = 0; i < n_values; i++)
        assert (start[i] != INT_MAX);

    /* Once values are set up, be sure they are in range.  This should be NOOP
       most of time unless we get mad input.  */
    constrain (start.data ());

    /* Maxgray is used to normalize equations for least squares to reduce
       numeric errors.  */
    maxgray = mingray = 0;
    if (!tiles[0].bw.empty ())
      {
        mingray = maxgray = bw_get_pixel (0, { 0, 0 });
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = 0; y < theight; y++)
            for (int x = 0; x < twidth; x++)
              {
                maxgray = std::max (maxgray, bw_get_pixel (tileid, { x, y }));
                mingray = std::min (mingray, bw_get_pixel (tileid, { x, y }));
              }
      }

    /* Fog should not be much greater than minimal value in the tile.  */
    if (optimize_fog)
      {
        rgb_histogram hist;
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = 0; y < theight; y++)
            for (int x = 0; x < twidth; x++)
              hist.pre_account (tiles[tileid].color[y * twidth + x]);
        hist.finalize_range (65535);
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = 0; y < theight; y++)
            for (int x = 0; x < twidth; x++)
              hist.account (tiles[tileid].color[y * twidth + x]);
        hist.finalize ();
        fog_range = hist.find_min ((coord_t)0.1);
        if (!(fog_range.red > 0))
          fog_range.red = (luminosity_t)4 / (luminosity_t)65536;
        if (!(fog_range.green > 0))
          fog_range.green = (luminosity_t)4 / (luminosity_t)65536;
        if (!(fog_range.blue > 0))
          fog_range.blue = (luminosity_t)4 / (luminosity_t)65536;
      }
    else
      assert (!colorscreen_checking || fog_index == -1);

    /* Normalize tile.  This depends on fog, so with fog optimization
       we normalize later.  */
    if (normalize && !optimize_fog)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              rgbdata &c = tiles[tileid].color[y * twidth + x];
              luminosity_t sum = c.red + c.green + c.blue;
              if (sum > 0)
                c /= sum;
            }

    if (least_squares)
      {
        free_least_squares ();
        alloc_least_squares ();
        if (!optimize_fog || fog_by_least_squares)
          init_least_squares (nullptr);
      }
    simulated_screen_border = 0;
    simulated_screen_width = twidth;
    simulated_screen_height = theight;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].simulated_screen.resize (
          simulated_screen_width * simulated_screen_height);
  }

  void
  compute_contrast ()
  {
    if (!tiles[0].bw.empty ())
      contrast = get_positional_color_contrast (type, last_color,
                                                optimize_coordinates == 2);
    else
      contrast = std::max (
          { get_positional_color_contrast (
                type,
                { (luminosity_t)last_red.red,
                  (luminosity_t)last_green.red,
                  (luminosity_t)last_blue.red },
                optimize_coordinates == 2),
            get_positional_color_contrast (
                type,
                { (luminosity_t)last_red.green,
                  (luminosity_t)last_green.green,
                  (luminosity_t)last_blue.green },
                optimize_coordinates == 2),
            get_positional_color_contrast (
                type,
                { (luminosity_t)last_red.blue,
                  (luminosity_t)last_green.blue,
                  (luminosity_t)last_blue.blue },
                optimize_coordinates == 2) });
  }

  /* Scale the minimum objective returned by SIMPLEX by the contrast available
     for registration.  This is a heuristic fit-quality score, not a
     statistical uncertainty or a simplex-spread estimate.  OBJFUNC has
     already updated the fitted colors.  */
  coord_t
  scale_fit_score_by_contrast (coord_t objective)
  {
    /* OBJFUNC uses the largest finite value as a hard failure sentinel when
       screen construction or sample evaluation fails.  Do not turn that
       sentinel into an apparently valid capped score, and do not inspect the
       fitted-colour caches because a failed objective need not refresh them.  */
    if (!my_isfinite (objective) || objective < 0
        || objective == std::numeric_limits<coord_t>::max ())
      return std::numeric_limits<coord_t>::max ();
    compute_contrast ();
    if (my_isfinite (contrast) && contrast > 1 / (luminosity_t)65535)
      return std::min (objective / contrast, (coord_t)(10000000));
    return 100000000;
  }

  /* Invoke solver.  If REPORT is true, set progress report.
     PROGRESS is used to report progress.
     This may be disabled if we run in OpenMP parallel.  */
  coord_t
  solve (progress_info *progress, bool report)
  {
    // if (verbose)
    // solver.print_values (solver.start);
    coord_t objective = simplex<coord_t, finetune_solver> (
        *this, "finetuning", progress, report);
    if (interpolate_scanner_mtf_defocus)
      objective = evaluate_final_focus_exactly ();
    coord_t score = scale_fit_score_by_contrast (objective);
    free_least_squares ();
    return score;
  }

  /* Re-evaluate the fitted scalar-defocus point using the exact physical
     filter.  Approximation is useful while simplex explores the objective,
     but the reported score, fitted colours and final result must describe the
     real forward model.  Leave exact mode active so outlier detection and
     SET_RESULTS can reuse the exact screen.  */
  coord_t
  evaluate_final_focus_exactly ()
  {
    if (!interpolate_scanner_mtf_defocus)
      return objfunc (start.data ());
    if (!force_exact_scanner_mtf_defocus)
      {
        force_exact_scanner_mtf_defocus = true;
        screen_revision++;
      }
    return objfunc (start.data ());
  }

  /* Resume the interpolated objective before another simplex pass.  The
     screen revision invalidates the exact final screen even when the first
     trial happens to reuse the same defocus value.  */
  void
  resume_interpolated_focus ()
  {
    if (interpolate_scanner_mtf_defocus
        && force_exact_scanner_mtf_defocus)
      {
        force_exact_scanner_mtf_defocus = false;
        screen_revision++;
      }
  }

  bool
  interpolated_focus_p () const
  {
    return interpolate_scanner_mtf_defocus;
  }

  /* Get screen pixel for simulated screen TILE at point P.  */
  rgbdata
  get_simulated_screen_pixel (int tile, int_point_t p)
  {
    return tiles[tile].simulated_screen[p.y * simulated_screen_width + p.x];
  }

  /* Return true when the periodic screen is filtered through the scanner MTF
     rather than through the legacy Gaussian screen-blur path.  */
  bool
  scanner_mtf_filter_p () const
  {
    return optimize_scanner_mtf_sigma || optimize_scanner_mtf_defocus
           || optimize_scanner_mtf_channel_defocus
           || (!optimize_screen_blur && !optimize_screen_channel_blurs);
  }

  /* Build the exact per-channel capture parameters represented by V.  */
  std::array<sharpen_parameters, 3>
  capture_sharpen_parameters (coord_t *v)
  {
    std::array<sharpen_parameters, 3> sp
        = { render_sharpen_params, render_sharpen_params,
            render_sharpen_params };
    for (int c = 0; c < 3; c++)
      {
        sp[c].scanner_mtf.sigma = get_scanner_mtf_sigma (v);
        sp[c].scanner_mtf_scale *= pixel_size;
      }

    if (sp[0].scanner_mtf.simulate_diffraction_p ())
      {
        const rgbdata defocus = get_scanner_mtf_channel_defocus (v);
        sp[0].scanner_mtf.defocus = defocus.red;
        sp[1].scanner_mtf.defocus = defocus.green;
        sp[2].scanner_mtf.defocus = defocus.blue;
        if (!tiles[0].color.empty ())
          for (int c = 0; c < 3; c++)
            sp[c].scanner_mtf.wavelength = 550;
#if 0
        /* TODO: Apply scanner spectral response before wavelength-specific
           defocus, then use measured channel wavelengths here.  */
        sp[0].scanner_mtf.wavelength = 466;
        sp[1].scanner_mtf.wavelength = 526;
        sp[2].scanner_mtf.wavelength = 653;
#endif
      }
    else
      {
        const rgbdata blur_diameter = get_scanner_mtf_channel_defocus (v);
        sp[0].scanner_mtf.blur_diameter = blur_diameter.red;
        sp[1].scanner_mtf.blur_diameter = blur_diameter.green;
        sp[2].scanner_mtf.blur_diameter = blur_diameter.blue;
      }
    return sp;
  }

  /* Obtain writable storage without modifying an exact screen still owned by
     the shared focus cache.  */
  screen *
  writable_tile_screen (int tileid)
  {
    if (!tiles[tileid].scr || tiles[tileid].scr.use_count () != 1)
      tiles[tileid].scr = std::make_shared<screen> ();
    return tiles[tileid].scr.get ();
  }

  /* Exact cached focus nodes are valid only when the capture transfer is
     applied directly to the process screen.  Emulsion-dependent source
     screens vary by tile and remain on the private construction path.  */
  bool
  focus_screen_cache_eligible_p () const
  {
    return (optimize_scanner_mtf_sigma || optimize_scanner_mtf_defocus
            || optimize_scanner_mtf_channel_defocus)
           && !optimize_emulsion_blur && !optimize_emulsion_intensities
           && !optimize_emulsion_offset;
  }

  /* Source spectra can be shared while the ideal periodic screen stays
     fixed.  Scalar physical defocus and compact fallback blur diameter both
     change only the capture transfer.  Strip-width fitting is intentionally
     left on the ordinary exact path because it changes the source screen
     itself.  */
  bool
  focus_source_cache_eligible_p () const
  {
    return optimize_scanner_mtf_defocus && !optimize_scanner_mtf_sigma
           && !optimize_scanner_mtf_channel_defocus && !optimize_strips
           && focus_screen_cache_eligible_p ()
           && (!tile_sharpened
               || render_sharpen_params.get_mode ()
                      != sharpen_parameters::richardson_lucy_deconvolution);
  }

  /* The discretized approximation is intentionally restricted to scalar
     physical defocus.  The empirical fallback objective is sufficiently
     multimodal that small interpolation changes can switch between blur/color
     compensation basins; its direct analytical exact path is already cheap.
     All other capture-transfer parameters and the source periodic screen must
     stay fixed during the fit.  */
  bool
  focus_screen_interpolation_eligible_p () const
  {
    return interpolate_scanner_mtf_defocus
           && optimize_scanner_mtf_defocus
           && !optimize_scanner_mtf_sigma
           && !optimize_scanner_mtf_channel_defocus && !optimize_strips
           && render_sharpen_params.scanner_mtf.simulate_diffraction_p ()
           && focus_source_cache_eligible_p ();
  }

  /* Obtain one exact filtered screen through the existing linked-list LRU
     cache and account for the lookup in the shared profile.  */
  std::shared_ptr<screen>
  get_profiled_cached_focus_screen (
      const std::array<sharpen_parameters, 3> &sp,
      coord_t red_strip_width, coord_t green_strip_width)
  {
    bool cache_hit = false;
    finetune_screen_build_info build_info;
    screen_filter_profile filter_profile;
    const auto cache_start
        = profile ? std::chrono::steady_clock::now ()
                  : std::chrono::steady_clock::time_point ();
    std::shared_ptr<screen> scr = get_cached_finetune_screen (
        type, red_strip_width, green_strip_width, tile_sharpened, sp,
        parallel, focus_source_cache_eligible_p (),
        profile ? &cache_hit : nullptr,
        profile ? &filter_profile : nullptr,
        profile ? &build_info : nullptr);
    if (profile)
      {
        const uint64_t elapsed
            = std::chrono::duration_cast<std::chrono::nanoseconds> (
                  std::chrono::steady_clock::now () - cache_start)
                  .count ();
        profile->screen_cache_nanoseconds.fetch_add (
            elapsed, std::memory_order_relaxed);
        if (cache_hit)
          profile->focus_screen_cache_hits.fetch_add (
              1, std::memory_order_relaxed);
        else
          {
            profile->focus_screen_cache_misses.fetch_add (
                1, std::memory_order_relaxed);
            profile->exact_screen_builds.fetch_add (
                1, std::memory_order_relaxed);
            profile->screen_filter_nanoseconds.fetch_add (
                elapsed, std::memory_order_relaxed);
            profile->add_filter_profile (filter_profile);
          }
        if (build_info.source_cache_lookup)
          (build_info.source_cache_hit ? profile->focus_source_cache_hits
                                       : profile->focus_source_cache_misses)
              .fetch_add (1, std::memory_order_relaxed);
      }
    return scr;
  }

  /* Obtain the immutable source spectrum used by an exact scalar blur/focus
     final evaluation and account for this direct source-cache lookup.  */
  std::shared_ptr<screen_filter_source>
  get_profiled_cached_focus_source (coord_t red_strip_width,
                                    coord_t green_strip_width,
                                    screen_filter_profile *filter_profile)
  {
    bool cache_hit = false;
    const auto cache_start
        = profile ? std::chrono::steady_clock::now ()
                  : std::chrono::steady_clock::time_point ();
    std::shared_ptr<screen_filter_source> source
        = get_cached_finetune_screen_source (
            type, red_strip_width, green_strip_width,
            profile ? &cache_hit : nullptr, filter_profile);
    if (profile)
      {
        const uint64_t elapsed
            = std::chrono::duration_cast<std::chrono::nanoseconds> (
                  std::chrono::steady_clock::now () - cache_start)
                  .count ();
        profile->screen_cache_nanoseconds.fetch_add (
            elapsed, std::memory_order_relaxed);
        (cache_hit ? profile->focus_source_cache_hits
                   : profile->focus_source_cache_misses)
            .fetch_add (1, std::memory_order_relaxed);
      }
    return source;
  }

  /* Obtain one exact scalar blur/focus node.  NODE_DEFOCUS is generated from
     an integer grid index, so all dense cells using the same range produce
     bit-identical cache keys.  For the empirical fallback it represents blur
     diameter rather than physical defocus.  This common-value override is
     used only by the scalar interpolation path; ordinary exact per-channel
     fits retain all three values from CAPTURE_SHARPEN_PARAMETERS.  */
  std::shared_ptr<screen>
  get_focus_screen_node (coord_t *v, int node_index, coord_t node_defocus,
                         coord_t red_strip_width,
                         coord_t green_strip_width)
  {
    assert (optimize_scanner_mtf_defocus
            && !optimize_scanner_mtf_channel_defocus);
    assert (node_index >= 0
            && node_index < scanner_mtf_defocus_interpolation_nodes
            && node_index < (int)focus_screen_nodes.size ());

    std::shared_ptr<screen> cached = focus_screen_nodes[node_index].lock ();
    if (cached)
      {
        if (profile)
          profile->focus_screen_local_node_hits.fetch_add (
              1, std::memory_order_relaxed);
        return cached;
      }
    if (profile)
      profile->focus_screen_local_node_misses.fetch_add (
          1, std::memory_order_relaxed);

    std::array<sharpen_parameters, 3> sp
        = capture_sharpen_parameters (v);
    for (int c = 0; c < 3; c++)
      if (sp[c].scanner_mtf.simulate_diffraction_p ())
        sp[c].scanner_mtf.defocus = node_defocus;
      else
        sp[c].scanner_mtf.blur_diameter = node_defocus;
    cached = get_profiled_cached_focus_screen (
        sp, red_strip_width, green_strip_width);
    if (cached)
      focus_screen_nodes[node_index] = cached;
    return cached;
  }

  /* Initialize TILEID by interpolating the two neighboring exact focus-grid
     screens.  Filtering is linear in the source periodic screen, so this is
     equivalent to linearly interpolating the signed transfer response rather
     than its nonnegative MTF magnitude.  */
  bool
  initialize_interpolated_focus_screen (coord_t *v, int tileid,
                                        coord_t red_strip_width,
                                        coord_t green_strip_width)
  {
    finetune_focus_grid_interval interval;
    if (!finetune_focus_grid_interval_for_value (
            get_scanner_mtf_defocus (v),
            scanner_mtf_defocus_interpolation_max,
            scanner_mtf_defocus_interpolation_nodes, &interval))
      return false;

    std::shared_ptr<screen> lower
        = get_focus_screen_node (v, interval.lower_index, interval.lower,
                                 red_strip_width,
                                 green_strip_width);
    if (!lower)
      return false;
    if (interval.lower_index == interval.upper_index)
      {
        tiles[tileid].scr = std::move (lower);
        if (profile)
          profile->focus_screen_exact_node_uses.fetch_add (
              1, std::memory_order_relaxed);
        return true;
      }

    std::shared_ptr<screen> upper
        = get_focus_screen_node (v, interval.upper_index, interval.upper,
                                 red_strip_width,
                                 green_strip_width);
    if (!upper)
      return false;

    finetune_profile_timer timer (
        profile, finetune_profile_timer_kind::screen_interpolation);
    screen *dst = writable_tile_screen (tileid);
    const luminosity_t upper_weight = interval.upper_weight;
    /* Optical filtering changes MULT only.  ADD is identical in both exact
       nodes, so copy it rather than introducing an unnecessary rounding step
       into presentation-only data.  */
    memcpy (dst->add, lower->add, sizeof (dst->add));
    finetune_interpolate_screen_mult (*dst, *lower, *upper, upper_weight);
    if (profile)
      profile->focus_screen_interpolations.fetch_add (
          1, std::memory_order_relaxed);
    return true;
  }

  /* Apply blur to SRC_SCR and compute DST_SCR.
     Values are in vector V.  TILEID is the tile ID.
     If WEIGHT_SCR is non-null, use it as a weight screen.  Return false when
     MTF/PSF construction fails; DST_SCR is then unspecified and must be
     ignored by the caller.  */
  bool
  apply_blur (coord_t *v, int tileid, screen *dst_scr, screen *src_scr,
              screen *weight_scr = nullptr,
              screen_filter_profile *filter_profile = nullptr)
  {
    rgbdata blur = get_channel_blur_radius (v);

    if (weight_scr)
      {
        finetune_apply_uniform_image_layer (
            *tiles[tileid].merged_scr, *src_scr, *weight_scr,
            get_emulsion_intensities (v, tileid),
            get_emulsion_offset (v, tileid));
        src_scr = tiles[tileid].merged_scr.get ();
      }

    if (scanner_mtf_filter_p ())
      {
        std::array<sharpen_parameters, 3> sp
            = capture_sharpen_parameters (v);
        sharpen_parameters *vs[3] = { &sp[0], &sp[1], &sp[2] };
        if (!dst_scr->initialize_with_sharpen_parameters (
                *src_scr, vs, tile_sharpened, parallel, filter_profile))
          return false;
      }
    else
      dst_scr->initialize_with_blur (*src_scr, blur * pixel_size);
    return true;
  }

  /* Initialize screen for tile TILEID using values in vector V.  Return false
     on MTF/PSF construction failure.  Store in UPDATED whether a successful
     call changed the tile screen.

     This is the hot path for focus fitting.  A solver retains its last exact
     filtered screen for phase-only objective evaluations.  In addition,
     MTF-focus fits whose source screen is independent of emulsion parameters
     share exact filtered nodes through a dedicated finetune cache.  The cache
     is deliberately separate from the renderer cache so transient simplex
     vertices do not evict normal rendering state.  */
  bool
  init_screen (coord_t *v, int tileid, bool *updated)
  {
    if (profile)
      profile->screen_init_calls.fetch_add (1, std::memory_order_relaxed);

    luminosity_t emulsion_blur = get_emulsion_blur_radius (v);
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t scanner_mtf_sigma = get_scanner_mtf_sigma (v);
    rgbdata scanner_mtf_defocus = get_scanner_mtf_channel_defocus (v);
    luminosity_t red_strip_width = get_red_strip_width (v);
    luminosity_t green_strip_width = get_green_strip_width (v);
    rgbdata intensities = get_emulsion_intensities (v, tileid);
    point_t emulsion_offset = get_emulsion_offset (v, tileid);
    bool global_updated = false;
    if (red_strip_width != last_width || green_strip_width != last_height)
      {
        original_scr->initialize (type, red_strip_width, green_strip_width);
        last_width = red_strip_width;
        last_height = green_strip_width;
        global_updated = true;
      }

    /* Fast path: if everything is fixed, use the ordinary renderer cache.
       The dedicated focus cache below is reserved for changing MTF nodes.  */
    if (!optimize_scanner_mtf_sigma && !optimize_scanner_mtf_defocus
        && !optimize_scanner_mtf_channel_defocus
        && !optimize_screen_blur && !optimize_screen_channel_blurs
        && !optimize_strips && !optimize_emulsion_blur
        && !optimize_emulsion_intensities && !optimize_emulsion_offset)
      {
        if (global_updated)
          screen_revision++;
        if (tiles[tileid].last_screen_revision != screen_revision)
          {
            sharpen_parameters sp = render_sharpen_params;
            sp.scanner_mtf_scale *= pixel_size;
            bool cache_hit = false;
            const auto cache_start
                = profile ? std::chrono::steady_clock::now ()
                          : std::chrono::steady_clock::time_point ();
            std::shared_ptr<screen> scr = render_to_scr::get_screen (
                type, false, tile_sharpened, sp, red_strip_width,
                green_strip_width, nullptr, nullptr, nullptr,
                profile ? &cache_hit : nullptr);
            if (profile)
              {
                const uint64_t elapsed
                    = std::chrono::duration_cast<std::chrono::nanoseconds> (
                          std::chrono::steady_clock::now () - cache_start)
                          .count ();
                profile->screen_cache_nanoseconds.fetch_add (
                    elapsed, std::memory_order_relaxed);
                (cache_hit ? profile->fixed_screen_cache_hits
                           : profile->fixed_screen_cache_misses)
                    .fetch_add (1, std::memory_order_relaxed);
                if (!cache_hit)
                  profile->exact_screen_builds.fetch_add (
                      1, std::memory_order_relaxed);
              }
            if (!scr)
              return false;
            tiles[tileid].scr = std::move (scr);
            tiles[tileid].last_screen_revision = screen_revision;
            *updated = true;
            return true;
          }
        if (profile)
          profile->screen_state_reuses.fetch_add (1,
                                                  std::memory_order_relaxed);
        *updated = false;
        return true;
      }

    if (optimize_emulsion_blur
        && (emulsion_blur != last_emulsion_blur || global_updated))
      {
        emulsion_scr->initialize_with_blur (*original_scr, emulsion_blur);
        last_emulsion_blur = emulsion_blur;
        global_updated = true;
      }

    if (blur != last_blur || scanner_mtf_sigma != last_scanner_mtf_sigma
        || scanner_mtf_defocus != last_scanner_mtf_defocus)
      {
        last_blur = blur;
        last_scanner_mtf_sigma = scanner_mtf_sigma;
        last_scanner_mtf_defocus = scanner_mtf_defocus;
        global_updated = true;
      }

    if (global_updated)
      screen_revision++;

    if (tiles[tileid].last_screen_revision != screen_revision
        || tiles[tileid].last_emulsion_intensities != intensities
        || tiles[tileid].last_emulsion_offset != emulsion_offset)
      {
        if (focus_screen_interpolation_eligible_p ()
            && !force_exact_scanner_mtf_defocus)
          {
            if (!initialize_interpolated_focus_screen (
                    v, tileid, red_strip_width, green_strip_width))
              return false;
          }
        else if (focus_screen_interpolation_eligible_p ()
                 && force_exact_scanner_mtf_defocus)
          {
            /* The exact final point is deliberately not inserted into the
               node cache: arbitrary simplex optima would evict the fixed
               reusable grid one value at a time.  */
            screen_filter_profile filter_profile;
            const auto filter_start
                = profile ? std::chrono::steady_clock::now ()
                          : std::chrono::steady_clock::time_point ();
            std::shared_ptr<screen_filter_source> source
                = get_profiled_cached_focus_source (
                    red_strip_width, green_strip_width,
                    profile ? &filter_profile : nullptr);
            std::array<sharpen_parameters, 3> sp
                = capture_sharpen_parameters (v);
            sharpen_parameters *channels[3]
                = { &sp[0], &sp[1], &sp[2] };
            const bool ok
                = source
                  && writable_tile_screen (tileid)
                         ->initialize_with_sharpen_parameters (
                             *source, channels, tile_sharpened, parallel,
                             profile ? &filter_profile : nullptr);
            if (profile)
              {
                const uint64_t elapsed
                    = std::chrono::duration_cast<std::chrono::nanoseconds> (
                          std::chrono::steady_clock::now () - filter_start)
                          .count ();
                profile->focus_screen_final_exact_builds.fetch_add (
                    1, std::memory_order_relaxed);
                profile->exact_screen_builds.fetch_add (
                    1, std::memory_order_relaxed);
                profile->screen_filter_nanoseconds.fetch_add (
                    elapsed, std::memory_order_relaxed);
                profile->add_filter_profile (filter_profile);
              }
            if (!ok)
              return false;
          }
        else if (focus_screen_cache_eligible_p ())
          {
            const std::array<sharpen_parameters, 3> sp
                = capture_sharpen_parameters (v);
            std::shared_ptr<screen> scr
                = get_profiled_cached_focus_screen (
                    sp, red_strip_width, green_strip_width);
            if (!scr)
              return false;
            tiles[tileid].scr = std::move (scr);
          }
        else
          {
            screen_filter_profile filter_profile;
            const auto filter_start
                = profile ? std::chrono::steady_clock::now ()
                          : std::chrono::steady_clock::time_point ();
            const bool ok = apply_blur (
                v, tileid, writable_tile_screen (tileid),
                optimize_emulsion_blur && !optimize_emulsion_intensities
                    ? emulsion_scr.get ()
                    : original_scr.get (),
                optimize_emulsion_intensities
                    ? (optimize_emulsion_blur ? emulsion_scr.get ()
                                               : original_scr.get ())
                    : nullptr,
                profile ? &filter_profile : nullptr);
            if (profile)
              {
                const uint64_t elapsed
                    = std::chrono::duration_cast<std::chrono::nanoseconds> (
                          std::chrono::steady_clock::now () - filter_start)
                          .count ();
                profile->exact_screen_builds.fetch_add (
                    1, std::memory_order_relaxed);
                profile->screen_filter_nanoseconds.fetch_add (
                    elapsed, std::memory_order_relaxed);
                profile->add_filter_profile (filter_profile);
              }
            if (!ok)
              return false;
          }
        tiles[tileid].last_screen_revision = screen_revision;
        tiles[tileid].last_emulsion_intensities = intensities;
        tiles[tileid].last_emulsion_offset = emulsion_offset;
        *updated = true;
        return true;
      }
    if (profile)
      profile->screen_state_reuses.fetch_add (1, std::memory_order_relaxed);
    *updated = false;
    return true;
  }

  /* Evaluate screen pixel for TILEID at X,Y with offset OFF.
     Fast version that does not assume scaling/rotation.  */
  pure_attr inline rgbdata
  evaluate_screen_pixel_fast (int tileid, int x, int y, point_t off) const
  {
    point_t p = tiles[tileid].pos[y * twidth + x] + off;
    /* When using scanner mtf, the screen is already blurred to
       estimate sensor mtf as well.  No need for antialiasing
       then.  */
    return tiles[tileid].scr->interpolated_mult (p);
  }

  /* Evaluate screen pixel for TILEID at X,Y with offset OFF.  */
  pure_attr inline rgbdata
  evaluate_screen_pixel_slow (coord_t *v, int tileid, int x, int y) const
  {
    point_t  p = get_pos (v, tileid, {x, y});
    /* When using scanner mtf, the screen is already blurred to
       estimate sensor mtf as well.  No need for antialiasing
       then.  */
    return tiles[tileid].scr->interpolated_mult (p);
  }

  /* Evaluate pixel at (X,Y) using RGB values RED, GREEN, BLUE and offsets OFF
     compensating coordinates stored in tile_pos.
     Values are in vector V.  TILEID is the tile ID.
     MIX_WEIGHTS and MIX_DARK are used for infrared simulation.  */
  pure_attr inline rgbdata
  evaluate_pixel (coord_t *v, int tileid, double_rgbdata red,
                  double_rgbdata green, double_rgbdata blue, int x, int y,
                  double_rgbdata mix_weights, double mix_dark)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
    rgbdata c = ((red * m.red + green * m.green + blue * m.blue)
                 * ((coord_t)1.0 / (coord_t)rgbscale));
    if (simulate_infrared)
      {
        rgbdata p = get_pixel (v, tileid, { x, y });
        luminosity_t intensity = p.red * mix_weights.red
                                 + p.green * mix_weights.green
                                 + p.blue * mix_weights.blue - mix_dark;
        c *= intensity;
      }
    return c;
  }

  /* Simulate screen for TILEID using values in vector V.
     Return true if simulation was updated.  */
  bool
  simulate_screen (coord_t *v, int tileid, bool force = false)
  {
    if (!optimize_coordinates)
      {
	point_t off = get_offset (v, tileid);
	if (!force && tiles[tileid].last_simulated_offset == off)
	  return false;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    tiles[tileid].simulated_screen[y * simulated_screen_width + x]
		= evaluate_screen_pixel_fast (tileid, x, y, off);
	tiles[tileid].last_simulated_offset = off;
      }
    else
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  tiles[tileid].simulated_screen[y * simulated_screen_width + x]
	      = evaluate_screen_pixel_slow (v, tileid, x, y);
    return true;
  }

  /* Evaluate pixel at (X,Y) using COLOR and offset OFF for TILEID.
     This is used for black and white mode.  */
  pure_attr inline luminosity_t
  bw_evaluate_pixel (int tileid, double_rgbdata color, int x, int y)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
    return ((m.red * color.red + m.green * color.green
             + m.blue * color.blue) /** (2 * maxgray)*/);
  }
  pure_attr rgbdata
  get_orig_pixel (coord_t *v, int tileid, int_point_t p)
  {
    if (!optimize_fog)
      return tiles[tileid].color[p.y * twidth + p.x];
    rgbdata d = tiles[tileid].color[p.y * twidth + p.x] - get_fog (v);
    if (normalize)
      {
        luminosity_t ssum = my_fabs (d.red + d.green + d.blue);
        if (ssum == 0)
          ssum = (luminosity_t)0.0000001;
        d /= ssum;
      }
    return d;
  }

  pure_attr rgbdata
  get_pixel_nofog (int tileid, int_point_t p)
  {
    return tiles[tileid].sharpened_color[p.y * twidth + p.x];
  }

  pure_attr rgbdata
  get_pixel (coord_t *v, int tileid, int_point_t p)
  {
    if (!optimize_fog)
      return tiles[tileid].sharpened_color[p.y * twidth + p.x];
    rgbdata d
        = tiles[tileid].sharpened_color[p.y * twidth + p.x] - get_fog (v);
    if (normalize)
      {
        luminosity_t ssum = my_fabs (d.red + d.green + d.blue);
        if (ssum == 0)
          ssum = (luminosity_t)0.0000001;
        d /= ssum;
      }
    return d;
  }

  luminosity_t
  bw_get_pixel (int tileid, int_point_t p)
  {
    return tiles[tileid].bw[p.y * twidth + p.x];
  }
  void
  determine_colors_using_data_collection (coord_t *v, double_rgbdata *ret_red,
                                          double_rgbdata *ret_green,
                                          double_rgbdata *ret_blue)
  {
    /* Use double_rgbdata to avoid accumulation of roundoff error.  */
    double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
    double_rgbdata color_red = { 0, 0, 0 }, color_green = { 0, 0, 0 },
                   color_blue = { 0, 0, 0 };
    luminosity_t threshold = collection_threshold;
    /* Use double to avoid accumulation of roundoff error.  */
    double wr = 0, wg = 0, wb = 0;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
              rgbdata d = get_pixel (v, tileid, { x, y });
              if (m.red > threshold)
                {
                  coord_t val = m.red - threshold;
                  wr += val;
                  red += m * val;
                  color_red += d * val;
                }
              if (m.green > threshold)
                {
                  coord_t val = m.green - threshold;
                  wg += val;
                  green += m * val;
                  color_green += d * val;
                }
              if (m.blue > threshold)
                {
                  coord_t val = m.blue - threshold;
                  wb += val;
                  blue += m * val;
                  color_blue += d * val;
                }
            }
    if (!wr || !wg || !wb)
      {
        *ret_red = *ret_green = *ret_blue = { -15, -15, -15 };
        return;
      }

    red /= wr;
    green /= wg;
    blue /= wb;
    color_red /= wr;
    color_green /= wg;
    color_blue /= wb;
    // sum /= n;
    // sum.print (stdout);
    double_rgbdata cred = (double_rgbdata){ red.red, green.red, blue.red };
    double_rgbdata cgreen
        = (double_rgbdata){ red.green, green.green, blue.green };
    double_rgbdata cblue = (double_rgbdata){ red.blue, green.blue, blue.blue };
    matrix4x4<double> sat (cred.red, cgreen.red, cblue.red, 0, cred.green,
                           cgreen.green, cblue.green, 0, cred.blue,
                           cgreen.blue, cblue.blue, 0, 0, 0, 0, 1);
    sat = sat.invert ();
    // sat.apply_to_rgb (color.red / (2 * maxgray), color.green / (2 *
    // maxgray), color.blue / (2 * maxgray), &color.red, &color.green,
    // &color.blue);
    sat.apply_to_rgb (color_red.red, color_green.red, color_blue.red,
                      &color_red.red, &color_green.red, &color_blue.red);
    sat.apply_to_rgb (color_red.green, color_green.green, color_blue.green,
                      &color_red.green, &color_green.green, &color_blue.green);
    sat.apply_to_rgb (color_red.blue, color_green.blue, color_blue.blue,
                      &color_red.blue, &color_green.blue, &color_blue.blue);
    /* Colors should be real reactions of scanner, so no negative values and
       also no excessively large values. Allow some overexposure.  */
    for (int c = 0; c < 3; c++)
      {
        color_red[c] = std::clamp (color_red[c], (double)-0.01, (double)2);
        color_green[c] = std::clamp (color_green[c], (double)-0.01, (double)2);
        color_blue[c] = std::clamp (color_blue[c], (double)-0.01, (double)2);
      }

    *ret_red = color_red;
    *ret_green = color_green;
    *ret_blue = color_blue;
  }

  coord_t
  determine_colors_using_least_squares (coord_t *v, double_rgbdata *red,
                                        double_rgbdata *green,
                                        double_rgbdata *blue)
  {
    /* Use double to not accumulate errors.  */
    double sqsum = 0;

    if (!least_squares_initialized)
      abort ();

    int e = 0;
    if (simulate_infrared)
      {
        rgbdata mix_weights = get_mix_weights (v);
        luminosity_t mix_dark = get_mix_dark (v);
        luminosity_t sum = /*v[mix_weights_index + 3]*/ 1;

        /* See below.  */
        assert (!fog_by_least_squares);
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
                  rgbdata d = fog_by_least_squares
                                  ? get_pixel_nofog (tileid, { x, y })
                                  : get_pixel (v, tileid, { x, y });
                  /* ??? This is not right when fog is optimized by least
                     squares. For this reason we use non-linear solver to
                     optimize fog.  */
                  c *= d.red * mix_weights.red + d.green * mix_weights.green
                       + d.blue * mix_weights.blue - mix_dark;
                  gsl_matrix_set (gsl_X.get (), e, 0, c.red);   /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1, c.green); /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2, c.blue);  /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3, 0);       /* red.green */
                  gsl_matrix_set (gsl_X.get (), e, 4, 0);       /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5, 0);       /* blue.green  */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 1);
                      gsl_matrix_set (gsl_X.get (), e, 7, 0);
                      gsl_matrix_set (gsl_X.get (), e, 8, 0);
                    }
                  gsl_vector_set (gsl_y[0].get (), e, d.red);
                  e++;
                  gsl_matrix_set (gsl_X.get (), e, 0, 0);       /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1, 0);       /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2, 0);       /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3, c.red);   /* red.green */
                  gsl_matrix_set (gsl_X.get (), e, 4, c.green); /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5, c.blue);  /* blue.green  */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 0);
                      gsl_matrix_set (gsl_X.get (), e, 7, 1);
                      gsl_matrix_set (gsl_X.get (), e, 8, 0);
                    }
                  gsl_vector_set (gsl_y[0].get (), e, d.green);
                  e++;

                  /* red.red * mix_weights.red + red.green * mix_weights.green
                     + red.blue * mix_weights.blue = sum gives: red.blue = (sum
                     - red.red * mix_weights.red - red.green *
                     mix_weights.green) / mix_weights.blue

                     Analogously:
                     green.blue = (sum - green.red * mix_weights.red -
                     green.green * mix_weights.green) / mix_weights.blue
                     blue.blue = (sum - blue.red * mix_weights.red - blue.green
                     * mix_weights.green) / mix_weights.blue  */

                  gsl_matrix_set (gsl_X.get (), e, 0,
                                  -c.red
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1,
                                  -c.green
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2,
                                  -c.blue
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3,
                                  -c.red
                                      * (mix_weights.green
                                         / mix_weights.blue)); /* red.green */
                  gsl_matrix_set (
                      gsl_X.get (), e, 4,
                      -c.green
                          * (mix_weights.green
                             / mix_weights.blue)); /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5,
                                  -c.blue
                                      * (mix_weights.green
                                         / mix_weights.blue)); /* blue.green */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 0);
                      gsl_matrix_set (gsl_X.get (), e, 7, 0);
                      gsl_matrix_set (gsl_X.get (), e, 8, 1);
                    }
                  gsl_vector_set (gsl_y[0].get (), e,
                                  d.blue
                                      - sum * (c.red + c.green + c.blue)
                                            / mix_weights.blue);
                  e++;
                }
        if (fog_by_least_squares)
          {
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6,
                            sample_points () * ((double)4 / 65546));
            gsl_matrix_set (gsl_X.get (), e, 7, 0);
            gsl_matrix_set (gsl_X.get (), e, 8, 0);
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6, 0);
            gsl_matrix_set (gsl_X.get (), e, 7,
                            sample_points () * ((double)4 / 65546));
            gsl_matrix_set (gsl_X.get (), e, 8, 0);
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6, 0);
            gsl_matrix_set (gsl_X.get (), e, 7, 0);
            gsl_matrix_set (gsl_X.get (), e, 8,
                            sample_points () * ((double)4 / 65546));
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
          }
        double chisq;
        if (gsl_multifit_linear (gsl_X.get (), gsl_y[0].get (), gsl_c.get (), gsl_cov.get (), &chisq,
                                 gsl_work.get ()) != GSL_SUCCESS)
          return 1e10;
        /* Colors should be real reactions of scanner, so no negative values
           and also no excessively large values. Allow some overexposure.  */
        (*red).red = gsl_vector_get (gsl_c.get (), 0);
        // to_range ((*red).red, -0.2, 2);
        (*green).red = gsl_vector_get (gsl_c.get (), 1);
        // to_range ((*green).red, -0.2, 2);
        (*blue).red = gsl_vector_get (gsl_c.get (), 2);
        // to_range ((*blue).red, -0.2, 2);
 
        (*red).green = gsl_vector_get (gsl_c.get (), 3);
        // to_range ((*red).green, -0.2, 2);
        (*green).green = gsl_vector_get (gsl_c.get (), 4);
        // to_range ((*green).green, -0.2, 2);
        (*blue).green = gsl_vector_get (gsl_c.get (), 5);
        // to_range ((*blue).green, -0.2, 2);

        (*red).blue = (sum - (*red).red * mix_weights.red
                       - (*red).green * mix_weights.green)
                      / mix_weights.blue;
        // to_range ((*red).blue, 0, 2);
        (*green).blue = (sum - (*green).red * mix_weights.red
                         - (*green).green * mix_weights.green)
                        / mix_weights.blue;
        // to_range ((*green).blue, 0, 2);
        (*blue).blue = (sum - (*blue).red * mix_weights.red
                        - (*blue).green * mix_weights.green)
                       / mix_weights.blue;
        // to_range ((*blue).blue, 0, 2);

        if (fog_by_least_squares)
          {
            last_fog.red = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 6),
                                       /*-fog_range.red*/ (luminosity_t)-0.1,
                                       fog_range.red);
            last_fog.green = std::clamp (
                (luminosity_t)gsl_vector_get (gsl_c.get (), 7),
                /*-fog_range.green*/ (luminosity_t)-0.1, fog_range.green);
            last_fog.blue = std::clamp (
                (luminosity_t)gsl_vector_get (gsl_c.get (), 8),
                /*-fog_range.blue*/ (luminosity_t)-0.1, fog_range.blue);
          }
        return chisq;
      }
    else
      {
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
                  gsl_matrix_set (gsl_X.get (), e, 0, c.red);
                  gsl_matrix_set (gsl_X.get (), e, 1, c.green);
                  gsl_matrix_set (gsl_X.get (), e, 2, c.blue);
                  if (fog_by_least_squares)
                    gsl_matrix_set (gsl_X.get (), e, 3, 1);
                  e++;
                }
        if (fog_by_least_squares)
          {
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3,
                            sample_points () * ((double)4 / 65546));
            e++;
          }
        if (e != (int)gsl_X->size1)
          abort ();
        for (int ch = 0; ch < 3; ch++)
          {
            double chisq;
            if (gsl_multifit_linear (gsl_X.get (), gsl_y[ch].get (), gsl_c.get (), gsl_cov.get (), &chisq,
                                     gsl_work.get ()) != GSL_SUCCESS)
              return 1e10;
            sqsum += chisq;
            /* Colors should be real reactions of scanner, so no negative
               values and also no excessively large values. Allow some
               overexposure.  */
            (*red)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 0),
                                     (luminosity_t)0, (luminosity_t)2);
            (*green)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 1),
                                       (luminosity_t)0, (luminosity_t)2);
            (*blue)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 2),
                                      (luminosity_t)0, (luminosity_t)2);
            if (fog_by_least_squares)
              {
                last_fog[ch]
                    = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 3),
                                  (luminosity_t)-0.1, fog_range[ch]);
              }
          }
        return sqsum;
      }
  }

  rgbdata
  bw_determine_color_using_data_collection (coord_t *v)
  {
    /* Use double_rgbdata to avoid accumulation of roundoff error.  */
    double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
    double_rgbdata color = { 0, 0, 0 };
    luminosity_t threshold = collection_threshold;
    /* Use double to avoid accumulation of roundoff error.  */
    double wr = 0, wg = 0, wb = 0;

    /* This follows same algorithm as data collection in analyze_base.
       We collect data only if screen has intensity greater than zero
       in given channel.  We also make statistics on how much saturation
       this process can lose and reverts that.  */
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
              luminosity_t l = bw_get_pixel (tileid, { x, y });
              if (m.red > threshold)
                {
                  coord_t val = m.red - threshold;
                  wr += val;
                  red += m * val;
                  color.red += l * val;
                }
              if (m.green > threshold)
                {
                  coord_t val = m.green - threshold;
                  wg += val;
                  green += m * val;
                  color.green += l * val;
                }
              if (m.blue > threshold)
                {
                  coord_t val = m.blue - threshold;
                  wb += val;
                  blue += m * val;
                  color.blue += l * val;
                }
            }
    if (!(wr > 0) || !(wg > 0) || !(wb > 0))
      return { -10, -10, -10 };

    red /= wr;
    green /= wg;
    blue /= wb;
    color.red /= wr;
    color.green /= wg;
    color.blue /= wb;

    double_rgbdata cred = (double_rgbdata){ red.red, green.red, blue.red };
    double_rgbdata cgreen
        = (double_rgbdata){ red.green, green.green, blue.green };
    double_rgbdata cblue = (double_rgbdata){ red.blue, green.blue, blue.blue };
    matrix4x4<double> sat (cred.red, cgreen.red, cblue.red, 0, cred.green,
                           cgreen.green, cblue.green, 0, cred.blue,
                           cgreen.blue, cblue.blue, 0, 0, 0, 0, 1);
    sat = sat.invert ();
    sat.apply_to_rgb (color.red, color.green, color.blue, &color.red,
                      &color.green, &color.blue);
    /* If infrared channel is simulated, negative values may be possible
       and it is kind of hard to constrain to reasonable bounds.
       Still allow values somewhat out of range to account for possible
       over-exposure or cropping  */
    if (!bw_is_simulated_infrared)
      {
        color.red = std::clamp (color.red, (double)-0.1, (double)1.1);
        color.green = std::clamp (color.green, (double)-0.1, (double)1.1);
        color.blue = std::clamp (color.blue, (double)-0.1, (double)1.1);
      }
    return color;
  }

  rgbdata
  bw_determine_color_using_least_squares (coord_t *v)
  {
    int e = 0;
    if (!least_squares_initialized)
      abort ();
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
              gsl_matrix_set (gsl_X.get (), e, 0, c.red);
              gsl_matrix_set (gsl_X.get (), e, 1, c.green);
              gsl_matrix_set (gsl_X.get (), e, 2, c.blue);
              e++;
              // gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 *
              // maxgray));
            }
    if (e != (int)gsl_X->size1)
      abort ();
    double chisq;
    if (gsl_multifit_linear (gsl_X.get (), gsl_y[0].get (), gsl_c.get (),
                             gsl_cov.get (), &chisq, gsl_work.get ()) != GSL_SUCCESS)
      return { -1, -1, -1 };
    rgbdata color
        = { (luminosity_t)gsl_vector_get (gsl_c.get (), 0) * (2 * maxgray),
            (luminosity_t)gsl_vector_get (gsl_c.get (), 1) * (2 * maxgray),
            (luminosity_t)gsl_vector_get (gsl_c.get (), 2) * (2 * maxgray) };
    /* If infrared channel is simulated, negative values may be possible
       and it is kind of hard to constrain to reasonable bounds.
       Still allow values somewhat out of range to account for possible
       over-exposure or cropping  */
    if (!bw_is_simulated_infrared)
      {
        color.red
            = std::clamp (color.red, (luminosity_t)-0.1, (luminosity_t)1.1);
        color.green
            = std::clamp (color.green, (luminosity_t)-0.1, (luminosity_t)1.1);
        color.blue
            = std::clamp (color.blue, (luminosity_t)-0.1, (luminosity_t)1.1);
      }
    return color;
  }

  rgbdata
  get_fog (coord_t *v)
  {
    if (!optimize_fog)
      return { 0, 0, 0 };
    if (fog_by_least_squares)
      return last_fog;
    assert (!colorscreen_checking || fog_index >= 0);
    return { (luminosity_t)v[fog_index] * fog_range.red,
             (luminosity_t)v[fog_index + 1] * fog_range.green,
             (luminosity_t)v[fog_index + 2] * fog_range.blue };
  }

  rgbdata
  get_emulsion_intensities (coord_t *v, int tileid)
  {
    if (optimize_emulsion_intensities)
      {
        /* Together with screen colors these are defined only up to scaling
         * factor.  */
        if (!tileid)
          {
            luminosity_t red = v[emulsion_intensity_index];
            luminosity_t green = v[emulsion_intensity_index + 1];
            luminosity_t blue = 1 - red - green;
            if (blue < 0)
              blue = 0;
            return { red, green, blue };
          }
        return { (luminosity_t)v[emulsion_intensity_index + 3 * tileid - 1],
                 (luminosity_t)v[emulsion_intensity_index + 3 * tileid - 0],
                 (luminosity_t)v[emulsion_intensity_index + 3 * tileid + 1] };
      }
    else
      return { 1, 1, 1 };
  }

  coord_t
  get_sharpen_radius (coord_t *v)
  {
    if (sharpen_index >= 0)
      return expand_range (v[sharpen_index], min_nonone_clen, 10);
    return 0;
  }
  coord_t
  get_sharpen_amount (coord_t *v)
  {
    if (sharpen_index >= 0)
      return v[sharpen_index + 1];
    return 0;
  }

  rgbdata
  get_mix_weights (coord_t *v)
  {
    if (mix_weights_index >= 0)
      {
        if (!least_squares)
          return { (luminosity_t)v[mix_weights_index],
                   (luminosity_t)v[mix_weights_index + 1],
                   1 - (luminosity_t)v[mix_weights_index]
                       - (luminosity_t)v[mix_weights_index + 1] };
        else
          return { (luminosity_t)v[mix_weights_index],
                   (luminosity_t)v[mix_weights_index + 1],
                   (luminosity_t)v[mix_weights_index + 2] };
      }
    double_rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    color_matrix process_colors (red.red, red.green, red.blue, 0, green.red,
                                 green.green, green.blue, 0, blue.red,
                                 blue.green, blue.blue, 0, 0, 0, 0, 1);
    rgbdata ret;
    process_colors.invert ().apply_to_rgb (1, 1, 1, &ret.red, &ret.green,
                                           &ret.blue);

#if 0
    if (simulate_infrared)
      return ret;
#endif
    luminosity_t sum = ret.red + ret.green + ret.blue;
    return ret * ((coord_t)1.0 / (coord_t)sum);
  }

  luminosity_t
  get_mix_dark (coord_t *v)
  {
    if (mix_dark_index >= 0)
      return v[mix_dark_index];
    return 0;
  }

  double_rgbdata
  bw_get_color (coord_t *v)
  {
    if (!least_squares && !data_collection)
      last_color = { v[color_index], v[color_index + 1], v[color_index + 2] };
    else if (data_collection)
      last_color = bw_determine_color_using_data_collection (v);
    else
      last_color = bw_determine_color_using_least_squares (v);
    return last_color;
  }
  void
  get_colors (coord_t *v, double_rgbdata *red, double_rgbdata *green,
              double_rgbdata *blue)
  {
    if (!least_squares && !data_collection)
      {
        *red = { v[color_index], v[color_index + 1], v[color_index + 2] };
        *green
            = { v[color_index + 3], v[color_index + 4], v[color_index + 5] };
        *blue = { v[color_index + 6], v[color_index + 7], v[color_index + 8] };
      }
    else if (data_collection)
      determine_colors_using_data_collection (v, red, green, blue);
    else
      {
        if (least_squares && (optimize_fog && !fog_by_least_squares))
          init_least_squares (v);
        determine_colors_using_least_squares (v, red, green, blue);
      }
    last_red = *red;
    last_green = *green;
    last_blue = *blue;
  }

  void
  update_transformation (coord_t *v)
  {
    if (optimize_coordinates == 1)
      {
	/* POS is row-major.  Keep the transformation centred on the actual
	   middle pixel also for rectangular tiles.  */
	point_t center
	    = tiles[0].pos[(theight / 2) * twidth + twidth / 2];
	/* First move center to 0.  */
	matrix3x3 trans = translation_3x3matrix (center * -1);
	/* Next apply scale  */
	trans = scale_3x3matrix (get_scale (v)) * trans;
	/* Next apply offset.  */
	trans = translation_3x3matrix (get_offset (v, 0)) * trans;
	/* Next apply rotation.  */
	trans = rotation_3x3matrix (get_rotation (v)) * trans;
	/* Now translate back.  */
	trans = translation_3x3matrix (center) * trans;
	transformation = trans;
      }
    else if (optimize_coordinates == 2)
      {
	/* Pixel coordinates are X=width and Y=height.  */
	point_t center = {(coord_t)(-twidth / 2), (coord_t)(-theight / 2)};
	/* First move center to 0.  */
	matrix3x3 trans = translation_3x3matrix (center);
	/* Next apply scale  */
	trans = scale_3x3matrix (get_scale (v)) * trans;
	/* Next apply offset.  */
	trans = translation_3x3matrix (get_offset (v, 0)) * trans;
	/* Next apply rotation.  */
	trans = rotation_3x3matrix (get_rotation (v)) * trans;
	transformation = trans;
	//printf ("offet %f %f, rotation %f, scale %f  %f tl %f %f; tr %f %f; bl %f %f; br%f %f centr %f %f\n", get_offset (v, 0).x, get_offset (v, 0).y, get_rotation (v), get_scale (v), (get_pos (v, 0, {0, 0}) - get_pos (v, 0, {0,1})).length (), get_pos (v,0,{0,0}).x,  get_pos (v,0,{0,0}).y, get_pos (v,0,{twidth,0}).x, get_pos (v,0,{twidth,0}).y, get_pos (v,0,{0,theight}).x, get_pos (v,0,{0,theight}).y,get_pos (v,0,{twidth,theight}).x, get_pos (v,0,{twidth,theight}).y,get_pos (v,0,{twidth/2,theight/2}).x, get_pos (v,0,{twidth/2,theight/2}).y);
      }
  }

  /* Objective function to minimize difference between simulated and actual
     scan.  V is vector of parameters.  */
  coord_t
  objfunc (coord_t *v)
  {
    if (profile)
      profile->objective_evaluations.fetch_add (1,
                                                std::memory_order_relaxed);
    finetune_profile_timer objective_timer (
        profile, finetune_profile_timer_kind::objective);

    /* Use double to avoid accumulation of round-off error.  */
    double sum = 0;
    const int nsamples = sample_points ();
    if (nsamples <= 0)
      return std::numeric_limits<coord_t>::max ();
    update_transformation (v);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        /* FIXME: parallelism is disabled because sometimes we are called from
         * parallel block.  */
        if (tiles[tileid].sharpened_color
            && tiles[tileid].sharpened_color != tiles[tileid].color.data ())
          sharpen<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
              tiles[tileid].sharpened_color, tiles[tileid].color.data (), theight,
              twidth, theight, get_sharpen_radius (v), get_sharpen_amount (v),
              nullptr, false);
        bool updated = false;
        if (!init_screen (v, tileid, &updated))
          return std::numeric_limits<coord_t>::max ();
        {
          finetune_profile_timer simulation_timer (
              profile, finetune_profile_timer_kind::screen_simulation);
          simulate_screen (v, tileid, updated);
        }
      }
    double_rgbdata red, green, blue;
    rgbdata color;
    rgbdata mix_weights;
    luminosity_t mix_dark = 0;
    {
      finetune_profile_timer color_timer (
          profile, finetune_profile_timer_kind::color_estimation);
      if (!tiles[0].color.empty ())
        get_colors (v, &red, &green, &blue);
      else
        color = bw_get_color (v);
      if (simulate_infrared)
        {
          mix_weights = get_mix_weights (v);
          mix_dark = get_mix_dark (v);
        }
    }
    finetune_profile_timer residual_timer (
        profile, finetune_profile_timer_kind::residual);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        if (!tiles[0].color.empty ())
          {
            for (int y = border; y < theight - border; y++)
              for (int x = border; x < twidth - border; x++)
                if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                  {
                    rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x,
                                                y, mix_weights, mix_dark);
                    rgbdata d = get_pixel (v, tileid, { x, y });

                    /* Bayer pattern.
                       TODO: This weighting is specific to Bayer patterns; for
                       general screen plates, a more uniform weighting should
                       be used. We will address this later.  */
                    if (!(x & 1) && !(y & 1))
                      sum += my_fabs (c.red - d.red) * 2;
                    else if ((x & 1) && (y & 1))
                      sum += my_fabs (c.blue - d.blue) * 2;
                    else
                      sum += my_fabs (c.green - d.green);
#if 0
		    sum += my_fabs (c.red - d.red) + my_fabs (c.green - d.green) + my_fabs (c.blue - d.blue);
#endif
                    /*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) *
                     * (c.green - d.green) + (c.blue - d.blue) * (c.blue -
                     * d.blue)*/
                  }
          }
        else if (!tiles[tileid].bw.empty ())
          {
            for (int y = border; y < theight - border; y++)
              for (int x = border; x < twidth - border; x++)
                if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                  {
                    luminosity_t c
                        = bw_evaluate_pixel (tileid, color, x, y);
                    luminosity_t d = bw_get_pixel (tileid, { x, y });
                    sum += my_fabs (c - d);
                  }
          }
      }
    /* MAXGRAY is shared by all BW tiles.  Normalizing inside the tile loop
       divided the accumulated errors from earlier tiles repeatedly.  */
    if (!tiles[0].bw.empty ())
      {
        if (!my_isfinite (maxgray) || !(maxgray > 0))
          return std::numeric_limits<coord_t>::max ();
        sum /= maxgray;
      }
    // printf ("%f\n", sum);
    /* Avoid solver from increasing blur past point it is no longer useful.
       Otherwise it will pick solutions with too large blur and very contrasty
       colors.  */
    //compute_contrast ();
    return (sum / nsamples)
           * ((coord_t)1
              + get_blur_radius (v)
                    * (coord_t)0.01) /*/ std::max (contrast, (luminosity_t)0.0000001)*/; /** (1 + get_emulsion_blur_radius (v) * 0.0001)*/;
  }

  void
  collect_screen (screen *s, coord_t *v, int tileid)
  {
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
        for (int c = 0; c < 3; c++)
          {
            s->mult[y][x][c] = (luminosity_t)0;
            s->add[y][x][c] = (luminosity_t)0;
          }
    for (int y = border; y < theight - border; y++)
      for (int x = border; x < twidth - border; x++)
        if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
          {
	    point_t p = get_pos (v, tileid, {x, y});
            int xx = ((int64_t)nearest_int (p.x * screen::size))
                     & (screen::size - 1);
            int yy = ((int64_t)nearest_int (p.y * screen::size))
                     & (screen::size - 1);
            if (!tiles[tileid].color.empty ())
              {
                s->mult[yy][xx][0]
                    = tiles[tileid].sharpened_color[y * twidth + x].red;
                s->mult[yy][xx][1]
                    = tiles[tileid].sharpened_color[y * twidth + x].green;
                s->mult[yy][xx][2]
                    = tiles[tileid].sharpened_color[y * twidth + x].blue;
              }
            else
              {
                s->mult[yy][xx][0] = tiles[tileid].bw[y * twidth + x];
                s->mult[yy][xx][1] = tiles[tileid].bw[y * twidth + x];
                s->mult[yy][xx][2] = tiles[tileid].bw[y * twidth + x];
              }
            s->add[yy][xx][0] = 1;
          }
    for (int i = 0; i < screen::size; i++)
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
          if (!s->add[y][x][0])
            {
              luminosity_t newv[3]
                  = { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
              int n = 0;
              for (int xo = -1; xo <= 1; xo++)
                for (int yo = -1; yo <= 1; yo++)
                  {
                    int nx = (x + xo) & (screen::size - 1);
                    int ny = (y + yo) & (screen::size - 1);
                    if (s->mult[ny][nx][0] + s->mult[ny][nx][1]
                        + s->mult[ny][nx][2])
                      {
                        newv[0] += s->mult[ny][nx][0];
                        newv[1] += s->mult[ny][nx][1];
                        newv[2] += s->mult[ny][nx][2];
                        n++;
                      }
                  }
              if (n)
                {
                  s->mult[y][x][0] = newv[0] / n;
                  s->mult[y][x][1] = newv[1] / n;
                  s->mult[y][x][2] = newv[2] / n;
                }
            }
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
        s->add[y][x][0] = 0;
  }

  int
  determine_outliers (coord_t *v, coord_t ratio)
  {
    double_rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    rgbdata mix_weights = { 0, 0, 0 };
    luminosity_t mix_dark = 0;

    if (simulate_infrared)
      {
        mix_weights = get_mix_weights (v);
        mix_dark = get_mix_dark (v);
      }
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        histogram hist;
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
              hist.pre_account (err);
            }
        hist.finalize_range (65535);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
              hist.account (err);
            }
        hist.finalize ();
        coord_t merr = hist.find_max (ratio) * (coord_t)1.3;
        tiles[tileid].outliers = std::make_shared<bitmap_2d> (twidth, theight);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
              if (err > merr)
                {
                  noutliers++;
                  tiles[tileid].outliers->set_bit (x, y);
                }
            }
      }
    if (!noutliers)
      return 0;
    if (least_squares)
      {
        free_least_squares ();
        alloc_least_squares ();
        if (!optimize_fog || fog_by_least_squares)
          init_least_squares (nullptr);
      }
    return noutliers;
  }

  int
  bw_determine_outliers (coord_t *v, coord_t ratio)
  {
    rgbdata color = bw_get_color (v);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        histogram hist;
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
              hist.pre_account (err);
            }
        hist.finalize_range (65535);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
              hist.account (err);
            }
        hist.finalize ();
        coord_t merr = hist.find_max (ratio) * (coord_t)1.3;
        tiles[tileid].outliers = std::make_shared<bitmap_2d> (twidth, theight);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
              if (err > merr)
                {
                  noutliers++;
                  tiles[tileid].outliers->set_bit (x, y);
                }
            }
      }
    if (!noutliers)
      return 0;
    if (least_squares)
      {
        free_least_squares ();
        alloc_least_squares ();
        if (!optimize_fog || fog_by_least_squares)
          init_least_squares (nullptr);
      }
    return noutliers;
  }
  std::unique_ptr<simple_image>
  produce_image (coord_t *v, int tileid, int type)
  {
    bool updated = false;
    if (!init_screen (v, tileid, &updated))
      return nullptr;

    std::unique_ptr<simple_image> img = std::make_unique<simple_image> ();
    if (!img || !img->allocate (twidth, theight))
      return nullptr;

    if (!tiles[0].color.empty ())
      {
        luminosity_t rmax = 0, gmax = 0, bmax = 0;
        double_rgbdata red, green, blue;
        rgbdata mix_weights = { 0, 0, 0 };
        luminosity_t mix_dark = 0;
        if (simulate_infrared)
          {
            mix_weights = get_mix_weights (v);
            mix_dark = get_mix_dark (v);
          }
        get_colors (v, &red, &green, &blue);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rmax = std::max (c.red, rmax);
              gmax = std::max (c.green, gmax);
              bmax = std::max (c.blue, bmax);
              rgbdata d = get_pixel (v, tileid, { x, y });
              rmax = std::max (d.red, rmax);
              gmax = std::max (d.green, gmax);
              bmax = std::max (d.blue, bmax);
            }
        rmax = diagnostic_normalization (rmax);
        gmax = diagnostic_normalization (gmax);
        bmax = diagnostic_normalization (bmax);

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      img->put_linear_pixel (
                          x, y,
                          { c.red / rmax, c.green / gmax, c.blue / bmax });
                    }
                    break;
                  case 1:
                    {
                      rgbdata d = get_orig_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax, d.green / gmax, d.blue / bmax });
                    }
                    break;
                  case 2:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rgbdata d = c - get_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax + (luminosity_t)0.5,
                            d.green / gmax + (luminosity_t)0.5,
                            d.blue / bmax + (luminosity_t)0.5 });
                    }
                    break;
                  case 3:
                    {
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax, d.green / gmax, d.blue / bmax });
                    }
                    break;
                  }
              else
                img->put_pixel (x, y, { 0, 0, 0 });
          }
      }
    if (!tiles[tileid].bw.empty ())
      {
        luminosity_t lmax = 0;
        rgbdata color = bw_get_color (v);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              lmax = std::max (bw_evaluate_pixel (tileid, color, x, y),
                               lmax);
              lmax = std::max (bw_get_pixel (tileid, { x, y }), lmax);
            }
        lmax = diagnostic_normalization (lmax);

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y)
                            / lmax;
                      img->put_linear_pixel (x, y, { c, c, c });
                    }
                    break;
                  case 1:
                    {
                      luminosity_t d = bw_get_pixel (tileid, { x, y }) / lmax;
                      img->put_linear_pixel (x, y, { d, d, d });
                    }
                    break;
                  case 2:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      luminosity_t d
                          = (c - bw_get_pixel (tileid, { x, y })) / lmax + (luminosity_t)0.5;
                      img->put_linear_pixel (x, y, { d, d, d });
                    }
                    break;
                  }
              else
                img->put_pixel (x, y, { 0, 0, 0 });
          }
      }
    return img;
  }

  bool
  write_file (coord_t *v, const char *name, int tileid, int type)
  {
    bool updated = false;
    if (!init_screen (v, tileid, &updated))
      return false;
    // void *buffer;
    // size_t len = create_linear_srgb_profile (&buffer);

    tiff_writer_params p;
    p.filename = name;
    p.width = twidth;
    // p.icc_profile = buffer;
    // p.icc_profile_len = len;
    p.height = theight;
    p.depth = 16;
    const char *error;
    tiff_writer rendered (p, &error);
    // free (buffer);
    if (error)
      return false;

    if (!tiles[0].color.empty ())
      {
        luminosity_t rmax = 0, gmax = 0, bmax = 0;
        double_rgbdata red, green, blue;
        rgbdata mix_weights = { 0, 0, 0 };
        luminosity_t mix_dark = 0;
        if (simulate_infrared)
          {
            mix_weights = get_mix_weights (v);
            mix_dark = get_mix_dark (v);
          }
        get_colors (v, &red, &green, &blue);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rmax = std::max (c.red, rmax);
              gmax = std::max (c.green, gmax);
              bmax = std::max (c.blue, bmax);
              rgbdata d = get_pixel (v, tileid, { x, y });
              rmax = std::max (d.red, rmax);
              gmax = std::max (d.green, gmax);
              bmax = std::max (d.blue, bmax);
            }
        rmax = diagnostic_normalization (rmax);
        gmax = diagnostic_normalization (gmax);
        bmax = diagnostic_normalization (bmax);

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rendered.put_pixel (
                          x, invert_gamma (c.red / rmax, -1) * 65535,
                          invert_gamma (c.green / gmax, -1) * 65535,
                          invert_gamma (c.blue / bmax, -1) * 65535);
                    }
                    break;
                  case 1:
                    {
                      rgbdata d = get_orig_pixel (v, tileid, { x, y });
                      rendered.put_pixel (
                          x, invert_gamma (d.red / rmax, -1) * 65535,
                          invert_gamma (d.green / gmax, -1) * 65535,
                          invert_gamma (d.blue / bmax, -1) * 65535);
                    }
                    break;
                  case 2:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      rendered.put_pixel (
                          x, (c.red - d.red) * (luminosity_t)65535 / rmax + (luminosity_t)32768,
                          (c.green - d.green) * (luminosity_t)65535 / gmax + (luminosity_t)32768,
                          (c.blue - d.blue) * (luminosity_t)65535 / bmax + (luminosity_t)32768);
                    }
                    break;
                  case 3:
                    {
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      rendered.put_pixel (x, d.red * (luminosity_t)65535 / rmax,
                                          d.green * (luminosity_t)65535 / gmax,
                                          d.blue * (luminosity_t)65535 / bmax);
                    }
                    break;
                  }
              else
                rendered.put_pixel (x, 0, 0, 0);
            if (!rendered.write_row ())
              return false;
          }
      }
    if (!tiles[tileid].bw.empty ())
      {
        luminosity_t lmax = 0;
        rgbdata color = bw_get_color (v);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              lmax = std::max (bw_evaluate_pixel (tileid, color, x, y),
                               lmax);
              lmax = std::max (bw_get_pixel (tileid, { x, y }), lmax);
            }
        lmax = diagnostic_normalization (lmax);

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      rendered.put_pixel (x, c * (luminosity_t)65535 / lmax,
                                          c * (luminosity_t)65535 / lmax, c * (luminosity_t)65535 / lmax);
                    }
                    break;
                  case 1:
                    {
                      luminosity_t d = bw_get_pixel (tileid, { x, y });
                      rendered.put_pixel (x, d * (luminosity_t)65535 / lmax,
                                          d * (luminosity_t)65535 / lmax, d * (luminosity_t)65535 / lmax);
                    }
                    break;
                  case 2:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      luminosity_t d = bw_get_pixel (tileid, { x, y });
                      rendered.put_pixel (x,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768);
                    }
                    break;
                  }
              else
                rendered.put_pixel (x, 65535, 0, 0);
            if (!rendered.write_row ())
              return false;
          }
      }
    return true;
  }
  void
  set_results (finetune_result &ret, const scr_to_img_parameters &param,
               const render_parameters &rparam, bool verbose,
               progress_info *progress)
  {
    ret.badness = objfunc (start.data ());
    if (!my_isfinite (ret.badness) || ret.badness < 0
        || ret.badness == std::numeric_limits<coord_t>::max ())
      {
        ret.err = "invalid final objective";
        return;
      }
    // if (optimize_screen_blur)
    // ret.screen_blur_radius = start[screen_index];
    /* TODO: Translate back to stitched project coordinates.  */
    ret.tile_pos = { (coord_t)(tiles[0].txmin + twidth / 2),
                     (coord_t)(tiles[0].tymin + theight / 2) };
    ret.red_strip_width = get_red_strip_width (start.data ());
    ret.green_strip_width = get_green_strip_width (start.data ());
    ret.scanner_mtf_sigma = get_scanner_mtf_sigma (start.data ());
    ret.scanner_mtf_defocus = rparam.sharpen.scanner_mtf.defocus;
    ret.scanner_mtf_blur_diameter
        = rparam.sharpen.scanner_mtf.blur_diameter;
    /* OBJFUNC above refreshed LAST_COLOR in BW mode and LAST_RED/GREEN/BLUE
       in RGB mode.  Use the common helper so RGB results do not accidentally
       report contrast from the unrelated BW cache.  */
    compute_contrast ();
    ret.contrast = contrast;

    if (optimize_coordinates)
      {
        point_t p1_img = {(coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin};
        point_t p1_scr = get_pos (start.data (), 0, {0, 0});
        point_t p2_scr = get_pos (start.data (), 0, {twidth - 1, 0});
        point_t p3_scr = get_pos (start.data (), 0, {0, theight - 1});

        coord_t dx_scr1_x = p2_scr.x - p1_scr.x;
        coord_t dx_scr1_y = p2_scr.y - p1_scr.y;
        coord_t dx_scr2_x = p3_scr.x - p1_scr.x;
        coord_t dx_scr2_y = p3_scr.y - p1_scr.y;
        coord_t det = dx_scr1_x * dx_scr2_y - dx_scr1_y * dx_scr2_x;

        if (det != 0)
          {
            ret.coordinate1.x = (twidth - 1) * dx_scr2_y / det;
            ret.coordinate2.x = -(twidth - 1) * dx_scr2_x / det;
            ret.coordinate1.y = -(theight - 1) * dx_scr1_y / det;
            ret.coordinate2.y = (theight - 1) * dx_scr1_x / det;
            ret.center.x = p1_img.x - ret.coordinate1.x * p1_scr.x - ret.coordinate2.x * p1_scr.y;
            ret.center.y = p1_img.y - ret.coordinate1.y * p1_scr.x - ret.coordinate2.y * p1_scr.y;
#ifdef COLORSCREEN_CHECKING
            {
              scr_to_img_parameters test_p;
              test_p.center = ret.center;
              test_p.coordinate1 = ret.coordinate1;
              test_p.coordinate2 = ret.coordinate2;
              scr_to_img test_map;
              if (!test_map.set_parameters (test_p, 1, 1))
                abort ();
              point_t p1_scr_test = test_map.to_scr (p1_img);
              point_t p2_scr_test = test_map.to_scr ({(coord_t)tiles[0].txmin + twidth - 1, (coord_t)tiles[0].tymin});
              point_t p3_scr_test = test_map.to_scr ({(coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin + theight - 1});
              if (p1_scr_test.dist_from (p1_scr) > 1e-4
                  || p2_scr_test.dist_from (p2_scr) > 1e-4
                  || p3_scr_test.dist_from (p3_scr) > 1e-4)
                {
                  printf ("VERIFICATION FAILED:\n");
                  printf ("  p1: %f %f -> %f %f (should be %f %f)\n", p1_img.x, p1_img.y, p1_scr_test.x, p1_scr_test.y, p1_scr.x, p1_scr.y);
                  printf ("  p2: %f %f -> %f %f (should be %f %f)\n", (coord_t)tiles[0].txmin + twidth - 1, (coord_t)tiles[0].tymin, p2_scr_test.x, p2_scr_test.y, p2_scr.x, p2_scr.y);
                  printf ("  p3: %f %f -> %f %f (should be %f %f)\n", (coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin + theight - 1, p3_scr_test.x, p3_scr_test.y, p3_scr.x, p3_scr.y);
		  printf ("Center %f %f to %f %f; Coordinates %f %f to %f %f; %f %f to %f %f\n", param.center.x, param.center.y, ret.center.x, ret.center.y, param.coordinate1.x, param.coordinate1.y, ret.coordinate1.x, ret.coordinate1.y, param.coordinate2.x, param.coordinate2.y, ret.coordinate2.x, ret.coordinate2.y);
		  abort ();
                }
            }
#endif
          }
        else
          {
            ret.center = param.center;
            ret.coordinate1 = param.coordinate1;
            ret.coordinate2 = param.coordinate2;
          }
      }
    else
      {
        ret.center = param.center;
        ret.coordinate1 = param.coordinate1;
        ret.coordinate2 = param.coordinate2;
      }

    if (optimize_scanner_mtf_defocus || optimize_scanner_mtf_channel_defocus)
      {
        const luminosity_t focus = get_scanner_mtf_defocus (start.data ());
        /* Only update the field interpreted by the active model.  Callers
           commonly copy both fields back to render_parameters, so mirroring
           the fit into the inactive field corrupts a later model switch.  */
        if (render_sharpen_params.scanner_mtf.simulate_diffraction_p ())
          ret.scanner_mtf_defocus = focus;
        else
          ret.scanner_mtf_blur_diameter = focus;
      }
    if (optimize_scanner_mtf_channel_defocus)
      ret.scanner_mtf_channel_defocus_or_blur
          = get_scanner_mtf_channel_defocus (start.data ());
    ret.screen_blur_radius = get_blur_radius (start.data ());
    ret.screen_channel_blur_radius = get_channel_blur_radius (start.data ());
    if (!tiles[0].color.empty ())
      {
        double_rgbdata screen_red, screen_green, screen_blue;
        get_colors (start.data (), &screen_red, &screen_green, &screen_blue);
        ret.screen_red = screen_red;
        ret.screen_green = screen_green;
        ret.screen_blue = screen_blue;
      }
    if (optimize_uniform_image_layer)
      {
        ret.tile_primary_intensities.resize (n_tiles);
        for (int tileid = 0; tileid < n_tiles; tileid++)
          ret.tile_primary_intensities[tileid]
              = get_emulsion_intensities (start.data (), tileid);
      }
    ret.emulsion_blur_radius = get_emulsion_blur_radius (start.data ());
    ret.screen_coord_adjust = get_offset (start.data (), 0);
    ret.emulsion_coord_adjust = get_emulsion_offset (start.data (), 0);
    ret.fog = get_fog (start.data ());
    /* The uniform-image-layer mode fits only per-tile scalar transmission.
       It does not recalibrate RGB-to-neutral mixing weights.  The historical
       coupled emulsion experiment may still derive those weights from its
       fitted primary response.  */
    if ((optimize_emulsion_intensities && !optimize_uniform_image_layer)
        || simulate_infrared)
      ret.mix_weights = get_mix_weights (start.data ());
    else
      {
        ret.mix_weights.red = rparam.mix_red;
        ret.mix_weights.green = rparam.mix_green;
        ret.mix_weights.blue = rparam.mix_blue;
      }

    /* MIX_DARK is fitted only by the experimental simulated-infrared mode.
       The historical coupled emulsion-intensity fit may derive new neutral
       mixing weights, but it has no dark variable; preserve the caller's
       current RGB dark rather than manufacturing zero from GET_MIX_DARK's
       inactive default.  Uniform-image-layer focus fitting preserves both
       the input mix weights and mix dark.  */
    if (simulate_infrared)
      ret.mix_dark = finetune_render_mix_dark (
          ret.mix_weights, get_mix_dark (start.data ()), rparam.mix_dark);
    else
      ret.mix_dark = rparam.mix_dark;

    if (optimize_position && !optimize_coordinate1)
      {
        int tileid = 0;
        /* Construct solver point.  Try to get closest point to the center of
         * analyzed tile.  */
        int fsx = nearest_int (
            get_pos (start.data (), tileid, { twidth / 2, theight / 2 }).x);
        int fsy = nearest_int (
            get_pos (start.data (), tileid, { twidth / 2, theight / 2 }).y);
        int bx = -1, by = -1;
        coord_t bdist = 0;
        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              {
                point_t p = get_pos (start.data (), tileid, { x, y });
                // printf ("  %-5.2f,%-5.2f", p.x, p.y);
                coord_t dist = my_fabs (p.x - fsx) + my_fabs (p.y - fsy);
                if (bx < 0 || dist < bdist)
                  {
                    bx = x;
                    by = y;
                    bdist = dist;
                  }
              }
            // printf ("\n");
          }
        if (!bx || bx == twidth - 1 || !by || by == theight - 1)
          {
            if (verbose)
              {
                if (progress)
                  progress->pause_stdout ();
                printf ("Solver point is out of tile\n");
                if (progress)
                  progress->resume_stdout ();
              }
            ret.err = "Solver point is out of tile";
            return;
          }
        point_t fp = { -1000, -1000 };

        bool found = false;
        // printf ("%i %i %i %i %f %f\n", bx, by, fsx, fsy,
        // tile_pos[twidth/2+(theight/2)*twidth].x,
        // tile_pos[twidth/2+(theight/2)*twidth].y);
        for (int y = by - 1; y <= by + 1; y++)
          for (int x = bx - 1; x <= bx + 1; x++)
            {
              /* Determine cell corners.  */
              point_t p = { (coord_t)fsx, (coord_t)fsy };
              point_t p1 = get_pos (start.data (), tileid, { x, y });
              point_t p2 = get_pos (start.data (), tileid, { x + 1, y });
              point_t p3 = get_pos (start.data (), tileid, { x, y + 1 });
              point_t p4 = get_pos (start.data (), tileid, { x + 1, y + 1 });
              /* Check if point is above or below diagonal.  */
              coord_t sgn1 = sign (p, p1, p4);
              if (sgn1 > 0)
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p3) < 0 || sign (p, p3, p1) < 0)
                    continue;
                  coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p3.x,
                                     p3.y, p4.x - p3.x, p4.y - p3.y, &rx, &ry);
                  rx = (coord_t)1 / rx;
                  found = true;
                  fp = { (ry * rx + x), (rx + y) };
                }
              else
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p2) > 0 || sign (p, p2, p1) > 0)
                    continue;
                  coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p2.x,
                                     p2.y, p4.x - p2.x, p4.y - p2.y, &rx, &ry);
                  rx = (coord_t)1 / rx;
                  found = true;
                  fp = { (rx + x), (ry * rx + y) };
                }
            }
        /* TODO: If we did not find the tile we could try some non-integer
           location.  */
        if (!found)
          {
            if (verbose)
              {
                if (progress)
                  progress->pause_stdout ();
                printf ("Failed to find solver point\n");
                if (progress)
                  progress->resume_stdout ();
              }
            ret.err = "Failed to find solver point";
            return;
          }
        ret.solver_point_img_location = { fp.x + tiles[tileid].txmin + (coord_t)0.5,
                                          fp.y + tiles[tileid].tymin + (coord_t)0.5 };
        // printf ("New location %f %f %f %f  %f %f\n", fp.x +
        // tiles[tileid].txmin
        // + 0.5, fp.y + tiles[tileid].tymin + 0.5, bx + tiles[tileid].txmin +
        // 0.5, by + tiles[tileid].tymin + 0.05, get_pos (start, tileid, {bx,
        // by}).x, get_pos (start, tileid, {bx, by}).y);
        ret.solver_point_screen_location.x = (coord_t)fsx;
        ret.solver_point_screen_location.y = (coord_t)fsy;
        ret.solver_point_color = solver_parameters::green;
      }
    ret.success = true;
  }
};
} // namespace

std::shared_ptr<screen>
finetune_get_cached_screen_for_test (
    scr_type type, coord_t red_strip_width, coord_t green_strip_width,
    bool anticipate_sharpening,
    const std::array<sharpen_parameters, 3> &sharpen, bool parallel,
    bool *cache_hit, screen_filter_profile *filter_profile)
{
  return get_cached_finetune_screen (
      type, red_strip_width, green_strip_width, anticipate_sharpening,
      sharpen, parallel, true, cache_hit, filter_profile);
}

void
finetune_prune_screen_cache_for_test ()
{
  finetune_screen_cache.prune ();
  finetune_screen_source_cache.prune ();
}
 
/* Produce element I of a geometric sequence from MIN to MAX divided into
   STEPS intervals.  */
static coord_t
geom_sequence (coord_t min, coord_t max, int steps, int i)
{
  double r = std::pow(max / min, 1.0 / steps);
  return min * std::pow (r, i);
}

/* Return true when AREA parameters are safe for grid construction and robust
   fit-score filtering.  UNCERTAINTY_RATIO is the fraction of the most reliable
   successful fits retained, not the fraction discarded.  */
static bool
valid_finetune_area_parameters_p (const finetune_area_parameters &parameters)
{
  return parameters.grid_width >= 0 && parameters.grid_height >= 0
         && my_isfinite (parameters.min_contrast)
         && parameters.min_contrast >= 0
         && my_isfinite (parameters.uncertainty_ratio)
         && parameters.uncertainty_ratio >= 0
         && parameters.uncertainty_ratio <= 1
         && my_isfinite (parameters.max_displacement)
         && parameters.max_displacement >= 0;
}

/* Return true when WIDTH*HEIGHT is a usable int-indexed grid and store its
   allocation size in COUNT.  The area helpers use int coordinates and
   progress totals even though std::vector itself accepts a larger size_t.  */
static bool
valid_finetune_grid_size_p (int width, int height, size_t *count)
{
  if (!count || width <= 0 || height <= 0)
    return false;
  const int64_t n = (int64_t)width * height;
  if (n <= 0 || n > INT_MAX)
    return false;
  *count = (size_t)n;
  return true;
}

/* Find the largest fit-quality score accepted when RETAIN_RATIO of the most
   reliable successful RESULTS is kept.  Failed and non-finite results do not
   participate in the quantile.  */
bool
finetune_retained_fit_score_cutoff (
    const std::vector<finetune_result> &results, coord_t retain_ratio,
    coord_t *cutoff)
{
  if (!cutoff || !my_isfinite (retain_ratio) || retain_ratio < 0
      || retain_ratio > 1)
    return false;

  std::vector<coord_t> scores;
  scores.reserve (results.size ());
  for (const finetune_result &result : results)
    if (result.success && valid_fit_score_p (result.uncertainty))
      scores.push_back (result.uncertainty);
  if (scores.empty ())
    return false;

  std::sort (scores.begin (), scores.end (), std::greater<coord_t> ());
  const size_t index
      = (size_t)((scores.size () - 1) * ((coord_t)1 - retain_ratio));
  *cutoff = scores[index];
  return true;
}

/* Fit the model selected by FPARAMS to tiles from IMG.  RPARAM supplies the
   fixed rendering state and starting values; it is not modified.  PARAM is
   the screen-to-image geometry, LOCS are tile locations, RESULTS optionally
   supply previous local offsets, and PROGRESS reports work/cancellation.  */

finetune_result
finetune (const render_parameters &rparam, const scr_to_img_parameters &param,
          const image_data &img, const std::vector<point_t> &locs,
          const std::vector<finetune_result> *results,
          const finetune_parameters &fparams, progress_info *progress)
{
  finetune_result ret;
  finetune_profile_accumulator profile_storage;
  finetune_profile_accumulator *profile
      = fparams.collect_profile ? &profile_storage : nullptr;
  auto finish = [&]() -> finetune_result {
    if (profile)
      ret.profile = profile->snapshot ();
    return std::move (ret);
  };
  bool tile_sharpened = false;

  if (const char *flag_error = finetune_flag_error (fparams.flags))
    {
      ret.err = flag_error;
      return finish ();
    }
  if (!my_isfinite (fparams.ignore_outliers)
      || fparams.ignore_outliers < 0 || fparams.ignore_outliers >= 1)
    {
      ret.err = "ignore_outliers must be finite and in [0,1)";
      return finish ();
    }
  if (fparams.range < 0)
    {
      ret.err = "negative finetune range";
      return finish ();
    }
  if (fparams.interpolate_scanner_mtf_defocus)
    {
      const uint64_t incompatible
          = finetune_scanner_mtf_sigma
            | finetune_scanner_mtf_channel_defocus | finetune_screen_blur
            | finetune_screen_channel_blurs | finetune_strips
            | finetune_emulsion_blur;
      if (!(fparams.flags & finetune_scanner_mtf_defocus)
          || (fparams.flags & incompatible))
        {
          ret.err = "focus interpolation requires scalar scanner MTF "
                    "defocus as the sole varying screen-filter parameter";
          return finish ();
        }
      if (!rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
        {
          ret.err = "focus interpolation requires an analytical physical "
                    "defocus model";
          return finish ();
        }
      if (!my_isfinite (fparams.scanner_mtf_defocus_interpolation_max)
          || fparams.scanner_mtf_defocus_interpolation_max <= 0
          || fparams.scanner_mtf_defocus_interpolation_max > 20)
        {
          ret.err = "invalid focus interpolation range";
          return finish ();
        }
      if (fparams.scanner_mtf_defocus_interpolation_nodes < 2
          || fparams.scanner_mtf_defocus_interpolation_nodes > 64)
        {
          ret.err = "focus interpolation node count must be in [2,64]";
          return finish ();
        }
    }

  int n_tiles;
  if (fparams.flags & (finetune_coordinates | finetune_guess_coordinates))
    {
      if (!locs.empty ())
        {
          ret.err = "did not expect tiles";
          return finish ();
        }
      n_tiles = 1;
    }
  else
    {
      if (locs.size () > (size_t)finetune_solver::max_tiles)
        {
          ret.err = "too many tile locations";
          return finish ();
        }
      n_tiles = (int)locs.size ();
      if (!n_tiles)
        {
          ret.err = "no tile locations";
          return finish ();
        }
    }
  if (results && results->size () < (size_t)n_tiles)
    {
      ret.err = "too few previous finetune results";
      return finish ();
    }
  const image_data *imgp[finetune_solver::max_tiles];
  scr_to_img *mapp[finetune_solver::max_tiles];
  int x[finetune_solver::max_tiles];
  int y[finetune_solver::max_tiles];
  coord_t pixel_size = -1;

  scr_to_img map;
  imgp[0] = nullptr;
  mapp[0] = nullptr;
  x[0] = 0;
  y[0] = 0;
  for (int tileid = 0; tileid < n_tiles; tileid++)
    {
      if (fparams.flags & (finetune_coordinates | finetune_guess_coordinates))
        {
          x[tileid] = param.center.x;
          y[tileid] = param.center.y;
        }
      else
        {
          x[tileid] = locs[tileid].x;
          y[tileid] = locs[tileid].y;
        }
      imgp[tileid] = &img;
      if (img.stitch)
        {
          int tx, ty;
          point_t scr = img.stitch->common_scr_to_img.final_to_scr (
              { (coord_t)(x[tileid] + img.xmin),
                (coord_t)(y[tileid] + img.ymin) });
          pixel_size = img.stitch->pixel_size;
          if (!img.stitch->tile_for_scr (&rparam, scr.x, scr.y, &tx, &ty,
                                         true))
            {
              ret.err = "no tile for given coordinates";
              return finish ();
            }
          point_t p = img.stitch->images[ty][tx].common_scr_to_img (scr);
          x[tileid] = nearest_int (p.x);
          y[tileid] = nearest_int (p.y);
          imgp[tileid] = img.stitch->images[ty][tx].img.get ();
          mapp[tileid] = &img.stitch->images[ty][tx].scr_to_img_map;
        }
      else
        {
          if (!tileid)
            {
              if (!map.set_parameters (param, *imgp[tileid]))
                {
                  ret.err = "failed to convert screen to image coordinates";
                  return finish ();
                }
	      /* TODO: determine correct pixel size area.  */
              pixel_size
                  = map.pixel_size ({0, 0, imgp[tileid]->width, imgp[tileid]->height});
            }
          mapp[tileid] = &map;
        }
    }
  if (!my_isfinite (pixel_size) || pixel_size <= 0)
    {
      ret.err = "invalid local pixel size";
      return finish ();
    }
  bool bw = fparams.flags & finetune_bw;
  bool verbose = fparams.flags & finetune_verbose;

  if (!bw && !imgp[0]->has_rgb ())
    bw = true;
  if (fparams.flags & finetune_uniform_image_layer)
    {
      if (n_tiles < 2)
        {
          ret.err = "uniform image-layer fitting requires at least two tiles";
          return finish ();
        }
      if (bw || !imgp[0]->has_rgb ())
        {
          ret.err = "uniform image-layer fitting requires RGB input";
          return finish ();
        }
    }

  int iterations = 0;
  int txmin, txmax, tymin, tymax;
  if (!(fparams.flags & finetune_guess_coordinates))
    {
      /* Determine tile to analyze.  */
      point_t tp = mapp[0]->to_scr ({ (coord_t)x[0], (coord_t)y[0] });
      int sx = nearest_int (tp.x);
      int sy = nearest_int (tp.y);

      coord_t def_xrange = 2;
      /* When not normalizing we want to avoid image in the tile.  */
      if ((fparams.flags & finetune_no_normalize) || bw)
        def_xrange = 1;
      /* To determine rotation we need quite large context especially for Dufaycolor.  */
      if (fparams.flags & finetune_coordinates)
        def_xrange = param.type == Dufay ? 8 : 5;

      coord_t test_xrange = fparams.range ? fparams.range : def_xrange;
      coord_t test_yrange = test_xrange;
      /* If screen tile is far from rectangular, compensate.
         Also screen with strips has too few elements, so
         finetuning is not very stressed to pick reasonable solution.  */
      if (screen_with_vertical_strips_p (param.type))
        test_yrange *= 3;
      do
        {
          if (iterations)
            test_xrange++, test_yrange++;
          point_t p = mapp[0]->to_img ({ (coord_t)sx, (coord_t)sy });
          coord_t sxmin = p.x, sxmax = p.x, symin = p.y, symax = p.y;
          p = mapp[0]->to_img ({ sx - test_xrange, sy - test_yrange });
          sxmin = std::min (sxmin, p.x);
          sxmax = std::max (sxmax, p.x);
          symin = std::min (symin, p.y);
          symax = std::max (symax, p.y);
          p = mapp[0]->to_img ({ sx + test_xrange, sy - test_yrange });
          sxmin = std::min (sxmin, p.x);
          sxmax = std::max (sxmax, p.x);
          symin = std::min (symin, p.y);
          symax = std::max (symax, p.y);
          p = mapp[0]->to_img ({ sx + test_xrange, sy + test_yrange });
          sxmin = std::min (sxmin, p.x);
          sxmax = std::max (sxmax, p.x);
          symin = std::min (symin, p.y);
          symax = std::max (symax, p.y);
          p = mapp[0]->to_img ({ sx - test_xrange, sy + test_yrange });
          sxmin = std::min (sxmin, p.x);
          sxmax = std::max (sxmax, p.x);
          symin = std::min (symin, p.y);
          symax = std::max (symax, p.y);

          txmin = my_floor (sxmin);
          tymin = my_floor (symin);
          txmax = my_ceil (sxmax);
          tymax = my_ceil (symax);
          if (txmin < 0)
            txmin = 0;
          if (txmax > imgp[0]->width)
            txmax = imgp[0]->width;
          if (tymin < 0)
            tymin = 0;
          if (tymax > imgp[0]->height)
            tymax = imgp[0]->height;
          iterations++;
        }
      while (iterations < 10 && (txmin + 10 > txmax || tymin + 10 > tymax));
    }
  else
    {
      const int sz = 100;
      txmin = param.center.x - sz / 2;
      txmax = param.center.x + sz / 2;
      tymin = param.center.y - sz / 2;
      tymax = param.center.y + sz / 2;
      if (txmin < 0)
        txmin = 0;
      if (txmax > imgp[0]->width)
        txmax = imgp[0]->width;
      if (tymin < 0)
        tymin = 0;
      if (tymax > imgp[0]->height)
        tymax = imgp[0]->height;
    }

  if (txmin + 10 > txmax || tymin + 10 > tymax)
    {
      if (verbose)
        {
          if (progress)
            progress->pause_stdout ();
          fprintf (stderr, "Too small tile %i-%i %i-%i\n", txmin, txmax, tymin,
                   tymax);
          if (progress)
            progress->resume_stdout ();
        }
      ret.err = "too small tile";
      return finish ();
    }
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if ((fparams.flags & finetune_sharpening)
      && (twidth <= 20 || theight <= 20))
    {
      ret.err = "tile too small for sharpening border";
      return finish ();
    }
  if (verbose)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Tile size %ix%i; %i tiles\n", twidth, theight,
               n_tiles);
      if (progress)
        progress->resume_stdout ();
    }
  finetune_solver best_solver;
  best_solver.set_profile (profile);
  coord_t best_fit_score = -1;
  std::atomic<bool> failed (false);

  render_parameters rparam2 = rparam;
  /* Working with sharpened tile is easier, since it is likely already
     used by renderers.  But if we are finetuning sharpening, we must
     use unsharpened data, so the parameters can be adjusted.
     TODO: It is not clear if sharpening does decrease quality of the
     simulation.  */
  if (bw)
    {
      if (fparams.flags
          & (finetune_screen_blur | finetune_screen_channel_blurs
             | finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus
             | finetune_scanner_mtf_channel_defocus))
        rparam2.sharpen.mode = sharpen_parameters::none;
      else
        tile_sharpened = true;
    }

  /* TODO: shall we reset contact copy?  */
  // rparam2.invert = 0;
  int maxtiles = fparams.multitile;
  if (maxtiles < 1)
    maxtiles = 1;
  if (!(maxtiles & 1))
    {
      if (maxtiles == INT_MAX)
        {
          ret.err = "multitile search is too large";
          return finish ();
        }
      maxtiles++;
    }
  if ((int64_t)maxtiles * maxtiles > INT_MAX)
    {
      ret.err = "multitile search is too large";
      return finish ();
    }

  bool bw_is_simulated_infrared = false;

  /* Multitile support only for 1 tile.  */
  if (n_tiles == 1
      /* Avoid openmp when we do not need it.  This seems also necessary to get
         Windows builds working.  */
      && (maxtiles > 1 && !(fparams.flags & finetune_no_progress_report)))
    {
      ///* FIXME: Hack; render is too large for stack in openmp thread.  */
      // std::unique_ptr<render_to_scr> rp(new render_to_scr (param, img,
      // rparam, 256));
      render render (*imgp[0], rparam2, 256);
#if 0
      if (maxtiles > 1)
	{
	  rxmin = std::max (txmin - twidth * (maxtiles / 2), 0);
	  rymin = std::max (tymin - theight * (maxtiles / 2), 0);
	  rxmax = std::max (txmax + (twidth * maxtiles / 2), imgp[0]->width - 1);
	  rymax = std::max (tymax + (theight * maxtiles / 2), imgp[0]->height - 1);
	}
      //if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, rxmin, rymin, rxmax + 1, rymax + 1, !(fparams.flags & finetune_no_progress_report) ? progress : nullptr))
#endif
      if (bw && (rparam2.ignore_infrared || !imgp[0]->has_grayscale_or_ir ()))
        bw_is_simulated_infrared = true;
      if (!render.precompute_all (
              bw ? PRECOMPUTE_IMAGE_LAYER : PRECOMPUTE_RGB_IMAGE,
              patch_proportions (param.type, &rparam2),
              !(fparams.flags & finetune_no_progress_report) ? progress
                                                             : nullptr))
        {
          if (verbose)
            {
              if (progress)
                progress->pause_stdout ();
              fprintf (stderr, "Precomputing failed. Tile: %i-%i %i-%i\n",
                       txmin, txmax, tymin, tymax);
              if (progress)
                progress->resume_stdout ();
            }
          ret.err = "precomputing failed";
          return finish ();
        }
      if (progress && progress->cancel_requested ())
        {
          ret.err = "cancelled";
          return finish ();
        }

      if (maxtiles * maxtiles > 1
          && !(fparams.flags & finetune_no_progress_report) && progress)
        progress->set_task ("finetuning samples", maxtiles * maxtiles);

      gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(fparams, maxtiles, rparam, pixel_size, best_fit_score, verbose,  \
               std::nothrow, imgp, twidth, theight, txmin, tymin, bw,         \
               progress, mapp, render, failed, best_solver, results,          \
               bw_is_simulated_infrared, tile_sharpened, profile)
      for (int ty = 0; ty < maxtiles; ty++)
        for (int tx = 0; tx < maxtiles; tx++)
          {
            int cur_txmin = std::min (std::max (txmin - twidth * (maxtiles / 2)
                                                    + tx * twidth,
                                                0),
                                      imgp[0]->width - twidth - 1)
                            & ~1;
            int cur_tymin
                = std::min (std::max (tymin - theight * (maxtiles / 2)
                                          + ty * theight,
                                      0),
                            imgp[0]->height - theight - 1)
                  & ~1;
            // int cur_txmax = cur_txmin + twidth;
            // int cur_tymax = cur_tymin + theight;
            finetune_solver solver;
            solver.set_profile (profile);
            solver.n_tiles = 1;
            solver.twidth = twidth;
            solver.theight = theight;
            solver.pixel_size = pixel_size;
            solver.render_sharpen_params = rparam.sharpen;
            solver.collection_threshold = rparam.collection_threshold;
            solver.parallel = !(fparams.flags & finetune_no_progress_report);
            if (!solver.init_tile (0, cur_txmin, cur_tymin, bw, *mapp[0],
                                   render))
              {
                failed.store (true, std::memory_order_relaxed);
                continue;
              }
            solver.init (fparams, rparam.screen_blur_radius,
                         rparam.red_strip_width, rparam.green_strip_width,
                         bw_is_simulated_infrared, tile_sharpened, results);
            if (progress && progress->cancel_requested ())
              continue;
            coord_t fit_score = solver.solve (
                progress, !(fparams.flags & finetune_no_progress_report)
                              && maxtiles == 1);

            if (maxtiles * maxtiles > 1
                && !(fparams.flags & finetune_no_progress_report) && progress)
              progress->inc_progress ();
#pragma omp critical
            {
              if (valid_fit_score_p (fit_score)
                  && (!valid_fit_score_p (best_fit_score)
                      || best_fit_score > fit_score))
                {
                  best_solver = std::move (solver);
                  best_fit_score = fit_score;
                }
            }
          }
      gsl_set_error_handler (old_handler);
    }
  else
    {
      best_solver.n_tiles = n_tiles;
      best_solver.twidth = twidth;
      best_solver.theight = theight;
      best_solver.collection_threshold = rparam.collection_threshold;
      best_solver.render_sharpen_params = rparam.sharpen;
      best_solver.pixel_size = pixel_size;
      best_solver.parallel = !(fparams.flags & finetune_no_progress_report);
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          int cur_txmin = std::min (std::max (x[tileid] - twidth / 2, 0),
                                    imgp[tileid]->width - twidth - 1)
                          & ~1;
          int cur_tymin = std::min (std::max (y[tileid] - theight / 2, 0),
                                    imgp[tileid]->height - theight - 1)
                          & ~1;
          const bool tile_uses_simulated_infrared
              = bw && (rparam2.ignore_infrared
                       || !imgp[tileid]->has_grayscale_or_ir ());
          if (bw && tileid == 0)
            bw_is_simulated_infrared = tile_uses_simulated_infrared;
          else if (bw
                   && bw_is_simulated_infrared
                          != tile_uses_simulated_infrared)
            {
              /* One solver has one colour/constraint model for all BW tiles.
                 Mixing measured IR with RGB-derived grayscale would make that
                 model depend on the tile while the fitted parameters remain
                 shared.  Reject the ambiguous configuration rather than
                 silently relaxing every tile to simulated-IR semantics.  */
              ret.err = "mixed measured and simulated infrared tiles";
              return finish ();
            }
          /* FIXME: We only use render_to_scr since we eventually want to know
             pixel size. For stitched projects this is wrong.  */
          render render (*imgp[tileid], rparam2, 256);
          if (!render.precompute_all (
                  bw ? PRECOMPUTE_IMAGE_LAYER : PRECOMPUTE_RGB_IMAGE,
                  patch_proportions (param.type, &rparam2),
                  !(fparams.flags & finetune_no_progress_report) ? progress
                                                                 : nullptr))
            {
              ret.err = "precomputing failed";
              return finish ();
            }
          if (progress && progress->cancel_requested ())
            {
              ret.err = "cancelled";
              return finish ();
            }
          if (cur_txmin < 0 || cur_tymin < 0)
            {
              ret.err = "tile too large for image";
              return finish ();
            }
          if (!best_solver.init_tile (tileid, cur_txmin, cur_tymin, bw,
                                      *mapp[tileid], render))
            {
              ret.err = "out of memory";
              return finish ();
            }
        }
      best_solver.init (fparams, rparam.screen_blur_radius,
                        rparam.red_strip_width, rparam.green_strip_width,
                        bw_is_simulated_infrared, tile_sharpened, results);
      gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
      /* Wild guessing needs brute forcing scale and rotation.  */
      if (best_solver.optimize_coordinates == 2)
        {
          best_fit_score = -1;
	  /* Limit angles and assume that original is scanned in sane position.  */
	  static constexpr coord_t paget_angles[][2]={{-5,5},{-15,-5},{5,15},{-25,-15},{15,25}};
	  const int n_paget_angles = sizeof (paget_angles) / sizeof (coord_t[2]);

	  /* Dufay is rotated by 23 degrees and it is not symmetric.  */
	  constexpr coord_t dufay_a = 23;
	  static constexpr coord_t dufay_angles[][2]
		  ={{dufay_a-5,dufay_a+5}, {dufay_a-15,dufay_a-5}, {dufay_a+5,dufay_a+15},
		    {dufay_a-5+90,dufay_a+5+90}, {dufay_a-15+90,dufay_a-5+90}, {dufay_a+5+90,dufay_a+15+90}};

	  const int n_dufay_angles = sizeof (dufay_angles) / sizeof (coord_t[2]);

	  const coord_t (*angles)[2] = paget_angles;
	  int n_angles = n_paget_angles;

	  if (param.type == Dufay)
	    {
	      angles = dufay_angles;
	      n_angles = n_dufay_angles;
	    }
	      

#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        fparams, rparam, pixel_size, best_fit_score, verbose, imgp, twidth, \
            theight, txmin, tymin, bw, progress, mapp, failed, best_solver,   \
            results, bw_is_simulated_infrared, tile_sharpened, n_angles, angles, \
            profile)
          for (int i = 0; i < 50; i++)
            {
              for (int rot = 0; rot < n_angles; rot ++)
                {
                  finetune_solver solver;
                  solver.set_profile (profile);
                  solver.n_tiles = 1;
                  solver.twidth = twidth;
                  solver.theight = theight;
                  solver.collection_threshold = rparam.collection_threshold;
                  solver.render_sharpen_params = rparam.sharpen;
                  solver.pixel_size = pixel_size;
                  solver.parallel
                      = !(fparams.flags & finetune_no_progress_report);

                  bool r;
#pragma omp critical
                  r = solver.copy_tile (0, best_solver);
                  if (!r)
                    {
                      failed.store (true, std::memory_order_relaxed);
                      continue;
                    }
                  solver.min_scale = geom_sequence (1/50.0, 1/1.5, 50, i);
                  solver.max_scale = geom_sequence (1/50.0, 1/1.5, 50, i+1);
                  solver.min_rotate = angles[rot][0];
                  solver.max_rotate = angles[rot][1];
                  solver.init (
                      fparams, rparam.screen_blur_radius,
                      rparam.red_strip_width, rparam.green_strip_width,
                      bw_is_simulated_infrared, tile_sharpened, results);
                  coord_t u = solver.solve (
                      progress,
                      !(fparams.flags & finetune_no_progress_report));
                  //printf ("step %i rotate %i %f %f %f\n", i, rot, u, solver.get_scale (solver.start.data ()), solver.get_rotation (solver.start.data ()));
#pragma omp critical
                  {
                    if (valid_fit_score_p (u)
                        && (!valid_fit_score_p (best_fit_score)
                            || best_fit_score > u))
                      {
                        best_solver = std::move (solver);
                        best_fit_score = u;
                      }
                  }
                }
            }
        }
      else
        best_fit_score = best_solver.solve (
            progress, !(fparams.flags & finetune_no_progress_report));
      gsl_set_error_handler (old_handler);
    }
  if (progress && progress->cancel_requested ())
    {
      ret.err = "cancelled";
      return finish ();
    }
  if (failed.load (std::memory_order_relaxed))
    {
      ret.err = "failed memory allocation";
      return finish ();
    }
  if (!valid_fit_score_p (best_fit_score))
    {
      ret.err = "invalid fit-quality score";
      return finish ();
    }

  if (best_solver.least_squares)
    {
      best_solver.alloc_least_squares ();
      if (!best_solver.optimize_fog || best_solver.fog_by_least_squares)
        best_solver.init_least_squares (best_solver.start.data ());
    }
  if (!best_solver.tiles[0].color.empty () && fparams.ignore_outliers > 0)
    best_solver.determine_outliers (best_solver.start.data (),
                                    fparams.ignore_outliers);
  else if (fparams.ignore_outliers > 0)
    best_solver.bw_determine_outliers (best_solver.start.data (),
                                       fparams.ignore_outliers);
  if (best_solver.has_outliers ())
    {
      best_solver.resume_interpolated_focus ();
      coord_t refined_objective = simplex<coord_t, finetune_solver> (
          best_solver, "finetuning with outliers", progress,
          !(fparams.flags & finetune_no_progress_report));
      if (best_solver.interpolated_focus_p ())
        refined_objective = best_solver.evaluate_final_focus_exactly ();
      /* The second simplex changes both the optimum and its fit-quality score.
         Keeping the score from the pre-outlier fit made the adaptive focus
         rejection threshold describe a different solution.  */
      best_fit_score
          = best_solver.scale_fit_score_by_contrast (refined_objective);
    }
  if (progress && progress->cancel_requested ())
    {
      ret.err = "cancelled";
      return finish ();
    }

  if (!valid_fit_score_p (best_fit_score))
    {
      ret.err = "invalid refined fit-quality score";
      return finish ();
    }
  ret.uncertainty = best_fit_score;
  if (verbose)
    {
      if (progress)
        progress->pause_stdout ();
      best_solver.print_values (best_solver.start.data ());
      if (progress)
        progress->resume_stdout ();
    }
  best_solver.set_results (ret, param, rparam, verbose, progress);
  if (!ret.success)
    return finish ();

  if (fparams.simulated_file)
    best_solver.write_file (best_solver.start.data (), fparams.simulated_file,
                            0, 0);
  if (fparams.orig_file)
    best_solver.write_file (best_solver.start.data (), fparams.orig_file, 0,
                            1);
  if (fparams.sharpened_file)
    best_solver.write_file (best_solver.start.data (), fparams.sharpened_file,
                            0, 3);
  if (fparams.diff_file)
    best_solver.write_file (best_solver.start.data (), fparams.diff_file, 0,
                            2);
  if (fparams.flags & finetune_produce_images)
    {
      ret.simulated
          = best_solver.produce_image (best_solver.start.data (), 0, 0);
      ret.orig = best_solver.produce_image (best_solver.start.data (), 0, 1);
      if (best_solver.optimize_sharpening)
        ret.sharpened
            = best_solver.produce_image (best_solver.start.data (), 0, 3);
      ret.diff = best_solver.produce_image (best_solver.start.data (), 0, 2);
      ret.screen = best_solver.original_scr->get_image ();
      ret.blurred_screen = best_solver.tiles[0].scr->get_image ();
      if (best_solver.optimize_emulsion_blur)
        ret.emulsion_screen = best_solver.emulsion_scr->get_image ();
      if (best_solver.optimize_emulsion_intensities)
        ret.merged_screen = best_solver.tiles[0].merged_scr->get_image ();

      screen tmp;
      best_solver.collect_screen (&tmp, best_solver.start.data (), 0);
      ret.collected_screen = tmp.get_image ();

      screen scr, scr1;
      scr1.initialize_dot ();
      if (best_solver.apply_blur (best_solver.start.data (), 0, &scr, &scr1))
        ret.dot_spread = scr.get_image (true, 1);
    }
  if (fparams.screen_file)
    best_solver.original_scr->save_tiff (fparams.screen_file);
  if (fparams.screen_blur_file)
    best_solver.tiles[0].scr->save_tiff (fparams.screen_blur_file);
  if (best_solver.emulsion_scr && fparams.emulsion_file)
    best_solver.emulsion_scr->save_tiff (fparams.emulsion_file);
  if (best_solver.tiles[0].merged_scr && fparams.merged_file)
    best_solver.tiles[0].merged_scr->save_tiff (fparams.merged_file);
  if (fparams.collected_file)
    {
      screen tmp;
      best_solver.collect_screen (&tmp, best_solver.start.data (), 0);
      tmp.save_tiff (fparams.collected_file);
    }
  if (fparams.dot_spread_file)
    {
      screen scr, scr1;
      scr1.initialize_dot ();
      if (best_solver.apply_blur (best_solver.start.data (), 0, &scr, &scr1))
        scr.save_tiff (fparams.dot_spread_file, true, 1);
    }
  // printf ("%i %i %i %i %f %f %f %f\n", bx, by, fsx, fsy,
  // best_solver.tile_pos[twidth/2+(theight/2)*twidth].x,
  // best_solver.tile_pos[twidth/2+(theight/2)*twidth].y, fp.x, fp.y);
  return finish ();
}

/* Finetune SOLVER parameters in given AREA using RPARAM and PARAM in IMG.
   PROGRESS is used to report progress.  
   Assume the registration is correct only in existing points in AREA.
   Start from these and try to carefully insert new points.  */

DLL_PUBLIC bool
finetune_misregistered_area (solver_parameters *solver,
			     render_parameters & rparam,
			     const scr_to_img_parameters & param,
			     const image_data & img,
			     const int_image_area & in_area,
			     const finetune_area_parameters & fparam,
			     progress_info *progress)
{
  if (!solver || !valid_finetune_area_parameters_p (fparam))
    return false;
  int_image_area area = in_area.intersect ({ 0, 0, img.width, img.height });
  const bool verbose = false;
  if (area.empty_p () || param.type == Random)
    {
      if (verbose)
	printf ("Finetuning area failed since area is empty or screen is "
		"Random\n");
      return false;
    }
  int xsteps, ysteps;
  fparam.get_grid_dimensions (area, param, &xsteps, &ysteps);
  size_t requested_grid_size;
  if (!valid_finetune_grid_size_p (xsteps, ysteps,
                                   &requested_grid_size))
    {
      if (verbose)
	printf ("Finetuning area failed because the requested grid is invalid\n");
      return false;
    }
  (void)requested_grid_size;
  int_image_area crop = rparam.get_scan_crop (img.width, img.height);
  if (crop.empty_p ())
    return false;
  int xstep = std::max (1, crop.width / xsteps);
  int ystep = std::max (1, crop.height / ysteps);
  if (verbose)
    printf ("Aiming for %ix%i grid, steps %ix%i\n", xsteps, ysteps, xstep,
	    ystep);

  int max_points = 10000;

  if (!solver->points.size ())
    {
      /* If registration seem to make sense, try to expand it.  */
      if (!area.contains_p (
			     {
			     (int) param.center.x, (int) param.center.y})
	  || param.coordinate1.length () < 3
	  || param.coordinate2.length () < 3)
	{
	  if (verbose)
	    printf ("Finetuning area failed since there are no solver points "
		    "and coordinate system starts elsewhere\n");
	  return false;
	}
      finetune_parameters fparam;
      fparam.flags |= finetune_position /*| finetune_multitile */  | finetune_bw
	| finetune_no_progress_report;
      finetune_result res = finetune (rparam, param, img, { param.center },
				      nullptr, fparam, progress);
      finetune_result res2
	= finetune (rparam, param, img, { param.center + param.coordinate1 },
		    nullptr, fparam, progress);
      finetune_result res3
	= finetune (rparam, param, img, { param.center + param.coordinate2 },
		    nullptr, fparam, progress);
      if (!res.success || !res2.success || !res3.success)
	{
	  if (verbose)
	    printf ("Finetuning area failed since we failed to identify 3 "
		    "basis points\n");
	  return false;
	}
      solver->add_point (res.solver_point_img_location,
			 res.solver_point_screen_location,
			 res.solver_point_color);
      solver->add_or_modify_point (res2.solver_point_img_location,
				   res2.solver_point_screen_location,
				   res2.solver_point_color);
      solver->add_or_modify_point (res3.solver_point_img_location,
				   res3.solver_point_screen_location,
				   res3.solver_point_color);
      max_points = 50;
      xstep =
	my_ceil (std::
		 max (fabs (param.coordinate1.x),
		      fabs (param.coordinate2.x)));
      ystep =
	my_ceil (std::
		 max (fabs (param.coordinate1.y),
		      fabs (param.coordinate2.y)));
      if (verbose)
	printf ("Finetuning area started by adding basis, steps %i %i\n",
		xstep, ystep);
    }
  /* See if points are correlated around a line.  In this case the current
     solution is probably quite iffy and we need to expand slowly.  */
  else if (solver->points.size () < 10000)
    {
      point_t origin, dir;
      double line_width = solver->fit_line (origin, dir);
      const int ratio = 5;
      if (xstep > line_width / ratio)
	{
	  xstep = my_ceil ((line_width) / ratio);
	  max_points = 50;
	}
      if (ystep > line_width / ratio)
	{
	  ystep = my_ceil ((line_width) / ratio);
	  max_points = 50;
	}
    }

  /* Too small step will lead to re-solving existing points only.  */
  xstep = std::max (xstep,
		    (int) my_ceil (std::max (fabs (param.coordinate1.x),
					     fabs (param.coordinate2.x)) *
				   2));
  ystep =
    std::max (ystep,
	      (int) my_ceil (std::
			     max (fabs (param.coordinate1.y),
				  fabs (param.coordinate2.y)) * 2));

  /* Locate trusted anchors in AREA.  The routine's contract says that points
     outside AREA are not known to be registered correctly, so an existing
     global point set with no local anchor cannot seed this flood fill.  */
  int xmin = INT_MAX;
  int ymin = INT_MAX;
  int xmax = INT_MIN;
  int ymax = INT_MIN;
  int points_in_area = 0;
  for (const auto &p : solver->points)
    if (area.contains_p ({ (int)p.img.x, (int)p.img.y }))
      {
        xmin = std::min (xmin, (int)p.img.x);
        ymin = std::min (ymin, (int)p.img.y);
        xmax = std::max (xmax, (int)p.img.x);
        ymax = std::max (ymax, (int)p.img.y);
        points_in_area++;
      }
  if (!points_in_area)
    return false;

  /* We may end up with a very large grid; limit it to the maximal search
     distance without overflowing int_image_area arithmetic.  */
  if (max_points < 1000)
    {
      const int64_t xmargin = (int64_t)xstep * max_points;
      const int64_t ymargin = (int64_t)ystep * max_points;
      const int64_t area_right = (int64_t)area.x + area.width;
      const int64_t area_bottom = (int64_t)area.y + area.height;
      const int64_t left = std::max ((int64_t)area.x,
                                     (int64_t)xmin - xmargin);
      const int64_t top = std::max ((int64_t)area.y,
                                    (int64_t)ymin - ymargin);
      const int64_t right = std::min (area_right,
                                      (int64_t)xmax + xmargin + 1);
      const int64_t bottom = std::min (area_bottom,
                                       (int64_t)ymax + ymargin + 1);
      if (right <= left || bottom <= top || left < INT_MIN || top < INT_MIN
          || right > INT_MAX || bottom > INT_MAX)
        return false;
      int_image_area max_search_range
          = { (int)left, (int)top, (int)(right - left),
              (int)(bottom - top) };
      if (verbose)
	printf ("Intersecting with range %i %i %i %i\n", max_search_range.x,
		max_search_range.y, max_search_range.width,
		max_search_range.height);
      area = area.intersect (max_search_range);
      if (area.empty_p ())
        return false;
    }

  const int range = 3;
  int xsubstep = std::max (1, xstep / range);
  int ysubstep = std::max (1, ystep / range);
  int xsubsteps
      = (int)std::max<int64_t> (1, ((int64_t)area.width + xsubstep - 1)
                                      / xsubstep);
  int ysubsteps
      = (int)std::max<int64_t> (1, ((int64_t)area.height + ysubstep - 1)
                                      / ysubstep);
  size_t tile_count;
  if (!valid_finetune_grid_size_p (xsubsteps, ysubsteps, &tile_count))
    return false;
  int npoints;
  int nfound = 0;
  coord_t max_uncertainty = 10000;

  enum elt
  {
    unknown,
    known,
    to_be_computed,
    bad
  };

  std::vector<elt> tiles (tile_count, unknown);

  const auto get_cell_pos =[area, xsubstep, ysubstep] (point_t p)->int_point_t {
    return {(int64_t) my_floor ((p.x - area.x) / (coord_t) xsubstep),
	    (int64_t) my_floor ((p.y - area.y) / (coord_t) ysubstep)};
  };
  const auto in_range =[xsubsteps, ysubsteps] (int_point_t p)->bool {
    return p.x >= 0 && p.x < xsubsteps && p.y >= 0 && p.y < ysubsteps;
  };
  const auto set_cell =
    [in_range, &tiles, xsubsteps] (int_point_t p, enum elt value)->void {
    if (in_range (p))
      tiles[p.y * xsubsteps + p.x] = value;
  };
  const auto get_cell =[in_range, &tiles, xsubsteps] (int_point_t p)->elt {
    assert (in_range (p));
    return tiles[p.y * xsubsteps + p.x];
  };

  if (verbose)
    printf ("Adding points to area with top left (%i,%i) width %i height %i, "
	    "steps %i %i size %i %i with known points %i\n",
	    area.x, area.y, area.width, area.height, xsubsteps, ysubsteps,
	    xsubstep, ysubstep, (int) solver->points.size ());
for (auto p:solver->points)
    set_cell (get_cell_pos (p.img), known);

  /* This is essentialy an floodfill.  If there is 3x3 tile
     with no known data such that just outside of it exists
     known point; enqueue its center for finetuning.  */
  do
    {
      std::vector < point_t > points;
      for (int y = range; y < ysubsteps - range; y++)
	for (int x = range; x < xsubsteps - range; x++)
	  {
	    bool ok = true;
	    /* If range is 3 we search

	       b b b b b b b
	       b . . . . . p
	       b . . . . . p
	       b . . p . . p
	       b . . . . . p
	       b . . . . . p
	       b b b b b b b

	       p is the tile we consider to compute points n.
	       . is required to have no control point
	       b is required to have at least one control point.

	       So at the end, the computed points should approximately
	       make grid with spacing of 3

	       p . . p . . p
	       . . . . . . .
	       . . . . . . .
	       p . . p . . p
	       . . . . . . .
	       . . . . . . .
	       p . . p . . p */
	    for (int yy = y - range + 1; yy <= y + range - 1 && ok; yy++)
	      for (int xx = x - range + 1; xx <= x + range - 1 && ok; xx++)
		if (get_cell (
			       {
			       xx, yy}
		    ) != unknown)
	      ok = false;
	    if (!ok)
	      continue;
	    int nknown = 0;
	    for (int yy = y - range; yy <= y + range; yy++)
	      for (int xx = x - range; xx <= x + range; xx++)
		if (get_cell (
			       {
			       xx, yy}
		    ) == known)
	      nknown++;
	    if (!nknown)
	      continue;
	    set_cell (
		       {
		       x, y}
		       , to_be_computed);
	    points.push_back (
			       {
			       ((x + (coord_t) 0.5) * xsubstep) + area.x,
			       ((y + (coord_t) 0.5) * ysubstep) + area.y}
	    );
	    assert ((get_cell_pos (points.back ()) == (int_point_t)
		     {
		     x, y}
		    ));
	    if (verbose && 0)
	      printf ("Will compute %i %i\n", x, y);
	  }
      if (!points.size ())
	break;
      if (progress)
	progress->set_task ("finetuning points nearby known points",
			    points.size ());
      std::vector < finetune_result > res (points.size ());
      /* We are going to initialize render inside of nested region.
         TODO: We probably want to set omp_nested on proper place.  */
#ifdef _OPENMP
      omp_set_max_active_levels (3);
#endif
#pragma omp parallel for default(none) schedule(dynamic)                      \
    shared(rparam, param, progress, img, solver, res, points)
      for (size_t i = 0; i < points.size (); i++)
	{
	  if (progress && progress->cancel_requested ())
	    continue;
	  finetune_parameters fparam;
	  fparam.flags |= finetune_position /*| finetune_multitile */  | finetune_bw
	    | finetune_no_progress_report;
	  res[i] = finetune (rparam, param, img,
			     {
			     points[i]}
			     , nullptr, fparam, progress);
	  if (progress)
	    progress->inc_progress ();
	}
      if (progress && progress->cancel_requested ())
	{
	  if (verbose)
	    printf ("Finetuning area cancelled\n");
	  return false;
	}
      if (progress)
	progress->set_task ("processing points found", 1);

      scr_to_img map;
      if (!map.set_parameters (param, img))
	{
	  if (verbose)
	    printf ("Finetuning area failed since it failed to initialize "
		    "scr-to-img\n");
	  return false;
	}

      /* Clear info about points to be computed.  */
      for (int i = 0; i < xsubsteps * ysubsteps; i++)
	if (tiles[i] == to_be_computed)
	  tiles[i] = unknown;
      npoints = 0;
      if (verbose)
	printf ("Will consider %i results\n", (int) res.size ());
      /* Prune failed, poorly registered, or poorly conditioned points.  */
      for (size_t i = 0; i < res.size ();)
	{
	  finetune_result & r = res[i];
	  bool ok = r.success && valid_fit_score_p (r.uncertainty)
	            && my_isfinite (r.contrast);
	  if (!ok)
	    {
	      /* The common removal path below records no additional geometry for
	         a failed or non-finite fit.  */
	    }
	  else if (fabs
		   (get_cell_pos (r.solver_point_img_location).x
		        - get_cell_pos (points[i]).x)
		       > 3
		   || fabs (get_cell_pos (r.solver_point_img_location).y
		            - get_cell_pos (points[i]).y)
		          > 3)
	    {
	      if (verbose)
		printf
		  ("found point: %f %f which is too far from desired location %f %f\n",
		   r.solver_point_img_location.x,
		   r.solver_point_img_location.y, points[i].x, points[i].y);
	      set_cell (get_cell_pos (r.solver_point_img_location), bad);
	      ok = false;
	    }
	  /* Point must be new.  */
	  else if (solver->find_point (r.solver_point_screen_location) >= 0)
	    {
	      if (verbose)
		printf ("found point: %f %f which already exists\n",
			r.solver_point_img_location.x,
			r.solver_point_img_location.y);
	      set_cell (get_cell_pos (r.solver_point_img_location), known);
	      ok = false;
	    }
	  /* Check contrast to be within threshold.  */
	  else if (r.contrast < fparam.min_contrast)
	    {
	      if (verbose)
		printf ("found point: %f %f with too small contrast %f\n",
			r.solver_point_img_location.x,
			r.solver_point_img_location.y, r.contrast);
	      set_cell (get_cell_pos (r.solver_point_img_location), bad);
	      ok = false;
	    }
	  /* Check distance threshold.  */
	  else
	    {
	      point_t transformed = map.to_scr (r.solver_point_img_location);
	      ok = transformed.dist_from (r.solver_point_screen_location)
		< fparam.max_displacement;
	      int_point_t cell = get_cell_pos (r.solver_point_img_location);
	      if (!ok)
		set_cell (cell, bad);
	      if (verbose)
		printf ("found grid: %i %i transformed: %f %f finetuned: %f "
			"%f displacement %f %s\n",
			(int) cell.x, (int) cell.y, transformed.x,
			transformed.y, r.solver_point_screen_location.x,
			r.solver_point_screen_location.y,
			transformed.dist_from (r.
					       solver_point_screen_location),
			ok ? "in threshold" : "out of threshold");
	    }
	  if (!ok)
	    {
	      res[i] = std::move (res.back ());
	      res.pop_back ();
	      points[i] = std::move (points.back ());
	      points.pop_back ();
	    }
	  else
	    i++;
	}
      if (verbose)
	printf ("Will consider %i meaningful results\n", (int) res.size ());
      /* If we have many points; rule out uncertain ones.  Let the value only
         drop in each wave.  */
      if (res.size () > 5)
	{
	  coord_t wave_cutoff;
	  if (!finetune_retained_fit_score_cutoff (res, fparam.uncertainty_ratio,
	                                  &wave_cutoff))
	    return false;
	  /* Let the accepted fit-quality threshold only tighten in later
	     flood-fill waves.  */
	  max_uncertainty = std::min (max_uncertainty, wave_cutoff);
	}

      /* Now add computed points to solver and update tiles.  */
      for (size_t i = 0; i < res.size (); i++)
	{
	  finetune_result & r = res[i];
	  int_point_t cell = get_cell_pos (r.solver_point_img_location);
	  if (r.uncertainty <= max_uncertainty
	      && solver->find_point (r.solver_point_screen_location) < 0)
	    {
	      solver->add_point (r.solver_point_img_location,
				 r.solver_point_screen_location,
				 r.solver_point_color);
	      set_cell (cell, known);
	      nfound++;
	      npoints++;
	    }
	  else
	    {
	      if (verbose)
		printf ("Bad point %f %f %i\n", r.uncertainty, max_uncertainty,solver->find_point (r.solver_point_screen_location));
	      set_cell (cell, bad);
	    }
	}
      if (nfound > max_points)
	{
	  if (verbose)
	    printf ("reached max points of %i\n", max_points);
	  return true;
	}
    }
  while (npoints);
  if (verbose)
    printf ("found %i points\n", nfound);
  return true;
}

/* Finetune SOLVER parameters in given AREA using RPARAM and PARAM in IMG.
   PROGRESS is used to report progress.  */

DLL_PUBLIC bool
finetune_area (solver_parameters *solver, render_parameters &rparam,
               const scr_to_img_parameters &param, const image_data &img,
               const int_image_area &in_area,
	       const finetune_area_parameters &fparam,
	       progress_info *progress)
{
  if (!solver || !valid_finetune_area_parameters_p (fparam))
    return false;
  int_image_area area = in_area.intersect ({ 0, 0, img.width, img.height });
  if (area.empty_p ())
    return false;
  int xsteps, ysteps;
  fparam.get_grid_dimensions (area, param, &xsteps, &ysteps);
  size_t requested_grid_size;
  if (!valid_finetune_grid_size_p (xsteps, ysteps,
                                   &requested_grid_size))
    return false;
  (void)requested_grid_size;
  int_image_area crop = rparam.get_scan_crop (img.width, img.height);
  if (crop.empty_p ())
    return false;
  int xstep = std::max (1, crop.width / xsteps);
  int ystep = std::max (1, crop.height / ysteps);
  xsteps = (int)(((int64_t)area.width + xstep - 1) / xstep);
  ysteps = (int)(((int64_t)area.height + ystep - 1) / ystep);
  size_t result_count;
  if (!valid_finetune_grid_size_p (xsteps, ysteps, &result_count))
    return false;
  std::vector<finetune_result> res (result_count);
  if (progress)
    progress->set_task ("finetuning grid", (int)result_count);
  /* We are going to initialize render inside of nested region.
     TODO: We probably want to set omp_nested on proper place.  */
#ifdef _OPENMP
  omp_set_max_active_levels (3);
#endif
  if (xsteps > 1 || ysteps > 1)
    {
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(xsteps, ysteps, rparam, param, progress, img, solver, res, area,   \
               xstep, ystep)
      for (int x = 0; x < xsteps; x++)
        for (int y = 0; y < ysteps; y++)
          {
            if (progress && progress->cancel_requested ())
              continue;
            finetune_parameters fparam;
            fparam.flags
                |= finetune_position /*| finetune_multitile*/ | finetune_bw
                   | finetune_no_progress_report;
            res[x + y * xsteps] = finetune (
                rparam, param, img,
                { { (coord_t)area.x + (x /*+ 0.5*/) * xstep, (coord_t)area.y + (y /*+ 0.5*/) * ystep } },
                nullptr, fparam, progress);
            if (progress)
              progress->inc_progress ();
          }
    }
  else if (!progress || !progress->cancel_requested ())
    {
      finetune_parameters fparam;
      fparam.flags |= finetune_position /*| finetune_multitile*/ | finetune_bw;
      res[0]
          = finetune (rparam, param, img,
                      { { (coord_t)area.x + /*(0.5) **/ xstep, (coord_t)area.y /*+ (0.5) * ystep*/ } },
                      nullptr, fparam, progress);
      if (progress)
        progress->inc_progress ();
    }
  if (progress && progress->cancel_requested ())
    return false;
  coord_t max_uncertainty;
  if (!finetune_retained_fit_score_cutoff (res, fparam.uncertainty_ratio,
                                  &max_uncertainty))
    return false;
  for (int x = 0; x < xsteps; x++)
    for (int y = 0; y < ysteps; y++)
      {
        finetune_result &r = res[x + y * xsteps];
        if (r.success && r.uncertainty <= max_uncertainty
	    && r.contrast > fparam.min_contrast
	    && solver->find_point (r.solver_point_screen_location) < 0)
	  {
	    solver->add_point (r.solver_point_img_location,
			       r.solver_point_screen_location,
			       r.solver_point_color);
	  }
      }
  return true;
}

/* Simulate data collection of scan of given color screen (assumed to be
   blurred) and return collected red, green and blue.  This can be used to
   increase color saturation to compensate losses caused by the collection.

   RET_RED, RET_GREEN, RET_BLUE are returned colors.
   SCR is screen used to render the pattern, while COLLECTION_SCR is used to do
   the data collection.  SIMULATED_SCREEN is optional simulated screen.
   SAMPLING specifies whether SCR still needs capture-pixel integration.
   THRESHOLD is the collection threshold.  SHARPEN_PARAM are sharpen
   parameters. MAP is the scr-to-img map.  AREA defines the area.  */

bool
determine_color_loss (rgbdata *ret_red, rgbdata *ret_green, rgbdata *ret_blue,
                      screen &scr, screen &collection_scr,
                      simulated_screen *simulated_screen,
                      screen_sampling sampling, luminosity_t threshold,
                      const sharpen_parameters &sharpen_param, scr_to_img &map,
                      int_image_area area)
{
  double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
  double wr = 0, wg = 0, wb = 0;
  const bool debugfiles = false;

  if (debugfiles)
    {
      scr.save_tiff ("/tmp/scr.tif", false, 3);
      collection_scr.save_tiff ("/tmp/collection-scr.tif", false, 3);
    }

  sharpen_parameters::sharpen_mode sharpen_mode = sharpen_param.get_mode ();
  if (simulated_screen)
    {
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, simulated_screen)                                 \
    reduction(+ : wr, wg, wb, red, green, blue)
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            /* Collection and screen colors are the same.  */
            rgbdata m = simulated_screen->get_pixel (x, y);
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += m * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += m * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += m * val;
              }
          }
    }
  /* If sharpening is not needed, we can avoid temporary buffer to store
     rendered screen.  */
  else if (sharpen_mode == sharpen_parameters::none)
    {
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, map, scr, collection_scr, sampling)               \
    reduction(+ : wr, wg, wb, red, green, blue)
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            point_t p;
            rgbdata am;
            /* Render the capture sample according to the explicit owner of
               the sensor aperture.  */
            if (sampling == screen_sampling::point_sample)
              am = noantialias_screen (scr, map, x, y, &p);
            else
              am = antialias_screen (scr, map, x, y, &p);
            /* Data collection does not antialias.  So just take pixel in the
               middle  */
            rgbdata m = collection_scr.noninterpolated_mult (p);
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += am * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += am * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += am * val;
              }
          }
    }
  else
    {
      int ext;
      if (sharpen_param.deconvolution_p ())
        {
          std::shared_ptr<mtf> cur_mtf
              = mtf::get_mtf (sharpen_param.scanner_mtf, nullptr);
          if (!cur_mtf || !cur_mtf->precompute ())
            return false;
          ext = cur_mtf->psf_size (sharpen_param.scanner_mtf_scale);
        }
      else
        ext = fir_blur::convolve_matrix_length (sharpen_param.usm_radius) / 2;
      int xsize = area.width + 2 * ext;
      int ysize = area.height + 2 * ext;
      std::vector<rgbdata> rendered (xsize * ysize);

      /* Render capture samples before applying the digital filter.  */
      if (sampling == screen_sampling::point_sample)
        for (int y = area.y - ext; y < area.y + area.height + ext; y++)
          for (int x = area.x - ext; x < area.x + area.width + ext; x++)
            rendered[(y - area.y + ext) * xsize + x - area.x + ext]
                = noantialias_screen (scr, map, x, y);
      else
        for (int y = area.y - ext; y < area.y + area.height + ext; y++)
          for (int x = area.x - ext; x < area.x + area.width + ext; x++)
            rendered[(y - area.y + ext) * xsize + x - area.x + ext]
                = antialias_screen (scr, map, x, y);

      /* Sharpen it  */
      std::vector<rgbdata> rendered2 (xsize * ysize);
      /* FIXME: parallelism is disabled because sometimes we are called from
       * parallel block.  */
      if (!sharpen_param.deconvolution_p ())
        sharpen<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
            rendered2.data (), rendered.data (), xsize, ysize, ysize,
            sharpen_param.usm_radius, sharpen_param.usm_amount, nullptr,
            false);
      else
        {
          if (!deconvolve_rgb<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
		      rendered2.data (), rendered.data (), xsize, ysize, ysize,
		      sharpen_param, nullptr, false))
	    return false;
        }

      if (debugfiles)
        {
          tiff_writer_params p;
          void *buffer;
          size_t len = create_linear_srgb_profile (&buffer);
          p.icc_profile = buffer;
          p.icc_profile_len = len;
          p.filename = "/tmp/sharpened.tif";
          p.width = xsize;
          p.height = ysize;
          p.depth = 16;
          const char *error;
          {
            tiff_writer renderedt (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    rgbdata d
                        = rendered2[(y - area.y + ext) * xsize + x - area.x + ext];
                    if (x == area.x - 1 || y == area.y - 1 || x == area.x + area.width
                        || y == area.y + area.height)
                      d = { 1, 1, 1 };
                    renderedt.put_pixel (
                        x - area.x + ext,
                        std::clamp (d.red, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (d.green, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (d.blue, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535);
                  }
                if (!renderedt.write_row ())
                  return false;
              }
          }
          {
            p.filename = "/tmp/unsharpened.tif";
            tiff_writer renderedt (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    rgbdata d
                        = rendered[(y - area.y + ext) * xsize + x - area.x + ext];
                    renderedt.put_pixel (x - area.x + ext, d.red * (luminosity_t)65535,
                                         d.green * (luminosity_t)65535, d.blue * (luminosity_t)65535);
                  }
                if (!renderedt.write_row ())
                  return false;
              }
          }
          {
            p.filename = "/tmp/collection.tif";
            tiff_writer renderedu (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    point_t p
                        = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
                    rgbdata m = collection_scr.noninterpolated_mult (p);
                    renderedu.put_pixel (x - area.x + ext,
                        std::clamp (m.red, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (m.green, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (m.blue, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535);
                  }
                if (!renderedu.write_row ())
                  return false;
              }
          }
          free (buffer);
        }

      /* Collect data  */
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, map, scr, collection_scr,rendered2,ext,xsize)     \
    reduction(+ : wr, wg, wb, red, green, blue)
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            point_t p = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
            rgbdata m = collection_scr.noninterpolated_mult (p);
            rgbdata am = rendered2[(y - area.y + ext) * xsize + x - area.x + ext];
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += am * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += am * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += am * val;
              }
          }
    }
  if (!(wr > 0 && wg > 0 && wb > 0))
    {
      *ret_red = { 1, 0, 0 };
      *ret_green = { 0, 1, 0 };
      *ret_blue = { 0, 0, 1 };
      return false;
    }
  //printf ("Color loss info %f %f %f\n", wr, wg, wb);
  red /= wr;
  green /= wg;
  blue /= wb;
#if 0
  *ret_red = red;
  *ret_green = green;
  *ret_blue = blue;
#else
  *ret_red = (rgbdata){ (luminosity_t)red.red, (luminosity_t)red.green,
                        (luminosity_t)red.blue };
  *ret_green = (rgbdata){ (luminosity_t)green.red, (luminosity_t)green.green,
                          (luminosity_t)green.blue };
  *ret_blue = (rgbdata){ (luminosity_t)blue.red, (luminosity_t)blue.green,
                         (luminosity_t)blue.blue };
#endif
#if 0
  printf ("Color loss info %i %i %i %i %f\n", area.x, area.y, area.width, area.height, map.pixel_size (area));
  ret_red->print (stdout);
  ret_green->print (stdout);
  ret_blue->print (stdout);
#endif
  return true;
}

/* Render simulated screen pattern to IMG using parameters PARAM, RPARAM and
   DPARAM.  The image is rendered in resolution WIDTH x HEIGHT.  */

bool
render_screen (image_data &img, const scr_to_img_parameters &param,
               const render_parameters &rparam, const scr_detect_parameters &dparam,
               int width, int height)
{
  scr_to_img map;
  if (!img.set_dimensions (width, height, true, false))
    return false;
  if (!map.set_parameters (param, img))
    return false;
  coord_t pixel_size = map.pixel_size ({0, 0, width, height});
  sharpen_parameters sharpen = rparam.sharpen;
  sharpen.usm_radius = rparam.screen_blur_radius * pixel_size;
  sharpen.scanner_mtf_scale *= pixel_size;
  screen_sampling sampling = screen_sampling::integrate_pixel;
  std::shared_ptr<screen> scr = render_to_scr::get_screen (
      param.type, false, false, sharpen, rparam.red_strip_width,
      rparam.green_strip_width, nullptr, nullptr, &sampling);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const rgbdata d
            = sampling == screen_sampling::point_sample
                  ? noantialias_screen (*scr, map, x, y)
                  : antialias_screen (*scr, map, x, y);
        img.put_rgb_pixel (x, y, {
          (unsigned short)(invert_gamma (d.red, rparam.gamma) * 65535),
          (unsigned short)(invert_gamma (d.green, rparam.gamma) * 65535),
          (unsigned short)(invert_gamma (d.blue, rparam.gamma) * 65535)
        });
      }
  return true;
}

static bool
similar_solution_p (finetune_result &res1, int type, finetune_result &res2, int type2)
{
  if (type != type2)
    return false;
  return res1.coordinate1.dist_from (res2.coordinate1) < 1
	 && res1.coordinate2.dist_from (res2.coordinate2) < 1;
}

/* Try to brute-force finetuning and detect screen coordinates.  */

bool
autodetect_coordinates (const image_data &img, scr_to_img_parameters &param,
                        const render_parameters &rparam_1,
                        progress_info *progress)
{
  int steps = 11;
  finetune_parameters fparams;
  /* What screens are autodetected well.
     TODO: For improved dioptichrome we need to add support for angle between
     elements; Normal dioptichrome will be misdetected as Dufay since geometry
     is same, but colors are different.  Similarly for Joly/Warner-Powrie etc.
     We may want to add combined screen types and let user to choose in GUI. */
  constexpr scr_type supported_screns[] = { Paget, Dufay, Joly };
  constexpr int n_supported_types
      = sizeof (supported_screns) / sizeof (scr_type);
  int screens = 1;
  render_parameters rparam = rparam_1;
  rparam.sharpen.mode = sharpen_parameters::none;
  if (param.type == Random)
    screens = n_supported_types;
  std::vector<finetune_result> res (steps * steps * screens);
  fparams.flags = colorscreen::finetune_position
                  | colorscreen::finetune_guess_coordinates
                  | colorscreen::finetune_bw /*| finetune_screen_blur*/;
  if (progress)
    progress->set_task ("autodetecting coordinates in multiple samples",
                        steps * steps * screens);
  for (int y = 0; y < steps; y++)
    for (int x = 0; x < steps; x++)
      for (int scr = 0; scr < screens; scr++)
        if (!progress || !progress->cancel_requested ())
          {
            scr_to_img_parameters p;
            if (param.type == Random)
              p.type = supported_screns[scr];
            else
              p.type = param.type;
            int_image_area area
                = rparam.get_image_area (img.width, img.height);
            p.center = { area.x + (0.5 + x) * area.width / steps,
                         area.y + (0.5 + y) * area.height / steps };
            {
              sub_task task (progress);
              res[y * steps * screens + x * screens + scr]
                  = finetune (rparam, p, img, {}, nullptr, fparams, progress);
            }
            if (progress)
              progress->inc_progress ();
          }

  int best_i = -1;
  int best_n = 0;
  double best_sum = 0;

  for (int i = 0; i < (int)res.size (); i++)
    if (res[i].success && res[i].contrast > 1/1280.0)
      {
	int n = 0;
	double sum = 0;
	double unc = 0;
	int bi = -1;
        for (int j = 0; j < (int)res.size (); j++)
          if (res[j].success && res[j].contrast > 1/1280.0
	      && similar_solution_p (res[i], screens > 1 ? i % screens : 1,
				     res[j], screens > 1 ? j % screens : 1))
	  {
	    n++;
	    sum += 1/res[j].uncertainty;
	    if (bi == -1 || unc > res[j].uncertainty)
	      {
		bi = j;
		unc = res[j].uncertainty;
	      }
	  }
	if (best_i == -1 || best_sum < sum)
	  {
	    best_i = bi;
	    best_sum = sum;
	    best_n = n;
	  }
      }
  if (best_i < 0)
    return false;
  //printf ("Best sum %f, n %i\n", best_sum, best_n);
  param.center = res[best_i].center;
  param.coordinate1 = res[best_i].coordinate1;
  param.coordinate2 = res[best_i].coordinate2;
  if (param.type == Random)
    param.type = supported_screns[best_i % screens];
  return true;
}
} // namespace colorscreen
