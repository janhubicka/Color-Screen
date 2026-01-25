#include "MainWindow.h"
#include "FinetuneWorker.h"
#include "DetectScreenWorker.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/histogram.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "ImageWidget.h"
#include "NavigationView.h"
#include "ScreenPanel.h"
#include "GeometryPanel.h"
#include "GeometrySolverWorker.h"
#include "mesh.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QComboBox>
#include <QDateTime>   // Added QDateTime include
#include <QDockWidget> // Added
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QActionGroup>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QFrame>
#include <QSizeGrip>
#include <QWindow>
#include <QtConcurrent>
#include <QSvgRenderer>

// Undo/Redo Implementation

class ChangeParametersCommand : public QUndoCommand {
public:
  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,
                          const ParameterState &newState,
                          const QString &description = QString())
      : m_window(window), m_oldState(oldState), m_newState(newState) {
    setText(description.isEmpty() ? "Change Parameters" : description);
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
  }

  int id() const override {
    return 1; // All parameter changes have the same ID
  }

  bool mergeWith(const QUndoCommand *other) override {
    if (other->id() != id())
      return false;

    const ChangeParametersCommand *cmd =
        static_cast<const ChangeParametersCommand *>(other);

    // Only merge if commands are within 500ms of each other (e.g., slider
    // dragging)
    qint64 timeDiff = cmd->m_timestamp - m_timestamp;
    if (timeDiff > 500) {
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
  qint64 m_timestamp; // Timestamp for merge window
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_undoStack = new QUndoStack(this);

  setupUi();

  // Progress Timer
  m_progressTimer = new QTimer(this);
  m_progressTimer->setInterval(100);
  connect(m_progressTimer, &QTimer::timeout, this,
          &MainWindow::onProgressTimer);

  // Initialize crash recovery directory
  m_recoveryDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  // Use "colorscreen" instead of app name "colorscreen-qt"
  m_recoveryDir.replace("/colorscreen-qt", "/colorscreen");
  QDir().mkpath(m_recoveryDir);

  // Check for recovery files and offer restoration
  if (hasRecoveryFiles()) {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Crash Recovery",
        "The application did not exit cleanly last time.\n"
        "Would you like to restore your previous session?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
      loadRecoveryState();
    } else {
      clearRecoveryFiles();
    }
  }

  // Set up recovery auto-save timer (30 seconds)
  m_recoveryTimer = new QTimer(this);
  m_recoveryTimer->setInterval(30000); // 30 seconds
  connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::saveRecoveryState);
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

  connect(m_solverWorker, &GeometrySolverWorker::finished, this, &MainWindow::onSolverFinished);
  
  // Solver Queue connections
  connect(&m_solverQueue, &TaskQueue::triggerRender, this, &MainWindow::onTriggerSolve);
  connect(&m_solverQueue, &TaskQueue::progressStarted, this, &MainWindow::addProgress);
  connect(&m_solverQueue, &TaskQueue::progressFinished, this, &MainWindow::removeProgress);
}

