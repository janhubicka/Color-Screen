#include "MainWindow.h"
#include "ChangeParametersCommand.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/histogram.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/screen-map.h"
#include "../libcolorscreen/include/stitch.h"
#include "AdaptiveSharpeningChart.h" // Added
#include "AdaptiveSharpeningWorker.h"
#include "ColorOptimizerWorker.h"
#include "ColorScreenApplication.h"
#include "WorkspaceWindow.h"
#include "CoordinateOptimizationWorker.h"
#include "DetectScreenWorker.h"
#include "FinetuneWorker.h"
#include "FinetuneMisregisteredWorker.h"
#include "FocusAnalysisWorker.h"
#include "GeometryPanel.h"
#include "GeometrySolverWorker.h"
#include "ImageWidget.h"
#include "InitialSetupGuideDialog.h"
#include "NavigationView.h"
#include "RenderDialog.h"
#include "ScreenPanel.h"
#include "mesh.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QColorSpace>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget> // Added
#include "BacklightChartWidget.h"
#include "MeasureDialog.h"
#include "SlantedEdgeDialog.h"
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

Q_DECLARE_METATYPE(MainWindow::SolverRequestData)
Q_DECLARE_METATYPE(MainWindow::ColorOptimizerRequestData)
Q_DECLARE_METATYPE(colorscreen::render_parameters)
Q_DECLARE_METATYPE(colorscreen::render_type_parameters)
Q_DECLARE_METATYPE(colorscreen::scr_detect_parameters)
Q_DECLARE_METATYPE(colorscreen::scr_to_img_parameters)
Q_DECLARE_METATYPE(std::vector<colorscreen::point_t>)
Q_DECLARE_METATYPE(std::vector<colorscreen::color_match>)
Q_DECLARE_METATYPE(std::vector<colorscreen::solver_parameters::solver_point_t>)
Q_DECLARE_METATYPE(std::vector<colorscreen::solver_parameters::solver_point_t>*)
Q_DECLARE_METATYPE(std::shared_ptr<colorscreen::progress_info>)
Q_DECLARE_METATYPE(colorscreen::finetune_result)

namespace {

/** Return the application-level document manager when MainWindow is running
    inside the normal Color-Screen Qt application.  */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QCoreApplication::instance());
}

/** Numerical result of one document-owned measured-MTF model fit. */
struct MtfModelFitResult {
  colorscreen::mtf_parameters fitted;
  double objective = -1;
  std::size_t observations = 0;
  std::string error;
  bool cancelled = false;
  std::shared_ptr<colorscreen::progress_info> progress;
};

/** Fit an analytical MTF model without touching QObject/UI state. */
void runMtfModelFit(const colorscreen::mtf_parameters &input,
                    const colorscreen::mtf_estimation_options &options,
                    int flags, MtfModelFitResult *result,
                    colorscreen::progress_info *progress) {
  if (!result)
    return;
  result->fitted = input;
  if (progress && progress->pool_cancel()) {
    result->cancelled = true;
    return;
  }

  for (std::size_t measurement = 0; measurement < input.measurements.size();
       ++measurement) {
    if (!options.include_measurement_p(measurement))
      continue;
    const colorscreen::mtf_measurement &curve = input.measurements[measurement];
    for (std::size_t sample = 0; sample < curve.size(); ++sample)
      if (curve.get_freq(static_cast<int>(sample)) <= 0.5)
        ++result->observations;
  }

  try {
    colorscreen::mtf_parameters fitInput = input;
    const char *error = nullptr;
    result->objective = result->fitted.estimate_parameters(
        fitInput, options, nullptr, progress, &error, flags);
    if (error)
      result->error = error;
  } catch (const std::exception &exception) {
    result->error = exception.what();
  } catch (...) {
    result->error = "unexpected exception during MTF fitting";
  }

  result->cancelled = progress &&
                      (progress->cancelled() || progress->pool_cancel());
}

/** Return whether geometry-fit prerequisites/calibration differ between two
    document snapshots. Appearance/output-only final-plane orientation is
    deliberately excluded: it does not change the screen-to-scan fit. */
bool geometryFitInputsDiffer(const ParameterState &before,
                             const ParameterState &after) {
  const auto &a = before.scrToImg;
  const auto &b = after.scrToImg;
  return before.solver != after.solver ||
         !(a.center == b.center) || !(a.coordinate1 == b.coordinate1) ||
         !(a.coordinate2 == b.coordinate2) ||
         a.projection_distance != b.projection_distance ||
         a.tilt_x != b.tilt_x || a.tilt_y != b.tilt_y ||
         a.type != b.type || a.scanner_type != b.scanner_type ||
         !(a.lens_correction == b.lens_correction) ||
         a.mesh_trans != b.mesh_trans ||
         a.mesh_trans_is_scr_to_img != b.mesh_trans_is_scr_to_img;
}

/** Return whether CURRENT still uses the geometry produced by BASELINE.

    Solver-point edits intentionally do not count here: they make a fit stale,
    but the displayed mapping is still the old control-point-derived mapping.
    Final-plane orientation is appearance/output state and is likewise ignored. */
bool screenGeometryMatchesFitBaseline(const ParameterState &baseline,
                                      const ParameterState &current) {
  const auto &a = baseline.scrToImg;
  const auto &b = current.scrToImg;
  return a.center == b.center && a.coordinate1 == b.coordinate1 &&
         a.coordinate2 == b.coordinate2 &&
         a.projection_distance == b.projection_distance &&
         a.tilt_x == b.tilt_x && a.tilt_y == b.tilt_y &&
         a.type == b.type && a.scanner_type == b.scanner_type &&
         a.lens_correction == b.lens_correction &&
         a.mesh_trans == b.mesh_trans &&
         a.mesh_trans_is_scr_to_img == b.mesh_trans_is_scr_to_img;
}

/** Compare the inputs actually handed to optimize_color_model_colors().
    The fitted profiled_* values are outputs, not prerequisites. Final-plane
    orientation and output profile selection likewise do not change the sampled
    screen/scan colors used by the optimizer. */
bool profileCalibrationInputsDiffer(
    const MainWindow::ColorOptimizerRequestData &before,
    const MainWindow::ColorOptimizerRequestData &after) {
  if (before.spots.size() != after.spots.size())
    return true;
  for (std::size_t i = 0; i < before.spots.size(); ++i)
    if (!(before.spots[i] == after.spots[i]))
      return true;

  colorscreen::scr_to_img_parameters aScr = before.scrParams;
  colorscreen::scr_to_img_parameters bScr = after.scrParams;
  aScr.final_rotation = bScr.final_rotation = 0;
  aScr.final_mirror = bScr.final_mirror = false;
  aScr.final_angle = bScr.final_angle = 90;
  aScr.final_ratio = bScr.final_ratio = 1;
  if (aScr != bScr)
    return true;

  colorscreen::render_parameters a = before.rparams;
  colorscreen::render_parameters b = after.rparams;
  const colorscreen::render_parameters defaults;
  a.profiled_dark = b.profiled_dark = defaults.profiled_dark;
  a.profiled_red = b.profiled_red = defaults.profiled_red;
  a.profiled_green = b.profiled_green = defaults.profiled_green;
  a.profiled_blue = b.profiled_blue = defaults.profiled_blue;
  // optimize_color_model_colors() forces XYZ output internally.
  a.output_profile = b.output_profile =
      colorscreen::render_parameters::output_profile_xyz;
  return a != b;
}

/** Choose the scalar/BW focus model for scans that have no native RGB data,
    and for RGB files that merely store one monochrome scanner signal in
    three gain-scaled channels.  Test raw scan chromaticity rather than the
    reconstructed image layer: a monochrome additive-screen scan can still
    reconstruct strongly coloured scene areas, which are exactly the areas
    whose independent primary intensities we want for focus analysis. */
bool focusAnalysisUsesMonochromeInput(const colorscreen::image_data &scan) {
  if (!scan.has_rgb())
    return scan.has_grayscale_or_ir();
  if (scan.width <= 0 || scan.height <= 0)
    return false;

  const int xStep = std::max(1, scan.width / 32);
  const int yStep = std::max(1, scan.height / 32);
  const double darkThreshold
      = static_cast<double>(std::max(1, scan.maxval)) * 0.02;
  long double sumR = 0;
  long double sumG = 0;
  long double sumR2 = 0;
  long double sumG2 = 0;
  size_t samples = 0;
  for (int y = yStep / 2; y < scan.height; y += yStep)
    for (int x = xStep / 2; x < scan.width; x += xStep) {
      const colorscreen::image_data::pixel pixel = scan.get_rgb_pixel(x, y);
      const double total = static_cast<double>(pixel.r) + pixel.g + pixel.b;
      if (total <= 3 * darkThreshold)
        continue;
      const long double r = pixel.r / total;
      const long double g = pixel.g / total;
      sumR += r;
      sumG += g;
      sumR2 += r * r;
      sumG2 += g * g;
      ++samples;
    }
  if (samples < 32)
    return false;
  const long double meanR = sumR / samples;
  const long double meanG = sumG / samples;
  const long double variance
      = std::max((long double)0, sumR2 / samples - meanR * meanR)
        + std::max((long double)0, sumG2 / samples - meanG * meanG);
  return variance < (long double)5e-5;
}
} // namespace

/** Return the one status bar belonging to the current top-level window. */
QStatusBar *MainWindow::statusBar() const {
  return m_workspaceStatusBar ? m_workspaceStatusBar.data()
                              : QMainWindow::statusBar();
}

/** Return this document's private status bar, regardless of attachment. */
QStatusBar *MainWindow::standaloneStatusBar() const {
  return QMainWindow::statusBar();
}

/** Share STATUSBAR with every tab in the enclosing workspace window. */
void MainWindow::setWorkspaceStatusBar(QStatusBar *sharedStatusBar) {
  if (m_workspaceStatusBar.data() == sharedStatusBar)
    return;

  QStatusBar *localStatusBar = QMainWindow::statusBar();
  const QString localMessage = localStatusBar->currentMessage();
  m_workspaceStatusBar = sharedStatusBar;
  if (sharedStatusBar) {
    localStatusBar->hide();
    if (!localMessage.isEmpty())
      sharedStatusBar->showMessage(localMessage);
  }
}

/** Construct one independent image-document window.
   Registers Qt meta-types needed for cross-thread signal/slot connections,
   sets up the UI (panels, docks, toolbar, menus), assigns the document's
   recovery directory, creates persistent background worker threads for the
   geometry solver, color optimizer, and coordinate optimization, and restores
   the preferred window layout from QSettings.  */
MainWindow::MainWindow(const QString &recoveryDirectory, QWidget *parent)
    : QMainWindow(parent), m_recoveryDir(recoveryDirectory) {
  qRegisterMetaType<MainWindow::SolverRequestData>();
  qRegisterMetaType<MainWindow::ColorOptimizerRequestData>();
  qRegisterMetaType<colorscreen::render_parameters>();
  qRegisterMetaType<colorscreen::render_type_parameters>(
      "colorscreen::render_type_parameters");
  qRegisterMetaType<colorscreen::scr_detect_parameters>(
      "colorscreen::scr_detect_parameters");
  qRegisterMetaType<const char *>("const char*");
  qRegisterMetaType<colorscreen::scr_to_img_parameters>();
  qRegisterMetaType<std::vector<colorscreen::point_t>>();
  qRegisterMetaType<std::vector<colorscreen::color_match>>();
  qRegisterMetaType<std::vector<colorscreen::solver_parameters::solver_point_t>>();
  qRegisterMetaType<std::vector<colorscreen::solver_parameters::solver_point_t>*>();
  qRegisterMetaType<colorscreen::finetune_result>();
  qRegisterMetaType<std::shared_ptr<colorscreen::progress_info>>();
  m_undoStack = new QUndoStack(this);
  connect(m_undoStack, &QUndoStack::cleanChanged, this,
          [this]() { updateWindowTitle(); });

  setupUi();

  m_fileRenderController.configure(
      {[this]() { return m_closing; },
       [this](std::shared_ptr<colorscreen::progress_info> progress,
              const QString &title) {
         addUserVisibleProgress(std::move(progress), title);
       },
       [this](std::shared_ptr<colorscreen::progress_info> progress) {
         removeProgress(std::move(progress));
       },
       [this](const QString &outputPath, bool success, bool cancelled) {
         if (cancelled) {
           statusBar()->showMessage(tr("Render cancelled"), 3000);
         } else if (success) {
           statusBar()->showMessage(tr("Rendered to %1").arg(outputPath), 5000);
         } else {
           QMessageBox::critical(
               this, tr("Render Failed"),
               tr("Failed to render to:\n%1").arg(outputPath));
         }
       }});

  // Set up per-document recovery auto-save timer (30 seconds)
  m_recoveryTimer = new QTimer(this);
  m_recoveryTimer->setInterval(30000); // 30 seconds
  connect(m_recoveryTimer, &QTimer::timeout, this,
          &MainWindow::saveRecoveryState);
  m_recoveryTimer->start();

  loadRecentFiles();
  loadRecentParams();

  // Restore window state (position, size, splitters)
  restoreWindowState();

  // Initialize UI state
  updateUIFromState(getCurrentState());

  // Initialize Solver Worker
  m_solverThread = new QThread(this);
  m_solverWorker = new GeometrySolverWorker(m_scan);
  m_solverWorker->moveToThread(m_solverThread);
  connect(m_solverThread, &QThread::finished, m_solverWorker,
          &QObject::deleteLater);
  m_solverThread->start();

  connect(m_solverWorker, &GeometrySolverWorker::finished, this,
          &MainWindow::onSolverFinished);

  // Solver Queue connections
  connect(&m_solverQueue, &TaskQueue::triggerRender, this,
          &MainWindow::onTriggerSolve);
  connect(&m_solverQueue, &TaskQueue::progressStarted, this,
          &MainWindow::addProgress);
  connect(&m_solverQueue, &TaskQueue::progressFinished, this,
          &MainWindow::removeProgress);

  m_oneShotOperations.configure(
      this,
      {[this]() { return m_closing; },
       [this]() { dismissOneShotPrompts(); },
       [this](std::shared_ptr<colorscreen::progress_info> progress) {
         addProgress(progress);
       },
       [this](std::shared_ptr<colorscreen::progress_info> progress) {
         removeProgress(progress);
       },
       [this](std::shared_ptr<colorscreen::progress_info> progress,
              const QString &title) {
         removeProgress(progress);
         addUserVisibleProgress(progress, title);
       }});

  // Initialize Color Optimizer Worker
  m_colorOptimizerThread = new QThread(this);
  m_colorOptimizerWorker = new ColorOptimizerWorker(m_scan);
  m_colorOptimizerWorker->moveToThread(m_colorOptimizerThread);
  connect(m_colorOptimizerThread, &QThread::finished, m_colorOptimizerWorker,
          &QObject::deleteLater);
  m_colorOptimizerThread->start();

  connect(m_colorOptimizerWorker, &ColorOptimizerWorker::finished, this,
          &MainWindow::onColorOptimizerFinished);
  connect(&m_colorOptimizerQueue, &TaskQueue::triggerRender, this,
          &MainWindow::onTriggerColorOptimize);
  connect(&m_colorOptimizerQueue, &TaskQueue::progressStarted, this,
          &MainWindow::addProgress);
  connect(&m_colorOptimizerQueue, &TaskQueue::progressFinished, this,
          &MainWindow::removeProgress);

  updateWindowTitle();
}

/** Destroy the main window.
   Hides the window first to prevent stale accessibility events on macOS
   (QTBUG-71850).  Shuts down all background worker threads (solver, color
   optimizer) and waits for them to finish. Explicitly
   deletes the main splitter before member variables are destroyed so that
   panel callbacks don't access freed data.  Finally cleans up any floating
   dock widgets that may hold detached chart views.  */
MainWindow::~MainWindow() {
  // QUndoStack clears its commands in its destructor and emits state-change
  // signals, including cleanChanged.  If it is left as a QObject child, that
  // destructor runs from QObject::~QObject(), after MainWindow members such as
  // m_scan have already been destroyed.  The cleanChanged connection can then
  // re-enter updateWindowTitle() and read those dead members.  Destroy the undo
  // stack now, with signals blocked, while the complete MainWindow is alive.
  if (m_undoStack) {
    m_undoStack->blockSignals(true);
    delete m_undoStack;
    m_undoStack = nullptr;
  }

  // Hide window first to avoid invalid accessibility/focus events during
  // destruction This is a known workaround for MacOS crashes on exit
  // (QTBUG-71850)
  hide();

  // Destruction can also happen without a preceding closeEvent.  Make every
  // queued result stale, request cooperative cancellation, and join one-shot
  // workers before any document parameters or panels can disappear.
  m_closing = true;
  dismissOneShotPrompts();
  m_solverQueue.cancelAll();
  m_colorOptimizerQueue.cancelAll();
  m_oneShotOperations.cancelAll();
  m_fileRenderController.shutdown();
  m_progressController.cancelAll();
  // Result delivery from persistent workers is no longer useful once teardown
  // starts. Disconnect before joining one-shot workers because shutdown may
  // service blocking queued calls from those workers.
  if (m_solverWorker)
    disconnect(m_solverWorker, nullptr, this, nullptr);
  if (m_colorOptimizerWorker)
    disconnect(m_colorOptimizerWorker, nullptr, this, nullptr);

  m_backgroundThreads.shutdown(this);

  // Workers have QObject affinity to their dedicated threads. Their
  // QThread::finished connections perform deferred destruction there, after
  // queued setScan()/work calls have either run or been discarded. Deleting
  // them here on the GUI thread would race worker-thread member access.
  if (m_solverThread) {
    m_solverThread->quit();
    m_solverThread->wait();
    m_solverWorker = nullptr;
  }

  if (m_colorOptimizerThread) {
    m_colorOptimizerThread->quit();
    m_colorOptimizerThread->wait();
    m_colorOptimizerWorker = nullptr;
  }

  // Explicitly delete UI components that might access member variables
  // (callbacks) This ensures they are destroyed BEFORE members like m_rparams
  // or m_scan. Reclaim the document inspector first if another view presented
  // it, then delete the main splitter which owns the panels.
  if (m_rightColumn && m_mainSplitter &&
      m_rightColumn->parentWidget() != m_mainSplitter) {
    m_rightColumn->setParent(m_mainSplitter);
    m_mainSplitter->addWidget(m_rightColumn);
  }
  if (m_mainSplitter) {
    m_mainSplitter->setParent(nullptr); // Detach first
    delete m_mainSplitter;
    m_mainSplitter = nullptr;
  }

}

/** Build the entire main window UI.
   Creates the horizontal splitter (image widget | right column), the right
   column (navigation view + tab widget with all panels), the status bar with
   document progress reporting, and the signal/slot connections between
   panels, ImageWidget, NavigationView, and MainWindow. Detachable panel
   sections own their dock lifecycle in ParameterPanel; the document window
   no longer duplicates that presentation machinery. */
/** Restore the saved inspector stage when that stage is currently available.

    Specialist panels may be hidden until an image and its capture type are
    known. setCurrentIndex() deliberately ignores hidden tabs, so calling this
    both during construction and after image load preserves a hidden preference
    through temporary fallback without forcing an inapplicable panel visible. */
void MainWindow::restorePreferredInspectorPanel() {
  if (!m_configTabs)
    return;

  QSettings settings;
  const QString preferredPanel =
      settings.value(QStringLiteral("inspector/activePanel")).toString();
  const int preferredIndex = m_configTabs->indexOfKey(preferredPanel);
  if (preferredIndex >= 0)
    m_configTabs->setCurrentIndex(preferredIndex);
}

