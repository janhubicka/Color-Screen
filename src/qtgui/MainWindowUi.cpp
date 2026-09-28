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

namespace {

/** Return the application-level document manager when MainWindow is running
    inside the normal Color-Screen Qt application. */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QCoreApplication::instance());
}

} // namespace

/** MainWindow user-interface construction lives here.

    Keep this translation unit limited to building/wiring the document
    inspector, toolbar, menus, and render-mode shortcuts. Document lifecycle
    belongs in MainWindowDocument.cpp; analysis workflows belong in
    MainWindowAnalysis.cpp. */

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
            if (!m_inspectorImageRouting.switching &&
                sender() == inspectorImageWidget() &&
                mode != ImageWidget::AddPointMode &&
                m_temporaryCanvas.pointClick.active())
              clearPointClickToolPresentation();
            if (!m_inspectorImageRouting.switching &&
                sender() == inspectorImageWidget() &&
                mode != ImageWidget::GenericAreaMode &&
                m_temporaryCanvas.areaSelectionCallback) {
              // If the active view switches tool during selection, abandon the
              // pending callback. Merely moving the inspector to another view
              // must not cancel the operation.
              cancelAreaSelectionPresentation();
            }
            if (!m_inspectorImageRouting.switching &&
                sender() == inspectorImageWidget() &&
                m_temporaryCanvas.instructionOwner &&
                mode != *m_temporaryCanvas.instructionOwner)
              clearTemporaryCanvasInstruction();
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

  // ImageWidget::pointAdded is routed to onPointAdded; one exclusive
  // document-owned point-click intent decides Profile, Focus, or registration.
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
            [this]() {
              m_imageLayerPanel->setNeutralAreaChecked(false);
              m_imageLayerPanel->updateUI();
            },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              return s.rparams.auto_mix_weights(scan, s.scrToImg, area, p);
            });
      });

  connect(
      m_imageLayerPanel, &ImageLayerPanel::infraredAreaRequested, this,
      [this]() {
        runAreaComputation(
            tr("Select area to set simulated mix parameters using infrared"),
            tr("Set simulated mix parameters using infrared"),
            [this]() { m_imageLayerPanel->setInfraredAreaEnabled(false); },
            [this]() {
              m_imageLayerPanel->setInfraredAreaChecked(false);
              m_imageLayerPanel->updateUI();
            },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              return s.rparams.auto_mix_weights_using_ir(
                  scan, s.scrToImg, area, p);
            });
      });

  connect(
      m_imageLayerPanel, &ImageLayerPanel::darkAreaRequested, this, [this]() {
        runAreaComputation(
            tr("Select dark area for simulated mixing"),
            tr("Set dark mix parameters by area"),
            [this]() { m_imageLayerPanel->setDarkAreaEnabled(false); },
            [this]() {
              m_imageLayerPanel->setDarkAreaChecked(false);
              m_imageLayerPanel->updateUI();
            },
            [](ParameterState &s, colorscreen::image_data &scan,
               const colorscreen::int_image_area &area,
               colorscreen::progress_info *p) {
              return s.rparams.auto_mix_dark(scan, s.scrToImg, area, p);
            });
      });

  connect(m_colorPanel, &ColorPanel::neutralAreaRequested, this, [this]() {
    runAreaComputation(
        tr("Select neutral area for white balance"),
        tr("Set white balance by neutral area"),
        [this]() { m_colorPanel->setNeutralAreaEnabled(false); },
        [this]() {
          m_colorPanel->setNeutralAreaChecked(false);
          m_colorPanel->updateUI();
        },
        [](ParameterState &s, colorscreen::image_data &scan,
           const colorscreen::int_image_area &area,
           colorscreen::progress_info *p) {
          return s.rparams.auto_white_balance(scan, s.scrToImg, area, p);
        });
  });

  connect(m_colorPanel, &ColorPanel::autoLevelsRequested, this, [this]() {
    runAreaComputation(
        tr("Select area for auto levels"),
        tr("Set auto levels by area"),
        [this]() { m_colorPanel->setAutoLevelsEnabled(false); },
        [this]() {
          m_colorPanel->setAutoLevelsChecked(false);
          m_colorPanel->updateUI();
        },
        [](ParameterState &s, colorscreen::image_data &scan,
           const colorscreen::int_image_area &area,
           colorscreen::progress_info *p) {
          return s.rparams.auto_dark_brightness(scan, s.scrToImg, area, p);
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
            if (!m_temporaryCanvas.pointClick.profileSpot())
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

  m_recentFiles.menu = m_fileMenu->addMenu("Open &Recent");
  m_recentFiles.settingsKey = QStringLiteral("recentFiles");
  m_recentFiles.emptyLabel = tr("No Recent Files");
  m_recentFiles.clearLabel = tr("Clear Recent Files");
  connect(m_recentFiles.menu, &QMenu::aboutToShow, this,
          &MainWindow::loadRecentFiles);
  updateRecentFileActions();

  QAction *openParamsAction = m_fileMenu->addAction("Open &Parameters...");
  openParamsAction->setToolTip(
      "Load rendering and geometry settings from a parameter (.par) file.");
  connect(openParamsAction, &QAction::triggered, this,
          &MainWindow::onOpenParameters);

  m_recentParams.menu = m_fileMenu->addMenu("Open Recent &Parameters");
  m_recentParams.settingsKey = QStringLiteral("recentParams");
  m_recentParams.emptyLabel = tr("No Recent Parameters");
  m_recentParams.clearLabel = tr("Clear Recent Parameters");
  connect(m_recentParams.menu, &QMenu::aboutToShow, this,
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
      tr("Show color-coded centers of screen elements from the most recent "
         "current automatic screen detection. The diagnostic expires when "
         "detection inputs change; geometry-only refinement keeps it current. "
         "The dense overlay is drawn at 100% zoom and above."));
  connect(m_detectedPatchCentersAction, &QAction::toggled, this,
          [this](bool show) {
            m_detectedScreenDiagnostics.showCenters = show;
            syncDetectedScreenDiagnostics(m_imageWidget);
            emit detectedScreenDiagnosticsChanged();
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
