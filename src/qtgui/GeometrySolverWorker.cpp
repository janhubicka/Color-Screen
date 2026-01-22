#include "GeometrySolverWorker.h"
#include "mesh.h"
#include <QDebug>

GeometrySolverWorker::GeometrySolverWorker(
    std::shared_ptr<colorscreen::image_data> scan, QObject *parent)
    : WorkerBase(scan, parent) {}

GeometrySolverWorker::~GeometrySolverWorker() = default;

void GeometrySolverWorker::solve(
    int reqId, colorscreen::scr_to_img_parameters params,
    colorscreen::solver_parameters solverParams,
    std::shared_ptr<colorscreen::progress_info> progress,
    bool computeMesh) {
  if (!m_scan || m_solveInProgress) {
    emit finished(reqId, params, false);
    return;
  }

  m_solveInProgress = true;
  bool success = false;

  try {
    colorscreen::sub_task task (progress.get ());
    // Lens optimization is slow. Disable it for nonlinear tranfomrs
    if (computeMesh)
      solverParams.optimize_lens = false;
    
    // colorscreen::solver modifies params in place and returns sum of squares of error
    colorscreen::coord_t error_sq = colorscreen::solver(&params, *m_scan, solverParams, progress.get());
    qDebug() << "Geometry solver finished with error squared:" << error_sq << " nonliner " << computeMesh;

    if (progress && progress->cancelled()) {
      success = false;
    } else {
      if (computeMesh) {
        params.mesh_trans = colorscreen::solver_mesh(&params, *m_scan, solverParams, progress.get());
        if (!params.mesh_trans) {
           success = false;
	   if (!progress->cancelled ())
             qWarning() << "Geometry solver failed to compute mesh";
        }
	else
	  success = true;
      } else {
        success = true;
        params.mesh_trans = nullptr;
      }
    }
  } catch (const std::exception &e) {
    qWarning() << "Geometry solver failed with exception:" << e.what();
    success = false;
  } catch (...) {
    qWarning() << "Geometry solver failed with unknown exception";
    success = false;
  }

  m_solveInProgress = false;
  emit finished(reqId, params, success);
}
