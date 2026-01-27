#ifndef GEOMETRY_SOLVER_WORKER_H
#define GEOMETRY_SOLVER_WORKER_H

#include "WorkerBase.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"

class GeometrySolverWorker : public WorkerBase {
  Q_OBJECT
public:
  explicit GeometrySolverWorker(std::shared_ptr<colorscreen::image_data> scan,
                                QObject *parent = nullptr);
  ~GeometrySolverWorker() override;

public slots:
  void solve(int reqId, colorscreen::scr_to_img_parameters params,
             colorscreen::solver_parameters solverParams,
             std::shared_ptr<colorscreen::progress_info> progress,
             bool computeMesh = false);

signals:
  void finished(int reqId, colorscreen::scr_to_img_parameters result, bool success);

private:
};

#endif // GEOMETRY_SOLVER_WORKER_H