MainWindow::~MainWindow() {
  // Hide window first to avoid invalid accessibility/focus events during destruction
  // This is a known workaround for MacOS crashes on exit (QTBUG-71850)
  hide();

  if (m_solverThread) {
    m_solverThread->quit();
    m_solverThread->wait();
    // Delete the worker that was moved to the thread
    delete m_solverWorker;
    m_solverWorker = nullptr;
  }
  
  // Explicitly delete UI components that might access member variables (callbacks)
  // This ensures they are destroyed BEFORE members like m_rparams or m_scan.
  // We delete the main splitter which contains the panels.
  if (m_mainSplitter) {
      m_mainSplitter->setParent(nullptr); // Detach first
      delete m_mainSplitter;
      m_mainSplitter = nullptr; 
  }

  // Also manually delete docks as they might hold detached panels
  if (m_mtfDock) delete m_mtfDock;
  if (m_spectraDock) delete m_spectraDock;
  if (m_tilesDock) delete m_tilesDock;
  if (m_colorTilesDock) delete m_colorTilesDock;
  if (m_correctedColorTilesDock) delete m_correctedColorTilesDock;
  if (m_screenPreviewDock) delete m_screenPreviewDock;
  if (m_deformationDock) delete m_deformationDock;
  if (m_lensDock) delete m_lensDock;
  if (m_perspectiveDock) delete m_perspectiveDock;
  if (m_nonlinearDock) delete m_nonlinearDock;
}

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
  QVBoxLayout *rightLayout = new QVBoxLayout(m_rightColumn);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  QSplitter *rightSplitter = new QSplitter(Qt::Vertical, m_rightColumn);
  rightLayout->addWidget(rightSplitter);

  // Top Right: Navigation View
  m_navigationView = new NavigationView(this);
  m_navigationView->setMinimumHeight(200);
  rightSplitter->addWidget(m_navigationView);

  // Connect Navigation Signals
  connect(m_imageWidget, &ImageWidget::viewStateChanged, m_navigationView,
          &NavigationView::onViewStateChanged);
  connect(m_navigationView, &NavigationView::zoomChanged, m_imageWidget,
          &ImageWidget::setZoom);
  connect(m_navigationView, &NavigationView::panChanged, m_imageWidget,
          &ImageWidget::setPan);

  // Connect NavigationView progress signals
  connect(m_navigationView, &NavigationView::progressStarted, this,
          &MainWindow::addProgress);
  connect(m_navigationView, &NavigationView::progressFinished, this,
          &MainWindow::removeProgress);

  // Bottom Right: Tabs
  m_configTabs = new QTabWidget(this);

  // Create Sharpness Panel
  m_sharpnessPanel = new SharpnessPanel(
      [this]() { return getCurrentState(); },
      [this](const ParameterState &s, const QString &desc) { changeParameters(s, desc); },
      [this]() { return m_scan; }, this);

  // Create Screen Panel
  m_screenPanel =
      new ScreenPanel([this]() { return getCurrentState(); },
                      [this](const ParameterState &s, const QString &desc) { changeParameters(s, desc); },
                      [this]() { return m_scan; }, this);

  // Create Color Panel (after Sharpness)
  // Create Color Panel (after Sharpness)
  m_colorPanel =
      new ColorPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s, const QString &desc) { changeParameters(s, desc); },
                     [this]() { return m_scan; }, this);

  // Connect Progress Signals from Panels
  connect(m_sharpnessPanel, &SharpnessPanel::progressStarted, this, &MainWindow::addProgress);
  connect(m_sharpnessPanel, &SharpnessPanel::progressFinished, this, &MainWindow::removeProgress);
  
  connect(m_screenPanel, &ScreenPanel::progressStarted, this, &MainWindow::addProgress);
  connect(m_screenPanel, &ScreenPanel::progressFinished, this, &MainWindow::removeProgress);
  connect(m_screenPanel, &ScreenPanel::autodetectRequested, this, &MainWindow::onAutodetectScreen);
  
  connect(m_colorPanel, &ColorPanel::progressStarted, this, &MainWindow::addProgress);
  connect(m_colorPanel, &ColorPanel::progressFinished, this, &MainWindow::removeProgress);

  m_configTabs->setObjectName("ConfigTabs");
  m_mtfDock = new QDockWidget("MTF Chart", this);

  m_mtfDock->setObjectName("MTFChartDock");
  m_mtfDock->setVisible(false);
  addDockWidget(Qt::BottomDockWidgetArea, m_mtfDock);

  m_tilesDock = new QDockWidget("Sharpness Preview", this);
  m_tilesDock->setObjectName("TilesDock");
  m_tilesDock->setVisible(false);
  addDockWidget(Qt::BottomDockWidgetArea, m_tilesDock);

  // Create Docks for Color components
  m_colorTilesDock = new QDockWidget("Color Preview", this);
  m_colorTilesDock->setObjectName("ColorTilesDock");
  m_colorTilesDock->setVisible(false);
  addDockWidget(Qt::BottomDockWidgetArea, m_colorTilesDock);

  m_correctedColorTilesDock = new QDockWidget("Corrected Color Preview", this);
  m_correctedColorTilesDock->setObjectName("CorrectedColorPreviewDock");
  m_correctedColorTilesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
  addDockWidget(Qt::RightDockWidgetArea, m_correctedColorTilesDock);
  m_correctedColorTilesDock->hide(); // Initially hidden

  // Screen Preview Dock
  m_screenPreviewDock = new QDockWidget("Screen Preview", this);
  m_screenPreviewDock->setObjectName("ScreenPreviewDock");
  m_screenPreviewDock->setAllowedAreas(Qt::AllDockWidgetAreas);
  addDockWidget(Qt::RightDockWidgetArea, m_screenPreviewDock);
  m_screenPreviewDock->hide(); // Initially hidden

  // Connection for Color Panel Spectra Chart
  m_spectraDock = new QDockWidget("Spectral Transmitance", this);
  m_spectraDock->setObjectName("SpectraDock");
  addDockWidget(Qt::RightDockWidgetArea, m_spectraDock);
  m_spectraDock->hide(); // Initially hidden

  // Deformation Chart Dock
  m_deformationDock = new QDockWidget("Deformation Visualization", this);
  m_deformationDock->setObjectName("DeformationDock");
  addDockWidget(Qt::RightDockWidgetArea, m_deformationDock);
  m_deformationDock->hide();

  m_lensDock = new QDockWidget("Lens Correction", this);
  m_lensDock->setObjectName("LensDock");
  addDockWidget(Qt::RightDockWidgetArea, m_lensDock);
  m_lensDock->hide();

  m_perspectiveDock = new QDockWidget("Perspective", this);
  m_perspectiveDock->setObjectName("PerspectiveDock");
  addDockWidget(Qt::RightDockWidgetArea, m_perspectiveDock);
  m_perspectiveDock->hide();

  m_nonlinearDock = new QDockWidget("Nonlinear transformation", this);
  m_nonlinearDock->setObjectName("NonlinearDock");
  addDockWidget(Qt::RightDockWidgetArea, m_nonlinearDock);
  m_nonlinearDock->hide();

  m_backlightDock = new QDockWidget("Backlight", this);
  m_backlightDock->setObjectName("BacklightDock");
  m_backlightDock->setVisible(false);
  addDockWidget(Qt::RightDockWidgetArea, m_backlightDock);

  // Event Filter for robust Close detection
  class DockCloseEventFilter : public QObject {
    std::function<void()> m_onClose;

  public:
    DockCloseEventFilter(QObject *parent, std::function<void()> onClose)
        : QObject(parent), m_onClose(onClose) {}

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
      if (event->type() == QEvent::Close) {
        if (m_onClose)
          m_onClose();
      }
      return QObject::eventFilter(obj, event);
    }
  };

  // Generic helper for docking connections
  auto setupDock = [this](QDockWidget *dock, auto *panel, auto detachSignal,
                          auto reattachMethod) {
    // Connect Detach
    connect(panel, detachSignal, this, [dock](QWidget *w) {
      if (!w)
        return;
      
      // Wrap widget in a frame to provide better resize borders
      QFrame *wrapper = new QFrame();
      wrapper->setObjectName("DetachedWrapper");
      wrapper->setFrameStyle(QFrame::Box | QFrame::Plain);
      wrapper->setLineWidth(1);
      // Modern sleek border
      wrapper->setStyleSheet(
          "QFrame#DetachedWrapper { border: 1px solid #555; background: Palette(Window); }");
      
      QVBoxLayout *outerLayout = new QVBoxLayout(wrapper);
      outerLayout->setContentsMargins(1, 1, 1, 1);
      outerLayout->setSpacing(0);

      QWidget *container = new QWidget();
      QGridLayout *gLayout = new QGridLayout(container);
      gLayout->setContentsMargins(0, 0, 0, 0);
      gLayout->setSpacing(0);
      
      gLayout->addWidget(w, 0, 0);
      
      QSizeGrip *grip = new QSizeGrip(container);
      gLayout->addWidget(grip, 0, 0, Qt::AlignRight | Qt::AlignBottom);

      outerLayout->addWidget(container);

      dock->setWidget(wrapper);
      w->show(); // Ensure widget is visible
      dock->setFloating(true);
      dock->show();
      
      // Use size hint of the original widget + some margins
      if (w->sizeHint().isValid()) {
          QSize s = w->sizeHint();
          dock->resize(s.width() + 4, s.height() + 4);
      }
    });

    // Connect Close/Reattach via Event Filter
    dock->installEventFilter(
        new DockCloseEventFilter(dock, [dock, panel, reattachMethod]() {
          if (dock->widget()) {
            // Find the original widget inside the wrapper
            QWidget *wrapper = dock->widget();
            QWidget *originalWidget = nullptr;
            // The structure is Wrapper -> QVBoxLayout -> Container -> QGridLayout -> Widget
            if (wrapper->layout() && wrapper->layout()->count() > 0) {
                QLayoutItem *containerItem = wrapper->layout()->itemAt(0);
                if (containerItem && containerItem->widget() && containerItem->widget()->layout()) {
                    QLayout *gLayout = containerItem->widget()->layout();
                    for (int i = 0; i < gLayout->count(); ++i) {
                        QWidget *child = gLayout->itemAt(i)->widget();
                        if (child && !qobject_cast<QSizeGrip*>(child)) {
                            originalWidget = child;
                            break;
                        }
                    }
                }
            }

            if (originalWidget) {
              (panel->*reattachMethod)(originalWidget);
            }
            dock->setWidget(nullptr);
            wrapper->deleteLater();
          }
        }));
  };

  // Wire up docks
  setupDock(m_mtfDock, m_sharpnessPanel,
            &SharpnessPanel::detachMTFChartRequested,
            &SharpnessPanel::reattachMTFChart);

  setupDock(m_tilesDock, m_sharpnessPanel,
            &SharpnessPanel::detachTilesRequested,
            &SharpnessPanel::reattachTiles);

  setupDock(m_colorTilesDock, m_colorPanel, &ColorPanel::detachTilesRequested,
            &ColorPanel::reattachTiles);

  setupDock(m_correctedColorTilesDock, m_colorPanel,
            &ColorPanel::detachCorrectedTilesRequested,
            &ColorPanel::reattachCorrectedTiles);

  setupDock(m_spectraDock, m_colorPanel,
            &ColorPanel::detachSpectraChartRequested,
            &ColorPanel::reattachSpectraChart);

  setupDock(m_screenPreviewDock, m_screenPanel,
            &ScreenPanel::detachPreviewRequested,
            &ScreenPanel::reattachPreview);
            


  // Create Digital Capture Panel
  m_capturePanel = new CapturePanel(
      [this]() { return getCurrentState(); },
      [this](const ParameterState &s, const QString &desc) {
        changeParameters(s, desc);
      },
      [this]() { return m_scan; },
      [this]() { 
        if (!m_currentImageFile.isEmpty()) {
          loadFile(m_currentImageFile, true); 
        }
      },
      this);

  // Create Geometry Panel
  m_geometryPanel =
      new GeometryPanel([this]() { return getCurrentState(); },
                        [this](const ParameterState &s, const QString &desc) { changeParameters(s, desc); },
                        [this]() { return m_scan; }, this);

  setupDock(m_deformationDock, m_geometryPanel,
            &GeometryPanel::detachDeformationChartRequested,
            &GeometryPanel::reattachDeformationChart);

  setupDock(m_lensDock, m_geometryPanel,
            &GeometryPanel::detachLensChartRequested,
            &GeometryPanel::reattachLensChart);

  setupDock(m_perspectiveDock, m_geometryPanel,
            &GeometryPanel::detachPerspectiveChartRequested,
            &GeometryPanel::reattachPerspectiveChart);

  setupDock(m_nonlinearDock, m_geometryPanel,
            &GeometryPanel::detachNonlinearChartRequested,
            &GeometryPanel::reattachNonlinearChart);

  m_configTabs->addTab(m_capturePanel, "Digital capture");
  connect(m_capturePanel, &CapturePanel::cropRequested, this, &MainWindow::onCropRequested);
  connect(m_capturePanel, &CapturePanel::flatFieldRequested, this, &MainWindow::onFlatFieldRequested);
  connect(m_capturePanel, &CapturePanel::autodetectRequested, this, &MainWindow::onAutodetectScreen);
  setupDock(m_backlightDock, m_capturePanel,
            &CapturePanel::detachBacklightRequested,
            &CapturePanel::reattachBacklight);

  connect(m_imageWidget, &ImageWidget::interactionModeChanged, this, [this](ImageWidget::InteractionMode mode) {
      if (m_capturePanel) {
          m_capturePanel->setCropChecked(mode == ImageWidget::CropMode);
      }
  });
  m_configTabs->addTab(m_sharpnessPanel, "Sharpness");
  m_configTabs->addTab(m_screenPanel, "Screen");
  m_configTabs->addTab(m_geometryPanel, "Geometry");
  m_configTabs->addTab(m_colorPanel, "Color");
  rightSplitter->addWidget(m_configTabs);

  // Register panels for updates
  m_panels.push_back(m_capturePanel);
  m_panels.push_back(m_sharpnessPanel);
  m_panels.push_back(m_screenPanel);
  m_panels.push_back(m_geometryPanel);
  m_panels.push_back(m_colorPanel);

  // Link Geometry Panel signals
  connect(m_geometryPanel, &GeometryPanel::optimizeRequested, this,
          &MainWindow::onOptimizeGeometry);
  connect(m_geometryPanel, &GeometryPanel::nonlinearToggled, this,
          &MainWindow::onNonlinearToggled);

  // Connect visualization sliders
  connect(m_geometryPanel, &GeometryPanel::heatmapToleranceChanged, m_imageWidget, &ImageWidget::setHeatmapTolerance);
  connect(m_geometryPanel, &GeometryPanel::exaggerateChanged, m_imageWidget, &ImageWidget::setExaggerate);
  connect(m_geometryPanel, &GeometryPanel::maxArrowLengthChanged, m_imageWidget, &ImageWidget::setMaxArrowLength);

  // Synchronization for Registration Points visibility
  m_registrationPointsAction->setChecked(
      m_imageWidget->registrationPointsVisible());
  
  // Link View menu -> ImageWidget
  connect(m_registrationPointsAction, &QAction::toggled, m_imageWidget,
          &ImageWidget::setShowRegistrationPoints);

  // Link ImageWidget -> View menu (to keep it in sync if changed elsewhere)
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          m_registrationPointsAction, &QAction::setChecked);

  // Link ImageWidget -> GeometryPanel checkbox
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged,
          this, [this](bool visible) {
    QCheckBox *cb = m_geometryPanel->findChild<QCheckBox*>("showRegistrationPointsBox");
    if (cb) {
        cb->blockSignals(true);
        cb->setChecked(visible);
        cb->blockSignals(false);
    }
  });

  // Connect fullscreen exit signal
  connect(m_imageWidget, &ImageWidget::exitFullscreenRequested, this, &MainWindow::toggleFullscreen);

  // Link GeometryPanel checkbox -> ImageWidget
  QCheckBox *regBox = m_geometryPanel->findChild<QCheckBox *>("showRegistrationPointsBox");
  if (regBox) {
    connect(regBox, &QCheckBox::toggled, m_imageWidget,
            &ImageWidget::setShowRegistrationPoints);
    QSignalBlocker blocker(regBox);
    regBox->setChecked(m_imageWidget->registrationPointsVisible());
  }

  // Auto solver trigger
  connect(m_imageWidget, &ImageWidget::pointManipulationStarted, this, &MainWindow::onPointManipulationStarted);
  connect(m_imageWidget, &ImageWidget::pointsChanged, this, &MainWindow::maybeTriggerAutoSolver);

  // Nonlinear corrections checkbox
  QCheckBox *nlBox = m_geometryPanel->findChild<QCheckBox *>("nonLinearBox");
  if (nlBox) {
    QSignalBlocker blocker(nlBox);
    nlBox->setChecked(m_scrToImgParams.mesh_trans != nullptr);
  }

  // Sync Auto Optimize checkbox with GeometryPanel
  QCheckBox *autoSolverBox = m_geometryPanel->findChild<QCheckBox *>("autoSolverBox");
  if (autoSolverBox && m_autoOptimizeAction) {
    // GeometryPanel -> Menu
    connect(autoSolverBox, &QCheckBox::toggled, m_autoOptimizeAction, &QAction::setChecked);
    // Menu -> GeometryPanel
    connect(m_autoOptimizeAction, &QAction::toggled, autoSolverBox, &QCheckBox::setChecked);
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

  // Progress Container (initially hidden)
  m_progressContainer = new QWidget(statusBar);
  QHBoxLayout *progressLayout = new QHBoxLayout(m_progressContainer);
  progressLayout->setContentsMargins(0, 0, 0, 0);
  progressLayout->setSpacing(8);

  m_statusLabel = new QLabel("", m_progressContainer);
  m_statusLabel->setMinimumWidth(150);
  progressLayout->addWidget(m_statusLabel);

  m_progressBar = new QProgressBar(m_progressContainer);
  m_progressBar->setRange(0, 100);
  m_progressBar->setTextVisible(false);
  m_progressBar->setMinimumWidth(200);
  progressLayout->addWidget(m_progressBar);

  // Progress switcher (count + prev/next buttons)
  m_progressCountLabel = new QLabel("1/1", m_progressContainer);
  m_progressCountLabel->setMinimumWidth(40);
  progressLayout->addWidget(m_progressCountLabel);

  m_prevProgressButton = new QPushButton("<", m_progressContainer);
  m_prevProgressButton->setMaximumWidth(30);
  m_prevProgressButton->setToolTip("Previous progress");
  connect(m_prevProgressButton, &QPushButton::clicked, this,
          &MainWindow::onPrevProgress);
  progressLayout->addWidget(m_prevProgressButton);

  m_nextProgressButton = new QPushButton(">", m_progressContainer);
  m_nextProgressButton->setMaximumWidth(30);
  m_nextProgressButton->setToolTip("Next progress");
  connect(m_nextProgressButton, &QPushButton::clicked, this,
          &MainWindow::onNextProgress);
  progressLayout->addWidget(m_nextProgressButton);

  m_cancelButton = new QPushButton("Cancel", m_progressContainer);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &MainWindow::onCancelClicked);
  progressLayout->addWidget(m_cancelButton);

  statusBar->addPermanentWidget(m_progressContainer);
  m_progressContainer->setVisible(false);

  // Initialize manual selection tracking
  m_manuallySelectedProgressIndex = -1;

  createToolbar(); // Initialize toolbar
}

