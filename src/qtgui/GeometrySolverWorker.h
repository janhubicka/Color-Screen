#ifndef GEOMETRY_SOLVER_WORKER_H
#define GEOMETRY_SOLVER_WORKER_H
#pragma once

#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include <memory>
#include <QObject>

namespace colorscreen {
class image_data;
}

class GeometrySolverWorker : public QObject {
  Q_OBJECT
public:
  explicit GeometrySolverWorker(QObject *parent = nullptr);

  /** Fit one immutable source-scan/request snapshot on the worker thread. */
  void solve(int reqId,
             std::shared_ptr<colorscreen::image_data> scan,
             colorscreen::scr_to_img_parameters params,
             colorscreen::solver_parameters solverParams,
             std::shared_ptr<colorscreen::progress_info> progress,
             bool computeMesh = false);

signals:
  void finished(int reqId, colorscreen::scr_to_img_parameters result, bool success, bool cancelled = false);
};

#endif // GEOMETRY_SOLVER_WORKER_H
