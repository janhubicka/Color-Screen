#include "MainWindow.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/histogram.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
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
#include "NavigationView.h"
#include "RenderDialog.h"
#include "ScreenPanel.h"
#include "mesh.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QColorSpace>
#include <QCoreApplication>
#include <QComboBox>
#include <QDateTime> // Added QDateTime include
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
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSizeGrip>
#include <QSplitter>
#include <QStringList>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

// Undo/Redo Implementation

/** Undo command that captures a full ParameterState snapshot before and after
   a change.  Successive commands with the same description within a 500 ms
   window are merged into a single undo step so that one slider drag produces
   one entry.  Different controls must remain separate even when changed
   quickly, otherwise undo can silently skip an intermediate user action. */
class ChangeParametersCommand : public QUndoCommand {
public:
  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,
                          const ParameterState &newState,
                          const QString &description = QString())
      : m_window(window), m_oldState(oldState), m_newState(newState),
        m_description(description) {
    setText(description.isEmpty() ? "Change Parameters" : description);
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
  }

  int id() const override {
    // Qt never attempts to merge commands whose id is -1.  Calls without a
    // logical description are therefore conservative one-shot undo entries.
    return m_description.isEmpty() ? -1 : 1;
  }

  bool mergeWith(const QUndoCommand *other) override {
    if (other->id() != id())
      return false;

    const ChangeParametersCommand *cmd =
        static_cast<const ChangeParametersCommand *>(other);

    // QUndoStack uses id() only as a coarse filter.  The description is the
    // stable logical identity supplied by ParameterPanel (normally the field
    // label), so do not merge two different edits merely because they happened
    // close together.
    if (cmd->m_description != m_description)
      return false;

    // Only merge adjacent updates from the same edit gesture.  Also reject a
    // negative delta in case the wall clock is adjusted between commands.
    qint64 timeDiff = cmd->m_timestamp - m_timestamp;
    if (timeDiff < 0 || timeDiff > 500) {
      return false; // Don't merge - create separate undo step
    }

    // Merge: update our newState to the newer command's newState
    // This allows slider dragging to be one undo operation
    m_newState = cmd->m_newState;
    m_timestamp = cmd->m_timestamp; // Update timestamp for next merge check
    return true;
  }

  void undo() override { m_window->applyState(m_oldState); }

  void redo() override { m_window->applyState(m_newState); }

private:
  MainWindow *m_window;
  ParameterState m_oldState;
  ParameterState m_newState;
  QString m_description;
  qint64 m_timestamp; // Timestamp for merge window
};

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

/** Result returned by the asynchronous cheap solid-area discovery pass. */
struct FocusAreaFindTaskResult {
  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::finetune_focus_area_candidate> candidates;
  std::string error;
};

/** Result returned by the asynchronous individual/joint focus-area pass. */
struct FocusAreaAnalyzeTaskResult {
  bool success = false;
  bool cancelled = false;
  std::vector<colorscreen::finetune_focus_area_candidate> candidates;
  colorscreen::finetune_focus_analysis_result analysis;
  std::string error;
};

/** Return the application-level document manager when MainWindow is running
    inside the normal Color-Screen Qt application.  */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QCoreApplication::instance());
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
/** Small extensible guide shown after an image is opened without parameter
    data.  Later setup recommendations can be added as more rows without
    changing the load/reload orchestration. */
class InitialSetupGuideDialog final : public QDialog {
public:
  explicit InitialSetupGuideDialog(QWidget *parent, bool suggestCaptureType,
                                   bool looksMonochrome, bool suggestBayer,
                                   bool suggestFStop,
                                   bool suggestPitch, bool suggestFill,
                                   bool suggestDPI, bool suggestWavelengths,
                                   const colorscreen::image_data *scan)
      : QDialog(parent) {
    setWindowTitle(tr("Suggested image setup"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    if (suggestCaptureType) {
      auto *intro = new QLabel(
          tr("Choose what kind of material this image captures. The capture "
             "type determines which restoration workflow is applicable."),
          this);
      intro->setWordWrap(true);
      layout->addWidget(intro);

      m_captureType = new QComboBox(this);
      m_captureType->setObjectName(QStringLiteral("InitialCaptureTypeCombo"));
      for (int i = 0; i < (int)colorscreen::render_parameters::capture_max;
           ++i) {
        const auto capture = static_cast<decltype(
            colorscreen::render_parameters::capture_unknown)>(i);
        bool show = capture == colorscreen::render_parameters::capture_unknown;
        if (looksMonochrome) {
          // RGB is only a Bayer-container detail in this case. Until we have
          // additive-process recognition, do not offer color-screen workflows.
          show = show ||
                 capture == colorscreen::render_parameters::capture_transparency ||
                 capture == colorscreen::render_parameters::capture_negative ||
                 capture == colorscreen::render_parameters::capture_plain_image;
        } else if (scan) {
          show = show ||
                 colorscreen::render_parameters::capture_type_compatible_p(
                     capture, scan);
        }
        if (show)
          m_captureType->addItem(
              QString::fromUtf8(colorscreen::render_parameters::
                                    capture_properties[i].pretty_name),
              i);
      }
      m_captureType->setCurrentIndex(
          m_captureType->findData(
              (int)colorscreen::render_parameters::capture_unknown));
      m_captureType->setToolTip(
          tr("Choose Unknown if you are not sure yet. Color-Screen will keep "
             "the restoration workflow conservative until this is known."));
      layout->addWidget(m_captureType);

      if (suggestBayer || suggestFStop || suggestPitch || suggestFill
          || suggestDPI || suggestWavelengths) {
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        layout->addWidget(line);
      }
    }
    
    if (suggestBayer) {
      auto *intro = new QLabel(
          tr("This appears to be a monochromatic capture made with a Bayer-filter camera."),
          this);
      intro->setWordWrap(true);
      layout->addWidget(intro);

      m_monochromeBayer =
          new QCheckBox(tr("Reload with Bayer-filter compensation"), this);
      m_monochromeBayer->setChecked(true);
      layout->addWidget(m_monochromeBayer);
      
      if (suggestFStop || suggestPitch || suggestFill || suggestDPI
          || suggestWavelengths) {
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        layout->addWidget(line);
      }
    }
    
    if (suggestFStop || suggestPitch || suggestFill || suggestDPI
          || suggestWavelengths) {
      auto *intro2 = new QLabel(
          tr("The following capture parameters were automatically detected:"), this);
      intro2->setWordWrap(true);
      layout->addWidget(intro2);
    }
    
    if (suggestFStop) {
      m_fstop = new QCheckBox(tr("Set nominal f-stop to f/%1").arg(scan->f_stop, 0, 'f', 1), this);
      m_fstop->setChecked(true);
      layout->addWidget(m_fstop);
    }
    
    if (suggestPitch) {
      int divisor = scan->width > 0 ? scan->width : 1;
      double sensorWidth = divisor * scan->pixel_pitch / 1000.0;
      QString sensorName = getSensorName(sensorWidth);
      m_pitch = new QCheckBox(tr("Set sensor pixel pitch to %1 μm (Sensor size: %2)").arg(scan->pixel_pitch, 0, 'f', 2).arg(sensorName), this);
      m_pitch->setChecked(true);
      layout->addWidget(m_pitch);
    }
    
    if (suggestFill) {
      m_fill = new QCheckBox(tr("Set sensor fill factor to %1").arg(scan->sensor_fill_factor, 0, 'f', 3), this);
      m_fill->setChecked(true);
      layout->addWidget(m_fill);
    }
    
    if (suggestDPI) {
      m_dpi = new QCheckBox(tr("Set image resolution to %1 PPI").arg(scan->xdpi, 0, 'f', 1), this);
      m_dpi->setChecked(true);
      layout->addWidget(m_dpi);
    }

    if (suggestWavelengths) {
      QStringList values;
      static const char *channelNames[] = {"R", "G", "B", "IR"};
      for (int c = 0; c < 4; ++c) {
        const bool present = c < 3 ? scan->has_rgb()
                                   : scan->has_grayscale_or_ir();
        const double wavelength = scan->wavelengths[c];
        if (!present || !colorscreen::my_isfinite(wavelength)
            || wavelength <= 0)
          continue;
        if (scan->has_rgb())
          values << QString("%1 %2 nm")
                        .arg(channelNames[c])
                        .arg(wavelength, 0, 'f', 0);
        else
          values << QString("%1 nm").arg(wavelength, 0, 'f', 0);
      }
      m_wavelengths = new QCheckBox(
          scan->has_rgb()
              ? tr("Set detected channel wavelengths: %1").arg(values.join(", "))
              : tr("Set capture wavelength to %1").arg(values.join(", ")),
          this);
      m_wavelengths->setChecked(true);
      layout->addWidget(m_wavelengths);
    }

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(suggestBayer ? tr("Apply and reload") : tr("Apply"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Not now"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
  }

  /** Return the capture type selected by the operator. */
  auto selectedCaptureType() const {
    if (!m_captureType)
      return colorscreen::render_parameters::capture_unknown;
    return static_cast<decltype(colorscreen::render_parameters::capture_unknown)>(
        m_captureType->currentData().toInt());
  }

  /** Return whether compensated monochrome Bayer loading was selected. */
  bool useMonochromeBayerCorrection() const {
    return m_monochromeBayer && m_monochromeBayer->isChecked();
  }
  
  bool useFStop() const {
    return m_fstop && m_fstop->isChecked();
  }
  
  bool usePixelPitch() const {
    return m_pitch && m_pitch->isChecked();
  }

  bool useFillFactor() const {
    return m_fill && m_fill->isChecked();
  }
  
  bool useDPI() const {
    return m_dpi && m_dpi->isChecked();
  }

  bool useWavelengths() const {
    return m_wavelengths && m_wavelengths->isChecked();
  }

private:
  QString getSensorName(double width_mm) const {
    struct Preset { const char* name; double w; };
    Preset presets[] = {
        {"PhaseOne 54.0mm", 54.0},
        {"PhaseOne 53.7mm", 53.7},
        {"PhaseOne 53.4mm", 53.4},
        {"Medium Format 43.8mm", 43.8},
        {"Full Frame (36mm)", 36.0},
        {"APS-H (28.3mm)", 28.3},
        {"APS-C (23.0mm)", 23.0},
        {"Micro Four Thirds (17.3mm)", 17.3},
        {"1-inch (13.2mm)", 13.2},
        {"1/1.7-inch (7.6mm)", 7.6},
        {"1/2.5-inch (5.76mm)", 5.76}
    };
    for (const auto& p : presets) {
        if (std::abs(width_mm - p.w) < 1.0)
            return QString::fromUtf8(p.name);
    }
    return tr("Unknown, %1 mm width").arg(width_mm, 0, 'f', 1);
  }

  QComboBox *m_captureType = nullptr;
  QCheckBox *m_monochromeBayer = nullptr;
  QCheckBox *m_fstop = nullptr;
  QCheckBox *m_pitch = nullptr;
  QCheckBox *m_fill = nullptr;
  QCheckBox *m_dpi = nullptr;
  QCheckBox *m_wavelengths = nullptr;
};

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

  // Progress Timer
  m_progressTimer = new QTimer(this);
  m_progressTimer->setInterval(100);
  connect(m_progressTimer, &QTimer::timeout, this,
          &MainWindow::onProgressTimer);

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

  // Initialize Color Optimizer Worker
  m_colorOptimizerThread = new QThread(this);
  m_colorOptimizerWorker = new ColorOptimizerWorker(m_scan);
  m_colorOptimizerWorker->moveToThread(m_colorOptimizerThread);
  m_colorOptimizerThread->start();

  connect(m_colorOptimizerWorker, &ColorOptimizerWorker::finished, this,
          &MainWindow::onColorOptimizerFinished);
  connect(&m_colorOptimizerQueue, &TaskQueue::triggerRender, this,
          &MainWindow::onTriggerColorOptimize);
  connect(&m_colorOptimizerQueue, &TaskQueue::progressStarted, this,
          &MainWindow::addProgress);
  connect(&m_colorOptimizerQueue, &TaskQueue::progressFinished, this,
          &MainWindow::removeProgress);

  // Initialize Coordinate Optimization Worker
  m_coordOptimizationThread = new QThread(this);
  m_coordOptimizationWorker = new CoordinateOptimizationWorker(m_scan);
  m_coordOptimizationWorker->moveToThread(m_coordOptimizationThread);
  m_coordOptimizationThread->start();

  connect(m_coordOptimizationWorker, &CoordinateOptimizationWorker::autodetectFinished, this,
          &MainWindow::onAutodetectCoordinatesFinished);
  connect(m_coordOptimizationWorker, &CoordinateOptimizationWorker::optimizeFinished, this,
          &MainWindow::onOptimizeCoordinatesFinished);

  updateWindowTitle();
}

/** Destroy the main window.
   Hides the window first to prevent stale accessibility events on macOS
   (QTBUG-71850).  Shuts down all background worker threads (solver, color
   optimizer, coordinate optimizer) and waits for them to finish.  Explicitly
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
  m_solverQueue.cancelAll();
  m_colorOptimizerQueue.cancelAll();
  for (const auto &entry : m_activeProgresses)
    if (entry.info)
      entry.info->cancel();
  // Result delivery from persistent workers is no longer useful once teardown
  // starts. Disconnect before joining one-shot workers because shutdown may
  // service blocking queued calls from those workers.
  if (m_solverWorker)
    disconnect(m_solverWorker, nullptr, this, nullptr);
  if (m_coordOptimizationWorker)
    disconnect(m_coordOptimizationWorker, nullptr, this, nullptr);
  if (m_colorOptimizerWorker)
    disconnect(m_colorOptimizerWorker, nullptr, this, nullptr);

  shutdownBackgroundThreads();

  if (m_solverThread) {
    m_solverThread->quit();
    m_solverThread->wait();
    delete m_solverWorker;
    m_solverWorker = nullptr;
  }
  
  if (m_coordOptimizationThread) {
    m_coordOptimizationThread->quit();
    m_coordOptimizationThread->wait();
    delete m_coordOptimizationWorker;
    m_coordOptimizationWorker = nullptr;
  }

  if (m_colorOptimizerThread) {
    m_colorOptimizerThread->quit();
    m_colorOptimizerThread->wait();
    delete m_colorOptimizerWorker;
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

/** Track a one-shot worker thread owned by this document's lifetime. */
void MainWindow::trackBackgroundThread(QThread *thread) {
  if (!thread)
    return;
  m_backgroundThreads.erase(
      std::remove_if(m_backgroundThreads.begin(), m_backgroundThreads.end(),
                     [](const QPointer<QThread> &candidate) {
                       return candidate.isNull();
                     }),
      m_backgroundThreads.end());
  m_backgroundThreads.emplace_back(thread);
}

/** Cancel/join every one-shot worker before document members are destroyed. */
void MainWindow::shutdownBackgroundThreads() {
  for (const QPointer<QThread> &guard : m_backgroundThreads) {
    if (QThread *thread = guard.data())
      if (thread->isRunning()) {
        thread->requestInterruption();
        thread->quit();
      }
  }

  // FinetuneMisregisteredWorker can be blocked asking the document for its
  // latest point set through a BlockingQueuedConnection.  A plain wait() from
  // the GUI thread would deadlock in that state.  Poll in short intervals and
  // service only MetaCall events addressed to this document; m_closing makes
  // every result callback a no-op while still allowing the blocking request to
  // return and observe cancellation.
  bool running = true;
  while (running) {
    running = false;
    for (const QPointer<QThread> &guard : m_backgroundThreads) {
      if (QThread *thread = guard.data(); thread && thread->isRunning()) {
        running = true;
        thread->wait(10);
      }
    }
    if (running)
      QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
  }
  m_backgroundThreads.clear();
}

/** Build the entire main window UI.
   Creates the horizontal splitter (image widget | right column), the right
   column (navigation view + tab widget with all panels), the status bar with
   document progress reporting, and the signal/slot connections between
   panels, ImageWidget, NavigationView, and MainWindow. Detachable panel
   sections own their dock lifecycle in ParameterPanel; the document window
   no longer duplicates that presentation machinery. */
void MainWindow::setupUi() {

  m_mainSplitter = new QSplitter(Qt::Horizontal, this);
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
      tr("Capture → Sharpen → Process → Register/Reconstruct → Color"),
      workflowSummary);
  workflowStages->setObjectName(QStringLiteral("WorkflowStages"));
  workflowStages->setWordWrap(true);
  workflowStages->setToolTip(tr(
      "Color-Screen processing stages. The specialist tabs below remain in "
      "their existing beta order."));
  workflowLayout->addWidget(workflowStages);

  m_workflowProcessLabel = new QLabel(workflowSummary);
  m_workflowProcessLabel->setObjectName(
      QStringLiteral("WorkflowProcessSummary"));
  m_workflowProcessLabel->setWordWrap(true);
  workflowLayout->addWidget(m_workflowProcessLabel);

  m_workflowRegistrationLabel = new QLabel(workflowSummary);
  m_workflowRegistrationLabel->setObjectName(
      QStringLiteral("WorkflowRegistrationSummary"));
  m_workflowRegistrationLabel->setWordWrap(true);
  m_workflowRegistrationLabel->setToolTip(tr(
      "Geometry freshness is tracked for fits completed in this session. "
      "Loaded or manually entered geometry remains available but is not "
      "labelled current until it is fitted."));
  workflowLayout->addWidget(m_workflowRegistrationLabel);

  m_workflowCalibrationLabel = new QLabel(workflowSummary);
  m_workflowCalibrationLabel->setObjectName(
      QStringLiteral("WorkflowCalibrationSummary"));
  m_workflowCalibrationLabel->setWordWrap(true);
  workflowLayout->addWidget(m_workflowCalibrationLabel);

  QFont workflowSectionFont = m_workflowProcessLabel->font();
  workflowSectionFont.setWeight(QFont::DemiBold);
  m_workflowProcessLabel->setFont(workflowSectionFont);
  m_workflowRegistrationLabel->setFont(workflowSectionFont);
  m_workflowCalibrationLabel->setFont(workflowSectionFont);

  m_workflowNextStepLabel = new QLabel(workflowSummary);
  m_workflowNextStepLabel->setObjectName(
      QStringLiteral("WorkflowNextStepSummary"));
  m_workflowNextStepLabel->setWordWrap(true);
  QFont nextStepFont = m_workflowNextStepLabel->font();
  nextStepFont.setBold(true);
  m_workflowNextStepLabel->setFont(nextStepFont);
  workflowLayout->addWidget(m_workflowNextStepLabel);

  const bool workflowExpanded =
      QSettings().value(QStringLiteral("workflowSummaryExpanded"), true).toBool();
  auto setWorkflowExpanded =
      [this, workflowToggle, workflowStages](bool expanded) {
        workflowToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        workflowStages->setVisible(expanded);
        m_workflowProcessLabel->setVisible(expanded);
        m_workflowRegistrationLabel->setVisible(expanded);
        m_workflowCalibrationLabel->setVisible(expanded);
        m_workflowNextStepLabel->setVisible(expanded);
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
  mtfCalibration.fitAvailable = [this]() { return !m_mtfFitRunning; };
  mtfCalibration.fitStarted = [this](const colorscreen::mtf_parameters &inputs) {
    return beginMtfModelFit(inputs);
  };
  mtfCalibration.fitFailed = [this](const colorscreen::mtf_parameters &inputs) {
    failMtfModelFit(inputs);
  };
  mtfCalibration.fitAccepted =
      [this](const colorscreen::mtf_parameters &fitted, double rms) {
        acceptMtfModelFit(fitted, rms);
      };
  mtfCalibration.fitFinishedWithoutResult =
      [this]() { finishMtfModelFitWithoutResult(); };
  m_sharpnessPanel =
      new SharpnessPanel([this]() { return getCurrentState(); },
                         [this](const ParameterState &s, const QString &desc) {
                           changeParameters(s, desc);
                         },
                         [this]() { return m_scan; }, std::move(mtfCalibration),
                         this);

  // Create Screen Panel
  m_screenPanel =
      new ScreenPanel([this]() { return getCurrentState(); },
                      [this](const ParameterState &s, const QString &desc) {
                        changeParameters(s, desc);
                      },
                      [this]() { return m_scan; }, this);

  // Create Color Panel (after Sharpness)
  m_contactCopyPanel = new ContactCopyPanel(
      [this]() { return getCurrentState(); },
      [this](const ParameterState &s, const QString &desc) {
        changeParameters(s, desc);
      },
      [this]() { return m_scan; }, this);

  m_colorPanel =
      new ColorPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s, const QString &desc) {
                       changeParameters(s, desc);
                     },
                     [this]() { return m_scan; }, this);

  // Create Profile Panel
  m_profilePanel =
      new ProfilePanel([this]() { return getCurrentState(); },
                       [this](const ParameterState &s, const QString &desc) {
                         changeParameters(s, desc);
                       },
                       [this]() { return m_scan; }, this);

  // Create Tiles Panel
  m_tilesPanel =
      new TilesPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s, const QString &desc) {
                       changeParameters(s, desc);
                     },
                     [this]() { return m_scan; }, this);

  // Create Image Layer Panel
  m_imageLayerPanel =
      new ImageLayerPanel([this]() { return getCurrentState(); },
                          [this](const ParameterState &s, const QString &desc) {
                            changeParameters(s, desc);
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
                       [this](const ParameterState &s, const QString &desc) {
                         changeParameters(s, desc);
                       },
                       [this]() { return m_scan; },
                       [this]() { reloadCurrentImageWithDemosaic(); },
                       this);

  // Create Geometry Panel
  m_geometryPanel =
      new GeometryPanel([this]() { return getCurrentState(); },
                        [this](const ParameterState &s, const QString &desc) {
                          changeParameters(s, desc);
                        },
                        [this]() { return m_scan; }, this);








  m_configTabs->addTab(m_capturePanel, "Digital capture");
  m_configTabs->addTab(m_tilesPanel, "Tiles");
  connect(m_capturePanel, &CapturePanel::cropRequested, this,
          &MainWindow::onCropRequested);
  connect(m_capturePanel, &CapturePanel::measureRequested, this,
          &MainWindow::onMeasureRequested);
  connect(m_capturePanel, &CapturePanel::flatFieldRequested, this,
          &MainWindow::onFlatFieldRequested);
  connect(m_capturePanel, &CapturePanel::autodetectRequested, this,
          &MainWindow::onAutodetectScreen);

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
  m_configTabs->addTab(m_sharpnessPanel, "Sharpness");
  m_configTabs->addTab(m_imageLayerPanel, "Image Layer");
  m_configTabs->addTab(m_contactCopyPanel, "Contact copy");
  m_configTabs->addTab(m_screenPanel, "Screen");
  m_configTabs->addTab(m_geometryPanel, "Geometry");
  m_configTabs->addTab(m_colorPanel, "Color");
  m_configTabs->addTab(m_profilePanel, "Profile");

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
  connect(m_geometryPanel, &GeometryPanel::alternateColorsRequested, this,
          &MainWindow::onAlternateColorsRequested);
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

  // Status Bar
  QStatusBar *statusBar = new QStatusBar(this);
  setStatusBar(statusBar);

  // Keep the ordinary status bar permanently one line high.  Short-lived work
  // may use that line, but long-running user-visible tasks live in a separate
  // frameless bottom dock above it so they can never make the status bar grow.
  m_progressContainer = new QWidget(statusBar);
  m_progressContainer->setObjectName(QStringLiteral("DocumentProgressContainer"));
  m_progressLayout = new QVBoxLayout(m_progressContainer);
  m_progressLayout->setContentsMargins(0, 0, 0, 0);
  m_progressLayout->setSpacing(0);

  m_userVisibleProgressContainer = new QWidget();
  m_userVisibleProgressContainer->setObjectName(
      QStringLiteral("UserVisibleProgressContainer"));
  m_userVisibleProgressLayout =
      new QVBoxLayout(m_userVisibleProgressContainer);
  m_userVisibleProgressLayout->setContentsMargins(4, 2, 4, 2);
  m_userVisibleProgressLayout->setSpacing(2);
  m_userVisibleProgressContainer->hide();

  m_userVisibleProgressDock = new QDockWidget(this);
  m_userVisibleProgressDock->setObjectName(
      QStringLiteral("UserVisibleProgressDock"));
  m_userVisibleProgressDock->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_userVisibleProgressDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  auto *taskDockTitle = new QWidget(m_userVisibleProgressDock);
  taskDockTitle->setFixedHeight(0);
  m_userVisibleProgressDock->setTitleBarWidget(taskDockTitle);
  m_userVisibleProgressDock->setWidget(m_userVisibleProgressContainer);
  addDockWidget(Qt::BottomDockWidgetArea, m_userVisibleProgressDock);
  m_userVisibleProgressDock->hide();

  m_transientProgressRow = new QWidget(m_progressContainer);
  m_transientProgressRow->setObjectName(QStringLiteral("TransientProgressRow"));
  QHBoxLayout *progressLayout = new QHBoxLayout(m_transientProgressRow);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(8);

  m_statusLabel = new QLabel("", m_transientProgressRow);
  m_statusLabel->setMinimumWidth(150);
  progressLayout->addWidget(m_statusLabel);

  m_progressBar = new QProgressBar(m_transientProgressRow);
  m_progressBar->setRange(0, 100);
  m_progressBar->setTextVisible(false);
  m_progressBar->setMinimumWidth(200);
  progressLayout->addWidget(m_progressBar);

  // Progress switcher (count + prev/next buttons)
  m_progressCountLabel = new QLabel("1/1", m_transientProgressRow);
  m_progressCountLabel->setMinimumWidth(40);
  progressLayout->addWidget(m_progressCountLabel);

  m_prevProgressButton = new QPushButton("<", m_transientProgressRow);
  m_prevProgressButton->setMaximumWidth(30);
  m_prevProgressButton->setToolTip("Previous progress");
  connect(m_prevProgressButton, &QPushButton::clicked, this,
          &MainWindow::onPrevProgress);
  progressLayout->addWidget(m_prevProgressButton);

  m_nextProgressButton = new QPushButton(">", m_transientProgressRow);
  m_nextProgressButton->setMaximumWidth(30);
  m_nextProgressButton->setToolTip("Next progress");
  connect(m_nextProgressButton, &QPushButton::clicked, this,
          &MainWindow::onNextProgress);
  progressLayout->addWidget(m_nextProgressButton);

  m_cancelButton = new QPushButton("Cancel", m_transientProgressRow);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &MainWindow::onCancelClicked);
  progressLayout->addWidget(m_cancelButton);

  m_progressLayout->addWidget(m_transientProgressRow);

  QSizePolicy sp = m_transientProgressRow->sizePolicy();
  sp.setRetainSizeWhenHidden(true);
  m_transientProgressRow->setSizePolicy(sp);

  m_progressContainer->setMinimumHeight(m_transientProgressRow->sizeHint().height());

  m_transientProgressRow->hide();
  m_progressContainer->hide();
  statusBar->addPermanentWidget(m_progressContainer, 1);

  // Initialize manual selection tracking
  m_manuallySelectedProgressIndex = -1;

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
    m_lockRelativeCoordinatesAction->setVisible(checked);
    m_optimizeCoordinatesAction->setVisible(checked);
  });

  connect(m_imageWidget, &ImageWidget::selectionChanged, this,
          &MainWindow::updateRegistrationActions);
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          this, &MainWindow::updateRegistrationActions);
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
      (m_scan && colorscreen::screen_has_regular_geometry_p(
                     m_scrToImgParams.type));

  m_coordinateComboBox->blockSignals(true);
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
  m_coordinateComboBox->blockSignals(false);

  const bool finalCoordinates =
      current == colorscreen::render_final_coordinates;
  if (m_finalRotationLabelAction)
    m_finalRotationLabelAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinAction)
    m_finalRotationSpinAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinBox) {
    m_finalRotationSpinBox->blockSignals(true);
    m_finalRotationSpinBox->setValue(m_scrToImgParams.final_rotation);
    m_finalRotationSpinBox->blockSignals(false);
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
  m_modeComboBox->blockSignals(true);
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
        (!hasScreenCapture || !colorscreen::screen_has_regular_geometry_p(
                                  m_scrToImgParams.type)))
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

  m_modeComboBox->blockSignals(false);
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
      "Maintain relative positions of registration points when the grid size "
      "changes.");
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

