#include "MainWindow.h"
#include "ChangeParametersCommand.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/screen-map.h"
#include "AdaptiveSharpeningChart.h"
#include "AdaptiveSharpeningWorker.h"
#include "CoordinateOptimizationWorker.h"
#include "DetectScreenWorker.h"
#include "FinetuneMisregisteredWorker.h"
#include "GeometryPanel.h"
#include "ImageWidget.h"
#include "NavigationView.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QThread>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

/** Return the display name of TYPE when it is a concrete known screen. */
static QString detectionScreenName(colorscreen::scr_type type) {
  const int index = static_cast<int>(type);
  if (!colorscreen::screen_has_regular_geometry_p(type) || index < 0 ||
      index >= colorscreen::max_scr_type ||
      !colorscreen::scr_names[index].pretty_name)
    return QString();
  return QString::fromUtf8(colorscreen::scr_names[index].pretty_name);
}

/** Explain regular-screen discovery failure while stating preserved state. */
QString screenDetectionFailureMessage(colorscreen::scr_type attemptedType) {
  const QString name = detectionScreenName(attemptedType);
  if (!name.isEmpty()) {
    return QCoreApplication::translate(
               "MainWindow",
               "No regular %1 lattice was found in this scan. Existing screen "
               "geometry and registration points were left unchanged. Try a "
               "scan with a clearer central raster, or verify the Screen type "
               "and capture settings.")
        .arg(name);
  }
  return QCoreApplication::translate(
      "MainWindow",
      "No supported regular screen lattice was found in this scan. Existing "
      "screen type, geometry, and registration points were left unchanged. "
      "Try a scan with a clearer central raster, or choose the Screen type "
      "manually and verify the capture settings.");
}

/** Explain initial coordinate discovery failure for a known regular screen. */
QString coordinateDetectionFailureMessage(colorscreen::scr_type attemptedType) {
  const QString name = detectionScreenName(attemptedType);
  if (!name.isEmpty()) {
    return QCoreApplication::translate(
               "MainWindow",
               "Could not establish %1 screen coordinates from this scan. "
               "Existing geometry was left unchanged. Try a scan with a "
               "clearer central raster, verify the Screen type, or adjust "
               "capture gamma/linearization before retrying.")
        .arg(name);
  }
  return QCoreApplication::translate(
      "MainWindow",
      "Could not establish screen coordinates from this scan. Existing "
      "geometry was left unchanged. Try a scan with a clearer central raster, "
      "verify the Screen type, or adjust capture gamma/linearization before "
      "retrying.");
}

/** Explain progressive registration failure without claiming rollback. */
QString registrationDiscoveryFailureMessage(bool screenAutodetection) {
  if (screenAutodetection) {
    return QCoreApplication::translate(
        "MainWindow",
        "Screen detection stopped while adding or refining registration "
        "points. Any points or geometry already accepted remain in the "
        "document. Inspect the registration overlay, then retry Detect screen "
        "or use Add points in selected area on a clearer region.");
  }
  return QCoreApplication::translate(
      "MainWindow",
      "Automatic registration stopped before completing. Any points or "
      "geometry already accepted remain in the document. Inspect the "
      "registration overlay, then retry Add registration points or use Add "
      "points in selected area on a clearer region.");
}

/** Explain geometry-fit failure without inventing an algorithmic cause. */
QString geometryFitFailureMessage() {
  return QCoreApplication::translate(
      "MainWindow",
      "Geometry fitting could not find a solution for the current registration "
      "points and fit settings. Existing geometry and registration points were "
      "left unchanged. Inspect the registration overlay and Geometry fit "
      "coverage/outlier guidance, adjust bad or under-covered points or fit "
      "settings, then retry Fit geometry.");
}

/** Explain profile-optimization failure without inventing a numerical cause. */
QString profileOptimizationFailureMessage() {
  return QCoreApplication::translate(
      "MainWindow",
      "Profile optimization could not fit the current calibration spots for "
      "this image and its current geometry/color-processing inputs. Existing "
      "profile correction values and calibration spots were left unchanged. "
      "Inspect the spot distribution and current geometry/color inputs, adjust "
      "the spots or inputs if needed, then retry Optimize profile.");
}

/** Explain a measured-MTF model-fit failure without guessing its numerical cause. */
QString mtfModelFitFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Measured-MTF model fitting could not fit the selected saved measurements "
      "with the current model and fit settings. Existing MTF measurements and "
      "model parameters were left unchanged. Inspect the selected measurement "
      "curves, capture metadata, and fit options in Fit measured MTF model, "
      "adjust them if needed, then retry Fit measured MTF model.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nFitter detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain a failed slanted-edge measurement without implying partial commit. */
QString mtfMeasurementFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Slanted-edge MTF measurement could not qualify the selected edge region "
      "with the current measurement and capture settings. Existing saved MTF "
      "measurements and model parameters were left unchanged; no partial "
      "per-channel measurement batch was accepted. Select one straight, "
      "isolated edge with clear plateaus on both sides, avoid dust, texture, "
      "multiple edges and edges parallel to the pixel grid, review the "
      "measurement/capture settings if needed, then retry Measure MTF.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nMeasurement detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain automatic focus-area discovery failure without guessing its cause. */
QString focusAreaSearchFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Focus-area search could not complete for the current image, screen "
      "geometry, and focus-analysis inputs. Existing processing parameters were "
      "left unchanged, and no new focus-area candidates were accepted. Verify "
      "the image and fitted Screen/Geometry setup, then retry Find focus areas; "
      "if necessary use a clearer scan with larger uniform colour regions.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nSearch detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain focus-area joint-analysis failure without guessing its cause. */
QString focusAreaAnalysisFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Focus-area analysis could not build a validated shared focus model from "
      "the current candidate regions and analysis settings. Existing processing "
      "and sharpening parameters were left unchanged. The candidate and failed-"
      "region overlays remain available for inspection. Review the marked "
      "regions and validation setup, rerun Find focus areas if the candidates "
      "are weak, then retry Analyze focus areas.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nAnalysis detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain flat-field reference-analysis failure without guessing its cause. */
