#include "CoordinateOptimizationWorker.h"
#include "../libcolorscreen/include/finetune.h"
#include <QDebug>

CoordinateOptimizationWorker::CoordinateOptimizationWorker(
    std::shared_ptr<colorscreen::image_data> scan, QObject *parent)
    : WorkerBase(scan, parent) {}

void CoordinateOptimizationWorker::autodetect(
    int reqId, colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::progress_info> progress) {
  if (!m_scan) {
    emit autodetectFinished(reqId, params, progress, false, false);
    return;
  }

  bool success = false;
  bool cancelled = false;

  try {
    colorscreen::sub_task task(progress.get());
    success = colorscreen::autodetect_coordinates(*m_scan, params, rparams,
                                                  progress.get());
    if (progress && progress->cancelled()) {
      success = false;
      cancelled = true;
    }
  } catch (const std::exception &e) {
    qWarning() << "Autodetect coordinates failed with exception:" << e.what();
    success = false;
  } catch (...) {
    qWarning() << "Autodetect coordinates failed with unknown exception";
    success = false;
  }

  emit autodetectFinished(reqId, params, progress, success, cancelled);
}

void CoordinateOptimizationWorker::optimize(
    int reqId, colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::progress_info> progress) {
  if (!m_scan) {
    emit optimizeFinished(reqId, colorscreen::finetune_result(), progress, false,
                          false);
    return;
  }

  colorscreen::finetune_result result;
  bool success = false;
  bool cancelled = false;

  try {
    colorscreen::sub_task task(progress.get());
    colorscreen::finetune_parameters fparams;
    fparams.flags = colorscreen::finetune_position | colorscreen::finetune_verbose |
                    colorscreen::finetune_coordinates | colorscreen::finetune_bw |
                    colorscreen::finetune_use_strip_widths |
                    colorscreen::finetune_produce_images;

    result = colorscreen::finetune(rparams, params, *m_scan, {}, nullptr, fparams,
                                   progress.get());
    success = result.success;

    if (progress && progress->cancelled()) {
      success = false;
      cancelled = true;
    }
  } catch (const std::exception &e) {
    qWarning() << "Optimize coordinates failed with exception:" << e.what();
    success = false;
  } catch (...) {
    qWarning() << "Optimize coordinates failed with unknown exception";
    success = false;
  }

  emit optimizeFinished(reqId, result, progress, success, cancelled);
}