void MainWindow::setupUi() {

  m_mainSplitter = new QSplitter(Qt::Horizontal, this);
  m_mainSplitter->setObjectName(QStringLiteral("DocumentMainSplitter"));
  setCentralWidget(m_mainSplitter);

  // Left: Image Widget
  m_imageWidget = new ImageWidget(this);
  m_mainSplitter->addWidget(m_imageWidget);

  createMenus();

  // Connect ImageWidget progress signals
  connect(m_imageWidget, &ImageWidget::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_imageWidget, &ImageWidget::progressFinished, this,
          &MainWindow::removeProgress);

  // Right: Column
  m_rightColumn = new QWidget(this);
  m_rightColumn->setObjectName(QStringLiteral("DocumentInspector"));
  QVBoxLayout *rightLayout = new QVBoxLayout(m_rightColumn);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(4);

  // Keep the major processing stages and the document's current readiness
  // visible without forcing an operator to inspect several specialist tabs.
  // This deliberately reports only state that can be derived reliably from
  // existing document parameters; later revision counters can add explicit
  // Completed/Stale analysis states without changing this presentation.
  QFrame *workflowSummary = new QFrame(m_rightColumn);
  workflowSummary->setObjectName(QStringLiteral("WorkflowSummary"));
  workflowSummary->setFrameShape(QFrame::StyledPanel);
  workflowSummary->setFrameShadow(QFrame::Plain);
  auto *workflowLayout = new QVBoxLayout(workflowSummary);
  workflowLayout->setContentsMargins(6, 4, 6, 4);
  workflowLayout->setSpacing(1);

  auto *workflowToggle = new QToolButton(workflowSummary);
  workflowToggle->setObjectName(QStringLiteral("WorkflowSummaryToggle"));
  workflowToggle->setText(tr("Workflow"));
  workflowToggle->setCheckable(true);
  workflowToggle->setAutoRaise(true);
  workflowToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  workflowToggle->setToolTip(tr("Show or hide the document workflow summary."));
  workflowLayout->addWidget(workflowToggle);

  auto *workflowStages = new QLabel(
      tr("Capture › Sharpen › Image layer › Process › Register › Reconstruct › Color"),
      workflowSummary);
  workflowStages->setObjectName(QStringLiteral("WorkflowStages"));
  workflowStages->setWordWrap(true);
  QSizePolicy workflowStagesPolicy = workflowStages->sizePolicy();
  workflowStagesPolicy.setHorizontalPolicy(QSizePolicy::Ignored);
  workflowStages->setSizePolicy(workflowStagesPolicy);
  workflowStages->setMinimumWidth(0);
  QFont workflowStageFont = workflowStages->font();
  if (workflowStageFont.pointSizeF() > 1.0)
    workflowStageFont.setPointSizeF(workflowStageFont.pointSizeF() - 1.0);
  workflowStages->setFont(workflowStageFont);
  workflowStages->setToolTip(tr(
      "Color-Screen processing stages. The specialist tabs below remain in "
      "their existing beta order."));
  workflowLayout->addWidget(workflowStages);

  auto configureDynamicWorkflowLabel = [](QLabel *label) {
    label->setWordWrap(true);
    // Live registration recommendations must wrap inside the current inspector
    // allocation rather than changing the horizontal splitter size hint.
    QSizePolicy policy = label->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    label->setSizePolicy(policy);
    label->setMinimumWidth(0);
  };

  m_workflowProcessLabel = new QLabel(workflowSummary);
  m_workflowProcessLabel->setObjectName(
      QStringLiteral("WorkflowProcessSummary"));
  configureDynamicWorkflowLabel(m_workflowProcessLabel);
  workflowLayout->addWidget(m_workflowProcessLabel);

  m_workflowImageLayerLabel = new QLabel(workflowSummary);
  m_workflowImageLayerLabel->setObjectName(
      QStringLiteral("WorkflowImageLayerSummary"));
  configureDynamicWorkflowLabel(m_workflowImageLayerLabel);
  m_workflowImageLayerLabel->setToolTip(tr(
      "Scalar analysis/reconstruction image source. Native grayscale/infrared "
      "uses the captured scalar plane; simulated RGB uses the Image Layer mix."));
  workflowLayout->addWidget(m_workflowImageLayerLabel);

  m_workflowRegistrationLabel = new QLabel(workflowSummary);
  m_workflowRegistrationLabel->setObjectName(
      QStringLiteral("WorkflowRegistrationSummary"));
  configureDynamicWorkflowLabel(m_workflowRegistrationLabel);
  m_workflowRegistrationLabel->setToolTip(tr(
      "Geometry freshness is tracked for fits completed in this session. "
      "Loaded or manually entered geometry remains available but is not "
      "labelled current until it is fitted."));
  workflowLayout->addWidget(m_workflowRegistrationLabel);

  m_workflowCalibrationLabel = new QLabel(workflowSummary);
  m_workflowCalibrationLabel->setObjectName(
      QStringLiteral("WorkflowCalibrationSummary"));
  configureDynamicWorkflowLabel(m_workflowCalibrationLabel);
  workflowLayout->addWidget(m_workflowCalibrationLabel);

  m_workflowProfileLabel = new QLabel(workflowSummary);
  m_workflowProfileLabel->setObjectName(
      QStringLiteral("WorkflowProfileSummary"));
  configureDynamicWorkflowLabel(m_workflowProfileLabel);
  m_workflowProfileLabel->setProperty("workflowApplicable", false);
  m_workflowProfileLabel->setToolTip(tr(
      "Optional RGB screen-capture calibration. It fits a simple matrix "
      "profile from calibration spots and is not part of sharpening."));
  workflowLayout->addWidget(m_workflowProfileLabel);

  QFont workflowSectionFont = m_workflowProcessLabel->font();
  workflowSectionFont.setWeight(QFont::DemiBold);
  if (workflowSectionFont.pointSizeF() > 1.0)
    workflowSectionFont.setPointSizeF(workflowSectionFont.pointSizeF() - 0.5);
  m_workflowProcessLabel->setFont(workflowSectionFont);
  m_workflowImageLayerLabel->setFont(workflowSectionFont);
  m_workflowRegistrationLabel->setFont(workflowSectionFont);
  m_workflowCalibrationLabel->setFont(workflowSectionFont);
  m_workflowProfileLabel->setFont(workflowSectionFont);

  QWidget *workflowNextRow = new QWidget(workflowSummary);
  auto *workflowNextLayout = new QHBoxLayout(workflowNextRow);
  workflowNextLayout->setContentsMargins(0, 0, 0, 0);
  workflowNextLayout->setSpacing(6);

  m_workflowNextStepLabel = new QLabel(workflowNextRow);
  m_workflowNextStepLabel->setObjectName(
      QStringLiteral("WorkflowNextStepSummary"));
  configureDynamicWorkflowLabel(m_workflowNextStepLabel);
  QFont nextStepFont = m_workflowNextStepLabel->font();
  nextStepFont.setBold(true);
  m_workflowNextStepLabel->setFont(nextStepFont);
  workflowNextLayout->addWidget(m_workflowNextStepLabel, 1);

  m_workflowNextStepButton =
      new QPushButton(tr("Open stage"), workflowNextRow);
  m_workflowNextStepButton->setObjectName(
      QStringLiteral("WorkflowOpenStageButton"));
  m_workflowNextStepButton->setSizePolicy(QSizePolicy::Maximum,
                                          QSizePolicy::Fixed);
  m_workflowNextStepButton->hide();
  connect(m_workflowNextStepButton, &QPushButton::clicked, this, [this]() {
    if (!m_configTabs || !m_workflowNextStepButton)
      return;
    const QString key =
        m_workflowNextStepButton->property("targetPanelKey").toString();
    const int index = m_configTabs->indexOfKey(key);
    if (index < 0)
      return;
    m_configTabs->setCurrentIndex(index);
    if (m_configTabs->currentIndex() != index)
      return;

    // This is explicit user navigation just like clicking the inspector tab.
    // Keep the semantic preference in sync without relying on numeric indices.
    QSettings settings;
    settings.setValue(QStringLiteral("inspector/activePanel"), key);
    updateWorkflowSummary();
  });
  workflowLayout->addWidget(workflowNextRow);

  const bool workflowExpanded =
      QSettings().value(QStringLiteral("workflowSummaryExpanded"), true).toBool();
  auto setWorkflowExpanded =
      [this, workflowToggle, workflowStages, workflowNextRow](bool expanded) {
        workflowToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        workflowStages->setVisible(expanded);
        m_workflowProcessLabel->setVisible(expanded);
        m_workflowImageLayerLabel->setVisible(expanded);
        m_workflowRegistrationLabel->setVisible(expanded);
        m_workflowCalibrationLabel->setVisible(expanded);
        m_workflowProfileLabel->setVisible(
            expanded &&
            m_workflowProfileLabel->property("workflowApplicable").toBool());
        workflowNextRow->setVisible(expanded);
        workflowToggle->setToolTip(
            expanded ? tr("Hide the document workflow summary.")
                     : tr("Show the document workflow summary."));
        workflowToggle->parentWidget()->updateGeometry();
      };
  workflowToggle->setChecked(workflowExpanded);
  setWorkflowExpanded(workflowExpanded);
  connect(workflowToggle, &QToolButton::toggled, this,
          [setWorkflowExpanded](bool expanded) {
            setWorkflowExpanded(expanded);
            QSettings settings;
            settings.setValue(QStringLiteral("workflowSummaryExpanded"),
                              expanded);
          });

  QSplitter *rightSplitter = new QSplitter(Qt::Vertical, m_rightColumn);
  rightSplitter->setObjectName(QStringLiteral("DocumentInspectorSplitter"));
  rightSplitter->setChildrenCollapsible(false);
  rightLayout->addWidget(rightSplitter, 1);

  // Top Right: Navigation View
  m_navigationView = new NavigationView(this);
  m_navigationView->setObjectName(QStringLiteral("InspectorNavigation"));
  m_navigationView->setMinimumHeight(200);
  rightSplitter->addWidget(m_navigationView);

  // Navigation controls whichever ordinary view is currently presenting this
  // document's inspector. The source side of viewStateChanged is switched by
  // setInspectorImageWidget().
  connect(m_navigationView, &NavigationView::zoomChanged, this,
          [this](double zoom) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setZoom(zoom);
          });
  connect(m_navigationView, &NavigationView::panChanged, this,
          [this](double x, double y) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setPan(x, y);
          });

  // Connect NavigationView progress signals
  connect(m_navigationView, &NavigationView::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_navigationView, &NavigationView::progressFinished, this,
          &MainWindow::removeProgress);

  // The document inspector remains one dockable ownership unit. Keep the
  // navigator independently resizable at the top, while the foldable workflow
  // guide and processing tabs form one stable controls region below. Other
  // diagnostic docks can still share the QMainWindow dock strip.
  auto *controlsArea = new QWidget(rightSplitter);
  controlsArea->setObjectName(QStringLiteral("DocumentControlsArea"));
  auto *controlsLayout = new QVBoxLayout(controlsArea);
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(4);
  controlsLayout->addWidget(workflowSummary);
  rightSplitter->addWidget(controlsArea);
  rightSplitter->setStretchFactor(0, 0);
  rightSplitter->setStretchFactor(1, 1);

  // Bottom Right: Tabs
  m_configTabs = new MultiLineTabWidget(controlsArea);

  // Create Sharpness Panel
  MtfCalibrationCallbacks mtfCalibration;
  mtfCalibration.summary = [this]() { return mtfCalibrationSummary(); };
  mtfCalibration.fitAvailable = [this]() { return !m_mtfFit.running; };
  mtfCalibration.fitRequested =
      [this](const ParameterState &baseline,
             const colorscreen::mtf_parameters &input,
             const colorscreen::mtf_estimation_options &options, int flags,
             QWidget *resultParent) {
        return requestMtfModelFit(baseline, input, options, flags, resultParent);
      };
  m_sharpnessPanel =
      new SharpnessPanel([this]() { return getCurrentState(); },
                         [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                         [this]() { return m_scan; }, std::move(mtfCalibration),
                         this);

  // Create Screen Panel
  m_screenPanel =
      new ScreenPanel([this]() { return getCurrentState(); },
                      [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                      [this]() { return m_scan; }, this);

  // Create Color Panel (after Sharpness)
  m_contactCopyPanel = new ContactCopyPanel(
      [this]() { return getCurrentState(); },
      [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
      [this]() { return m_scan; }, this);

  m_colorPanel =
      new ColorPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                     [this]() { return m_scan; }, this);

  // Create Profile Panel
  m_profilePanel =
      new ProfilePanel([this]() { return getCurrentState(); },
                       [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                       [this]() { return m_scan; }, this);

  // Create Tiles Panel
  m_tilesPanel =
      new TilesPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                     [this]() { return m_scan; }, this);

  // Create Image Layer Panel
  m_imageLayerPanel =
      new ImageLayerPanel([this]() { return getCurrentState(); },
                          [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                          [this]() { return m_scan; }, this);

  // Connect Progress Signals from Panels
  connect(m_sharpnessPanel, &SharpnessPanel::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_sharpnessPanel, &SharpnessPanel::progressFinished, this,
          &MainWindow::removeProgress);

  connect(m_screenPanel, &ScreenPanel::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_screenPanel, &ScreenPanel::progressFinished, this,
          &MainWindow::removeProgress);
  connect(m_screenPanel, &ScreenPanel::autodetectRequested, this,
          &MainWindow::onAutodetectScreen);
  connect(m_screenPanel, &ScreenPanel::alternateColorsRequested, this,
          &MainWindow::onAlternateColorsRequested);

  connect(m_colorPanel, &ColorPanel::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_colorPanel, &ColorPanel::progressFinished, this,
          &MainWindow::removeProgress);

  m_configTabs->setObjectName("ConfigTabs");











  connect(m_sharpnessPanel, &SharpnessPanel::focusAnalysisRequested, this,
          &MainWindow::onFocusAnalysisRequested);
  connect(m_sharpnessPanel, &SharpnessPanel::findFocusAreasRequested, this,
          &MainWindow::onFindFocusAreasRequested);
  connect(m_sharpnessPanel, &SharpnessPanel::analyzeFocusAreasRequested, this,
          &MainWindow::onAnalyzeFocusAreasRequested);
  connect(m_sharpnessPanel,
          &SharpnessPanel::openSlantedEdgeReferenceRequested, this,
          [this]() {
            if (ColorScreenApplication *application = documentApplication())
              application->openSlantedEdgeReference(this, this);
          });
  connect(m_sharpnessPanel, &SharpnessPanel::measureMtfRequested, this,
          &MainWindow::onMeasureMtfRequested);
  connect(m_sharpnessPanel, &SharpnessPanel::mtfMeasurementSelected, this,
          [this](int index) {
            m_selectedMtfMeasurement = index;
            updateMtfMeasurementOverlay(false);
          });
  connect(m_sharpnessPanel, &SharpnessPanel::mtfMeasurementLocateRequested, this,
          [this](int index) {
            m_selectedMtfMeasurement = index;
            updateMtfMeasurementOverlay(true);
          });


  // Create Digital Capture Panel
  m_capturePanel =
      new CapturePanel([this]() { return getCurrentState(); },
                       [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                       [this]() { return m_scan; },
                       [this]() { reloadCurrentImageWithDemosaic(); },
                       this);

  // Create Geometry Panel
  m_geometryPanel =
      new GeometryPanel([this]() { return getCurrentState(); },
                        [this](const ParameterState &s, const QString &desc,
                             const QString &parameterKey) {
                         changeParameters(s, desc, parameterKey);
                       },
                        [this]() { return m_scan; }, this);








  m_configTabs->addTab(m_capturePanel, "Digital capture",
                       QStringLiteral("digital_capture"));
  m_configTabs->addTab(m_tilesPanel, "Tiles", QStringLiteral("tiles"));
  connect(m_capturePanel, &CapturePanel::cropRequested, this,
          &MainWindow::onCropRequested);
  connect(m_capturePanel, &CapturePanel::measureRequested, this,
          &MainWindow::onMeasureRequested);
  connect(m_capturePanel, &CapturePanel::flatFieldRequested, this,
          &MainWindow::onFlatFieldRequested);

  connect(m_imageWidget, &ImageWidget::interactionModeChanged, this,
          [this](ImageWidget::InteractionMode mode) {
            // Update toolbar actions to match the widget mode
            if (m_panAction) {
              QSignalBlocker blocker(m_panAction);
              m_panAction->setChecked(mode == ImageWidget::PanMode);
            }
            if (m_selectAction) {
              QSignalBlocker blocker(m_selectAction);
              m_selectAction->setChecked(mode == ImageWidget::SelectMode);
            }
            if (m_addPointAction) {
              QSignalBlocker blocker(m_addPointAction);
              m_addPointAction->setChecked(mode == ImageWidget::AddPointMode);
            }
            if (m_setCenterAction) {
              QSignalBlocker blocker(m_setCenterAction);
              m_setCenterAction->setChecked(mode == ImageWidget::SetCenterMode);
            }
            updateScreenCoordinateToolPresentation();

            if (m_capturePanel) {
              m_capturePanel->setCropChecked(mode == ImageWidget::CropMode);
            }
            if (!m_switchingInspectorImage &&
                sender() == inspectorImageWidget() &&
                mode != ImageWidget::GenericAreaMode &&
                m_areaSelectionCallback) {
              // If the active view switches tool during selection, abandon the
              // pending callback. Merely moving the inspector to another view
              // must not cancel the operation.
              m_areaSelectionCallback = nullptr;
              if (m_imageLayerPanel) {
                m_imageLayerPanel->setNeutralAreaChecked(false);
                m_imageLayerPanel->setInfraredAreaChecked(false);
                m_imageLayerPanel->setDarkAreaChecked(false);
                m_imageLayerPanel->updateUI();
              }
              if (m_colorPanel) {
                m_colorPanel->setNeutralAreaChecked(false);
                m_colorPanel->setAutoLevelsChecked(false);
                m_colorPanel->updateUI();
              }
            }
          });
  connect(m_imageWidget, &ImageWidget::distanceMeasured, this, &MainWindow::onDistanceMeasured);
  m_configTabs->addTab(m_sharpnessPanel, "Sharpness",
                       QStringLiteral("sharpness"));
  m_configTabs->addTab(m_imageLayerPanel, "Image Layer",
                       QStringLiteral("image_layer"));
  m_configTabs->addTab(m_contactCopyPanel, "Contact copy",
                       QStringLiteral("contact_copy"));
  m_configTabs->addTab(m_screenPanel, "Screen", QStringLiteral("screen"));
  m_configTabs->addTab(m_geometryPanel, "Geometry",
                       QStringLiteral("geometry"));
  m_configTabs->addTab(m_colorPanel, "Color", QStringLiteral("color"));
  m_configTabs->addTab(m_profilePanel, "Profile calibration",
                       QStringLiteral("profile"));

  m_configTabs->setTabToolTip(0, "Capture — configure demosaicking, resolution, "
                                 "sensor parameters, and image gamma.");
  m_configTabs->setTabToolTip(1, "Capture — manage per-tile adjustments "
                                 "(exposure, dark point) for stitched images.");
  m_configTabs->setTabToolTip(2, "Capture/Reconstruct — configure sharpening "
                                 "algorithms and MTF models.");
  m_configTabs->setTabToolTip(3, "Process — choose or synthesize the analysis "
                                 "image layer, including infrared/dark mixing.");
  m_configTabs->setTabToolTip(4, "Process — simulate photographic contact "
                                 "printing on glass plate emulsions using the "
                                 "H&D curve.");
  m_configTabs->setTabToolTip(5, "Process/Register — select the physical color "
                                 "screen type, detect it, and configure "
                                 "reconstruction.");
  m_configTabs->setTabToolTip(6,
                              "Register — align screen and image geometry, "
                              "including rotation, tilt, and lens correction.");
  m_configTabs->setTabToolTip(7, "Color — adjust white balance, black point, "
                                 "presaturation, and dye model parameters.");
  m_configTabs->setTabToolTip(
      8, "Color calibration — optimize a profile and manage calibration spots.");

  // The preferred stage is application presentation state, not document state.
  // Persist a semantic key rather than a numeric index so later tab reordering
  // or translated labels cannot change its meaning. Only explicit user clicks
  // update the preference; programmatic fallback from a hidden tab does not
  // replace the user's preferred stage.
  restorePreferredInspectorPanel();
  connect(m_configTabs, &MultiLineTabWidget::tabActivated, this,
          [this](int index) {
            const QString key = m_configTabs->tabKey(index);
            if (key.isEmpty())
              return;
            QSettings settings;
            settings.setValue(QStringLiteral("inspector/activePanel"), key);
          });

  connect(m_profilePanel, &ProfilePanel::optimizeColorRequested, this,
          &MainWindow::onColorOptimizeRequested);
  connect(m_profilePanel, &ProfilePanel::addSpotModeRequested, this,
          &MainWindow::onAddSpotModeRequested);
  connect(m_profilePanel, &ProfilePanel::showProfileSpotsChanged, this,
          [this](bool show) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setShowProfileSpots(show);
          });

  // ImageWidget::pointAdded is routed to onPointAdded; profile spot
  // handling is done there when m_addingProfileSpot is true.
  controlsLayout->addWidget(m_configTabs, 1);

  // Register panels for updates
  m_panels.push_back(m_capturePanel);
  m_panels.push_back(m_tilesPanel);
  m_panels.push_back(m_sharpnessPanel);
  m_panels.push_back(m_imageLayerPanel);
  m_panels.push_back(m_screenPanel);
  m_panels.push_back(m_geometryPanel);
  m_panels.push_back(m_contactCopyPanel);
  m_panels.push_back(m_colorPanel);
  m_panels.push_back(m_profilePanel);

  connect(
      m_imageLayerPanel, &ImageLayerPanel::neutralAreaRequested, this,
      [this]() {
        runAreaComputation(
            tr("Select neutral area for simulated mixing"),
            tr("Set simulated mix parameters by neutral area"),
            [this]() { m_imageLayerPanel->setNeutralAreaEnabled(false); },
            [this]() { m_imageLayerPanel->setNeutralAreaChecked(false); },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              s.rparams.auto_mix_weights(scan, s.scrToImg, area, p);
            });
      });

  connect(
      m_imageLayerPanel, &ImageLayerPanel::infraredAreaRequested, this,
      [this]() {
        runAreaComputation(
            tr("Select area to set simulated mix parameters using infrared"),
            tr("Set simulated mix parameters using infrared"),
            [this]() { m_imageLayerPanel->setInfraredAreaEnabled(false); },
            [this]() { m_imageLayerPanel->setInfraredAreaChecked(false); },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              s.rparams.auto_mix_weights_using_ir(scan, s.scrToImg, area, p);
            });
      });

  connect(
      m_imageLayerPanel, &ImageLayerPanel::darkAreaRequested, this, [this]() {
        runAreaComputation(
            tr("Select dark area for simulated mixing"),
            tr("Set dark mix parameters by area"),
            [this]() { m_imageLayerPanel->setDarkAreaEnabled(false); },
            [this]() { m_imageLayerPanel->setDarkAreaChecked(false); },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              s.rparams.auto_mix_dark(scan, s.scrToImg, area, p);
            });
      });

  connect(m_colorPanel, &ColorPanel::neutralAreaRequested, this, [this]() {
    runAreaComputation(
        tr("Select neutral area for white balance"),
        tr("Set white balance by neutral area"),
        [this]() { m_colorPanel->setNeutralAreaEnabled(false); },
        [this]() { m_colorPanel->setNeutralAreaChecked(false); },
        [](ParameterState &s, colorscreen::image_data &scan,
           const colorscreen::int_image_area &area,
           colorscreen::progress_info *p) {
          s.rparams.auto_white_balance(scan, s.scrToImg, area, p);
        });
  });

  connect(m_colorPanel, &ColorPanel::autoLevelsRequested, this, [this]() {
    runAreaComputation(
        tr("Select area for auto levels"),
        tr("Set auto levels by area"),
        [this]() { m_colorPanel->setAutoLevelsEnabled(false); },
        [this]() { m_colorPanel->setAutoLevelsChecked(false); },
        [](ParameterState &s, colorscreen::image_data &scan,
           const colorscreen::int_image_area &area,
           colorscreen::progress_info *p) {
          s.rparams.auto_dark_brightness(scan, s.scrToImg, area, p);
        });
  });

  // Already pushed above

  // Connect Adaptive Sharpening signal from Sharpness Panel
  connect(m_sharpnessPanel, &SharpnessPanel::adaptiveSharpeningRequested, this,
          &MainWindow::onAdaptiveSharpeningRequested);

  // Link Geometry Panel signals
  connect(m_geometryPanel, &GeometryPanel::optimizeRequested, this,
          &MainWindow::onOptimizeGeometry);
  connect(m_geometryPanel, &GeometryPanel::automaticallyAddPointsRequested, this,
          &MainWindow::onAutomaticallyAddPointsRequested);
  connect(m_geometryPanel, &GeometryPanel::automaticallyAddPointsInAreaRequested, this,
          &MainWindow::onAutomaticallyAddPointsInAreaRequested);
  connect(m_geometryPanel, &GeometryPanel::nonlinearToggled, this,
          &MainWindow::onNonlinearToggled);
  connect(m_geometryPanel, &GeometryPanel::centerOnRequested, this,
          [this](const colorscreen::point_t &point) {
            if (ImageWidget *image = inspectorImageWidget())
              image->centerOn(point);
          });

  // Connect visualization sliders to the view currently controlled by the
  // shared document inspector.
  connect(m_geometryPanel, &GeometryPanel::heatmapToleranceChanged, this,
          [this](double value) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setHeatmapTolerance(value);
          });
  connect(m_geometryPanel, &GeometryPanel::exaggerateChanged, this,
          [this](double value) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setExaggerate(value);
          });
  connect(m_geometryPanel, &GeometryPanel::maxArrowLengthChanged, this,
          [this](double value) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setMaxArrowLength(value);
          });
  connect(m_geometryPanel, &GeometryPanel::autodetectCoordinatesRequested, this,
          &MainWindow::onAutodetectCoordinatesRequested);
  connect(m_geometryPanel, &GeometryPanel::optimizeCoordinatesRequested, this,
          &MainWindow::onOptimizeCoordinatesRequested);

  // Synchronization for Registration Points visibility
  m_registrationPointsAction->setChecked(
      m_imageWidget->registrationPointsVisible());

  // Link View menu -> the ordinary view currently controlled by the inspector.
  connect(m_registrationPointsAction, &QAction::toggled, this,
          [this](bool show) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setShowRegistrationPoints(show);
          });

  // Link ImageWidget -> View menu (to keep it in sync if changed elsewhere)
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          m_registrationPointsAction, &QAction::setChecked);

  // Link ImageWidget -> GeometryPanel checkbox
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          m_geometryPanel, &GeometryPanel::setRegistrationPointsVisible);

  // Connect fullscreen exit signal
  connect(m_imageWidget, &ImageWidget::exitFullscreenRequested, this,
          &MainWindow::toggleFullscreen);

  // Link GeometryPanel checkbox -> ImageWidget
  QCheckBox *regBox = m_geometryPanel->registrationPointsCheckBox();
  if (regBox) {
    connect(regBox, &QCheckBox::toggled, this, [this](bool show) {
      if (ImageWidget *image = inspectorImageWidget())
        image->setShowRegistrationPoints(show);
    });
    QSignalBlocker blocker(regBox);
    regBox->setChecked(m_imageWidget->registrationPointsVisible());
  }

  // Auto solver trigger
  connect(m_imageWidget, &ImageWidget::pointManipulationStarted, this,
          &MainWindow::onPointManipulationStarted);
  connect(m_imageWidget, &ImageWidget::pointsChanged, this,
          &MainWindow::maybeTriggerAutoSolver);
  connect(m_imageWidget, &ImageWidget::pointsChanged, this,
          [this]() { emit documentStateChanged(); });

  // Nonlinear corrections checkbox
  m_geometryPanel->setNonlinearChecked(m_scrToImgParams.mesh_trans != nullptr);

  // Sync Auto Optimize checkbox with GeometryPanel
  QCheckBox *autoSolverBox = m_geometryPanel->autoOptimizeCheckBox();
  if (autoSolverBox && m_autoOptimizeAction) {
    // GeometryPanel -> Menu
    connect(autoSolverBox, &QCheckBox::toggled, m_autoOptimizeAction,
            &QAction::setChecked);
    // Menu -> GeometryPanel
    connect(m_autoOptimizeAction, &QAction::toggled, autoSolverBox,
            &QCheckBox::setChecked);
    // Initialize state
    m_autoOptimizeAction->setChecked(autoSolverBox->isChecked());
  }

  m_mainSplitter->addWidget(m_rightColumn);

  // Set initial sizes (approx 80% for image, 20% for right panel)
  m_mainSplitter->setStretchFactor(0, 9);
  m_mainSplitter->setStretchFactor(1, 1);

  // Status bar and document-owned progress presentation.
  QStatusBar *statusBar = new QStatusBar(this);
  setStatusBar(statusBar);

  DocumentProgressController::Labels progressLabels;
  progressLabels.stop = tr("Stop");
  progressLabels.cancel = tr("Cancel");
  progressLabels.stopping = tr("Stopping...");
  progressLabels.cancelling = tr("Cancelling...");
  progressLabels.working = tr("Working...");

  DocumentProgressController::Callbacks progressCallbacks;
  progressCallbacks.confirmTermination =
      [this](const std::shared_ptr<colorscreen::progress_info> &info,
             ProgressAction action) {
        if (action != ProgressAction::Cancel ||
            !m_fileRenderController.ownsProgress(info))
          return true;
        const auto result = QMessageBox::question(
            this, tr("Cancel Rendering"), tr("Cancel the current rendering?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return result == QMessageBox::Yes;
      };
  progressCallbacks.releaseFocus =
      [this](QWidget *row) { releaseUserVisibleProgressFocus(row); };
  progressCallbacks.transientVisibilityChanged =
      [this](bool visible) { emit transientProgressVisibilityChanged(visible); };
  progressCallbacks.userVisibleVisibilityChanged =
      [this](bool visible) {
        emit userVisibleProgressVisibilityChanged(visible);
      };
  m_progressController.initialize(this, statusBar, std::move(progressLabels),
                                  std::move(progressCallbacks));

  createToolbar(); // Initialize toolbar

  // Add actions to ImageWidget so shortcuts work when it is a detached fullscreen window
  // This must be done AFTER createMenus and createToolbar, since both initialize actions.
  if (m_panAction) m_imageWidget->addAction(m_panAction);
  if (m_selectAction) m_imageWidget->addAction(m_selectAction);
  if (m_addPointAction) m_imageWidget->addAction(m_addPointAction);
  if (m_setCenterAction) m_imageWidget->addAction(m_setCenterAction);
  if (m_zoomInAction) m_imageWidget->addAction(m_zoomInAction);
  if (m_zoomOutAction) m_imageWidget->addAction(m_zoomOutAction);
  if (m_zoom100Action) m_imageWidget->addAction(m_zoom100Action);
  if (m_zoomFitAction) m_imageWidget->addAction(m_zoomFitAction);
  if (m_fullscreenAction) m_imageWidget->addAction(m_fullscreenAction);
  if (m_selectAllAction) m_imageWidget->addAction(m_selectAllAction);
  if (m_deselectAllAction) m_imageWidget->addAction(m_deselectAllAction);
  if (m_deleteSelectedAction) m_imageWidget->addAction(m_deleteSelectedAction);
  if (m_pruneMisplacedAction) m_imageWidget->addAction(m_pruneMisplacedAction);
  if (m_rotateLeftAction) m_imageWidget->addAction(m_rotateLeftAction);
  if (m_rotateRightAction) m_imageWidget->addAction(m_rotateRightAction);

  // Note: exploreModeAction and mode shortcuts (1-0) are added dynamically in createToolbar/createModeShortcuts
  // However, we should also add them. We will do this where they are created.

  setInspectorImageWidget(m_imageWidget);
}

// Helper to manually load and recolor symbolic icons on Windows where
// auto-recoloring fails Helper to manually load and recolor symbolic icons
/** Load an SVG icon by name with cross-platform support.
   On Windows, searches Adwaita icon directories and re-colors the SVG to
   white for visibility on dark toolbars, generating pixmaps at multiple
   DPI-aware sizes.  On other platforms and for Qt resource paths (":/"),
   delegates to QIcon or QIcon::fromTheme.  */
QIcon getSymbolicIcon(const QString &name) {
  // If it is a resource, use it directly (Qt handles SVG scaling properly)
  // We assume resources are already correct color (white)
  if (name.startsWith(":/")) {
    return QIcon(name);
  }

  QString path;
#ifdef Q_OS_WIN
  // Fallback logic for Windows specific paths if needed,
  // but mostly we should use resources or standard theme.
  // Keeping existing logic for finding files if they are not resources.
  static QStringList subdirs = {"actions",    "devices", "places",
                                "status",     "ui",      "legacy",
                                "categories", "apps",    "mimetypes"};
  QString appDir = QCoreApplication::applicationDirPath();
  QStringList bases = {appDir + "/share/icons/Adwaita/symbolic",
                       appDir + "/../share/icons/Adwaita/symbolic"};

  for (const auto &base : bases) {
    for (const auto &subdir : subdirs) {
      QString tryPath = base + "/" + subdir + "/" + name + ".svg";
      if (QFile::exists(tryPath)) {
        path = tryPath;
        break;
      }
      tryPath = base + "/" + subdir + "/" + name + ".symbolic.svg";
      if (QFile::exists(tryPath)) {
        path = tryPath;
        break;
      }
    }
    if (!path.isEmpty())
      break;
  }
#endif

  if (!path.isEmpty()) {
    QIcon icon;
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
      return QIcon::fromTheme(name);

    // Generate multiple sizes for DPI
    QList<int> sizes = {16, 24, 32, 48, 64, 96, 128};
    for (int size : sizes) {
      QPixmap pix(size, size);
      pix.fill(Qt::transparent);

      QPainter p(&pix);
      renderer.render(&p);

      // Recolor to white
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(pix.rect(), Qt::white);
      p.end();

      icon.addPixmap(pix);
    }
    return icon;
  }

  return QIcon::fromTheme(name); // Fallback to system theme
}

/** Create the main toolbar.
   Adds the render mode combo box, color (IR/RGB) checkbox, interaction tool
   actions (Pan, Select, Add Point, Set Center) as a mutually exclusive
   QActionGroup, zoom and rotation buttons, and the registration-specific
   tools (lock coordinates, optimize coordinates).  Connects each tool
   action to set the corresponding ImageWidget interaction mode and wires
   up explore mode shortcut (Ctrl+M).  */
void MainWindow::createToolbar() {
  m_toolbar = addToolBar("Main Toolbar");
  m_toolbar->setObjectName("MainToolbar"); // Fix state saving warning
  m_toolbar->setMovable(false);

  QLabel *modeLabel = new QLabel("Mode: ", m_toolbar);
  m_toolbar->addWidget(modeLabel);

  m_modeComboBox = new QComboBox(m_toolbar);
  m_modeComboBox->setMinimumWidth(150);
  m_toolbar->addWidget(m_modeComboBox);
  connect(m_modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::onModeChanged);

  // Color checkbox (moved here, right after Mode)
  m_colorCheckBox = new QCheckBox("Color", m_toolbar);
  m_colorCheckBox->setEnabled(false);
  connect(m_colorCheckBox, &QCheckBox::toggled, this,
          &MainWindow::onColorCheckBoxChanged);
  m_colorCheckBoxAction = m_toolbar->addWidget(m_colorCheckBox);

  m_toolbar->addWidget(new QLabel(tr("Coordinates: "), m_toolbar));
  m_coordinateComboBox = new QComboBox(m_toolbar);
  m_coordinateComboBox->setObjectName(QStringLiteral("CoordinateSpaceCombo"));
  m_coordinateComboBox->setToolTip(
      tr("Choose raw scan geometry or geometrically corrected screen geometry"));
  m_toolbar->addWidget(m_coordinateComboBox);
  connect(m_coordinateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            ImageWidget *image = inspectorImageWidget();
            if (index < 0 || !image)
              return;
            const auto coordinates = static_cast<colorscreen::render_coordinate_space>(
                m_coordinateComboBox->itemData(index).toInt());
            if (!image->setCoordinateSpace(coordinates)) {
              updateCoordinateSpaceControls();
              return;
            }
            updateCoordinateSpaceControls();
            if (m_navigationView)
              m_navigationView->setCoordinateSpace(image->coordinateSpace());
            syncInspectorViewActions();
          });

  m_finalRotationLabelAction = m_toolbar->addWidget(
      new QLabel(tr("Final rotation: "), m_toolbar));
  m_finalRotationSpinBox = new QDoubleSpinBox(m_toolbar);
  m_finalRotationSpinBox->setObjectName(QStringLiteral("FinalRotationSpin"));
  m_finalRotationSpinBox->setRange(-180.0, 180.0);
  m_finalRotationSpinBox->setDecimals(2);
  m_finalRotationSpinBox->setSingleStep(0.1);
  m_finalRotationSpinBox->setSuffix(QStringLiteral("°"));
  m_finalRotationSpinBox->setToolTip(
      tr("Continuous rotation of the final-coordinate image; saved in the parameter file"));
  m_finalRotationSpinAction = m_toolbar->addWidget(m_finalRotationSpinBox);
  connect(m_finalRotationSpinBox, &QDoubleSpinBox::valueChanged, this,
          [this](double degrees) {
            ImageWidget *image = inspectorImageWidget();
            if (image && image->coordinateSpace() ==
                             colorscreen::render_final_coordinates)
              setDocumentFinalRotation(degrees);
          });
  updateCoordinateSpaceControls();

  m_toolbar->addSeparator();

  // Interaction Tools - Pan in View group
  QActionGroup *toolGroup = new QActionGroup(this);

  m_panAction = new QAction(getSymbolicIcon(":/icons/hand.svg"), "Pan", this);
  m_panAction->setActionGroup(toolGroup);
  m_panAction->setCheckable(true);
  m_panAction->setChecked(true);
  m_panAction->setToolTip("Pan Tool (P)");
  m_panAction->setShortcut(QKeySequence("P"));
  m_panAction->setShortcutContext(Qt::WindowShortcut);
  m_toolbar->addAction(m_panAction);

  // Zoom controls
  m_toolbar->addAction(m_zoomInAction);
  m_toolbar->addAction(m_zoomOutAction);
  m_toolbar->addAction(m_zoom100Action);
  m_toolbar->addAction(m_zoomFitAction);

  // Scan quarter-turn actions are shared with the View menu.  Final mode
  // hides them and exposes the continuous degree control above instead.
  if (m_rotateLeftAction) {
    m_rotateLeftAction->setIcon(getSymbolicIcon(":/icons/rotate-left.svg"));
    m_toolbar->addAction(m_rotateLeftAction);
  }
  if (m_rotateRightAction) {
    m_rotateRightAction->setIcon(getSymbolicIcon(":/icons/rotate-right.svg"));
    m_toolbar->addAction(m_rotateRightAction);
  }
  if (m_mirrorAction)
    m_toolbar->addAction(m_mirrorAction);

  // === REGISTRATION GROUP ===
  QAction *regSeparator = m_toolbar->addSeparator();
  m_registrationActions.append(regSeparator);

  m_selectAction =
      new QAction(getSymbolicIcon(":/icons/arrow.svg"), "Select", this);
  m_selectAction->setActionGroup(toolGroup);
  m_selectAction->setCheckable(true);
  m_selectAction->setToolTip("Select Tool (S)");
  m_selectAction->setShortcut(QKeySequence("S"));
  m_selectAction->setShortcutContext(Qt::WindowShortcut);
  m_toolbar->addAction(m_selectAction);
  m_registrationActions.append(m_selectAction);

  m_addPointAction =
      new QAction(getSymbolicIcon(":/icons/plus.svg"), "Add Point", this);
  m_addPointAction->setActionGroup(toolGroup);
  m_addPointAction->setCheckable(true);
  m_addPointAction->setToolTip("Add Registration Point (A)");
  m_addPointAction->setShortcut(QKeySequence("A"));
  m_addPointAction->setShortcutContext(Qt::WindowShortcut);
  m_toolbar->addAction(m_addPointAction);
  m_registrationActions.append(m_addPointAction);

  m_setCenterAction = new QAction(getSymbolicIcon(":/icons/crosshair.svg"),
                                  "Screen coordinates", this);
  m_setCenterAction->setActionGroup(toolGroup);
  m_setCenterAction->setCheckable(true);
  m_setCenterAction->setToolTip("Set Screen Coordinates (C)");
  m_setCenterAction->setShortcut(QKeySequence("C"));
  m_setCenterAction->setShortcutContext(Qt::WindowShortcut);
  m_toolbar->addAction(m_setCenterAction);
  m_registrationActions.append(m_setCenterAction);

  // Lock toggle (visible only when Set Center is active)
  m_toolbar->addAction(m_lockRelativeCoordinatesAction);
  m_lockRelativeCoordinatesAction->setVisible(false);
  m_registrationActions.append(m_lockRelativeCoordinatesAction);

  // Optimize button (visible only when Set Center is active)
  m_toolbar->addAction(m_optimizeCoordinatesAction);
  m_optimizeCoordinatesAction->setVisible(false);
  m_registrationActions.append(m_optimizeCoordinatesAction);

  connect(m_panAction, &QAction::toggled, this, [this](bool checked) {
    if (checked)
      if (ImageWidget *image = inspectorImageWidget())
        image->setInteractionMode(ImageWidget::PanMode);
  });
  connect(m_selectAction, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      if (ImageWidget *image = inspectorImageWidget()) {
        image->setInteractionMode(ImageWidget::SelectMode);
        // Auto-enable registration points visibility
        if (!image->registrationPointsVisible())
          image->setShowRegistrationPoints(true);
      }
    }
  });
  connect(m_addPointAction, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      if (ImageWidget *image = inspectorImageWidget()) {
        image->setInteractionMode(ImageWidget::AddPointMode);
        // Auto-enable registration points visibility
        if (!image->registrationPointsVisible())
          image->setShowRegistrationPoints(true);
      }
    }
  });
  connect(m_setCenterAction, &QAction::toggled, this, [this](bool checked) {
    if (checked)
      if (ImageWidget *image = inspectorImageWidget())
        image->setInteractionMode(ImageWidget::SetCenterMode);
    updateScreenCoordinateToolPresentation();
  });

  connect(m_imageWidget, &ImageWidget::selectionChanged, this,
          &MainWindow::updateRegistrationActions);
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          this, &MainWindow::updateRegistrationActions);
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          this, [this](bool) { updateWorkflowSummary(); });
  connect(m_imageWidget, &ImageWidget::pointAdded, this,
          &MainWindow::onPointAdded);
  connect(m_imageWidget, &ImageWidget::profileSpotRemoveRequested, this,
          [this](int index) {
            if (!m_addingProfileSpot)
              return;
            ParameterState newState = getCurrentState();
            if (index >= 0 && index < (int)newState.profileSpots.size()) {
              newState.profileSpots.erase(newState.profileSpots.begin() +
                                          index);
              changeParameters(newState, "Remove profile spot");
            }
          });
  connect(m_imageWidget, &ImageWidget::areaSelected, this,
          &MainWindow::onAreaSelected);
  connect(m_imageWidget, &ImageWidget::setCenterRequested, this,
          &MainWindow::onSetCenter);
  connect(m_imageWidget, &ImageWidget::coordinateSystemChanged, this,
          &MainWindow::onCoordinateSystemChanged);
  connect(m_imageWidget, &ImageWidget::coordinateSystemManipulationStarted, this,
          &MainWindow::onCoordinateSystemManipulationStarted);
  connect(m_imageWidget, &ImageWidget::coordinateSystemManipulationFinished, this,
          &MainWindow::onCoordinateSystemManipulationFinished);

  // Initially hide registration group
  updateRegistrationGroupVisibility();
  createModeShortcuts();
  updateModeMenu();

  QAction *exploreModeAction = new QAction("Explore Mode", this);
  exploreModeAction->setShortcut(QKeySequence("Ctrl+M"));
  exploreModeAction->setShortcutContext(Qt::WindowShortcut);
  connect(exploreModeAction, &QAction::triggered, this, [this]() {
    if (m_imageWidget) {
      m_imageWidget->setExploreMode(m_imageWidget->interactionMode() !=
                                    ImageWidget::ExploreMode);
    }
  });
  addAction(exploreModeAction);
  if (m_imageWidget) m_imageWidget->addAction(exploreModeAction); // Add to ImageWidget for fullscreen
}

