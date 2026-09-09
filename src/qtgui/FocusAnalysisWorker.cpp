#include "FocusAnalysisWorker.h"

#include "../libcolorscreen/include/imagedata.h"

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
