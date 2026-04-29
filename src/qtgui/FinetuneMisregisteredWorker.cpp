#include "FinetuneMisregisteredWorker.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/mesh.h"
#include <QElapsedTimer>

FinetuneMisregisteredWorker::FinetuneMisregisteredWorker(
    colorscreen::solver_parameters solverParams,
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::int_image_area area,
    std::shared_ptr<colorscreen::progress_info> progress,
    bool computeMesh)
    : m_solverParams(solverParams), m_rparams(rparams), m_scrToImg(scrToImg),
      m_scan(scan), m_area(area), m_progress(progress),
      m_computeMesh(computeMesh) {}

void FinetuneMisregisteredWorker::run() {
  // Create local copies to work with and accumulate
  colorscreen::solver_parameters localSolver = m_solverParams;
  colorscreen::scr_to_img_parameters localScrToImg = m_scrToImg;
  
  std::vector<colorscreen::solver_parameters::solver_point_t> accumulatedPoints;
  QElapsedTimer lastUpdateTime;
  lastUpdateTime.start();
  size_t pointsAtLastUpdate = localSolver.points.size();

  try {
    colorscreen::sub_task task(m_progress.get());

    while (true) {
      if (m_progress && m_progress->cancelled())
	break;

      // Store the initial point count
      size_t initialPointCount = localSolver.points.size();

      // Call finetune_misregistered_area
      struct colorscreen::finetune_area_parameters fparam;
      bool found = colorscreen::finetune_misregistered_area(
	  &localSolver, m_rparams, localScrToImg, *m_scan, m_area,
	  fparam, m_progress.get());

      if (m_progress && m_progress->cancelled())
	break;

      if (!found || localSolver.points.size () == initialPointCount)
	break;

      // Accumulate the new points that were added
      for (size_t i = initialPointCount; i < localSolver.points.size(); ++i) {
	accumulatedPoints.push_back(localSolver.points[i]);
      }

      // Invoke geometry solver same way as in GeometrySolverWorker.cpp
      bool nonlinear = m_computeMesh && (int) localSolver.n_points () > colorscreen::solver_parameters::min_mesh_points (localScrToImg.type);

      // Lens optimization is slow. Disable it for nonlinear transforms
      colorscreen::solver_parameters solverParamsCopy = localSolver;
      if (nonlinear || localSolver.points.size () < 30)
	solverParamsCopy.optimize_lens = false;

      // Check if we should send updates to GUI
      bool timeThreshold = lastUpdateTime.elapsed() >= 5000;
      bool countThreshold = localSolver.points.size() >= (size_t)(pointsAtLastUpdate * 1.1 + 0.5);

      // Send points if they are desired.  Do this before solving so we receive user updates
      // earlier
      if (timeThreshold || countThreshold) {
	if (!accumulatedPoints.empty()) {
	  emit pointsReady(accumulatedPoints);
	  accumulatedPoints.clear();
	}
	std::vector<colorscreen::solver_parameters::solver_point_t> currentPoints = localSolver.points;
	emit requestCurrentPoints(&currentPoints);
	localSolver.points = currentPoints;
      }

      // colorscreen::solver modifies params in place
      colorscreen::solver(&localScrToImg, *m_scan, solverParamsCopy,
			  m_progress.get());

      if (m_progress && m_progress->cancelled())
	break;

      if (nonlinear) {
	localScrToImg.mesh_trans = colorscreen::solver_mesh(
	    &localScrToImg, *m_scan, solverParamsCopy, m_progress.get());
	localScrToImg.mesh_trans_is_scr_to_img = false;
	if (!localScrToImg.mesh_trans) {
	  if (!m_progress->cancelled())
	    emit finished(false);
	  return;
	}
      }

      if (m_progress && m_progress->cancelled())
	break;

      // Check if we should send updates to GUI
      if (!timeThreshold && !countThreshold)
        {
          timeThreshold = lastUpdateTime.elapsed() >= 5000;
          countThreshold = localSolver.points.size() >= (size_t)(pointsAtLastUpdate * 1.1 + 0.5);
	}

      if (timeThreshold || countThreshold) {
	if (!accumulatedPoints.empty()) {
	  emit pointsReady(accumulatedPoints);
	  accumulatedPoints.clear();
	}
        emit geometryReady(localScrToImg);
	
	std::vector<colorscreen::solver_parameters::solver_point_t> currentPoints = localSolver.points;
	emit requestCurrentPoints(&currentPoints);
	localSolver.points = currentPoints;
	
	pointsAtLastUpdate = localSolver.points.size();
	lastUpdateTime.restart();
      }
    }
  } catch (...) {
    emit finished(false);
    return;
  }

  // Final update if something changed since last one
  if (!accumulatedPoints.empty()) {
    emit pointsReady(accumulatedPoints);
    emit geometryReady(localScrToImg);
  }

  emit finished(true);
}
