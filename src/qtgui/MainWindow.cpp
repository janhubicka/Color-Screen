#include "MainWindow.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "ImageWidget.h"
#include "NavigationView.h"
#include "ScreenPanel.h"
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtConcurrent>

// Undo/Redo Implementation

class ChangeParametersCommand : public QUndoCommand {
public:
  ChangeParametersCommand(MainWindow *window, const ParameterState &oldState,
                          const ParameterState &newState)
      : m_window(window), m_oldState(oldState), m_newState(newState) {
    setText("Change Parameters");
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
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
  createMenus();

  m_mainSplitter = new QSplitter(Qt::Horizontal, this);
  setCentralWidget(m_mainSplitter);

  // Left: Image Widget
  m_imageWidget = new ImageWidget(this);
  m_mainSplitter->addWidget(m_imageWidget);

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
      [this](const ParameterState &s) { changeParameters(s); },
      [this]() { return m_scan; }, this);

  // Create Screen Panel
  m_screenPanel =
      new ScreenPanel([this]() { return getCurrentState(); },
                      [this](const ParameterState &s) { changeParameters(s); },
                      [this]() { return m_scan; }, this);

  // Create Color Panel (after Sharpness)
  m_colorPanel =
      new ColorPanel([this]() { return getCurrentState(); },
                     [this](const ParameterState &s) { changeParameters(s); },
                     [this]() { return m_scan; }, this);

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
      dock->setWidget(w);
      dock->setFloating(true);
      dock->show();
      if (w->sizeHint().isValid())
        dock->resize(w->sizeHint());
    });

    // Connect Close/Reattach via Event Filter
    dock->installEventFilter(
        new DockCloseEventFilter(dock, [dock, panel, reattachMethod]() {
          if (dock->widget()) {
            (panel->*reattachMethod)(dock->widget());
            dock->setWidget(nullptr);
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

  m_linearizationPanel = new LinearizationPanel(
      [this]() { return getCurrentState(); },
      [this](const ParameterState &s) { changeParameters(s); },
      [this]() { return m_scan; }, this);
  m_configTabs->addTab(m_linearizationPanel, "Linearization");
  m_configTabs->addTab(m_sharpnessPanel, "Sharpness");
  m_configTabs->addTab(m_screenPanel, "Screen");
  m_configTabs->addTab(m_colorPanel, "Color");
  rightSplitter->addWidget(m_configTabs);

  // Register panels for updates (Order matters for tab order but not for
  // updates necessarily)
  m_panels.push_back(m_linearizationPanel);
  m_panels.push_back(m_sharpnessPanel);
  m_panels.push_back(m_screenPanel);
  m_panels.push_back(m_colorPanel);

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

void MainWindow::createToolbar() {
  m_toolbar = addToolBar("Main Toolbar");
  m_toolbar->setMovable(false);

  QLabel *modeLabel = new QLabel("Mode: ", m_toolbar);
  m_toolbar->addWidget(modeLabel);

  m_modeComboBox = new QComboBox(m_toolbar);
  m_modeComboBox->setMinimumWidth(200);
  connect(m_modeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &MainWindow::onModeChanged);
  m_toolbar->addWidget(m_modeComboBox);

  m_toolbar->addSeparator();
  QAction *rotLeftAction = m_toolbar->addAction(
      QIcon::fromTheme("object-rotate-left-symbolic"), "Rotate Left");
  connect(rotLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);

  QAction *rotRightAction = m_toolbar->addAction(
      QIcon::fromTheme("object-rotate-right-symbolic"), "Rotate Right");
  connect(rotRightAction, &QAction::triggered, this, &MainWindow::rotateRight);

  m_toolbar->addSeparator();
  m_colorCheckBox = new QCheckBox("Color", m_toolbar);
  m_colorCheckBox->setEnabled(false); // Initially disabled
  connect(m_colorCheckBox, &QCheckBox::toggled, this,
          &MainWindow::onColorCheckBoxChanged);
  m_colorCheckBoxAction = m_toolbar->addWidget(m_colorCheckBox);

  updateModeMenu();
}

void MainWindow::rotateLeft() {
  if (!m_scan)
    return;
  m_scrToImgParams.final_rotation -= 90.0;
  m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                          &m_detectParams, &m_renderTypeParams);
  m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                             &m_detectParams);
}

void MainWindow::rotateRight() {
  if (!m_scan)
    return;
  m_scrToImgParams.final_rotation += 90.0;
  m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                          &m_detectParams, &m_renderTypeParams);
  m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                             &m_detectParams);
}