// Helper to manually load and recolor symbolic icons on Windows where auto-recoloring fails
// Helper to manually load and recolor symbolic icons
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
      static QStringList subdirs = {"actions", "devices", "places", "status", "ui",
                                    "legacy", "categories", "apps", "mimetypes"};
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
        if (!path.isEmpty()) break;
      }
#endif

  if (!path.isEmpty()) {
    QIcon icon;
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return QIcon::fromTheme(name);
    
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

  m_toolbar->addSeparator();

  // Interaction Tools - Pan in View group
  QActionGroup *toolGroup = new QActionGroup(this);
  
  m_panAction = new QAction(getSymbolicIcon(":/icons/hand.svg"), "Pan", this);
  m_panAction->setActionGroup(toolGroup);
  m_panAction->setCheckable(true);
  m_panAction->setChecked(true);
  m_panAction->setToolTip("Pan Tool (P)");
  m_panAction->setShortcut(QKeySequence("P"));
  m_toolbar->addAction(m_panAction);

  // Zoom controls
  m_toolbar->addAction(m_zoomInAction);
  m_toolbar->addAction(m_zoomOutAction);
  m_toolbar->addAction(m_zoom100Action);
  m_toolbar->addAction(m_zoomFitAction);

  // Rotation
  QAction *rotLeftAction = m_toolbar->addAction(
      getSymbolicIcon(":/icons/rotate-left.svg"), "Rotate Left");
  connect(rotLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);

  QAction *rotRightAction = m_toolbar->addAction(
      getSymbolicIcon(":/icons/rotate-right.svg"), "Rotate Right");
  connect(rotRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
  
  if (m_mirrorAction) {
      m_toolbar->addAction(m_mirrorAction);
  }

  // === REGISTRATION GROUP ===
  QAction *regSeparator = m_toolbar->addSeparator();
  m_registrationActions.append(regSeparator);

  m_selectAction = new QAction(getSymbolicIcon(":/icons/arrow.svg"), "Select", this);
  m_selectAction->setActionGroup(toolGroup);
  m_selectAction->setCheckable(true);
  m_selectAction->setToolTip("Select Tool (S)");
  m_selectAction->setShortcut(QKeySequence("S"));
  m_toolbar->addAction(m_selectAction);
  m_registrationActions.append(m_selectAction);

  m_addPointAction = new QAction(getSymbolicIcon(":/icons/plus.svg"), "Add Point", this);
  m_addPointAction->setActionGroup(toolGroup);
  m_addPointAction->setCheckable(true);
  m_addPointAction->setToolTip("Add Registration Point (A)");
  m_addPointAction->setShortcut(QKeySequence("A"));
  m_toolbar->addAction(m_addPointAction);
  m_registrationActions.append(m_addPointAction);

  m_setCenterAction = new QAction(getSymbolicIcon(":/icons/crosshair.svg"), "Screen coordinates", this);
  m_setCenterAction->setActionGroup(toolGroup);
  m_setCenterAction->setCheckable(true);
  m_setCenterAction->setToolTip("Set Screen Coordinates (C)");
  m_setCenterAction->setShortcut(QKeySequence("C"));
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
    if (checked) m_imageWidget->setInteractionMode(ImageWidget::PanMode);
  });
  connect(m_selectAction, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      m_imageWidget->setInteractionMode(ImageWidget::SelectMode);
      // Auto-enable registration points visibility
      if (!m_imageWidget->registrationPointsVisible()) {
        m_imageWidget->setShowRegistrationPoints(true);
      }
    }
  });
  connect(m_addPointAction, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      m_imageWidget->setInteractionMode(ImageWidget::AddPointMode);
      // Auto-enable registration points visibility
      if (!m_imageWidget->registrationPointsVisible()) {
        m_imageWidget->setShowRegistrationPoints(true);
      }
    }
  });
  connect(m_setCenterAction, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
        m_imageWidget->setInteractionMode(ImageWidget::SetCenterMode);
    }
    m_lockRelativeCoordinatesAction->setVisible(checked);
    m_optimizeCoordinatesAction->setVisible(checked);
  });
  
  connect(m_imageWidget, &ImageWidget::selectionChanged, this, &MainWindow::updateRegistrationActions);
  connect(m_imageWidget, &ImageWidget::registrationPointsVisibilityChanged, this, &MainWindow::updateRegistrationActions);
  connect(m_imageWidget, &ImageWidget::pointAdded, this, &MainWindow::onPointAdded);
  connect(m_imageWidget, &ImageWidget::areaSelected, this, &MainWindow::onAreaSelected);
  connect(m_imageWidget, &ImageWidget::setCenterRequested, this, &MainWindow::onSetCenter);
  connect(m_imageWidget, &ImageWidget::coordinateSystemChanged, this, &MainWindow::onCoordinateSystemChanged);

  // Initially hide registration group
  updateRegistrationGroupVisibility();

  updateModeMenu();
}

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

void MainWindow::onMirrorHorizontally(bool checked) {
  if (!m_scan)
    return;
    
  ParameterState newState = getCurrentState();
  newState.rparams.scan_mirror = checked;
  
  changeParameters(newState, "Mirror Horizontally");
}

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
    m_imageWidget->setFocus(); // Set focus so key events work
    m_imageWidget->activateWindow(); // Also activate the window
    m_fullscreenAction->setChecked(true);
  }
}

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

    // If given type has render_type_property::NEEDS_SCR_TO_IMG do not show it
    // if scr_to_img type is Random.
    if (prop.flags & render_type_property::NEEDS_SCR_TO_IMG) {
      if (m_scrToImgParams.type == colorscreen::Random) {
        show = false;
      }
    }

    // If given type has render_type_property::NEEDS_RGB do not show it if
    // m_scan->rgbdata is NULL.
    if (prop.flags & render_type_property::NEEDS_RGB) {
      // Check m_scan
      if (!m_scan || !m_scan->rgbdata) {
        show = false;
      }
    }

    if (show) {
      m_modeComboBox->addItem(prop.name, QVariant(i));
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

  // Update color checkbox state based on current render type
  updateColorCheckBoxState();

  m_modeComboBox->blockSignals(false);
}

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

  bool ok = colorscreen::render_screen_tile(tile, type, rparams, 1.0, colorscreen::original_screen,
                               nullptr);

  if (ok) {
    QImage img(buffer.data(), w, h, w * 3, QImage::Format_RGB888);
    return QIcon(QPixmap::fromImage(img.copy()));
  }
  return QIcon();
}

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