/** Add the shared document canvas actions to an ordinary New View toolbar. */
void MainWindow::appendOrdinaryViewToolActions(QToolBar *toolbar) {
  if (!toolbar)
    return;

  if (m_panAction)
    toolbar->addAction(m_panAction);
  if (m_zoomInAction)
    toolbar->addAction(m_zoomInAction);
  if (m_zoomOutAction)
    toolbar->addAction(m_zoomOutAction);
  if (m_zoom100Action)
    toolbar->addAction(m_zoom100Action);
  if (m_zoomFitAction)
    toolbar->addAction(m_zoomFitAction);
  if (m_rotateLeftAction)
    toolbar->addAction(m_rotateLeftAction);
  if (m_rotateRightAction)
    toolbar->addAction(m_rotateRightAction);
  if (m_mirrorAction)
    toolbar->addAction(m_mirrorAction);
  for (QAction *action : m_registrationActions)
    if (action)
      toolbar->addAction(action);
}

/** Return the document-owned Edit menu action shared by ordinary views. */
QAction *MainWindow::ordinaryViewEditMenuAction() const {
  return m_editMenu ? m_editMenu->menuAction() : nullptr;
}

/** Return the document-owned Registration menu action shared by ordinary views. */
QAction *MainWindow::ordinaryViewRegistrationMenuAction() const {
  return m_registrationMenu ? m_registrationMenu->menuAction() : nullptr;
}

/** Rebuild the view-local Scan/Screen coordinate selector. */
void MainWindow::updateCoordinateSpaceControls() {
  if (!m_coordinateComboBox || !m_imageWidget)
    return;
  const bool stitched = m_scan && m_scan->stitch;
  const bool hasFinal = stitched ||
      (m_scan && colorscreen::screen_geometry_configured_p(m_scrToImgParams));

  QSignalBlocker coordinateSignalBlocker(m_coordinateComboBox);
  m_coordinateComboBox->clear();
  if (!stitched)
    m_coordinateComboBox->addItem(tr("Scan coordinates"),
        (int)colorscreen::render_scan_coordinates);
  if (hasFinal)
    m_coordinateComboBox->addItem(tr("Screen coordinates"),
        (int)colorscreen::render_final_coordinates);

  auto current = m_imageWidget->coordinateSpace();
  if (stitched && current != colorscreen::render_final_coordinates) {
    m_imageWidget->setCoordinateSpace(colorscreen::render_final_coordinates);
    current = m_imageWidget->coordinateSpace();
  } else if (!hasFinal && current == colorscreen::render_final_coordinates) {
    m_imageWidget->setCoordinateSpace(colorscreen::render_scan_coordinates);
    current = m_imageWidget->coordinateSpace();
  }
  int index = m_coordinateComboBox->findData((int)current);
  if (index < 0 && m_coordinateComboBox->count())
    index = 0;
  if (index >= 0)
    m_coordinateComboBox->setCurrentIndex(index);
  m_coordinateComboBox->setEnabled(m_coordinateComboBox->count() > 1);
  coordinateSignalBlocker.unblock();

  const bool finalCoordinates =
      current == colorscreen::render_final_coordinates;
  if (m_finalRotationLabelAction)
    m_finalRotationLabelAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinAction)
    m_finalRotationSpinAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinBox) {
    const QSignalBlocker blocker(m_finalRotationSpinBox);
    m_finalRotationSpinBox->setValue(m_scrToImgParams.final_rotation);
  }

  // Rotate/mirror are document-owned actions also shown in ordinary New View
  // toolbars. Their state follows the view currently presenting the inspector,
  // not necessarily the primary canvas whose coordinate combo lives here.
  syncInspectorViewActions();
}

/** Create keyboard shortcuts 1–0 mapped to the first 10 render modes.
   Each shortcut triggers the corresponding index in m_modeComboBox.
   Actions are initially disabled and enabled dynamically as modes
   are added to the combo box by updateModeMenu().  */
void MainWindow::createModeShortcuts() {
  for (int i = 0; i < 10; ++i) {
    int key = (i + 1) % 10;
    QAction *action = new QAction(this);
    action->setShortcut(QKeySequence(QString::number(key)));
    action->setShortcutContext(Qt::WindowShortcut);
    action->setEnabled(false); // Initially disabled
    connect(action, &QAction::triggered, this, [this, i]() {
      if (i < m_modeComboBox->count()) {
        m_modeComboBox->setCurrentIndex(i);
      }
    });
    addAction(action);
    if (m_imageWidget) m_imageWidget->addAction(action); // Add to ImageWidget for fullscreen
    m_modeActions.append(action);
  }
}

/** Rotate the scan image 90° counter-clockwise.
   Updates scan_rotation in the parameter state, adjusts the viewport
   pivot so the visible area stays centered, and pushes an undo command.  */
void MainWindow::rotateLeft() {
  if (!m_scan)
    return;

  // Get current state and modify rotation
  ParameterState newState = getCurrentState();
  int oldRot = (int)newState.rparams.scan_rotation;
  int newRot = (oldRot - 1 + 4) % 4;
  newState.rparams.scan_rotation = newRot;

  // Pivot viewport before applying state
  if (m_imageWidget) {
    m_imageWidget->pivotViewport(oldRot, newRot);
  }

  changeParameters(newState, "Rotate Left");
}

/** Rotate the scan image 90° clockwise.
   Same logic as rotateLeft but increments rotation instead.  */
void MainWindow::rotateRight() {
  if (!m_scan)
    return;

  // Get current state and modify rotation
  ParameterState newState = getCurrentState();
  int oldRot = (int)newState.rparams.scan_rotation;
  int newRot = (oldRot + 1) % 4;
  newState.rparams.scan_rotation = newRot;

  // Pivot viewport before applying state
  if (m_imageWidget) {
    m_imageWidget->pivotViewport(oldRot, newRot);
  }

  changeParameters(newState, "Rotate Right");
}

/** Toggle horizontal mirroring of the scan image.
   Useful for glass plates that may have been scanned from the wrong side.  */
void MainWindow::onMirrorHorizontally(bool checked) {
  if (!m_scan)
    return;
  ParameterState newState = getCurrentState();
  ImageWidget *image = inspectorImageWidget();
  if (image && image->coordinateSpace() ==
                   colorscreen::render_final_coordinates) {
    newState.scrToImg.final_mirror = !newState.scrToImg.final_mirror;
    newState.scrToImg.final_rotation = -newState.scrToImg.final_rotation;
    changeParameters(newState, "Mirror Final Image");
  } else {
    newState.rparams.scan_mirror = !newState.rparams.scan_mirror;
    newState.rparams.scan_rotation = (4 - (int)newState.rparams.scan_rotation) % 4;
    changeParameters(newState, "Mirror Horizontally");
  }
}

/** Toggle fullscreen mode for the ImageWidget.
   When entering fullscreen, detaches ImageWidget from the splitter, saves
   the splitter sizes, moves the widget to the main window's screen, and
   shows it fullscreen.  When exiting, re-parents the widget back into the
   splitter and restores the saved splitter sizes.  Blocks adaptive resize
   during the transition to prevent glitches.  */
void MainWindow::toggleFullscreen() {
  if (m_imageWidget->isFullScreen()) {
    // Exit fullscreen: Block adaptive resize during reparenting glitches
    QSize fullscreenSize = m_imageWidget->size();
    m_imageWidget->setLastSize(QSize());

    m_imageWidget->setParent(m_mainSplitter);
    m_imageWidget->showNormal();
    m_fullscreenAction->setChecked(false);

    // Re-add to splitter (insert at index 0, before right column)
    m_mainSplitter->insertWidget(0, m_imageWidget);
    m_imageWidget->show();

    // Restore splitter sizes after a short delay
    // We use the fullscreen size as the reference for the final jump
    if (!m_splitterSizesBeforeFullscreen.isEmpty() &&
        m_splitterSizesBeforeFullscreen.size() == 2) {
      QList<int> savedSizes = m_splitterSizesBeforeFullscreen;

      QTimer::singleShot(10, this, [this, fullscreenSize, savedSizes]() {
        m_imageWidget->setLastSize(fullscreenSize);
        m_mainSplitter->setSizes(savedSizes);
      });
      m_splitterSizesBeforeFullscreen.clear();
    }
  } else {
    // Save current splitter sizes BEFORE removing the widget
    m_splitterSizesBeforeFullscreen = m_mainSplitter->sizes();

    // Capture current size and block intermediate resizes
    QSize baseSize = m_imageWidget->size();
    m_imageWidget->setLastSize(QSize());

    // Enter fullscreen on the same screen as the main window
    m_imageWidget->setParent(nullptr);

    // Get the screen where the main window is located
    QScreen *targetScreen = screen();
    if (targetScreen) {
      // Move the widget to the target screen before going fullscreen
      m_imageWidget->setGeometry(targetScreen->geometry());
    }

    // Set reference size just before the big resize
    m_imageWidget->setLastSize(baseSize);

    m_imageWidget->showFullScreen();
    m_imageWidget->setFocus();       // Set focus so key events work
    m_imageWidget->activateWindow(); // Also activate the window
    m_fullscreenAction->setChecked(true);
  }
}

/** Handle the color (IR/RGB) checkbox toggle.
   Updates m_renderTypeParams.color and triggers a re-render of the image
   without resetting the current viewport.  The checkbox is only visible
   and enabled when RGB data is available and the current render type
   supports the IR/RGB switch.  */
void MainWindow::onColorCheckBoxChanged(bool checked) {
  // Update the color field in render_type_parameters
  m_renderTypeParams.color = checked;

  // Trigger re-render when color changes (without resetting view)
  if (m_scan) {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
  }
}

/** Rebuild the render mode combo box from the static render_type_properties
   table in libcolorscreen.  Filters out modes that are hidden, require
   screen-to-image mapping when no regular screen is available, screen-colour
   detection when the capture cannot support it, RGB data that isn't
   available, or a correction profile that doesn't exist.
   After populating, re-selects the current mode, updates 1-0 hotkey
   tooltips, and refreshes the color checkbox state.  */
void MainWindow::updateModeMenu() {
  const QSignalBlocker modeSignalBlocker(m_modeComboBox);
  m_modeComboBox->clear();

  // We access the static array in anonymous namespace from
  // render-type-parameters.h Since we included the header, we can try accessing
  // it via the namespace. However, anonymous namespace members have internal
  // linkage. Usually headers shouldn't define static data in anonymous
  // namespaces unless used carefully. Assuming we can access
  // colorscreen::render_type_properties NOTE: In C++, anonymous namespace
  // members are accessible in the same TU. If render_type_properties is in a
  // header in anonymous namespace, each TU gets a copy. But we need to refer to
  // it. It's inside namespace colorscreen { namespace { ... } } or just
  // namespace { ... } inside colorscreen? The header has: namespace colorscreen
  // { namespace { static const ... } }

  using namespace colorscreen;

  // Update color checkbox state based on current render type
  updateColorCheckBoxState();
  for (int i = 0; i < render_type_max; ++i) {
    const render_type_property &prop = render_type_properties[i];

    // Filter logic
    bool show = true;

    if (prop.flags & render_type_property::HIDE_IN_GUI)
      show = false;

    const auto capture =
        m_scan ? m_rparams.get_capture_type(m_scan.get())
               : colorscreen::render_parameters::capture_unknown;
    const bool hasScreenCapture =
        colorscreen::render_parameters::capture_has_screen_p(capture);
    const bool supportsScreenDetection =
        colorscreen::render_parameters::capture_supports_screen_detection_p(
            capture);

    if ((prop.flags & render_type_property::NEEDS_SCR_TO_IMG) &&
        (!hasScreenCapture || !colorscreen::screen_geometry_configured_p(m_scrToImgParams)))
      show = false;

    if ((prop.flags & render_type_property::USES_SCR_DETECT) &&
        (!supportsScreenDetection ||
         !colorscreen::screen_present_p(m_scrToImgParams.type)))
      show = false;

    // If given type has render_type_property::NEEDS_RGB do not show it if
    // m_scan->rgbdata is NULL.
    if (prop.flags & render_type_property::NEEDS_RGB) {
      // Check m_scan
      if (!m_scan || !m_scan->has_rgb()) {
        show = false;
      }
    }
    if ((prop.flags & render_type_property::NEEDS_CORRECTION_PROFILE) &&
        !m_rparams.has_correction_profile())
      show = false;

    if (show) {
      m_modeComboBox->addItem(prop.pretty_name, QVariant(i));
      if (prop.help)
        m_modeComboBox->setItemData(i, QString::fromUtf8(prop.help),
                                    Qt::ToolTipRole);
    }
  }

  // Select current type if present
  int idx = m_modeComboBox->findData((int)m_renderTypeParams.type);
  if (idx != -1) {
    m_modeComboBox->setCurrentIndex(idx);
  } else if (m_modeComboBox->count() > 0) {
    // Fallback
    m_modeComboBox->setCurrentIndex(0);
    // We might want to update m_renderTypeParams.type?
    // Let's defer that to user interaction or explicit set.
  }

  // Update shortcuts and tooltips
  for (int i = 0; i < m_modeActions.size(); ++i) {
    if (i < m_modeComboBox->count()) {
      m_modeActions[i]->setEnabled(true);
      QString hotkeyStr = QString::number((i + 1) % 10);

      QString help = m_modeComboBox->itemData(i, Qt::ToolTipRole).toString();
      if (help.isEmpty()) {
        help = m_modeComboBox->itemText(i);
      }
      m_modeComboBox->setItemData(i, QString("[%1] %2").arg(hotkeyStr, help),
                                  Qt::ToolTipRole);
    } else {
      m_modeActions[i]->setEnabled(false);
    }
  }

}

/** Render a 64×64 preview icon of a screen type pattern.
   Uses libcolorscreen's render_screen_tile to produce a small RGB buffer,
   converts it to a QIcon for display in the autodetection result dialog.  */
QIcon MainWindow::renderScreenIcon(colorscreen::scr_type type) {
  int w = 64;
  int h = 64;
  std::vector<uint8_t> buffer(w * h * 3);

  colorscreen::tile_parameters tile;
  tile.pixels = buffer.data();
  tile.rowstride = w * 3;
  tile.pixelbytes = 3;
  tile.width = w;
  tile.height = h;
  tile.pos = {0.0, 0.0};
  tile.step = 1.0;

  colorscreen::render_parameters rparams;

  bool ok = colorscreen::render_screen_tile(
      tile, type, rparams, 1.0, colorscreen::original_screen, nullptr);

  if (ok) {
    QImage img(buffer.data(), w, h, w * 3, QImage::Format_RGB888);
    img.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return QIcon(QPixmap::fromImage(img.copy()));
  }
  return QIcon();
}

/** Handle render mode combo box selection change.
   Updates the render type in m_renderTypeParams and triggers a re-render
   of the image widget.  Also refreshes the color checkbox state since
   different render types may or may not support the IR/RGB switch.  */
void MainWindow::onModeChanged(int index) {
  if (index < 0)
    return;

  int newType = m_modeComboBox->itemData(index).toInt();
  if (newType >= 0 && newType < colorscreen::render_type_max) {
    if (m_renderTypeParams.type != (colorscreen::render_type_t)newType) {
      m_renderTypeParams.type = (colorscreen::render_type_t)newType;

      // Update color checkbox based on new render type
      updateColorCheckBoxState();

      // Trigger render update (without resetting view)
      if (m_scan) {
        m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                        &m_detectParams, &m_renderTypeParams,
                                        &m_solverParams);
      }
      updateWorkflowSummary();
    }
  }
}

/** Create all document-window menus in conventional application order:
   File, Edit, View, Registration, Window, Help.
   File menu: multi-image Open, Save, Render, Close, and application Exit.
   Edit menu: this document's Undo/Redo stack.
   View menu: Zoom controls, rotation, mirror, fullscreen, gamut warning.
   Registration menu: Point selection, deletion, pruning, geometry
   optimization, coordinate lock/optimize, auto-optimize toggle.
   Window menu: create, arrange, cycle, and activate image documents.
   Help menu: application and Qt version information.
   Also sets up the ExploreMode zoom shortcut management that disables
   global zoom shortcuts while ExploreMode is active to allow continuous
   hold-to-zoom.  */