void MainWindow::onColorCheckBoxChanged(bool checked) {
  // Update the color field in render_type_parameters
  m_renderTypeParams.color = checked;

  // Trigger re-render when color changes (without resetting view)
  if (m_scan) {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams);
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
                                        &m_detectParams, &m_renderTypeParams);
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
  m_zoomInAction->setShortcut(QKeySequence::ZoomIn); // Ctrl++
  connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);

  m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
  m_zoomOutAction->setShortcut(QKeySequence::ZoomOut); // Ctrl+-
  m_zoomOutAction->setStatusTip(tr("Zoom out"));
  connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
  m_viewMenu->addAction(m_zoomOutAction);

  m_zoom100Action = new QAction(tr("Zoom &1:1"), this);
  m_zoom100Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  m_zoom100Action->setStatusTip(tr("Zoom to 100%"));
  connect(m_zoom100Action, &QAction::triggered, this, &MainWindow::onZoom100);
  m_viewMenu->addAction(m_zoom100Action);

  m_zoomFitAction = new QAction(tr("Fit to &Screen"), this);
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
  colorscreen::scr_to_img_parameters oldScrToImg = m_scrToImgParams;
  colorscreen::scr_detect_parameters oldDetect = m_detectParams;
  // render_parameters comparison?
  // The user said: "Pass scr_to_img_parameters and scr_detect_parameters to
  // renderers. They can be compared by ==."

  if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams, &m_rparams,
                             &m_solverParams, &error)) {
    fclose(f);
    QString errStr =
        error ? QString::fromUtf8(error) : "Unknown error loading parameters.";
    QMessageBox::critical(this, "Error Loading Parameters", errStr);
    return;
  }
  fclose(f);

  // Update Renderer if needed
  // Check if parameters changed
  bool changed = false;
  if (!(oldScrToImg == m_scrToImgParams))
    changed = true;
  if (!(oldDetect == m_detectParams))
    changed = true;
  // We should also check m_rparams but user emphasized the others.
  // Let's assume ANY successful load might warrant a refresh or update.
  // But specific comparison requested for re-render.

  if (changed) {
    if (m_scan) {
      // Re-set image to re-create renderer with new params
      m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams);
    }
  } else {
    // Just update params... actually setImage logic uses values.
    // If we want to support run-time tuning without re-creating renderer, we'd
    // need setters in Renderer. But for Open Parameters (file load),
    // re-creating renderer is fine. Even if not changed, we might want to
    // update rparams? Let's just update if we have a scan.
    if (m_scan) {
      m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                              &m_detectParams, &m_renderTypeParams);
      m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                 &m_detectParams); // Update nav too
    }
  }
  m_undoStack->clear();
  updateModeMenu();
  updateUIFromState(getCurrentState());

  addToRecentParams(fileName);
  m_currentParamsFile = fileName; // Track current file for Save
}

void MainWindow::onSaveParameters() {
  // If we don't have a current file, fall back to Save As
  if (m_currentParamsFile.isEmpty()) {
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

    const char *tName = "";
    float percent = 0;
    task->info->get_status(&tName, &percent);

    m_statusLabel->setText(QString::fromUtf8(tName));
    if (percent >= 0) {
      m_progressBar->setValue((int)percent);
    } else {
      m_progressBar->setValue(0);
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
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    m_navigationView->setMinScale(m_imageWidget->getMinScale());
  }

  // Refresh param values too
  applyState(getCurrentState());
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

  // Clear current image and stop rendering
  m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr);

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
          if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams,
                                     &m_rparams, &m_solverParams, &error)) {
            QMessageBox::warning(this, "Error Loading Parameters",
                                 error ? QString::fromUtf8(error)
                                       : "Unknown error loading parameters.");
          } else {
            m_prevScrToImgParams = m_scrToImgParams;
            m_prevDetectParams = m_detectParams;
          }
          fclose(f);
        }
      }
    }
  }

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Loading image", 0);
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
                                      &m_detectParams, &m_renderTypeParams);
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
  double currentRot = m_scrToImgParams.final_rotation;

  m_rparams = state.rparams;
  m_scrToImgParams = state.scrToImg;
  m_detectParams = state.detect;
  m_solverParams = state.solver; // Manually copy logic if needed? Struct copy
                                 // should work if fields are copyable.
  // solver_parameters has vector, copy constructor should be fine
  // (std::vector).

  m_scrToImgParams.final_rotation = currentRot;

  // Update widgets - use updateParameters to avoid blocking
  if (m_scan) {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams);
    m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                       &m_detectParams);
  }

  updateUIFromState(state);
}

void MainWindow::updateUIFromState(const ParameterState &state) {
  for (auto panel : m_panels) {
    if (panel)
      panel->updateUI();
  }
}

ParameterState MainWindow::getCurrentState() const {
  ParameterState state;
  state.rparams = m_rparams;
  state.scrToImg = m_scrToImgParams;
  state.detect = m_detectParams;
  state.solver = m_solverParams;
  return state;
}

void MainWindow::changeParameters(const ParameterState &newState) {
  ParameterState currentState = getCurrentState();
  if (currentState == newState)
    return;

  m_undoStack->push(new ChangeParametersCommand(this, currentState, newState));
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
                              &m_detectParams, &m_renderTypeParams);
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

void MainWindow::onZoomFit() { m_imageWidget->fitToView(); }

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

void MainWindow::onGamutWarningToggled(bool checked) {
  if (m_rparams.gammut_warning != checked) {
    m_rparams.gammut_warning = checked;

    // Trigger update
    if (m_scan) {
      m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                      &m_detectParams, &m_renderTypeParams);
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
    // We need to track the current image path - for now use recent files
    if (!m_recentFiles.isEmpty()) {
      out << m_recentFiles.first();
    }
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