void MainWindow::createMenus() {
  QMenu *fileMenu = menuBar()->addMenu("&File");
  m_openAction = fileMenu->addAction("&Open Image...");
  m_openAction->setShortcut(QKeySequence::Open); // Ctrl+O
  connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenImage);

  m_recentFilesMenu = fileMenu->addMenu("Open &Recent");
  updateRecentFileActions();

  QAction *openParamsAction = fileMenu->addAction("Open &Parameters...");
  connect(openParamsAction, &QAction::triggered, this,
          &MainWindow::onOpenParameters);

  m_recentParamsMenu = fileMenu->addMenu("Open Recent &Parameters");
  updateRecentParamsActions();

  fileMenu->addSeparator();

  m_saveAction = fileMenu->addAction("&Save Parameters");
  m_saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
  connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveParameters);

  m_saveAsAction = fileMenu->addAction("Save Parameters &As...");
  m_saveAsAction->setShortcut(QKeySequence::SaveAs); // Ctrl+Shift+S
  connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveParametersAs);

  fileMenu->addSeparator();

  QAction *exitAction = fileMenu->addAction("E&xit");
  connect(exitAction, &QAction::triggered, this, &QWidget::close);

  QMenu *editMenu = menuBar()->addMenu("&Edit");
  QAction *undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
  undoAction->setIcon(QIcon::fromTheme("edit-undo-symbolic"));
  undoAction->setShortcut(QKeySequence::Undo);
  editMenu->addAction(undoAction);

  QAction *redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
  redoAction->setIcon(QIcon::fromTheme("edit-redo-symbolic"));
  redoAction->setShortcut(QKeySequence::Redo);
  editMenu->addAction(redoAction);

  // View Menu
  m_viewMenu = menuBar()->addMenu("&View");

  m_zoomInAction = m_viewMenu->addAction("Zoom &In");
  m_zoomInAction->setIcon(getSymbolicIcon(":/icons/zoom-in.svg"));
  m_zoomInAction->setShortcut(QKeySequence::ZoomIn); // Ctrl++
  connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);

  m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
  m_zoomOutAction->setIcon(getSymbolicIcon(":/icons/zoom-out.svg"));
  m_zoomOutAction->setShortcut(QKeySequence::ZoomOut); // Ctrl+-
  m_zoomOutAction->setStatusTip(tr("Zoom out"));
  connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
  m_viewMenu->addAction(m_zoomOutAction);

  m_zoom100Action = new QAction(tr("Zoom &1:1"), this);
  m_zoom100Action->setIcon(getSymbolicIcon(":/icons/zoom-100.svg"));
  m_zoom100Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  m_zoom100Action->setStatusTip(tr("Zoom to 100%"));
  connect(m_zoom100Action, &QAction::triggered, this, &MainWindow::onZoom100);
  m_viewMenu->addAction(m_zoom100Action);

  m_zoomFitAction = new QAction(tr("Fit to &Screen"), this);
  m_zoomFitAction->setIcon(getSymbolicIcon(":/icons/zoom-fit.svg"));
  m_zoomFitAction->setShortcut(Qt::CTRL | Qt::Key_0);
  connect(m_zoomFitAction, &QAction::triggered, this, &MainWindow::onZoomFit);
  m_viewMenu->addAction(m_zoomFitAction);

  // Gamut Warning Action
  m_gamutWarningAction = new QAction(tr("Gamut Warning"), this);
  m_gamutWarningAction->setCheckable(true);
  m_gamutWarningAction->setChecked(false);
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
  connect(m_rotateLeftAction, &QAction::triggered, this,
          &MainWindow::rotateLeft);

  m_rotateRightAction = m_viewMenu->addAction("Rotate &Right");
  m_rotateRightAction->setShortcut(Qt::CTRL | Qt::Key_R);
  connect(m_rotateRightAction, &QAction::triggered, this,
          &MainWindow::rotateRight);
          
  m_mirrorAction = m_viewMenu->addAction(getSymbolicIcon(":/icons/mirror.svg"), "Mirror \u0026Horizontally");
  m_mirrorAction->setCheckable(true);
  connect(m_mirrorAction, &QAction::triggered, this, &MainWindow::onMirrorHorizontally);

  m_viewMenu->addSeparator();

  m_fullscreenAction = m_viewMenu->addAction("&Fullscreen");
  m_fullscreenAction->setCheckable(true);
  m_fullscreenAction->setShortcut(Qt::Key_F11);
  connect(m_fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);

  // Registration Menu
  m_registrationMenu = menuBar()->addMenu("&Registration");

  m_lockRelativeCoordinatesAction = new QAction(QIcon::fromTheme("system-lock-screen-symbolic"), tr("Lock relative coordinates"), this);
  m_lockRelativeCoordinatesAction->setCheckable(true);
  m_lockRelativeCoordinatesAction->setChecked(true); // Default ON
  connect(m_lockRelativeCoordinatesAction, &QAction::toggled, m_imageWidget, &ImageWidget::setLockRelativeCoordinates);
  m_registrationMenu->addAction(m_lockRelativeCoordinatesAction);

  m_registrationPointsAction = new QAction(tr("Show Registration &Points"), this);
  m_registrationPointsAction->setCheckable(true);
  m_registrationPointsAction->setChecked(false);
  
  // Visibility toggle at the top
  m_registrationMenu->addAction(m_registrationPointsAction);
  m_registrationMenu->addSeparator();

  m_selectAllAction = m_registrationMenu->addAction("Select &All");
  m_selectAllAction->setShortcut(QKeySequence::SelectAll); // Ctrl+A
  connect(m_selectAllAction, &QAction::triggered, this, &MainWindow::onSelectAll);

  m_deselectAllAction = m_registrationMenu->addAction("&Deselect All");
  m_deselectAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
  connect(m_deselectAllAction, &QAction::triggered, this, &MainWindow::onDeselectAll);

  m_deleteSelectedAction = m_registrationMenu->addAction("&Remove Selected Points");
  m_deleteSelectedAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
  connect(m_deleteSelectedAction, &QAction::triggered, this, &MainWindow::onDeleteSelected);

  m_pruneMisplacedAction = m_registrationMenu->addAction("&Prune Misplaced Points");
  m_pruneMisplacedAction->setShortcuts({QKeySequence("Ctrl+Delete"), QKeySequence("Ctrl+Backspace")});
  connect(m_pruneMisplacedAction, &QAction::triggered, this, &MainWindow::onPruneMisplaced);

  m_registrationMenu->addSeparator();

  m_optimizeGeometryAction = m_registrationMenu->addAction("&Optimize Geometry");
  connect(m_optimizeGeometryAction, &QAction::triggered, this, [this]() {
    onOptimizeGeometry(m_autoOptimizeAction->isChecked());
  });

  m_autoOptimizeAction = new QAction(tr("Auto &Optimize"), this);
  m_autoOptimizeAction->setCheckable(true);
  m_autoOptimizeAction->setChecked(false);
  m_registrationMenu->addAction(m_autoOptimizeAction);

  m_registrationMenu->addSeparator();

  // Connect toggle to update tool state
  connect(m_registrationPointsAction, &QAction::toggled, this, &MainWindow::updateRegistrationActions);

  m_optimizeCoordinatesAction = new QAction(QIcon::fromTheme("system-run"), tr("Optimize Coordinates"), this);
  m_optimizeCoordinatesAction->setToolTip("Optimize Coordinates");
  connect(m_optimizeCoordinatesAction, &QAction::triggered, this, &MainWindow::onOptimizeCoordinates);
}

void MainWindow::onOpenParameters() {
  // Check for unsaved changes before loading new parameters
  if (!maybeSave()) {
    return;
  }
  
  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Parameters", QString(), "Parameters (*.par);;All Files (*)");
  if (fileName.isEmpty())
    return;

  FILE *f = fopen(fileName.toUtf8().constData(), "r");
  if (!f) {
    QMessageBox::critical(this, "Error", "Could not open file.");
    return;
  }

  const char *error = nullptr;


  // Store previous state for comparison
  ParameterState old = {m_rparams, m_scrToImgParams, m_detectParams, m_solverParams};

  // load_csp merges parameters in; reset first.
  colorscreen::scr_to_img_parameters emptyScrToImg;
  m_scrToImgParams = emptyScrToImg;
  colorscreen::scr_detect_parameters emptyScrDetect;
  m_detectParams = emptyScrDetect;
  colorscreen::render_parameters emptyRparams;
  m_rparams = emptyRparams;
  colorscreen::solver_parameters emptySolver;
  m_solverParams = emptySolver;

  if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams, &m_rparams,
                             &m_solverParams, &error)) {
    fclose(f);
    QString errStr =
        error ? QString::fromUtf8(error) : "Unknown error loading parameters.";
    QMessageBox::critical(this, "Error Loading Parameters", errStr);
    m_scrToImgParams = old.scrToImg;
    m_detectParams = old.detect;
    m_rparams = old.rparams;
    m_solverParams = old.solver;
    return;
  }
  fclose(f);

  ParameterState new_params = {m_rparams, m_scrToImgParams, m_detectParams, m_solverParams};
  bool changed = old != new_params;

  if (changed) {
    if (m_scan) {
      // Re-set image to re-create renderer with new params
      m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams,
                              &m_solverParams);
    }
  }
  m_undoStack->clear();
  updateModeMenu();
  updateUIFromState(getCurrentState());

  addToRecentParams(fileName);
  m_currentParamsFile = fileName; // Track current file for Save
}

void MainWindow::onSaveParameters() {
  // If we don't have a current file OR it's a weak suggestion, fall back to Save As
  if (m_currentParamsFile.isEmpty() || m_currentParamsFileIsWeak) {
    onSaveParametersAs();
    return;
  }

  FILE *f = fopen(m_currentParamsFile.toUtf8().constData(), "wt");
  if (!f) {
    QMessageBox::critical(this, "Error", 
                          QString("Could not open file for writing: %1").arg(m_currentParamsFile));
    return;
  }

  // Save parameters using save_csp
  // Pass scan detection params only if we have RGB data (matching GTK behavior)
  bool has_rgb = m_scan && m_scan->has_rgb();
  if (!colorscreen::save_csp(f, &m_scrToImgParams, 
                             has_rgb ? &m_detectParams : nullptr,
                             &m_rparams, &m_solverParams)) {
    fclose(f);
    QMessageBox::critical(this, "Error", "Failed to save parameters.");
    return;
  }

  fclose(f);
  statusBar()->showMessage(QString("Parameters saved to %1").arg(m_currentParamsFile), 3000);
  
  // Mark as saved (clean state)
  if (m_undoStack) {
    m_undoStack->setClean();
  }
}

void MainWindow::onSaveParametersAs() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Save Parameters", m_currentParamsFile.isEmpty() ? QString() : m_currentParamsFile,
      "Parameters (*.par);;All Files (*)");
  
  if (fileName.isEmpty())
    return;

  // Add .par extension if not present
  if (!fileName.endsWith(".par", Qt::CaseInsensitive)) {
    fileName += ".par";
  }

  FILE *f = fopen(fileName.toUtf8().constData(), "wt");
  if (!f) {
    QMessageBox::critical(this, "Error", 
                          QString("Could not open file for writing: %1").arg(fileName));
    return;
  }

  // Save parameters using save_csp
  // Pass scan detection params only if we have RGB data (matching GTK behavior)
  bool has_rgb = m_scan && m_scan->has_rgb();
  if (!colorscreen::save_csp(f, &m_scrToImgParams, 
                             has_rgb ? &m_detectParams : nullptr,
                             &m_rparams, &m_solverParams)) {
    fclose(f);
    QMessageBox::critical(this, "Error", "Failed to save parameters.");
    return;
  }

  fclose(f);
  
  // Update current file path and add to recent
  m_currentParamsFile = fileName;
  m_currentParamsFileIsWeak = false; // Now it's a real file, not a suggestion
  addToRecentParams(fileName);
  
  statusBar()->showMessage(QString("Parameters saved to %1").arg(fileName), 3000);
  
  // Mark as saved (clean state)
  if (m_undoStack) {
    m_undoStack->setClean();
  }
}


void MainWindow::onOpenImage() {
  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Image", QString(),
      "Images (*.tif *.tiff *.jpg *.jpeg *.raw *.dng *.iiq *.nef *.NEF *.cr2 "
      "*.CR2 *.eip *.arw *.ARW *.raf *.RAF *.arq *.ARQ *.csprj);;All Files "
      "(*)");
  if (fileName.isEmpty())
    return;

  loadFile(fileName);
}

void MainWindow::addProgress(std::shared_ptr<colorscreen::progress_info> info) {
  ProgressEntry entry;
  entry.info = info;
  entry.startTime.start();
  m_activeProgresses.push_back(entry);

  if (!m_progressTimer->isActive()) {
    m_progressTimer->start();
  }
}

void MainWindow::removeProgress(
    std::shared_ptr<colorscreen::progress_info> info) {
  int removedIndex = -1;
  for (auto it = m_activeProgresses.begin(); it != m_activeProgresses.end();
       ++it) {
    if (it->info == info) {
      removedIndex = std::distance(m_activeProgresses.begin(), it);
      m_activeProgresses.erase(it);
      break;
    }
  }

  // Clear the currently displayed progress if it was the one we just removed
  if (m_currentlyDisplayedProgress == info) {
    m_currentlyDisplayedProgress.reset();
  }

  // Handle manual selection when progress is removed
  if (removedIndex >= 0) {
    if (m_manuallySelectedProgressIndex == removedIndex) {
      // The manually selected progress was removed, reset to auto-select
      m_manuallySelectedProgressIndex = -1;
    } else if (m_manuallySelectedProgressIndex > removedIndex) {
      // Adjust index if progress before the selected one was removed
      m_manuallySelectedProgressIndex--;
    }
  }

  if (m_activeProgresses.empty()) {
    m_progressTimer->stop();
    m_progressContainer->setVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
  }
}

