#include "include/focus-analysis.h"
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

} // namespace

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

  result->joint_fit
      = finetune (rparam, param, img, locations, &starts, fparams, progress);
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

      finetune_result fit = finetune (rparam, param, img, subset_locations,
                                     &subset_starts, fparams, progress);
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
