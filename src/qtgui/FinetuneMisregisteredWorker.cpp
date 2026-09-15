#include "FinetuneMisregisteredWorker.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/mesh.h"
#include "../libcolorscreen/solver.h"
#include <QElapsedTimer>

FinetuneMisregisteredWorker::FinetuneMisregisteredWorker(
    colorscreen::solver_parameters solverParams,
    colorscreen::render_parameters rparams,
    colorscreen::scr_to_img_parameters scrToImg,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::int_image_area area,
    std::shared_ptr<colorscreen::progress_info> progress,
    colorscreen::finetune_area_parameters fparams,
    bool computeMesh, bool allowRegistrationBootstrap)
    : m_solverParams(solverParams), m_rparams(rparams), m_scrToImg(scrToImg),
      m_scan(scan), m_area(area), m_progress(progress), m_fparams(fparams),
      m_computeMesh(computeMesh),
      m_allowRegistrationBootstrap(allowRegistrationBootstrap) {}

void FinetuneMisregisteredWorker::run() {
  // Work on local snapshots. Point batches are progressively published to the
  // GUI, while speculative bootstrap points stay local until they
  // have been validated against the final ordinary/lens mapping.
  colorscreen::solver_parameters localSolver = m_solverParams;
  colorscreen::scr_to_img_parameters localScrToImg = m_scrToImg;

  std::vector<colorscreen::solver_parameters::solver_point_t> accumulatedPoints;
  QElapsedTimer lastUpdateTime;
  lastUpdateTime.start();
  size_t pointsAtLastUpdate = localSolver.points.size();
  bool stalled = false;

  auto cancelled = [this]() {
    return m_progress && m_progress->cancelled();
  };

  auto solveGeometry =
      [this, &cancelled](colorscreen::scr_to_img_parameters &geometry,
                         const colorscreen::solver_parameters &points,
                         bool nonlinear,
                         colorscreen::coord_t *errorOut) -> bool {
    colorscreen::solver_parameters solveParams = points;
    // Lens optimization is slow and intentionally incompatible with the
    // temporary local mesh. Small point sets cannot constrain it anyway.
    if (nonlinear || points.n_points() < 30)
      solveParams.optimize_lens = false;

    const colorscreen::coord_t error = colorscreen::solver(
        &geometry, *m_scan, solveParams, m_progress.get());
    if (errorOut)
      *errorOut = error;
    if (cancelled())
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
        stalled = true;
        break;
      }

      const auto &points = localSolver.points;
      for (size_t i = initialPointCount; i < points.size(); ++i)
        accumulatedPoints.push_back(points[i]);

      const bool nonlinear =
          m_computeMesh &&
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

      if (!solveGeometry(localScrToImg, localSolver, nonlinear, nullptr)) {
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

    // A compact point cloud may be locally well registered but still too small
    // to identify global lens distortion. If discovery stalls before it reaches
    // the image edges, ordinary screen geometries first try a deliberately
    // low-order lens fit from the trusted points even though the broad-coverage
    // gate is not yet met. Dufay-like screens retain the temporary nonlinear
    // search aid because their film geometry eventually requires that model.
    // Either provisional map is used for one discovery pass only; acceptance
    // still requires the ordinary global lens fit and speculative-point pruning.
    const bool lensCoverageMissing =
        localSolver.optimize_lens &&
        !localSolver.lens_optimization_sufficient(
            localScrToImg.type, m_scan->width, m_scan->height,
            localScrToImg.scanner_type);
    const bool dufayLikeBootstrap =
        colorscreen::dufay_like_screen_p(localScrToImg.type);
    const bool enoughBootstrapPoints =
        dufayLikeBootstrap
            ? (int)localSolver.n_points() >
                  colorscreen::solver_parameters::min_mesh_points(
                      localScrToImg.type)
            : (int)localSolver.n_points() >=
                  colorscreen::solver_parameters::min_lens_bootstrap_points(
                      localScrToImg.type);
    const bool canBootstrap =
        stalled && m_allowRegistrationBootstrap && !m_computeMesh &&
        !localScrToImg.mesh_trans && lensCoverageMissing &&
        enoughBootstrapPoints;

    if (!cancelled() && canBootstrap) {
      publishAndSync();
      pointsAtLastUpdate = localSolver.points.size();
      lastUpdateTime.restart();

      const bool refreshedDufayLike =
          colorscreen::dufay_like_screen_p(localScrToImg.type);
      const bool refreshedPointCountSufficient =
          refreshedDufayLike
              ? (int)localSolver.n_points() >
                    colorscreen::solver_parameters::min_mesh_points(
                        localScrToImg.type)
              : (int)localSolver.n_points() >=
                  colorscreen::solver_parameters::min_lens_bootstrap_points(
                      localScrToImg.type);
      const bool bootstrapStillNeeded =
          !cancelled() && localSolver.optimize_lens &&
          !localScrToImg.mesh_trans &&
          !localSolver.lens_optimization_sufficient(
              localScrToImg.type, m_scan->width, m_scan->height,
              localScrToImg.scanner_type) &&
          refreshedPointCountSufficient;
      if (bootstrapStillNeeded) {
        const colorscreen::solver_parameters preBootstrapSolver = localSolver;
        const colorscreen::scr_to_img_parameters preBootstrapGeometry = localScrToImg;
        const size_t bootstrapFirstPoint = localSolver.points.size();

        colorscreen::scr_to_img_parameters bootstrapGeometry = preBootstrapGeometry;
        bootstrapGeometry.mesh_trans = nullptr;

        bool provisionalMappingReady = false;
        if (refreshedDufayLike) {
          provisionalMappingReady =
              solveGeometry(bootstrapGeometry, localSolver, true, nullptr);
        } else {
          const auto previousLens = bootstrapGeometry.lens_correction;
          const colorscreen::coord_t bootstrapError =
              colorscreen::bootstrap_lens_solver(
                  &bootstrapGeometry, *m_scan, localSolver, m_progress.get());
          provisionalMappingReady =
              !cancelled() && colorscreen::my_isfinite(bootstrapError) &&
              bootstrapError < 1e29 &&
              !(bootstrapGeometry.lens_correction == previousLens);
        }

        bool bootstrapAccepted = false;
        if (provisionalMappingReady && !cancelled()) {
          const bool found = colorscreen::finetune_misregistered_area(
              &localSolver, m_rparams, bootstrapGeometry, *m_scan, m_area,
              m_fparams, m_progress.get());

          if (!cancelled() && found &&
              localSolver.points.size() > bootstrapFirstPoint &&
              localSolver.lens_optimization_sufficient(
                  preBootstrapGeometry.type, m_scan->width, m_scan->height,
                  preBootstrapGeometry.scanner_type)) {
            colorscreen::scr_to_img_parameters finalGeometry = preBootstrapGeometry;
            finalGeometry.mesh_trans = nullptr;
            colorscreen::coord_t error = 0;
            if (solveGeometry(finalGeometry, localSolver, false, &error) &&
                colorscreen::my_isfinite(error) && error < 1e29) {
              localSolver.prune_points_outside_mapping_tolerance(
                  finalGeometry, *m_scan, bootstrapFirstPoint,
                  m_fparams.max_displacement);

              if (localSolver.points.size() > bootstrapFirstPoint &&
                  localSolver.lens_optimization_sufficient(
                      finalGeometry.type, m_scan->width, m_scan->height,
                      finalGeometry.scanner_type)) {
                colorscreen::coord_t refinedError = 0;
                if (solveGeometry(finalGeometry, localSolver, false,
                                  &refinedError) &&
                    colorscreen::my_isfinite(refinedError) &&
                    refinedError < 1e29) {
                  localScrToImg = finalGeometry;
                  for (size_t i = bootstrapFirstPoint;
                       i < localSolver.points.size(); ++i)
                    accumulatedPoints.push_back(localSolver.points[i]);
                  bootstrapAccepted = true;
                }
              }
            }
          }
        }

        if (!bootstrapAccepted) {
          localSolver = preBootstrapSolver;
          localScrToImg = preBootstrapGeometry;
        }
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
