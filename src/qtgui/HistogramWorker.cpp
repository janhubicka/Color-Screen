#include "HistogramWorker.h"

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

  std::vector<uint64_t> hist = colorscreen::hd_x_histogram(params, *m_scan, steps, minX, maxX, axisType, progress.get());

  // Check if we were cancelled (progress_info would know, but hd_x_histogram returns early if cancelled)
  bool success = !hist.empty() || steps == 0;
  
  emit finished(reqId, hist, minX, maxX, success);
}