ProgressEntry *MainWindow::getLongestRunningTask() {
  ProgressEntry *oldestActive = nullptr;
  ProgressEntry *oldestAny = nullptr;
  qint64 maxActiveTime = -1;
  qint64 maxAnyTime = -1;

  for (auto &entry : m_activeProgresses) {
    qint64 elapsed = entry.startTime.elapsed();

    // Check if this task has non-zero progress
    const char *tName = "";
    float percent = 0;
    entry.info->get_status(&tName, &percent);

    // Track oldest task with non-zero progress
    if (percent > 0 && elapsed > maxActiveTime) {
      maxActiveTime = elapsed;
      oldestActive = &entry;
    }

    // Also track oldest task overall as fallback
    if (elapsed > maxAnyTime) {
      maxAnyTime = elapsed;
      oldestAny = &entry;
    }
  }

  // Prefer task with non-zero progress, otherwise use oldest
  return oldestActive ? oldestActive : oldestAny;
}

void MainWindow::onProgressTimer() {
  if (m_activeProgresses.empty()) {
    // No active tasks, hide progress
    m_progressContainer->setVisible(false);
    m_currentlyDisplayedProgress.reset();
    m_manuallySelectedProgressIndex = -1;
    return;
  }

  ProgressEntry *task = nullptr;
  int currentIndex = 0;

  // Determine which progress to display
  if (m_manuallySelectedProgressIndex >= 0 &&
      m_manuallySelectedProgressIndex < (int)m_activeProgresses.size()) {
    // Use manually selected progress
    task = &m_activeProgresses[m_manuallySelectedProgressIndex];
    currentIndex = m_manuallySelectedProgressIndex;
  } else {
    // Auto-select: use default logic
    task = getLongestRunningTask();
    if (task) {
      // Find index of this task for display
      for (size_t i = 0; i < m_activeProgresses.size(); ++i) {
        if (&m_activeProgresses[i] == task) {
          currentIndex = i;
          break;
        }
      }
    }
    m_manuallySelectedProgressIndex = -1; // Reset manual selection
  }

  if (!task)
    return;

  // Track which progress is currently being displayed (for cancel button)
  m_currentlyDisplayedProgress = task->info;

  // Update progress count label
  m_progressCountLabel->setText(
      QString("%1/%2").arg(currentIndex + 1).arg(m_activeProgresses.size()));

  // Show/hide prev/next buttons based on count
  bool multipleProgresses = m_activeProgresses.size() > 1;
  m_prevProgressButton->setVisible(multipleProgresses);
  m_nextProgressButton->setVisible(multipleProgresses);
  m_progressCountLabel->setVisible(multipleProgresses);

  if (task->startTime.elapsed() > 300) {
    if (!m_progressContainer->isVisible()) {
      m_progressContainer->setVisible(true);
    }

    std::vector<colorscreen::progress_info::status> statusStack =
        task->info->get_status();

    if (statusStack.empty()) {
       m_statusLabel->setText("no progress info (yet)");
       m_progressBar->setValue(0);
       m_progressBar->setRange(0, 0);
    } else {
      QStringList tasks;
      float percent = -1;

      for (const auto &s : statusStack) {
        if (s.task && s.task[0]) {
           QString taskName = QString::fromUtf8(s.task);
           if (s.progress >= 0 && &s != &statusStack.back()) {
               taskName += QString(" (%1%)").arg((int)s.progress);
           }
           tasks.append(taskName);
        }
        // Use the progress of the most specific task that has progress
        if (s.progress >= 0) {
           percent = s.progress;
        }
      }

      m_statusLabel->setText(tasks.join(" > "));

      if (percent >= 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue((int)percent);
      } else {
        // Indeterminate progress (animated busy indicator)
        m_progressBar->setRange(0, 0); 
      }
    }
  }
}

void MainWindow::onCancelClicked() {
  // Cancel the currently displayed progress (not necessarily the longest
  // running)
  if (m_currentlyDisplayedProgress) {
    m_currentlyDisplayedProgress->cancel();
  }
}

void MainWindow::onPrevProgress() {
  if (m_activeProgresses.size() <= 1)
    return;

  // If not manually selected yet, start from current auto-selected index
  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = getLongestRunningTask();
    for (size_t i = 0; i < m_activeProgresses.size(); ++i) {
      if (&m_activeProgresses[i] == currentTask) {
        m_manuallySelectedProgressIndex = i;
        break;
      }
    }
  }

  // Go to previous
  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex - 1 + m_activeProgresses.size()) %
      m_activeProgresses.size();
}

void MainWindow::onNextProgress() {
  if (m_activeProgresses.size() <= 1)
    return;

  // If not manually selected yet, start from current auto-selected index
  if (m_manuallySelectedProgressIndex < 0) {
    ProgressEntry *currentTask = getLongestRunningTask();
    for (size_t i = 0; i < m_activeProgresses.size(); ++i) {
      if (&m_activeProgresses[i] == currentTask) {
        m_manuallySelectedProgressIndex = i;
        break;
      }
    }
  }

  // Go to next
  m_manuallySelectedProgressIndex =
      (m_manuallySelectedProgressIndex + 1) % m_activeProgresses.size();
}

void MainWindow::onImageLoaded() {
  // Update UI components that depend on loaded image
  updateModeMenu();
  if (m_scan) {
    if (m_solverWorker)
      m_solverWorker->setScan(m_scan);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    m_navigationView->setMinScale(m_imageWidget->getMinScale());
  }

  // Refresh param values too
  applyState(getCurrentState());
  updateRegistrationActions();
  updateRegistrationGroupVisibility();
}

// Recent Files Implementation

void MainWindow::addToRecentFiles(const QString &filePath) {
  m_recentFiles.removeAll(filePath);
  m_recentFiles.prepend(filePath);

  while (m_recentFiles.size() > MaxRecentFiles)
    m_recentFiles.removeLast();

  updateRecentFileActions();
  saveRecentFiles();
}

void MainWindow::updateRecentFileActions() {
  m_recentFilesMenu->clear();
  m_recentFileActions.clear();

  for (int i = 0; i < m_recentFiles.size(); ++i) {
    QString text =
        tr("&%1 %2").arg(i + 1).arg(QFileInfo(m_recentFiles[i]).fileName());
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

void MainWindow::openRecentFile() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (action) {
    QString fileName = action->data().toString();
    loadFile(fileName);
  }
}

void MainWindow::loadFile(const QString &fileName, bool suppressParamPrompt) {
  if (fileName.isEmpty())
    return;

  m_currentImageFile = fileName;

  // Clear current image and stop rendering
  m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  // Check for .par file (only if not suppressed, e.g., during recovery)
  if (!suppressParamPrompt) {
    QFileInfo fileInfo(fileName);
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
          if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams,
                                     &m_rparams, &m_solverParams, &error)) {
            QMessageBox::warning(this, "Error Loading Parameters",
                                 error ? QString::fromUtf8(error)
                                       : "Unknown error loading parameters.");
          } else {
            m_prevScrToImgParams = m_scrToImgParams;
            m_prevDetectParams = m_detectParams;
            
            // Track the loaded parameter file
            m_currentParamsFile = parFile;
            m_currentParamsFileIsWeak = false; // This is a real file, not a suggestion
            addToRecentParams(parFile);
            
            // If we have a valid screen type, default to formatted (interpolated) view
            if (m_scrToImgParams.type != colorscreen::Random) {
              m_renderTypeParams.type = colorscreen::render_type_interpolated;
            }
          }
          fclose(f);
        }
      } else {
        // User declined to load parameters - suggest filename
        QFileInfo fileInfo(fileName);
        m_currentParamsFile = fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
        m_currentParamsFileIsWeak = true;
      }
    } else {
      // No parameter file exists - suggest filename
      QFileInfo fileInfo(fileName);
      m_currentParamsFile = fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
      m_currentParamsFileIsWeak = true;
    }
  }

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Opening image", 0);
  addProgress(progress);

  std::shared_ptr<colorscreen::image_data> tempScan =
      std::make_shared<colorscreen::image_data>();
  // Access m_rparams carefully. It's a member.
  colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;

  QFutureWatcher<std::pair<bool, QString>> *watcher =
      new QFutureWatcher<std::pair<bool, QString>>(this);
  connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
          [this, watcher, tempScan, progress, fileName]() {
            std::pair<bool, QString> result = watcher->result();
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

              m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                      &m_detectParams, &m_renderTypeParams,
                                      &m_solverParams);
              onImageLoaded();

              // Add to recent files
              addToRecentFiles(fileName);

            } else {
              if (progress->cancelled()) {
              } else {
                QMessageBox::critical(this, "Error Loading Image",
                                      result.second.isEmpty()
                                          ? "Failed to load image."
                                          : result.second);
              }
            }
          });

  QFuture<std::pair<bool, QString>> future =
      QtConcurrent::run([tempScan, fileName, progress, demosaic]() {
        const char *error = nullptr;
	colorscreen::sub_task task (progress.get ());
        bool res = tempScan->load(fileName.toUtf8().constData(), true, &error,
                                  progress.get(), demosaic);
        QString errStr;
        if (!res && error) {
          errStr = QString::fromUtf8(error);
        }
        return std::make_pair(res, errStr);
      });

  watcher->setFuture(future);
}

void MainWindow::loadRecentFiles() {
  QSettings settings;
  m_recentFiles = settings.value("recentFiles").toStringList();
  updateRecentFileActions();
}

void MainWindow::saveRecentFiles() {
  QSettings settings;
  settings.setValue("recentFiles", m_recentFiles);
}

// Undo/Redo Implementation

void MainWindow::applyState(const ParameterState &state) {
  // User requested rotation is not part of parameters.
  // Preserve current rotation when applying state.
  m_rparams = state.rparams;
  m_scrToImgParams = state.scrToImg;
  m_detectParams = state.detect;
  m_solverParams = state.solver; // Manually copy logic if needed? Struct copy
                                 // should work if fields are copyable.
  // solver_parameters has vector, copy constructor should be fine
  // (std::vector).

  // Update widgets - use updateParameters to avoid blocking
  if (m_scan) {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                       &m_detectParams);
  }

  updateUIFromState(state);
  updateRegistrationActions();
  updateModeMenu();
}