void MainWindow::createMenus() {
  m_fileMenu = menuBar()->addMenu("&File");
  m_openAction = m_fileMenu->addAction("&Open Image(s)...");
  m_openAction->setShortcut(QKeySequence::Open); // Ctrl+O
  m_openAction->setShortcutContext(Qt::WindowShortcut);
  connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenImage);

  m_recentFilesMenu = m_fileMenu->addMenu("Open &Recent");
  connect(m_recentFilesMenu, &QMenu::aboutToShow, this,
          &MainWindow::loadRecentFiles);
  updateRecentFileActions();

  QAction *openParamsAction = m_fileMenu->addAction("Open &Parameters...");
  openParamsAction->setToolTip(
      "Load rendering and geometry settings from a parameter (.par) file.");
  connect(openParamsAction, &QAction::triggered, this,
          &MainWindow::onOpenParameters);

  m_recentParamsMenu = m_fileMenu->addMenu("Open Recent &Parameters");
  connect(m_recentParamsMenu, &QMenu::aboutToShow, this,
          &MainWindow::loadRecentParams);
  updateRecentParamsActions();

  m_fileMenu->addSeparator();

  m_saveAction = m_fileMenu->addAction("&Save Parameters");
  m_saveAction->setToolTip(
      "Save all current parameters to the current .par file.");
  m_saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
  m_saveAction->setShortcutContext(Qt::WindowShortcut);
  connect(m_saveAction, &QAction::triggered, this,
          &MainWindow::onSaveParameters);

  m_saveAsAction = m_fileMenu->addAction("Save Parameters &As...");
  m_saveAsAction->setToolTip("Save current parameters to a new .par file.");
  m_saveAsAction->setShortcut(QKeySequence::SaveAs); // Ctrl+Shift+S
  m_saveAsAction->setShortcutContext(Qt::WindowShortcut);
  connect(m_saveAsAction, &QAction::triggered, this,
          &MainWindow::onSaveParametersAs);

  m_fileMenu->addSeparator();

  m_renderAction = m_fileMenu->addAction("&Render...");
  m_renderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
  m_renderAction->setShortcutContext(Qt::WindowShortcut);
  m_renderAction->setEnabled(false);
  connect(m_renderAction, &QAction::triggered, this, &MainWindow::onRender);

  m_fileMenu->addSeparator();

  QAction *closeAction = m_fileMenu->addAction("&Close Window");
  closeAction->setShortcut(QKeySequence::Close);
  closeAction->setShortcutContext(Qt::WindowShortcut);
  closeAction->setToolTip(
      "Close this view. The image remains open while another view exists.");
  connect(closeAction, &QAction::triggered, this, &QWidget::close);

  QAction *exitAction = m_fileMenu->addAction("E&xit");
  exitAction->setShortcut(QKeySequence::Quit);
  exitAction->setShortcutContext(Qt::WindowShortcut);
  exitAction->setToolTip("Close all image documents and exit Color-Screen.");
  connect(exitAction, &QAction::triggered, this, []() {
    if (ColorScreenApplication *application = documentApplication())
      application->closeAllDocumentWindows();
    else
      QApplication::closeAllWindows();
  });

  m_editMenu = menuBar()->addMenu("&Edit");
  QAction *undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
  undoAction->setObjectName(QStringLiteral("UndoParametersAction"));
  undoAction->setIcon(QIcon::fromTheme("edit-undo-symbolic"));
  undoAction->setShortcut(QKeySequence::Undo);
  m_editMenu->addAction(undoAction);

  QAction *redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
  redoAction->setIcon(QIcon::fromTheme("edit-redo-symbolic"));
  redoAction->setShortcut(QKeySequence::Redo);
  m_editMenu->addAction(redoAction);

  // View Menu
  m_viewMenu = menuBar()->addMenu("&View");

  m_zoomInAction = m_viewMenu->addAction("Zoom &In");
  m_zoomInAction->setIcon(getSymbolicIcon(":/icons/zoom-in.svg"));
  m_zoomInAction->setShortcuts({QKeySequence::ZoomIn,
                                QKeySequence(Qt::Key_Plus),
                                QKeySequence(Qt::Key_Equal)}); // Ctrl++, +, =
  m_zoomInAction->setShortcutContext(Qt::WindowShortcut);
  m_zoomInAction->setToolTip("Increase view magnification.");
  connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);

  m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
  m_zoomOutAction->setIcon(getSymbolicIcon(":/icons/zoom-out.svg"));
  m_zoomOutAction->setShortcuts(
      {QKeySequence::ZoomOut, QKeySequence(Qt::Key_Minus)}); // Ctrl+-, -
  m_zoomOutAction->setShortcutContext(Qt::WindowShortcut);
  m_zoomOutAction->setStatusTip(tr("Zoom out"));
  m_zoomOutAction->setToolTip("Decrease view magnification.");
  connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
  m_viewMenu->addAction(m_zoomOutAction);

  m_zoom100Action = new QAction(tr("Zoom &1:1"), this);
  m_zoom100Action->setIcon(getSymbolicIcon(":/icons/zoom-100.svg"));
  m_zoom100Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  m_zoom100Action->setShortcutContext(Qt::WindowShortcut);
  m_zoom100Action->setStatusTip(tr("Zoom to 100%"));
  m_zoom100Action->setToolTip("Restore view to 1:1 pixel scale (100%).");
  connect(m_zoom100Action, &QAction::triggered, this, &MainWindow::onZoom100);
  m_viewMenu->addAction(m_zoom100Action);

  m_zoomFitAction = new QAction(tr("Fit to &Screen"), this);
  m_zoomFitAction->setIcon(getSymbolicIcon(":/icons/zoom-fit.svg"));
  m_zoomFitAction->setShortcut(Qt::CTRL | Qt::Key_0);
  m_zoomFitAction->setShortcutContext(Qt::WindowShortcut);
  m_zoomFitAction->setToolTip(
      "Adjust zoom to fit the entire image in the viewer.");
  connect(m_zoomFitAction, &QAction::triggered, this, &MainWindow::onZoomFit);
  m_viewMenu->addAction(m_zoomFitAction);

  // Gamut Warning Action
  m_gamutWarningAction = new QAction(tr("Gamut Warning"), this);
  m_gamutWarningAction->setCheckable(true);
  m_gamutWarningAction->setChecked(false);
  m_gamutWarningAction->setToolTip("Highlight colors that cannot be accurately "
                                   "represented in the target color space.");
  connect(m_gamutWarningAction, &QAction::toggled, this,
          &MainWindow::onGamutWarningToggled);
  m_viewMenu->addAction(m_gamutWarningAction);

  m_viewMenu->addSeparator();

  m_rotateLeftAction = m_viewMenu->addAction("Rotate &Left");
  m_rotateLeftAction->setShortcut(Qt::CTRL | Qt::Key_Left); // Or Ctrl+L?
  // User asked for "usual shortcuts". Photoshop uses Image > Image Rotation.
  // Viewers use L/R or Ctrl+L/Ctrl+R.
  // Let's bind Ctrl+L and Ctrl+R for explicit global feeling if not
  // conflicting. Actually, Qt::Key_L and Qt::Key_R are better than arrows
  // (which might pan). But navigation pan is usually just arrows. Let's use
  // Ctrl+L and Ctrl+R.
  m_rotateLeftAction->setShortcut(Qt::CTRL | Qt::Key_L);
  m_rotateLeftAction->setShortcutContext(Qt::WindowShortcut);
  m_rotateLeftAction->setToolTip(
      "Rotate the digital scan 90 degrees counter-clockwise.");
  connect(m_rotateLeftAction, &QAction::triggered, this,
          &MainWindow::rotateLeft);

  m_rotateRightAction = m_viewMenu->addAction("Rotate &Right");
  m_rotateRightAction->setShortcut(Qt::CTRL | Qt::Key_R);
  m_rotateRightAction->setShortcutContext(Qt::WindowShortcut);
  m_rotateRightAction->setToolTip(
      "Rotate the digital scan 90 degrees clockwise.");
  connect(m_rotateRightAction, &QAction::triggered, this,
          &MainWindow::rotateRight);

  m_mirrorAction = m_viewMenu->addAction(getSymbolicIcon(":/icons/mirror.svg"),
                                         "Mirror \u0026Horizontally");
  m_mirrorAction->setCheckable(true);
  m_mirrorAction->setToolTip("Flip the image horizontally (useful for glass "
                             "plates scanned from the wrong side).");
  connect(m_mirrorAction, &QAction::triggered, this,
          &MainWindow::onMirrorHorizontally);

  m_viewMenu->addSeparator();

  m_fullscreenAction = m_viewMenu->addAction("&Fullscreen");
  m_fullscreenAction->setCheckable(true);
  m_fullscreenAction->setShortcut(Qt::Key_F11);
  m_fullscreenAction->setShortcutContext(Qt::WindowShortcut);
  m_fullscreenAction->setToolTip("Toggle fullscreen image display.");
  connect(m_fullscreenAction, &QAction::triggered, this,
          &MainWindow::toggleFullscreen);

  // Registration Menu
  m_registrationMenu = menuBar()->addMenu("&Registration");

  m_lockRelativeCoordinatesAction =
      new QAction(QIcon::fromTheme("system-lock-screen-symbolic"),
                  tr("Lock relative coordinates"), this);
  m_lockRelativeCoordinatesAction->setCheckable(true);
  m_lockRelativeCoordinatesAction->setChecked(true); // Default ON
  m_lockRelativeCoordinatesAction->setToolTip(
      "Keep the X and Y screen axes at their current relative angle and scale "
      "ratio while either axis is edited.");
  connect(m_lockRelativeCoordinatesAction, &QAction::toggled, this,
          [this](bool checked) {
            if (ImageWidget *image = inspectorImageWidget())
              image->setLockRelativeCoordinates(checked);
          });
  m_registrationMenu->addAction(m_lockRelativeCoordinatesAction);

  m_registrationPointsAction =
      new QAction(tr("Show Registration &Points"), this);
  m_registrationPointsAction->setCheckable(true);
  m_registrationPointsAction->setChecked(false);
  m_registrationPointsAction->setToolTip(
      "Show or hide dots indicating registration points and their errors.");

  // Visibility toggle at the top
  m_registrationMenu->addAction(m_registrationPointsAction);

  m_detectedPatchCentersAction =
      new QAction(tr("Show Auto-detected Patch &Centers"), this);
  m_detectedPatchCentersAction->setObjectName(
      QStringLiteral("DetectedPatchCentersAction"));
  m_detectedPatchCentersAction->setCheckable(true);
  m_detectedPatchCentersAction->setChecked(false);
  m_detectedPatchCentersAction->setEnabled(false);
  m_detectedPatchCentersAction->setToolTip(
      tr("Show color-coded centers of screen elements found by the most recent "
         "automatic basic-screen detection. The dense overlay is drawn at "
         "100% zoom and above."));
  connect(m_detectedPatchCentersAction, &QAction::toggled, this,
          [this](bool show) {
            m_showDetectedPatchCenters = show;
            if (ImageWidget *image = inspectorImageWidget())
              image->setShowDetectedPatchCenters(show);
          });
  m_registrationMenu->addAction(m_detectedPatchCentersAction);
  m_registrationMenu->addSeparator();

  m_selectAllAction = m_registrationMenu->addAction("Select &All");
  m_selectAllAction->setShortcut(QKeySequence::SelectAll); // Ctrl+A
  m_selectAllAction->setShortcutContext(Qt::WindowShortcut);
  m_selectAllAction->setToolTip("Select all registration points.");
  connect(m_selectAllAction, &QAction::triggered, this,
          &MainWindow::onSelectAll);

  m_deselectAllAction = m_registrationMenu->addAction("&Deselect All");
  m_deselectAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
  m_deselectAllAction->setShortcutContext(Qt::WindowShortcut);
  m_deselectAllAction->setToolTip("Clear current point selection.");
  connect(m_deselectAllAction, &QAction::triggered, this,
          &MainWindow::onDeselectAll);

  m_deleteSelectedAction =
      m_registrationMenu->addAction("&Remove Selected Points");
  m_deleteSelectedAction->setShortcuts(
      {QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
  m_deleteSelectedAction->setShortcutContext(Qt::WindowShortcut);
  m_deleteSelectedAction->setToolTip(
      "Delete the currently selected registration points.");
  connect(m_deleteSelectedAction, &QAction::triggered, this,
          &MainWindow::onDeleteSelected);

  m_pruneMisplacedAction =
      m_registrationMenu->addAction("&Prune Misplaced Points");
  m_pruneMisplacedAction->setShortcuts(
      {QKeySequence("Ctrl+Delete"), QKeySequence("Ctrl+Backspace")});
  m_pruneMisplacedAction->setShortcutContext(Qt::WindowShortcut);
  m_pruneMisplacedAction->setToolTip(
      "Automatically delete points with high registration error scores.");
  connect(m_pruneMisplacedAction, &QAction::triggered, this,
          &MainWindow::onPruneMisplaced);

  m_registrationMenu->addSeparator();

  m_optimizeGeometryAction =
      m_registrationMenu->addAction("&Optimize Geometry");
  m_optimizeGeometryAction->setToolTip(
      "Run the geometry solver to align screen and image using the current "
      "registration points.");
  connect(m_optimizeGeometryAction, &QAction::triggered, this,
          [this]() { onOptimizeGeometry(m_autoOptimizeAction->isChecked()); });

  m_autoOptimizeAction = new QAction(tr("Auto &Optimize"), this);
  m_autoOptimizeAction->setCheckable(true);
  m_autoOptimizeAction->setChecked(false);
  m_autoOptimizeAction->setToolTip("Automatically run the geometry solver "
                                   "whenever points are added or moved.");
  m_registrationMenu->addAction(m_autoOptimizeAction);

  m_registrationMenu->addSeparator();

  // Connect toggle to update tool state
  connect(m_registrationPointsAction, &QAction::toggled, this,
          &MainWindow::updateRegistrationActions);

  m_optimizeCoordinatesAction = new QAction(QIcon::fromTheme("system-run"),
                                            tr("Optimize Coordinates"), this);
  m_optimizeCoordinatesAction->setToolTip("Optimize Coordinates");
  connect(m_optimizeCoordinatesAction, &QAction::triggered, this,
          &MainWindow::onOptimizeCoordinates);

  // Window comes after document-specific editing menus and immediately before
  // Help, matching the conventional desktop-application menu order.  Its
  // document list is rebuilt immediately before display so every MainWindow
  // sees images opened or closed from another document.
  m_windowMenu = menuBar()->addMenu("&Window");
  connect(m_windowMenu, &QMenu::aboutToShow, this,
          &MainWindow::refreshWindowMenu);
  refreshWindowMenu();

  // Help is application-wide presentation even though the actions live on the
  // document while it is detached.  WorkspaceWindow surfaces the active
  // document's menu actions without changing their ownership.
  m_helpMenu = menuBar()->addMenu("&Help");
  QAction *aboutAction = m_helpMenu->addAction(tr("&About Color-Screen"));
  connect(aboutAction, &QAction::triggered, this, [this]() {
    QMessageBox::about(
        QApplication::activeWindow(), tr("About Color-Screen"),
        tr("<b>Color-Screen %1</b><br><br>"
           "Open-source software for digital reconstruction and analysis of "
           "early color photographic processes.")
            .arg(QApplication::applicationVersion()));
  });
  QAction *aboutQtAction = m_helpMenu->addAction(tr("About &Qt"));
  connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

  // Dynamically manage zoom shortcuts for ExploreMode to allow continuous
  // hold-to-zoom
  connect(m_imageWidget, &ImageWidget::interactionModeChanged, this,
          [this](ImageWidget::InteractionMode mode) {
            if (mode == ImageWidget::ExploreMode) {
              m_zoomInAction->setShortcuts({});
              m_zoomOutAction->setShortcuts({});
            } else {
              m_zoomInAction->setShortcuts({QKeySequence::ZoomIn,
                                            QKeySequence(Qt::Key_Plus),
                                            QKeySequence(Qt::Key_Equal)});
              m_zoomOutAction->setShortcuts(
                  {QKeySequence::ZoomOut, QKeySequence(Qt::Key_Minus)});
            }
          });
}

/** Register ordinary transient background progress. */
void MainWindow::addProgress(std::shared_ptr<colorscreen::progress_info> info) {
  m_progressController.addProgress(std::move(info));
}

/** Register a long-running task with its own status-bar row. */
void MainWindow::addUserVisibleProgress(
    std::shared_ptr<colorscreen::progress_info> info, const QString &title,
    ProgressAction action) {
  m_progressController.addUserVisibleProgress(std::move(info), title, action);
}

/** Move keyboard focus away from ROW before a visible task control is disabled
    or destroyed.  Attached task rows live in WorkspaceWindow, so focus returns
    to whichever image presentation is currently active rather than to this
    task's owning document. */
void MainWindow::releaseUserVisibleProgressFocus(QWidget *row) {
  QWidget *focus = QApplication::focusWidget();
  if (!row || !focus || (focus != row && !row->isAncestorOf(focus)))
    return;

  if (m_workspaceEmbedded) {
    if (ColorScreenApplication *application = documentApplication()) {
      if (WorkspaceWindow *workspace = application->workspaceWindow()) {
        if (workspace->restoreFocusFromTaskControl(focus))
          return;
      }
    }
  }

  if (ImageWidget *image = inspectorImageWidget())
    image->setFocus(Qt::OtherFocusReason);
  else
    setFocus(Qt::OtherFocusReason);
}

/** Remove a completed or terminated background task from progress tracking. */
void MainWindow::removeProgress(
    std::shared_ptr<colorscreen::progress_info> info) {
  m_progressController.removeProgress(std::move(info));
}

// Recent Files Implementation

/** Rebuild this document's Window menu from the application's live list. */
void MainWindow::refreshWindowMenu() {
  if (ColorScreenApplication *application = documentApplication())
    application->populateWindowMenu(m_windowMenu, this);
}

/** Remove transient progress from the private status bar for workspace hosting. */
QWidget *MainWindow::takeWorkspaceStatusWidget() {
  return m_progressController.takeTransientWidget(standaloneStatusBar());
}

/** Return transient progress to this document's private status bar. */
void MainWindow::restoreWorkspaceStatusWidget() {
  m_progressController.restoreTransientWidget(standaloneStatusBar());
}

/** Remove persistent progress rows from the local task-progress dock. */
QWidget *MainWindow::takeUserVisibleStatusWidget() {
  return m_progressController.takeUserVisibleWidget();
}

/** Return persistent user-visible progress rows to this document's task dock. */
void MainWindow::restoreUserVisibleStatusWidget() {
  m_progressController.restoreUserVisibleWidget();
}

/** Detach the document-owned inspector from whichever presentation hosts it. */
QWidget *MainWindow::takeWorkspaceInspector() {
  if (!m_rightColumn)
    return nullptr;
  m_rightColumn->hide();
  if (m_rightColumn->parentWidget())
    m_rightColumn->setParent(nullptr);
  return m_rightColumn;
}

/** Restore the document-owned inspector beside the primary image view. */
void MainWindow::restoreWorkspaceInspector() {
  if (!m_rightColumn || !m_mainSplitter)
    return;

  if (m_rightColumn->parentWidget() != m_mainSplitter) {
    takeWorkspaceInspector();
    m_mainSplitter->addWidget(m_rightColumn);
    if (!m_workspaceSplitterState.isEmpty())
      m_mainSplitter->restoreState(m_workspaceSplitterState);
  }
  setInspectorImageWidget(m_imageWidget);
  m_rightColumn->show();
}

/** Return true when IMAGEWIDGET is an ordinary presentation of this scan. */
bool MainWindow::acceptsInspectorImageWidget(ImageWidget *imageWidget) const {
  if (!imageWidget)
    return false;
  if (imageWidget == m_imageWidget)
    return true;
  return m_scan && imageWidget->sharedImageData() == m_scan;
}

/** Synchronize shared interaction actions with MODE in the active view. */
void MainWindow::syncInspectorInteractionActions(ImageWidget::InteractionMode mode) {
  if (m_panAction) {
    const QSignalBlocker blocker(m_panAction);
    m_panAction->setChecked(mode == ImageWidget::PanMode);
  }
  if (m_selectAction) {
    const QSignalBlocker blocker(m_selectAction);
    m_selectAction->setChecked(mode == ImageWidget::SelectMode);
  }
  if (m_addPointAction) {
    const QSignalBlocker blocker(m_addPointAction);
    m_addPointAction->setChecked(mode == ImageWidget::AddPointMode);
  }
  if (m_setCenterAction) {
    const QSignalBlocker blocker(m_setCenterAction);
    m_setCenterAction->setChecked(mode == ImageWidget::SetCenterMode);
  }
  updateScreenCoordinateToolPresentation();
  if (m_capturePanel)
    m_capturePanel->setCropChecked(mode == ImageWidget::CropMode);
}

/** Synchronize coordinate-dependent shared actions with the active view. */
void MainWindow::syncInspectorViewActions() {
  ImageWidget *image = inspectorImageWidget();
  if (!image)
    return;

  const bool finalCoordinates =
      image->coordinateSpace() == colorscreen::render_final_coordinates;
  if (m_rotateLeftAction)
    m_rotateLeftAction->setVisible(!finalCoordinates);
  if (m_rotateRightAction)
    m_rotateRightAction->setVisible(!finalCoordinates);
  if (m_mirrorAction) {
    const QSignalBlocker blocker(m_mirrorAction);
    m_mirrorAction->setChecked(finalCoordinates ? m_scrToImgParams.final_mirror
                                                : m_rparams.scan_mirror);
    m_mirrorAction->setText(finalCoordinates ? tr("Mirror Final Image")
                                             : tr("Mirror Horizontally"));
    m_mirrorAction->setToolTip(finalCoordinates
        ? tr("Mirror the final-coordinate image; saved in the parameter file")
        : tr("Mirror the digital scan horizontally"));
  }
}

/** Route the shared inspector's navigation and editing gestures to IMAGEWIDGET.
    Pending document tools follow between ordinary views of the same loaded scan.
    A view presenting a different image (for example a slanted-edge reference)
    is deliberately rejected rather than receiving an incompatible operation. */
void MainWindow::setInspectorImageWidget(ImageWidget *imageWidget) {
  ImageWidget *target = imageWidget ? imageWidget : m_imageWidget;
  if (!acceptsInspectorImageWidget(target))
    return;

  ImageWidget *previous = inspectorImageWidget();
  const ImageWidget::InteractionMode previousMode =
      previous ? previous->interactionMode() : ImageWidget::PanMode;
  const bool transferTool =
      previous && previous != target && acceptsInspectorImageWidget(previous) &&
      previousMode != ImageWidget::PanMode &&
      previousMode != ImageWidget::ExploreMode;

  for (const QMetaObject::Connection &connection : m_inspectorImageConnections)
    disconnect(connection);
  m_inspectorImageConnections.clear();
  m_inspectorImageWidget = target;
  target->setDetectedScreenMap(m_detectedScreenMap);
  target->setShowDetectedPatchCenters(m_showDetectedPatchCenters);

  // A selected document tool belongs to the document operation, not to the
  // canvas that happened to be active when it was armed. Move it to the newly
  // active compatible view and leave the old view harmlessly in Pan mode.
  if (transferTool) {
    m_switchingInspectorImage = true;
    previous->setInteractionMode(ImageWidget::PanMode);
    target->setInteractionMode(previousMode);
    m_switchingInspectorImage = false;
  }

  if (m_navigationView) {
    m_navigationView->setCoordinateSpace(target->coordinateSpace());
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::viewStateChanged, m_navigationView,
                &NavigationView::onViewStateChanged));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::viewCoordinateSpaceChanged, this,
        [this](int space) {
          if (m_navigationView)
            m_navigationView->setCoordinateSpace(
                static_cast<colorscreen::render_coordinate_space>(space));
          syncInspectorViewActions();
        }));
  }

  // The primary ImageWidget already has the full document-editing signal
  // wiring installed by setupUi(). Secondary ordinary views acquire the same
  // document-side behavior only while they present this inspector.
  if (target != m_imageWidget) {
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::progressStarted, this,
                &MainWindow::addProgress));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::progressFinished, this,
                &MainWindow::removeProgress));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::distanceMeasured, this,
                &MainWindow::onDistanceMeasured));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::selectionChanged, this,
                &MainWindow::updateRegistrationActions));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::registrationPointsVisibilityChanged, this,
                &MainWindow::updateRegistrationActions));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::registrationPointsVisibilityChanged, this,
        [this](bool) { updateWorkflowSummary(); }));
    if (m_registrationPointsAction) {
      m_inspectorImageConnections.push_back(connect(
          target, &ImageWidget::registrationPointsVisibilityChanged,
          m_registrationPointsAction, &QAction::setChecked));
    }
    if (m_geometryPanel) {
      m_inspectorImageConnections.push_back(connect(
          target, &ImageWidget::registrationPointsVisibilityChanged,
          m_geometryPanel, &GeometryPanel::setRegistrationPointsVisible));
    }
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::pointManipulationStarted, this,
                &MainWindow::onPointManipulationStarted));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::pointsChanged, this,
                &MainWindow::maybeTriggerAutoSolver));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::pointsChanged, this,
                [this]() { emit documentStateChanged(); }));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::pointAdded, this, &MainWindow::onPointAdded));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::areaSelected, this, &MainWindow::onAreaSelected));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::setCenterRequested, this,
                &MainWindow::onSetCenter));
    m_inspectorImageConnections.push_back(
        connect(target, &ImageWidget::coordinateSystemChanged, this,
                &MainWindow::onCoordinateSystemChanged));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::coordinateSystemManipulationStarted, this,
        &MainWindow::onCoordinateSystemManipulationStarted));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::coordinateSystemManipulationFinished, this,
        &MainWindow::onCoordinateSystemManipulationFinished));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::profileSpotRemoveRequested, this,
        [this](int index) {
          if (!m_addingProfileSpot)
            return;
          ParameterState state = getCurrentState();
          if (index >= 0 && index < static_cast<int>(state.profileSpots.size())) {
            state.profileSpots.erase(state.profileSpots.begin() + index);
            changeParameters(state, "Remove profile spot");
          }
        }));
    m_inspectorImageConnections.push_back(connect(
        target, &ImageWidget::interactionModeChanged, this,
        [this](ImageWidget::InteractionMode mode) {
          syncInspectorInteractionActions(mode);
          if (!m_switchingInspectorImage && sender() == inspectorImageWidget() &&
              mode != ImageWidget::GenericAreaMode && m_areaSelectionCallback) {
            m_areaSelectionCallback = nullptr;
            if (m_imageLayerPanel) {
              m_imageLayerPanel->setNeutralAreaChecked(false);
              m_imageLayerPanel->setInfraredAreaChecked(false);
              m_imageLayerPanel->setDarkAreaChecked(false);
              m_imageLayerPanel->updateUI();
            }
            if (m_colorPanel) {
              m_colorPanel->setNeutralAreaChecked(false);
              m_colorPanel->setAutoLevelsChecked(false);
              m_colorPanel->updateUI();
            }
          }
        }));
  }

  syncInspectorInteractionActions(target->interactionMode());
  syncInspectorViewActions();
  if (m_registrationPointsAction) {
    const QSignalBlocker blocker(m_registrationPointsAction);
    m_registrationPointsAction->setChecked(target->registrationPointsVisible());
  }
  if (m_detectedPatchCentersAction) {
    const QSignalBlocker blocker(m_detectedPatchCentersAction);
    m_detectedPatchCentersAction->setChecked(m_showDetectedPatchCenters);
    m_detectedPatchCentersAction->setEnabled(
        static_cast<bool>(m_detectedScreenMap));
  }
  if (m_geometryPanel)
    m_geometryPanel->setRegistrationPointsVisible(
        target->registrationPointsVisible());
  updateRegistrationActions();
  updateFocusAreaOverlays();
  updateMtfMeasurementOverlay(false);
}