/** Open a .par parameter file chosen by the user.
   Prompts for unsaved changes first, then resets all parameter structs to
   defaults before loading (load_csp merges into existing values, so a reset
   is needed for clean loading).  On success, re-initialises the image widget
   and renderer with new parameters, clears undo history, and refreshes the
   UI.  On error, restores the previous parameter values.  */
void MainWindow::onOpenParameters() {
  // Check for unsaved changes before loading new parameters
  if (!maybeSave()) {
    return;
  }

  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Parameters", QString(), "Parameters (*.par);;All Files (*)");
  if (fileName.isEmpty())
    return;

  QTimer::singleShot(0, this, [this, fileName]() {
    if (loadParameterFile(fileName)) {
      statusBar()->showMessage(QString("Parameters loaded from %1").arg(fileName),
                               3000);
    }
  });
}

/** Save parameters to the current .par file.
   A weak auto-suggested filename is confirmed through Save As before writing.
   Saving is synchronous so closeEvent can reliably decide whether it is safe
   to close this particular document window.  */
void MainWindow::onSaveParameters() {
  if (m_currentParamsFile.isEmpty() || m_currentParamsFileIsWeak) {
    saveParametersAs();
    return;
  }
  saveParametersToFile(m_currentParamsFile);
}

/** Save parameters to a new .par file chosen by the user. */
void MainWindow::onSaveParametersAs() { saveParametersAs(); }

/** Write the current document parameters to FILENAME and mark them saved. */
bool MainWindow::saveParametersToFile(const QString &fileName) {
  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  FILE *f = fopen(absoluteFileName.toUtf8().constData(), "wt");
  if (!f) {
    QMessageBox::critical(
        this, "Error",
        QString("Could not open file for writing: %1").arg(absoluteFileName));
    return false;
  }

  const bool hasRgb = m_scan && m_scan->has_rgb();
  bool saved = colorscreen::save_csp_with_profile_spots(
      f, &m_scrToImgParams, hasRgb ? &m_detectParams : nullptr, &m_rparams,
      &m_solverParams, m_profileSpots);
  if (fclose(f) != 0)
    saved = false;

  if (!saved) {
    QMessageBox::critical(this, "Error", "Failed to save parameters.");
    return false;
  }

  m_currentParamsFile = absoluteFileName;
  m_currentParamsFileIsWeak = false;
  m_recoveryDirty = false;
  addToRecentParams(absoluteFileName);
  if (m_undoStack)
    m_undoStack->setClean();
  updateWindowTitle();
  saveRecoveryState();

  statusBar()->showMessage(
      QString("Parameters saved to %1").arg(absoluteFileName), 3000);
  return true;
}

/** Ask for a parameter filename and save it before returning to the caller. */
bool MainWindow::saveParametersAs() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Save Parameters",
      m_currentParamsFile.isEmpty() ? QString() : m_currentParamsFile,
      "Parameters (*.par);;All Files (*)");
  if (fileName.isEmpty())
    return false;

  if (!fileName.endsWith(QLatin1String(".par"), Qt::CaseInsensitive))
    fileName += QStringLiteral(".par");
  return saveParametersToFile(fileName);
}

/** Show a multi-selection file dialog and open each image independently.
   The application may reuse this window only when it is untouched and empty;
   otherwise every selected image receives a new MainWindow.  Dispatch is
   deferred by one event-loop turn so KDE can dispose of KIO file-dialog jobs
   before an associated parameter prompt is shown.  */
void MainWindow::onOpenImage() {
  const QStringList fileNames = QFileDialog::getOpenFileNames(
      this, "Open Images", m_lastOpenDir,
      "Images (*.tif *.tiff *.jpg *.jpeg *.jp2 *.j2k *.jpc *.jpf *.jpx *.png "
      "*.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq *.csprj);;All "
      "Files (*)");
  if (fileNames.isEmpty())
    return;

  m_lastOpenDir = QFileInfo(fileNames.constFirst()).absolutePath();
  const QPointer<MainWindow> guardedWindow(this);
  QTimer::singleShot(0, qApp, [guardedWindow, fileNames]() {
    if (!guardedWindow)
      return;
    if (ColorScreenApplication *application = documentApplication())
      application->openFiles(fileNames, guardedWindow);
    else
      guardedWindow->loadFile(fileNames.constFirst());
  });
}

/** Register ordinary transient background progress. */
void MainWindow::addProgress(std::shared_ptr<colorscreen::progress_info> info) {
  registerProgress(std::move(info), false, QString(), ProgressAction::Cancel);
}

/** Register a long-running task with its own status-bar row. */
void MainWindow::addUserVisibleProgress(
    std::shared_ptr<colorscreen::progress_info> info, const QString &title,
    ProgressAction action) {
  registerProgress(std::move(info), true, title, action);
}

