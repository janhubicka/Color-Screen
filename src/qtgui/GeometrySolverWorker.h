#ifndef GEOMETRY_SOLVER_WORKER_H
#define GEOMETRY_SOLVER_WORKER_H
#pragma once

#include "WorkerBase.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include <memory>

class GeometrySolverWorker : public WorkerBase {
  Q_OBJECT
public:
  GeometrySolverWorker(std::shared_ptr<colorscreen::image_data> scan, QObject *parent = nullptr);

public slots:
  void solve(int reqId,
             colorscreen::scr_to_img_parameters params,
             colorscreen::solver_parameters solverParams,
             std::shared_ptr<colorscreen::progress_info> progress,
             bool computeMesh = false);

signals:
  void finished(int reqId, colorscreen::scr_to_img_parameters result, bool success, bool cancelled = false);
};

#endif // GEOMETRY_SOLVER_WORKER_H