/** Reclaim the inspector when a detached primary document becomes active. */
void MainWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (event && event->type() == QEvent::WindowActivate && !m_workspaceEmbedded)
    restoreWorkspaceInspector();
}

/** Prepare this document for presentation inside the shared MDI workspace.
    The image view remains inside this MainWindow, while the navigation and
    parameter column is moved to the workspace inspector.  The workspace also
    presents the active document's menu and toolbar; document-owned diagnostic
    docks and progress state remain with the embedded document. */
void MainWindow::prepareForWorkspaceEmbedding() {
  if (m_workspaceEmbedded)
    return;

  if (m_mainSplitter)
    m_workspaceSplitterState = m_mainSplitter->saveState();
  takeWorkspaceInspector();
  if (m_toolbar)
    m_toolbar->hide();
  if (menuBar())
    menuBar()->hide();
  if (statusBar())
    statusBar()->hide();
  m_workspaceEmbedded = true;
}

/** Restore this document's ordinary standalone QMainWindow presentation. */
void MainWindow::restoreFromWorkspaceEmbedding() {
  if (!m_workspaceEmbedded)
    return;

  restoreWorkspaceInspector();
  if (m_toolbar)
    m_toolbar->show();
  if (menuBar())
    menuBar()->show();
  if (statusBar())
    statusBar()->show();
  m_workspaceEmbedded = false;
}

// Undo/Redo Implementation

/** Apply a full ParameterState to the application.
   Copies all parameter structs (render, scr-to-img, detect, solver,
   profile spots) to member variables, updates ImageWidget and
   NavigationView, refreshes all panels, and rebuilds the mode menu.
   Called by undo/redo commands and by changeParameters().  */
void MainWindow::applyState(const ParameterState &state) {
  // Final-result one-shot operations consume an exact document snapshot. Any
  // accepted parameter change (including Undo/Redo) makes that result stale.
  dismissOneShotPrompts();
  m_oneShotOperations.cancelAll();

  const bool invalidateFocusAreas
      = m_scrToImgParams != state.scrToImg
        || !(m_rparams.scan_crop == state.rparams.scan_crop);
  bool profileSpotsChanged = m_profileSpots.size() != state.profileSpots.size();
  if (!profileSpotsChanged) {
    for (std::size_t i = 0; i < m_profileSpots.size(); ++i) {
      if (!(m_profileSpots[i] == state.profileSpots[i])) {
        profileSpotsChanged = true;
        break;
      }
    }
  }
  // User requested rotation is not part of parameters.
  // Preserve current rotation when applying state.
  m_rparams = state.rparams;
  m_scrToImgParams = state.scrToImg;
  m_detectParams = state.detect;
  m_solverParams = state.solver; // Manually copy logic if needed? Struct copy
  m_profileSpots = state.profileSpots;
  if (profileSpotsChanged) {
    // Per-spot optimizer output is indexed by the spot list. Never show old
    // DeltaE/colour matches beside a different set of spots.
    m_profileSpotResults.clear();
    if (m_profilePanel)
      m_profilePanel->setSpotResults(m_profileSpotResults);
  }
  // should work if fields are copyable.
  // solver_parameters has vector, copy constructor should be fine
  // (std::vector).

  // Update widgets - use updateParameters to avoid blocking
  if (m_scan) {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    m_imageWidget->setProfileSpots(&m_profileSpots, &m_profileSpotResults);
    m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                       &m_detectParams);
  }

  updateUIFromState(state);
  updateRegistrationActions();
  updateModeMenu();
  if (invalidateFocusAreas)
    clearFocusAreaAnalysis();
}

QString MainWindow::mtfCalibrationSummary() const {
  const colorscreen::mtf_parameters &mtf = m_rparams.sharpen.scanner_mtf;
  const qsizetype count = static_cast<qsizetype>(mtf.measurements.size());
  if (count == 0)
    return tr("Capture MTF: not measured");

  QString summary = tr("Capture MTF: %1 saved measurement%2")
                        .arg(count)
                        .arg(count == 1 ? QString() : QStringLiteral("s"));
  const bool fitCurrent =
      m_mtfFit.baseline && m_mtfFit.baseline->fit_inputs_equal_p(mtf);
  const bool failureCurrent =
      m_mtfFit.failureInputs && m_mtfFit.failureInputs->fit_inputs_equal_p(mtf);
  if (m_mtfFit.running) {
    if (m_mtfFit.pendingInputs && !m_mtfFit.pendingInputs->equal_p(mtf))
      summary += tr(" • fit inputs changed — result will be discarded");
    else
      summary += tr(" • fitting model…");
  } else if (fitCurrent) {
    summary += tr(" • model current");
    if (m_mtfFit.rms >= 0)
      summary += tr(" • RMS %1 pp").arg(m_mtfFit.rms, 0, 'g', 4);
    if (failureCurrent)
      summary += tr(" • last refit failed");
  } else if (failureCurrent) {
    summary += tr(" • fit failed — adjust settings and retry");
  } else if (m_mtfFit.baseline) {
    summary += tr(" • model stale — refit");
  } else {
    summary += tr(" • ready to fit/validate model");
  }
  return summary;
}

void MainWindow::refreshMtfCalibrationPresentation() {
  updateWorkflowSummary();
  if (m_sharpnessPanel)
    m_sharpnessPanel->refreshMtfCalibrationStatus();
  emit mtfCalibrationStateChanged();
}