QString flatFieldFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Flat-field reference analysis could not produce a correction from the "
      "selected white/black reference inputs and current capture settings. "
      "Existing flat-field correction and document parameters were left "
      "unchanged. Verify that the reference files are readable and match the "
      "capture setup, review capture gamma and demosaic settings, then retry "
      "Flat field — Set reference.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nAnalysis detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain adaptive-sharpening failure without confusing it with cancellation. */
QString adaptiveSharpeningFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Adaptive sharpening analysis could not produce a spatial correction for "
      "the current image, fitted screen geometry, and analysis settings. "
      "Existing adaptive sharpening correction and document parameters were "
      "left unchanged, and the chart was restored to the accepted correction. "
      "Review the Screen/Geometry fit and adaptive analysis settings, then retry "
      "Analyze adaptive sharpening.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nAnalysis detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain failed refinement of an already configured coordinate basis. */
QString coordinateOptimizationFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Coordinate refinement could not improve the current screen coordinate "
      "basis for this scan and its current Screen/capture inputs. Existing "
      "screen coordinates, nonlinear correction, and registration points were "
      "left unchanged. Inspect whether the current coordinates land on a clear "
      "raster and whether Screen type and capture gamma/linearization are "
      "correct. If the starting basis is wrong, run Detect screen coordinates "
      "again; otherwise retry Optimize coordinates.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nOptimizer detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain a failed file render without implying document-state mutation. */
QString renderToFileFailureMessage(const QString &outputPath,
                                   const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Rendering the accepted document snapshot to \"%1\" did not complete. "
      "The current document parameters and image state were not changed, and "
      "no failed partial output is retained at that path. Verify that the "
      "destination is writable and has enough free space, review the export "
      "format/size settings, then retry Render to file.")
                        .arg(outputPath);
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nRender detail: %1")
                   .arg(trimmedDetail);
  return message;
}

/** Explain failure of the experimental one-area Focus analyzer. */
QString areaComputationFailureMessage(const QString &operation) {
  return QCoreApplication::translate(
             "MainWindow",
             "The selected area could not be analyzed for %1. Existing "
             "processing parameters were left unchanged. Choose a larger, "
             "clearer area that matches the operation's requirements and retry.")
      .arg(operation);
}

QString pointFocusAnalysisFailureMessage(const QString &detail) {
  QString message = QCoreApplication::translate(
      "MainWindow",
      "Focus analysis could not fit the selected image area with the current "
      "screen geometry and focus settings. Existing MTF/sharpening parameters "
      "were left unchanged. Choose a clear area with visible screen structure, "
      "verify the fitted Screen/Geometry setup, or adjust the focus-analysis "
      "options, then retry Analyze area.");
  const QString trimmedDetail = detail.trimmed();
  if (!trimmedDetail.isEmpty())
    message += QCoreApplication::translate("MainWindow", "\n\nAnalysis detail: %1")
                   .arg(trimmedDetail);
  return message;
}

