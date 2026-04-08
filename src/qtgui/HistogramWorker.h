#ifndef HISTOGRAM_WORKER_H
#define HISTOGRAM_WORKER_H

#include "WorkerBase.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/progress-info.h"
#include <memory>
#include <vector>

class HistogramWorker : public WorkerBase {
  Q_OBJECT
public:
  HistogramWorker(std::shared_ptr<colorscreen::image_data> scan, QObject *parent = nullptr);

public slots:
  void compute(int reqId,
               colorscreen::render_parameters params,
               int steps,
               double minX,
               double maxX,
               colorscreen::hd_axis_type axisType,
               std::shared_ptr<colorscreen::progress_info> progress);

signals:
  void finished(int reqId, std::vector<uint64_t> result, double minX, double maxX, bool success);
};

#endif // HISTOGRAM_WORKER_H