bool MainWindow::requestMtfModelFit(
    const ParameterState &baseline, const colorscreen::mtf_parameters &input,
    const colorscreen::mtf_estimation_options &options, int flags,
    QWidget *resultParent) {
  if (m_closing || m_mtfFit.running || getCurrentState() != baseline)
    return false;

  const colorscreen::mtf_parameters baselineMtf =
      baseline.rparams.sharpen.scanner_mtf;
  const QPointer<QWidget> guardedResultParent(resultParent);
  auto result = std::make_shared<MtfModelFitResult>();

  OneShotOperation operation;
  operation.description = tr("Fitting measured MTF model");
  operation.progressTitle = tr("MTF model fit");
  operation.prerequisites = [this, baseline]() {
    return !m_mtfFit.running && getCurrentState() == baseline;
  };
  operation.onStart = [this, baselineMtf, result](
                          std::shared_ptr<colorscreen::progress_info> progress) {
    m_mtfFit.running = true;
    m_mtfFit.pendingInputs = baselineMtf;
    m_mtfFit.failureInputs.reset();
    m_mtfFit.progress = progress;
    result->progress = std::move(progress);
    refreshMtfCalibrationPresentation();
  };
  operation.resultValid = [this, baseline, result]() {
    return result->progress &&
           m_mtfFit.progress.lock() == result->progress &&
           getCurrentState() == baseline && !result->cancelled;
  };
  operation.applyResult = [this, baselineMtf, result,
                           guardedResultParent]() {
    // Only the request that still owns document fit provenance reaches here.
    m_mtfFit.running = false;
    m_mtfFit.pendingInputs.reset();
    m_mtfFit.progress.reset();

    if (result->objective < 0 || !result->error.empty()) {
      m_mtfFit.failureInputs = baselineMtf;
      refreshMtfCalibrationPresentation();
      auto *box = new QMessageBox(
          QMessageBox::Warning, tr("MTF model fit"),
          tr("The MTF model could not be fitted: %1")
              .arg(QString::fromStdString(
                  result->error.empty() ? "unknown fitting error"
                                        : result->error)),
          QMessageBox::Ok,
          guardedResultParent ? guardedResultParent.data() : this);
      box->setObjectName(QStringLiteral("MtfFitErrorDialog"));
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
      return;
    }

    const colorscreen::mtf_parameters fitted = result->fitted;
    const double rms = result->observations
                           ? std::sqrt(result->objective / result->observations)
                           : 0.0;
    m_mtfFit.baseline = fitted;
    m_mtfFit.failureInputs.reset();
    m_mtfFit.rms = rms;

    ParameterState updated = getCurrentState();
    updated.rparams.sharpen.scanner_mtf = fitted;
    changeParameters(updated, tr("Fit measured MTF model"));
    refreshMtfCalibrationPresentation();

    QString details =
        tr("The selected model was fitted successfully.\n\n"
           "RMS residual: %1 percentage points\n"
           "Gaussian sigma: %2 px")
            .arg(rms, 0, 'g', 6)
            .arg(fitted.sigma, 0, 'g', 8);
    if (fitted.model == colorscreen::mtf_model::physical_diffraction) {
      details += tr("\nDefocus: %1 mm\nMarked f-number: %2"
                    "\nSensor fill factor: %3\nHalo fraction: %4")
                     .arg(fitted.defocus, 0, 'g', 8)
                     .arg(fitted.f_stop, 0, 'g', 8)
                     .arg(fitted.sensor_fill_factor, 0, 'g', 8)
                     .arg(fitted.halo_fraction, 0, 'g', 8);
      if (fitted.halo_fraction > 0)
        details += tr("\nHalo radius: %1 px").arg(fitted.halo_sigma, 0, 'g', 8);
      else
        details += tr("\nHalo radius: inactive");
    } else {
      details += tr("\nFallback blur diameter: %1 px")
                     .arg(fitted.blur_diameter, 0, 'g', 8);
    }
    auto *box = new QMessageBox(QMessageBox::Information, tr("MTF model fit"),
                                details, QMessageBox::Ok,
                                guardedResultParent ? guardedResultParent.data()
                                                    : this);
    box->setObjectName(QStringLiteral("MtfFitResultDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
  };
  operation.onDone = [this, result]() {
    // Parameter/image replacement may reset the fit and start another request
    // before this cancelled worker returns. Request identity prevents that old
    // completion from clearing the new fit's provenance or enabled state.
    if (!result->progress || m_mtfFit.progress.lock() != result->progress)
      return;
    m_mtfFit.progress.reset();
    m_mtfFit.running = false;
    m_mtfFit.pendingInputs.reset();
    refreshMtfCalibrationPresentation();
  };

  runOneShotOperation(
      std::move(operation),
      [input, options, flags, result](colorscreen::progress_info *progress) {
        runMtfModelFit(input, options, flags, result.get(), progress);
      });
  return true;
}

QString MainWindow::profileCalibrationSummary() const {
  if (!m_scan)
    return QString();

  const auto capture = m_rparams.get_capture_type(m_scan.get());
  if (!colorscreen::render_parameters::capture_supports_screen_detection_p(
          capture) ||
      !m_scan->has_rgb())
    return QString();

  const qsizetype count = static_cast<qsizetype>(m_profileSpots.size());
  const bool savedCalibration = m_rparams.has_correction_profile();
  if (count < 4) {
    if (savedCalibration)
      return tr("Profile: optional matrix correction • saved calibration present — provenance not verified • %1/4 spots")
          .arg(count);
    return tr("Profile: optional matrix correction • %1/4 calibration spots — add %2 more")
        .arg(count).arg(4 - count);
  }

  const ColorOptimizerRequestData current{
      m_scrToImgParams, m_rparams, m_profileSpots};
  const bool fitCurrent = m_profileCalibration.baseline &&
      !profileCalibrationInputsDiffer(*m_profileCalibration.baseline, current);
  const bool failureCurrent = m_profileCalibration.failureInputs &&
      !profileCalibrationInputsDiffer(*m_profileCalibration.failureInputs, current);

  QString summary =
      tr("Profile: optional matrix correction • %1 calibration spots")
          .arg(count);
  if (m_profileCalibration.pendingInputs) {
    summary += tr(" • optimizing…");
  } else if (fitCurrent) {
    summary += tr(" • calibration current");
    if (m_profileCalibration.averageDeltaE >= 0)
      summary += tr(" • avg ΔE₂₀₀₀ %1")
          .arg(m_profileCalibration.averageDeltaE, 0, 'f', 2);
    if (failureCurrent)
      summary += tr(" • last retry failed");
  } else if (failureCurrent) {
    summary += tr(" • optimization failed — adjust inputs and retry");
  } else if (m_profileCalibration.baseline) {
    summary += tr(" • calibration stale — reoptimize");
  } else if (savedCalibration) {
    summary += tr(" • saved calibration present — provenance not verified");
  } else {
    summary += tr(" • ready to optimize");
  }
  return summary;
}

/** Refresh the persistent workflow summary from document-owned state.

    Geometry fit freshness is session-local rather than serialized into .par:
    an accepted fit captures the exact fit-related state as a baseline. Later
    edits therefore remain visible while being labelled stale. A running fit
    whose inputs changed is cancelled before it can publish an obsolete result. */
void MainWindow::updateWorkflowSummary() {
  const ParameterState currentState = getCurrentState();
  // Progressive workers have their own publication ownership. Adaptive
  // sharpening has an immutable baseline; registration discovery has an
  // expected state that advances with each accepted worker-owned batch.
  cancelStaleAdaptiveSharpening(currentState);
  cancelStaleRegistrationDiscovery(currentState);

  if (!m_workflowProcessLabel || !m_workflowImageLayerLabel ||
      !m_workflowRegistrationLabel || !m_workflowCalibrationLabel ||
      !m_workflowProfileLabel || !m_workflowNextStepLabel)
    return;

  const bool pendingNonlinearModeChanged =
      m_geometryFit.pendingNonlinearEnabled && m_geometryPanel &&
      *m_geometryFit.pendingNonlinearEnabled !=
          m_geometryPanel->isNonlinearEnabled();
  if (m_geometryFit.pendingInputs &&
      (geometryFitInputsDiffer(*m_geometryFit.pendingInputs, currentState) ||
       pendingNonlinearModeChanged)) {
    // Reset first because cancelAll() may synchronously trigger progress/UI
    // callbacks that refresh this summary again.
    m_geometryFit.pendingInputs.reset();
    m_geometryFit.pendingNonlinearEnabled.reset();
    m_geometryFit.pendingRequestId.reset();
    m_solverQueue.cancelAll();
  }

  if (m_profileCalibration.pendingInputs) {
    const ColorOptimizerRequestData currentProfileInputs{
        m_scrToImgParams, m_rparams, m_profileSpots};
    if (profileCalibrationInputsDiffer(*m_profileCalibration.pendingInputs,
                                       currentProfileInputs)) {
      // Reset identity first because cancellation can synchronously drive
      // progress/UI callbacks.
      m_profileCalibration.pendingInputs.reset();
      m_profileCalibration.pendingRequestId.reset();
      m_colorOptimizerQueue.cancelAll();
    }
  }

  using capture_type =
      decltype(colorscreen::render_parameters::capture_unknown);
  const capture_type capture =
      m_scan ? m_rparams.get_capture_type(m_scan.get())
             : m_rparams.capture_type;
  const colorscreen::scr_type type = m_scrToImgParams.type;
  const bool hasScreen =
      colorscreen::render_parameters::capture_has_screen_p(capture);
  const bool colorDetection =
      colorscreen::render_parameters::capture_supports_screen_detection_p(
          capture);
  const bool regularScreen = colorscreen::screen_has_regular_geometry_p(type);
  const bool geometryConfigured =
      colorscreen::screen_geometry_configured_p(m_scrToImgParams);
  const bool stochasticScreen = colorscreen::stochastic_screen_p(type);
  const bool reconstructionModeSelected =
      m_renderTypeParams.type == colorscreen::render_type_interpolated ||
      m_renderTypeParams.type == colorscreen::render_type_predictive ||
      m_renderTypeParams.type == colorscreen::render_type_realistic ||
      m_renderTypeParams.type == colorscreen::render_type_combined;
  const int renderTypeIndex = static_cast<int>(m_renderTypeParams.type);
  const bool screenDetectionModeSelected =
      renderTypeIndex >= 0 &&
      renderTypeIndex < colorscreen::render_type_max &&
      (colorscreen::render_type_properties[renderTypeIndex].flags &
       colorscreen::render_type_property::USES_SCR_DETECT);
  const bool screenDetectionAvailable =
      colorDetection && m_scan && m_scan->has_rgb() &&
      colorscreen::screen_present_p(type);
  const ImageWidget *workflowImage = inspectorImageWidget();
  const bool registrationPointsVisible =
      workflowImage && workflowImage->registrationPointsVisible();

  QString captureName = tr("Unknown");
  const int captureIndex = static_cast<int>(capture);
  if (captureIndex >= 0 &&
      captureIndex < colorscreen::render_parameters::capture_max)
    captureName = QString::fromUtf8(
        colorscreen::render_parameters::capture_properties[captureIndex]
            .pretty_name);

  QString screenName;
  const int typeIndex = static_cast<int>(type);
  if (typeIndex >= 0 && typeIndex < colorscreen::max_scr_type &&
      colorscreen::scr_names[typeIndex].pretty_name)
    screenName =
        QString::fromUtf8(colorscreen::scr_names[typeIndex].pretty_name);

  QString processSummary;
  if (capture == colorscreen::render_parameters::capture_unknown) {
    processSummary = tr("Process: choose capture type in Digital capture");
  } else if (!hasScreen) {
    processSummary =
        tr("Process: %1 • no screen reconstruction").arg(captureName);
  } else {
    processSummary = tr("Process: %1 • %2").arg(captureName, screenName);
  }
  if (colorscreen::render_parameters::capture_negative_p(capture)) {
    processSummary += m_rparams.contact_copy.simulate
        ? tr(" • positive conversion active")
        : tr(" • positive conversion off");
  }
  m_workflowProcessLabel->setText(processSummary);

  QString imageLayerSummary;
  if (!m_scan) {
    imageLayerSummary = tr("Image layer: load an image to choose source");
  } else {
    const bool nativeScalar =
        m_scan->has_grayscale_or_ir() &&
        (!m_scan->has_rgb() || !m_rparams.ignore_infrared);
    if (nativeScalar) {
      imageLayerSummary = tr("Image layer: native grayscale/IR channel");
      const double wavelength = m_scan->wavelengths[3];
      if (colorscreen::my_isfinite(wavelength) && wavelength > 0)
        imageLayerSummary +=
            tr(" • %1 nm").arg(wavelength, 0, 'f', 0);
    } else if (m_scan->has_rgb()) {
      imageLayerSummary =
          tr("Image layer: simulated RGB — %1 R + %2 G + %3 B")
              .arg(m_rparams.mix_red, 0, 'f', 2)
              .arg(m_rparams.mix_green, 0, 'f', 2)
              .arg(m_rparams.mix_blue, 0, 'f', 2);
      if (m_rparams.mix_dark.red != 0 || m_rparams.mix_dark.green != 0 ||
          m_rparams.mix_dark.blue != 0)
        imageLayerSummary += tr(" • dark offsets set");
    } else {
      imageLayerSummary = tr("Image layer: unavailable");
    }
  }
  m_workflowImageLayerLabel->setText(imageLayerSummary);

  QString registration;
  qsizetype pointCount = static_cast<qsizetype>(m_solverParams.n_points());
  int minimumPoints = 0;
  bool fitCurrent = false;
  bool failureCurrent = false;
  if (!m_scan) {
    registration = tr("Registration: load an image to begin");
  } else if (capture == colorscreen::render_parameters::capture_unknown) {
    registration = tr("Registration: choose the capture type first");
  } else if (!hasScreen) {
    registration = tr("Registration: not applicable — no color screen");
  } else if (!regularScreen) {
    if (stochasticScreen && colorDetection) {
      registration = tr(
          "Registration: geometry not used — reconstruct from detected "
          "screen colours");
    } else if (stochasticScreen) {
      registration = tr(
          "Registration: stochastic screen recovery unavailable — "
          "monochrome capture needs the original regular Screen type");
    } else {
      registration = tr("Registration: choose a screen type");
    }
  } else {
    minimumPoints = colorscreen::solver_parameters::min_points(type);
    const QString prefix =
        colorDetection ? tr("Registration: optional geometry")
                       : tr("Registration: geometry");
    if (!geometryConfigured) {
      if (pointCount > 0) {
        registration = tr(
            "%1 — geometry not configured; %2 existing control point(s) "
            "must keep their original coordinate system")
                           .arg(prefix)
                           .arg(pointCount);
      } else {
        registration = tr(
            "%1 — geometry not configured; detect screen coordinates")
                           .arg(prefix);
      }
    } else if (pointCount == 0) {
      registration = tr("%1 — no points; detect or add at least %2")
                         .arg(prefix)
                         .arg(minimumPoints);
    } else if (pointCount < minimumPoints) {
      registration = tr("%1 — %2/%3 points; add %4 more")
                         .arg(prefix)
                         .arg(pointCount)
                         .arg(minimumPoints)
                         .arg(minimumPoints - pointCount);
    } else {
      registration = tr("%1 — %2 points").arg(prefix).arg(pointCount);
      fitCurrent = m_geometryFit.baseline &&
          !geometryFitInputsDiffer(*m_geometryFit.baseline, currentState);
      failureCurrent = m_geometryFit.failureInputs &&
          !geometryFitInputsDiffer(*m_geometryFit.failureInputs, currentState);
      if (m_geometryFit.pendingInputs) {
        registration += tr(" • fitting geometry…");
      } else if (fitCurrent) {
        registration += tr(" • geometry fitted");
        if (failureCurrent)
          registration += tr(" • last refit failed");
      } else if (failureCurrent) {
        registration += tr(" • fit failed — adjust points/settings and retry");
      } else if (m_geometryFit.baseline) {
        registration += tr(" • geometry stale — refit");
      } else {
        registration += tr(" • ready to fit geometry");
      }
      if (m_scrToImgParams.mesh_trans)
        registration += tr(" • nonlinear correction present");
    }
  }
  m_workflowRegistrationLabel->setText(registration);

  const auto &sharpen = currentState.rparams.sharpen;
  const auto &mtf = sharpen.scanner_mtf;
  using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;
  const sharpen_mode configuredSharpenMode = sharpen.mode;
  const sharpen_mode effectiveSharpenMode = sharpen.get_mode();

  auto sharpenModeName = [](sharpen_mode mode) {
    const int index = static_cast<int>(mode);
    if (index >= 0 &&
        index < static_cast<int>(sharpen_mode::sharpen_mode_max) &&
        colorscreen::sharpen_parameters::sharpen_mode_names[index].pretty_name)
      return QString::fromUtf8(
          colorscreen::sharpen_parameters::sharpen_mode_names[index]
              .pretty_name);
    return QStringLiteral("Sharpening");
  };
  auto positiveFinite = [](double value) {
    return colorscreen::my_isfinite(value) && value > 0;
  };

  QString sharpenSummary;
  if (configuredSharpenMode == sharpen_mode::none) {
    sharpenSummary = tr("Sharpening: off");
  } else if (effectiveSharpenMode == sharpen_mode::none) {
    QStringList missingActivation;
    switch (configuredSharpenMode) {
    case sharpen_mode::unsharp_mask:
      if (!positiveFinite(sharpen.usm_radius))
        missingActivation << tr("radius");
      if (!positiveFinite(sharpen.usm_amount))
        missingActivation << tr("amount");
      break;
    case sharpen_mode::wiener_deconvolution:
      if (!positiveFinite(sharpen.scanner_mtf_scale))
        missingActivation << tr("MTF scale");
      if (!positiveFinite(sharpen.scanner_snr))
        missingActivation << tr("SNR");
      break;
    case sharpen_mode::richardson_lucy_deconvolution:
      if (!positiveFinite(sharpen.scanner_mtf_scale))
        missingActivation << tr("MTF scale");
      if (sharpen.richardson_lucy_iterations <= 0)
        missingActivation << tr("iterations");
      break;
    case sharpen_mode::blur_deconvolution:
      if (!positiveFinite(sharpen.scanner_mtf_scale))
        missingActivation << tr("MTF scale");
      break;
    case sharpen_mode::none:
    case sharpen_mode::sharpen_mode_max:
      break;
    }

    sharpenSummary =
        tr("Sharpening: %1 inactive").arg(sharpenModeName(configuredSharpenMode));
    if (!missingActivation.isEmpty())
      sharpenSummary +=
          tr(" — set %1").arg(missingActivation.join(", "));
  } else if (effectiveSharpenMode == sharpen_mode::unsharp_mask) {
    sharpenSummary = tr("Sharpening: Unsharp mask active");
  } else {
    QString transferSummary;
    if (mtf.use_measured_mtf()) {
      transferSummary = tr("measured MTF");
    } else if (mtf.simulate_diffraction_p()) {
      transferSummary = tr("physical MTF");
    } else {
      transferSummary = tr("empirical MTF");
      if (mtf.model != colorscreen::mtf_model::empirical_fallback) {
        QStringList missingPhysicalInputs;
        if (!positiveFinite(mtf.pixel_pitch))
          missingPhysicalInputs << tr("pixel pitch");
        if (!positiveFinite(mtf.f_stop))
          missingPhysicalInputs << tr("f-stop");
        if (!positiveFinite(mtf.scan_dpi))
          missingPhysicalInputs << tr("resolution");
        if (!missingPhysicalInputs.isEmpty())
          transferSummary +=
              tr(" (physical model needs %1)")
                  .arg(missingPhysicalInputs.join(", "));
      }
    }
    sharpenSummary =
        tr("Sharpening: %1 • %2")
            .arg(sharpenModeName(effectiveSharpenMode), transferSummary);
  }

  // Preserve #251's richer calibration/provenance summary rather than
  // collapsing it back to a saved-measurement count.
  const QString mtfSummary = mtfCalibrationSummary();

  const bool profileApplicable =
      colorDetection && m_scan && m_scan->has_rgb();
  const QString profileSummary =
      profileApplicable ? profileCalibrationSummary() : QString();
  if (m_profilePanel)
    m_profilePanel->setCalibrationStatus(profileSummary);
  m_workflowCalibrationLabel->setText(
      sharpenSummary + QStringLiteral(" • ") + mtfSummary);
  m_workflowProfileLabel->setProperty("workflowApplicable",
                                      profileApplicable);
  m_workflowProfileLabel->setText(profileSummary);
  m_workflowProfileLabel->setVisible(
      profileApplicable && m_workflowProcessLabel->isVisible());

  QString nextStep;
  QString nextPanelKey;
  const auto screenAutodetectionProgress =
      m_screenAutodetectionProgress.lock();
  if (screenAutodetectionProgress) {
    if (screenAutodetectionProgress->pool_cancel()) {
      nextStep = m_screenAutodetectionUsesStop
          ? tr("Next: screen detection is stopping…")
          : tr("Next: screen detection is cancelling…");
    } else {
      nextStep = m_screenAutodetectionUsesStop
          ? tr("Next: screen detection is running. Wait for it to finish, or press Stop.")
          : tr("Next: screen detection is running. Wait for it to finish, or press Cancel.");
    }
  } else if (!m_scan) {
    nextStep = tr("Next: load an image.");
  } else if (capture == colorscreen::render_parameters::capture_unknown) {
    nextStep = tr("Next: choose Capture type in Digital capture.");
    nextPanelKey = QStringLiteral("digital_capture");
  } else if (colorscreen::render_parameters::capture_negative_p(capture) &&
             !m_rparams.contact_copy.simulate) {
    nextStep = tr(
        "Next: Simulated darkroom — enable Contact copy simulation to turn "
        "the negative into a positive.");
    nextPanelKey = QStringLiteral("contact_copy");
  } else if (!hasScreen) {
    nextStep = tr(
        "Next: set capture correction, black/backlight and sharpening, then "
        "render the corrected capture. Contact copy remains available for "
        "negative or photolab simulation.");
  } else if (colorDetection && stochasticScreen) {
    nextStep = tr(
        "Next: reconstruct from detected screen colours; stochastic screens "
        "do not use Geometry.");
    nextPanelKey = QStringLiteral("screen");
  } else if (screenDetectionAvailable && screenDetectionModeSelected) {
    nextStep = tr(
        "Next: screen-colour detection is selected. Refine Screen → "
        "Reconstruction if needed, then continue with Sharpness/Color. "
        "Geometry is optional for this RGB path.");
    nextPanelKey = QStringLiteral("screen");
  } else if (screenDetectionAvailable && regularScreen &&
             !geometryConfigured && pointCount > 0) {
    nextStep = tr(
        "Next: choose a reconstruction path. Mode → Image layer + "
        "auto-detected screen filter uses the RGB screen colours without "
        "Geometry. To continue the Geometry path, restore the coordinate "
        "system compatible with the existing control points or delete those "
        "points before detecting new coordinates.");
  } else if (screenDetectionAvailable && regularScreen &&
             !geometryConfigured) {
    nextStep = tr(
        "Next: choose a reconstruction path — Mode → Image layer + "
        "auto-detected screen filter uses the RGB screen colours without "
        "Geometry, or Geometry → Detect screen coordinates for lattice-based "
        "reconstruction.");
  } else if (regularScreen && !geometryConfigured && pointCount > 0) {
    nextStep = tr(
        "Next: restore the coordinate system compatible with the existing "
        "control points, or delete the points before detecting new screen "
        "coordinates.");
    nextPanelKey = QStringLiteral("geometry");
  } else if (regularScreen && !geometryConfigured) {
    nextStep = tr("Next: Geometry — detect screen coordinates.");
    nextPanelKey = QStringLiteral("geometry");
  } else if (regularScreen && m_geometryFit.pendingInputs) {
    nextStep = tr("Next: Geometry fit is running…");
  } else if (regularScreen && fitCurrent) {
    const QString pointGuidance = registrationPointsVisible
        ? tr("The green registration overlay is visible; hide it with "
             "Registration → Show Registration Points (or Geometry → Show "
             "registration points) when you want an unobstructed image.")
        : tr("Show the control points with Registration → Show Registration "
             "Points (or Geometry → Show registration points) when you want "
             "to inspect them.");
    const QString editGuidance = tr(
        "Use Select (S) to inspect/move points and Add Point (A) for missing "
        "ones. If the reconstructed screen colours are swapped, use Screen → "
        "Swap screen colors.");
    if (reconstructionModeSelected) {
      nextStep = tr("Next: inspect registration. %1 %2 When alignment is clean, "
                    "continue with Sharpness/Color.")
                     .arg(pointGuidance, editGuidance);
      nextPanelKey = QStringLiteral("geometry");
    } else {
      nextStep = tr(
          "Next: reconstruct — choose Mode → Image layer + screen filter (or "
          "Image layer + screen filter demosaiced with detail recovery). %1 %2")
                     .arg(pointGuidance, editGuidance);
    }
  } else if (colorDetection && regularScreen) {
    nextStep = tr(
        "Next: choose either Geometry-based reconstruction or screen-colour "
        "detection from the RGB scan.");
  } else if (colorscreen::render_parameters::
                 capture_requires_regular_screen_p(capture)
             && !regularScreen) {
    nextStep = tr(
        "Next: choose the original regular Screen type. Stochastic screen "
        "colors cannot be recovered from a monochrome capture.");
    nextPanelKey = QStringLiteral("screen");
  } else if (!colorDetection && regularScreen) {
    if (failureCurrent) {
      nextStep = tr(
          "Next: Geometry — adjust registration points/settings and optimize "
          "the fit again.");
    } else {
      nextStep = tr("Next: Geometry — optimize the fit.");
    }
    nextPanelKey = QStringLiteral("geometry");
  } else if (hasScreen && type == colorscreen::NoScreen) {
    nextStep = tr("Next: choose the physical Screen type.");
    nextPanelKey = QStringLiteral("screen");
  } else {
    nextStep = tr("Next: reconstruct the image and refine Color/Profile.");
  }
  m_workflowNextStepLabel->setText(nextStep);

  // Workflow navigation is intentionally conservative. Only expose a button
  // when the recommendation names one unambiguous inspector stage; choices
  // between reconstruction paths, toolbar Mode changes, file loading, and
  // running operations remain text-only.
  if (m_workflowNextStepButton && m_configTabs) {
    const int targetIndex = m_configTabs->indexOfKey(nextPanelKey);
    const bool targetIsDifferent =
        targetIndex >= 0 && targetIndex != m_configTabs->currentIndex();
    m_workflowNextStepButton->setProperty("targetPanelKey", nextPanelKey);
    m_workflowNextStepButton->setText(
        targetIndex >= 0
            ? tr("Open %1").arg(m_configTabs->tabText(targetIndex))
            : tr("Open stage"));
    m_workflowNextStepButton->setToolTip(
        targetIndex >= 0
            ? tr("Open the %1 inspector stage.")
                  .arg(m_configTabs->tabText(targetIndex))
            : QString());
    m_workflowNextStepButton->setVisible(targetIsDifferent);
  }
}

/** Refresh all UI panels and toolbar state from a ParameterState.
   Calls updateUI() on every registered panel, syncs the mirror toggle,
   nonlinear corrections checkbox, deformation chart, backlight dock
   visibility, adaptive sharpening chart, and registration group
   visibility.  Does not update ImageWidget or NavigationView directly
   (that is done by applyState).  */
void MainWindow::updateUIFromState(const ParameterState &state) {
  for (auto panel : m_panels) {
    if (panel)
      panel->updateUI();
  }
  // Sync the shared mirror action to the coordinate space of the primary view.
  if (m_mirrorAction) {
    const bool finalCoordinates = m_imageWidget &&
        m_imageWidget->coordinateSpace() == colorscreen::render_final_coordinates;
    const QSignalBlocker blocker(m_mirrorAction);
    m_mirrorAction->setChecked(finalCoordinates ? state.scrToImg.final_mirror
                                                : state.rparams.scan_mirror);
  }

  // Sync nonlinear checkbox in GeometryPanel
  m_geometryPanel->setNonlinearChecked(state.scrToImg.mesh_trans != nullptr);

  // Update deformation chart
  if (m_geometryPanel) {
    m_geometryPanel->updateDeformationChart();
  }
  updateRegistrationGroupVisibility();

  if (m_sharpnessPanel) {
    if (AdaptiveSharpeningChart *chart = m_sharpnessPanel->getAdaptiveChart())
      chart->setCorrection(state.rparams.scanner_blur_correction);
  }

  updateCoordinateSpaceControls();
  updateWorkflowSummary();
  emit documentStateChanged();
}

/** Return a copy of the shared document parameters for secondary views. */
ParameterState MainWindow::documentStateSnapshot() const {
  return getCurrentState();
}

/** Apply shared document parameters changed by a secondary/specialized view. */
void MainWindow::applySharedDocumentState(const ParameterState &state,
                                          const QString &description,
                                          const QString &parameterKey) {
  changeParameters(state, description, parameterKey);
}

/** Rotate the shared document left on behalf of a secondary view. */
void MainWindow::rotateDocumentLeft() { rotateLeft(); }

/** Rotate the shared document right on behalf of a secondary view. */
void MainWindow::rotateDocumentRight() { rotateRight(); }

/** Change shared scan mirroring on behalf of a secondary view. */
void MainWindow::setDocumentMirror(bool checked) {
  if (!m_scan)
    return;
  ParameterState newState = getCurrentState();
  if (newState.rparams.scan_mirror == checked)
    return;
  newState.rparams.scan_mirror = checked;
  changeParameters(newState, "Mirror Horizontally");
}

/** Change continuous final rotation on behalf of an ordinary view. */
void MainWindow::setDocumentFinalRotation(double degrees) {
  if (!m_scan)
    return;
  ParameterState newState = getCurrentState();
  if (newState.scrToImg.final_rotation == degrees)
    return;
  newState.scrToImg.final_rotation = degrees;
  changeParameters(newState, "Set final rotation");
}

/** Change final-coordinate mirroring on behalf of an ordinary view. */
void MainWindow::setDocumentFinalMirror(bool checked) {
  if (!m_scan)
    return;
  ParameterState newState = getCurrentState();
  if (newState.scrToImg.final_mirror == checked)
    return;
  newState.scrToImg.final_mirror = checked;
  changeParameters(newState, "Mirror final image");
}

/** Create a snapshot of the current application parameters.
   Bundles render_parameters, scr_to_img_parameters, scr_detect_parameters,
   solver_parameters, and profile spots into a ParameterState struct for
   use in undo commands and state comparisons.  */
ParameterState MainWindow::getCurrentState() const {
  ParameterState state;
  state.rparams = m_rparams;
  state.scrToImg = m_scrToImgParams;
  state.detect = m_detectParams;
  state.solver = m_solverParams;
  state.profileSpots = m_profileSpots;
  return state;
}

/** Push an undoable parameter change.
   Compares the current state with NEWSTATE; if different, creates a
   ChangeParametersCommand and pushes it onto the undo stack.
   DESCRIPTION appears in the Edit > Undo/Redo menu text. PARAMETERKEY is a
   stable machine-readable merge identity; when empty, DESCRIPTION preserves
   the historical behavior for controls not migrated yet. */
void MainWindow::changeParameters(const ParameterState &newState,
                                  const QString &description,
                                  const QString &parameterKey) {
  ParameterState currentState = getCurrentState();
  if (currentState == newState)
    return;

  m_undoStack->push(new ChangeParametersCommand(
      this, currentState, newState, description, parameterKey));
}

// Save the current interaction mode so it can be restored later.
// We ignore GenericAreaMode to avoid "saving" a temporary selection state.
void MainWindow::saveInteractionMode() {
  if (inspectorImageWidget()->interactionMode() != ImageWidget::GenericAreaMode)
    m_previousInteractionMode = inspectorImageWidget()->interactionMode();
}

// Return to the interaction mode that was active before a temporary operation
// started. This handles both setting the ImageWidget mode and updating the
// checked state of the corresponding toolbar actions.
void MainWindow::restoreInteractionMode() {
  inspectorImageWidget()->setInteractionMode(m_previousInteractionMode);
}


// Recent Parameters Implementation

/** Zoom in by 25% with smooth animation.  */
void MainWindow::onZoomIn() {
  if (ImageWidget *image = inspectorImageWidget())
    image->smoothZoomBy(1.25);
}

/** Zoom out by ~10% with smooth animation.  */
void MainWindow::onZoomOut() {
  if (ImageWidget *image = inspectorImageWidget())
    image->smoothZoomBy(1.0 / 1.1); // Zoom out by 10%
}

/** Zoom to 100% (1:1 pixel scale) with smooth animation.  */
void MainWindow::onZoom100() {
  if (ImageWidget *image = inspectorImageWidget())
    image->smoothZoomTo(1.0, true);
}
/** Toggle nonlinear mesh corrections.
   When enabling: if no mesh exists yet, triggers a full geometry
   optimization to compute one.  When disabling: clears the mesh_trans
   pointer and pushes an undo command.  */
void MainWindow::onNonlinearToggled(bool checked) {
  updateScreenCoordinateToolPresentation();
  if (checked) {
    // If not already set, trigger optimization
    if (!m_scrToImgParams.mesh_trans) {
      onOptimizeGeometry(
          false); // pass false for Auto assuming button is manual
    }
  } else {
    // If set, clear it
    if (m_scrToImgParams.mesh_trans) {
      ParameterState newState = getCurrentState();
      newState.scrToImg.mesh_trans = nullptr;
      changeParameters(newState, "Disable Nonlinear Corrections");
    }
    // With no materialized mesh the checkbox is otherwise panel-local. A
    // running nonlinear fit must still notice that the user turned it off.
    updateWorkflowSummary();
  }
}

/** Zoom to fit the entire image in the viewer with smooth animation.  */
void MainWindow::onZoomFit() {
  if (ImageWidget *image = inspectorImageWidget())
    image->smoothFitToView();
}

/** Toggle visibility of registration points in the ImageWidget.  */
void MainWindow::onRegistrationPointsToggled(bool checked) {
  if (ImageWidget *image = inspectorImageWidget())
    image->setShowRegistrationPoints(checked);
}

/** Request a geometry optimisation via the solver queue.
   Captures the current scr_to_img and solver parameters along with
   the nonlinear mesh flag, and submits them to m_solverQueue which
   will cancel any in-flight solve and start a new one.  */
void MainWindow::onOptimizeGeometry(bool /*autoChecked*/) {
  if (!m_geometryPanel)
    return;
  requestGeometryOptimization(m_geometryPanel->isNonlinearEnabled());
}

/** Submit one geometry fit with independent mesh recomputation and UI-mode gates. */
void MainWindow::requestGeometryOptimization(bool computeMesh) {
  if (!m_scan || !m_solverWorker || !m_geometryPanel)
    return;

  SolverRequestData data;
  data.scrToImg = m_scrToImgParams;
  data.solver = m_solverParams;
  data.computeMesh = computeMesh;

  // The document snapshot and current nonlinear presentation mode form the
  // domain-level stale gate beyond TaskQueue's newest-request check. The
  // worker's COMPUTEMESH choice is deliberately independent: automatic screen
  // detection can refine the remaining geometry while preserving its mesh.
  m_geometryFit.pendingInputs = getCurrentState();
  m_geometryFit.pendingNonlinearEnabled = m_geometryPanel->isNonlinearEnabled();
  m_geometryFit.pendingRequestId.reset();
  m_geometryFit.failureInputs.reset();
  updateWorkflowSummary();

  m_solverQueue.requestRender(QVariant::fromValue(data));
}

/** TaskQueue callback that dispatches the solver request to the
   GeometrySolverWorker running in m_solverThread.
   Called on the main thread when the queue is ready to execute.
   Invokes the worker's solve() method via QMetaObject for thread-safe
   cross-thread invocation.  */
void MainWindow::onTriggerSolve(
    int reqId, std::shared_ptr<colorscreen::progress_info> progress,
    const QVariant &userData) {
  // A pending queue request has no progress object yet, so bind its concrete
  // identity when TaskQueue actually dispatches it.
  if (m_geometryFit.pendingInputs)
    m_geometryFit.pendingRequestId = reqId;

  if (!m_scan || !m_solverWorker || !userData.canConvert<SolverRequestData>()) {
    m_solverQueue.reportFinished(reqId, false);
    if (m_geometryFit.pendingRequestId &&
        *m_geometryFit.pendingRequestId == reqId) {
      m_geometryFit.pendingInputs.reset();
      m_geometryFit.pendingNonlinearEnabled.reset();
      m_geometryFit.pendingRequestId.reset();
      updateWorkflowSummary();
    }
    return;
  }

  SolverRequestData data = userData.value<SolverRequestData>();

  if (progress) {
    progress->set_task("Optimizing geometry", 1);
  }
  // colorscreen::sub_task task (progress.get ());

  // Invoke solver in worker
  QMetaObject::invokeMethod(
      m_solverWorker, "solve", Qt::QueuedConnection, Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, data.scrToImg),
      Q_ARG(colorscreen::solver_parameters, data.solver),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
      Q_ARG(bool, data.computeMesh));
}

/** Handle geometry solver completion.
   On success, merges the solver's optimised parameters (center, tilt,
   lens, perspective, mesh) into the current state and pushes an undo
   command.  On failure, shows a warning unless the solver was cancelled.  */
void MainWindow::onSolverFinished(int reqId,
                                  colorscreen::scr_to_img_parameters result,
                                  bool success, bool cancelled) {
  // TaskQueue suppresses superseded requests. The pending input snapshot adds
  // a domain-level gate: even the newest request is obsolete if its geometry
  // inputs changed without starting another solve.
  const bool publishable = m_solverQueue.reportFinished(reqId, success);
  if (m_closing)
    return;

  // reportFinished() deliberately rejects cancelled and superseded work.
  // Request identity, not publishability, determines which completion owns
  // cleanup of the session-local "fitting geometry" provenance.
  if (!m_geometryFit.pendingRequestId ||
      *m_geometryFit.pendingRequestId != reqId)
    return;

  const ParameterState now = getCurrentState();
  const bool nonlinearModeStillCurrent =
      m_geometryFit.pendingNonlinearEnabled && m_geometryPanel &&
      *m_geometryFit.pendingNonlinearEnabled ==
          m_geometryPanel->isNonlinearEnabled();
  const bool inputsStillCurrent =
      m_geometryFit.pendingInputs && nonlinearModeStillCurrent &&
      !geometryFitInputsDiffer(*m_geometryFit.pendingInputs, now);
  m_geometryFit.pendingInputs.reset();
  m_geometryFit.pendingNonlinearEnabled.reset();
  m_geometryFit.pendingRequestId.reset();

  if (!publishable || cancelled || !inputsStillCurrent) {
    updateWorkflowSummary();
    return;
  }

  if (success) {
    ParameterState newState = now;
    newState.scrToImg.merge_solver_solution(result);
    changeParameters(newState, "Optimize Geometry");
    m_geometryFit.baseline = getCurrentState();
    updateScreenCoordinateToolPresentation();
    m_geometryFit.failureInputs.reset();
  } else {
    m_geometryFit.failureInputs = now;
    QMessageBox::warning(this, "Optimization Failed",
                         "The geometry solver failed to find a solution.");
  }
  updateWorkflowSummary();
}

/** Update the color (IR/RGB) checkbox visibility and enabled state.
   Visible only when RGB scan data is available.  Enabled only when the
   current render type supports the IR/RGB switch.  Syncs the checked
   state with m_renderTypeParams.color while blocking signals to prevent
   recursive updates.  */
void MainWindow::updateColorCheckBoxState() {
  if (!m_colorCheckBox || !m_colorCheckBoxAction)
    return;

  bool hasRgb = m_scan && m_scan->has_rgb();

  // Calculate enabled state based on render type support
  using namespace colorscreen;
  const render_type_property &prop =
      render_type_properties[(int)m_renderTypeParams.type];
  bool supportsColorSwitch =
      prop.flags & render_type_property::SUPPORTS_IR_RGB_SWITCH;

  // Final Visibility Rule:
  // Must have RGB data AND (optionally) rely on render type logic if we wanted
  // to hide it for non-supported types. But user request specifically says
  // "invisible when m_scan->rgbdata is NULL".

  bool isVisible = hasRgb;
  bool isEnabled = supportsColorSwitch && hasRgb;

  m_colorCheckBoxAction->setVisible(isVisible);
  m_colorCheckBox->setVisible(isVisible);
  m_colorCheckBox->setEnabled(isEnabled);

  const QSignalBlocker blocker(m_colorCheckBox);
  if (!hasRgb) {
    m_colorCheckBox->setChecked(false);
  } else {
    m_colorCheckBox->setChecked(m_renderTypeParams.color);
  }
}

/** Show or hide restoration controls that depend on capture/screen type.
   Geometry actions require a loaded screen capture with a regular lattice;
   ordinary and unknown captures keep only the general capture-processing
   stages. If geometry becomes unavailable, return to Pan mode.  */
void MainWindow::updateRegistrationGroupVisibility() {
  const auto capture =
      m_scan ? m_rparams.get_capture_type(m_scan.get())
             : colorscreen::render_parameters::capture_unknown;
  const bool hasScreenCapture =
      m_scan && colorscreen::render_parameters::capture_has_screen_p(capture);
  const bool hasScreenColorData =
      m_scan && colorscreen::render_parameters::
                    capture_supports_screen_detection_p(capture);
  const bool hasRegularGeometry =
      hasScreenCapture && colorscreen::screen_has_regular_geometry_p(
                              m_scrToImgParams.type);

  for (QAction *action : m_registrationActions)
    action->setVisible(hasRegularGeometry);

  if (m_registrationMenu)
    m_registrationMenu->menuAction()->setVisible(hasRegularGeometry);

  // Keep the beta tab order stable. Hide specialist color-screen stages that
  // do not apply to ordinary/unknown captures. Color and Contact copy remain
  // because their general appearance/darkroom controls are useful without an
  // additive screen. Stochastic RGB screen captures retain Screen/Color/Profile
  // while Geometry is hidden.
  if (m_configTabs) {
    const auto setPanelVisible = [this](QWidget *panel, bool visible) {
      const int index = m_configTabs->indexOf(panel);
      if (index >= 0)
        m_configTabs->setTabVisible(index, visible);
    };
    setPanelVisible(m_screenPanel, hasScreenCapture);
    setPanelVisible(m_geometryPanel, hasRegularGeometry);
    setPanelVisible(m_contactCopyPanel, m_scan != nullptr);
    // Color also owns generic black/backlight/output appearance controls. The
    // panel itself hides its historical dye sections for ordinary/unknown
    // captures, so keep the tab available whenever an image is loaded.
    setPanelVisible(m_colorPanel, m_scan != nullptr);
    setPanelVisible(m_profilePanel,
                    hasScreenColorData && m_scan && m_scan->has_rgb());
  }

  updateScreenCoordinateToolPresentation();

  if (!hasRegularGeometry &&
      (m_selectAction->isChecked() || m_addPointAction->isChecked() ||
       m_setCenterAction->isChecked()))
    m_panAction->setChecked(true);
}

/** Toggle the gamut warning overlay.
   When enabled, out-of-gamut colors are highlighted in the rendered image.
   Updates m_rparams.gamut_warning and triggers a re-render.  */
void MainWindow::onGamutWarningToggled(bool checked) {
  if (m_rparams.gamut_warning != checked) {
    m_rparams.gamut_warning = checked;

    // Trigger update
    if (m_scan) {
      m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                      &m_detectParams, &m_renderTypeParams,
                                      &m_solverParams);
    }
  }
}

// Crash Recovery Methods

/** Select all registration points in the ImageWidget.  */
void MainWindow::onSelectAll() {
  if (ImageWidget *image = inspectorImageWidget())
    image->selectAll();
}

/** Clear the current registration point selection.  */
void MainWindow::onDeselectAll() {
  if (ImageWidget *image = inspectorImageWidget())
    image->clearSelection();
}

/** Delete all currently selected registration points.  */
void MainWindow::onDeleteSelected() {
  if (ImageWidget *image = inspectorImageWidget())
    image->deleteSelectedPoints();
}

/** Remove registration points with high error from the selection.
   Builds a histogram of point-to-predicted-position distances, finds
   a threshold at the 10% tail, and removes all selected points
   exceeding that threshold.  Pushes an undo command and triggers
   auto-solver if enabled.  */
void MainWindow::onPruneMisplaced() {
  ImageWidget *image = inspectorImageWidget();
  if (!m_scan || !image)
    return;

  const auto &selectedPoints = image->selectedPoints();
  if (selectedPoints.empty()) {
    return;
  }

  // Create map for current geometry
  colorscreen::scr_to_img map;
  if (!map.set_parameters(m_scrToImgParams, *m_scan))
    return;

  // Build histogram of distances
  colorscreen::histogram hist;

  const auto &points = m_solverParams.points;

  // First pass: pre-account all distances
  for (const auto &sp : selectedPoints) {
    if (sp.type == ImageWidget::SelectedPoint::RegistrationPoint &&
        sp.index < points.size()) {
      const auto &point = points[sp.index];

      colorscreen::coord_t dist;
      if (!colorscreen::screen_with_vertical_strips_p(m_scrToImgParams.type)) {
        colorscreen::point_t predicted = map.to_img(point.scr);
        dist = predicted.dist_from(point.img);
      } else {
        colorscreen::point_t predicted = map.to_scr(point.img);
        dist = fabs(predicted.x - point.scr.x);
      }
      hist.pre_account(dist);
    }
  }

  hist.finalize_range(65536);

  // Second pass: account distances
  for (const auto &sp : selectedPoints) {
    if (sp.type == ImageWidget::SelectedPoint::RegistrationPoint &&
        sp.index < points.size()) {
      const auto &point = points[sp.index];

      colorscreen::coord_t dist;
      if (!colorscreen::screen_with_vertical_strips_p(m_scrToImgParams.type)) {
        colorscreen::point_t predicted = map.to_img(point.scr);
        dist = predicted.dist_from(point.img);
      } else {
        colorscreen::point_t predicted = map.to_scr(point.img);
        dist = fabs(predicted.x - point.scr.x);
      }
      hist.account(dist);
    }
  }

  hist.finalize();
  colorscreen::coord_t threshold = hist.find_max(0.1);

  // Remove points exceeding threshold
  ParameterState oldState = getCurrentState();

  // Collect indices to remove (in reverse order to avoid index shifting issues)
  std::vector<size_t> indicesToRemove;
  for (const auto &sp : selectedPoints) {
    if (sp.type == ImageWidget::SelectedPoint::RegistrationPoint &&
        sp.index < points.size()) {
      const auto &point = points[sp.index];

      colorscreen::coord_t dist;
      if (!colorscreen::screen_with_vertical_strips_p(m_scrToImgParams.type)) {
        colorscreen::point_t predicted = map.to_img(point.scr);
        dist = predicted.dist_from(point.img);
      } else {
        colorscreen::point_t predicted = map.to_scr(point.img);
        dist = fabs(predicted.x - point.scr.x);
      }

      if (dist > threshold) {
        indicesToRemove.push_back(sp.index);
      }
    }
  }

  // Sort in descending order and remove
  std::sort(indicesToRemove.begin(), indicesToRemove.end(),
            std::greater<size_t>());
  for (size_t idx : indicesToRemove) {
    m_solverParams.remove_point(idx);
  }

  // Update UI
  m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                  &m_detectParams, &m_renderTypeParams,
                                  &m_solverParams);
  m_imageWidget->clearSelection();
  m_imageWidget->update();

  // Create undo command
  ParameterState newState = getCurrentState();
  m_undoStack->push(new ChangeParametersCommand(this, oldState, newState,
                                                "Prune misplaced points"));

  // Trigger auto solver if enabled
  if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
    ImageWidget *image = inspectorImageWidget();
    size_t count = image ? image->registrationPointCount() : 0;
    if (count >= (size_t)colorscreen::solver_parameters::min_points(m_scrToImgParams.type)) {
      onOptimizeGeometry(true);
    }
  }
  updateRegistrationActions();
}