namespace {
/** Return the physical scan resolution inferred from a configured screen. */
std::optional<double> estimateScreenDpi(
    const colorscreen::scr_to_img_parameters &geometry,
    const colorscreen::image_data *scan) {
  if (!scan || scan->width <= 0 || scan->height <= 0 ||
      !colorscreen::screen_geometry_configured_p(geometry))
    return std::nullopt;

  colorscreen::scr_to_img map;
  if (!map.set_parameters(geometry, *scan))
    return std::nullopt;
  const double pixelSize =
      map.pixel_size({0, 0, scan->width, scan->height});
  const double dpi = geometry.estimate_dpi(pixelSize);
  if (!colorscreen::my_isfinite(dpi) || dpi <= 0)
    return std::nullopt;
  return dpi;
}

/** Confirmation used after screen geometry becomes known.

    Screen identity/geometry are not optional here; the checkboxes cover only
    derived recommendations that can be declined independently. */
class ScreenDetectionSuggestionDialog final : public QDialog {
public:
  ScreenDetectionSuggestionDialog(
      const QString &screenName, const QIcon &screenIcon,
      const QString &currentColorModel, const QString &preferredColorModel,
      bool suggestColorModel, std::optional<double> screenDpi,
      bool suggestScreenDpi, QWidget *parent)
      : QDialog(parent) {
    setWindowTitle(tr("Screen Detection"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    auto *summary = new QHBoxLayout();
    if (!screenIcon.isNull()) {
      auto *icon = new QLabel(this);
      icon->setPixmap(screenIcon.pixmap(96, 96));
      icon->setFixedSize(96, 96);
      summary->addWidget(icon, 0, Qt::AlignTop);
    }
    auto *message = new QLabel(
        tr("Screen geometry detected for <b>%1</b>.").arg(screenName), this);
    message->setWordWrap(true);
    summary->addWidget(message, 1);
    root->addLayout(summary);

    if (suggestColorModel) {
      m_colorModel = new QCheckBox(
          tr("Change color model from %1 to preferred %2")
              .arg(currentColorModel, preferredColorModel),
          this);
      m_colorModel->setChecked(true);
      m_colorModel->setObjectName(
          QStringLiteral("DetectedScreenPreferredColorModelCheck"));
      root->addWidget(m_colorModel);
    }

    if (suggestScreenDpi && screenDpi) {
      m_screenDpi = new QCheckBox(
          tr("Set resolution to %1 PPI (estimated from screen geometry)")
              .arg(*screenDpi, 0, 'f', 1),
          this);
      m_screenDpi->setChecked(true);
      m_screenDpi->setObjectName(
          QStringLiteral("DetectedScreenResolutionCheck"));
      root->addWidget(m_screenDpi);
    }

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(
        suggestColorModel || suggestScreenDpi ? tr("Apply suggestions")
                                              : tr("Continue"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
  }

  bool usePreferredColorModel() const {
    return m_colorModel && m_colorModel->isChecked();
  }
  bool useScreenDpi() const {
    return m_screenDpi && m_screenDpi->isChecked();
  }

private:
  QCheckBox *m_colorModel = nullptr;
  QCheckBox *m_screenDpi = nullptr;
};

} // namespace

/** Handle request to automatically add registration points in a
   user-selected area.  Enters area selection mode; when the user draws
   a rectangle, launches a FinetuneMisregisteredWorker that iteratively
   detects points, solves geometry, and repeats.
   PARAMS contains grid spacing and tolerance settings from GeometryPanel.
   Results arrive incrementally via pointsReady and geometryReady signals.  */
void MainWindow::onAutomaticallyAddPointsInAreaRequested(
    const colorscreen::finetune_area_parameters &params) {
  if (!m_scan)
    return;
  if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
    inspectorStatusBar()->showMessage(
        tr("Detect screen coordinates before adding registration points."),
        3000);
    return;
  }

  startAreaSelection(
      tr("Select area to add points"), [this, params](QRect area) {
        if (area.width() <= 0 || area.height() <= 0)
          return;

        const colorscreen::int_image_area crop = {
            area.x(), area.y(), area.width(), area.height()};
        startRegistrationDiscovery(crop, params, false, false, true);
      });
}

/** Pin Workflow's next-step guidance to one active Detect-screen request. */
void MainWindow::setScreenAutodetectionProgress(
    const std::shared_ptr<colorscreen::progress_info> &progress,
    bool usesStop) {
  m_screenAutodetection.progress = progress;
  m_screenAutodetection.usesStop = usesStop;
  updateWorkflowSummary();
}

/** Clear Detect-screen guidance only if PROGRESS still owns the workflow.

    Coordinate autodetection hands off directly to incremental point discovery.
    Its delayed one-shot completion must therefore not clear the newer Stop
    phase after that handoff has already happened. */
void MainWindow::clearScreenAutodetectionProgress(
    const std::shared_ptr<colorscreen::progress_info> &progress) {
  const auto current = m_screenAutodetection.progress.lock();
  if (!current || current != progress)
    return;
  m_screenAutodetection.clearProgress();
  updateWorkflowSummary();
}

/** Handle a panel request to add registration points outside Detect screen. */
void MainWindow::onAutomaticallyAddPointsRequested(
    const colorscreen::finetune_area_parameters &params) {
  startAutomaticPointDiscovery(params, false);
}

/** Return whether GENERATION/PROGRESS still own the progressive registration
    request and the live document matches the worker's evolving accepted state. */
bool MainWindow::registrationDiscoveryRequestCurrent(
    uint64_t generation,
    const std::shared_ptr<colorscreen::progress_info> &progress) const {
  if (m_closeLifecycle.closing() || !progress || progress->pool_cancel() ||
      progress->cancelled() ||
      generation != m_registrationDiscovery.generation)
    return false;
  if (m_registrationDiscovery.progress.lock() != progress ||
      !m_registrationDiscovery.expectedState ||
      m_registrationDiscovery.scan != m_scan)
    return false;
  return *m_registrationDiscovery.expectedState == getCurrentState();
}

/** Cancel progressive registration after an unrelated document edit.

    Batches already accepted into the document remain ordinary Undo history.
    Generation advances before cooperative cancellation so queued point/geometry
    signals from the old worker cannot publish after the edit. */
void MainWindow::cancelStaleRegistrationDiscovery(
    const ParameterState &currentState) {
  if (!m_registrationDiscovery.expectedState)
    return;
  if (m_registrationDiscovery.scan == m_scan &&
      *m_registrationDiscovery.expectedState == currentState)
    return;

  const auto progress = m_registrationDiscovery.progress.lock();
  ++m_registrationDiscovery.generation;
  m_registrationDiscovery.clearRequest();
  if (progress)
    progress->cancel();

  const auto workflowProgress = m_screenAutodetection.progress.lock();
  if (progress && workflowProgress == progress)
    m_screenAutodetection.clearProgress();

  if (!m_closeLifecycle.closing())
    inspectorStatusBar()->showMessage(
        tr("Automatic point discovery stopped because its inputs changed."),
        3000);
}

/** Launch one progressive point/geometry discovery request over AREA.

    Worker-owned batches advance EXPECTEDSTATE before they are applied through
    Undo, so applyState()/updateWorkflowSummary() recognizes them as current.
    A user edit, Undo, image replacement or another discovery request instead
    invalidates the generation and prevents all later live publication. */
void MainWindow::startRegistrationDiscovery(
    const colorscreen::int_image_area &area,
    const colorscreen::finetune_area_parameters &params,
    bool screenAutodetection, bool allowRegistrationBootstrap,
    bool selectedArea) {
  if (!m_scan ||
      !colorscreen::screen_geometry_configured_p(m_scrToImgParams))
    return;

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();

  const auto previousProgress = m_registrationDiscovery.progress.lock();
  const uint64_t generation = ++m_registrationDiscovery.generation;
  if (previousProgress)
    previousProgress->cancel();
  if (previousProgress &&
      m_screenAutodetection.progress.lock() == previousProgress)
    m_screenAutodetection.clearProgress();

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("finding missing registration points", 1);
  m_registrationDiscovery.expectedState = baseline;
  m_registrationDiscovery.scan = scan;
  m_registrationDiscovery.progress = progress;

  const QString progressTitle =
      selectedArea ? tr("Find registration points in area")
                   : tr("Find registration points");
  const QString pointDescription =
      selectedArea ? tr("Add registration points in area")
                   : tr("Add registration points");
  const QString geometryDescription =
      selectedArea ? tr("Update geometry from registration points in area")
                   : tr("Update geometry from registration points");

  addUserVisibleProgress(progress, progressTitle, ProgressAction::Stop);
  if (screenAutodetection)
    setScreenAutodetectionProgress(progress, true);

  const bool computeMesh =
      m_geometryPanel && m_geometryPanel->isNonlinearEnabled();
  auto *worker = new FinetuneMisregisteredWorker(
      baseline.solver, baseline.rparams, baseline.scrToImg, scan, area,
      progress, params, computeMesh, allowRegistrationBootstrap);
  auto *thread = new QThread(this);
  worker->moveToThread(thread);
  m_backgroundThreads.track(thread);

  connect(thread, &QThread::started, worker,
          &FinetuneMisregisteredWorker::run);
  connect(worker, &FinetuneMisregisteredWorker::finished, thread,
          &QThread::quit, Qt::DirectConnection);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);

  connect(
      worker, &FinetuneMisregisteredWorker::pointsReady, this,
      [this, generation, progress, pointDescription](
          std::vector<colorscreen::solver_parameters::solver_point_t> points) {
        if (!registrationDiscoveryRequestCurrent(generation, progress)) {
          progress->cancel();
          return;
        }
        if (points.empty())
          return;

        const ParameterState oldState = getCurrentState();
        ParameterState newState = oldState;
        for (const auto &point : points)
          newState.solver.add_or_modify_point(point.img, point.scr, point.color);
        if (newState == oldState)
          return;

        // Advance ownership before QUndoStack::push() synchronously calls
        // applyState(newState), whose workflow refresh checks for staleness.
        m_registrationDiscovery.expectedState = newState;
        changeParameters(newState, pointDescription);
      });

  connect(
      worker, &FinetuneMisregisteredWorker::geometryReady, this,
      [this, generation, progress, geometryDescription](
          colorscreen::scr_to_img_parameters result) {
        if (!registrationDiscoveryRequestCurrent(generation, progress)) {
          progress->cancel();
          return;
        }

        const ParameterState oldState = getCurrentState();
        ParameterState newState = oldState;
        newState.scrToImg.merge_solver_solution(result);
        if (newState == oldState)
          return;

        m_registrationDiscovery.expectedState = newState;
        changeParameters(newState, geometryDescription);
        m_geometryFit.baseline = getCurrentState();
        m_geometryFit.failureInputs.reset();
        updateScreenCoordinateToolPresentation();
        updateWorkflowSummary();
      });

  connect(
      worker, &FinetuneMisregisteredWorker::requestCurrentPoints, this,
      [this, generation, progress](
          std::vector<colorscreen::solver_parameters::solver_point_t> *points) {
        if (!points)
          return;
        if (!registrationDiscoveryRequestCurrent(generation, progress)) {
          progress->cancel();
          points->clear();
          return;
        }
        *points = m_solverParams.points;
      },
      Qt::BlockingQueuedConnection);

  connect(
      worker, &FinetuneMisregisteredWorker::finished, this,
      [this, generation, progress, screenAutodetection](bool success) {
        const bool ownsRequest =
            generation == m_registrationDiscovery.generation &&
            m_registrationDiscovery.progress.lock() == progress;
        const bool publishable =
            registrationDiscoveryRequestCurrent(generation, progress);
        const bool cancelled =
            progress && (progress->pool_cancel() || progress->cancelled());

        if (ownsRequest)
          m_registrationDiscovery.clearRequest();

        if (!m_closeLifecycle.closing())
          removeProgress(progress);
        if (!m_closeLifecycle.closing() && screenAutodetection)
          clearScreenAutodetectionProgress(progress);

        if (!m_closeLifecycle.closing() && ownsRequest && publishable &&
            screenAutodetection && success && !cancelled && m_scan) {
          const auto currentScan = m_scan;
          const ParameterState current = getCurrentState();
          presentScreenDetectionSuggestions(
              currentScan, current, current.scrToImg, false,
              [this, current](bool updateColorModel, bool updateDpi,
                              double screenDpi) {
                ParameterState newState = current;
                QStringList changes;
                if (updateColorModel) {
                  const auto previous = newState.rparams.color_model;
                  if (newState.rparams.auto_color_model(
                          newState.scrToImg.type) &&
                      newState.rparams.color_model != previous)
                    changes << tr("preferred color model");
                }
                if (updateDpi && screenDpi > 0) {
                  newState.rparams.sharpen.scanner_mtf.scan_dpi = screenDpi;
                  changes << tr("screen-derived resolution");
                }
                if (!changes.isEmpty())
                  changeParameters(
                      newState, tr("Use %1").arg(changes.join(", ")));
              });
        }

        if (!m_closeLifecycle.closing() && ownsRequest && publishable && !success &&
            !cancelled) {
          auto *box = new QMessageBox(
              QMessageBox::Warning, tr("Automatic Registration"),
              registrationDiscoveryFailureMessage(screenAutodetection),
              QMessageBox::Ok, inspectorDialogParent());
          box->setObjectName(
              QStringLiteral("RegistrationDiscoveryFailureDialog"));
          box->setAttribute(Qt::WA_DeleteOnClose);
          box->open();
        }
      });

  thread->start();
}

/** Add registration points across the entire cropped image area.

    SCREENAUTODETECTION is true only for the combined Screen -> Detect screen
    workflow. While it is true, accepted point and geometry batches advance one
    evolving request snapshot, while Workflow keeps a stable wait/Stop hint. */
void MainWindow::startAutomaticPointDiscovery(
    const colorscreen::finetune_area_parameters &params,
    bool screenAutodetection) {
  if (!m_scan)
    return;
  if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
    inspectorStatusBar()->showMessage(
        tr("Detect screen coordinates before adding registration points."),
        3000);
    return;
  }