void MainWindow::updateUIFromState(const ParameterState &state) {
  for (auto panel : m_panels) {
    if (panel)
      panel->updateUI();
  }
  // Sync mirror action
  if (m_mirrorAction) {
    m_mirrorAction->setChecked(state.rparams.scan_mirror);
  }

  // Sync nonlinear checkbox in GeometryPanel
  // Sync nonlinear checkbox in GeometryPanel
  QCheckBox *nlBox = m_geometryPanel->findChild<QCheckBox *>("nonLinearBox");
  if (nlBox) {
    QSignalBlocker blocker(nlBox);
    nlBox->setChecked(state.scrToImg.mesh_trans != nullptr);
  }
  
  // Update deformation chart
  if (m_geometryPanel) {
    m_geometryPanel->updateDeformationChart();
  }

  // Handle backlight dock visibility
  if (m_backlightDock) {
    bool hasBacklight = state.rparams.backlight_correction != nullptr;
    m_backlightDock->setVisible(hasBacklight && m_backlightDock->widget() != nullptr);
  }
  
  updateRegistrationGroupVisibility();
}

ParameterState MainWindow::getCurrentState() const {
  ParameterState state;
  state.rparams = m_rparams;
  state.scrToImg = m_scrToImgParams;
  state.detect = m_detectParams;
  state.solver = m_solverParams;
  return state;
}

void MainWindow::changeParameters(const ParameterState &newState, const QString &description) {
  ParameterState currentState = getCurrentState();
  if (currentState == newState)
    return;

  m_undoStack->push(new ChangeParametersCommand(this, currentState, newState, description));
}

// Helper to check for unsaved changes and prompt to save
bool MainWindow::maybeSave() {
  // Only prompt if there are unsaved changes (undo stack is not clean)
  if (!m_undoStack || m_undoStack->isClean()) {
    return true; // No changes or no undo stack, proceed
  }

  QMessageBox::StandardButton ret = QMessageBox::warning(
      this, "Unsaved Changes",
      "Parameters have been modified.\nDo you want to save your changes?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

  switch (ret) {
  case QMessageBox::Save:
    onSaveParameters();
    // If save was successful or user cancelled save dialog, we're clean
    return true;
  case QMessageBox::Discard:
    return true;
  case QMessageBox::Cancel:
  default:
    return false;
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // Check for unsaved changes
  if (!maybeSave()) {
    event->ignore();
    return;
  }
  
  // Cancel all active processes
  for (const auto &progress : m_activeProgresses) {
      if (progress.info) {
          progress.info->cancel();
      }
  }

  // Clean up recovery files on normal exit
  clearRecoveryFiles();
  
  saveWindowState();
  saveRecentFiles();
  saveRecentParams();
  event->accept();
}

void MainWindow::saveWindowState() {
  QSettings settings;

  // Save window geometry and state
  // Save window geometry and state
  settings.setValue("windowGeometry", saveGeometry());
  settings.setValue("windowState", saveState());

  // Save desktop size for validation on restore
  QScreen *screen = QApplication::primaryScreen();
  if (screen) {
    settings.setValue("desktopSize", screen->availableGeometry().size());
  }

  // Save splitter positions
  if (m_mainSplitter) {
    settings.setValue("mainSplitterState", m_mainSplitter->saveState());
  }
}

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

    // Fix for docks showing up empty if restored as visible but content is in
    // panel (not detached)
    auto fixDockVisibility = [](QDockWidget *dock) {
      if (dock && dock->widget() == nullptr) {
        dock->hide();
        dock->setFloating(false); // Ensure it's not floating empty
      }
    };
    fixDockVisibility(m_mtfDock);
    fixDockVisibility(m_tilesDock);
    fixDockVisibility(m_colorTilesDock);
    fixDockVisibility(m_correctedColorTilesDock);
    
    // Add missing chart docks
    fixDockVisibility(m_spectraDock);
    fixDockVisibility(m_deformationDock);
    fixDockVisibility(m_lensDock);
    fixDockVisibility(m_perspectiveDock);
    fixDockVisibility(m_nonlinearDock);
    fixDockVisibility(m_screenPreviewDock);
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

void MainWindow::addToRecentParams(const QString &filePath) {
  m_recentParams.removeAll(filePath);
  m_recentParams.prepend(filePath);

  while (m_recentParams.size() > MaxRecentFiles)
    m_recentParams.removeLast();

  updateRecentParamsActions();
  saveRecentParams();
}

void MainWindow::updateRecentParamsActions() {
  m_recentParamsMenu->clear();
  m_recentParamsActions.clear();

  for (int i = 0; i < m_recentParams.size(); ++i) {
    QString text =
        tr("&%1 %2").arg(i + 1).arg(QFileInfo(m_recentParams[i]).fileName());
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

void MainWindow::openRecentParams() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (action) {
    QString fileName = action->data().toString();

    FILE *f = fopen(fileName.toUtf8().constData(), "r");
    if (!f) {
      QMessageBox::critical(this, "Error", "Could not open file.");
      return;
    }

    const char *error = nullptr;

    // Store previous state
    colorscreen::scr_to_img_parameters oldScrToImg = m_scrToImgParams;
    colorscreen::scr_detect_parameters oldDetect = m_detectParams;

    // load_csp merges parameters in; reset first.
    colorscreen::scr_to_img_parameters emptyScrToImg;
    m_scrToImgParams = emptyScrToImg;
    colorscreen::scr_detect_parameters emptyScrDetect;
    m_detectParams = emptyScrDetect;
    colorscreen::render_parameters emptyRparams;
    m_rparams = emptyRparams;
    colorscreen::solver_parameters emptySolver;
    m_solverParams = emptySolver;
    if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams,
                               &m_rparams, &m_solverParams, &error)) {
      fclose(f);
      QString errStr = error ? QString::fromUtf8(error)
                             : "Unknown error loading parameters.";
      QMessageBox::critical(this, "Error Loading Parameters", errStr);
      return;
    }
    fclose(f);

    // Update UI/Renderer
    bool changed = false;
    if (!(oldScrToImg == m_scrToImgParams))
      changed = true;
    if (!(oldDetect == m_detectParams))
      changed = true;

    if (m_scan) {
      m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams,
                              &m_solverParams);
      // Also update navigation view image
      m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                 &m_detectParams);

      m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                 &m_detectParams);

      updateColorCheckBoxState();
    }

    // Sync Gamut Warning Button
    if (m_gamutWarningAction) {
      bool signalBlocked = m_gamutWarningAction->blockSignals(true);
      m_gamutWarningAction->setChecked(m_rparams.gammut_warning);
      m_gamutWarningAction->blockSignals(signalBlocked);
    }

    m_undoStack->clear();
    updateModeMenu();
    updateUIFromState(getCurrentState());

    addToRecentParams(fileName);
  }
}

void MainWindow::loadRecentParams() {
  QSettings settings;
  m_recentParams = settings.value("recentParams").toStringList();
  updateRecentParamsActions();
}

void MainWindow::saveRecentParams() {
  QSettings settings;
  settings.setValue("recentParams", m_recentParams);
}

void MainWindow::onZoomIn() {
  double currentZoom = m_imageWidget->getZoom();
  m_imageWidget->setZoom(currentZoom * 1.25);
}

void MainWindow::onZoomOut() {
  if (m_imageWidget) {
    double currentZoom = m_imageWidget->getZoom();
    m_imageWidget->setZoom(currentZoom / 1.1); // Zoom out by 10%
  }
}

void MainWindow::onZoom100() {
  if (m_imageWidget) {
    m_imageWidget->setZoom(1.0);
  }
}
void MainWindow::onNonlinearToggled(bool checked) {
  if (checked) {
    // If not already set, trigger optimization
    if (!m_scrToImgParams.mesh_trans) {
       onOptimizeGeometry(false); // pass false for Auto assuming button is manual
    }
  } else {
    // If set, clear it
    if (m_scrToImgParams.mesh_trans) {
      ParameterState newState = getCurrentState();
      newState.scrToImg.mesh_trans = nullptr;
      changeParameters(newState, "Disable Nonlinear Corrections");
    }
  }
}

void MainWindow::onZoomFit() { m_imageWidget->fitToView(); }

void MainWindow::onRegistrationPointsToggled(bool checked) {
  m_imageWidget->setShowRegistrationPoints(checked);
}

void MainWindow::onOptimizeGeometry(bool autoChecked) {
  if (!m_scan || !m_solverWorker)
    return;

  // Request new solve task
  m_solverQueue.requestRender();
}

void MainWindow::onTriggerSolve(int reqId, std::shared_ptr<colorscreen::progress_info> progress) {
  if (!m_scan || !m_solverWorker) {
    m_solverQueue.reportFinished(reqId, false);
    return;
  }
  
  // Update progress info
  if (progress) {
      progress->set_task("Optimizing geometry", 100);
  }

  // Check if nonlinear corrections are enabled
  bool computeMesh = m_geometryPanel->isNonlinearEnabled();
  
  // Invoke solver in worker
  QMetaObject::invokeMethod(
      m_solverWorker, "solve", Qt::QueuedConnection, Q_ARG(int, reqId),
      Q_ARG(colorscreen::scr_to_img_parameters, m_scrToImgParams),
      Q_ARG(colorscreen::solver_parameters, m_solverParams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
      Q_ARG(bool, computeMesh));
}

void MainWindow::onSolverFinished(int reqId, colorscreen::scr_to_img_parameters result, bool success) {
  // Report back to queue
  m_solverQueue.reportFinished(reqId, success);

  if (success) {
    ParameterState newState = getCurrentState();
    newState.scrToImg.merge_solver_solution(result);
    changeParameters(newState, "Optimize Geometry");
  } else {
    QMessageBox::warning(this, "Optimization Failed", "The geometry solver failed to find a solution.");
  }
}

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

void MainWindow::updateRegistrationGroupVisibility() {
  // Show registration group only if:
  // 1. Image is loaded
  // 2. Screen type is not Random
  bool shouldShow = m_scan && (m_scrToImgParams.type != colorscreen::Random);
  
  for (QAction *action : m_registrationActions) {
    action->setVisible(shouldShow);
  }
  
  // If hiding and a registration tool is active, switch to Pan
  if (!shouldShow && (m_selectAction->isChecked() || 
                      m_addPointAction->isChecked() || 
                      m_setCenterAction->isChecked())) {
    m_panAction->setChecked(true);
  }
}

void MainWindow::onGamutWarningToggled(bool checked) {
  if (m_rparams.gammut_warning != checked) {
    m_rparams.gammut_warning = checked;

    // Trigger update
    if (m_scan) {
      m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                      &m_detectParams, &m_renderTypeParams,
                                      &m_solverParams);
    }
  }
}

// Crash Recovery Methods

bool MainWindow::hasRecoveryFiles() {
  QString imagePath = m_recoveryDir + "/recovery_image.txt";
  QString paramsPath = m_recoveryDir + "/recovery_params.par";
  return QFile::exists(imagePath) || QFile::exists(paramsPath);
}