/** Return whether manual linear screen-coordinate editing belongs in the UI. */
bool MainWindow::screenCoordinateToolAvailable() const {
  if (!m_scan)
    return false;
  const auto capture = m_rparams.get_capture_type(m_scan.get());
  if (!colorscreen::render_parameters::capture_has_screen_p(capture) ||
      !colorscreen::screen_has_regular_geometry_p(m_scrToImgParams.type))
    return false;
  if (m_scrToImgParams.mesh_trans ||
      (m_geometryPanel && m_geometryPanel->isNonlinearEnabled()))
    return false;
  return !(m_geometryFit.baseline &&
           screenGeometryMatchesFitBaseline(*m_geometryFit.baseline,
                                            getCurrentState()));
}

/** Keep manual coordinate controls scoped to the Screen coordinates tool. */
void MainWindow::updateScreenCoordinateToolPresentation() {
  ImageWidget *image = inspectorImageWidget();
  const bool available = screenCoordinateToolAvailable();
  const bool active = available && image &&
      image->interactionMode() == ImageWidget::SetCenterMode;
  const bool configured = m_scan &&
      colorscreen::screen_geometry_configured_p(m_scrToImgParams);

  if (m_setCenterAction) {
    m_setCenterAction->setVisible(available);
    m_setCenterAction->setEnabled(available);
    if (available && image) {
      switch (image->screenCoordinateSetupStage()) {
      case ImageWidget::ScreenCoordinateSetupStage::NeedCenter:
        m_setCenterAction->setToolTip(
            tr("Screen coordinates: click a green dot for the center (C)"));
        break;
      case ImageWidget::ScreenCoordinateSetupStage::NeedXAxis:
        m_setCenterAction->setToolTip(tr(
            "Screen coordinates: click the neighboring green dot along +X"));
        break;
      case ImageWidget::ScreenCoordinateSetupStage::Editing:
        m_setCenterAction->setToolTip(tr(
            "Screen coordinates (C): drag center; right/Ctrl-drag X; middle/Alt-drag Y"));
        break;
      }
    }
  }
  if (m_lockRelativeCoordinatesAction) {
    m_lockRelativeCoordinatesAction->setVisible(active);
    m_lockRelativeCoordinatesAction->setEnabled(active && configured);
  }
  if (m_optimizeCoordinatesAction) {
    m_optimizeCoordinatesAction->setVisible(active);
    m_optimizeCoordinatesAction->setEnabled(active && configured);
  }

  if (!available && image &&
      image->interactionMode() == ImageWidget::SetCenterMode && m_panAction &&
      !m_panAction->isChecked())
    m_panAction->setChecked(true);
}

/** Update enabled state of all registration-related menu actions.
   Enables select/delete/prune based on current selection, enables
   optimize based on minimum point count for the current screen type,
   and calls GeometryPanel::updateRegistrationPointInfo() to refresh
   the panel's status display. */
void MainWindow::updateRegistrationActions() {
  ImageWidget *image = inspectorImageWidget();
  bool hasPoints = image && image->registrationPointsVisible() &&
                   image->registrationPointCount() > 0;
  bool hasSelection = image && !image->selectedPoints().empty();
  const bool geometryConfigured = m_scan &&
      colorscreen::screen_geometry_configured_p(m_scrToImgParams);

  // Disable selection actions if registration points aren't visible
  if (m_selectAllAction) {
    m_selectAllAction->setEnabled(hasPoints);
  }
  if (m_deselectAllAction) {
    m_deselectAllAction->setEnabled(hasSelection);
  }
  if (m_deleteSelectedAction) {
    m_deleteSelectedAction->setEnabled(hasSelection);
  }
  if (m_pruneMisplacedAction) {
    m_pruneMisplacedAction->setEnabled(hasSelection && geometryConfigured);
  }

  // Add Point and Set Center need a loaded image and a regular screen lattice.
  if (m_addPointAction) {
    bool canAddPoints = geometryConfigured;
    m_addPointAction->setEnabled(canAddPoints);
    // If tool is active but we can't add points, switch to Pan mode
    if (!canAddPoints && m_addPointAction->isChecked()) {
      m_panAction->setChecked(true);
    }
  }
  if (m_setCenterAction)
    m_setCenterAction->setEnabled(screenCoordinateToolAvailable());

  // Solver points are document state. Do not derive coordinate-system
  // safety from whether the active view currently shows registration points.
  const size_t documentPointCount = m_solverParams.n_points();
  size_t count = image ? image->registrationPointCount() : documentPointCount;

  // Coordinate autodetection now uses the shared one-shot cancellation and
  // exact snapshot gate, which also rejects any newly added control point.

  int min_points = colorscreen::solver_parameters::min_points(m_scrToImgParams.type);
  if (m_selectAllAction) {
    m_selectAllAction->setEnabled(count > 0);
  }
  if (m_optimizeGeometryAction) {
    m_optimizeGeometryAction->setEnabled(count >= (size_t)min_points);
  }
  if (m_optimizeCoordinatesAction)
    m_optimizeCoordinatesAction->setEnabled(
        geometryConfigured && screenCoordinateToolAvailable());

  updateScreenCoordinateToolPresentation();

  // Update buttons in GeometryPanel is now handled by the panel itself
  if (m_geometryPanel) {
    m_geometryPanel->updateRegistrationPointInfo(getCurrentState());
  }
  updateWorkflowSummary();
}

/** Save a state snapshot before a point drag operation begins.
   This snapshot becomes the "old state" for the undo command that
   is created when the drag finishes in maybeTriggerAutoSolver().  */
void MainWindow::onPointManipulationStarted() {
  m_undoSnapshot = getCurrentState();
}

/** Called after a point drag or point addition via ImageWidget.
   Creates an undo command if the state changed, then triggers
   the auto-solver if enabled and enough points exist.  */
void MainWindow::maybeTriggerAutoSolver() {
  ImageWidget *image = qobject_cast<ImageWidget *>(sender());
  if (!acceptsInspectorImageWidget(image))
    image = inspectorImageWidget();

  ParameterState newState = getCurrentState();
  if (newState != m_undoSnapshot) {
    m_undoStack->push(new ChangeParametersCommand(
        this, m_undoSnapshot, newState, "Move registration point"));
    m_undoSnapshot = newState;
  }

  if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
    size_t count = image ? image->registrationPointCount() : 0;
    if (count >= (size_t)colorscreen::solver_parameters::min_points(m_scrToImgParams.type)) {
      onOptimizeGeometry(true); // Trigger solver (auto=true)
    }
  }
  updateRegistrationActions();
}

/** Handle a new point added by clicking in the ImageWidget.
   Three mutually exclusive behaviours:
   1. Profile spot mode (m_addingProfileSpot): converts the image position
      to screen coordinates and adds it as a color calibration spot.
   2. Focus analysis mode (m_focusAnalysisPending): launches a
      FocusAnalysisWorker at the clicked position to measure MTF.
   3. Normal mode: runs synchronous finetune to snap the click to the
      nearest screen element, adds the resulting registration point to
      solver_parameters, updates the image widget, creates an undo
      command, and triggers auto-solver if enabled.  */
void MainWindow::onPointAdded(colorscreen::point_t imgPos,
                              colorscreen::point_t scrPos,
                              colorscreen::point_t color) {
  if (!m_scan)
    return;

  ImageWidget *image = qobject_cast<ImageWidget *>(sender());
  if (!acceptsInspectorImageWidget(image))
    image = inspectorImageWidget();

  // Profile spot mode: convert img coords → screen coords and store
  if (m_addingProfileSpot) {
    colorscreen::scr_to_img map;
    if (!map.set_parameters(m_scrToImgParams, *m_scan)) {
      statusBar()->showMessage(tr("Fit screen geometry before adding profile spots."), 3000);
      return;
    }
    colorscreen::point_t screen = map.to_scr(imgPos);
    ParameterState newState = getCurrentState();
    newState.profileSpots.push_back(screen);
    changeParameters(newState, "Add profile spot");
    return;
  }

  if (m_focusAnalysisPending) {
    m_focusAnalysisPending = false;
    restoreInteractionMode();

    colorscreen::finetune_parameters fparam;
    fparam.multitile = 3;
    fparam.range = 4;
    fparam.flags = m_focusAnalysisFlags;
    fparam.flags |= colorscreen::finetune_position | colorscreen::finetune_bw |
                    colorscreen::finetune_verbose |
                    colorscreen::finetune_produce_images;

    const std::shared_ptr<colorscreen::image_data> scan = m_scan;
    const ParameterState baseline = getCurrentState();
    auto result = std::make_shared<FocusAnalysisResult>();

    OneShotOperation operation;
    operation.description = tr("Focus analysis");
    operation.resultValid = [this, scan, baseline]() {
      return m_scan == scan && getCurrentState() == baseline;
    };
    operation.applyResult = [this, result]() {
      if (!result->success) {
        if (!result->cancelled)
          statusBar()->showMessage(tr("Focus analysis failed"), 3000);
        return;
      }

      ParameterState newState = getCurrentState();
      newState.rparams.sharpen.scanner_mtf.sigma =
          result->finetune.scanner_mtf_sigma;
      newState.rparams.sharpen.scanner_mtf.defocus =
          result->finetune.scanner_mtf_defocus;
      newState.rparams.sharpen.scanner_mtf.blur_diameter =
          result->finetune.scanner_mtf_blur_diameter;
      changeParameters(newState, tr("Focus analysis"));
      if (m_sharpnessPanel)
        m_sharpnessPanel->updateFinetuneImages(result->finetune);
      statusBar()->showMessage(tr("Focus analysis complete"), 3000);
    };
    operation.onDone = [this]() {
      if (m_sharpnessPanel)
        m_sharpnessPanel->setFocusAnalysisChecked(false);
    };

    runOneShotOperation(
        std::move(operation),
        [rparams = baseline.rparams, scrToImg = baseline.scrToImg, scan,
         imgPos, fparam, result](colorscreen::progress_info *progress) mutable {
          *result = FocusAnalysisWorker::analyze(
              std::move(rparams), std::move(scrToImg), scan, imgPos,
              std::move(fparam), progress);
        });
    return;
  }

  // Run finetune to get the accurate screen location and color
  colorscreen::finetune_parameters fparam;
  fparam.multitile = 3;
  fparam.flags |= colorscreen::finetune_position | colorscreen::finetune_bw |
                  colorscreen::finetune_verbose |
                  colorscreen::finetune_use_strip_widths |
                  colorscreen::finetune_produce_images;

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Adding control points", 0);
  colorscreen::sub_task task(progress.get()); /* Keep so tasks are nested.  */
  addProgress(progress);

  colorscreen::finetune_result res = colorscreen::finetune(
      m_rparams, m_scrToImgParams, *m_scan, {{imgPos.x, imgPos.y}}, nullptr,
      fparam, progress.get());

  removeProgress(progress);

  if (res.success) {
    // Snapshot state for undo
    ParameterState oldState = getCurrentState();

    // Add the point to solver parameters
    m_solverParams.add_point(res.solver_point_img_location,
                             res.solver_point_screen_location,
                             res.solver_point_color);

    // Refresh the canvas that produced the click.
    if (image) {
      image->updateParameters(&m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams,
                              &m_solverParams);
      image->update();
    }

    // Create undo command with correct description
    ParameterState newState = getCurrentState();
    m_undoStack->push(new ChangeParametersCommand(this, oldState, newState,
                                                  "Add registration point"));

    // Update finetune diagnostic images
    if (m_geometryPanel) {
      m_geometryPanel->updateFinetuneImages(res);
    }

    // Trigger auto solver if enabled
    if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
      size_t count = image ? image->registrationPointCount() : 0;
      if (count >= (size_t)colorscreen::solver_parameters::min_points(m_scrToImgParams.type)) {
        onOptimizeGeometry(true);
      }
    }
    updateRegistrationActions();
  }
}

/** Enter or exit crop mode.
   If already in crop mode, restores the previous tool.  Otherwise, clears
   any existing crop (so the full image is visible for re-selection),
   saves the current interaction mode, and enters CropMode.  Preserves
   the viewport center across the crop state change.  */
void MainWindow::onCropRequested() {
  if (inspectorImageWidget()->interactionMode() == ImageWidget::CropMode) {
    restoreInteractionMode();
    statusBar()->clearMessage();
    return;
  }

  if (!m_scan)
    return;

  // Preserve center across crop state change
  colorscreen::point_t center =
      inspectorImageWidget()->widgetToImage(inspectorImageWidget()->rect().center());

  ParameterState state = getCurrentState();
  if (state.rparams.scan_crop.set) {
    state.rparams.scan_crop.set = false;
    changeParameters(state, "Clear crop for re-cropping");
    inspectorImageWidget()->centerOn(center);
  }

  saveInteractionMode();
  inspectorImageWidget()->setInteractionMode(ImageWidget::CropMode);
  inspectorImageWidget()->setAreaSelectionInstruction(tr("Select crop"));
  statusBar()->showMessage(tr("Select crop"));
}

/** Enter generic area selection mode with a callback.
   Saves the current tool, switches to GenericAreaMode, and shows MESSAGE
   in the status bar.  When the user draws a rectangle, onAreaSelected()
   invokes the CALLBACK with the image-space rectangle.  If called while
   already in GenericAreaMode, cancels the selection and restores the
   previous tool.  */
void MainWindow::startAreaSelection(const QString &message,
                                    std::function<void(QRect)> callback) {
  if (!m_scan)
    return;

  if (inspectorImageWidget()->interactionMode() == ImageWidget::GenericAreaMode) {
    restoreInteractionMode();
    statusBar()->clearMessage();
    m_areaSelectionCallback = nullptr;
    if (m_imageLayerPanel)
      m_imageLayerPanel->setNeutralAreaChecked(false);
    return;
  }

  m_areaSelectionCallback = callback;
  saveInteractionMode();
  inspectorImageWidget()->setInteractionMode(ImageWidget::GenericAreaMode);
  inspectorImageWidget()->setAreaSelectionInstruction(message);
  statusBar()->showMessage(message);
}

/** Close stale final-result confirmations without applying their results. */
void MainWindow::dismissOneShotPrompts() {
  if (QDialog *prompt = m_detectScreenPrompt.data()) {
    // Clear first: close() emits finished, whose callback must recognize that
    // this prompt no longer owns publication, even after a later Undo.
    m_detectScreenPrompt = nullptr;
    prompt->close();
  }
  if (QMessageBox *prompt = m_focusAreaAnalysis.prompt.data()) {
    m_focusAreaAnalysis.prompt = nullptr;
    prompt->close();
  }
}

/** Delegate one final-result operation to the shared lifecycle controller. */
void MainWindow::runOneShotOperation(
    OneShotOperation operation,
    std::function<void(colorscreen::progress_info *)> worker) {
  m_oneShotOperations.run(std::move(operation), std::move(worker));
}

/** Launch an area-based parameter computation.
   The image and complete ParameterState are snapshotted after the rectangle is
   chosen. The worker edits a private copy. A newer one-shot request, explicit
   cancellation, image replacement, or any intervening document edit vetoes
   publication, so an old whole-state snapshot can never overwrite newer work. */
void MainWindow::runAreaComputation(
    const QString &message,
    const QString &description,
    std::function<void()> onStart,
    std::function<void()> onDone,
    std::function<void(ParameterState &, colorscreen::image_data &,
                       const colorscreen::int_image_area &,
                       colorscreen::progress_info *)> worker) {
  startAreaSelection(message, [this, description, onStart, onDone,
                               worker](QRect area) {
    if (area.width() <= 0 || area.height() <= 0 || !m_scan)
      return;

    const auto scan = m_scan;
    const ParameterState baseline = getCurrentState();
    auto result = std::make_shared<ParameterState>(baseline);

    OneShotOperation operation;
    operation.description = description;
    operation.prerequisites =
        [this, scan]() { return !m_closing && m_scan == scan; };
    operation.onStart = [onStart = std::move(onStart)](
                            std::shared_ptr<colorscreen::progress_info>) {
      if (onStart)
        onStart();
    };
    operation.resultValid = [this, scan, baseline]() {
      return m_scan == scan && getCurrentState() == baseline;
    };
    operation.applyResult = [this, result, description]() {
      changeParameters(*result, description);
    };
    operation.onDone = std::move(onDone);

    runOneShotOperation(
        std::move(operation),
        [scan, result, area, worker](colorscreen::progress_info *progress) {
          worker(*result, *scan,
                 {area.x(), area.y(), area.width(), area.height()}, progress);
        });
  });
}

/** Convert a widget-local rectangle to the enclosing scan rectangle. */
QRect MainWindow::getImageArea(QRect area, ImageWidget *imageWidget) {
  ImageWidget *image = imageWidget ? imageWidget : inspectorImageWidget();
  return image ? image->widgetAreaToImageArea(area) : QRect();
}

/** Dispatch a drawn rectangle to the appropriate handler based on the
   current interaction mode.
   - GenericAreaMode: invokes the m_areaSelectionCallback and restores
     the previous tool.
   - CropMode: sets the crop rectangle in the parameter state.
   - SelectMode/AddPointMode: runs a one-shot finetune to find
     registration points in the selected area.  */
void MainWindow::onAreaSelected(QRect area) {
  ImageWidget *image = qobject_cast<ImageWidget *>(sender());
  if (!image)
    image = inspectorImageWidget();
  if (!m_scan || !image || image != inspectorImageWidget())
    return;

  QRect imgArea = getImageArea(area, image);
  if (imgArea.width() <= 0 || imgArea.height() <= 0)
    return;

  if (image->interactionMode() == ImageWidget::GenericAreaMode) {
    auto cb = m_areaSelectionCallback;
    m_areaSelectionCallback =
        nullptr; // Clear first so interactionModeChanged doesn't uncheck
    restoreInteractionMode();
    statusBar()->clearMessage();
    if (cb) {
      cb(imgArea);
    }
    return;
  }

  if (image->interactionMode() == ImageWidget::CropMode) {
    // Preserve center
    colorscreen::point_t center =
        image->widgetToImage(image->rect().center());

    ParameterState state = getCurrentState();
    state.rparams.scan_crop.x = imgArea.x();
    state.rparams.scan_crop.y = imgArea.y();
    state.rparams.scan_crop.width = imgArea.width();
    state.rparams.scan_crop.height = imgArea.height();
    state.rparams.scan_crop.set = true;

    changeParameters(state, "Set Crop Area");

    // Keep center
    image->centerOn(center);

    restoreInteractionMode();
    statusBar()->clearMessage();
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  const colorscreen::int_image_area selectedArea = {
      imgArea.x(), imgArea.y(), imgArea.width(), imgArea.height()};
  const colorscreen::finetune_area_parameters finetuneParams =
      m_geometryPanel->finetuneAreaParams();
  auto result = std::make_shared<FinetuneAreaResult>();

  OneShotOperation operation;
  operation.description = tr("Finding registration points");
  operation.prerequisites =
      [this, scan]() { return !m_closing && m_scan == scan; };
  operation.resultValid =
      [this, scan, baseline, finetuneParams, result]() {
        if (m_scan != scan || getCurrentState() != baseline ||
            result->cancelled || !m_geometryPanel)
          return false;
        const colorscreen::finetune_area_parameters currentParams =
            m_geometryPanel->finetuneAreaParams();
        return currentParams.grid_width == finetuneParams.grid_width &&
               currentParams.grid_height == finetuneParams.grid_height &&
               currentParams.min_contrast == finetuneParams.min_contrast &&
               currentParams.uncertainty_ratio ==
                   finetuneParams.uncertainty_ratio &&
               currentParams.max_displacement ==
                   finetuneParams.max_displacement;
      };
  operation.applyResult = [this, result]() {
    if (!result->success || result->points.empty())
      return;

    ParameterState state = getCurrentState();
    for (const auto &point : result->points)
      state.solver.add_or_modify_point(point.img, point.scr, point.color);
    changeParameters(state, tr("Add registration points"));

    if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
      const std::size_t count = m_imageWidget->registrationPointCount();
      if (count >= static_cast<std::size_t>(
                       colorscreen::solver_parameters::min_points(
                           m_scrToImgParams.type)))
        onOptimizeGeometry(true);
    }
    updateRegistrationActions();
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, selectedArea, finetuneParams,
       result](colorscreen::progress_info *progress) mutable {
        *result = FinetuneWorker::findPoints(
            baseline.solver, baseline.rparams, baseline.scrToImg, scan,
            selectedArea, finetuneParams, progress);
      });
}

/** Swap the color assignments of registration points.
   Delegates to scr_to_img_parameters::alternate_colors() which cycles
   through colour channel interpretations.  Pushes an undo command.  */
void MainWindow::onAlternateColorsRequested() {
  ParameterState state = getCurrentState();
  state.scrToImg.alternate_colors(state.solver);
  changeParameters(state, tr("Alternate colors"));
}

/** Open white/optional black references and launch one flat-field analysis.
   The computation uses the shared final-result OneShotOperation lifecycle, so
   document edits, replacement requests, cancellation and close all veto stale
   publication without a dedicated QThread or generation counter. */
void MainWindow::onFlatFieldRequested() {
  const QString filters =
      "Images (*.tif *.tiff *.jpg *.jpeg *.raw *.dng *.iiq *.nef *.NEF *.cr2 "
      "*.CR2 *.eip *.arw *.ARW *.raf *.RAF *.arq *.ARQ *.csprj);;All Files "
      "(*)";
  const QString whiteFile = QFileDialog::getOpenFileName(
      this, "Choose White Reference", m_currentImageFile, filters);
  if (whiteFile.isEmpty())
    return;

  QTimer::singleShot(0, this, [this, filters, whiteFile]() {
    QString blackFile;
    if (QMessageBox::question(
            this, "Flat Field",
            "Do you want to provide a black reference image (optional)?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      blackFile = QFileDialog::getOpenFileName(this, "Choose Black Reference",
                                               m_currentImageFile, filters);
    }

    const colorscreen::luminosity_t gamma = m_rparams.gamma;
    const colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;
    auto result = std::make_shared<FlatFieldAnalysisResult>();

    OneShotOperation operation;
    operation.description = tr("Flat field analysis");
    operation.resultValid = [this, gamma, demosaic]() {
      return m_rparams.gamma == gamma && m_rparams.demosaic == demosaic;
    };
    operation.applyResult = [this, result]() {
      if (!result->success || !result->correction) {
        if (!result->cancelled) {
          QMessageBox::warning(
              this, tr("Flat Field"),
              result->error.isEmpty()
                  ? tr("Flat field analysis failed.")
                  : tr("Flat field analysis failed: %1").arg(result->error));
        }
        return;
      }

      ParameterState newState = getCurrentState();
      newState.rparams.backlight_correction = result->correction;
      changeParameters(newState, tr("Flat field"));
      statusBar()->showMessage(
          tr("Flat-field correction applied."), 4000);
    };

    runOneShotOperation(
        std::move(operation),
        [whiteFile, blackFile, gamma, demosaic, result](
            colorscreen::progress_info *progress) {
          *result = FlatFieldWorker::analyze(whiteFile, blackFile, gamma,
                                             demosaic, progress);
        });
  });
}

/** Toggle focus analysis mode.
   When CHECKED is true, saves the current tool, switches to AddPoint
   mode, and sets a flag so the next point-add triggers a focus analysis
   worker instead of adding a registration point.  FLAGS controls which
   finetune features to run (e.g. strip widths).  */
void MainWindow::onFocusAnalysisRequested(bool checked, uint64_t flags) {
  if (!m_imageWidget)
    return;
  m_focusAnalysisPending = checked;
  m_focusAnalysisFlags = flags;
  if (checked) {
    saveInteractionMode();
    m_imageWidget->setInteractionMode(ImageWidget::AddPointMode);
    statusBar()->showMessage(tr("Select point for focus analysis"), 5000);
  } else {
    restoreInteractionMode();
    statusBar()->clearMessage();
  }
}

/** Show/locate one selected stored MTF measurement on ordinary views when its
    source image matches this document. */
void MainWindow::updateMtfMeasurementOverlay(bool locate) {
  const auto &measurements = m_rparams.sharpen.scanner_mtf.measurements;
  const colorscreen::mtf_measurement *measurement = nullptr;
  if (m_selectedMtfMeasurement >= 0 &&
      m_selectedMtfMeasurement < static_cast<int>(measurements.size()))
    measurement = &measurements[m_selectedMtfMeasurement];

  auto matchesSource = [this](const colorscreen::mtf_measurement *m) {
    if (!m || !m->has_spatial_metadata() || !m_scan ||
        m->source_filename.empty())
      return false;
    if (m->source_width > 0 && m->source_height > 0 &&
        (m->source_width != m_scan->width || m->source_height != m_scan->height))
      return false;
    const QFileInfo recorded(QString::fromUtf8(m->source_filename.c_str()));
    const QFileInfo current(m_currentImageFile);
    return recorded.absoluteFilePath() == current.absoluteFilePath() ||
           (recorded.fileName() == current.fileName() &&
            m->source_width == m_scan->width && m->source_height == m_scan->height);
  };

  const colorscreen::mtf_measurement *overlay =
      matchesSource(measurement) ? measurement : nullptr;
  if (m_imageWidget)
    m_imageWidget->setMtfMeasurementOverlay(overlay);
  if (ImageWidget *target = inspectorImageWidget();
      target && target != m_imageWidget)
    target->setMtfMeasurementOverlay(overlay);

  if (!locate)
    return;
  if (!overlay) {
    if (measurement && !measurement->source_filename.empty())
      statusBar()->showMessage(
          tr("MTF measurement belongs to %1; open that source/reference to locate it.")
              .arg(QFileInfo(QString::fromUtf8(
                       measurement->source_filename.c_str())).fileName()),
          5000);
    return;
  }
  if (ImageWidget *target = inspectorImageWidget()) {
    const colorscreen::point_t center = {
        overlay->roi.x + overlay->roi.width / 2.0,
        overlay->roi.y + overlay->roi.height / 2.0};
    target->centerOn(center);
    target->setFocus();
  }
}