  const colorscreen::int_image_area crop =
      m_rparams.get_scan_crop(m_scan->width, m_scan->height);
  startRegistrationDiscovery(crop, params, screenAutodetection, true, false);
}

/** Launch automatic screen detection.
   Regular screens with a known type follow the coordinate/registration path.
   Unknown regular-screen detection is a final-result OneShotOperation whose
   scan and complete ParameterState must remain current through computation and
   the later confirmation prompt. */
void MainWindow::onAutodetectScreen() {
  if (!m_scan)
    return;

  // Starting this action supersedes an older successful detection that may
  // still be waiting for screen-derived setup recommendations.
  dismissOneShotPrompts();

  // A stochastic process has no regular lattice to identify. Color-element
  // autodetection is performed by the screen-detection render modes instead.
  if (colorscreen::stochastic_screen_p(m_scrToImgParams.type))
    return;

  if (colorscreen::screen_has_regular_geometry_p(m_scrToImgParams.type)) {
    if (m_solverParams.n_points() > 0) {
      // Existing points are expressed in the current basis. Detect Screen may
      // refine/add points, but must never replace that basis underneath them.
      if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
        inspectorStatusBar()->showMessage(
            tr("Existing control points require their original screen "
               "coordinate system. Restore it or delete the points before "
               "detecting new coordinates."),
            6000);
        return;
      }
      startAutomaticPointDiscovery(m_geometryPanel->finetuneAreaParams(), true);
      return;
    }

    startCoordinateAutodetection(true);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<DetectScreenAnalysisResult>();

  OneShotOperation operation;
  operation.description = tr("Detecting screen");
  operation.prerequisites =
      [this, scan]() { return !m_closeLifecycle.closing() && m_scan == scan; };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline &&
           !result->cancelled;
  };
  operation.applyResult = [this, scan, baseline, result]() {
    presentDetectedScreenResult(*result, scan, baseline);
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline,
       result](colorscreen::progress_info *progress) mutable {
        *result = DetectScreenWorker::analyze(
            baseline.detect, baseline.solver, baseline.scrToImg,
            baseline.rparams, scan, progress);
      });
}

