#include "HistogramWorker.h"

#include <QDebug>

#include <exception>

HistogramWorker::HistogramWorker(std::shared_ptr<colorscreen::image_data> scan, QObject *parent)
    : WorkerBase(scan, parent) {}

void HistogramWorker::compute(int reqId,
                             colorscreen::render_parameters params,
                             int steps,
                             double minX,
                             double maxX,
                             colorscreen::hd_axis_type axisType,
                             std::shared_ptr<colorscreen::progress_info> progress) {
  if (!m_scan) {
    emit finished(reqId, {}, minX, maxX, false);
    return;
  }

  if (progress) {
    progress->set_task("Computing histogram", 1);
  }

  std::vector<uint64_t> hist;
  bool success = false;
  try {
    hist = colorscreen::hd_x_histogram(params, *m_scan, steps, minX, maxX,
                                       axisType, progress.get());
    // hd_x_histogram returns early with an empty result when cancelled.
    success = (!hist.empty() || steps == 0) &&
              (!progress || !progress->cancelled());
  } catch (const std::exception &exception) {
    qWarning() << "Histogram worker failed with exception:" << exception.what();
  } catch (...) {
    qWarning() << "Histogram worker failed with unknown exception";
  }

  emit finished(reqId, hist, minX, maxX, success);
}
