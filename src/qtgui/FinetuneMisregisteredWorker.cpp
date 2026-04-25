#include "FinetuneMisregisteredWorker.h"
#include "../libcolorscreen/include/finetune.h"

FinetuneMisregisteredWorker::FinetuneMisregisteredWorker(
    colorscreen::solver_parameters solverParams,
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::int_image_area area,
    std::shared_ptr<colorscreen::progress_info> progress)
    : m_solverParams(solverParams), m_rparams(rparams), m_scrToImg(scrToImg),
      m_scan(scan), m_area(area), m_progress(progress) {}

void FinetuneMisregisteredWorker::run() {
  // Create a local copy of solver parameters to work with
  colorscreen::solver_parameters localSolver = m_solverParams;

  // Store the initial point count
  size_t initialPointCount = localSolver.points.size();

  // Call finetune_misregistered_area
  bool success = colorscreen::finetune_misregistered_area(
      &localSolver, m_rparams, m_scrToImg, *m_scan, m_area, m_progress.get());

  // Check if cancelled
  if (m_progress && m_progress->cancelled()) {
    emit finished(false);
    return;
  }

  // If successful, extract the new points that were added
  if (success && localSolver.points.size() > initialPointCount) {
    std::vector<colorscreen::solver_parameters::solver_point_t> newPoints;
    for (size_t i = initialPointCount; i < localSolver.points.size(); ++i) {
      newPoints.push_back(localSolver.points[i]);
    }
    emit pointsReady(newPoints);
  }

  emit finished(success);
}
