#include "FinetuneMisregisteredWorker.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/mesh.h"

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
  try {
    colorscreen::sub_task task(m_progress.get());

    while (true) {
      if (m_progress && m_progress->cancelled())
	break;

      // Store the initial point count
      size_t initialPointCount = localSolver.points.size();

      // Call finetune_misregistered_area
      printf ("Finetuning\n");
      bool found = colorscreen::finetune_misregistered_area(
	  &localSolver, m_rparams, localScrToImg, *m_scan, m_area,
	  m_progress.get());

      if (m_progress && m_progress->cancelled())
	break;

      printf ("Finetuning finished %i %i %i\n", found, localSolver.points.size (), initialPointCount);
      if (!found || localSolver.points.size () == initialPointCount)
	break;

      // If successful, extract the new points that were added
      std::vector<colorscreen::solver_parameters::solver_point_t> newPoints;
      for (size_t i = initialPointCount; i < localSolver.points.size(); ++i) {
	newPoints.push_back(localSolver.points[i]);
      }
      emit pointsReady(newPoints);

      // Invoke geometry solver same way as in GeometrySolverWorker.cpp
      //
      bool nonlinear = m_computeMesh && localSolver.points.size () > 10;

      // Lens optimization is slow. Disable it for nonlinear transforms
      colorscreen::solver_parameters solverParamsCopy = localSolver;
      if (nonlinear || localSolver.points.size () < 30)
	solverParamsCopy.optimize_lens = false;

      //auto originalMesh = localScrToImg.mesh_trans;

      // colorscreen::solver modifies params in place
      colorscreen::solver(&localScrToImg, *m_scan, solverParamsCopy,
			  m_progress.get());

      //if (!nonlinear)
	//localScrToImg.mesh_trans = originalMesh;

      if (m_progress && m_progress->cancelled())
	break;

      if (nonlinear) {
	localScrToImg.mesh_trans = colorscreen::solver_mesh(
	    &localScrToImg, *m_scan, solverParamsCopy, m_progress.get());
	if (!localScrToImg.mesh_trans) {
	  if (!m_progress->cancelled())
	    emit finished(false);
	  return;
	}
      }

      if (m_progress && m_progress->cancelled())
	break;

      emit geometryReady(localScrToImg);
    }
  } catch (...) {
    emit finished(false);
    return;
  }

  emit finished(true);
}