/** Register INFO and create a dedicated row when USERVISIBLE is true. */
void MainWindow::registerProgress(
    std::shared_ptr<colorscreen::progress_info> info, bool userVisible,
    const QString &title, ProgressAction action) {
  if (!info)
    return;

  ProgressEntry entry;
  entry.info = std::move(info);
  entry.userVisible = userVisible;
  entry.action = action;
  entry.title = title;
  entry.startTime.start();

  if (userVisible) {
    entry.row = new QWidget(m_userVisibleProgressContainer);
    entry.row->setObjectName(QStringLiteral("UserVisibleProgressRow"));
    entry.row->setProperty("progressTitle", title);
    QHBoxLayout *rowLayout = new QHBoxLayout(entry.row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    entry.rowLabel = new QLabel(title, entry.row);
    entry.rowLabel->setMinimumWidth(220);
    rowLayout->addWidget(entry.rowLabel, 1);

    entry.rowProgressBar = new QProgressBar(entry.row);
    entry.rowProgressBar->setRange(0, 0);
    entry.rowProgressBar->setTextVisible(false);
    entry.rowProgressBar->setMinimumWidth(200);
    rowLayout->addWidget(entry.rowProgressBar);

    entry.rowActionButton = new QPushButton(
        action == ProgressAction::Stop ? tr("Stop") : tr("Cancel"), entry.row);
    // Keep mouse clicks on a workspace-global task from taking keyboard focus
    // away from the active MDI image. Keyboard users can still Tab to the
    // control and activate it normally.
    entry.rowActionButton->setFocusPolicy(Qt::TabFocus);
    entry.rowActionButton->setProperty(
        "progressAction",
        action == ProgressAction::Stop ? QStringLiteral("stop")
                                       : QStringLiteral("cancel"));
    const std::shared_ptr<colorscreen::progress_info> progress = entry.info;
    connect(entry.rowActionButton, &QPushButton::clicked, this,
            [this, progress, action]() {
              requestProgressTermination(progress, action);
            });
    rowLayout->addWidget(entry.rowActionButton);

    // Dedicated rows stay together in the persistent user-visible container.
    m_userVisibleProgressLayout->addWidget(entry.row);
    entry.row->show();
  }

  m_activeProgresses.push_back(std::move(entry));
  if (!m_progressTimer->isActive())
    m_progressTimer->start();

  updateProgressContainerVisibility();
}

/** Return the active progresses that share the transient status row. */
std::vector<ProgressEntry *> MainWindow::transientProgresses() {
  std::vector<ProgressEntry *> result;
  result.reserve(m_activeProgresses.size());
  for (ProgressEntry &entry : m_activeProgresses)
    if (!entry.userVisible)
      result.push_back(&entry);
  return result;
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
  int removedTransientIndex = -1;
  int transientIndex = 0;

  for (auto it = m_activeProgresses.begin(); it != m_activeProgresses.end();
       ++it) {
    if (it->info == info) {
      if (!it->userVisible)
        removedTransientIndex = transientIndex;
      if (it->row) {
        releaseUserVisibleProgressFocus(it->row);
        m_userVisibleProgressLayout->removeWidget(it->row);
        it->row->deleteLater();
      }
      m_activeProgresses.erase(it);
      break;
    }
    if (!it->userVisible)
      ++transientIndex;
  }

  if (m_currentlyDisplayedProgress == info)
    m_currentlyDisplayedProgress.reset();

  if (removedTransientIndex >= 0 && m_manuallySelectedProgressIndex >= 0) {
    if (m_manuallySelectedProgressIndex == removedTransientIndex)
      m_manuallySelectedProgressIndex = -1;
    else if (m_manuallySelectedProgressIndex > removedTransientIndex)
      --m_manuallySelectedProgressIndex;
  }

  const std::vector<ProgressEntry *> transient = transientProgresses();
  if (transient.empty()) {
    setTransientProgressVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
  } else if (m_manuallySelectedProgressIndex >= (int)transient.size()) {
    m_manuallySelectedProgressIndex = -1;
  }

  if (m_activeProgresses.empty())
    m_progressTimer->stop();

  updateProgressContainerVisibility();
}

/** Find the most relevant transient task to display in the shared row. */
ProgressEntry *MainWindow::getLongestRunningTask() {
  ProgressEntry *oldestActive = nullptr;
  ProgressEntry *oldestAny = nullptr;
  qint64 maxActiveTime = -1;
  qint64 maxAnyTime = -1;

  for (ProgressEntry *entry : transientProgresses()) {
    const qint64 elapsed = entry->startTime.elapsed();
    float percent = 0;
    entry->info->get_status(&percent);

    if (percent > 0 && elapsed > maxActiveTime) {
      maxActiveTime = elapsed;
      oldestActive = entry;
    }
    if (elapsed > maxAnyTime) {
      maxAnyTime = elapsed;
      oldestAny = entry;
    }
  }

  return oldestActive ? oldestActive : oldestAny;
}

/** Format ENTRY's nested progress state into LABEL and BAR. */
void MainWindow::updateProgressWidgets(const ProgressEntry &entry, QLabel *label,
                                       QProgressBar *bar,
                                       const QString &title) {
  if (!entry.info || !label || !bar)
    return;

  const std::vector<colorscreen::progress_info::status> statusStack =
      entry.info->get_status();
  QStringList tasks;
  float percent = -1;

  for (const auto &status : statusStack) {
    if (!status.task.empty()) {
      QString taskName = QString::fromUtf8(status.task.c_str());
      if (status.progress >= 0 && &status != &statusStack.back())
        taskName += QString(" (%1%)").arg((int)status.progress);
      tasks.append(taskName);
    }
    if (status.progress >= 0)
      percent = status.progress;
  }

  QString statusText = tasks.join(QStringLiteral(" > "));
  if (!title.isEmpty()) {
    if (statusText.isEmpty())
      statusText = title;
    else if (statusText.compare(title, Qt::CaseInsensitive) != 0)
      statusText = title + QStringLiteral(": ") + statusText;
  } else if (statusText.isEmpty()) {
    statusText = tr("Working...");
  }

  const qint64 elapsedMs = entry.startTime.elapsed();
  if (elapsedMs > 20000 && percent > 0.1f) {
    const double doneFraction = (double)percent / 100.0;
    const qint64 remainingMs =
        (qint64)((double)elapsedMs / doneFraction) - elapsedMs;
    if (remainingMs > 0) {
      const int remainingSec = (remainingMs / 1000) % 60;
      const int remainingMin = remainingMs / 60000;
      statusText += QString(" (ETR: %1:%2)")
                        .arg(remainingMin)
                        .arg(remainingSec, 2, 10, QChar('0'));
    }
  }

  label->setText(statusText);
  if (percent >= 0) {
    bar->setRange(0, 100);
    bar->setValue((int)percent);
  } else {
    bar->setRange(0, 0);
  }
}

/** Show or hide this document's one-line transient progress presentation. */
void MainWindow::setTransientProgressVisible(bool visible) {
  if (m_transientProgressRow)
    m_transientProgressRow->setVisible(visible);
  if (m_progressContainer)
    m_progressContainer->setVisible(visible);
  if (m_transientProgressVisible == visible)
    return;
  m_transientProgressVisible = visible;
  emit transientProgressVisibilityChanged(visible);
}

/** Synchronize the one-line transient status and dedicated task dock. */
void MainWindow::updateProgressContainerVisibility() {
  bool hasUserVisibleRows = false;
  for (const ProgressEntry &entry : m_activeProgresses) {
    if (entry.userVisible && entry.row && !entry.row->isHidden()) {
      hasUserVisibleRows = true;
      break;
    }
  }

  const bool visibilityChanged =
      m_userVisibleProgressContainer->isHidden() == hasUserVisibleRows;
  m_userVisibleProgressContainer->setVisible(hasUserVisibleRows);
  if (m_userVisibleProgressDock &&
      m_userVisibleProgressDock->widget() == m_userVisibleProgressContainer)
    m_userVisibleProgressDock->setVisible(hasUserVisibleRows);
  if (visibilityChanged)
    emit userVisibleProgressVisibilityChanged(hasUserVisibleRows);

}

/** Periodically update transient progress and every dedicated long-task row. */
void MainWindow::onProgressTimer() {
  if (m_activeProgresses.empty()) {
    setTransientProgressVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
    m_progressTimer->stop();
    return;
  }

  for (ProgressEntry &entry : m_activeProgresses) {
    if (!entry.userVisible)
      continue;
    updateProgressWidgets(entry, entry.rowLabel, entry.rowProgressBar,
                          entry.title);
    if (entry.rowActionButton && entry.info->pool_cancel()) {
      // Cancellation can also be requested externally. If a keyboard user had
      // focused this control, move focus back to the current image before the
      // disabled button makes Qt choose a fallback MDI child.
      releaseUserVisibleProgressFocus(entry.row);
      entry.rowActionButton->setText(entry.action == ProgressAction::Stop
                                         ? tr("Stopping...")
                                         : tr("Cancelling..."));
      entry.rowActionButton->setEnabled(false);
    }
  }

  const std::vector<ProgressEntry *> transient = transientProgresses();
  if (transient.empty()) {
    setTransientProgressVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
    updateProgressContainerVisibility();
    return;
  }

  ProgressEntry *task = nullptr;
  int currentIndex = 0;
  if (m_manuallySelectedProgressIndex >= 0 &&
      m_manuallySelectedProgressIndex < (int)transient.size()) {
    currentIndex = m_manuallySelectedProgressIndex;
    task = transient[currentIndex];
  } else {
    task = getLongestRunningTask();
    for (size_t i = 0; task && i < transient.size(); ++i) {
      if (transient[i] == task) {
        currentIndex = (int)i;
        break;
      }
    }
    m_manuallySelectedProgressIndex = -1;
  }

  if (!task) {
    setTransientProgressVisible(false);
    updateProgressContainerVisibility();
    return;
  }

  m_currentlyDisplayedProgress = task->info;
  m_progressCountLabel->setText(
      QString("%1/%2").arg(currentIndex + 1).arg(transient.size()));

  const bool multiple = transient.size() > 1;
  m_prevProgressButton->setVisible(multiple);
  m_nextProgressButton->setVisible(multiple);
  m_progressCountLabel->setVisible(multiple);

  if (task->startTime.elapsed() > 300) {
    updateProgressWidgets(*task, m_statusLabel, m_progressBar, QString());
    setTransientProgressVisible(true);
  } else {
    setTransientProgressVisible(false);
  }

  updateProgressContainerVisibility();
}

/** Request cooperative termination of INFO using ACTION's user-facing policy. */
void MainWindow::requestProgressTermination(
    const std::shared_ptr<colorscreen::progress_info> &info,
    ProgressAction action) {
  if (!info)
    return;

  auto renderProgress = m_renderProgress.lock();
  if (action == ProgressAction::Cancel && renderProgress &&
      renderProgress == info) {
    const auto ret = QMessageBox::question(
        this, tr("Cancel Rendering"), tr("Cancel the current rendering?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
      return;
  }

  // A mouse click gives the Stop/Cancel button keyboard focus.  Move that
  // focus to the currently active image before cancel() can synchronously
  // finish the worker or before disabling the button makes Qt choose another
  // MDI child as the fallback focus widget.
  for (ProgressEntry &entry : m_activeProgresses) {
    if (entry.info == info && entry.row)
      releaseUserVisibleProgressFocus(entry.row);
  }

  info->cancel();
  for (ProgressEntry &entry : m_activeProgresses) {
    if (entry.info != info || !entry.rowActionButton)
      continue;
    entry.rowActionButton->setText(action == ProgressAction::Stop
                                       ? tr("Stopping...")
                                       : tr("Cancelling..."));
    entry.rowActionButton->setEnabled(false);
  }
}

/** Cancel the currently displayed transient progress task. */
void MainWindow::onCancelClicked() {
  requestProgressTermination(m_currentlyDisplayedProgress,
                             ProgressAction::Cancel);
}

/** Switch the shared transient progress row to the previous background task. */
void MainWindow::onPrevProgress() {
  const std::vector<ProgressEntry *> transient = transientProgresses();
  if (transient.size() <= 1)
    return;

  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = getLongestRunningTask();
    for (size_t i = 0; i < transient.size(); ++i) {
      if (transient[i] == currentTask) {
        m_manuallySelectedProgressIndex = (int)i;
        break;
      }
    }
  }

  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex - 1 + (int)transient.size()) %
      (int)transient.size();
}

/** Switch the shared transient progress row to the next background task. */
void MainWindow::onNextProgress() {
  const std::vector<ProgressEntry *> transient = transientProgresses();
  if (transient.size() <= 1)
    return;

  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = getLongestRunningTask();
    for (size_t i = 0; i < transient.size(); ++i) {
      if (transient[i] == currentTask) {
        m_manuallySelectedProgressIndex = (int)i;
        break;
      }
    }
  }

  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex + 1) % (int)transient.size();
}

/** Post-load initialisation after a new image has been opened.
   Updates the render mode menu, sets the scan reference on all background
   workers, feeds the image to NavigationView, shows/hides the Tiles tab
   based on stitch data, shows/hides Profile and ImageLayer tabs based on
   RGB availability, refreshes all panel state, and enables the Render
   action.  */
void MainWindow::onImageLoaded() {
  clearFocusAreaAnalysis();
  resetProfileCalibrationProvenance();
  // Update UI components that depend on loaded image
  updateModeMenu();
  if (m_scan) {
    if (m_solverWorker)
      m_solverWorker->setScan(m_scan);
    if (m_colorOptimizerWorker)
      m_colorOptimizerWorker->setScan(m_scan);
    if (m_coordOptimizationWorker)
      m_coordOptimizationWorker->setScan(m_scan);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    m_navigationView->setMinScale(m_imageWidget->getMinScale());
  }

  if (m_tilesPanel) {
    m_tilesPanel->updateForNewImage();
    bool hasStitch = (m_scan && m_scan->stitch);
    int tilesTabIndex = m_configTabs->indexOf(m_tilesPanel);
    if (tilesTabIndex >= 0) {
      m_configTabs->setTabVisible(tilesTabIndex, hasStitch);
    }
  }

  if (m_profilePanel) {
    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);
    if (profileTabIndex >= 0) {
      m_configTabs->setTabVisible(profileTabIndex, m_scan && m_scan->has_rgb());
    }
  }

  if (m_imageLayerPanel) {
    int layerTabIndex = m_configTabs->indexOf(m_imageLayerPanel);
    if (layerTabIndex >= 0) {
      m_configTabs->setTabVisible(layerTabIndex, m_scan && m_scan->has_rgb());
    }
  }

  // Refresh param values too
  applyState(getCurrentState());
  updateRegistrationActions();
  updateRegistrationGroupVisibility();
  m_renderAction->setEnabled(m_scan != nullptr);
}

// Recent Files Implementation

/** Add a file path to the most-recently-used image files list.
   Moves it to the front, caps the list at MaxRecentFiles, rebuilds
   the menu, and persists to QSettings.  */
void MainWindow::addToRecentFiles(const QString &filePath) {
  // Another document may have updated the application-wide list since this
  // window was created.  Merge against the latest persisted value before
  // writing so independently finishing image loads cannot lose entries.
  QSettings settings;
  m_recentFiles = settings.value("recentFiles").toStringList();
  QString absolutePath = QFileInfo(filePath).absoluteFilePath();
  m_recentFiles.removeAll(absolutePath);
  m_recentFiles.prepend(absolutePath);

  while (m_recentFiles.size() > MaxRecentFiles)
    m_recentFiles.removeLast();

  settings.setValue("recentFiles", m_recentFiles);
  updateRecentFileActions();
}

/** Rebuild the "Open Recent" submenu from the m_recentFiles list.
   Adds a "Clear Recent Files" action at the bottom.  */
void MainWindow::updateRecentFileActions() {
  m_recentFilesMenu->clear();
  m_recentFileActions.clear();

  for (int i = 0; i < m_recentFiles.size(); ++i) {
    QString fileName = QFileInfo(m_recentFiles[i]).fileName();
    fileName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QString text = tr("&%1 %2").arg(i + 1).arg(fileName);
    QAction *action =
        m_recentFilesMenu->addAction(text, this, &MainWindow::openRecentFile);
    action->setData(m_recentFiles[i]);
    action->setToolTip(m_recentFiles[i]);
    m_recentFileActions.append(action);
  }

  if (m_recentFiles.isEmpty()) {
    m_recentFilesMenu->addAction("No Recent Files")->setEnabled(false);
  } else {
    m_recentFilesMenu->addSeparator();
    QAction *clearAction = m_recentFilesMenu->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, this, [this]() {
      m_recentFiles.clear();
      updateRecentFileActions();
      saveRecentFiles();
    });
  }
}

/** Rebuild this document's Window menu from the application's live list. */
void MainWindow::refreshWindowMenu() {
  if (ColorScreenApplication *application = documentApplication())
    application->populateWindowMenu(m_windowMenu, this);
}

/** Remove transient progress from the private status bar for workspace hosting. */
QWidget *MainWindow::takeWorkspaceStatusWidget() {
  if (!m_progressContainer)
    return nullptr;
  standaloneStatusBar()->removeWidget(m_progressContainer);
  m_progressContainer->setParent(nullptr);
  return m_progressContainer;
}

/** Return transient progress to this document's private status bar. */
void MainWindow::restoreWorkspaceStatusWidget() {
  if (!m_progressContainer)
    return;
  if (m_progressContainer->parentWidget() != standaloneStatusBar()) {
    m_progressContainer->setParent(standaloneStatusBar());
    standaloneStatusBar()->addPermanentWidget(m_progressContainer, 1);
  }
  m_progressContainer->setVisible(m_transientProgressVisible);
}

/** Remove persistent progress rows from the local task-progress dock. */
QWidget *MainWindow::takeUserVisibleStatusWidget() {
  if (!m_userVisibleProgressContainer || !m_userVisibleProgressDock)
    return m_userVisibleProgressContainer;
  if (m_userVisibleProgressDock->widget() == m_userVisibleProgressContainer) {
    m_userVisibleProgressDock->setWidget(nullptr);
    m_userVisibleProgressContainer->setParent(nullptr);
    m_userVisibleProgressDock->hide();
  }
  return m_userVisibleProgressContainer;
}

/** Return persistent user-visible progress rows to this document's task dock. */
void MainWindow::restoreUserVisibleStatusWidget() {
  if (!m_userVisibleProgressContainer || !m_userVisibleProgressDock)
    return;
  if (m_userVisibleProgressDock->widget() != m_userVisibleProgressContainer)
    m_userVisibleProgressDock->setWidget(m_userVisibleProgressContainer);
  updateProgressContainerVisibility();
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
  if (m_lockRelativeCoordinatesAction)
    m_lockRelativeCoordinatesAction->setVisible(mode == ImageWidget::SetCenterMode);
  if (m_optimizeCoordinatesAction)
    m_optimizeCoordinatesAction->setVisible(mode == ImageWidget::SetCenterMode);
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

/** Open a recent image without replacing an occupied document window. */
void MainWindow::openRecentFile() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action)
    return;

  const QString fileName = action->data().toString();
  if (ColorScreenApplication *application = documentApplication())
    application->openFiles({fileName}, this);
  else
    loadFile(fileName);
}

/** Reload the current scan with the selected demosaic mode.  Reloading clears
   the undo stack after replacing image_data, so preserve the document's dirty
   state explicitly when unsaved parameters preceded the reload. */
void MainWindow::reloadCurrentImageWithDemosaic() {
  if (m_currentImageFile.isEmpty())
    return;
  if (isDocumentModified())
    m_recoveryDirty = true;
  loadFile(m_currentImageFile, true);
  if (ColorScreenApplication *application = documentApplication())
    application->reloadSlantedEdgeReferences(this);
}

/** Offer conservative post-load setup recommendations for a new image.
   Detected capture metadata is copied only after explicit user confirmation;
   loading an existing parameter file remains authoritative. */
void MainWindow::maybeOfferInitialSetupGuide(
    const colorscreen::monochrome_bayer_analysis &analysis,
    bool suggestDetectedMetadata) {
  if (!m_scan)
    return;

  const bool suggestCaptureType =
      m_rparams.get_capture_type(m_scan.get()) ==
          colorscreen::render_parameters::capture_unknown;
  const bool looksMonochrome = analysis.candidate && m_scan->has_rgb();
  bool suggestBayer = suggestDetectedMetadata && looksMonochrome &&
      m_rparams.demosaic !=
          colorscreen::image_data::demosaic_monochromatic_bayer_corrected;
  bool suggestFStop = suggestDetectedMetadata && m_scan->f_stop > 0 &&
      std::abs(m_scan->f_stop - m_rparams.sharpen.scanner_mtf.f_stop) > 0.01;
  bool suggestPitch = suggestDetectedMetadata && m_scan->pixel_pitch > 0 &&
      std::abs(m_scan->pixel_pitch - m_rparams.sharpen.scanner_mtf.pixel_pitch) > 0.001;
  bool suggestFill = suggestDetectedMetadata && m_scan->sensor_fill_factor > 0 &&
      std::abs(m_scan->sensor_fill_factor - m_rparams.sharpen.scanner_mtf.sensor_fill_factor) > 0.001;
  bool suggestDPI = suggestDetectedMetadata && m_scan->xdpi > 0 &&
      std::abs(m_scan->xdpi - m_rparams.sharpen.scanner_mtf.scan_dpi) > 0.1;
  bool suggestWavelengths = false;
  for (int c = 0; suggestDetectedMetadata && c < 4; ++c) {
    const bool present = c < 3 ? m_scan->has_rgb()
                               : m_scan->has_grayscale_or_ir();
    const double wavelength = m_scan->wavelengths[c];
    if (present && colorscreen::my_isfinite(wavelength) && wavelength > 0
        && std::abs(wavelength
                    - m_rparams.sharpen.scanner_mtf.wavelengths[c]) > 0.5)
      suggestWavelengths = true;
  }

  if (!suggestCaptureType && !suggestBayer && !suggestFStop && !suggestPitch
      && !suggestFill && !suggestDPI && !suggestWavelengths)
    return;

  const std::shared_ptr<colorscreen::image_data> guideScan = m_scan;
  auto *dialog = new InitialSetupGuideDialog(
      this, suggestCaptureType, looksMonochrome, suggestBayer, suggestFStop,
      suggestPitch,
      suggestFill, suggestDPI, suggestWavelengths, guideScan.get());
  connect(
      dialog, &QDialog::finished, this,
      [this, dialog, guideScan, suggestCaptureType, suggestBayer, suggestFStop,
       suggestPitch, suggestFill, suggestDPI, suggestWavelengths](int result) {
        if (result != QDialog::Accepted || !m_scan ||
            m_scan.get() != guideScan.get())
          return;

        ParameterState state = getCurrentState();
        QStringList changes;

        if (suggestCaptureType) {
          const auto capture = dialog->selectedCaptureType();
          if (capture != colorscreen::render_parameters::capture_unknown) {
            state.rparams.capture_type = capture;
            if (!colorscreen::render_parameters::capture_has_screen_p(capture))
              state.scrToImg.type = colorscreen::NoScreen;
            changes << tr("capture type");
          }
        }

        const bool useBayer =
            suggestBayer && dialog->useMonochromeBayerCorrection();
        if (useBayer) {
          state.rparams.demosaic =
              colorscreen::image_data::demosaic_monochromatic_bayer_corrected;
          changes << tr("Bayer compensation");
        }

        if (suggestFStop && dialog->useFStop()) {
          state.rparams.sharpen.scanner_mtf.f_stop = guideScan->f_stop;
          changes << tr("f-stop");
        }

        if (suggestPitch && dialog->usePixelPitch()) {
          state.rparams.sharpen.scanner_mtf.pixel_pitch =
              guideScan->pixel_pitch;
          changes << tr("pixel pitch");
        }

        if (suggestFill && dialog->useFillFactor()) {
          state.rparams.sharpen.scanner_mtf.sensor_fill_factor =
              guideScan->sensor_fill_factor;
          changes << tr("sensor fill factor");
        }

        if (suggestDPI && dialog->useDPI()) {
          state.rparams.sharpen.scanner_mtf.scan_dpi = guideScan->xdpi;
          changes << tr("image resolution");
        }

        if (suggestWavelengths && dialog->useWavelengths()) {
          for (int c = 0; c < 4; ++c) {
            const bool present = c < 3 ? guideScan->has_rgb()
                                       : guideScan->has_grayscale_or_ir();
            const double wavelength = guideScan->wavelengths[c];
            if (present && colorscreen::my_isfinite(wavelength)
                && wavelength > 0)
              state.rparams.sharpen.scanner_mtf.wavelengths[c] = wavelength;
          }
          changes << tr("channel wavelengths");
        }

        if (changes.isEmpty())
          return;

        changeParameters(
            state, tr("Use autodetected %1").arg(changes.join(", ")));
        if (useBayer)
          reloadCurrentImageWithDemosaic();
      });
  connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
  dialog->open();
}

/** Load an image file and optionally its associated .par parameter file.
   If SUPPRESSPARAMPROMPT is false, checks for a .par file alongside the
   image and offers to load it.  If the user declines or no .par file exists,
   a weak (suggested) parameter filename is set for later Save.
   The actual image loading runs asynchronously via QtConcurrent::run; on
   completion, the scan is set on ImageWidget, stitch tile loading is launched
   in parallel for .csprj projects, and undo history is cleared.  */