/** Offer optional setup derived from a now-known regular screen.

    The prompt is snapshot-bound exactly like other final-result confirmations:
    changing the image or any document parameter while it is visible vetoes its
    delayed suggestions. APPLY owns the caller-specific mandatory publication
    (for RGB discovery this includes the newly detected screen/geometry). */
void MainWindow::presentScreenDetectionSuggestions(
    std::shared_ptr<colorscreen::image_data> scan,
    const ParameterState &baseline,
    const colorscreen::scr_to_img_parameters &geometry,
    bool alwaysShow,
    std::function<void(bool, bool, double)> apply) {
  if (!scan || !colorscreen::screen_has_regular_geometry_p(geometry.type)) {
    if (apply)
      apply(false, false, -1);
    return;
  }

  colorscreen::render_parameters preferredColor = baseline.rparams;
  const bool hasPreferredColor =
      preferredColor.auto_color_model(geometry.type);
  const bool suggestColor =
      hasPreferredColor &&
      preferredColor.color_model != baseline.rparams.color_model;

  const std::optional<double> dpi = estimateScreenDpi(geometry, scan.get());
  const bool suggestDpi =
      dpi && std::abs(*dpi - baseline.rparams.sharpen.scanner_mtf.scan_dpi) >
                 0.1;

  if (!alwaysShow && !suggestColor && !suggestDpi) {
    if (m_scan == scan && getCurrentState() == baseline && apply)
      apply(false, false, dpi.value_or(-1));
    return;
  }

  const QString screenName = QString::fromUtf8(
      colorscreen::scr_names[(int)geometry.type].pretty_name);
  const QString currentColor = QString::fromUtf8(
      colorscreen::render_parameters::color_model_properties[
          baseline.rparams.color_model]
          .pretty_name);
  const QString preferredColorName = QString::fromUtf8(
      colorscreen::render_parameters::color_model_properties[
          preferredColor.color_model]
          .pretty_name);

  auto *dialog = new ScreenDetectionSuggestionDialog(
      screenName, renderScreenIcon(geometry.type), currentColor,
      preferredColorName, suggestColor, dpi, suggestDpi, inspectorDialogParent());
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_screenAutodetection.prompt = dialog;

  connect(
      dialog, &QDialog::finished, this,
      [this, dialog, scan, baseline, dpi, apply = std::move(apply)](int result) {
        if (m_screenAutodetection.prompt != dialog)
          return;
        m_screenAutodetection.prompt = nullptr;
        if (m_closeLifecycle.closing() || m_scan != scan || getCurrentState() != baseline)
          return;

        // Closing the window still accepts the mandatory detected geometry in
        // the RGB path, matching the old confirmation behavior, but must not
        // silently accept optional dye/PPI recommendations.
        const bool acceptSuggestions = result == QDialog::Accepted;
        if (apply) {
          apply(acceptSuggestions && dialog->usePreferredColorModel(),
                acceptSuggestions && dialog->useScreenDpi(),
                dpi.value_or(-1));
        }
      });
  dialog->open();
}

/** Present a completed screen detection and publish it only after confirmation.
   BASELINE is deliberately retained after the worker finishes: the asynchronous
   recommendation dialog leaves the GUI responsive, so an edit, image change,
   or newer final-result operation while it is visible must invalidate the
   result instead of applying it to a different document state. */
