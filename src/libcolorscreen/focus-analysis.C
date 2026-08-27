#include "include/focus-analysis.h"
#include "include/scr-to-img.h"
#include "include/scr-to-img-parameters.h"
#include "render-interpolate.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace colorscreen
{
namespace
{

/* Extract the scalar parameter whose stability can be compared directly
   between the joint and leave-one-out fits.  Per-channel focus, strip widths
   and geometry are intentionally not collapsed into an arbitrary scalar.  */
static bool
focus_scalar (const render_parameters &rparam, uint64_t flags,
              const finetune_result &fit, coord_t *value)
{
  if (!value || !fit.success)
    return false;

  int active = 0;
  coord_t v = -1;
  if (flags & finetune_screen_blur)
    {
      active++;
      v = fit.screen_blur_radius;
    }
  if (flags & finetune_scanner_mtf_sigma)
    {
      active++;
      v = fit.scanner_mtf_sigma;
    }
  if (flags & finetune_scanner_mtf_defocus)
    {
      active++;
      v = rparam.sharpen.scanner_mtf.simulate_diffraction_p ()
              ? fit.scanner_mtf_defocus
              : fit.scanner_mtf_blur_diameter;
    }
  if (active != 1 || !std::isfinite ((double)v))
    return false;
  *value = v;
  return true;
}

static void
set_analysis_error (finetune_focus_analysis_result *result,
                    const char *message)
{
  result->success = false;
  result->err = message;
}

static coord_t
observed_color_norm (rgbdata color)
{
  const coord_t red = std::max ((luminosity_t)0, color.red);
  const coord_t green = std::max ((luminosity_t)0, color.green);
  const coord_t blue = std::max ((luminosity_t)0, color.blue);
  return std::sqrt (red * red + green * green + blue * blue);
}

/* Copy scalar shared focus/blur state from FIT to RPARAM.  Return false for
   the per-channel focus models, whose complete frozen transfer is not
   representable in render_parameters.  */
static bool
freeze_focus_from_fit (render_parameters *rparam, uint64_t flags,
                       const finetune_result &fit)
{
  if (!rparam || !fit.success)
    return false;
  if (flags & (finetune_screen_channel_blurs
               | finetune_scanner_mtf_channel_defocus))
    return false;
  if ((flags & finetune_screen_blur) && my_isfinite (fit.screen_blur_radius))
    rparam->screen_blur_radius = fit.screen_blur_radius;
  if ((flags & finetune_scanner_mtf_sigma)
      && my_isfinite (fit.scanner_mtf_sigma))
    rparam->sharpen.scanner_mtf.sigma = fit.scanner_mtf_sigma;
  if (flags & finetune_scanner_mtf_defocus)
    {
      if (rparam->sharpen.scanner_mtf.simulate_diffraction_p ())
        {
          if (!my_isfinite (fit.scanner_mtf_defocus))
            return false;
          rparam->sharpen.scanner_mtf.defocus = fit.scanner_mtf_defocus;
        }
      else
        {
          if (!my_isfinite (fit.scanner_mtf_blur_diameter))
            return false;
          rparam->sharpen.scanner_mtf.blur_diameter
              = fit.scanner_mtf_blur_diameter;
        }
    }
  if ((flags & finetune_strips) && my_isfinite (fit.red_strip_width)
      && my_isfinite (fit.green_strip_width))
    {
      rparam->red_strip_width = fit.red_strip_width;
      rparam->green_strip_width = fit.green_strip_width;
    }
  return true;
}

/* Return the process-screen carrier frequency in scan cycles per pixel.  The
   same definition is used by adaptive focus interpolation: SCR_NAMES gives
   cycles per screen coordinate and PIXEL_SIZE converts image pixels back to
   screen coordinates.  */
static bool
focus_screen_frequency (const scr_to_img_parameters &param,
                        const image_data &img, coord_t *frequency)
{
  if (!frequency || param.type == Random)
    return false;
  scr_to_img map;
  if (!map.set_parameters (param, img))
    return false;
  const coord_t pixel_size
      = map.pixel_size ({ 0, 0, img.width, img.height });
  const coord_t value = scr_names[param.type].frequency * pixel_size;
  if (!my_isfinite (value) || value <= 0)
    return false;
  *frequency = value;
  return true;
}

/* Return the scanner-MTF parameters exactly as the periodic-screen focus
   forward model interprets them.  RGB fitting uses one achromatic screen
   transfer at 550 nm; BW/IR fitting keeps the capture wavelength supplied by
   the caller.  */
static mtf_parameters
focus_mtf_parameters (const render_parameters &rparam, const image_data &img,
                      uint64_t flags)
{
  mtf_parameters mtf = rparam.sharpen.scanner_mtf;
  if (!(flags & finetune_bw) && img.has_rgb ())
    mtf.wavelength = 550;
  return mtf;
}

/* Return system MTF at FREQUENCY after applying the scalar scanner-MTF values
   exported by FIT.  */
static coord_t
focus_fit_mtf (const render_parameters &rparam, const image_data &img,
               uint64_t flags, const finetune_result &fit,
               coord_t frequency)
{
  if (!fit.success || !my_isfinite (frequency) || frequency <= 0)
    return -1;
  mtf_parameters mtf = focus_mtf_parameters (rparam, img, flags);
  if ((flags & finetune_scanner_mtf_sigma)
      && my_isfinite (fit.scanner_mtf_sigma)
      && fit.scanner_mtf_sigma >= 0)
    mtf.sigma = fit.scanner_mtf_sigma;
  if (flags & finetune_scanner_mtf_defocus)
    {
      if (mtf.simulate_diffraction_p ())
        {
          if (!my_isfinite (fit.scanner_mtf_defocus)
              || fit.scanner_mtf_defocus < 0)
            return -1;
          mtf.defocus = fit.scanner_mtf_defocus;
        }
      else
        {
          if (!my_isfinite (fit.scanner_mtf_blur_diameter)
              || fit.scanner_mtf_blur_diameter < 0)
            return -1;
          mtf.blur_diameter = fit.scanner_mtf_blur_diameter;
        }
    }
  const coord_t value = mtf.system_mtf (frequency);
  return my_isfinite (value) && value >= 0 ? value : -1;
}

/* Find the first nonnegative physical defocus whose system MTF at FREQUENCY
   reaches TARGET.  Defocus OTFs can acquire later lobes, so scan from perfect
   focus and bracket the first crossing rather than assuming global
   monotonicity.  */
static bool
first_defocus_for_screen_mtf (mtf_parameters mtf, coord_t frequency,
                              coord_t target, coord_t *defocus)
{
  if (!defocus || !mtf.simulate_diffraction_p () || !my_isfinite (frequency)
      || frequency <= 0 || !my_isfinite (target) || target <= 0
      || target > 1)
    return false;

  mtf.defocus = 0;
  const coord_t sharp = mtf.system_mtf (frequency);
  if (!my_isfinite (sharp) || sharp < target)
    return false;
  const coord_t tolerance
      = std::numeric_limits<coord_t>::epsilon () * 128;
  if (my_fabs (sharp - target) <= tolerance)
    {
      *defocus = 0;
      return true;
    }

  constexpr coord_t hard_max = 20;
  constexpr int samples = 4096;
  coord_t previous = 0;
  for (int i = 1; i <= samples; i++)
    {
      const coord_t t = (coord_t)i / samples;
      const coord_t current = hard_max * t * t;
      mtf.defocus = current;
      const coord_t value = mtf.system_mtf (frequency);
      if (!my_isfinite (value))
        return false;
      if (value <= target)
        {
          coord_t low = previous;
          coord_t high = current;
          for (int iteration = 0; iteration < 60; iteration++)
            {
              const coord_t middle = (low + high) * (coord_t)0.5;
              mtf.defocus = middle;
              const coord_t middle_mtf = mtf.system_mtf (frequency);
              if (!my_isfinite (middle_mtf))
                return false;
              if (middle_mtf > target)
                low = middle;
              else
                high = middle;
            }
          *defocus = high;
          return true;
        }
      previous = current;
    }
  return false;
}

/* Fit a coupled physical scanner MTF while making the process-screen carrier
   contrast the primary invariant.

   A free sigma+defocus pixel fit has a broad compensation valley and can move
   to a solution with a materially different carrier MTF.  Instead first fit a
   canonical cold sigma-only model.  Its system MTF at the known process-screen
   frequency is the effective contrast loss measured by the historical scan.
   Then evaluate several sigma/defocus decompositions on exactly that MTF
   contour, refitting local phase and image-layer intensities at each point.
   Pixel residual chooses the MTF shape along the contour, but cannot trade
   away the carrier contrast that colour recovery needs.  */
static finetune_result
fit_coupled_physical_focus_at_screen_mtf (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img, const std::vector<point_t> &locations,
    const std::vector<finetune_result> &starts,
    const finetune_parameters &fparams, coord_t screen_frequency,
    coord_t *target_screen_mtf, int *contour_evaluations,
    progress_info *progress)
{
  finetune_result failure;
  if (target_screen_mtf)
    *target_screen_mtf = -1;
  if (contour_evaluations)
    *contour_evaluations = 0;

  constexpr uint64_t coupled
      = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus;
  if ((fparams.flags & coupled) != coupled
      || !rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
    {
      failure.err = "coupled physical focus contour requested for non-physical model";
      return failure;
    }

  /* Use one canonical sharp physical baseline to measure carrier attenuation.
     This deliberately does not inherit a previous sigma/defocus calibration:
     an existing slanted-edge model is a useful decomposition prior, not the
     historical plate's carrier measurement.  */
  render_parameters probe_rparam = rparam;
  probe_rparam.sharpen.scanner_mtf.sigma = 0;
  probe_rparam.sharpen.scanner_mtf.defocus = 0;
  finetune_parameters probe_params = fparams;
  probe_params.flags
      &= ~(finetune_scanner_mtf_defocus | finetune_position);
  probe_params.flags |= finetune_scanner_mtf_sigma;
  probe_params.interpolate_scanner_mtf_defocus = false;
  finetune_result probe = finetune (probe_rparam, param, img, locations,
                                    &starts, probe_params, progress);
  if (!probe.success)
    {
      failure.err = "screen-frequency sigma probe failed";
      if (!probe.err.empty ())
        failure.err += ": " + probe.err;
      return failure;
    }
  if (!my_isfinite (probe.scanner_mtf_sigma)
      || probe.scanner_mtf_sigma < 0)
    {
      failure.err = "screen-frequency sigma probe returned invalid sigma";
      return failure;
    }

  mtf_parameters probe_mtf
      = focus_mtf_parameters (probe_rparam, img, fparams.flags);
  probe_mtf.sigma = probe.scanner_mtf_sigma;
  probe_mtf.defocus = 0;
  const coord_t target = probe_mtf.system_mtf (screen_frequency);
  if (!my_isfinite (target) || target <= 0 || target > 1)
    {
      failure.err = "screen-frequency sigma probe returned invalid MTF";
      return failure;
    }
  if (target_screen_mtf)
    *target_screen_mtf = target;

  /* The sigma-only endpoint already has the desired MTF and has optimized the
     same local phase/intensity nuisance variables, so it is a valid first
     contour candidate without another expensive fit.  */
  probe.scanner_mtf_defocus = 0;
  finetune_result best = probe;
  coord_t best_badness = probe.badness;
  int evaluations = 1;

  std::vector<coord_t> sigmas;
  static constexpr coord_t fractions[]
      = { 0, (coord_t)0.125, (coord_t)0.25, (coord_t)0.5,
          (coord_t)0.75, (coord_t)0.875, 1 };
  for (coord_t fraction : fractions)
    sigmas.push_back (probe.scanner_mtf_sigma * fraction);
  const coord_t current_sigma = rparam.sharpen.scanner_mtf.sigma;
  if (my_isfinite (current_sigma) && current_sigma >= 0
      && current_sigma <= probe.scanner_mtf_sigma)
    sigmas.push_back (current_sigma);
  std::sort (sigmas.begin (), sigmas.end ());
  sigmas.erase (std::unique (sigmas.begin (), sigmas.end (),
                            [] (coord_t a, coord_t b) {
                              return my_fabs (a - b) <= (coord_t)1e-7;
                            }),
                sigmas.end ());

  finetune_parameters fixed_params = fparams;
  fixed_params.flags &= ~coupled;
  /* Refit local phase on every contour point.  Otherwise an MTF candidate can
     lose merely because its phase was inherited from a differently blurred
     verification fit.  */
  fixed_params.flags |= finetune_position;
  fixed_params.interpolate_scanner_mtf_defocus = false;

  for (coord_t sigma : sigmas)
    {
      if (progress && progress->cancelled ())
        {
          failure.err = "cancelled";
          return failure;
        }
      if (my_fabs (sigma - probe.scanner_mtf_sigma) <= (coord_t)1e-7)
        continue;

      mtf_parameters mtf
          = focus_mtf_parameters (rparam, img, fparams.flags);
      mtf.sigma = sigma;
      coord_t defocus = 0;
      if (!first_defocus_for_screen_mtf (mtf, screen_frequency, target,
                                         &defocus))
        continue;

      render_parameters candidate_rparam = rparam;
      candidate_rparam.sharpen.scanner_mtf.sigma = sigma;
      candidate_rparam.sharpen.scanner_mtf.defocus = defocus;
      finetune_result candidate
          = finetune (candidate_rparam, param, img, locations, &starts,
                      fixed_params, progress);
      evaluations++;
      if (!candidate.success || !my_isfinite (candidate.badness)
          || candidate.badness < 0)
        continue;
      candidate.scanner_mtf_sigma = sigma;
      candidate.scanner_mtf_defocus = defocus;
      candidate.scanner_mtf_blur_diameter
          = candidate_rparam.sharpen.scanner_mtf.blur_diameter;
      if (!best.success || candidate.badness < best_badness)
        {
          best = std::move (candidate);
          best_badness = best.badness;
        }
    }

  if (contour_evaluations)
    *contour_evaluations = evaluations;
  return best;
}

/* Run one selected-area joint fit.  Coupled physical sigma+defocus uses the
   carrier-preserving contour estimator above; every other model keeps the
   ordinary FINETUNE path.  */
static finetune_result
fit_selected_focus_areas (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img, const std::vector<point_t> &locations,
    const std::vector<finetune_result> &starts,
    const finetune_parameters &fparams, coord_t screen_frequency,
    coord_t *target_screen_mtf, int *contour_evaluations,
    progress_info *progress)
{
  constexpr uint64_t coupled
      = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus;
  if ((fparams.flags & coupled) == coupled
      && rparam.sharpen.scanner_mtf.simulate_diffraction_p ()
      && my_isfinite (screen_frequency) && screen_frequency > 0)
    return fit_coupled_physical_focus_at_screen_mtf (
        rparam, param, img, locations, starts, fparams, screen_frequency,
        target_screen_mtf, contour_evaluations, progress);
  if (target_screen_mtf)
    *target_screen_mtf = -1;
  if (contour_evaluations)
    *contour_evaluations = 1;
  return finetune (rparam, param, img, locations, &starts, fparams, progress);
}

} // namespace

bool
finetune_find_focus_area_candidates_in_image (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img,
    const finetune_focus_area_image_search_parameters &parameters,
    std::vector<finetune_focus_area_candidate> *candidates,
    progress_info *progress, std::string *error)
{
  if (error)
    error->clear ();
  if (!candidates)
    return false;
  candidates->clear ();
  /* RENDER_INTERPOLATE reconstructs the image layer from either native RGB
     or a monochrome/IR additive-screen scan.  Flatness belongs after screen
     interpolation; do not pretend that a raw scalar scan is RGB.  */
  if (!img.has_rgb () && !img.has_grayscale_or_ir ())
    {
      if (error)
        *error = "automatic focus-area discovery requires image data";
      return false;
    }
  if (param.type == Random || parameters.max_analysis_dimension < 32
      || !my_isfinite (parameters.automatic_window_screen_periods)
      || parameters.automatic_window_screen_periods <= 0)
    {
      if (error)
        *error = "invalid focus-area image search parameters";
      return false;
    }

  const int_image_area crop = rparam.get_image_area (img.width, img.height);
  if (crop.width < 4 || crop.height < 4)
    {
      if (error)
        *error = "focus-area search region is too small";
      return false;
    }
  const coord_t step
      = std::max ((coord_t)1,
                  std::max ((coord_t)crop.width, (coord_t)crop.height)
                      / parameters.max_analysis_dimension);
  const int width = (int)std::floor (crop.width / step);
  const int height = (int)std::floor (crop.height / step);
  if (width < 2 || height < 2)
    {
      if (error)
        *error = "focus-area search region is too narrow after downscaling";
      return false;
    }

  render_parameters analysis_rparam = rparam;
  analysis_rparam.sharpen.mode = sharpen_parameters::none;
  analysis_rparam.sharpen.scanner_mtf_scale = 0;
  render_interpolate renderer (param, img, analysis_rparam, 65535);
  renderer.set_unadjusted ();
  if (!renderer.precompute_img_range (crop, progress))
    {
      if (error)
        *error = "failed to prepare interpolated focus-area image";
      return false;
    }
  std::vector<rgbdata> data ((size_t)width * height);
  if (!renderer.get_color_data (data.data (), { (coord_t)crop.x,
                                                (coord_t)crop.y },
                                width, height, step, progress))
    {
      if (error)
        *error = "failed to sample interpolated focus-area image";
      return false;
    }

  finetune_focus_area_search_parameters search = parameters.search;
  search.origin = { crop.x + step * (coord_t)0.5,
                    crop.y + step * (coord_t)0.5 };
  search.xstep = search.ystep = step;
  if (!search.window_width || !search.window_height)
    {
      const coord_t period
          = std::max (std::hypot (param.coordinate1.x, param.coordinate1.y),
                      std::hypot (param.coordinate2.x, param.coordinate2.y));
      if (!my_isfinite (period) || period <= 0)
        {
          if (error)
            *error = "invalid screen period for focus-area search";
          return false;
        }
      const int automatic_window
          = std::max (4, (int)std::ceil (
                            period * parameters.automatic_window_screen_periods
                            / step));
      if (!search.window_width)
        search.window_width = std::min (automatic_window, width);
      if (!search.window_height)
        search.window_height = std::min (automatic_window, height);
    }
  if (!finetune_find_focus_area_candidates (data.data (), width, height, width,
                                             search, candidates))
    {
      if (error)
        *error = "invalid flat-area detector parameters";
      return false;
    }
  return !progress || !progress->cancelled ();
}

bool
finetune_analyze_focus_areas (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img,
    const std::vector<finetune_focus_area_candidate> &candidates,
    const finetune_parameters &fparams,
    const finetune_focus_analysis_parameters &analysis_parameters,
    finetune_focus_analysis_result *result, progress_info *progress)
{
  if (!result)
    return false;
  *result = finetune_focus_analysis_result ();

  if (fparams.flags & (finetune_coordinates | finetune_guess_coordinates))
    {
      set_analysis_error (result,
                          "focus-area analysis requires explicit tile locations");
      return false;
    }

  if (!finetune_select_focus_areas (candidates, analysis_parameters.selection,
                                    &result->selected,
                                    &result->color_volume))
    {
      set_analysis_error (result, "no reliable diverse focus-area subset");
      return false;
    }

  if ((fparams.flags & finetune_uniform_image_layer)
      && result->selected.size () < 2)
    {
      set_analysis_error (result,
                          "uniform image-layer fitting requires at least two areas");
      return false;
    }
  if (analysis_parameters.leave_one_out
      && (fparams.flags & finetune_uniform_image_layer)
      && result->selected.size () < 3)
    {
      set_analysis_error (
          result,
          "leave-one-out uniform image-layer fitting requires at least three areas");
      return false;
    }

  std::vector<point_t> locations;
  std::vector<finetune_result> starts;
  locations.reserve (result->selected.size ());
  starts.reserve (result->selected.size ());
  for (size_t index : result->selected)
    {
      if (index >= candidates.size () || !candidates[index].fit.success)
        {
          set_analysis_error (result,
                              "selected focus area has no successful local fit");
          return false;
        }
      locations.push_back (candidates[index].center);
      starts.push_back (candidates[index].fit);
    }

  coord_t screen_frequency = -1;
  focus_screen_frequency (param, img, &screen_frequency);
  result->screen_frequency = screen_frequency;
  result->joint_fit = fit_selected_focus_areas (
      rparam, param, img, locations, starts, fparams, screen_frequency,
      &result->target_screen_mtf, &result->contour_evaluations, progress);
  if (!result->joint_fit.success)
    {
      result->err = "joint focus-area fit failed";
      if (!result->joint_fit.err.empty ())
        result->err += ": " + result->joint_fit.err;
      return false;
    }

  if (screen_frequency > 0)
    result->joint_screen_mtf
        = focus_fit_mtf (rparam, img, fparams.flags, result->joint_fit,
                         screen_frequency);

  if (!analysis_parameters.leave_one_out)
    {
      result->success = true;
      return true;
    }

  result->leave_one_out_fits.reserve (result->selected.size ());
  for (size_t omitted = 0; omitted < result->selected.size (); omitted++)
    {
      std::vector<point_t> subset_locations;
      std::vector<finetune_result> subset_starts;
      subset_locations.reserve (locations.size () - 1);
      subset_starts.reserve (starts.size () - 1);
      for (size_t i = 0; i < locations.size (); i++)
        if (i != omitted)
          {
            subset_locations.push_back (locations[i]);
            subset_starts.push_back (starts[i]);
          }

      coord_t subset_target_mtf = -1;
      int subset_evaluations = 0;
      finetune_result fit = fit_selected_focus_areas (
          rparam, param, img, subset_locations, subset_starts, fparams,
          screen_frequency, &subset_target_mtf, &subset_evaluations, progress);
      if (subset_target_mtf >= 0)
        result->leave_one_out_target_screen_mtf.push_back (subset_target_mtf);
      result->leave_one_out_fits.push_back (std::move (fit));
      if (!result->leave_one_out_fits.back ().success)
        {
          result->err = "leave-one-out focus-area fit failed for selected area "
                        + std::to_string (omitted);
          if (!result->leave_one_out_fits.back ().err.empty ())
            result->err += ": " + result->leave_one_out_fits.back ().err;
          return false;
        }
    }

  /* True held-out checking is defined for the RGB uniform-image-layer model.
     Each omitted tile is allowed its local phase and three transmissions, but
     not the shared focus transfer or screen-primary scanner responses.  */
  if (analysis_parameters.held_out
      && (fparams.flags & finetune_uniform_image_layer))
    {
      const uint64_t unsupported
          = finetune_screen_channel_blurs
            | finetune_scanner_mtf_channel_defocus | finetune_fog
            | finetune_emulsion_blur | finetune_sharpening;
      if (!(fparams.flags & unsupported))
        {
          result->held_out_fits.reserve (result->selected.size ());
          result->held_out_relative_badness.reserve (result->selected.size ());
          coord_t max_relative = 0;
          for (size_t omitted = 0; omitted < result->selected.size (); omitted++)
            {
              const finetune_result &reference
                  = result->leave_one_out_fits[omitted];
              render_parameters held_rparam = rparam;
              if (!freeze_focus_from_fit (&held_rparam, fparams.flags,
                                          reference))
                break;

              finetune_parameters held_fparams = fparams;
              held_fparams.interpolate_scanner_mtf_defocus = false;
              held_fparams.flags
                  &= ~(finetune_screen_blur | finetune_screen_channel_blurs
                       | finetune_scanner_mtf_sigma
                       | finetune_scanner_mtf_defocus
                       | finetune_scanner_mtf_channel_defocus | finetune_strips
                       | finetune_fog | finetune_emulsion_blur
                       | finetune_sharpening);
              held_fparams.flags
                  |= finetune_uniform_image_layer
                     | finetune_fixed_screen_colors | finetune_no_normalize
                     | finetune_no_data_collection;

              const size_t candidate_index = result->selected[omitted];
              finetune_result start = candidates[candidate_index].fit;
              start.screen_red = reference.screen_red;
              start.screen_green = reference.screen_green;
              start.screen_blue = reference.screen_blue;
              std::vector<finetune_result> held_starts = { start };
              finetune_result held = finetune (
                  held_rparam, param, img,
                  { candidates[candidate_index].center }, &held_starts,
                  held_fparams, progress);
              result->held_out_fits.push_back (std::move (held));
              if (!result->held_out_fits.back ().success)
                {
                  result->err
                      = "held-out focus-area evaluation failed for selected area "
                        + std::to_string (omitted);
                  if (!result->held_out_fits.back ().err.empty ())
                    result->err += ": " + result->held_out_fits.back ().err;
                  return false;
                }
              const coord_t norm
                  = observed_color_norm (candidates[candidate_index].mean_color);
              const coord_t relative
                  = norm > 0 ? result->held_out_fits.back ().badness / norm
                             : std::numeric_limits<coord_t>::max ();
              result->held_out_relative_badness.push_back (relative);
              max_relative = std::max (max_relative, relative);
            }
          if (result->held_out_fits.size () == result->selected.size ())
            result->held_out_max_relative_badness = max_relative;
          else
            {
              result->held_out_fits.clear ();
              result->held_out_relative_badness.clear ();
            }
        }
    }

  if (screen_frequency > 0 && result->joint_screen_mtf >= 0
      && !result->leave_one_out_fits.empty ())
    {
      coord_t min_mtf = std::numeric_limits<coord_t>::max ();
      coord_t max_mtf = -std::numeric_limits<coord_t>::max ();
      coord_t max_delta = 0;
      bool valid = true;
      for (const finetune_result &fit : result->leave_one_out_fits)
        {
          const coord_t value
              = focus_fit_mtf (rparam, img, fparams.flags, fit,
                               screen_frequency);
          if (value < 0)
            {
              valid = false;
              break;
            }
          min_mtf = std::min (min_mtf, value);
          max_mtf = std::max (max_mtf, value);
          max_delta = std::max (
              max_delta, (coord_t)my_fabs (value - result->joint_screen_mtf));
        }
      if (valid)
        {
          result->leave_one_out_screen_mtf_span = max_mtf - min_mtf;
          result->leave_one_out_screen_mtf_max_delta = max_delta;
        }
    }

  coord_t joint_focus;
  if (focus_scalar (rparam, fparams.flags, result->joint_fit, &joint_focus))
    {
      coord_t min_focus = std::numeric_limits<coord_t>::max ();
      coord_t max_focus = -std::numeric_limits<coord_t>::max ();
      coord_t max_delta = 0;
      bool scalar = true;
      for (const finetune_result &fit : result->leave_one_out_fits)
        {
          coord_t value;
          if (!focus_scalar (rparam, fparams.flags, fit, &value))
            {
              scalar = false;
              break;
            }
          min_focus = std::min (min_focus, value);
          max_focus = std::max (max_focus, value);
          max_delta
              = std::max (max_delta,
                          (coord_t)std::fabs (value - joint_focus));
        }
      if (scalar && !result->leave_one_out_fits.empty ())
        {
          result->leave_one_out_focus_span = max_focus - min_focus;
          result->leave_one_out_focus_max_delta = max_delta;
        }
    }

  result->success = true;
  return true;
}

} // namespace colorscreen
