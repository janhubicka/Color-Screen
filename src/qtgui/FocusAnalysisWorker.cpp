#include "FocusAnalysisWorker.h"

#include "../libcolorscreen/include/imagedata.h"

#include <exception>
#include <utility>
#include <vector>

/** Analyze one selected image point for scanner/process focus parameters. */
FocusAnalysisResult FocusAnalysisWorker::analyze(
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::point_t point, colorscreen::finetune_parameters fparam,
    colorscreen::progress_info *progress) {
  FocusAnalysisResult result;
  if (!scan)
    return result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }

  const std::vector<colorscreen::point_t> points = {point};
  result.finetune = colorscreen::finetune(rparams, scrToImg, *scan, points,
                                          nullptr, fparam, progress);
  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = !result.cancelled && result.finetune.success;
  return result;
}

/** Discover at most 24 candidates using the existing unadjusted reconstruction. */
FocusAreaFindResult FocusAnalysisWorker::findAreas(
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  FocusAreaFindResult result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan) {
    result.error = "No scan available.";
    return result;
  }

  try {
    colorscreen::finetune_focus_area_image_search_parameters search;
    search.search.max_candidates = 24;
    result.success = colorscreen::finetune_find_focus_area_candidates_in_image(
        rparams, scrToImg, *scan, search, &result.candidates, progress,
        &result.error);
  } catch (const std::exception &e) {
    result.error = e.what();
  } catch (...) {
    result.error = "Unknown focus-area search exception.";
  }
  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}

/** Verify local fits before colour-diverse joint/leave-one-out/held-out fitting. */
FocusAreaAnalyzeResult FocusAnalysisWorker::analyzeAreas(
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    std::vector<colorscreen::finetune_focus_area_candidate> candidates,
    uint64_t flags, bool useMonochrome,
    colorscreen::progress_info *progress) {
  FocusAreaAnalyzeResult result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan) {
    result.error = "No scan available.";
    return result;
  }
  result.candidates = std::move(candidates);

  try {
    colorscreen::finetune_parameters local;
    local.range = 4;
    local.ignore_outliers = 0;
    /* Candidate verification determines local phase/colour only.
       Scanner MTF is shared and is fitted after area selection. */
    local.flags = colorscreen::finetune_position;
    if (useMonochrome)
      local.flags |= colorscreen::finetune_bw
          | colorscreen::finetune_no_normalize
          | colorscreen::finetune_no_data_collection;
    for (auto &candidate : result.candidates) {
      if (progress && (progress->pool_cancel() || progress->cancelled())) {
        result.cancelled = true;
        return result;
      }
      candidate.fit = colorscreen::finetune(
          rparams, scrToImg, *scan, {candidate.center}, nullptr, local,
          progress);
    }

    if (progress && (progress->pool_cancel() || progress->cancelled())) {
      result.cancelled = true;
      return result;
    }

    colorscreen::finetune_parameters joint = local;
    joint.flags |= flags | colorscreen::finetune_no_normalize
        | colorscreen::finetune_no_data_collection;
    if (!useMonochrome)
      joint.flags |= colorscreen::finetune_uniform_image_layer;
    colorscreen::finetune_focus_analysis_parameters analysisParameters;
    analysisParameters.selection.min_areas = 3;
    analysisParameters.selection.max_areas = 8;
    /* Full RGB rank is required to learn shared RGB screen-primary
       responses, but it is not an identifiability condition for BW: each
       BW area has its own three primary weights and only blur is shared.
       Keep D-optimal ordering, but do not reject the best BW subset solely
       because its cross-area colour Gram matrix is rank deficient. */
    if (useMonochrome)
      analysisParameters.selection.minimum_color_volume = 0;
    analysisParameters.leave_one_out = true;
    /* Frozen RGB-primary held-out validation belongs to the RGB uniform
       image-layer model.  BW still gets full leave-one-out stability. */
    analysisParameters.held_out = !useMonochrome;
    result.success = colorscreen::finetune_analyze_focus_areas(
        rparams, scrToImg, *scan, result.candidates, joint,
        analysisParameters, &result.analysis, progress);
    result.cancelled = progress &&
        (progress->pool_cancel() || progress->cancelled());
    if (!result.success && !result.cancelled)
      result.error = result.analysis.err.empty()
          ? "focus-area joint analysis failed"
          : result.analysis.err;
  } catch (const std::exception &e) {
    result.error = e.what();
    result.success = false;
  } catch (...) {
    result.error = "Unknown focus-area analysis exception.";
    result.success = false;
  }
  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}
