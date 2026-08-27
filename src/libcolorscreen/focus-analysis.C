#include "include/focus-analysis.h"
#include "finetune-int.h"
#include "include/scr-to-img.h"
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

/* Return the process-screen carrier frequency in cycles per image pixel.
   This matches adaptive scanner-blur analysis and therefore the frequency
   whose contrast matters most to additive-screen colour recovery.  */
static coord_t
process_screen_frequency (const scr_to_img_parameters &param,
                          const image_data &img)
{
  if (param.type < 0 || param.type >= max_scr_type)
    return -1;
  scr_to_img map;
  if (!map.set_parameters (param, img))
    return -1;
  const coord_t pixel_size
      = map.pixel_size ({ 0, 0, img.width, img.height });
  const coord_t frequency = scr_names[param.type].frequency * pixel_size;
  return my_isfinite (frequency) && frequency > 0 ? frequency : (coord_t)-1;
}

/* Evaluate the scanner system MTF represented by FIT at FREQUENCY.  The RGB
   periodic-screen finetuner uses the same achromatic 550 nm convention.  */
static coord_t
fit_system_mtf (const render_parameters &rparam, const image_data &img,
                uint64_t flags, const finetune_result &fit,
                coord_t frequency)
{
  if (!fit.success || !(frequency > 0))
    return -1;
  mtf_parameters mtf
      = (flags & finetune_bw)
            ? rparam.get_image_layer_sharpen_parameters (&img).scanner_mtf
            : rparam.sharpen.scanner_mtf;
  if ((flags & finetune_scanner_mtf_sigma)
      && my_isfinite (fit.scanner_mtf_sigma))
    mtf.sigma = fit.scanner_mtf_sigma;
  if (flags & finetune_scanner_mtf_defocus)
    {
      if (mtf.simulate_diffraction_p ())
        {
          if (!my_isfinite (fit.scanner_mtf_defocus))
            return -1;
          mtf.defocus = fit.scanner_mtf_defocus;
        }
      else
        {
          if (!my_isfinite (fit.scanner_mtf_blur_diameter))
            return -1;
          mtf.blur_diameter = fit.scanner_mtf_blur_diameter;
        }
    }
  if (!(flags & finetune_bw) && img.has_rgb ())
    mtf.wavelength = 550;
  const coord_t value = mtf.system_mtf (frequency);
  return my_isfinite (value) ? value : (coord_t)-1;
}

/* Run one shared all-area fit from RPARAM and keep the better successful
   result.  */
static void
consider_joint_fit (const render_parameters &rparam,
                    const scr_to_img_parameters &param, const image_data &img,
                    const std::vector<point_t> &locations,
                    const std::vector<finetune_result> &starts,
                    const finetune_parameters &fparams, progress_info *progress,
                    finetune_result *best, int *best_seed_kind,
                    int seed_kind)
{
  finetune_result fit
      = finetune (rparam, param, img, locations, &starts, fparams, progress);
  if (!fit.success || !my_isfinite (fit.badness))
    return;
  if (!best->success || fit.badness < best->badness)
    {
      *best = std::move (fit);
      if (best_seed_kind)
        *best_seed_kind = seed_kind;
    }
}

/* Coupled physical sigma/defocus fitting is poorly conditioned from a single
   start, especially at the exact (0,0) boundary.  Use the cheap one-dimensional
   sigma-only and defocus-only fits to locate useful basins, then release both
   coordinates and select by the actual all-area pixel residual.  The loaded
   calibration remains a competing start, so a good slanted-edge model can be
   adjusted rather than discarded.  */