void MainWindow::loadFile(const QString &fileName, bool suppressParamPrompt) {
  if (fileName.isEmpty())
    return;

  m_imageLoadPending = true;
  bool parameterDataLoaded = false;
  if (!suppressParamPrompt)
    m_recoveryDirty = false;
  m_currentImageFile = QFileInfo(fileName).absoluteFilePath();
  updateWindowTitle();

  // Clear current image and stop rendering
  m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  // Check for .par file (only if not suppressed, e.g., during recovery)
  if (!suppressParamPrompt) {
    QFileInfo fileInfo(m_currentImageFile);
    QString parFile =
        fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";

    if (QFile::exists(parFile)) {
      if (QMessageBox::question(this, "Load Parameters?",
                                "A parameter file was found for this image. Do "
                                "you want to load it?") == QMessageBox::Yes) {

        FILE *f = fopen(parFile.toUtf8().constData(), "r");
        if (f) {
          const char *error = nullptr;
          // load_csp merges parameters in; reset first.
          colorscreen::scr_to_img_parameters emptyScrToImg;
          m_scrToImgParams = emptyScrToImg;
          colorscreen::scr_detect_parameters emptyScrDetect;
          m_detectParams = emptyScrDetect;
          colorscreen::render_parameters emptyRparams;
          m_rparams = emptyRparams;
          colorscreen::solver_parameters emptySolver;
          m_solverParams = emptySolver;
          m_profileSpots.clear();
          m_profileSpotResults.clear();
          if (!colorscreen::load_csp_with_profile_spots(
                  f, &m_scrToImgParams, &m_detectParams, &m_rparams,
                  &m_solverParams, &error, &m_profileSpots,
                  &m_profileSpotResults)) {
            QMessageBox::warning(this, "Error Loading Parameters",
                                 error ? QString::fromUtf8(error)
                                       : "Unknown error loading parameters.");
          } else {
            parameterDataLoaded = true;
            m_prevScrToImgParams = m_scrToImgParams;
            m_prevDetectParams = m_detectParams;

            // Track the loaded parameter file
            m_currentParamsFile = parFile;
            m_currentParamsFileIsWeak =
                false; // This is a real file, not a suggestion
            addToRecentParams(parFile);

            // If we have a valid screen type, default to formatted
            // (interpolated) view
            if (colorscreen::screen_has_regular_geometry_p(
                    m_scrToImgParams.type)) {
              m_renderTypeParams.type = colorscreen::render_type_interpolated;
            }
          }
          fclose(f);
        }
      } else {
        // User declined to load parameters - suggest filename
        QFileInfo fileInfo(fileName);
        m_currentParamsFile =
            fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
        m_currentParamsFileIsWeak = true;
      }
    } else {
      // No parameter file exists - suggest filename
      QFileInfo fileInfo(m_currentImageFile);
      m_currentParamsFile =
          fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
      m_currentParamsFileIsWeak = true;
    }
  }

  const bool suggestDetectedMetadata =
      !suppressParamPrompt && !parameterDataLoaded;
  const bool offerInitialGuide =
      !suppressParamPrompt &&
      (suggestDetectedMetadata ||
       m_rparams.capture_type == colorscreen::render_parameters::capture_unknown);

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Opening image", 0);
  addProgress(progress);

  std::shared_ptr<colorscreen::image_data> tempScan =
      std::make_shared<colorscreen::image_data>();
  // Access m_rparams carefully. It's a member.
  colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;

  bool isCsprj =
      fileName.endsWith(QLatin1String(".csprj"), Qt::CaseInsensitive);

  QFutureWatcher<std::pair<bool, QString>> *watcher =
      new QFutureWatcher<std::pair<bool, QString>>(this);
  connect(
      watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
      [this, watcher, tempScan, progress, fileName, isCsprj,
       offerInitialGuide, suggestDetectedMetadata]() {
        if (m_closing) {
          watcher->deleteLater();
          return;
        }
        std::pair<bool, QString> result = watcher->result();
        m_imageLoadPending = false;
        removeProgress(progress);
        watcher->deleteLater();

        if (result.first) {
          m_scan = tempScan;

          if ((int)m_scan->gamma != -2 && m_scan->gamma > 0 &&
              m_rparams.gamma == -1) // Update only if unknown
            m_rparams.gamma = m_scan->gamma;
          else if (m_rparams.gamma == -1)
            m_rparams.gamma = -1;

          m_undoStack->clear();

          // If this is a stitched project, disable all tiles initially so
          // the UI is responsive while tiles load in the background.
          if (isCsprj && m_scan->stitch) {
            colorscreen::stitch_project *stitch = m_scan->stitch;
            int w = stitch->params.width;
            int h = stitch->params.height;
            m_rparams.set_tile_adjustments_dimensions(w, h);
            for (int y = 0; y < h; y++)
              for (int x = 0; x < w; x++)
                m_rparams.get_tile_adjustment(x, y).enabled = false;
          }

          m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                  &m_detectParams, &m_renderTypeParams,
                                  &m_solverParams);
          onImageLoaded();

          // Add to recent files and immediately establish this document's
          // independent crash-recovery payload.
          addToRecentFiles(m_currentImageFile);
          saveRecoveryState();
          updateWindowTitle();

          if (offerInitialGuide) {
            const colorscreen::monochrome_bayer_analysis analysis =
                m_scan->analyze_monochrome_bayer();
            QTimer::singleShot(
                0, this, [this, analysis, suggestDetectedMetadata]() {
                  maybeOfferInitialSetupGuide(analysis,
                                              suggestDetectedMetadata);
                });
          }

          // Launch background tile loading for stitch projects.
          if (isCsprj && m_scan->stitch) {
            colorscreen::stitch_project *stitch = m_scan->stitch;
            int w = stitch->params.width;
            int h = stitch->params.height;

            for (int ty = 0; ty < h; ty++) {
              for (int tx = 0; tx < w; tx++) {
                auto tileProgress =
                    std::make_shared<colorscreen::progress_info>();
                tileProgress->set_task(
                    qPrintable(tr("Loading tile %1,%2").arg(tx).arg(ty)), 1);
                addProgress(tileProgress);

                auto scanRef = m_scan; // keep scan alive
                int capturedX = tx;
                int capturedY = ty;

                auto *tileWatcher = new QFutureWatcher<bool>(this);
                connect(tileWatcher, &QFutureWatcher<bool>::finished, this,
                        [this, tileWatcher, tileProgress, scanRef, capturedX,
                         capturedY]() {
                          if (m_closing) {
                            tileWatcher->deleteLater();
                            return;
                          }
                          bool ok = tileWatcher->result();
                          removeProgress(tileProgress);
                          tileWatcher->deleteLater();

                          if (ok) {
                            // Enable the tile and trigger a re-render.
                            ParameterState state = getCurrentState();
                            state.rparams
                                .get_tile_adjustment(capturedX, capturedY)
                                .enabled = true;
                            changeParameters(state, tr("Tile loaded %1,%2")
                                                        .arg(capturedX)
                                                        .arg(capturedY));
                          }
                        });

                QFuture<bool> tileFuture = QtConcurrent::run(
                    [scanRef, capturedX, capturedY, tileProgress]() -> bool {
                      if (!scanRef || !scanRef->stitch)
                        return false;
                      const char *err = nullptr;
                      bool ok = scanRef->stitch->images[capturedY][capturedX]
                                    .load_img(&err, tileProgress.get());
                      return ok;
                    });
                tileWatcher->setFuture(tileFuture);
              }
            }
          }

        } else {
          updateWindowTitle();
          if (progress->cancelled()) {
          } else {
            QMessageBox::critical(this, "Error Loading Image",
                                  result.second.isEmpty()
                                      ? "Failed to load image."
                                      : result.second);
          }
        }
      });

  QString absolutePath = m_currentImageFile;
  QFuture<std::pair<bool, QString>> future = QtConcurrent::run(
      [tempScan, absolutePath, progress, demosaic, isCsprj]() {
        const char *error = nullptr;
        colorscreen::sub_task task(progress.get());
        bool res = tempScan->load(absolutePath.toUtf8().constData(),
                                  /*preload_all=*/!isCsprj, &error,
                                  progress.get(), demosaic);
        QString errStr;
        if (!res && error) {
          errStr = QString::fromUtf8(error);
        }
        return std::make_pair(res, errStr);
      });

  watcher->setFuture(future);
}

/** Load the recent image files list from QSettings on startup.  */
void MainWindow::loadRecentFiles() {
  QSettings settings;
  m_recentFiles = settings.value("recentFiles").toStringList();
  updateRecentFileActions();
}

/** Persist the recent image files list to QSettings.  */
void MainWindow::saveRecentFiles() {
  QSettings settings;
  settings.setValue("recentFiles", m_recentFiles);
}

// Undo/Redo Implementation

/** Apply a full ParameterState to the application.
   Copies all parameter structs (render, scr-to-img, detect, solver,
   profile spots) to member variables, updates ImageWidget and
   NavigationView, refreshes all panels, and rebuilds the mode menu.
   Called by undo/redo commands and by changeParameters().  */
