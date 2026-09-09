#include "FinetuneWorker.h"

#include "../libcolorscreen/include/imagedata.h"

/** Find the registration points produced by one selected-area finetune. */
FinetuneAreaResult FinetuneWorker::findPoints(
    colorscreen::solver_parameters solverParams,
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::int_image_area area,
    colorscreen::finetune_area_parameters fparams,
    colorscreen::progress_info *progress) {
  FinetuneAreaResult result;
  if (!scan)
    return result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }

  const std::size_t initialPointCount = solverParams.points.size();
  const bool success = colorscreen::finetune_area(
      &solverParams, rparams, scrToImg, *scan, area, fparams, progress);

  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  if (result.cancelled)
    return result;

  result.success = success;
  if (success && solverParams.points.size() > initialPointCount) {
    const auto first = solverParams.points.begin() + initialPointCount;
    result.points.assign(first, solverParams.points.end());
  }
  return result;
}
