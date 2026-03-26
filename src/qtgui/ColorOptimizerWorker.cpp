#include "ColorOptimizerWorker.h"
#include <QDebug>

ColorOptimizerWorker::ColorOptimizerWorker(
    std::shared_ptr<colorscreen::image_data> scan, QObject *parent)
    : WorkerBase(scan, parent) {}

void ColorOptimizerWorker::optimize(
    int reqId,
    colorscreen::scr_to_img_parameters scrParams,
    colorscreen::render_parameters rparams,
    std::vector<colorscreen::point_t> spots,
    std::shared_ptr<colorscreen::progress_info> progress)
{
  if (!m_scan || spots.empty()) {
    emit finished(reqId, rparams, {}, false, false);
    return;
  }

  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::color_match> report;

  try {
    colorscreen::sub_task task(progress.get());

    success = colorscreen::optimize_color_model_colors(
        &scrParams, *m_scan, rparams, spots, &report, progress.get());

    if (progress && progress->cancelled()) {
      success = false;
      cancelled = true;
    }
  } catch (const std::exception &e) {
    qWarning() << "Color optimizer failed with exception:" << e.what();
    success = false;
  } catch (...) {
    qWarning() << "Color optimizer failed with unknown exception";
    success = false;
  }

  if (progress && progress->cancelled())
    cancelled = true;

  emit finished(reqId, rparams, report, success, cancelled);
}