static finetune_result
multistart_joint_focus_fit (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img, const std::vector<point_t> &locations,
    const std::vector<finetune_result> &starts, const finetune_parameters &fparams,
    progress_info *progress, int *best_seed_kind)
{
  constexpr uint64_t coupled
      = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus;
  if ((fparams.flags & coupled) != coupled
      || !rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
    {
      if (best_seed_kind)
        *best_seed_kind = 0;
      return finetune (rparam, param, img, locations, &starts, fparams, progress);
    }

  finetune_result best;
  if (best_seed_kind)
    *best_seed_kind = -1;
  consider_joint_fit (rparam, param, img, locations, starts, fparams, progress,
                      &best, best_seed_kind, 0);

  struct scalar_seed
  {
    bool success = false;
    coord_t badness = std::numeric_limits<coord_t>::max ();
    render_parameters rparam;
    finetune_result fit;
    int kind = 0;
  };
  scalar_seed scalar[2];
  const uint64_t scalar_flags[2]
      = { finetune_scanner_mtf_sigma, finetune_scanner_mtf_defocus };
  auto consider_scalar_seed = [&] (int which,
                                           const render_parameters &seed) {
    finetune_parameters one = fparams;
    one.flags &= ~coupled;
    one.flags |= scalar_flags[which];
    finetune_result fit
        = finetune (seed, param, img, locations, &starts, one, progress);
    if (!fit.success || !my_isfinite (fit.badness))
      return;
    render_parameters frozen = seed;
    if (!freeze_focus_from_fit (&frozen, one.flags, fit))
      return;
    if (!scalar[which].success || fit.badness < scalar[which].badness)
      {
        scalar[which].success = true;
        scalar[which].badness = fit.badness;
        scalar[which].rparam = std::move (frozen);
        scalar[which].fit = std::move (fit);
        scalar[which].kind = which + 1;
      }
  };
  for (int which = 0; which < 2; which++)
    consider_scalar_seed (which, rparam);

  /* Physical defocus is even at exact focus, so a simplex initialized
     at zero has no first-order direction.  Seed nonzero defocus starts
     from the same optics-derived useful range as adaptive focusing.  */
  constexpr coord_t cold_epsilon = (coord_t)1e-8;
  if (my_isfinite (rparam.sharpen.scanner_mtf.defocus)
      && rparam.sharpen.scanner_mtf.defocus >= 0
      && rparam.sharpen.scanner_mtf.defocus <= cold_epsilon)
    {
      mtf_parameters focus_mtf
          = (fparams.flags & finetune_bw)
                ? rparam.get_image_layer_sharpen_parameters (&img).scanner_mtf
                : rparam.sharpen.scanner_mtf;
      if (!(fparams.flags & finetune_bw) && img.has_rgb ())
        focus_mtf.wavelength = 550;
      const coord_t frequency = process_screen_frequency (param, img);
      coord_t useful_limit = 0;
      if (frequency > 0
          && finetune_useful_defocus_limit (
              focus_mtf, frequency, (coord_t)0.05, (coord_t)20,
              &useful_limit)
          && useful_limit > cold_epsilon)
        {
          static constexpr coord_t fractions[]
              = { (coord_t)0.125, (coord_t)0.25, (coord_t)0.5 };
          for (coord_t fraction : fractions)
            {
              render_parameters seed = rparam;
              seed.sharpen.scanner_mtf.defocus
                  = useful_limit * fraction;
              consider_scalar_seed (1, seed);
            }
        }
    }

  /* A scalar prefit is already a fully evaluated feasible point of the
     coupled parameter space.  Retain it as a candidate before asking
     the released simplex to improve it; a failed release must not
     discard a better seed solution.  */
  for (int which = 0; which < 2; which++)
    if (scalar[which].success
        && (!best.success || scalar[which].fit.badness < best.badness))
      {
        best = scalar[which].fit;
        if (best_seed_kind)
          *best_seed_kind = scalar[which].kind;
      }

  /* Release both coordinates from the better scalar basin first.  Usually one
     of these already captures the process-screen attenuation and this avoids
     paying for two equally poor coupled searches.  If it does not beat the
     loaded start, try the other basin as a fallback.  */
  int first = 0, second = 1;
  if ((!scalar[first].success && scalar[second].success)
      || (scalar[first].success && scalar[second].success
          && scalar[second].badness < scalar[first].badness))
    std::swap (first, second);
  if (scalar[first].success)
    consider_joint_fit (scalar[first].rparam, param, img, locations, starts,
                        fparams, progress, &best, best_seed_kind,
                        scalar[first].kind);
  if (scalar[second].success)
    consider_joint_fit (scalar[second].rparam, param, img, locations, starts,
                        fparams, progress, &best, best_seed_kind,
                        scalar[second].kind);

  /* A cold defocus coordinate cannot leave zero by local first-order
     exploration.  First obtain the sigma-only explanation, then explore
     nonzero physical-defocus starts with that sigma held fixed.  This follows
     the compensation valley from an identifiable axis into its interior.  */
  constexpr coord_t cold_focus_epsilon = (coord_t)1e-8;
  if (scalar[0].success
      && my_isfinite (rparam.sharpen.scanner_mtf.defocus)
      && rparam.sharpen.scanner_mtf.defocus >= 0
      && rparam.sharpen.scanner_mtf.defocus <= cold_focus_epsilon)
    {
      mtf_parameters stage_mtf
          = (fparams.flags & finetune_bw)
                ? scalar[0].rparam
                      .get_image_layer_sharpen_parameters (&img)
                      .scanner_mtf
                : scalar[0].rparam.sharpen.scanner_mtf;
      if (!(fparams.flags & finetune_bw) && img.has_rgb ())
        stage_mtf.wavelength = 550;
      const coord_t frequency = process_screen_frequency (param, img);
      coord_t useful_limit = 0;
      if (frequency > 0
          && finetune_useful_defocus_limit (
              stage_mtf, frequency, (coord_t)0.05, (coord_t)20,
              &useful_limit)
          && useful_limit > cold_focus_epsilon)
        {
          struct staged_seed
          {
            bool success = false;
            coord_t badness = std::numeric_limits<coord_t>::max ();
            render_parameters rparam;
            finetune_result fit;
          } staged;

          finetune_parameters defocus_stage = fparams;
          defocus_stage.flags &= ~coupled;
          defocus_stage.flags |= finetune_scanner_mtf_defocus;
          static constexpr coord_t fractions[]
              = { (coord_t)0.125, (coord_t)0.25, (coord_t)0.5,
                  (coord_t)0.75 };
          for (coord_t fraction : fractions)
            {
              render_parameters seed = scalar[0].rparam;
              seed.sharpen.scanner_mtf.defocus = useful_limit * fraction;
              finetune_result fit
                  = finetune (seed, param, img, locations, &starts,
                              defocus_stage, progress);
              if (!fit.success || !my_isfinite (fit.badness))
                continue;
              render_parameters frozen = seed;
              if (!freeze_focus_from_fit (&frozen, defocus_stage.flags, fit))
                continue;
              if (!staged.success || fit.badness < staged.badness)
                {
                  staged.success = true;
                  staged.badness = fit.badness;
                  staged.rparam = std::move (frozen);
                  staged.fit = std::move (fit);
                }
            }

          if (staged.success)
            {
              if (!best.success || staged.fit.badness < best.badness)
                {
                  best = staged.fit;
                  if (best_seed_kind)
                    *best_seed_kind = scalar[0].kind;
                }

              /* Polish sigma with the recovered nonzero defocus fixed before
                 finally releasing both physical transfer coordinates.  */
              finetune_parameters sigma_stage = fparams;
              sigma_stage.flags &= ~coupled;
              sigma_stage.flags |= finetune_scanner_mtf_sigma;
              sigma_stage.interpolate_scanner_mtf_defocus = false;
              finetune_result fit
                  = finetune (staged.rparam, param, img, locations, &starts,
                              sigma_stage, progress);
              if (fit.success && my_isfinite (fit.badness))
                {
                  render_parameters polished = staged.rparam;
                  if (freeze_focus_from_fit (&polished, sigma_stage.flags, fit))
                    {
                      fit.scanner_mtf_defocus
                          = polished.sharpen.scanner_mtf.defocus;
                      if (!best.success || fit.badness < best.badness)
                        {
                          best = fit;
                          if (best_seed_kind)
                            *best_seed_kind = scalar[0].kind;
                        }
                      consider_joint_fit (
                          polished, param, img, locations, starts, fparams,
                          progress, &best, best_seed_kind, scalar[0].kind);
                    }
                }
            }
        }
    }
  return best;
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

  result->screen_frequency = process_screen_frequency (param, img);
  result->joint_fit = multistart_joint_focus_fit (
      rparam, param, img, locations, starts, fparams, progress,
      &result->joint_seed_kind);
  result->joint_screen_mtf = fit_system_mtf (
      rparam, img, fparams.flags, result->joint_fit, result->screen_frequency);
  if (!result->joint_fit.success)
    {
      result->err = "joint focus-area fit failed";
      if (!result->joint_fit.err.empty ())
        result->err += ": " + result->joint_fit.err;
      return false;
    }

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

      render_parameters subset_rparam = rparam;
      freeze_focus_from_fit (&subset_rparam, fparams.flags, result->joint_fit);
      finetune_result fit = finetune (subset_rparam, param, img, subset_locations,
                                     &subset_starts, fparams, progress);
      result->leave_one_out_screen_mtf.push_back (fit_system_mtf (
          rparam, img, fparams.flags, fit, result->screen_frequency));
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