void MainWindow::applyState(const ParameterState &state) {
  const bool invalidateFocusAreas
      = m_scrToImgParams != state.scrToImg
        || !(m_rparams.scan_crop == state.rparams.scan_crop);
  // User requested rotation is not part of parameters.
  // Preserve current rotation when applying state.
  m_rparams = state.rparams;
  m_scrToImgParams = state.scrToImg;
  m_detectParams = state.detect;
  m_solverParams = state.solver; // Manually copy logic if needed? Struct copy
  m_profileSpots = state.profileSpots;
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

/** Return the complete state snapshot consumed by the profile optimizer. */
MainWindow::ColorOptimizerRequestData
MainWindow::profileCalibrationInputs() const {
  return ColorOptimizerRequestData{m_scrToImgParams, m_rparams, m_profileSpots};
}

/** Summarize profile prerequisites, fit freshness, and last accepted quality. */
QString MainWindow::profileCalibrationSummary() const {
  if (!m_scan)
    return tr("Profile: load an image to calibrate");

  const auto capture = m_rparams.get_capture_type(m_scan.get());
  if (!colorscreen::render_parameters::capture_has_screen_p(capture))
    return tr("Profile: not applicable to this capture workflow");
  if (!m_scan->has_rgb())
    return tr("Profile: unavailable — capture has no RGB channels");

  const qsizetype count = static_cast<qsizetype>(m_profileSpots.size());
  const bool savedProfile = m_rparams.has_correction_profile();
  if (count < 4) {
    QString summary = count == 0
        ? tr("Profile: no calibration spots")
        : tr("Profile: %1/4 calibration spots — add %2 more")
              .arg(count)
              .arg(4 - count);
    if (m_profileFitBaseline) {
      summary += tr(" • fitted profile stale");
      if (m_profileFitAverageDeltaE >= 0)
        summary += tr(" • last avg ΔE₂₀₀₀ %1")
                       .arg(m_profileFitAverageDeltaE, 0, 'f', 2);
    } else if (savedProfile) {
      summary += tr(" • saved profile freshness unverified");
    }
    return summary;
  }

  const ColorOptimizerRequestData inputs = profileCalibrationInputs();
  const bool fitCurrent = m_profileFitBaseline && *m_profileFitBaseline == inputs;
  const bool failureCurrent =
      m_profileFitFailureInputs && *m_profileFitFailureInputs == inputs;

  QString summary = tr("Profile: %1 calibration spots").arg(count);
  if (m_profileFitPendingInputs) {
    if (*m_profileFitPendingInputs != inputs)
      summary += tr(" • inputs changed — result will be discarded");
    else
      summary += tr(" • optimizing…");
  } else if (fitCurrent) {
    summary += tr(" • current");
    if (m_profileFitAverageDeltaE >= 0)
      summary += tr(" • avg ΔE₂₀₀₀ %1")
                     .arg(m_profileFitAverageDeltaE, 0, 'f', 2);
    if (failureCurrent)
      summary += tr(" • last refit failed");
  } else if (failureCurrent) {
    summary += tr(" • fit failed — adjust spots/settings and retry");
  } else if (m_profileFitBaseline) {
    summary += tr(" • stale — optimize again");
    if (m_profileFitAverageDeltaE >= 0)
      summary += tr(" • last avg ΔE₂₀₀₀ %1")
                     .arg(m_profileFitAverageDeltaE, 0, 'f', 2);
  } else if (savedProfile) {
    summary += tr(" • saved profile freshness unverified — optimize to validate");
  } else {
    summary += tr(" • ready to optimize");
  }
  return summary;
}

/** Forget optimizer-derived profile provenance for a changed image context. */
void MainWindow::resetProfileCalibrationProvenance() {
  // The match report and freshness markers are derived from one exact image
  // and optimizer-input snapshot.  They must never cross a document/reload
  // boundary even though the fitted profile coefficients themselves persist.
  m_colorOptimizerQueue.cancelAll();
  m_profileFitBaseline.reset();
  m_profileFitPendingInputs.reset();
  m_profileFitFailureInputs.reset();
  m_profileFitAverageDeltaE = -1;
  m_profileSpotResults.clear();
  if (m_profilePanel)
    m_profilePanel->setSpotResults({});
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
      m_mtfFitBaseline && m_mtfFitBaseline->fit_inputs_equal_p(mtf);
  const bool failureCurrent =
      m_mtfFitFailureInputs && m_mtfFitFailureInputs->fit_inputs_equal_p(mtf);
  if (m_mtfFitRunning) {
    if (m_mtfFitPendingInputs && !m_mtfFitPendingInputs->equal_p(mtf))
      summary += tr(" • fit inputs changed — result will be discarded");
    else
      summary += tr(" • fitting model…");
  } else if (fitCurrent) {
    summary += tr(" • model current");
    if (m_mtfFitRms >= 0)
      summary += tr(" • RMS %1 pp").arg(m_mtfFitRms, 0, 'g', 4);
    if (failureCurrent)
      summary += tr(" • last refit failed");
  } else if (failureCurrent) {
    summary += tr(" • fit failed — adjust settings and retry");
  } else if (m_mtfFitBaseline) {
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

bool MainWindow::beginMtfModelFit(const colorscreen::mtf_parameters &inputs) {
  if (m_mtfFitRunning)
    return false;
  m_mtfFitRunning = true;
  m_mtfFitPendingInputs = inputs;
  m_mtfFitFailureInputs.reset();
  refreshMtfCalibrationPresentation();
  return true;
}

void MainWindow::failMtfModelFit(const colorscreen::mtf_parameters &inputs) {
  m_mtfFitRunning = false;
  m_mtfFitPendingInputs.reset();
  m_mtfFitFailureInputs = inputs;
  refreshMtfCalibrationPresentation();
}

void MainWindow::acceptMtfModelFit(const colorscreen::mtf_parameters &fitted,
                                   double rms) {
  m_mtfFitRunning = false;
  m_mtfFitPendingInputs.reset();
  m_mtfFitBaseline = fitted;
  m_mtfFitFailureInputs.reset();
  m_mtfFitRms = rms;
  /* SharpnessPanel applies FITTED immediately after this callback. Defer the
     presentation refresh so it observes the accepted document state rather
     than briefly calling the new baseline stale. */
  QTimer::singleShot(0, this, [this]() { refreshMtfCalibrationPresentation(); });
}

void MainWindow::finishMtfModelFitWithoutResult() {
  m_mtfFitRunning = false;
  m_mtfFitPendingInputs.reset();
  refreshMtfCalibrationPresentation();
}

/** Refresh the persistent workflow summary from document-owned state.

    Geometry fit freshness is session-local rather than serialized into .par:
    an accepted fit captures the exact fit-related state as a baseline. Later
    edits therefore remain visible while being labelled stale. A running fit
    whose inputs changed is cancelled before it can publish an obsolete result. */
void MainWindow::updateWorkflowSummary() {
  if (!m_workflowProcessLabel || !m_workflowRegistrationLabel ||
      !m_workflowCalibrationLabel || !m_workflowNextStepLabel)
    return;

  const ParameterState currentState = getCurrentState();
  const bool pendingMeshModeChanged =
      m_geometryFitPendingComputeMesh && m_geometryPanel &&
      *m_geometryFitPendingComputeMesh != m_geometryPanel->isNonlinearEnabled();
  if (m_geometryFitPendingInputs &&
      (geometryFitInputsDiffer(*m_geometryFitPendingInputs, currentState) ||
       pendingMeshModeChanged)) {
    // Reset first because cancelAll() may synchronously trigger progress/UI
    // callbacks that refresh this summary again.
    m_geometryFitPendingInputs.reset();
    m_geometryFitPendingComputeMesh.reset();
    m_solverQueue.cancelAll();
  }

  if (m_profileFitPendingInputs &&
      *m_profileFitPendingInputs != profileCalibrationInputs()) {
    // As with geometry, invalidate the domain snapshot before cancellation so
    // a synchronous progress refresh cannot mistake the obsolete fit for live.
    m_profileFitPendingInputs.reset();
    m_colorOptimizerQueue.cancelAll();
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
  const bool stochasticScreen = colorscreen::stochastic_screen_p(type);

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

  if (capture == colorscreen::render_parameters::capture_unknown) {
    m_workflowProcessLabel->setText(
        tr("Process: capture type unknown — choose it in Digital capture"));
  } else if (!hasScreen) {
    m_workflowProcessLabel->setText(
        tr("Process: %1 — no historical color-screen reconstruction")
            .arg(captureName));
  } else {
    m_workflowProcessLabel->setText(
        tr("Process: %1 • screen: %2").arg(captureName, screenName));
  }

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
    if (stochasticScreen) {
      registration = tr(
          "Registration: geometry not used — reconstruct from detected "
          "screen colours");
    } else {
      registration = tr("Registration: choose a screen type");
    }
  } else {
    minimumPoints = colorscreen::solver_parameters::min_points(type);
    const QString prefix = tr("Registration: optional geometry path");
    if (pointCount == 0) {
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
      fitCurrent = m_geometryFitBaseline &&
          !geometryFitInputsDiffer(*m_geometryFitBaseline, currentState);
      failureCurrent = m_geometryFitFailureInputs &&
          !geometryFitInputsDiffer(*m_geometryFitFailureInputs, currentState);
      if (m_geometryFitPendingInputs) {
        registration += tr(" • fitting geometry…");
      } else if (fitCurrent) {
        registration += tr(" • geometry current");
        if (failureCurrent)
          registration += tr(" • last refit failed");
      } else if (failureCurrent) {
        registration += tr(" • fit failed — adjust points/settings and retry");
      } else if (m_geometryFitBaseline) {
        registration += tr(" • geometry stale — refit");
      } else {
        registration += tr(" • ready to fit/validate geometry");
      }
      if (m_scrToImgParams.mesh_trans)
        registration += tr(" • nonlinear correction present");
    }
  }
  m_workflowRegistrationLabel->setText(registration);

  const auto &mtf = m_rparams.sharpen.scanner_mtf;
  QStringList missingLensData;
  if (!colorscreen::my_isfinite(mtf.pixel_pitch) || mtf.pixel_pitch <= 0)
    missingLensData << tr("pixel pitch");
  if (!colorscreen::my_isfinite(mtf.f_stop) || mtf.f_stop <= 0)
    missingLensData << tr("f-stop");
  if (!colorscreen::my_isfinite(mtf.scan_dpi) || mtf.scan_dpi <= 0)
    missingLensData << tr("resolution");
  const QString sharpenSummary = missingLensData.isEmpty()
      ? tr("Sharpening: lens model ready")
      : tr("Sharpening: lens model needs %1")
            .arg(missingLensData.join(", "));

  // Preserve #251's richer calibration/provenance summary rather than
  // collapsing it back to a saved-measurement count.
  const QString mtfSummary = mtfCalibrationSummary();

  const QString profileSummary = profileCalibrationSummary();
  if (m_profilePanel)
    m_profilePanel->setCalibrationStatus(profileSummary);
  m_workflowCalibrationLabel->setText(
      sharpenSummary + QStringLiteral(" • ") + mtfSummary +
      QStringLiteral(" • ") + profileSummary);

  QString nextStep;
  if (!m_scan) {
    nextStep = tr("Next: load an image.");
  } else if (capture == colorscreen::render_parameters::capture_unknown) {
    nextStep = tr("Next: choose Capture type in Digital capture.");
  } else if (colorscreen::render_parameters::capture_negative_p(capture) &&
             !m_rparams.contact_copy.simulate) {
    nextStep = tr(
        "Next: Simulated darkroom — enable Contact copy simulation to turn "
        "the negative into a positive.");
  } else if (!hasScreen) {
    nextStep = tr(
        "Next: set capture correction, black/backlight and sharpening, then "
        "render the corrected capture. Contact copy remains available for "
        "negative or photolab simulation.");
  } else if (colorDetection && stochasticScreen) {
    nextStep = tr(
        "Next: reconstruct from detected screen colours; stochastic screens "
        "do not use Geometry.");
  } else if (colorDetection && regularScreen) {
    nextStep = tr(
        "Next: choose either Geometry-based reconstruction or screen-colour "
        "detection from the RGB scan.");
  } else if (hasScreen && type == colorscreen::NoScreen) {
    nextStep = tr("Next: choose the physical Screen type.");
  } else {
    nextStep = tr("Next: reconstruct the image and refine Color/Profile.");
  }
  m_workflowNextStepLabel->setText(nextStep);
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
    m_mirrorAction->blockSignals(true);
    m_mirrorAction->setChecked(finalCoordinates ? state.scrToImg.final_mirror
                                                : state.rparams.scan_mirror);
    m_mirrorAction->blockSignals(false);
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
                                          const QString &description) {
  changeParameters(state, description);
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
   The DESCRIPTION string appears in the Edit > Undo/Redo menu text.
   Successive calls with the same DESCRIPTION within 500 ms are merged by the
   command's mergeWith() into a single undo step.  */
void MainWindow::changeParameters(const ParameterState &newState,
                                  const QString &description) {
  ParameterState currentState = getCurrentState();
  if (currentState == newState)
    return;

  m_undoStack->push(
      new ChangeParametersCommand(this, currentState, newState, description));
}

/** Return whether this document has parameters not represented by its saved
    .par file.  Recovered state remains dirty even though the reconstructed undo
    stack starts empty.  */
bool MainWindow::isDocumentModified() const {
  return m_recoveryDirty || (m_undoStack && !m_undoStack->isClean());
}

/** Return whether a new image may safely reuse this document window. */
bool MainWindow::canReuseForOpen() const {
  return !m_closing && !m_scan && !m_imageLoadPending &&
         m_currentImageFile.isEmpty() && !isDocumentModified();
}

/** Return this document's concise name for the application Window menu. */
QString MainWindow::documentDisplayName() const {
  QString name = m_currentImageFile.isEmpty()
                     ? tr("Untitled")
                     : QFileInfo(m_currentImageFile).fileName();
  if (isDocumentModified())
    name += QLatin1Char('*');
  return name;
}

/** Check for unsaved parameter changes and prompt the user.
   Returns true only after a successful synchronous save, an explicit discard,
   or when this document has no unsaved state.  */
bool MainWindow::maybeSave() {
  if (!isDocumentModified())
    return true;

  QString displayName = documentDisplayName();
  displayName.remove(QLatin1Char('*'));
  const QMessageBox::StandardButton result = QMessageBox::warning(
      this, "Unsaved Changes",
      QString("Parameters for %1 have been modified.\n"
              "Do you want to save your changes?")
          .arg(displayName),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

  switch (result) {
  case QMessageBox::Save:
    if (m_currentParamsFile.isEmpty() || m_currentParamsFileIsWeak)
      return saveParametersAs();
    return saveParametersToFile(m_currentParamsFile);
  case QMessageBox::Discard:
    return true;
  case QMessageBox::Cancel:
  default:
    return false;
  }
}

/** Ask all user-visible questions that may veto destroying this document. */
bool MainWindow::confirmClose() {
  if (!maybeSave())
    return false;

  if (!m_renderProgress.expired()) {
    const auto result = QMessageBox::question(
        this, tr("Rendering in Progress"),
        tr("A render is currently in progress. Cancel it and close this window?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes)
      return false;
  }
  return true;
}

/** Preflight final application closure without tearing down this document. */
bool MainWindow::prepareForApplicationClose() {
  if (m_closing || m_applicationClosePrepared)
    return true;
  if (!confirmClose())
    return false;
  m_applicationClosePrepared = true;
  return true;
}

/** Forget a preflight approval when another document vetoes File -> Exit. */
void MainWindow::cancelPreparedApplicationClose() {
  m_applicationClosePrepared = false;
}

/** Handle closing one image-document window.
   Prompts for this document's unsaved changes, asks to cancel its active
   render, cancels only its background tasks, removes only its recovery data,
   and saves the shared preferred window layout.  */
void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_closing) {
    event->accept();
    return;
  }

  // The primary MainWindow is also the document state owner.  Closing this
  // presentation must not destroy that owner while peer ImageViewWindows still
  // need its parameters, undo stack, workers, and recovery state.
  if (ColorScreenApplication *application = documentApplication()) {
    if (application->retainDocumentForOpenViews(this)) {
      event->ignore();
      return;
    }
  }

  if (m_applicationClosePrepared) {
    // File -> Exit already resolved every user-visible veto before it started
    // tearing down secondary views.  Consume the one-shot approval here.
    m_applicationClosePrepared = false;
  } else if (!confirmClose()) {
    event->ignore();
    return;
  }

  m_closing = true;

  // Cancel all active processes
  for (const auto &progress : m_activeProgresses) {
    if (progress.info) {
      progress.info->cancel();
    }
  }

  // Clean up recovery files on normal exit
  clearRecoveryFiles();

  // Return workspace-owned presentation widgets before saving state or
  // destroying this document.  This keeps all document UI owned by the
  // MainWindow during teardown and lets the workspace select the next image.
  if (ColorScreenApplication *application = documentApplication())
    application->prepareDocumentForClose(this);

  // Recent lists are persisted at the point of each change.  Saving a stale
  // per-window copy here would let the last closed document overwrite entries
  // added by another open window.
  saveWindowState();
  event->accept();
}

/** Update the title and standard Qt modification marker for this document. */
void MainWindow::updateWindowTitle() {
  QString title = QStringLiteral("Color-Screen " PACKAGE_VERSION ": ") +
                  (m_currentImageFile.isEmpty()
                       ? tr("Untitled")
                       : QFileInfo(m_currentImageFile).fileName());
  setWindowModified(isDocumentModified());
  setWindowTitle(title + QStringLiteral("[*]"));
  if (ColorScreenApplication *application = documentApplication())
    application->refreshDocumentPresentation(this);
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


/** Save window geometry, state, splitter positions, and current desktop
   size to QSettings for restoration on next launch.  */
void MainWindow::saveWindowState() {
  QSettings settings;

  // The primary workspace owns tabbed-window geometry.  Save document-window
  // geometry only while this document is detached; dock state is useful in
  // either presentation.
  if (isWindow()) {
    settings.setValue("windowGeometry", saveGeometry());
    if (QScreen *screen = QApplication::primaryScreen())
      settings.setValue("desktopSize", screen->availableGeometry().size());
  }
  settings.setValue("windowState", saveState());

  // Save splitter positions
  if (m_mainSplitter) {
    settings.setValue("mainSplitterState", m_mainSplitter->saveState());
  }
}

/** Restore window geometry and splitter positions from QSettings.
   Validates that the desktop size hasn't changed significantly (>100 px)
   since the layout was saved; if it has, falls back to a default size.
   After restoring, fixes any docks that were saved as visible but have
   no widget content (they would appear as empty floating windows).  */
void MainWindow::restoreWindowState() {
  QSettings settings;

  // Check if desktop size has changed
  bool desktopSizeValid = true;
  QScreen *screen = QApplication::primaryScreen();
  if (screen) {
    QSize savedDesktopSize = settings.value("desktopSize").toSize();
    QSize currentDesktopSize = screen->availableGeometry().size();

    // Allow some tolerance (e.g., taskbar changes)
    if (savedDesktopSize.isValid()) {
      int widthDiff =
          qAbs(savedDesktopSize.width() - currentDesktopSize.width());
      int heightDiff =
          qAbs(savedDesktopSize.height() - currentDesktopSize.height());

      // If desktop size changed significantly (more than 100 pixels), don't
      // restore
      if (widthDiff > 100 || heightDiff > 100) {
        desktopSizeValid = false;
      }
    }
  }

  // Restore window geometry if desktop size is compatible
  if (desktopSizeValid && settings.contains("windowGeometry")) {
    restoreGeometry(settings.value("windowGeometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
  } else {
    // Default size and position
    resize(1200, 800);
    // Center on screen
    if (screen) {
      QRect screenGeometry = screen->availableGeometry();
      move(screenGeometry.center() - rect().center());
    }
  }

  // Restore splitter state (always try this, it's safe)
  if (m_mainSplitter && settings.contains("mainSplitterState")) {
    m_mainSplitter->restoreState(
        settings.value("mainSplitterState").toByteArray());
  }
}

// Recent Parameters Implementation

/** Add a file path to the most-recently-used parameter files list.
   Same pattern as addToRecentFiles.  */
void MainWindow::addToRecentParams(const QString &filePath) {
  // Parameter saves and loads can finish in different document windows; start
  // from QSettings so the most recent writer merges rather than overwrites.
  QSettings settings;
  m_recentParams = settings.value("recentParams").toStringList();
  QString absolutePath = QFileInfo(filePath).absoluteFilePath();
  m_recentParams.removeAll(absolutePath);
  m_recentParams.prepend(absolutePath);

  while (m_recentParams.size() > MaxRecentFiles)
    m_recentParams.removeLast();

  settings.setValue("recentParams", m_recentParams);
  updateRecentParamsActions();
}

/** Rebuild the "Open Recent Parameters" submenu.  */
void MainWindow::updateRecentParamsActions() {
  m_recentParamsMenu->clear();
  m_recentParamsActions.clear();

  for (int i = 0; i < m_recentParams.size(); ++i) {
    QString fileName = QFileInfo(m_recentParams[i]).fileName();
    fileName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QString text = tr("&%1 %2").arg(i + 1).arg(fileName);
    QAction *action = m_recentParamsMenu->addAction(
        text, this, &MainWindow::openRecentParams);
    action->setData(m_recentParams[i]);
    action->setToolTip(m_recentParams[i]);
    m_recentParamsActions.append(action);
  }

  if (m_recentParams.isEmpty()) {
    m_recentParamsMenu->addAction("No Recent Parameters")->setEnabled(false);
  } else {
    m_recentParamsMenu->addSeparator();
    QAction *clearAction =
        m_recentParamsMenu->addAction("Clear Recent Parameters");
    connect(clearAction, &QAction::triggered, this, [this]() {
      m_recentParams.clear();
      updateRecentParamsActions();
      saveRecentParams();
    });
  }
}

/** Slot invoked when a recent parameter menu item is clicked.
   Loads the .par file (reset + merge), updates the renderer and UI,
   syncs gamut warning state, and clears undo history.  */
void MainWindow::openRecentParams() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action)
    return;

  // maybeSave() can rebuild this submenu and delete ACTION, so capture its
  // payload before opening a nested save dialog.
  const QString fileName = action->data().toString();
  if (maybeSave()) {
    if (loadParameterFile(fileName)) {
      statusBar()->showMessage(QString("Parameters loaded from %1").arg(fileName),
                               3000);
    }
  }
}

/** Load the recent parameter files list from QSettings on startup.  */
void MainWindow::loadRecentParams() {
  QSettings settings;
  m_recentParams = settings.value("recentParams").toStringList();
  updateRecentParamsActions();
}

/** Persist the recent parameter files list to QSettings.  */
void MainWindow::saveRecentParams() {
  QSettings settings;
  settings.setValue("recentParams", m_recentParams);
}

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
void MainWindow::onOptimizeGeometry(bool autoChecked) {
  if (!m_scan || !m_solverWorker)
    return;

  SolverRequestData data;
  data.scrToImg = m_scrToImgParams;
  data.solver = m_solverParams;
  data.computeMesh = m_geometryPanel->isNonlinearEnabled();

  // The pending snapshot is both user-visible provenance and a second stale
  // result gate beyond TaskQueue's newest-request check. It catches edits to
  // points/geometry and the requested nonlinear mode while this solve runs.
  m_geometryFitPendingInputs = getCurrentState();
  m_geometryFitPendingComputeMesh = data.computeMesh;
  m_geometryFitFailureInputs.reset();
  updateWorkflowSummary();

  // Request new solve task with captured data
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
  if (!m_scan || !m_solverWorker || !userData.canConvert<SolverRequestData>()) {
    m_solverQueue.reportFinished(reqId, false);
    m_geometryFitPendingInputs.reset();
    m_geometryFitPendingComputeMesh.reset();
    updateWorkflowSummary();
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
  const bool current = m_solverQueue.reportFinished(reqId, success);
  if (!current || m_closing)
    return;

  const ParameterState now = getCurrentState();
  const bool meshModeStillCurrent =
      m_geometryFitPendingComputeMesh && m_geometryPanel &&
      *m_geometryFitPendingComputeMesh == m_geometryPanel->isNonlinearEnabled();
  const bool inputsStillCurrent =
      m_geometryFitPendingInputs && meshModeStillCurrent &&
      !geometryFitInputsDiffer(*m_geometryFitPendingInputs, now);
  m_geometryFitPendingInputs.reset();
  m_geometryFitPendingComputeMesh.reset();

  if (cancelled || !inputsStillCurrent) {
    updateWorkflowSummary();
    return;
  }

  if (success) {
    ParameterState newState = now;
    newState.scrToImg.merge_solver_solution(result);
    changeParameters(newState, "Optimize Geometry");
    m_geometryFitBaseline = getCurrentState();
    m_geometryFitFailureInputs.reset();
  } else {
    m_geometryFitFailureInputs = now;
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

  m_colorCheckBox->blockSignals(true);
  if (!hasRgb) {
    m_colorCheckBox->setChecked(false);
  } else {
    m_colorCheckBox->setChecked(m_renderTypeParams.color);
  }
  m_colorCheckBox->blockSignals(false);
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
                    hasScreenCapture && m_scan && m_scan->has_rgb());
  }

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

/** Auto-save this document into its private crash-recovery directory.
   Called by a 30-second timer and immediately after image load/save.  The
   payload contains the image path, complete parameters, and current parameter
   filename metadata.  */
void MainWindow::saveRecoveryState() {
  if (!m_scan || m_recoveryDir.isEmpty())
    return;
  if (!QDir().mkpath(m_recoveryDir))
    return;

  const QDir directory(m_recoveryDir);
  QFile imageFile(directory.filePath(QStringLiteral("recovery_image.txt")));
  if (imageFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&imageFile);
    out << m_currentImageFile;
  }

  const QString paramsPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  bool paramsSaved = false;
  FILE *f = fopen(paramsPath.toUtf8().constData(), "wt");
  if (f) {
    const bool hasRgb = m_scan->has_rgb();
    paramsSaved = colorscreen::save_csp_with_profile_spots(
        f, &m_scrToImgParams, hasRgb ? &m_detectParams : nullptr, &m_rparams,
        &m_solverParams, m_profileSpots);
    if (fclose(f) != 0)
      paramsSaved = false;
  }
  if (!paramsSaved)
    QFile::remove(paramsPath);

  QFile metaFile(
      directory.filePath(QStringLiteral("recovery_params_meta.txt")));
  if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&metaFile);
    out << m_currentParamsFile << '\n';
    out << (m_currentParamsFileIsWeak ? "1" : "0") << '\n';
    out << (isDocumentModified() ? "1" : "0") << '\n';
  }
}

/** Restore this document from its private recovery payload.
   Restores the saved dirty flag when present; legacy payloads are treated as
   modified because they may contain edits never written to the user's .par
   file. Returns false only when the directory contains no usable data.  */
bool MainWindow::restoreRecoveryState() {
  if (m_recoveryDir.isEmpty())
    return false;

  const QDir directory(m_recoveryDir);
  const QString imagePath =
      directory.filePath(QStringLiteral("recovery_image.txt"));
  const QString paramsPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  if (!QFile::exists(imagePath) && !QFile::exists(paramsPath))
    return false;

  QString imageToLoad;
  QFile imageFile(imagePath);
  if (imageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&imageFile);
    imageToLoad = in.readLine().trimmed();
  }

  if (QFile::exists(paramsPath)) {
    FILE *f = fopen(paramsPath.toUtf8().constData(), "r");
    if (f) {
      const char *error = nullptr;
      const bool loaded = colorscreen::load_csp_with_profile_spots(
          f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams,
          &error, &m_profileSpots, &m_profileSpotResults);
      fclose(f);
      if (!loaded || error) {
        QMessageBox::warning(
            this, "Recovery Warning",
            error ? QString("Error loading parameters: %1").arg(error)
                  : QStringLiteral("Could not load recovered parameters."));
      } else {
        m_prevScrToImgParams = m_scrToImgParams;
        m_prevDetectParams = m_detectParams;
      }
    }
  }

  m_recoveryDirty = true; // Legacy recovery metadata has no dirty flag.
  QFile metaFile(
      directory.filePath(QStringLiteral("recovery_params_meta.txt")));
  if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&metaFile);
    m_currentParamsFile = in.readLine().trimmed();
    m_currentParamsFileIsWeak = (in.readLine().trimmed() == QLatin1String("1"));
    const QString dirtyFlag = in.readLine().trimmed();
    if (!dirtyFlag.isEmpty())
      m_recoveryDirty = (dirtyFlag == QLatin1String("1"));
  }

  if (!imageToLoad.isEmpty() && QFile::exists(imageToLoad)) {
    loadFile(imageToLoad, true);
  } else if (!imageToLoad.isEmpty()) {
    QMessageBox::warning(
        this, "Recovery Warning",
        QString("Could not find image file: %1").arg(imageToLoad));
  }

  updateUIFromState(getCurrentState());
  updateWindowTitle();
  return true;
}

/** Delete only this document's recovery directory after a clean close. */
void MainWindow::clearRecoveryFiles() {
  if (!m_recoveryDir.isEmpty())
    QDir(m_recoveryDir).removeRecursively();
}
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
  map.set_parameters(m_scrToImgParams, *m_scan);

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

/** Update enabled state of all registration-related menu actions.
   Enables select/delete/prune based on current selection, enables
   optimize based on minimum point count for the current screen type,
   and calls GeometryPanel::updateRegistrationPointInfo() to refresh
   the panel's status display.  */
void MainWindow::updateRegistrationActions() {
  ImageWidget *image = inspectorImageWidget();
  bool hasPoints = image && image->registrationPointsVisible() &&
                   image->registrationPointCount() > 0;
  bool hasSelection = image && !image->selectedPoints().empty();

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
    m_pruneMisplacedAction->setEnabled(hasSelection);
  }

  // Add Point and Set Center need a loaded image and a regular screen lattice.
  if (m_addPointAction) {
    bool canAddPoints = m_scan && colorscreen::screen_has_regular_geometry_p(
                                     m_scrToImgParams.type);
    m_addPointAction->setEnabled(canAddPoints);
    // If tool is active but we can't add points, switch to Pan mode
    if (!canAddPoints && m_addPointAction->isChecked()) {
      m_panAction->setChecked(true);
    }
  }
  if (m_setCenterAction) {
    bool canSetCenter = m_scan && colorscreen::screen_has_regular_geometry_p(
                                     m_scrToImgParams.type);
    m_setCenterAction->setEnabled(canSetCenter);
    // If tool is active but we can't set center, switch to Pan mode
    if (!canSetCenter && m_setCenterAction->isChecked()) {
      m_panAction->setChecked(true);
    }
  }

  // Disable/enable optimize geometry and select all based on point count
  size_t count = image ? image->registrationPointCount() : 0;
  int min_points = colorscreen::solver_parameters::min_points(m_scrToImgParams.type);
  if (m_selectAllAction) {
    m_selectAllAction->setEnabled(count > 0);
  }
  if (m_optimizeGeometryAction) {
    m_optimizeGeometryAction->setEnabled(count >= (size_t)min_points);
  }

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
  ParameterState newState = getCurrentState();
  if (newState != m_undoSnapshot) {
    m_undoStack->push(new ChangeParametersCommand(
        this, m_undoSnapshot, newState, "Move registration point"));
    m_undoSnapshot = newState;
  }

  if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
    size_t count = m_imageWidget->registrationPointCount();
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

  // Profile spot mode: convert img coords → screen coords and store
  if (m_addingProfileSpot) {
    colorscreen::scr_to_img map;
    map.set_parameters(m_scrToImgParams, *m_scan);
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

    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task("Focus analysis", 0);
    addProgress(progress);

    const uint64_t generation = ++m_focusAnalysisGeneration;
    FocusAnalysisWorker *worker = new FocusAnalysisWorker(
        m_rparams, m_scrToImgParams, m_scan, imgPos, fparam, progress);
    QThread *thread = new QThread(this);
    worker->moveToThread(thread);
    trackBackgroundThread(thread);

    connect(thread, &QThread::started, worker, &FocusAnalysisWorker::run);
    connect(worker, &FocusAnalysisWorker::finished, thread, &QThread::quit,
          Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &FocusAnalysisWorker::finished, this,
            [this, progress, generation](bool success,
                                         colorscreen::finetune_result result) {
              const bool cancelled = progress && progress->pool_cancel();
              if (!m_closing && generation == m_focusAnalysisGeneration) {
                if (!cancelled)
                  onFocusAnalysisFinished(success, result);
                else if (m_sharpnessPanel)
                  m_sharpnessPanel->setFocusAnalysisChecked(false);
              }
              if (!m_closing)
                removeProgress(progress);
            });

    thread->start();
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

    // Update the image widget
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    m_imageWidget->update();

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
      size_t count = m_imageWidget->registrationPointCount();
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
  statusBar()->showMessage("Select crop");
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
  statusBar()->showMessage(message);
}

/** Launch an area-based parameter computation.
   Enters area selection mode with MESSAGE in the status bar.  When the user
   draws a rectangle, calls ON_START, creates a progress tracker, runs WORKER
   in a background thread, then pushes the modified state as an undoable change
   with DESCRIPTION.  Calls ON_DONE on completion regardless of success.  */
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
    if (area.width() <= 0 || area.height() <= 0)
      return;

    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task(description.toUtf8().constData(), 1);
    colorscreen::sub_task task(progress.get());
    addProgress(progress);
    if (onStart)
      onStart();

    auto scan = m_scan;
    auto state = getCurrentState();

    QFutureWatcher<ParameterState> *watcher =
        new QFutureWatcher<ParameterState>(this);
    connect(watcher, &QFutureWatcher<ParameterState>::finished, this,
            [this, watcher, progress, description, onDone]() {
              ParameterState newState = watcher->result();
              const bool cancelled = progress && progress->pool_cancel();
              if (!m_closing && !cancelled)
                changeParameters(newState, description);
              if (!m_closing)
                removeProgress(progress);
              if (!m_closing && onDone)
                onDone();
              watcher->deleteLater();
            });

    QFuture<ParameterState> future = QtConcurrent::run(
        [scan, state, area, progress, worker]() mutable -> ParameterState {
          worker(state, *scan,
                 {area.x(), area.y(), area.width(), area.height()},
                 progress.get());
          return state;
        });
    watcher->setFuture(future);
  });
}

