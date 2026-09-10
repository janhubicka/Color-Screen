#include "CoordinateOptimizationWorker.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QDebug>
#include <exception>

CoordinateAutodetectionResult CoordinateOptimizationWorker::autodetect(
    colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  CoordinateAutodetectionResult result;
  result.coordinates = params;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan)
    return result;

  try {
    colorscreen::sub_task task(progress);
    result.success = colorscreen::autodetect_coordinates(
        *scan, result.coordinates, rparams, progress);
  } catch (const std::exception &e) {
    qWarning() << "Autodetect coordinates failed with exception:" << e.what();
  } catch (...) {
    qWarning() << "Autodetect coordinates failed with unknown exception";
  }

  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}

CoordinateOptimizationResult CoordinateOptimizationWorker::optimize(
    colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  CoordinateOptimizationResult result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan) {
    result.finetune.err = "No scan available.";
    return result;
  }

  try {
    colorscreen::sub_task task(progress);
    colorscreen::finetune_parameters fparams;
    fparams.flags = colorscreen::finetune_position | colorscreen::finetune_verbose |
                    colorscreen::finetune_coordinates | colorscreen::finetune_bw |
                    colorscreen::finetune_use_strip_widths |
                    colorscreen::finetune_produce_images;

    // RANGE is the half-extent in periodic screen coordinates: keep the
    // existing local 2x2-screen-period sample rather than the larger default.
    fparams.range = 1;

    result.finetune = colorscreen::finetune(
        rparams, params, *scan, {}, nullptr, fparams, progress);
    result.success = result.finetune.success;
  } catch (const std::exception &e) {
    result.finetune.err = e.what();
    qWarning() << "Optimize coordinates failed with exception:" << e.what();
  } catch (...) {
    result.finetune.err = "Unknown coordinate optimization exception.";
    qWarning() << "Optimize coordinates failed with unknown exception";
  }

  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}