void MainWindow::saveRecoveryState() {
  // Only save if we have an image loaded
  if (!m_scan) {
    return;
  }

  // Save current image path
  QString imagePath = m_recoveryDir + "/recovery_image.txt";
  QFile imageFile(imagePath);
  if (imageFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&imageFile);
    out << m_currentImageFile;
    imageFile.close();
  }

  // Save current parameters
  QString paramsPath = m_recoveryDir + "/recovery_params.par";
  FILE *f = fopen(paramsPath.toUtf8().constData(), "wt");
  if (f) {
    bool has_rgb = m_scan && m_scan->has_rgb();
    colorscreen::save_csp(f, &m_scrToImgParams,
                          has_rgb ? &m_detectParams : nullptr,
                          &m_rparams, &m_solverParams);
    fclose(f);
  }
  
  // Save parameter file path and weak flag
  QString paramsMetaPath = m_recoveryDir + "/recovery_params_meta.txt";
  QFile metaFile(paramsMetaPath);
  if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&metaFile);
    out << m_currentParamsFile << "\n";
    out << (m_currentParamsFileIsWeak ? "1" : "0") << "\n";
    metaFile.close();
  }
}

void MainWindow::loadRecoveryState() {
  // Load image path
  QString imagePath = m_recoveryDir + "/recovery_image.txt";
  QString imageToLoad;
  
  QFile imageFile(imagePath);
  if (imageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&imageFile);
    imageToLoad = in.readLine().trimmed();
    imageFile.close();
  }

  // Load parameters
  QString paramsPath = m_recoveryDir + "/recovery_params.par";
  if (QFile::exists(paramsPath)) {
    FILE *f = fopen(paramsPath.toUtf8().constData(), "r");
    if (f) {
      const char *error = nullptr;
      colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams,
                            &m_rparams, &m_solverParams, &error);
      fclose(f);
      
      if (error) {
        QMessageBox::warning(this, "Recovery Warning",
                            QString("Error loading parameters: %1").arg(error));
      }
    }
  }
  
  // Load parameter file path and weak flag
  QString paramsMetaPath = m_recoveryDir + "/recovery_params_meta.txt";
  QFile metaFile(paramsMetaPath);
  if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&metaFile);
    m_currentParamsFile = in.readLine().trimmed();
    QString weakFlag = in.readLine().trimmed();
    m_currentParamsFileIsWeak = (weakFlag == "1");
    metaFile.close();
  }

  // Load image if path was saved
  if (!imageToLoad.isEmpty() && QFile::exists(imageToLoad)) {
    loadFile(imageToLoad, true);  // Suppress param prompt during recovery
  } else if (!imageToLoad.isEmpty()) {
    QMessageBox::warning(this, "Recovery Warning",
                        QString("Could not find image file: %1").arg(imageToLoad));
  }

  // Update UI with recovered parameters
  updateUIFromState(getCurrentState());
}

void MainWindow::clearRecoveryFiles() {
  QString imagePath = m_recoveryDir + "/recovery_image.txt";
  QString paramsPath = m_recoveryDir + "/recovery_params.par";
  
  QFile::remove(imagePath);
  QFile::remove(paramsPath);
}
void MainWindow::onSelectAll() {
  m_imageWidget->selectAll();
}

void MainWindow::onDeselectAll() {
  m_imageWidget->clearSelection();
}

void MainWindow::onDeleteSelected() {
  m_imageWidget->deleteSelectedPoints();
}

void MainWindow::onPruneMisplaced() {
  if (!m_scan || !m_imageWidget) {
    return;
  }
  
  const auto &selectedPoints = m_imageWidget->selectedPoints();
  if (selectedPoints.empty()) {
    return;
  }
  
  // Create map for current geometry
  colorscreen::scr_to_img map;
  map.set_parameters(m_scrToImgParams, *m_scan);
  
  // Build histogram of distances
  colorscreen::histogram hist;
  
  // First pass: pre-account all distances
  for (const auto &sp : selectedPoints) {
    if (sp.type == ImageWidget::SelectedPoint::RegistrationPoint &&
        sp.index < m_solverParams.points.size()) {
      const auto &point = m_solverParams.points[sp.index];
      
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
        sp.index < m_solverParams.points.size()) {
      const auto &point = m_solverParams.points[sp.index];
      
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
        sp.index < m_solverParams.points.size()) {
      const auto &point = m_solverParams.points[sp.index];
      
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
  std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<size_t>());
  for (size_t idx : indicesToRemove) {
    m_solverParams.remove_point(idx);
  }
  
  // Update UI
  m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams, &m_solverParams);
  m_imageWidget->clearSelection();
  m_imageWidget->update();
  
  // Create undo command
  ParameterState newState = getCurrentState();
  m_undoStack->push(new ChangeParametersCommand(this, oldState, newState, "Prune misplaced points"));
  
  // Trigger auto solver if enabled
  if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
    size_t count = m_imageWidget->registrationPointCount();
    if (count >= 3) {
      onOptimizeGeometry(true);
    }
  }
  updateRegistrationActions();
}

void MainWindow::updateRegistrationActions() {
  bool hasPoints = m_imageWidget && m_imageWidget->registrationPointsVisible() && m_imageWidget->registrationPointCount() > 0;
  bool hasSelection = m_imageWidget && !m_imageWidget->selectedPoints().empty();
  
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
  
  // Disable Add Point and Set Center tools when screen type is Random or no scan loaded
  if (m_addPointAction) {
    bool canAddPoints = m_scan && m_scrToImgParams.type != colorscreen::Random;
    m_addPointAction->setEnabled(canAddPoints);
    // If tool is active but we can't add points, switch to Pan mode
    if (!canAddPoints && m_addPointAction->isChecked()) {
      m_panAction->setChecked(true);
    }
  }
  if (m_setCenterAction) {
    bool canSetCenter = m_scan && m_scrToImgParams.type != colorscreen::Random;
    m_setCenterAction->setEnabled(canSetCenter);
    // If tool is active but we can't set center, switch to Pan mode
    if (!canSetCenter && m_setCenterAction->isChecked()) {
      m_panAction->setChecked(true);
    }
  }
  
  // Disable/enable optimize geometry and select all based on point count
  size_t count = m_imageWidget ? m_imageWidget->registrationPointCount() : 0;
  if (m_selectAllAction) {
    m_selectAllAction->setEnabled(count > 0);
  }
  if (m_optimizeGeometryAction) {
    m_optimizeGeometryAction->setEnabled(count >= 3);
  }

  // Update buttons in GeometryPanel
  if (m_geometryPanel) {
    QPushButton *optBtn = m_geometryPanel->findChild<QPushButton *>("optimizeButton");
    if (optBtn) optBtn->setEnabled(count >= 3);

    QCheckBox *nlBox = m_geometryPanel->findChild<QCheckBox *>("nonlinearBox");
    if (nlBox) nlBox->setEnabled(count >= 5);
  }
}

void MainWindow::onPointManipulationStarted() {
  m_undoSnapshot = getCurrentState();
}

void MainWindow::maybeTriggerAutoSolver() {
  ParameterState newState = getCurrentState();
  if (newState != m_undoSnapshot) {
    m_undoStack->push(new ChangeParametersCommand(this, m_undoSnapshot, newState, "Move registration point"));
    m_undoSnapshot = newState;
  }

  if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
    size_t count = m_imageWidget->registrationPointCount();
    if (count >= 3) {
      onOptimizeGeometry(true); // Trigger solver (auto=true)
    }
  }
  updateRegistrationActions();
}

void MainWindow::onPointAdded(colorscreen::point_t imgPos, colorscreen::point_t scrPos, colorscreen::point_t color) {
  if (!m_scan) {
    return;
  }

  // Run finetune to get the accurate screen location and color
  colorscreen::finetune_parameters fparam;
  fparam.multitile = 3;
  fparam.flags |= colorscreen::finetune_position | colorscreen::finetune_bw | colorscreen::finetune_verbose | colorscreen::finetune_use_srip_widths;
  
  auto progress = std::make_shared<colorscreen::progress_info>();
  addProgress(progress);
  
  colorscreen::finetune_result res = colorscreen::finetune(m_rparams, m_scrToImgParams, *m_scan, 
                                                            {{imgPos.x, imgPos.y}}, nullptr, fparam, progress.get());
  
  removeProgress(progress);
  
  if (res.success) {
    // Snapshot state for undo
    ParameterState oldState = getCurrentState();
    
    // Add the point to solver parameters
    m_solverParams.add_point(res.solver_point_img_location, res.solver_point_screen_location, res.solver_point_color);
    
    // Update the image widget
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams, &m_solverParams);
    m_imageWidget->update();
    
    // Create undo command with correct description
    ParameterState newState = getCurrentState();
    m_undoStack->push(new ChangeParametersCommand(this, oldState, newState, "Add registration point"));
    
    // Trigger auto solver if enabled
    if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
      size_t count = m_imageWidget->registrationPointCount();
      if (count >= 3) {
        onOptimizeGeometry(true);
      }
    }
    updateRegistrationActions();
  }
}

void MainWindow::onCropRequested() {
  if (!m_scan) return;

  if (m_imageWidget->interactionMode() == ImageWidget::CropMode) {
      m_imageWidget->setInteractionMode(ImageWidget::PanMode);
      statusBar()->clearMessage();
      return;
  }

  // Preserve center across crop state change
  colorscreen::point_t center = m_imageWidget->widgetToImage(m_imageWidget->rect().center());

  ParameterState state = getCurrentState();
  if (state.rparams.scan_crop.set) {
      state.rparams.scan_crop.set = false;
      changeParameters(state, "Clear crop for re-cropping");
      m_imageWidget->centerOn(center);
  }

  m_imageWidget->setInteractionMode(ImageWidget::CropMode);
  statusBar()->showMessage("Select crop");
}

