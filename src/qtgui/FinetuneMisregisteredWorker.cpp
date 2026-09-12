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
    colorscreen::finetune_area_parameters fparams,
    bool computeMesh, bool allowAutomaticStallRecovery)
    : m_solverParams(solverParams), m_rparams(rparams), m_scrToImg(scrToImg),
      m_scan(scan), m_area(area), m_progress(progress), m_fparams(fparams),
      m_computeMesh(computeMesh),
      m_allowAutomaticStallRecovery(allowAutomaticStallRecovery) {}

void FinetuneMisregisteredWorker::run() {
  colorscreen::solver_parameters localSolver = m_solverParams;
  colorscreen::scr_to_img_parameters localScrToImg = m_scrToImg;
  bool computeMesh = m_computeMesh;
  bool forcedLensRecoveryUsed = false;

  std::vector<colorscreen::solver_parameters::solver_point_t> accumulatedPoints;
  QElapsedTimer lastUpdateTime;
  lastUpdateTime.start();
  size_t pointsAtLastUpdate = localSolver.points.size();

  auto cancelled = [this]() {
    return m_progress && m_progress->cancelled();
  };

  auto solveGeometry =
      [this, &cancelled](colorscreen::scr_to_img_parameters &geometry,
                         const colorscreen::solver_parameters &points,
                         bool nonlinear, bool ignoreLensCoverage,
                         bool forceStandardLens) -> bool {
    colorscreen::solver_parameters solveParams = points;
    if (forceStandardLens)
      solveParams.lens_fit_model =
          colorscreen::solver_parameters::lens_fit_standard;

    // A mesh is fitted on top of the current global lens correction. Do not
    // simultaneously refit the lens while deriving local nonlinear residuals.
    if (nonlinear)
      solveParams.optimize_lens = false;

    const colorscreen::coord_t error = colorscreen::solver(
        &geometry, *m_scan, solveParams, m_progress.get(), ignoreLensCoverage);
    if (cancelled() || !colorscreen::my_isfinite(error) || error >= 1e29)
      return false;

    if (!nonlinear)
      return true;

    if ((int)points.n_points() <=
        colorscreen::solver_parameters::min_mesh_points(geometry.type))
      return false;

    geometry.mesh_trans = colorscreen::solver_mesh(
        &geometry, *m_scan, solveParams, m_progress.get());
    geometry.mesh_trans_is_scr_to_img = false;
    return geometry.mesh_trans && !cancelled();
  };

  auto publishAndSync = [this, &localSolver, &localScrToImg,
                         &accumulatedPoints]() {
    if (!accumulatedPoints.empty()) {
      emit pointsReady(accumulatedPoints);
      accumulatedPoints.clear();
    }
    emit geometryReady(localScrToImg);
    std::vector<colorscreen::solver_parameters::solver_point_t> currentPoints =
        localSolver.points;
    emit requestCurrentPoints(&currentPoints);
    localSolver.points = currentPoints;
  };

  try {
    colorscreen::sub_task task(m_progress.get());

    while (true) {
      if (cancelled())
        break;

      const size_t initialPointCount = localSolver.points.size();
      const bool found = colorscreen::finetune_misregistered_area(
          &localSolver, m_rparams, localScrToImg, *m_scan, m_area, m_fparams,
          m_progress.get());

      if (cancelled())
        break;

      if (!found || localSolver.points.size() == initialPointCount) {
        bool recovered = false;

        if (m_allowAutomaticStallRecovery) {
          const bool dufayCanPromoteNonlinear =
              colorscreen::integrated_screen_p(localScrToImg.type) &&
              !computeMesh && !localScrToImg.mesh_trans &&
              (int)localSolver.n_points() >
                  colorscreen::solver_parameters::min_mesh_points(
                      localScrToImg.type);
          const bool ordinaryLensCanBeForced =
              !colorscreen::integrated_screen_p(localScrToImg.type) &&
              !computeMesh && !forcedLensRecoveryUsed &&
              localSolver.optimize_lens &&
              (int)localSolver.n_points() >=
                  colorscreen::solver_parameters::min_lens_points(
                      localScrToImg.type) &&
              !localSolver.lens_coverage_sufficient(
                  m_scan->width, m_scan->height,
                  localScrToImg.scanner_type);

          if (dufayCanPromoteNonlinear || ordinaryLensCanBeForced) {
            // Make the ordinary discoveries visible before changing the model,
            // and absorb any interactive point edits made while it was running.
            publishAndSync();
            pointsAtLastUpdate = localSolver.points.size();
            lastUpdateTime.restart();

            if (colorscreen::integrated_screen_p(localScrToImg.type) &&
                !computeMesh && !localScrToImg.mesh_trans &&
                (int)localSolver.n_points() >
                    colorscreen::solver_parameters::min_mesh_points(
                        localScrToImg.type)) {
              colorscreen::scr_to_img_parameters nonlinearGeometry =
                  localScrToImg;
              if (solveGeometry(nonlinearGeometry, localSolver, true, false,
                                false)) {
                localScrToImg = nonlinearGeometry;
                computeMesh = true;
                emit geometryReady(localScrToImg);
                recovered = true;
              }
            } else if (!colorscreen::integrated_screen_p(localScrToImg.type) &&
                       !computeMesh && !forcedLensRecoveryUsed &&
                       localSolver.optimize_lens &&
                       (int)localSolver.n_points() >=
                           colorscreen::solver_parameters::min_lens_points(
                               localScrToImg.type) &&
                       !localSolver.lens_coverage_sufficient(
                           m_scan->width, m_scan->height,
                           localScrToImg.scanner_type)) {
              // Coverage is a conservative automatic gate, not a proof that a
              // low-order lens model is impossible. Once ordinary discovery
              // stalls, make one Standard-radial attempt. The usual physical
              // envelope and projected-Jacobian identifiability checks still
              // decide whether the fitted lens is safe enough to install.
              forcedLensRecoveryUsed = true;
              colorscreen::scr_to_img_parameters lensGeometry = localScrToImg;
              if (solveGeometry(lensGeometry, localSolver, false, true, true)) {
                localScrToImg = lensGeometry;
                emit geometryReady(localScrToImg);
                recovered = true;
              }
            }
          }
        }

        if (recovered)
          continue;
        break;
      }

      const auto &points = localSolver.points;
      for (size_t i = initialPointCount; i < points.size(); ++i)
        accumulatedPoints.push_back(points[i]);

      const bool nonlinear =
          computeMesh &&
          (int)localSolver.n_points() >
              colorscreen::solver_parameters::min_mesh_points(
                  localScrToImg.type);

      bool timeThreshold = lastUpdateTime.elapsed() >= 5000;
      bool countThreshold =
          localSolver.points.size() >=
          (size_t)(pointsAtLastUpdate * 1.1 + 0.5);

      // Publish points before the potentially slow solve, then refresh the
      // local copy from the GUI in case the user edited the progressive set.
      if (timeThreshold || countThreshold) {
        if (!accumulatedPoints.empty()) {
          emit pointsReady(accumulatedPoints);
          accumulatedPoints.clear();
        }
        std::vector<colorscreen::solver_parameters::solver_point_t>
            currentPoints = localSolver.points;
        emit requestCurrentPoints(&currentPoints);
        localSolver.points = currentPoints;
      }

      if (!solveGeometry(localScrToImg, localSolver, nonlinear, false, false)) {
        if (!cancelled())
          emit finished(false);
        else
          emit finished(true);
        return;
      }

      if (cancelled())
        break;

      if (!timeThreshold && !countThreshold) {
        timeThreshold = lastUpdateTime.elapsed() >= 5000;
        countThreshold =
            localSolver.points.size() >=
            (size_t)(pointsAtLastUpdate * 1.1 + 0.5);
      }

      if (timeThreshold || countThreshold) {
        if (!accumulatedPoints.empty()) {
          emit pointsReady(accumulatedPoints);
          accumulatedPoints.clear();
        }
        emit geometryReady(localScrToImg);

        std::vector<colorscreen::solver_parameters::solver_point_t>
            currentPoints = localSolver.points;
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

  if (!accumulatedPoints.empty()) {
    emit pointsReady(accumulatedPoints);
    emit geometryReady(localScrToImg);
  }

  emit finished(true);
}