void MainWindow::presentDetectedScreenResult(
    const DetectScreenAnalysisResult &result,
    std::shared_ptr<colorscreen::image_data> scan,
    const ParameterState &baseline) {
  if (!result.success || !result.detected.success) {
    auto *box = new QMessageBox(
        QMessageBox::Warning, tr("Screen Detection"),
        screenDetectionFailureMessage(baseline.scrToImg.type),
        QMessageBox::Ok, inspectorDialogParent());
    box->setObjectName(QStringLiteral("ScreenDetectionFailureDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    return;
  }

  const colorscreen::scr_to_img_parameters detectedParam =
      result.detected.param;
  const std::shared_ptr<colorscreen::mesh> detectedMesh =
      result.detected.mesh_trans;
  const auto solverPoints = result.solver.points;
  const std::shared_ptr<const colorscreen::screen_map> detectedScreenMap =
      result.screenMap;

  presentScreenDetectionSuggestions(
      scan, baseline, detectedParam, true,
      [this, scan, baseline, detectedParam, detectedMesh, solverPoints,
       detectedScreenMap](bool updateColorModel, bool updateDpi,
                          double screenDpi) mutable {
        ParameterState newState = baseline;
        newState.scrToImg.type = detectedParam.type;
        if (detectedMesh) {
          newState.scrToImg.merge_solver_solution(detectedParam);
          newState.scrToImg.mesh_trans = detectedMesh;
          newState.scrToImg.mesh_trans_is_scr_to_img = true;
        }
        if (updateColorModel)
          newState.rparams.auto_color_model(detectedParam.type);
        if (updateDpi && screenDpi > 0)
          newState.rparams.sharpen.scanner_mtf.scan_dpi = screenDpi;
        newState.solver.points = solverPoints;

        // Render mode is view/session state, not part of the undoable document
        // snapshot. Set it before applying parameters so the refresh uses it.
        m_renderTypeParams.type = colorscreen::render_type_interpolated;
        changeParameters(newState, tr("Autodetect screen"));

        publishDetectedScreenDiagnostics(
            std::move(detectedScreenMap), scan, baseline);

        updateRegistrationActions();
        updateModeMenu();

        // Refine the remaining geometry while preserving the detected mesh.
        requestGeometryOptimization(false);
      });
}

/** Return whether GENERATION/PROGRESS still own the progressive adaptive
    sharpening request and its immutable inputs are unchanged.  This gate is
    used by every live chart signal as well as final result publication. */
bool MainWindow::adaptiveSharpeningRequestCurrent(
    uint64_t generation,
    const std::shared_ptr<colorscreen::progress_info> &progress) const {
  if (m_closeLifecycle.closing() || !progress || progress->pool_cancel() ||
      progress->cancelled() ||
      generation != m_adaptiveSharpening.generation)
    return false;
  if (m_adaptiveSharpening.progress.lock() != progress ||
      !m_adaptiveSharpening.baseline ||
      m_adaptiveSharpening.scan != m_scan)
    return false;
  return *m_adaptiveSharpening.baseline == getCurrentState();
}

/** Restore the adaptive chart to accepted document state after a live request
    is cancelled, fails, becomes stale or is superseded before publication. */
void MainWindow::restoreAdaptiveSharpeningChart() {
  if (!m_sharpnessPanel)
    return;
  if (AdaptiveSharpeningChart *chart = m_sharpnessPanel->getAdaptiveChart()) {
    chart->clear();
    chart->setCorrection(m_rparams.scanner_blur_correction);
  }
  m_sharpnessPanel->setAdaptiveAnalysisRunning(false);
}

/** Cancel a progressive adaptive-sharpening request when CURRENTSTATE no
    longer matches its captured input snapshot.  Increment generation before
    requesting cancellation so already queued chart cells are rejected too. */
void MainWindow::cancelStaleAdaptiveSharpening(
    const ParameterState &currentState) {
  if (!m_adaptiveSharpening.baseline)
    return;
  if (m_adaptiveSharpening.scan == m_scan &&
      *m_adaptiveSharpening.baseline == currentState)
    return;

  const auto progress = m_adaptiveSharpening.progress.lock();
  ++m_adaptiveSharpening.generation;
  m_adaptiveSharpening.clearRequest();
  if (progress)
    progress->cancel();
  restoreAdaptiveSharpeningChart();
  if (!m_closeLifecycle.closing())
    inspectorStatusBar()->showMessage(
        tr("Adaptive sharpening analysis stopped because its inputs changed."),
        3000);
}

/** Launch adaptive sharpening analysis with PARAMETERS selected by the user.
    The request owns one immutable scan/ParameterState snapshot. Incremental
    chart cells and the final correction table share that same generation and
    progress identity, so a superseded/cancelled/stale worker cannot repaint or
    publish into a newer document state. */
void MainWindow::onAdaptiveSharpeningRequested(
    const AdaptiveSharpeningParameters &parameters) {
  if (!m_scan)
    return;
  if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
    inspectorStatusBar()->showMessage(
        tr("Fit screen geometry before adaptive sharpening analysis."), 3000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();

  // Supersede any older live request immediately. Its worker may need time to
  // unwind, but generation/progress gates below prevent further publication.
  const auto previousProgress = m_adaptiveSharpening.progress.lock();
  const uint64_t generation = ++m_adaptiveSharpening.generation;
  if (previousProgress)
    previousProgress->cancel();

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("adaptive sharpening analysis", 1);
  m_adaptiveSharpening.baseline = baseline;
  m_adaptiveSharpening.scan = scan;
  m_adaptiveSharpening.progress = progress;
  addUserVisibleProgress(progress, tr("Adaptive sharpening analysis"));

  AdaptiveSharpeningWorker *worker = new AdaptiveSharpeningWorker(
      baseline.scrToImg, baseline.rparams, scan, parameters, progress);

  QThread *thread = new QThread(this);
  worker->moveToThread(thread);
  m_backgroundThreads.track(thread);

  connect(thread, &QThread::started, worker, &AdaptiveSharpeningWorker::run);

  // Route every incremental publication through MainWindow. Connecting the
  // worker directly to the chart let an old request overwrite a newer chart.
  QPointer<AdaptiveSharpeningChart> chart;
  if (m_sharpnessPanel && m_sharpnessPanel->getAdaptiveChart()) {
    m_sharpnessPanel->showAdaptiveChart();
    chart = m_sharpnessPanel->getAdaptiveChart();
    chart->clear();

    connect(worker, &AdaptiveSharpeningWorker::stripAnalysisStarted, this,
            [this, chart, generation, progress](int w, int h) {
              if (chart &&
                  adaptiveSharpeningRequestCurrent(generation, progress))
                chart->initialize(w, h);
            });
    connect(worker, &AdaptiveSharpeningWorker::stripAnalyzed, this,
            [this, chart, generation, progress](int x, int y, double red,
                                                double green) {
              if (chart &&
                  adaptiveSharpeningRequestCurrent(generation, progress))
                chart->updateStrip(x, y, red, green);
            });
    connect(worker, &AdaptiveSharpeningWorker::blurAnalysisStarted, this,
            [this, chart, generation, progress](int w, int h) {
              if (chart &&
                  adaptiveSharpeningRequestCurrent(generation, progress))
                chart->initialize(w, h);
            });
    connect(worker, &AdaptiveSharpeningWorker::blurAnalyzed, this,
            [this, chart, generation, progress](int x, int y,
                                                double correction) {
              if (chart &&
                  adaptiveSharpeningRequestCurrent(generation, progress))
                chart->updateBlur(x, y, correction);
            });
  }

  connect(worker, &AdaptiveSharpeningWorker::finished, thread, &QThread::quit,
          Qt::DirectConnection);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  connect(worker, &AdaptiveSharpeningWorker::finished, this,
          [this, progress, generation](
              bool success,
              std::shared_ptr<colorscreen::scanner_blur_correction_parameters>
                  result,
              const QString &error) {
            const bool ownsRequest =
                generation == m_adaptiveSharpening.generation &&
                m_adaptiveSharpening.progress.lock() == progress;
            const bool publishable =
                adaptiveSharpeningRequestCurrent(generation, progress);
            const bool cancelled =
                progress && (progress->pool_cancel() || progress->cancelled());

            // Request ownership controls cleanup; publication validity is a
            // separate question. Clear first so applying a successful result
            // through Undo/applyState cannot cancel its own completed request.
            if (ownsRequest)
              m_adaptiveSharpening.clearRequest();

            if (!m_closeLifecycle.closing() && ownsRequest) {
              if (publishable && !cancelled)
                onAdaptiveSharpeningFinished(success, result, error);
              else {
                restoreAdaptiveSharpeningChart();
                if (cancelled)
                  inspectorStatusBar()->showMessage(
                      tr("Adaptive sharpening analysis cancelled"), 3000);
                else
                  inspectorStatusBar()->showMessage(
                      tr("Displacement analysis result discarded because "
                         "its inputs changed."),
                      4000);
              }
            }
            if (!m_closeLifecycle.closing())
              removeProgress(progress);
          });

  thread->start();
}

/** Handle completion of adaptive sharpening analysis.
    On success, wraps the computed scanner_blur_correction in an undo command
    and updates the chart widget.  Shows a success info or failure warning. */
void MainWindow::onAdaptiveSharpeningFinished(
    bool success,
    std::shared_ptr<colorscreen::scanner_blur_correction_parameters> result,
    const QString &error) {
  if (!success || !result) {
    restoreAdaptiveSharpeningChart();
    auto *box = new QMessageBox(
        QMessageBox::Warning, tr("Adaptive Sharpening"),
        adaptiveSharpeningFailureMessage(error), QMessageBox::Ok, inspectorDialogParent());
    box->setObjectName(QStringLiteral("AdaptiveSharpeningFailureDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    return;
  }

  ParameterState oldState = getCurrentState();
  ParameterState newState = oldState;
  newState.rparams.scanner_blur_correction = result;

  m_undoStack->push(new ChangeParametersCommand(
      this, oldState, newState, "Adaptive Sharpening Analysis"));

  // The accepted correction belongs to the exact scan/document snapshot after
  // the undo command has installed RESULT. Loaded corrections intentionally
  // have no such session baseline and remain usable with unverified freshness.
  m_adaptiveSharpening.acceptedBaseline = getCurrentState();
  m_adaptiveSharpening.acceptedScan = m_scan;

  // applyState() already refreshes panel/chart state through the undo command.
  if (m_sharpnessPanel && m_sharpnessPanel->getAdaptiveChart()) {
    m_sharpnessPanel->getAdaptiveChart()->setCorrection(result);
    m_sharpnessPanel->setAdaptiveAnalysisRunning(false);
  }

  updateWorkflowSummary();
  inspectorStatusBar()->showMessage(
      tr("Adaptive sharpening analysis completed."), 4000);
}

/** Set the screen coordinate system center to the clicked image position.
   Updates m_scrToImgParams.center and propagates the change via
   onCoordinateSystemChanged().  */
void MainWindow::onSetCenter(colorscreen::point_t imgPos) {
  if (!m_scan) {
    return;
  }

  // Set the screen center to the clicked position
  m_scrToImgParams.center = imgPos;

  onCoordinateSystemChanged();

  m_imageWidget->update();
}

/** Detect coordinates without implicitly continuing another request's workflow. */
void MainWindow::onAutodetectCoordinatesRequested() {
  startCoordinateAutodetection(false);
}

/** Detect an initial coordinate system from immutable scan/parameter snapshots.
    Existing control points prohibit replacement of their coordinate frame.
    ADDPOINTSAFTERDETECTION is captured by this request, so a superseded request
    cannot start registration work or change a newer request's continuation. */
void MainWindow::startCoordinateAutodetection(bool addPointsAfterDetection) {
  if (!m_scan)
    return;
  if (m_solverParams.n_points() > 0) {
    inspectorStatusBar()->showMessage(
        tr("Delete existing control points before detecting a new screen "
           "coordinate system."),
        5000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<CoordinateAutodetectionResult>();
  // Keep the coordinate stage's progress alive through onDone so a cancelled
  // or failed operation can always clear its own Workflow marker. MainWindow
  // itself still stores only a weak reference.
  auto workflowProgress =
      std::make_shared<std::shared_ptr<colorscreen::progress_info>>();

  OneShotOperation operation;
  operation.description = tr("Autodetecting coordinates");
  operation.progressTitle = tr("Coordinate autodetection");
  operation.onStart =
      [this, addPointsAfterDetection, workflowProgress](
          std::shared_ptr<colorscreen::progress_info> progress) {
        if (!addPointsAfterDetection)
          return;
        *workflowProgress = progress;
        setScreenAutodetectionProgress(progress, false);
      };
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline &&
           m_solverParams.n_points() == 0 && !result->cancelled;
  };
  operation.applyResult = [this, result, addPointsAfterDetection]() {
    if (!result->success) {
      auto *box = new QMessageBox(
          QMessageBox::Warning, tr("Detect Screen Coordinates"),
          coordinateDetectionFailureMessage(
              getCurrentState().scrToImg.type),
          QMessageBox::Ok, inspectorDialogParent());
      box->setObjectName(
          QStringLiteral("CoordinateDetectionFailureDialog"));
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
      return;
    }

    ParameterState newState = getCurrentState();
    newState.scrToImg = result->coordinates;
    // Switch before applyState refreshes the canvas with accepted geometry.
    m_renderTypeParams.type = colorscreen::render_type_interpolated;
    changeParameters(newState, tr("Detect screen coordinates"));
    m_imageWidget->update();
    inspectorStatusBar()->showMessage(tr("Screen coordinates detected."), 3000);

    if (addPointsAfterDetection && m_geometryPanel)
      startAutomaticPointDiscovery(m_geometryPanel->finetuneAreaParams(), true);
  };
  operation.onDone = [this, addPointsAfterDetection, workflowProgress]() {
    if (!addPointsAfterDetection)
      return;
    if (*workflowProgress)
      clearScreenAutodetectionProgress(*workflowProgress);
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, result](colorscreen::progress_info *progress) mutable {
        *result = CoordinateOptimizationWorker::autodetect(
            baseline.scrToImg, baseline.rparams, scan, progress);
      });
}

/** Forward coordinate optimisation request to onOptimizeCoordinates. */
void MainWindow::onOptimizeCoordinatesRequested() {
  onOptimizeCoordinates();
}

/** Refine an existing coordinate system under the shared one-shot lifecycle. */
void MainWindow::onOptimizeCoordinates() {
  if (!m_scan)
    return;
  if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
    inspectorStatusBar()->showMessage(tr("Detect screen coordinates before refining them."), 3000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<CoordinateOptimizationResult>();

  OneShotOperation operation;
  operation.description = tr("Optimizing coordinates");
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline && !result->cancelled;
  };
  operation.applyResult = [this, result]() {
    if (result->success) {
      applyOptimizedCoordinates(result->finetune);
    } else {
      auto *box = new QMessageBox(
          QMessageBox::Warning, tr("Coordinate refinement"),
          coordinateOptimizationFailureMessage(
              QString::fromStdString(result->finetune.err)),
          QMessageBox::Ok, inspectorDialogParent());
      box->setObjectName(QStringLiteral("CoordinateOptimizationFailureDialog"));
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
    }
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, result](colorscreen::progress_info *progress) mutable {
        *result = CoordinateOptimizationWorker::optimize(
            baseline.scrToImg, baseline.rparams, scan, progress);
      });
}

/** Apply accepted coordinates without mutating the live state before Undo sees it.
    A new linear basis invalidates the old nonlinear mesh. Diagnostics remain
    document-local derived presentation, outside the saved ParameterState. */
void MainWindow::applyOptimizedCoordinates(
    const colorscreen::finetune_result &result) {
  ParameterState newState = getCurrentState();
  newState.scrToImg.center = result.center;
  newState.scrToImg.coordinate1 = result.coordinate1;
  newState.scrToImg.coordinate2 = result.coordinate2;
  newState.scrToImg.mesh_trans = nullptr;
  changeParameters(newState, "Optimize Coordinates");
  m_imageWidget->update();
  if (m_geometryPanel)
    m_geometryPanel->updateFinetuneImages(result);
  updateWorkflowSummary();
  inspectorStatusBar()->showMessage(tr("Optimize coordinates finished"), 3000);
}

/** Propagate coordinate system parameter changes to the renderer.
   Always updates NavigationView (which uses FAST mode depending on
   scr_to_img).  Only triggers ImageWidget re-render if the current
   render type requires screen-to-image mapping.  */
void MainWindow::onCoordinateSystemChanged() {
  if (!m_scan)
    return;

  ImageWidget *image = qobject_cast<ImageWidget *>(sender());
  if (!acceptsInspectorImageWidget(image))
    image = inspectorImageWidget();

  // Navigation View always needs update because it uses FAST mode (which relies
  // on ScrToImg)
  m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                     &m_detectParams);

  // Main area: checks flag
  // Using colorscreen::render_type_max to safe check
  if (m_renderTypeParams.type < colorscreen::render_type_max) {
    const auto &prop =
        colorscreen::render_type_properties[m_renderTypeParams.type];
    if (image &&
        (prop.flags & colorscreen::render_type_property::NEEDS_SCR_TO_IMG)) {
      image->updateParameters(&m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams,
                              &m_solverParams);
    }
  }

  // Grid drags mutate scr_to_img parameters in-place rather than through
  // changeParameters(). Refresh freshness here so a current/pending fit reacts
  // immediately instead of waiting for an unrelated panel refresh.
  updateWorkflowSummary();
}
/** Snapshot state before a grid drag operation for undo bookkeeping.  */
void MainWindow::onCoordinateSystemManipulationStarted() {
  m_canvasGestureUndo.coordinateEdit = getCurrentState();
}

/** Create an undo command after a grid drag operation completes.  */
void MainWindow::onCoordinateSystemManipulationFinished() {
  ParameterState newState = getCurrentState();
  const std::optional<ParameterState> oldState =
      std::move(m_canvasGestureUndo.coordinateEdit);
  m_canvasGestureUndo.coordinateEdit.reset();
  if (oldState && newState != *oldState)
    m_undoStack->push(new ChangeParametersCommand(
        this, *oldState, newState, "Modify coordinate system"));
  updateRegistrationActions();
}