/** Refresh automatic focus-area rectangles in all ordinary views currently
    owned/presented by this document. */
void MainWindow::updateFocusAreaOverlays() {
  std::vector<ImageWidget::FocusAreaOverlay> overlays;
  overlays.reserve(m_focusAreaAnalysis.candidates.size());
  std::map<size_t, colorscreen::coord_t> heldOut;
  for (size_t i = 0; i < m_focusAreaAnalysis.result.selected.size(); ++i) {
    if (i < m_focusAreaAnalysis.result.held_out_relative_badness.size())
      heldOut[m_focusAreaAnalysis.result.selected[i]] =
          m_focusAreaAnalysis.result.held_out_relative_badness[i];
  }
  std::set<size_t> selected(m_focusAreaAnalysis.result.selected.begin(),
                            m_focusAreaAnalysis.result.selected.end());
  for (size_t i = 0; i < m_focusAreaAnalysis.candidates.size(); ++i) {
    ImageWidget::FocusAreaOverlay overlay;
    overlay.area = m_focusAreaAnalysis.candidates[i].area;
    overlay.fitSuccessful = m_focusAreaAnalysis.candidates[i].fit.success;
    overlay.selected = selected.count(i) != 0;
    auto score = heldOut.find(i);
    if (score != heldOut.end())
      overlay.heldOutRelativeBadness = score->second;
    overlays.push_back(overlay);
  }
  if (m_imageWidget)
    m_imageWidget->setFocusAreaOverlays(overlays);
  if (ImageWidget *target = inspectorImageWidget(); target && target != m_imageWidget)
    target->setFocusAreaOverlays(overlays);
}

/** Clear transient automatic focus-area state without changing parameters. */
void MainWindow::clearFocusAreaAnalysis() {
  m_focusAreaAnalysis.candidates.clear();
  m_focusAreaAnalysis.result = colorscreen::finetune_focus_analysis_result();
  updateFocusAreaOverlays();
  if (m_sharpnessPanel)
    m_sharpnessPanel->setFocusAreaAnalysisState(0, m_focusAreaAnalysis.running);
}

/** Finish a focus-area request without overwriting its accepted result summary. */
void MainWindow::finishFocusAreaOperation(const QString &summary) {
  m_focusAreaAnalysis.running = false;
  if (m_sharpnessPanel)
    m_sharpnessPanel->setFocusAreaAnalysisState(
        static_cast<int>(m_focusAreaAnalysis.candidates.size()), false, summary);
}

/** Find uniform areas under the shared final-result snapshot/publication rules. */
void MainWindow::onFindFocusAreasRequested() {
  if (!m_scan || m_focusAreaAnalysis.running)
    return;

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<FocusAreaFindResult>();
  auto summary = std::make_shared<QString>(
      tr("Focus-area search cancelled or superseded."));

  OneShotOperation operation;
  operation.description = tr("Finding focus analysis areas");
  operation.progressTitle = tr("Find focus areas");
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.onStart = [this](std::shared_ptr<colorscreen::progress_info>) {
    m_focusAreaAnalysis.running = true;
    clearFocusAreaAnalysis();
    if (m_sharpnessPanel)
      m_sharpnessPanel->setFocusAreaAnalysisState(
          0, true, tr("Searching for uniform colour areas…"));
  };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline && !result->cancelled;
  };
  operation.applyResult = [this, result, summary]() {
    if (!result->success) {
      *summary = tr("Focus-area search failed: %1")
                     .arg(QString::fromStdString(result->error));
      return;
    }
    m_focusAreaAnalysis.candidates = std::move(result->candidates);
    m_focusAreaAnalysis.result = colorscreen::finetune_focus_analysis_result();
    updateFocusAreaOverlays();
    const int count = static_cast<int>(m_focusAreaAnalysis.candidates.size());
    *summary = tr("Found %1 candidate uniform area(s).").arg(count);
    statusBar()->showMessage(
        tr("Found %1 focus analysis area(s)").arg(count), 4000);
  };
  operation.onDone = [this, summary]() { finishFocusAreaOperation(*summary); };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, result](colorscreen::progress_info *progress) {
        *result = FocusAnalysisWorker::findAreas(
            baseline.rparams, baseline.scrToImg, scan, progress);
      });
}

/** Jointly fit discovered areas; keep diagnostics and approval snapshot-bound. */
void MainWindow::onAnalyzeFocusAreasRequested(uint64_t flags) {
  if (!m_scan || m_focusAreaAnalysis.running)
    return;
  if (m_focusAreaAnalysis.candidates.size() < 3) {
    statusBar()->showMessage(
        tr("Find at least three focus areas before analyzing them."), 4000);
    return;
  }
  const uint64_t focusMask = colorscreen::finetune_screen_blur
      | colorscreen::finetune_scanner_mtf_sigma
      | colorscreen::finetune_scanner_mtf_defocus;
  flags &= focusMask;
  if (!flags) {
    statusBar()->showMessage(
        tr("Enable at least one blur/focus parameter before analysis."), 4000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  const auto candidates = m_focusAreaAnalysis.candidates;
  const bool useMonochrome = focusAnalysisUsesMonochromeInput(*scan);
  auto result = std::make_shared<FocusAreaAnalyzeResult>();
  auto summary = std::make_shared<QString>(
      tr("Focus-area analysis cancelled or superseded."));

  OneShotOperation operation;
  operation.description = tr("Analyzing focus areas");
  operation.progressTitle = tr("Analyze focus areas");
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.onStart = [this](std::shared_ptr<colorscreen::progress_info>) {
    m_focusAreaAnalysis.running = true;
    if (m_sharpnessPanel)
      m_sharpnessPanel->setFocusAreaAnalysisState(
          static_cast<int>(m_focusAreaAnalysis.candidates.size()), true,
          tr("Verifying and jointly fitting focus areas…"));
  };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline && !result->cancelled;
  };
  operation.applyResult = [this, scan, baseline, flags, result, summary]() {
    // Partial diagnostics from cancelled/stale runs never reach this callback.
    m_focusAreaAnalysis.candidates = std::move(result->candidates);
    m_focusAreaAnalysis.result = std::move(result->analysis);
    updateFocusAreaOverlays();
    if (!result->success) {
      const QString error = QString::fromStdString(result->error);
      *summary = tr("Focus-area analysis failed: %1").arg(error);
      auto *box = new QMessageBox(QMessageBox::Warning,
                                  tr("Focus analysis areas"), error,
                                  QMessageBox::Ok, this);
      box->setAttribute(Qt::WA_DeleteOnClose);
      box->open();
      return;
    }
    *summary = presentFocusAreaAnalysisResult(
        m_focusAreaAnalysis.result, scan, baseline, flags);
  };
  operation.onDone = [this, summary]() { finishFocusAreaOperation(*summary); };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, candidates, flags, useMonochrome, result](
          colorscreen::progress_info *progress) {
        *result = FocusAnalysisWorker::analyzeAreas(
            baseline.rparams, baseline.scrToImg, scan, candidates, flags,
            useMonochrome, progress);
      });
}

/** Show accepted diagnostics and return their summary. The separate Apply step
    must still own the same scan/state when the asynchronous prompt finishes. */
QString MainWindow::presentFocusAreaAnalysisResult(
    const colorscreen::finetune_focus_analysis_result &analysis,
    std::shared_ptr<colorscreen::image_data> scan,
    const ParameterState &baseline, uint64_t flags) {
  if (m_closing || m_scan != scan || getCurrentState() != baseline ||
      !analysis.success)
    return QString();
  dismissOneShotPrompts();

  QStringList details;
  details << tr("Selected %1 of %2 verified candidates.")
                 .arg(static_cast<int>(analysis.selected.size()))
                 .arg(static_cast<int>(m_focusAreaAnalysis.candidates.size()));
  if (analysis.leave_one_out_focus_span >= 0)
    details << tr("Leave-one-out focus span: %1")
                   .arg(analysis.leave_one_out_focus_span, 0, 'g', 5);
  if (analysis.leave_one_out_focus_max_delta >= 0)
    details << tr("Maximum leave-one-out displacement: %1")
                   .arg(analysis.leave_one_out_focus_max_delta, 0,
                        'g', 5);
  if (analysis.held_out_max_relative_badness >= 0)
    details << tr("Maximum held-out relative residual: %1")
                   .arg(analysis.held_out_max_relative_badness, 0,
                        'g', 5);
  if (analysis.screen_frequency > 0 && analysis.joint_screen_mtf >= 0)
    details << tr("Process-screen MTF at %1 cycles/pixel: %2%")
                   .arg(analysis.screen_frequency, 0, 'g', 6)
                   .arg(analysis.joint_screen_mtf * 100, 0, 'g', 5);
  if ((flags & colorscreen::finetune_scanner_mtf_sigma)
      && analysis.joint_fit.scanner_mtf_sigma >= 0)
    details << tr("Residual MTF sigma: %1 px")
                   .arg(analysis.joint_fit.scanner_mtf_sigma, 0, 'g',
                        5);
  if (flags & colorscreen::finetune_scanner_mtf_defocus) {
    if (baseline.rparams.sharpen.scanner_mtf.simulate_diffraction_p())
      details << tr("Physical defocus: %1 mm")
                     .arg(analysis.joint_fit.scanner_mtf_defocus, 0,
                          'g', 5);
    else
      details << tr("Compact blur diameter: %1 px")
                     .arg(analysis.joint_fit.scanner_mtf_blur_diameter,
                          0, 'g', 5);
  }
  const QString summary = details.join(QStringLiteral("\n"));
  auto *box = new QMessageBox(this);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Focus analysis areas"));
  box->setIcon(QMessageBox::Information);
  box->setText(summary);
  box->setInformativeText(
      tr("The value is not applied automatically. Inspect the selected "
         "rectangles and validation diagnostics before accepting it."));
  QPushButton *applyButton =
      box->addButton(tr("Apply focus"), QMessageBox::AcceptRole);
  box->addButton(QMessageBox::Close);
  m_focusAreaAnalysis.prompt = box;
  connect(box, &QMessageBox::finished, this,
          [this, box, analysis, flags, applyButton, scan, baseline](int) {
            // Clear ownership before changeParameters() can dismiss prompts.
            // An obsolete box cannot become valid again after Edit -> Undo.
            if (m_focusAreaAnalysis.prompt != box)
              return;
            m_focusAreaAnalysis.prompt = nullptr;
            if (m_closing || m_scan != scan || getCurrentState() != baseline ||
                box->clickedButton() != applyButton)
              return;

            ParameterState state = getCurrentState();
            if ((flags & colorscreen::finetune_screen_blur) &&
                analysis.joint_fit.screen_blur_radius >= 0)
              state.rparams.screen_blur_radius =
                  analysis.joint_fit.screen_blur_radius;
            if ((flags & colorscreen::finetune_scanner_mtf_sigma) &&
                analysis.joint_fit.scanner_mtf_sigma >= 0)
              state.rparams.sharpen.scanner_mtf.sigma =
                  analysis.joint_fit.scanner_mtf_sigma;
            if (flags & colorscreen::finetune_scanner_mtf_defocus) {
              if (state.rparams.sharpen.scanner_mtf
                      .simulate_diffraction_p())
                state.rparams.sharpen.scanner_mtf.defocus =
                    analysis.joint_fit.scanner_mtf_defocus;
              else
                state.rparams.sharpen.scanner_mtf.blur_diameter =
                    analysis.joint_fit.scanner_mtf_blur_diameter;
            }
            changeParameters(
                state, tr("Apply multi-area focus analysis"));
          });
  box->open();
  return summary;
}

/** Render the current image to a TIFF or DNG file.
   MainWindow chooses the path, presents RenderDialog, and snapshots accepted
   document/settings state. FileRenderController owns background execution,
   progress identity, cancellation, incomplete-file cleanup, and completion. */
void MainWindow::onRender() {
  if (!m_scan) {
    QMessageBox::warning(this, tr("Render"),
                         tr("No image loaded. Please open an image first."));
    return;
  }

  // Default output filename: same directory as scan, with .tif extension
  QString defaultPath;
  if (!m_currentImageFile.isEmpty()) {
    QFileInfo fi(m_currentImageFile);
    defaultPath = fi.dir().filePath(fi.completeBaseName() + "-rendered.tif");
  }

  QString outputPath = QFileDialog::getSaveFileName(
      this, tr("Render to File"), defaultPath,
      tr("TIFF images (*.tif *.tiff);;DNG images (*.dng);;All files (*.*)"));
  if (outputPath.isEmpty())
    return;

  QTimer::singleShot(0, this, [this, outputPath]() {
    bool isDng = outputPath.endsWith(".dng", Qt::CaseInsensitive);

    // Keep the dialog parent-owned and asynchronous: a nested event loop can
    // otherwise let this document be destroyed while a stack child still exists.
    auto *dialog = new RenderDialog(m_renderTypeParams, m_rparams,
                                    m_scrToImgParams, m_scan.get(), isDng,
                                    this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this,
            [this, dialog, outputPath, isDng]() {
      // Snapshot current parameters and accepted dialog settings before the
      // delete-on-close dialog disappears.
      auto scan = m_scan;
      if (!scan)
        return;

      FileRenderController::Request request;
      request.scan = std::move(scan);
      request.scrParams = m_scrToImgParams;
      request.detectParams = m_detectParams;
      request.renderParams = m_rparams;
      request.renderType = dialog->renderTypeParams();
      request.renderParams.output_profile = dialog->outputProfile();
      request.outputPath = outputPath;
      request.progressTitle =
          tr("Rendering %1").arg(QFileInfo(outputPath).fileName());
      request.dng = isDng;
      request.hdr = dialog->hdr();
      request.depth = dialog->depth();
      request.geometry = dialog->geometry();
      request.antialias = dialog->antialias();
      request.scale = dialog->scale();
      request.screenScale = dialog->screenScale();
      request.width = dialog->outputWidth();
      request.height = dialog->outputHeight();

      m_fileRenderController.start(std::move(request));
    });
    dialog->open();
  });
}

/** Enter or exit profile spot adding mode.
   When ACTIVE is true, saves the current tool and switches to AddPoint
   mode.  The m_addingProfileSpot flag causes onPointAdded to add
   profile calibration spots instead of registration points.  */
void MainWindow::onAddSpotModeRequested(bool active) {
  m_addingProfileSpot = active;
  if (active) {
    saveInteractionMode();
    inspectorImageWidget()->setInteractionMode(ImageWidget::AddPointMode);
  } else {
    inspectorImageWidget()->setInteractionMode(m_previousInteractionMode);
  }
}

/** Handle profile color optimisation request from ProfilePanel.
   Packs the current scr_to_img, render params, and profile spots
   into a request and submits it to m_colorOptimizerQueue.  If an
   optimisation is already running, the queue cancels it first.  */
void MainWindow::onColorOptimizeRequested(bool /*autoMode*/) {
  if (!m_scan || !m_colorOptimizerWorker)
    return;
  ParameterState state = getCurrentState();
  if (!colorscreen::screen_geometry_configured_p(state.scrToImg)) {
    statusBar()->showMessage(tr("Fit screen geometry before optimizing the color profile."), 3000);
    return;
  }
  if (state.profileSpots.size() < 4)
    return;

  // The snapshot is both user-visible provenance and a publication gate. A
  // newer request still supersedes an older TaskQueue job, while unrelated
  // edits invalidate even the newest request before it can publish.
  ColorOptimizerRequestData d{m_scrToImgParams, m_rparams, state.profileSpots};
  const int requestId =
      m_colorOptimizerQueue.requestRender(QVariant::fromValue(d));
  if (requestId <= 0)
    return;

  m_profileCalibration.pendingInputs = d;
  m_profileCalibration.pendingRequestId = requestId;
  m_profileCalibration.failureInputs.reset();
  updateWorkflowSummary();
}

/** TaskQueue callback that dispatches the color optimisation request to
   the ColorOptimizerWorker running in m_colorOptimizerThread.
   Invoked on the main thread when the queue is ready.  */
void MainWindow::onTriggerColorOptimize(
    int reqId, std::shared_ptr<colorscreen::progress_info> progress,
    const QVariant &userData) {
  if (!m_scan || !m_colorOptimizerWorker ||
      !userData.canConvert<ColorOptimizerRequestData>()) {
    m_colorOptimizerQueue.reportFinished(reqId, false);
    if (m_profileCalibration.pendingRequestId &&
        *m_profileCalibration.pendingRequestId == reqId) {
      m_profileCalibration.pendingInputs.reset();
      m_profileCalibration.pendingRequestId.reset();
      updateWorkflowSummary();
    }
    return;
  }

  auto d = userData.value<ColorOptimizerRequestData>();
  if (progress)
    progress->set_task("Optimizing color profile", 1);

  QMetaObject::invokeMethod(
      m_colorOptimizerWorker, "optimize", Qt::QueuedConnection,
      Q_ARG(int, reqId), Q_ARG(colorscreen::scr_to_img_parameters, d.scrParams),
      Q_ARG(colorscreen::render_parameters, d.rparams),
      Q_ARG(std::vector<colorscreen::point_t>, d.spots),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress));
}

/** Handle completion of color profile optimisation.
   On success, applies the profiled dark/red/green/blue corrections,
   pushes an undo command, and updates the profile panel and image
   widget with spot match results for visual feedback.  */
void MainWindow::onColorOptimizerFinished(
    int reqId, colorscreen::render_parameters updatedRparams,
    std::vector<colorscreen::color_match> results, bool success,
    bool cancelled) {
  const bool publishable =
      m_colorOptimizerQueue.reportFinished(reqId, success);
  if (m_closing)
    return;

  // Request identity owns provenance cleanup. TaskQueue's boolean controls
  // whether this result may publish into document state.
  if (!m_profileCalibration.pendingRequestId ||
      *m_profileCalibration.pendingRequestId != reqId)
    return;

  const ColorOptimizerRequestData now{
      m_scrToImgParams, m_rparams, m_profileSpots};
  const bool inputsStillCurrent = m_profileCalibration.pendingInputs &&
      !profileCalibrationInputsDiffer(*m_profileCalibration.pendingInputs, now);
  const std::optional<ColorOptimizerRequestData> completedInputs =
      m_profileCalibration.pendingInputs;
  m_profileCalibration.pendingInputs.reset();
  m_profileCalibration.pendingRequestId.reset();

  if (!publishable || cancelled || !inputsStillCurrent) {
    updateWorkflowSummary();
    return;
  }

  if (success) {
    m_profileCalibration.baseline = completedInputs;
    m_profileCalibration.failureInputs.reset();
    m_profileCalibration.averageDeltaE = -1;
    if (!results.empty()) {
      double total = 0;
      for (const auto &match : results)
        total += match.deltaE;
      m_profileCalibration.averageDeltaE = total / results.size();
    }

    ParameterState newState = getCurrentState();
    newState.rparams.profiled_dark = updatedRparams.profiled_dark;
    newState.rparams.profiled_red = updatedRparams.profiled_red;
    newState.rparams.profiled_green = updatedRparams.profiled_green;
    newState.rparams.profiled_blue = updatedRparams.profiled_blue;
    changeParameters(newState, tr("Optimize profile"));

    m_profileSpotResults = results;
    if (m_profilePanel)
      m_profilePanel->setSpotResults(results);
    if (m_imageWidget) {
      m_imageWidget->setProfileSpots(&m_profileSpots, &m_profileSpotResults);
      m_imageWidget->update();
    }
  } else {
    m_profileCalibration.failureInputs = completedInputs;
    statusBar()->showMessage(tr("Color optimization failed"), 4000);
  }
  updateWorkflowSummary();
}

/** Enter measurement mode for DPI calculation.
   Saves the current tool and switches to MeasureMode.  The user
   click-drags to define a distance; ImageWidget emits distanceMeasured
   which is handled by onDistanceMeasured.  */
void MainWindow::onMeasureRequested() {
  saveInteractionMode();
  inspectorImageWidget()->setInteractionMode(ImageWidget::MeasureMode);
  statusBar()->showMessage(
      tr("Click the first point, zoom as needed, then click the second point; dragging also works"),
      7000);
}

/** Handle a completed distance measurement.
   Restores the previous tool, calculates the pixel distance between
   the two measured points, and opens MeasureDialog where the user
   enters the physical distance and unit to compute the scan DPI.
   If accepted, updates scan_dpi in the parameter state.  */
void MainWindow::onDistanceMeasured(colorscreen::point_t p1, colorscreen::point_t p2) {
  if (ImageWidget *image = qobject_cast<ImageWidget *>(sender()))
    if (image != inspectorImageWidget())
      return;
  restoreInteractionMode();
  statusBar()->clearMessage();

  double dx = p1.x - p2.x;
  double dy = p1.y - p2.y;
  double distPixels = sqrt(dx * dx + dy * dy);

  if (distPixels < 1.0)
    return;

  auto *dialog = new MeasureDialog(
      distPixels, m_rparams.sharpen.scanner_mtf.scan_dpi, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    const double newDpi = dialog->getResultDpi();
    ParameterState state = getCurrentState();
    state.rparams.sharpen.scanner_mtf.scan_dpi = newDpi;
    changeParameters(state, tr("Set DPI by measurement"));
  });
  dialog->open();
}

/** Configure a slanted-edge measurement and, when CHECKED, ask the user for
    its image area.  Analysis settings are chosen before the edge is measured
    because oversampling, LSF support and windowing change the stored curve and
    cannot be altered later by the model-fitting dialog.  */
void MainWindow::onMeasureMtfRequested(bool checked) {
  if (checked) {
    ParameterState currentState = getCurrentState();
    const colorscreen::mtf_parameters currentMtf =
        currentState.rparams.sharpen.scanner_mtf;
    colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
    const bool hasRgb = m_scan && m_scan->has_rgb();
    const bool hasInfrared = m_scan && m_scan->has_grayscale_or_ir();
    if (!hasRgb && m_scan) {
      defaults.wavelength
          = currentState.rparams.get_image_layer_wavelength(m_scan.get());
    } else if (defaults.wavelength <= 0) {
      if (!currentMtf.measurements.empty()
          && currentMtf.measurements.back().wavelength > 0)
        defaults.wavelength = currentMtf.measurements.back().wavelength;
      else if (currentMtf.wavelength > 0)
        defaults.wavelength = currentMtf.wavelength;
    }

    auto *dialog = new SlantedEdgeDialog(
        defaults, !currentMtf.measurements.empty(), hasRgb, hasInfrared, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::rejected, this, [this]() {
      if (m_sharpnessPanel)
        m_sharpnessPanel->setMeasureMtfChecked(false);
    });
    connect(dialog, &QDialog::accepted, this,
            [this, dialog, currentMtf, hasInfrared]() {
      const colorscreen::slanted_edge_parameters baseParameters =
          dialog->parameters();
      m_slantedEdgeParameters = baseParameters;

      std::vector<colorscreen::slanted_edge_parameters> measurementParameters;
      if (dialog->measureNativeChannels()) {
        static const char *const channelNames[4] =
            {"Red", "Green", "Blue", "Infrared"};
        const int channelCount = hasInfrared ? 4 : 3;
        measurementParameters.reserve(channelCount);
        for (int channel = 0; channel < channelCount; ++channel) {
          colorscreen::slanted_edge_parameters p = baseParameters;
          p.channel = channel;
          p.name = baseParameters.name + " " + channelNames[channel];
          p.same_capture = channel == 0 ? baseParameters.same_capture : true;
          p.source_filename = m_currentImageFile.toUtf8().toStdString();

          double wavelength = currentMtf.wavelengths[channel];
          if (!(colorscreen::my_isfinite(wavelength) && wavelength > 0)
              && m_scan) {
            wavelength = m_scan->wavelengths[channel];
          }
          p.wavelength =
              colorscreen::my_isfinite(wavelength) && wavelength > 0
                  ? wavelength
                  : 0;
          measurementParameters.push_back(std::move(p));
        }
      } else {
        colorscreen::slanted_edge_parameters p = baseParameters;
        p.channel = -1;
        p.source_filename = m_currentImageFile.toUtf8().toStdString();
        measurementParameters.push_back(std::move(p));
      }

      auto results =
          std::make_shared<std::vector<colorscreen::slanted_edge_results>>();
      auto batchError = std::make_shared<std::string>();
      runAreaComputation(
          tr("Select an area containing a slanted edge to compute its MTF"),
          measurementParameters.size() > 1
              ? tr("Measure per-channel MTF of a slanted edge")
              : tr("Measure MTF of a slanted edge"),
          [this]() { m_sharpnessPanel->setMeasureMtfEnabled(false); },
          [this, batchError]() {
            if (!batchError->empty()) {
              QString reason = QString::fromStdString(*batchError);
              QMessageBox::warning(
                  this, tr("MTF Measurement Failed"),
                  tr("%1\n\nSelect one straight, isolated edge with clear "
                     "plateaus on both sides. Avoid dust, texture, multiple "
                     "edges, and edges parallel to the pixel grid.")
                      .arg(reason));
            }
            m_sharpnessPanel->setMeasureMtfChecked(false);
            m_sharpnessPanel->setMeasureMtfEnabled(true);
          },
          [results, batchError, measurementParameters](
              ParameterState &s, colorscreen::image_data &scan,
              const colorscreen::int_image_area &area,
              colorscreen::progress_info *progress) {
            results->clear();
            batchError->clear();
            results->reserve(measurementParameters.size());

            for (const auto &parameters : measurementParameters) {
              colorscreen::slanted_edge_results result =
                  colorscreen::slanted_edge_mtf(
                      s.rparams, scan, area, parameters, progress);
              if (!result.success) {
                *batchError =
                    parameters.name + ": "
                    + (result.error.empty()
                           ? std::string("no usable single slanted edge was found")
                           : result.error);
                results->push_back(std::move(result));
                return;
              }
              results->push_back(std::move(result));
            }

            /* Commit the set only after every requested native channel passed
               qualification.  This prevents partial RGB measurement groups.  */
            for (auto &result : *results)
              s.rparams.sharpen.scanner_mtf.measurements.push_back(
                  std::move(result.measurement));
          });
    });
    dialog->open();
  } else {
    if (inspectorImageWidget()->interactionMode() == ImageWidget::GenericAreaMode) {
      restoreInteractionMode();
      statusBar()->clearMessage();
      m_areaSelectionCallback = nullptr;
    }
  }
}