QRect MainWindow::getImageArea(QRect area) {
  if (!m_scan) return QRect();

  // Convert widget coordinates to image coordinates
  // Get the four corners and find min/max
  colorscreen::point_t p1 = m_imageWidget->widgetToImage(area.topLeft());
  colorscreen::point_t p2 = m_imageWidget->widgetToImage(area.topRight());
  colorscreen::point_t p3 = m_imageWidget->widgetToImage(area.bottomLeft());
  colorscreen::point_t p4 = m_imageWidget->widgetToImage(area.bottomRight());
  
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

void MainWindow::onAreaSelected(QRect area) {
  if (!m_scan) {
    return;
  }

  QRect imgArea = getImageArea(area);
  if (imgArea.width() <= 0 || imgArea.height() <= 0) return;

  if (m_imageWidget->interactionMode() == ImageWidget::CropMode) {
      // Preserve center
      colorscreen::point_t center = m_imageWidget->widgetToImage(m_imageWidget->rect().center());

      ParameterState state = getCurrentState();
      state.rparams.scan_crop.x = imgArea.x();
      state.rparams.scan_crop.y = imgArea.y();
      state.rparams.scan_crop.width = imgArea.width();
      state.rparams.scan_crop.height = imgArea.height();
      state.rparams.scan_crop.set = true;

      changeParameters(state, "Set Crop Area");
      
      // Keep center
      m_imageWidget->centerOn(center);

      m_imageWidget->setInteractionMode(ImageWidget::PanMode);
      statusBar()->clearMessage();
      return;
  }
  
  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Finding registration points", 100);
  addProgress(progress);
  
  // Create worker and thread
  FinetuneWorker *worker = new FinetuneWorker(m_solverParams, m_rparams, m_scrToImgParams,
                                              m_scan, imgArea.left(), imgArea.top(), imgArea.right(), imgArea.bottom(), progress);
  QThread *thread = new QThread();
  worker->moveToThread(thread);
  
  // Connect signals
  connect(thread, &QThread::started, worker, &FinetuneWorker::run);
  connect(worker, &FinetuneWorker::finished, thread, &QThread::quit);
  connect(worker, &FinetuneWorker::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  
  // Connect to our slot to handle results
  connect(worker, &FinetuneWorker::pointsReady, this,
          [this, thread, progress](std::vector<colorscreen::solver_parameters::solver_point_t> points) {
            onFinetuneFinished(true, points, thread, progress);
          });
  connect(worker, &FinetuneWorker::finished, this,
          [this, thread, progress](bool success) {
            if (!success) {
              onFinetuneFinished(false, {}, thread, progress);
            }
          });
  
  // Track thread
  m_finetuneThreads.push_back(thread);
  
  // Start thread
  thread->start();
}

void MainWindow::onFinetuneFinished(bool success, std::vector<colorscreen::solver_parameters::solver_point_t> points,
                                    QThread *thread, std::shared_ptr<colorscreen::progress_info> progress) {
  // Remove progress
  removeProgress(progress);
  
  // Remove thread from tracking
  auto it = std::find(m_finetuneThreads.begin(), m_finetuneThreads.end(), thread);
  if (it != m_finetuneThreads.end()) {
    m_finetuneThreads.erase(it);
  }
  
  // Check if cancelled
  if (progress && progress->cancelled()) {
    return;
  }
  
  // Add points if successful
  if (success && !points.empty()) {
    ParameterState oldState = getCurrentState();
    
    // Add all points using add_or_modify_point
    for (const auto &point : points) {
      m_solverParams.add_or_modify_point(point.img, point.scr, point.color);
    }
    
    // Update UI
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams, &m_solverParams);
    m_imageWidget->update();
    
    // Create undo command
    ParameterState newState = getCurrentState();
    m_undoStack->push(new ChangeParametersCommand(this, oldState, newState, "Add registration points"));
    
    // Trigger auto solver if enabled
    if (m_geometryPanel && m_geometryPanel->isAutoEnabled()) {
      size_t count = m_imageWidget->registrationPointCount();
      if (count >= 3) {
        onOptimizeGeometry(true);
      }
    }
    updateRegistrationActions();
  }
}

void MainWindow::onAutodetectScreen() {
  if (!m_scan) {
    return;
  }
  
  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Detecting screen", 100);
  addProgress(progress);
  
  // Create worker and thread
  DetectScreenWorker *worker = new DetectScreenWorker(
      m_detectParams, m_solverParams, m_scrToImgParams, m_scan, progress, m_rparams.gamma);
  QThread *thread = new QThread();
  worker->moveToThread(thread);
  
  // Connect signals
  connect(thread, &QThread::started, worker, &DetectScreenWorker::detect);
  connect(worker, &DetectScreenWorker::finished, thread, &QThread::quit);
  connect(worker, &DetectScreenWorker::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  
  // Connect to our slot to handle results
  connect(worker, &DetectScreenWorker::finished, this,
          [this, progress](bool success, colorscreen::detected_screen result, colorscreen::solver_parameters solverParams) {
            onDetectScreenFinished(success, result, solverParams);
            removeProgress(progress);
          });
  
  // Store thread reference
  m_detectScreenThread = thread;
  
  // Start thread
  thread->start();
}

void MainWindow::onDetectScreenFinished(bool success, colorscreen::detected_screen result, colorscreen::solver_parameters solverParams) {
  // Clean up thread reference
  m_detectScreenThread = nullptr;
  
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
      colorscreen::render_parameters::color_model_properties[m_rparams.color_model].pretty_name);
  QString detectedDye = QString::fromUtf8(
      colorscreen::render_parameters::color_model_properties[tempParams.color_model].pretty_name);
  QString detectedScreen = QString::fromUtf8(
      colorscreen::scr_names[(int)result.param.type].pretty_name);
  
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Screen Detection");
  msgBox.setIconPixmap(renderScreenIcon(result.param.type).pixmap(64, 64));
  
  if (currentDye != detectedDye) {
    msgBox.setText(QString("Detected Screen: <b>%1</b>").arg(detectedScreen));
    msgBox.setInformativeText(QString("Change color model (Dyes) from %1 to %2?")
                              .arg(currentDye).arg(detectedDye));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    updateColorModel = (msgBox.exec() == QMessageBox::Yes);
  } else {
    msgBox.setText(QString("Detected Screen: <b>%1</b> successfully.").arg(detectedScreen));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
  }
  
  // Create undo snapshot before making changes
  ParameterState oldState = getCurrentState();
  
  // Update parameters
  m_scrToImgParams.type = result.param.type;
  m_scrToImgParams.mesh_trans = result.mesh_trans;
  
  // Update color model if requested
  if (updateColorModel) {
    m_rparams.auto_color_model(result.param.type);
  }
  
  // Copy the modified solver points from the worker's local copy
  m_solverParams.points = solverParams.points;
  
  // Change render type to interpolated after successful autodetection
  m_renderTypeParams.type = colorscreen::render_type_interpolated;
  
  // Update UI
  m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams, &m_solverParams);
  m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams);
  updateUIFromState(getCurrentState());
  updateRegistrationActions();
  updateModeMenu();
  
  // Always trigger geometry solver with computeMesh=false to preserve detected mesh
  // The solver will update center, coordinates, lens parameters, etc.
  if (m_solverParams.points.size() >= 3) {
    // Request solver with special handling to preserve mesh
    m_solverQueue.requestRender();
  }
  
  // Create undo command
  ParameterState newState = getCurrentState();
  m_undoStack->push(new ChangeParametersCommand(this, oldState, newState, "Autodetect screen"));
}


void MainWindow::onSetCenter(colorscreen::point_t imgPos) {
  if (!m_scan) {
    return;
  }

  // Set the screen center to the clicked position
  m_scrToImgParams.center = imgPos;
  
  onCoordinateSystemChanged();
  
  m_imageWidget->update();
}

void MainWindow::onOptimizeCoordinates() {
  if (!m_scan)
    return;

  colorscreen::finetune_parameters fparams;
  fparams.flags = colorscreen::finetune_position |
                  colorscreen::finetune_coordinates |
                  colorscreen::finetune_bw |
                  colorscreen::finetune_use_srip_widths;

  std::vector<colorscreen::point_t> locs;
  
  statusBar()->showMessage("Optimizing coordinates...");
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QApplication::processEvents(); // Ensure UI updates

  colorscreen::finetune_result ret = colorscreen::finetune(
      m_rparams, m_scrToImgParams, *m_scan, locs, nullptr, fparams, nullptr);

  QApplication::restoreOverrideCursor();
  statusBar()->clearMessage();

  if (ret.success) {
      // Update parameters
      m_scrToImgParams.center = ret.center;
      m_scrToImgParams.coordinate1 = ret.coordinate1;
      m_scrToImgParams.coordinate2 = ret.coordinate2;
      
      // Update UI
      changeParameters(getCurrentState(), "Optimize Coordinates");
      m_imageWidget->update();
      
      QMessageBox::information(this, "Optimization", "Coordinates optimized successfully.");
  } else {
      QMessageBox::warning(this, "Optimization", "Optimization failed: " + QString::fromStdString(ret.err));
  }
}

void MainWindow::onCoordinateSystemChanged() {
  if (!m_scan) return;
  
  // Navigation View always needs update because it uses FAST mode (which relies on ScrToImg) 
  m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams);

  // Main area: checks flag
  // Using colorscreen::render_type_max to safe check
  if (m_renderTypeParams.type < colorscreen::render_type_max) {
      const auto &prop = colorscreen::render_type_properties[m_renderTypeParams.type];
      if (prop.flags & colorscreen::render_type_property::NEEDS_SCR_TO_IMG) {
          m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams, 
                                          &m_renderTypeParams, &m_solverParams);
      }
  }
}
void MainWindow::onFlatFieldRequested() {
  QString filters = "Images (*.tif *.tiff *.jpg *.jpeg *.raw *.dng *.iiq *.nef *.NEF *.cr2 "
                    "*.CR2 *.eip *.arw *.ARW *.raf *.RAF *.arq *.ARQ *.csprj);;All Files "
                    "(*)";
  QString whiteFile = QFileDialog::getOpenFileName(this, "Choose White Reference", m_currentImageFile, filters);
  if (whiteFile.isEmpty()) return;

  QString blackFile;
  if (QMessageBox::question(this, "Flat Field", "Do you want to provide a black reference image (optional)?",
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    blackFile = QFileDialog::getOpenFileName(this, "Choose Black Reference", m_currentImageFile, filters);
  }
  
  // Create progress info
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Flat field analysis", 100);
  addProgress(progress);

  // Create worker and thread
  FlatFieldWorker *worker = new FlatFieldWorker(
      whiteFile, blackFile, m_rparams.gamma, m_rparams.demosaic, progress);
  QThread *thread = new QThread();
  worker->moveToThread(thread);

  // Connect signals
  connect(thread, &QThread::started, worker, &FlatFieldWorker::run);
  connect(worker, &FlatFieldWorker::finished, thread, &QThread::quit);
  connect(worker, &FlatFieldWorker::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);

  // Connect results
  connect(worker, &FlatFieldWorker::finished, this,
          [this, progress](bool success, std::shared_ptr<colorscreen::backlight_correction_parameters> result) {
            onFlatFieldFinished(success, result);
            removeProgress(progress);
          });

  m_flatFieldThread = thread;
  thread->start();
}

void MainWindow::onFlatFieldFinished(bool success, std::shared_ptr<colorscreen::backlight_correction_parameters> result) {
  m_flatFieldThread = nullptr;

  if (!success || !result) {
    QMessageBox::warning(this, "Flat Field", "Flat field analysis failed.");
    return;
  }

  // Update parameters with undo support
  ParameterState newState = getCurrentState();
  newState.rparams.backlight_correction = result;
  
  changeParameters(newState, "Flat field");
  
  QMessageBox::information(this, "Flat Field", "Flat field analysis successful.");
}