/** Load parameters from a .par file and update all UI components.
   Resets parameters to defaults before loading (as load_csp merges into
   existing values).  Updates ImageWidget, NavigationView, gamut warning,
   undo history, and all panels.  Returns true on success.  */
bool MainWindow::loadParameterFile(const QString &fileName) {
  FILE *f = fopen(fileName.toUtf8().constData(), "r");
  if (!f) {
    QMessageBox::critical(this, "Error", "Could not open file.");
    return false;
  }

  const char *error = nullptr;

  // Store previous state in case load fails.  Profile match results are
  // derived UI state rather than part of ParameterState, but failed loads must
  // preserve those too.
  const ParameterState oldState = getCurrentState();
  const std::vector<colorscreen::color_match> oldProfileSpotResults =
      m_profileSpotResults;

  // load_csp merges parameters in; reset first to ensure clean load.
  m_scrToImgParams = colorscreen::scr_to_img_parameters();
  m_detectParams = colorscreen::scr_detect_parameters();
  m_rparams = colorscreen::render_parameters();
  m_solverParams = colorscreen::solver_parameters();

  if (!colorscreen::load_csp_with_profile_spots(
          f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams,
          &error, &m_profileSpots, &m_profileSpotResults)) {
    fclose(f);
    QString errStr =
        error ? QString::fromUtf8(error) : "Unknown error loading parameters.";
    QMessageBox::critical(this, "Error Loading Parameters", errStr);

    // Restore previous state
    m_scrToImgParams = oldState.scrToImg;
    m_detectParams = oldState.detect;
    m_rparams = oldState.rparams;
    m_solverParams = oldState.solver;
    m_profileSpots = oldState.profileSpots;
    m_profileSpotResults = oldProfileSpotResults;
    return false;
  }
  fclose(f);

  // Successful external parameter load establishes a new calibration context.
  resetProfileCalibrationProvenance();
  m_geometryFitBaseline.reset();
  m_geometryFitPendingInputs.reset();
  m_geometryFitPendingComputeMesh.reset();
  m_geometryFitFailureInputs.reset();
  m_mtfFitBaseline.reset();
  m_mtfFitPendingInputs.reset();
  m_mtfFitFailureInputs.reset();
  m_mtfFitRms = -1;
  m_mtfFitRunning = false;

  // Update UI/Renderer
  if (m_scan) {
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                            &m_detectParams, &m_renderTypeParams,
                            &m_solverParams);
    m_imageWidget->setProfileSpots(&m_profileSpots, &m_profileSpotResults);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    updateColorCheckBoxState();
  }

  // Sync Gamut Warning Button
  if (m_gamutWarningAction) {
    QSignalBlocker blocker(m_gamutWarningAction);
    m_gamutWarningAction->setChecked(m_rparams.gamut_warning);
  }

  if (m_undoStack)
    m_undoStack->clear();
  m_recoveryDirty = false;

  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  m_currentParamsFile = absoluteFileName;
  m_currentParamsFileIsWeak = false;

  updateModeMenu();
  updateUIFromState(getCurrentState());
  addToRecentParams(absoluteFileName);
  updateWindowTitle();
  saveRecoveryState();

  return true;
}

/** Convert a widget-space rectangle to an image-space rectangle.
   Maps all four corners through ImageWidget::widgetToImage (which
   accounts for rotation, zoom, and pan), finds the axis-aligned
   bounding box, and clamps it to the scan dimensions.  */
QRect MainWindow::getImageArea(QRect area, ImageWidget *imageWidget) {
  ImageWidget *image = imageWidget ? imageWidget : inspectorImageWidget();
  if (!m_scan || !image)
    return QRect();

  // Convert widget coordinates to image coordinates
  // Get the four corners and find min/max
  colorscreen::point_t p1 = image->widgetToImage(area.topLeft());
  colorscreen::point_t p2 = image->widgetToImage(area.topRight());
  colorscreen::point_t p3 = image->widgetToImage(area.bottomLeft());
  colorscreen::point_t p4 = image->widgetToImage(area.bottomRight());

  // Find bounding box in image coordinates
  int xmin = std::min({p1.x, p2.x, p3.x, p4.x});
  int xmax = std::max({p1.x, p2.x, p3.x, p4.x});
  int ymin = std::min({p1.y, p2.y, p3.y, p4.y});
  int ymax = std::max({p1.y, p2.y, p3.y, p4.y});

  // Clamp to image bounds
  xmin = std::max(0, xmin);
  ymin = std::max(0, ymin);
  xmax = std::min((int)m_scan->width - 1, xmax);
  ymax = std::min((int)m_scan->height - 1, ymax);

  return QRect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
}

/** Dispatch a drawn rectangle to the appropriate handler based on the
   current interaction mode.
   - GenericAreaMode: invokes the m_areaSelectionCallback and restores
     the previous tool.
   - CropMode: sets the crop rectangle in the parameter state.
   - SelectMode/AddPointMode: launches a FinetuneWorker to find
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

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Finding registration points", 1);
  colorscreen::sub_task task(progress.get()); /* Keep so tasks are nested.  */
  addProgress(progress);

  // Create worker and thread
  FinetuneWorker *worker = new FinetuneWorker(
      m_solverParams, m_rparams, m_scrToImgParams, m_scan,
      {imgArea.x(), imgArea.y(), imgArea.width(), imgArea.height()}, progress,
      m_geometryPanel->finetuneAreaParams());
  QThread *thread = new QThread(this);
  worker->moveToThread(thread);
  trackBackgroundThread(thread);

  // Connect signals
  connect(thread, &QThread::started, worker, &FinetuneWorker::run);
  connect(worker, &FinetuneWorker::finished, thread, &QThread::quit,
          Qt::DirectConnection);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);

  // Connect to our slot to handle results
  connect(
      worker, &FinetuneWorker::pointsReady, this,
      [this, thread, progress](
          std::vector<colorscreen::solver_parameters::solver_point_t> points) {
        onFinetuneFinished(true, points, thread, progress);
      });
  connect(worker, &FinetuneWorker::finished, this,
          [this, thread, progress](bool success) {
            if (!success) {
              onFinetuneFinished(false, {}, thread, progress);
            }
          });

  // Start thread
  thread->start();
}

/** Swap the color assignments of registration points.
   Delegates to scr_to_img_parameters::alternate_colors() which cycles
   through colour channel interpretations.  Pushes an undo command.  */
void MainWindow::onAlternateColorsRequested() {
  ParameterState state = getCurrentState();
  state.scrToImg.alternate_colors(state.solver);
  changeParameters(state, tr("Alternate colors"));
}

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

  startAreaSelection(
      tr("Select area to add points"), [this, params](QRect area) {
        if (area.width() <= 0 || area.height() <= 0)
          return;

        const colorscreen::int_image_area crop = {
            area.x(), area.y(), area.width(), area.height()};

        auto progress = std::make_shared<colorscreen::progress_info>();
        progress->set_task("Finding missing registration points", 1);
        colorscreen::sub_task task(progress.get());
        addUserVisibleProgress(
            progress, tr("Automatically add points to area"),
            ProgressAction::Stop);

        auto *worker = new FinetuneMisregisteredWorker(
            m_solverParams, m_rparams, m_scrToImgParams, m_scan, crop, progress,
            params, m_geometryPanel->isNonlinearEnabled());
        auto *thread = new QThread(this);
        worker->moveToThread(thread);
        trackBackgroundThread(thread);

        connect(thread, &QThread::started, worker,
                &FinetuneMisregisteredWorker::run);
        connect(worker, &FinetuneMisregisteredWorker::finished, thread,
                &QThread::quit, Qt::DirectConnection);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        // Points and geometry are intentionally applied in batches while the
        // operation is running.  Stopping therefore keeps everything already
        // visible instead of rolling the document back.
        connect(
            worker, &FinetuneMisregisteredWorker::pointsReady, this,
            [this, progress](
                std::vector<colorscreen::solver_parameters::solver_point_t>
                    points) {
              if (m_closing || (progress && progress->pool_cancel()))
                return;
              if (points.empty())
                return;

              ParameterState oldState = getCurrentState();
              for (const auto &point : points)
                m_solverParams.add_or_modify_point(point.img, point.scr,
                                                   point.color);
              m_imageWidget->updateParameters(
                  &m_rparams, &m_scrToImgParams, &m_detectParams,
                  &m_renderTypeParams, &m_solverParams);
              m_imageWidget->update();
              ParameterState newState = getCurrentState();
              m_undoStack->push(new ChangeParametersCommand(
                  this, oldState, newState, "Add registration points"));
              updateRegistrationActions();
            });
        connect(worker, &FinetuneMisregisteredWorker::geometryReady, this,
                [this, progress](colorscreen::scr_to_img_parameters result) {
                  if (m_closing || (progress && progress->pool_cancel()))
                    return;
                  ParameterState newState = getCurrentState();
                  newState.scrToImg.merge_solver_solution(result);
                  changeParameters(
                      newState,
                      "Automatically add points to area (Geometry update)");
                  m_geometryFitBaseline = getCurrentState();
                  m_geometryFitFailureInputs.reset();
                  updateWorkflowSummary();
                });
        connect(worker, &FinetuneMisregisteredWorker::requestCurrentPoints,
                this,
                [this](std::vector<colorscreen::solver_parameters::solver_point_t>
                           *points) {
                  if (points)
                    *points = m_closing
                                  ? std::vector<colorscreen::solver_parameters::solver_point_t>()
                                  : m_solverParams.points;
                },
                Qt::BlockingQueuedConnection);
        connect(worker, &FinetuneMisregisteredWorker::finished, this,
                [this, progress](bool success) {
                  if (!m_closing)
                    removeProgress(progress);

                  if (!m_closing && !success &&
                      (!progress || !progress->pool_cancel())) {
                    QMessageBox::warning(
                        this, tr("Optimization Failed"),
                        tr("Automatically add points to area failed."));
                  }
                });

        thread->start();
      });
}

/** Handle request to automatically add registration points across the
   entire cropped image area.  Similar to the area-restricted variant
   but uses the full scan crop as the search region.
   The worker reports results incrementally: pointsReady batches add
   new registration points; geometryReady updates trigger immediate
   re-optimisation.  The requestCurrentPoints signal uses a blocking
   queued connection so the worker can read the latest point set from
   the main thread.  */
void MainWindow::onAutomaticallyAddPointsRequested(const colorscreen::finetune_area_parameters &params) {
  if (!m_scan) {
    return;
  }

  // Get current scan crop
  colorscreen::int_image_area crop =
      m_rparams.get_scan_crop(m_scan->width, m_scan->height);

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Finding missing registration points", 1);
  colorscreen::sub_task task(progress.get());
  addUserVisibleProgress(progress, tr("Automatically add points"),
                         ProgressAction::Stop);

  // Create worker and thread
  FinetuneMisregisteredWorker *worker = new FinetuneMisregisteredWorker(
      m_solverParams, m_rparams, m_scrToImgParams, m_scan, crop, progress,
      params, m_geometryPanel->isNonlinearEnabled());
  QThread *thread = new QThread(this);
  worker->moveToThread(thread);
  trackBackgroundThread(thread);

  // Connect signals
  connect(thread, &QThread::started, worker, &FinetuneMisregisteredWorker::run);
  connect(worker, &FinetuneMisregisteredWorker::finished, thread,
          &QThread::quit, Qt::DirectConnection);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);

  // Connect to our slot to handle results
  connect(
      worker, &FinetuneMisregisteredWorker::pointsReady, this,
      [this, progress](
          std::vector<colorscreen::solver_parameters::solver_point_t> points) {
        if (m_closing || (progress && progress->pool_cancel()))
          return;

        if (!points.empty()) {
          ParameterState oldState = getCurrentState();
          for (const auto &point : points) {
            m_solverParams.add_or_modify_point(point.img, point.scr,
                                               point.color);
          }
          m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                          &m_detectParams, &m_renderTypeParams,
                                          &m_solverParams);
          m_imageWidget->update();
          ParameterState newState = getCurrentState();
          m_undoStack->push(new ChangeParametersCommand(
              this, oldState, newState, "Add registration points"));
          updateRegistrationActions();
        }
      });
  connect(worker, &FinetuneMisregisteredWorker::geometryReady, this,
          [this, progress](colorscreen::scr_to_img_parameters result) {
            if (m_closing || (progress && progress->pool_cancel()))
              return;
            ParameterState newState = getCurrentState();
            newState.scrToImg.merge_solver_solution(result);
            changeParameters(newState,
                             "Automatically add points (Geometry update)");
            m_geometryFitBaseline = getCurrentState();
            m_geometryFitFailureInputs.reset();
            updateWorkflowSummary();
          });
  connect(worker, &FinetuneMisregisteredWorker::requestCurrentPoints, this,
          [this](std::vector<colorscreen::solver_parameters::solver_point_t> *points) {
            if (points)
              *points = m_closing
                            ? std::vector<colorscreen::solver_parameters::solver_point_t>()
                            : m_solverParams.points;
          }, Qt::BlockingQueuedConnection);
  connect(worker, &FinetuneMisregisteredWorker::finished, this,
          [this, progress](bool success) {
            if (!m_closing)
              removeProgress(progress);

            if (!m_closing && !success &&
                (!progress || !progress->pool_cancel())) {
              QMessageBox::warning(this, "Optimization Failed",
                                   "Automatically add points failed.");
            }
          });

  // Start thread
  thread->start();
}

/** Handle completion of a single-area finetune (rectangle selection).
   Adds all discovered points using add_or_modify_point (which updates
   existing points if they're close to a new detection), creates an
   undo command, and triggers auto-solver if enabled.  */
void MainWindow::onFinetuneFinished(
    bool success,
    std::vector<colorscreen::solver_parameters::solver_point_t> points,
    QThread *thread, std::shared_ptr<colorscreen::progress_info> progress) {
  Q_UNUSED(thread);

  // Remove progress only while the document still owns live presentation UI.
  if (!m_closing)
    removeProgress(progress);

  // A close or cancellation request makes any final batch stale even if the
  // worker raced to completion before acknowledging cancellation.
  if (m_closing || (progress && progress->pool_cancel()))
    return;

  // Add points if successful
  if (success && !points.empty()) {
    ParameterState oldState = getCurrentState();

    // Add all points using add_or_modify_point
    for (const auto &point : points) {
      m_solverParams.add_or_modify_point(point.img, point.scr, point.color);
    }

    // Update UI
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    m_imageWidget->update();

    // Create undo command
    ParameterState newState = getCurrentState();
    m_undoStack->push(new ChangeParametersCommand(this, oldState, newState,
                                                  "Add registration points"));

    // Trigger auto solver if enabled
    if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
      size_t count = m_imageWidget->registrationPointCount();
      if (count >= (size_t)colorscreen::solver_parameters::min_points(m_scrToImgParams.type)) {
        onOptimizeGeometry(true);
      }
    }
    updateRegistrationActions();
  }
}

/** Launch the automatic screen type detection worker.
   Creates a DetectScreenWorker running in a new thread, which analyses
   the scan to determine the colour screen type, initial geometry, and
   registration points.  Results are handled by onDetectScreenFinished.  */
