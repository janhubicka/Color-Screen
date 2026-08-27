#ifndef COLORSCREEN_FOCUS_ANALYSIS_H
#define COLORSCREEN_FOCUS_ANALYSIS_H

#include "finetune.h"
#include <string>
#include <vector>

namespace colorscreen
{

/* Controls the expensive stage after solid-colour candidates have been fitted
   independently.  Selection is performed first; the selected tiles are then
   fitted jointly with FINETUNE_PARAMETERS.  */
struct finetune_focus_analysis_parameters
{
  finetune_focus_area_selection_parameters selection;
  /* Refit the selected set once for every omitted area.  This measures how
     much the shared focus/blur estimate depends on any one candidate.  */
  bool leave_one_out = true;
  /* Evaluate each omitted RGB tile with the shared focus and screen-primary
     responses frozen from the corresponding leave-one-out fit.  Only the
     omitted tile's local phase and three uniform image-layer transmissions
     are allowed to move.  This is a genuine held-out model-mismatch check,
     unlike LEAVE_ONE_OUT which reoptimizes on the retained tiles.  */
  bool held_out = true;
};

/* Controls construction of the linear interpolated analysis image used by
   automatic solid-area discovery.  SEARCH controls the flat-window detector.
   A zero SEARCH.WINDOW_WIDTH/HEIGHT derives a window from eight screen
   periods.  MAX_ANALYSIS_DIMENSION caps the longer sampled image dimension. */
struct finetune_focus_area_image_search_parameters
{
  finetune_focus_area_search_parameters search;
  int max_analysis_dimension = 768;
  coord_t automatic_window_screen_periods = 8;
};

/* Result of joint focus analysis.  SELECTED contains indexes into the caller's
   candidate vector.  LEAVE_ONE_OUT_FITS follows SELECTED order: entry I is the
   fit with SELECTED[I] omitted.  */
struct finetune_focus_analysis_result
{
  bool success = false;
  std::vector<size_t> selected;
  coord_t color_volume = 0;
  finetune_result joint_fit;
  /* Process-screen carrier frequency and system MTF represented by JOINT_FIT.
     This is the transfer value directly relevant to restoring screen contrast. */
  coord_t screen_frequency = -1;
  coord_t joint_screen_mtf = -1;
  /* Coupled physical fit start that won by final joint badness: 0 loaded
     calibration, 1 sigma-only scalar seed, 2 defocus-only scalar seed.  */
  int joint_seed_kind = -1;
  std::vector<finetune_result> leave_one_out_fits;
  std::vector<coord_t> leave_one_out_screen_mtf;
  /* Entry I evaluates SELECTED[I] after the shared model was learned without
     that tile.  RELATIVE_BADNESS divides the raw objective by the observed
     mean-colour norm, matching the candidate quality normalization.  */
  std::vector<finetune_result> held_out_fits;
  std::vector<coord_t> held_out_relative_badness;
  coord_t held_out_max_relative_badness = -1;

  /* Stability of the active scalar focus/blur parameter.  These fields stay
     negative when FINETUNE_PARAMETERS does not optimize exactly one supported
     scalar focus/blur parameter or when leave-one-out checking is disabled.
     SPAN is max(leave-one-out)-min(leave-one-out); MAX_DELTA is the largest
     absolute difference from JOINT_FIT.  Units are those of the active model:
     scan pixels for screen blur / residual scanner sigma / empirical compact
     blur diameter and millimetres for physical scanner-MTF defocus.  */
  coord_t leave_one_out_focus_span = -1;
  coord_t leave_one_out_focus_max_delta = -1;
  std::string err;
};

/* Build a downscaled, unadjusted interpolated RGB image from IMG and run the
   cheap solid-area detector on it.  Returned coordinates are scan/image
   coordinates suitable for FINETUNE.  Sharpening and output adjustments do
   not participate in this analysis image.  */
nodiscard_attr DLL_PUBLIC bool
finetune_find_focus_area_candidates_in_image (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img,
    const finetune_focus_area_image_search_parameters &parameters,
    std::vector<finetune_focus_area_candidate> *candidates,
    progress_info *progress, std::string *error = nullptr);

/* Select independently verified solid-colour candidates, fit their shared
   focus/blur model jointly, and optionally perform leave-one-out stability
   checking.  CANDIDATE.FIT supplies the local registration start for each
   selected tile; callers should therefore run the cheap one-tile verification
   stage before this function.

   FINETUNE_PARAMETERS describes the joint model.  In particular RGB focus
   analysis over differently coloured tiles normally includes
   FINETUNE_UNIFORM_IMAGE_LAYER; BW/IR callers keep using their existing
   uniform-tile model and should not add that RGB-only flag.

   The function does not impose a universal pass/fail threshold on focus
   stability because the optimized parameters have different physical units.
   It returns the raw leave-one-out diagnostics for the caller to interpret.  */
nodiscard_attr DLL_PUBLIC bool
finetune_analyze_focus_areas (
    const render_parameters &rparam, const scr_to_img_parameters &param,
    const image_data &img,
    const std::vector<finetune_focus_area_candidate> &candidates,
    const finetune_parameters &finetune_parameters,
    const finetune_focus_analysis_parameters &analysis_parameters,
    finetune_focus_analysis_result *result, progress_info *progress);

} // namespace colorscreen

#endif