void MainWindow::onAutodetectScreen() {
  if (!m_scan) {
    return;
  }

  // A stochastic process has no regular lattice to identify. Color-element
  // autodetection is performed by the screen-detection render modes instead.
  if (colorscreen::stochastic_screen_p(m_scrToImgParams.type)) {
    return;
  }

  if (colorscreen::screen_has_regular_geometry_p(m_scrToImgParams.type)) {
    m_autoAddPointsAfterCoordinates = true;
    onAutodetectCoordinatesRequested();
    return;
  }

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Detecting screen", 1);
  colorscreen::sub_task task(progress.get()); /* Keep so tasks appear nested. */
  addProgress(progress);

  // Create worker and thread
  const uint64_t generation = ++m_detectScreenGeneration;
  DetectScreenWorker *worker =
      new DetectScreenWorker(m_detectParams, m_solverParams, m_scrToImgParams,
                             m_scan, progress, m_rparams.gamma);
  QThread *thread = new QThread(this);
  worker->moveToThread(thread);
  trackBackgroundThread(thread);

  // Connect signals
  connect(thread, &QThread::started, worker, &DetectScreenWorker::detect);
  connect(worker, &DetectScreenWorker::finished, thread, &QThread::quit,
          Qt::DirectConnection);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);

  // Connect to our slot to handle results
  connect(worker, &DetectScreenWorker::finished, this,
          [this, progress, generation](
              bool success, colorscreen::detected_screen result,
              colorscreen::solver_parameters solverParams) {
            if (!m_closing && generation == m_detectScreenGeneration &&
                (!progress || !progress->pool_cancel()))
              onDetectScreenFinished(success, result, solverParams);
            if (!m_closing)
              removeProgress(progress);
          });

  thread->start();
}

/** Handle completion of automatic screen detection.
   Shows the detected screen type with a preview icon.  If the detected
   dye model differs from the current one, prompts the user to switch.
   Applies the detected screen type, geometry, mesh, and solver points,
   switches to interpolated render mode, and queues a geometry solve
   to refine the detected parameters (without recomputing the mesh,
   to preserve the detected pattern).  */
void MainWindow::onDetectScreenFinished(
    bool success, colorscreen::detected_screen result,
    colorscreen::solver_parameters solverParams) {
  if (!success || !result.success) {
    QMessageBox::warning(this, "Screen Detection", "Screen detection failed.");
    return;
  }

  // Store detected mesh for later restoration
  m_detectedMesh = result.mesh_trans;

  // Ask user about color model
  bool updateColorModel = false;

  // Determine what the auto color model would be
  colorscreen::render_parameters tempParams = m_rparams;
  tempParams.auto_color_model(result.param.type);

  // Always ask if detected dye differs from current
  QString currentDye = QString::fromUtf8(
      colorscreen::render_parameters::color_model_properties[m_rparams
                                                                 .color_model]
          .pretty_name);
  QString detectedDye = QString::fromUtf8(
      colorscreen::render_parameters::color_model_properties[tempParams
                                                                 .color_model]
          .pretty_name);
  QString detectedScreen = QString::fromUtf8(
      colorscreen::scr_names[(int)result.param.type].pretty_name);

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Screen Detection");
  msgBox.setIconPixmap(renderScreenIcon(result.param.type).pixmap(128, 128));

  if (currentDye != detectedDye) {
    msgBox.setText(QString("Detected Screen: <b>%1</b>").arg(detectedScreen));
    msgBox.setInformativeText(
        QString("Change color model (Dyes) from %1 to %2?")
            .arg(currentDye)
            .arg(detectedDye));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    updateColorModel = (msgBox.exec() == QMessageBox::Yes);
  } else {
    msgBox.setText(QString("Detected Screen: <b>%1</b> successfully.")
                       .arg(detectedScreen));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
  }

  // Create undo snapshot before making changes
  ParameterState oldState = getCurrentState();

  // Update parameters
  m_scrToImgParams.type = result.param.type;
  if (result.mesh_trans)
    {
      m_scrToImgParams.merge_solver_solution (result.param);
      m_scrToImgParams.mesh_trans = result.mesh_trans;
      m_scrToImgParams.mesh_trans_is_scr_to_img = true;
    }

  // Update color model if requested
  if (updateColorModel) {
    m_rparams.auto_color_model(result.param.type);
  }

  // Copy the modified solver points from the worker's local copy
  m_solverParams.points = solverParams.points;

  // Change render type to interpolated after successful autodetection
  m_renderTypeParams.type = colorscreen::render_type_interpolated;

  // Update UI
  m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                  &m_detectParams, &m_renderTypeParams,
                                  &m_solverParams);
  m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                     &m_detectParams);
  updateUIFromState(getCurrentState());
  updateRegistrationActions();
  updateModeMenu();

  // Always trigger geometry solver with computeMesh=false to preserve detected
  // mesh The solver will update center, coordinates, lens parameters, etc.
  // Request solver with explicit mesh computation disabled to preserve detected
  // pattern
  m_solverQueue.requestRender(false);

  // Create undo command
  ParameterState newState = getCurrentState();
  m_undoStack->push(new ChangeParametersCommand(this, oldState, newState,
                                                "Autodetect screen"));
}

/** Launch adaptive sharpening analysis with PARAMETERS selected by the user.
   The worker resolves automatic coarse/dense grid dimensions and connects
   incremental results to the AdaptiveSharpeningChart for real-time
   visualisation.  The final correction table is applied in
   onAdaptiveSharpeningFinished.  */
void MainWindow::onAdaptiveSharpeningRequested(
    const AdaptiveSharpeningParameters &parameters) {
  if (!m_scan)
    return;

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Adaptive sharpening analysis", 1);
  addUserVisibleProgress(progress, tr("Analyze displacements"));

  // Create worker from the complete one-run configuration selected in the
  // dialog. The worker resolves automatic dimensions in STEP1.
  const uint64_t generation = ++m_adaptiveSharpeningGeneration;
  AdaptiveSharpeningWorker *worker = new AdaptiveSharpeningWorker(
      m_scrToImgParams, m_rparams, m_scan, parameters, progress);

  QThread *thread = new QThread(this);
  worker->moveToThread(thread);
  trackBackgroundThread(thread);

  connect(thread, &QThread::started, worker, &AdaptiveSharpeningWorker::run);

  // Connect visualization signals.  Let the worker report the actual resolved
  // coarse and dense grids instead of duplicating its aspect-ratio logic here.
  if (m_sharpnessPanel && m_sharpnessPanel->getAdaptiveChart()) {
    m_sharpnessPanel->showAdaptiveChart();
    AdaptiveSharpeningChart *chart = m_sharpnessPanel->getAdaptiveChart();

    connect(worker, &AdaptiveSharpeningWorker::stripAnalysisStarted, chart,
            [chart](int w, int h) { chart->initialize(w, h); });
    connect(worker, &AdaptiveSharpeningWorker::stripAnalyzed, chart,
            &AdaptiveSharpeningChart::updateStrip);
    connect(worker, &AdaptiveSharpeningWorker::blurAnalysisStarted, chart,
            [chart](int w, int h) {
              // Re-initialize for high-res blur analysis
              chart->initialize(w, h);
            });
    connect(worker, &AdaptiveSharpeningWorker::blurAnalyzed, chart,
            &AdaptiveSharpeningChart::updateBlur);
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
            const bool cancelled = progress && progress->pool_cancel();
            if (!m_closing && generation == m_adaptiveSharpeningGeneration) {
              if (!cancelled)
                onAdaptiveSharpeningFinished(success, result, error);
              else
                statusBar()->showMessage(tr("Displacement analysis cancelled"),
                                         3000);
            }
            if (!m_closing)
              removeProgress(progress);
          });

  thread->start();
}

/** Handle completion of adaptive sharpening analysis.
   On success, wraps the computed scanner_blur_correction in an undo command
   and updates the chart widget.  Shows a success info or failure warning.  */
void MainWindow::onAdaptiveSharpeningFinished(
    bool success,
    std::shared_ptr<colorscreen::scanner_blur_correction_parameters> result,
    const QString &error) {
  if (!success || !result) {
    if (!success) {
      QMessageBox::warning(this, tr("Adaptive Sharpening"),
                           error.isEmpty()
                               ? tr("Analysis failed or cancelled.")
                               : error);
    }
    return;
  }

  ParameterState oldState = getCurrentState();
  ParameterState newState = oldState;
  newState.rparams.scanner_blur_correction = result;

  m_undoStack->push(new ChangeParametersCommand(
      this, oldState, newState, "Adaptive Sharpening Analysis"));

  // Update UI
  updateUIFromState(newState);

  // Explicitly update chart
  if (m_sharpnessPanel && m_sharpnessPanel->getAdaptiveChart()) {
    m_sharpnessPanel->getAdaptiveChart()->setCorrection(result);
  }

  QMessageBox::information(this, tr("Adaptive Sharpening"),
                           tr("Analysis completed successfully."));
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

/** Launch autodetection of screen coordinates (center, coordinate1,
   coordinate2).  Invokes the CoordinateOptimizationWorker's autodetect
   method in its background thread.  Results arrive at
   onAutodetectCoordinatesFinished.  */
void MainWindow::onAutodetectCoordinatesRequested() {
  if (!m_scan || !m_coordOptimizationWorker)
    return;

  m_coordOptimizationWorker->setScan(m_scan);

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Autodetecting coordinates", 1);
  addUserVisibleProgress(progress, tr("Coordinate autodetection"));

  const int reqId = ++m_coordinateAutodetectRequest;
  QMetaObject::invokeMethod(
      m_coordOptimizationWorker, "autodetect", Qt::QueuedConnection,
      Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, m_scrToImgParams),
      Q_ARG(colorscreen::render_parameters, m_rparams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress));
}

/** Forward coordinate optimisation request to onOptimizeCoordinates.  */
void MainWindow::onOptimizeCoordinatesRequested() {
  onOptimizeCoordinates();
}

/** Handle completion of coordinate autodetection.
   On success, applies the detected coordinates, switches to interpolated
   render mode, activates the AddPoint tool, and pushes an undo command.
   On failure, shows a warning.  */
void MainWindow::onAutodetectCoordinatesFinished(
    int reqId, colorscreen::scr_to_img_parameters result,
    std::shared_ptr<colorscreen::progress_info> progress, bool success,
    bool cancelled) {
  if (progress && !m_closing)
    removeProgress(progress);

  if (m_closing || reqId != m_coordinateAutodetectRequest)
    return;
  if (cancelled || (progress && progress->pool_cancel())) {
    m_autoAddPointsAfterCoordinates = false;
    return;
  }

  if (success) {
    ParameterState oldState = getCurrentState();
    m_scrToImgParams = result;

    // Automatically switch to interpolated mode
    m_renderTypeParams.type = colorscreen::render_type_interpolated;

    // Update UI
    updateUIFromState(getCurrentState());
    changeParameters(getCurrentState(), "Autodetect Coordinates");

    // Enable "Set screen coordinates" tool
    if (m_addPointAction) {
      m_addPointAction->setChecked(true);
    }

    m_imageWidget->update();
    statusBar()->showMessage("Autodetect coordinates finished", 3000);

    if (m_autoAddPointsAfterCoordinates) {
      m_autoAddPointsAfterCoordinates = false;
      onAutomaticallyAddPointsRequested(m_geometryPanel->finetuneAreaParams());
    }
  } else {
    m_autoAddPointsAfterCoordinates = false;
    QMessageBox::warning(this, "Autodetect Coordinates",
                         "Autodetect coordinates failed.");
  }
}

/** Launch coordinate optimisation.
   Invokes the CoordinateOptimizationWorker's optimize method in its
   background thread to refine center, coordinate1, and coordinate2
   using finetune-based analysis.  */
void MainWindow::onOptimizeCoordinates() {
  if (!m_scan || !m_coordOptimizationWorker)
    return;

  m_coordOptimizationWorker->setScan(m_scan);

  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Optimizing coordinates", 1);
  addProgress(progress);

  const int reqId = ++m_coordinateOptimizeRequest;
  QMetaObject::invokeMethod(
      m_coordOptimizationWorker, "optimize", Qt::QueuedConnection,
      Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, m_scrToImgParams),
      Q_ARG(colorscreen::render_parameters, m_rparams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress));
}

/** Handle completion of coordinate optimisation.
   On success, applies the refined center and coordinates, clears any
   mesh_trans (since coordinates changed), pushes an undo command, and
   updates the finetune diagnostic images.  */
void MainWindow::onOptimizeCoordinatesFinished(
    int reqId, colorscreen::finetune_result ret,
    std::shared_ptr<colorscreen::progress_info> progress, bool success,
    bool cancelled) {
  if (progress && !m_closing)
    removeProgress(progress);

  if (m_closing || reqId != m_coordinateOptimizeRequest || cancelled ||
      (progress && progress->pool_cancel()))
    return;

  if (success) {
    // Update parameters
    m_scrToImgParams.center = ret.center;
    m_scrToImgParams.coordinate1 = ret.coordinate1;
    m_scrToImgParams.coordinate2 = ret.coordinate2;
    m_scrToImgParams.mesh_trans = NULL;

    // Update UI
    changeParameters(getCurrentState(), "Optimize Coordinates");
    m_imageWidget->update();

    // Update finetune diagnostic images
    if (m_geometryPanel) {
      m_geometryPanel->updateFinetuneImages(ret);
    }
    updateWorkflowSummary();
    statusBar()->showMessage("Optimize coordinates finished", 3000);
  } else {
    QMessageBox::warning(this, "Optimization",
                         "Optimization failed: " +
                             QString::fromStdString(ret.err));
  }
}

/** Propagate coordinate system parameter changes to the renderer.
   Always updates NavigationView (which uses FAST mode depending on
   scr_to_img).  Only triggers ImageWidget re-render if the current
   render type requires screen-to-image mapping.  */
void MainWindow::onCoordinateSystemChanged() {
  if (!m_scan)
    return;

  // Navigation View always needs update because it uses FAST mode (which relies
  // on ScrToImg)
  m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                     &m_detectParams);

  // Main area: checks flag
  // Using colorscreen::render_type_max to safe check
  if (m_renderTypeParams.type < colorscreen::render_type_max) {
    const auto &prop =
        colorscreen::render_type_properties[m_renderTypeParams.type];
    if (prop.flags & colorscreen::render_type_property::NEEDS_SCR_TO_IMG) {
      m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
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
  m_gridManipulationOldState = getCurrentState();
}

/** Create an undo command after a grid drag operation completes.  */
void MainWindow::onCoordinateSystemManipulationFinished() {
  ParameterState newState = getCurrentState();
  m_undoStack->push(new ChangeParametersCommand(
      this, m_gridManipulationOldState, newState, "Modify coordinate system"));
}

/** Open file dialogs for white and optional black reference images,
   then launch a FlatFieldWorker to compute backlight correction
   parameters.  The worker runs in a background thread; results
   arrive at onFlatFieldFinished.  */
void MainWindow::onFlatFieldRequested() {
  QString filters =
      "Images (*.tif *.tiff *.jpg *.jpeg *.raw *.dng *.iiq *.nef *.NEF *.cr2 "
      "*.CR2 *.eip *.arw *.ARW *.raf *.RAF *.arq *.ARQ *.csprj);;All Files "
      "(*)";
  QString whiteFile = QFileDialog::getOpenFileName(
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

    // Create progress info
    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task("Flat field analysis", 100);
    this->addProgress(progress);

    // Create worker and thread
    const uint64_t generation = ++m_flatFieldGeneration;
    FlatFieldWorker *worker = new FlatFieldWorker(
        whiteFile, blackFile, m_rparams.gamma, m_rparams.demosaic, progress);
    QThread *thread = new QThread(this);
    worker->moveToThread(thread);
    trackBackgroundThread(thread);

    // Connect signals
    connect(thread, &QThread::started, worker, &FlatFieldWorker::run);
    connect(worker, &FlatFieldWorker::finished, thread, &QThread::quit,
          Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // Connect results
    connect(worker, &FlatFieldWorker::finished, this,
            [this, progress, generation](
                bool success,
                std::shared_ptr<colorscreen::backlight_correction_parameters>
                    result) {
              if (!m_closing && generation == m_flatFieldGeneration &&
                  (!progress || !progress->pool_cancel()))
                onFlatFieldFinished(success, result);
              if (!m_closing)
                removeProgress(progress);
            });

    thread->start();
  });
}

/** Handle completion of flat field analysis.
   On success, stores the backlight_correction in the parameter state
   with undo support and shows a success message.  */
void MainWindow::onFlatFieldFinished(
    bool success,
    std::shared_ptr<colorscreen::backlight_correction_parameters> result) {
  if (!success || !result) {
    QMessageBox::warning(this, "Flat Field", "Flat field analysis failed.");
    return;
  }

  // Update parameters with undo support
  ParameterState newState = getCurrentState();
  newState.rparams.backlight_correction = result;

  changeParameters(newState, "Flat field");

  QMessageBox::information(this, "Flat Field",
                           "Flat field analysis successful.");
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

/** Handle completion of focus analysis.
   On success, applies the measured scanner MTF parameters (sigma, defocus,
   blur_diameter) to the render parameters and pushes an undo command.
   Updates the sharpness panel's finetune diagnostic images.  */
void MainWindow::onFocusAnalysisFinished(bool success,
                                         colorscreen::finetune_result res) {
  if (m_sharpnessPanel) {
    m_sharpnessPanel->setFocusAnalysisChecked(false);
  }

  if (success) {
    ParameterState oldState = getCurrentState();
    m_rparams.sharpen.scanner_mtf.sigma = res.scanner_mtf_sigma;
    m_rparams.sharpen.scanner_mtf.defocus = res.scanner_mtf_defocus;
    m_rparams.sharpen.scanner_mtf.blur_diameter = res.scanner_mtf_blur_diameter;

    ParameterState newState = getCurrentState();
    m_undoStack->push(new ChangeParametersCommand(this, oldState, newState,
                                                  "Focus analysis"));
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    m_sharpnessPanel->updateFinetuneImages(res);
    updateUIFromState(newState);
    statusBar()->showMessage(tr("Focus analysis complete"), 3000);
  } else {
    statusBar()->showMessage(tr("Focus analysis failed"), 3000);
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
  overlays.reserve(m_focusAreaCandidates.size());
  std::map<size_t, colorscreen::coord_t> heldOut;
  for (size_t i = 0; i < m_focusAreaAnalysisResult.selected.size(); ++i) {
    if (i < m_focusAreaAnalysisResult.held_out_relative_badness.size())
      heldOut[m_focusAreaAnalysisResult.selected[i]] =
          m_focusAreaAnalysisResult.held_out_relative_badness[i];
  }
  std::set<size_t> selected(m_focusAreaAnalysisResult.selected.begin(),
                            m_focusAreaAnalysisResult.selected.end());
  for (size_t i = 0; i < m_focusAreaCandidates.size(); ++i) {
    ImageWidget::FocusAreaOverlay overlay;
    overlay.area = m_focusAreaCandidates[i].area;
    overlay.fitSuccessful = m_focusAreaCandidates[i].fit.success;
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
  m_focusAreaCandidates.clear();
  m_focusAreaAnalysisResult = colorscreen::finetune_focus_analysis_result();
  updateFocusAreaOverlays();
  if (m_sharpnessPanel)
    m_sharpnessPanel->setFocusAreaAnalysisState(0, m_focusAreaAnalysisRunning);
}

/** Find locally uniform areas on an unadjusted interpolated reconstruction.
    The complete render/search pass runs outside the GUI thread and stores its
    result only in this MainWindow, preserving the document boundary. */
void MainWindow::onFindFocusAreasRequested() {
  if (!m_scan || m_focusAreaAnalysisRunning)
    return;

  m_focusAreaAnalysisRunning = true;
  clearFocusAreaAnalysis();
  if (m_sharpnessPanel)
    m_sharpnessPanel->setFocusAreaAnalysisState(
        0, true, tr("Searching for uniform colour areas…"));

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Finding focus analysis areas", 1);
  addUserVisibleProgress(progress, tr("Find focus areas"));

  const colorscreen::render_parameters rparams = m_rparams;
  const colorscreen::scr_to_img_parameters geometry = m_scrToImgParams;
  const std::shared_ptr<colorscreen::image_data> scan = m_scan;
  auto *watcher = new QFutureWatcher<FocusAreaFindTaskResult>(this);
  connect(watcher, &QFutureWatcher<FocusAreaFindTaskResult>::finished, this,
          [this, watcher, progress]() {
            if (m_closing) {
              watcher->deleteLater();
              return;
            }
            const FocusAreaFindTaskResult result = watcher->result();
            watcher->deleteLater();
            removeProgress(progress);
            m_focusAreaAnalysisRunning = false;
            if (result.cancelled) {
              if (m_sharpnessPanel)
                m_sharpnessPanel->setFocusAreaAnalysisState(
                    0, false, tr("Focus-area search cancelled."));
              return;
            }
            if (!result.success) {
              if (m_sharpnessPanel)
                m_sharpnessPanel->setFocusAreaAnalysisState(
                    0, false,
                    tr("Focus-area search failed: %1")
                        .arg(QString::fromStdString(result.error)));
              return;
            }
            m_focusAreaCandidates = result.candidates;
            m_focusAreaAnalysisResult
                = colorscreen::finetune_focus_analysis_result();
            updateFocusAreaOverlays();
            const int count = static_cast<int>(m_focusAreaCandidates.size());
            if (m_sharpnessPanel)
              m_sharpnessPanel->setFocusAreaAnalysisState(
                  count, false,
                  tr("Found %1 candidate uniform area(s).").arg(count));
            statusBar()->showMessage(
                tr("Found %1 focus analysis area(s)").arg(count), 4000);
          });

  colorscreen::finetune_focus_area_image_search_parameters search;
  search.search.max_candidates = 24;
  watcher->setFuture(QtConcurrent::run(
      [rparams, geometry, scan, progress, search]() mutable {
        FocusAreaFindTaskResult result;
        result.success = colorscreen::finetune_find_focus_area_candidates_in_image(
            rparams, geometry, *scan, search, &result.candidates,
            progress.get(), &result.error);
        result.cancelled = progress->cancelled();
        return result;
      }));
}

/** Verify discovered areas individually, fit the selected colour-diverse set
    jointly, and compute leave-one-out plus true held-out diagnostics.  No
    measured focus value is applied until the user explicitly accepts it. */
void MainWindow::onAnalyzeFocusAreasRequested(uint64_t flags) {
  if (!m_scan || m_focusAreaAnalysisRunning)
    return;
  if (m_focusAreaCandidates.size() < 3) {
    QMessageBox::information(this, tr("Focus analysis areas"),
                             tr("Find at least three candidate areas first."));
    return;
  }
  const uint64_t focusMask = colorscreen::finetune_screen_blur
      | colorscreen::finetune_scanner_mtf_sigma
      | colorscreen::finetune_scanner_mtf_defocus;
  flags &= focusMask;
  if (!flags) {
    QMessageBox::information(
        this, tr("Focus analysis areas"),
        tr("Enable at least one scalar blur/focus parameter to optimize."));
    return;
  }


  m_focusAreaAnalysisRunning = true;
  if (m_sharpnessPanel)
    m_sharpnessPanel->setFocusAreaAnalysisState(
        static_cast<int>(m_focusAreaCandidates.size()), true,
        tr("Verifying and jointly fitting focus areas…"));

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Analyzing focus areas", 1);
  addUserVisibleProgress(progress, tr("Analyze focus areas"));

  const colorscreen::render_parameters rparams = m_rparams;
  const colorscreen::scr_to_img_parameters geometry = m_scrToImgParams;
  const std::shared_ptr<colorscreen::image_data> scan = m_scan;
  const std::vector<colorscreen::finetune_focus_area_candidate> candidates
      = m_focusAreaCandidates;
  const bool useMonochrome = focusAnalysisUsesMonochromeInput(*scan);
  auto *watcher = new QFutureWatcher<FocusAreaAnalyzeTaskResult>(this);
  connect(watcher, &QFutureWatcher<FocusAreaAnalyzeTaskResult>::finished, this,
          [this, watcher, progress, flags]() {
            if (m_closing) {
              watcher->deleteLater();
              return;
            }
            const FocusAreaAnalyzeTaskResult result = watcher->result();
            watcher->deleteLater();
            removeProgress(progress);
            m_focusAreaAnalysisRunning = false;
            m_focusAreaCandidates = result.candidates;
            m_focusAreaAnalysisResult = result.analysis;
            updateFocusAreaOverlays();
            if (result.cancelled) {
              if (m_sharpnessPanel)
                m_sharpnessPanel->setFocusAreaAnalysisState(
                    static_cast<int>(m_focusAreaCandidates.size()), false,
                    tr("Focus-area analysis cancelled."));
              return;
            }
            if (!result.success) {
              const QString error = QString::fromStdString(result.error);
              if (m_sharpnessPanel)
                m_sharpnessPanel->setFocusAreaAnalysisState(
                    static_cast<int>(m_focusAreaCandidates.size()), false,
                    tr("Focus-area analysis failed: %1").arg(error));
              QMessageBox::warning(this, tr("Focus analysis areas"), error);
              return;
            }

            const auto &analysis = m_focusAreaAnalysisResult;
            QStringList details;
            details << tr("Selected %1 of %2 verified candidates.")
                           .arg(static_cast<int>(analysis.selected.size()))
                           .arg(static_cast<int>(m_focusAreaCandidates.size()));
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
              if (m_rparams.sharpen.scanner_mtf.simulate_diffraction_p())
                details << tr("Physical defocus: %1 mm")
                               .arg(analysis.joint_fit.scanner_mtf_defocus, 0,
                                    'g', 5);
              else
                details << tr("Compact blur diameter: %1 px")
                               .arg(analysis.joint_fit.scanner_mtf_blur_diameter,
                                    0, 'g', 5);
            }
            const QString summary = details.join(QStringLiteral("\n"));
            if (m_sharpnessPanel)
              m_sharpnessPanel->setFocusAreaAnalysisState(
                  static_cast<int>(m_focusAreaCandidates.size()), false,
                  summary);

            QMessageBox box(this);
            box.setWindowTitle(tr("Focus analysis areas"));
            box.setIcon(QMessageBox::Information);
            box.setText(summary);
            box.setInformativeText(
                tr("The value is not applied automatically. Inspect the "
                   "selected rectangles and validation diagnostics before "
                   "accepting it."));
            QPushButton *applyButton
                = box.addButton(tr("Apply focus"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Close);
            box.exec();
            if (box.clickedButton() != applyButton)
              return;

            ParameterState state = getCurrentState();
            if ((flags & colorscreen::finetune_screen_blur)
                && analysis.joint_fit.screen_blur_radius >= 0)
              state.rparams.screen_blur_radius
                  = analysis.joint_fit.screen_blur_radius;
            if ((flags & colorscreen::finetune_scanner_mtf_sigma)
                && analysis.joint_fit.scanner_mtf_sigma >= 0)
              state.rparams.sharpen.scanner_mtf.sigma
                  = analysis.joint_fit.scanner_mtf_sigma;
            if (flags & colorscreen::finetune_scanner_mtf_defocus) {
              if (state.rparams.sharpen.scanner_mtf.simulate_diffraction_p())
                state.rparams.sharpen.scanner_mtf.defocus
                    = analysis.joint_fit.scanner_mtf_defocus;
              else
                state.rparams.sharpen.scanner_mtf.blur_diameter
                    = analysis.joint_fit.scanner_mtf_blur_diameter;
            }
            changeParameters(state, tr("Apply multi-area focus analysis"));
          });

  watcher->setFuture(QtConcurrent::run(
      [rparams, geometry, scan, candidates, progress, flags,
       useMonochrome]() mutable {
        FocusAreaAnalyzeTaskResult result;
        result.candidates = candidates;
        colorscreen::finetune_parameters local;
        local.range = 4;
        local.ignore_outliers = 0;
        /* Candidate verification determines local phase/colour only.
           Scanner MTF is shared and is fitted after area selection. */
        local.flags = colorscreen::finetune_position;
        if (useMonochrome)
          local.flags |= colorscreen::finetune_bw
              | colorscreen::finetune_no_normalize
              | colorscreen::finetune_no_data_collection;
        for (auto &candidate : result.candidates) {
          if (progress->cancelled()) {
            result.cancelled = true;
            return result;
          }
          candidate.fit = colorscreen::finetune(
              rparams, geometry, *scan, {candidate.center}, nullptr, local,
              progress.get());
        }

        colorscreen::finetune_parameters joint = local;
        joint.flags |= flags | colorscreen::finetune_no_normalize
            | colorscreen::finetune_no_data_collection;
        if (!useMonochrome)
          joint.flags |= colorscreen::finetune_uniform_image_layer;
        colorscreen::finetune_focus_analysis_parameters analysisParameters;
        analysisParameters.selection.min_areas = 3;
        analysisParameters.selection.max_areas = 8;
        /* Full RGB rank is required to learn shared RGB screen-primary
           responses, but it is not an identifiability condition for BW: each
           BW area has its own three primary weights and only blur is shared.
           Keep D-optimal ordering, but do not reject the best BW subset solely
           because its cross-area colour Gram matrix is rank deficient. */
        if (useMonochrome)
          analysisParameters.selection.minimum_color_volume = 0;
        analysisParameters.leave_one_out = true;
        /* Frozen RGB-primary held-out validation belongs to the RGB uniform
           image-layer model.  BW still gets full leave-one-out stability. */
        analysisParameters.held_out = !useMonochrome;
        result.success = colorscreen::finetune_analyze_focus_areas(
            rparams, geometry, *scan, result.candidates, joint,
            analysisParameters, &result.analysis, progress.get());
        result.cancelled = progress->cancelled();
        if (!result.success && !result.cancelled)
          result.error = result.analysis.err.empty()
              ? "focus-area joint analysis failed"
              : result.analysis.err;
        return result;
      }));
}

/** Render the current image to a TIFF or DNG file.
   Shows a render settings dialog (RenderDialog) for output format,
   scale, geometry, and antialiasing options.  Runs the render in a
   background thread via QtConcurrent::run, tracking it with
   m_renderProgress so the cancel button can prompt before aborting.
   On completion, shows a status message or error; on cancellation,
   removes the incomplete output file.  */
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

    // Show render settings dialog
    RenderDialog dlg(m_renderTypeParams, m_rparams, m_scrToImgParams,
                     m_scan.get(), isDng, this);
    if (dlg.exec() != QDialog::Accepted)
      return;

    // Snapshot current parameters (render runs in background)
    auto scan = m_scan;
    colorscreen::scr_to_img_parameters scrParams = m_scrToImgParams;
    colorscreen::scr_detect_parameters detectParams = m_detectParams;
    colorscreen::render_parameters rparams = m_rparams;
    colorscreen::render_type_parameters rtparams = dlg.renderTypeParams();
    rparams.output_profile = dlg.outputProfile();
    std::string outputPathStd = outputPath.toStdString();

    auto progress = std::make_shared<colorscreen::progress_info>();
    m_renderProgress =
        progress; // track so close/cancel can ask for confirmation
    addUserVisibleProgress(
        progress, tr("Rendering %1").arg(QFileInfo(outputPath).fileName()));

    // Run render in background thread
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, progress, outputPath]() {
              bool success = watcher->result();
              bool cancelled = progress->pool_cancel();
              m_renderProgress.reset(); // no longer active
              if (!m_closing)
                removeProgress(progress);
              watcher->deleteLater();
              if (cancelled || !success) {
                // Remove the incomplete output file
                if (QFile::exists(outputPath))
                  QFile::remove(outputPath);
              }
              if (m_closing)
                return;
              if (cancelled) {
                statusBar()->showMessage(tr("Render cancelled"), 3000);
              } else if (success) {
                statusBar()->showMessage(tr("Rendered to %1").arg(outputPath),
                                         5000);
              } else {
                QMessageBox::critical(
                    this, tr("Render Failed"),
                    tr("Failed to render to:\n%1").arg(outputPath));
              }
            });

    // Extract all dialog results before spawning the background thread
    // (QDialog is stack-allocated; capturing it by reference would be unsafe)
    bool renderHdr = dlg.hdr();
    int renderDepth = dlg.depth();
    auto renderGeometry = dlg.geometry();
    int renderAntialias = dlg.antialias();
    double renderScale = dlg.scale();
    double renderScreenScale = dlg.screenScale();
    int renderWidth = dlg.outputWidth();
    int renderHeight = dlg.outputHeight();

    QFuture<bool> future = QtConcurrent::run(
        [scan, scrParams, detectParams, rparams, rtparams, outputPathStd, isDng,
         progress, renderHdr, renderDepth, renderGeometry, renderAntialias,
         renderScale, renderScreenScale, renderWidth,
         renderHeight]() mutable -> bool {
          colorscreen::render_to_file_params rfparams;
          rfparams.filename = outputPathStd.c_str();
          rfparams.verbose = false;
          rfparams.dng = isDng;
          rfparams.hdr = renderHdr;
          rfparams.depth = renderDepth;
          rfparams.geometry = renderGeometry;
          rfparams.antialias = renderAntialias;
          rfparams.scale = renderScale;
          rfparams.screen_scale = renderScreenScale;
          rfparams.width = renderWidth;
          rfparams.height = renderHeight;

          const char *error = nullptr;
          return colorscreen::render_to_file(*scan, scrParams, detectParams,
                                             rparams, rfparams, rtparams,
                                             progress.get(), &error);
        });

    watcher->setFuture(future);
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
   Snapshot every input consumed by optimize_color_model_colors().  TaskQueue
   still provides newest-request replacement, while m_profileFitPendingInputs
   additionally rejects a result if the document changes without starting a
   replacement request. */
void MainWindow::onColorOptimizeRequested(bool /*autoMode*/) {
  if (!m_scan || !m_colorOptimizerWorker || m_profileSpots.size() < 4)
    return;

  const ColorOptimizerRequestData inputs = profileCalibrationInputs();
  m_profileFitPendingInputs = inputs;
  m_profileFitFailureInputs.reset();
  updateWorkflowSummary();
  m_colorOptimizerQueue.requestRender(QVariant::fromValue(inputs));
}

/** TaskQueue callback that dispatches the color optimisation request to
   the ColorOptimizerWorker running in m_colorOptimizerThread. */
void MainWindow::onTriggerColorOptimize(
    int reqId, std::shared_ptr<colorscreen::progress_info> progress,
    const QVariant &userData) {
  if (!m_scan || !m_colorOptimizerWorker ||
      !userData.canConvert<ColorOptimizerRequestData>()) {
    const bool current = m_colorOptimizerQueue.reportFinished(reqId, false);
    if (current) {
      m_profileFitPendingInputs.reset();
      updateWorkflowSummary();
    }
    return;
  }

  const auto inputs = userData.value<ColorOptimizerRequestData>();
  if (progress)
    progress->set_task("Optimizing color profile", 1);

  QMetaObject::invokeMethod(
      m_colorOptimizerWorker, "optimize", Qt::QueuedConnection,
      Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, inputs.scrParams),
      Q_ARG(colorscreen::render_parameters, inputs.rparams),
      Q_ARG(std::vector<colorscreen::point_t>, inputs.spots),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress));
}

/** Handle completion of color profile optimisation.
   The result is publishable only when the exact render/screen/spot snapshot is
   still current.  On success the fitted profile becomes the new baseline;
   later edits retain the coefficients but label them stale. */
void MainWindow::onColorOptimizerFinished(
    int reqId, colorscreen::render_parameters updatedRparams,
    std::vector<colorscreen::color_match> results, bool success,
    bool cancelled) {
  const bool current = m_colorOptimizerQueue.reportFinished(reqId, success);
  if (!current || m_closing)
    return;

  const ColorOptimizerRequestData now = profileCalibrationInputs();
  const bool inputsStillCurrent =
      m_profileFitPendingInputs && *m_profileFitPendingInputs == now;
  m_profileFitPendingInputs.reset();

  if (cancelled || !inputsStillCurrent) {
    updateWorkflowSummary();
    return;
  }

  if (success) {
    ParameterState newState = getCurrentState();
    newState.rparams.profiled_dark = updatedRparams.profiled_dark;
    newState.rparams.profiled_red = updatedRparams.profiled_red;
    newState.rparams.profiled_green = updatedRparams.profiled_green;
    newState.rparams.profiled_blue = updatedRparams.profiled_blue;
    changeParameters(newState, tr("Optimize color"));

    m_profileFitBaseline = profileCalibrationInputs();
    m_profileFitFailureInputs.reset();
    m_profileFitAverageDeltaE = -1;
    if (!results.empty()) {
      double total = 0;
      for (const colorscreen::color_match &match : results)
        total += match.deltaE;
      m_profileFitAverageDeltaE = total / results.size();
    }

    m_profileSpotResults = results;
    if (m_profilePanel)
      m_profilePanel->setSpotResults(results);
    if (m_imageWidget) {
      m_imageWidget->setProfileSpots(&m_profileSpots, &m_profileSpotResults);
      m_imageWidget->update();
    }
  } else {
    m_profileFitFailureInputs = now;
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
  statusBar()->showMessage(tr("Click and drag to measure distance for DPI calculation"), 5000);
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

  MeasureDialog dlg(distPixels, m_rparams.sharpen.scanner_mtf.scan_dpi, this);
  if (dlg.exec() == QDialog::Accepted) {
    double newDpi = dlg.getResultDpi();
    ParameterState state = getCurrentState();
    state.rparams.sharpen.scanner_mtf.scan_dpi = newDpi;
    changeParameters(state, tr("Set DPI by measurement"));
  }
}

/** Configure a slanted-edge measurement and, when CHECKED, ask the user for
    its image area.  Analysis settings are chosen before the edge is measured
    because oversampling, LSF support and windowing change the stored curve and
    cannot be altered later by the model-fitting dialog.  */
void MainWindow::onMeasureMtfRequested(bool checked) {
  if (checked) {
    ParameterState currentState = getCurrentState();
    const colorscreen::mtf_parameters &currentMtf =
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

    SlantedEdgeDialog dialog(defaults, !currentMtf.measurements.empty(),
                             hasRgb, hasInfrared, this);
    if (dialog.exec() != QDialog::Accepted) {
      m_sharpnessPanel->setMeasureMtfChecked(false);
      return;
    }
    const colorscreen::slanted_edge_parameters baseParameters =
        dialog.parameters();
    m_slantedEdgeParameters = baseParameters;

    std::vector<colorscreen::slanted_edge_parameters> measurementParameters;
    if (dialog.measureNativeChannels()) {
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
  } else {
    if (inspectorImageWidget()->interactionMode() == ImageWidget::GenericAreaMode) {
      restoreInteractionMode();
      statusBar()->clearMessage();
      m_areaSelectionCallback = nullptr;
    }
  }
}
