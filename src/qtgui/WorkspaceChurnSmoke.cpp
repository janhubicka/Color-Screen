#include "WorkspaceChurnSmoke.h"

#include "ColorScreenApplication.h"
#include "ImageViewWindow.h"
#include "MainWindow.h"
#include "MultiLineTabWidget.h"
#include "ScreenPanel.h"
#include "WorkspaceWindow.h"

#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QWidget>

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace {

constexpr int workspaceChurnFailure = 18;

/** Live objects, document-local processing-state sentinels, and completion
    callback shared by the staged smoke test. */
struct WorkspaceChurnState {
  QPointer<WorkspaceWindow> workspace;
  QPointer<MainWindow> first;
  QPointer<MainWindow> second;
  QPointer<ImageViewWindow> view;
  QPointer<QToolBar> workspaceToolBar;
  QPointer<QWidget> workspaceToolBarHost;
  bool originalFirstScanMirror = false;
  bool expectedFirstScanMirror = false;
  bool expectedSecondScanMirror = false;
  bool expectedStatesSet = false;
  bool oneShotStarted = false;
  bool oneShotApplied = false;
  bool oneShotDone = false;
  std::atomic_bool oneShotSawCancellation{false};
  QPointer<ImageViewWindow> reference;
  std::unique_ptr<QTemporaryDir> referenceDirectory;
  ParameterState beforeReference;
  ParameterState referenceInputs;
  int referenceUndoIndex = 0;
  bool referenceReplacementDone = false;
  std::shared_ptr<colorscreen::progress_info> referenceReplacementProgress;
  std::vector<colorscreen::slanted_edge_parameters> referenceParameters;
  ParameterState mtfFitBaseline;
  ParameterState mtfFitExpected;
  colorscreen::mtf_parameters mtfFitInput;
  colorscreen::mtf_estimation_options mtfFitOptions;
  int mtfFitUndoIndex = 0;
  std::shared_ptr<colorscreen::progress_info> mtfFitCancelledProgress;
  std::function<void()> completed;
};

} // namespace

/** Exercise repeated MDI, detached-window, and peer-view ownership changes. */
void startWorkspaceChurnSmoke(ColorScreenApplication &app,
                              std::function<void()> completed) {
  auto startWorkspaceChurn =
      std::make_shared<std::function<void(int)>>();
  const std::weak_ptr<std::function<void(int)>> weakStartWorkspaceChurn =
      startWorkspaceChurn;
  *startWorkspaceChurn =
      [&app, completed = std::move(completed),
       weakStartWorkspaceChurn](int attemptsLeft) {
    const QList<MainWindow *> documents = app.documentWindows();
    WorkspaceWindow *workspace = app.workspaceWindow();
    bool ready = documents.size() == 2 && workspace && app.tabCount() == 2 &&
                 workspace->isTabbedView();
    for (MainWindow *document : documents) {
      if (!workspace || !document || !document->sharedImageData() ||
          !workspace->containsDocument(document)) {
        ready = false;
        break;
      }
    }
    if (!ready) {
      if (attemptsLeft > 0) {
        if (auto retry = weakStartWorkspaceChurn.lock()) {
          QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
            (*retry)(attemptsLeft - 1);
          });
          return;
        }
      }
      qCritical()
          << "Workspace churn smoke test requires two loaded document tabs";
      app.exit(workspaceChurnFailure);
      return;
    }

    auto state = std::make_shared<WorkspaceChurnState>();
    state->workspace = workspace;
    state->first = documents[0];
    state->second = documents[1];
    state->workspaceToolBar = workspace->findChild<QToolBar *>(
        QStringLiteral("WorkspaceToolbar"), Qt::FindDirectChildrenOnly);
    state->workspaceToolBarHost = state->workspaceToolBar
        ? state->workspaceToolBar->findChild<QWidget *>(
              QStringLiteral("WorkspaceToolbarHost"),
              Qt::FindDirectChildrenOnly)
        : nullptr;
    state->view = app.createViewWindow(state->first.data());
    state->completed = completed;
    if (!state->view) {
      qCritical() << "Workspace churn smoke could not create an ordinary view";
      app.exit(workspaceChurnFailure);
      return;
    }

    auto runPhase =
        std::make_shared<std::function<void(int, int)>>();
    const std::weak_ptr<std::function<void(int, int)>> weakRunPhase = runPhase;
    *runPhase = [&app, state, weakRunPhase](int phase, int attemptsLeft) {
      auto fail = [&app](const QString &message) {
        qCritical().noquote() << message;
        app.exit(workspaceChurnFailure);
      };
      auto schedule = [&app, weakRunPhase](int nextPhase, int delay,
                                           int attempts) {
        if (auto callback = weakRunPhase.lock()) {
          QTimer::singleShot(delay, &app,
                             [callback, nextPhase, attempts]() {
            (*callback)(nextPhase, attempts);
          });
          return true;
        }
        qCritical() << "Workspace churn smoke callback expired";
        app.exit(workspaceChurnFailure);
        return false;
      };
      auto retryOrFail = [&](const QString &message) {
        if (attemptsLeft > 0) {
          schedule(phase, 50, attemptsLeft - 1);
          return true;
        }
        fail(message);
        return false;
      };

      WorkspaceWindow *workspace = state->workspace.data();
      MainWindow *first = state->first.data();
      MainWindow *second = state->second.data();
      ImageViewWindow *view = state->view.data();
      if (!workspace || !first) {
        fail(QStringLiteral(
            "Workspace churn smoke lost its workspace or source document"));
        return;
      }

      QToolBar *workspaceToolBar = state->workspaceToolBar.data();
      QWidget *workspaceToolBarHost = state->workspaceToolBarHost.data();
      const QList<QToolBar *> directWorkspaceToolBars =
          workspace->findChildren<QToolBar *>(QString(),
                                              Qt::FindDirectChildrenOnly);
      if (!workspaceToolBar || !workspaceToolBarHost ||
          directWorkspaceToolBars.size() != 1 ||
          directWorkspaceToolBars.constFirst() != workspaceToolBar ||
          workspaceToolBar->parentWidget() != workspace ||
          !workspaceToolBar->isAncestorOf(workspaceToolBarHost) ||
          workspace->toolBarArea(workspaceToolBar) != Qt::TopToolBarArea) {
        fail(QStringLiteral(
            "Workspace churn changed the permanent workspace toolbar topology"));
        return;
      }

      auto documentStatesPreserved = [state, first, second]() {
        if (!state->expectedStatesSet)
          return true;
        if (!first || first->documentStateSnapshot().rparams.scan_mirror !=
                          state->expectedFirstScanMirror)
          return false;
        return !second ||
               second->documentStateSnapshot().rparams.scan_mirror ==
                   state->expectedSecondScanMirror;
      };
      if (phase > 0 && !documentStatesPreserved()) {
        fail(QStringLiteral(
            "Workspace churn changed a document processing-state sentinel during a presentation-only operation"));
        return;
      }

      auto *mdiArea = workspace->findChild<QMdiArea *>(
          QStringLiteral("documentMdiArea"));
      auto hasExactWrappers = [mdiArea, first, second, view]() {
        if (!mdiArea)
          return false;
        const int expected = 1 + (second ? 1 : 0) + (view ? 1 : 0);
        const QList<QMdiSubWindow *> windows =
            mdiArea->subWindowList(QMdiArea::CreationOrder);
        if (windows.size() != expected)
          return false;
        bool foundFirst = false;
        bool foundSecond = second == nullptr;
        bool foundView = view == nullptr;
        for (QMdiSubWindow *window : windows) {
          QWidget *hosted = window ? window->widget() : nullptr;
          if (!hosted)
            return false;
          foundFirst = foundFirst || hosted == first;
          foundSecond = foundSecond || hosted == second;
          foundView = foundView || hosted == view;
        }
        return foundFirst && foundSecond && foundView;
      };

      switch (phase) {
      case 0: {
        if (!second || !view || view->sourceDocument() != first ||
            view->sharedImageData() != first->sharedImageData() ||
            app.documentWindows().size() != 2 ||
            app.viewWindows().size() != 1 || app.tabCount() != 3 ||
            !workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            !workspace->containsView(view) || !view->isWorkspaceEmbedded() ||
            first->statusBar() != workspace->statusBar() ||
            second->statusBar() != workspace->statusBar() ||
            view->statusBar() != workspace->statusBar() ||
            workspace->currentDocument() != first ||
            first->inspectorImageWidget() != view->imageWidget()) {
          fail(QStringLiteral(
              "Workspace churn did not create a complete third peer tab"));
          return;
        }

        // Attached documents deliberately reparent their inspector column into
        // WorkspaceWindow's shared inspector stack.  Follow the document-owned
        // inspector handle rather than relying on QObject parentage.
        QWidget *inspector = first->workspaceInspectorWidget();
        auto *processingTabs =
            inspector ? inspector->findChild<MultiLineTabWidget *>(
                            QStringLiteral("ConfigTabs"))
                      : nullptr;
        const QStringList expectedProcessingTabs = {
            QStringLiteral("Digital capture"), QStringLiteral("Tiles"),
            QStringLiteral("Sharpness"), QStringLiteral("Image Layer"),
            QStringLiteral("Contact copy"), QStringLiteral("Screen"),
            QStringLiteral("Geometry"), QStringLiteral("Color"),
            QStringLiteral("Profile")};
        QStringList actualProcessingTabs;
        if (processingTabs) {
          for (int i = 0; i < processingTabs->count(); ++i)
            actualProcessingTabs.append(processingTabs->tabText(i));
        }
        QStringList missingProcessingTabs;
        for (const QString &name : expectedProcessingTabs) {
          if (actualProcessingTabs.count(name) != 1)
            missingProcessingTabs.append(name);
        }
        if (!processingTabs || !missingProcessingTabs.isEmpty()) {
          const QString detail = QStringLiteral(
                                     "Workspace churn source document lost part of the processing-panel workflow; missing=[%1], actual=[%2]")
                                     .arg(missingProcessingTabs.join(
                                              QStringLiteral(", ")),
                                          actualProcessingTabs.join(
                                              QStringLiteral(", ")));
          if (retryOrFail(detail))
            return;
          return;
        }

QWidget *workflowSummary =
    inspector->findChild<QWidget *>(QStringLiteral("WorkflowSummary"));
QToolButton *workflowToggle = inspector->findChild<QToolButton *>(
    QStringLiteral("WorkflowSummaryToggle"));
QLabel *workflowStages =
    inspector->findChild<QLabel *>(QStringLiteral("WorkflowStages"));
QLabel *processSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowProcessSummary"));
QLabel *registrationSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowRegistrationSummary"));
QLabel *calibrationSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowCalibrationSummary"));
QLabel *profileSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowProfileSummary"));
QLabel *nextStepSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowNextStepSummary"));
QComboBox *captureTypeCombo = inspector->findChild<QComboBox *>(
    QStringLiteral("CaptureTypeCombo"));
bool captureChoicesCompatible = captureTypeCombo != nullptr;
if (captureTypeCombo && first->sharedImageData()) {
  for (int i = 0; i < captureTypeCombo->count(); ++i) {
    const auto capture = static_cast<decltype(
        colorscreen::render_parameters::capture_unknown)>(
        captureTypeCombo->itemData(i).toInt());
    if (capture != colorscreen::render_parameters::capture_unknown &&
        !colorscreen::render_parameters::capture_type_compatible_p(
            capture, first->sharedImageData().get())) {
      captureChoicesCompatible = false;
      break;
    }
  }
}
QLabel *profileCalibrationStatus = inspector->findChild<QLabel *>(
    QStringLiteral("ProfileCalibrationStatus"));
QPushButton *profileOptimizeButton = inspector->findChild<QPushButton *>(
    QStringLiteral("ProfileOptimizeButton"));
QLabel *mtfCalibrationStatus = inspector->findChild<QLabel *>(
    QStringLiteral("MtfCalibrationStatus"));
QComboBox *mtfMeasurementSelector = inspector->findChild<QComboBox *>(
    QStringLiteral("MtfMeasurementSelector"));
QLabel *mtfMeasurementProvenance = inspector->findChild<QLabel *>(
    QStringLiteral("MtfMeasurementProvenance"));
QPushButton *mtfMeasurementLocate = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfMeasurementLocate"));
QCheckBox *mtfUseMeasured = inspector->findChild<QCheckBox *>(
    QStringLiteral("MtfUseMeasuredCheck"));
QWidget *redStripWidth = inspector->findChild<QWidget *>(
    QStringLiteral("ScreenRedStripWidth"));
QWidget *greenStripWidth = inspector->findChild<QWidget *>(
    QStringLiteral("ScreenGreenStripWidth"));
QComboBox *screenTypeCombo = inspector->findChild<QComboBox *>(
    QStringLiteral("ScreenTypeCombo"));
QAction *undoParametersAction = first->findChild<QAction *>(
    QStringLiteral("UndoParametersAction"));
const QList<QToolButton *> parameterResetButtons =
    inspector->findChildren<QToolButton *>(
        QStringLiteral("ParameterResetButton"));
auto findParameterResetButton =
    [&parameterResetButtons](const QString &parameterKey) {
      for (QToolButton *button : parameterResetButtons)
        if (button && button->property("parameterKey").toString() ==
                          parameterKey)
          return button;
      return static_cast<QToolButton *>(nullptr);
    };
auto findParameterSpinBox = [inspector](const QString &parameterKey) {
  const QList<QDoubleSpinBox *> spins =
      inspector->findChildren<QDoubleSpinBox *>();
  for (QDoubleSpinBox *spin : spins)
    if (spin && spin->property("parameterKey").toString() == parameterKey)
      return spin;
  return static_cast<QDoubleSpinBox *>(nullptr);
};
auto hasParameterKey = [inspector](const QString &parameterKey) {
  const QList<QWidget *> widgets = inspector->findChildren<QWidget *>();
  for (QWidget *widget : widgets)
    if (widget && widget->property("parameterKey").toString() == parameterKey)
      return true;
  return false;
};
QToolButton *resolutionResetButton = findParameterResetButton(
    QStringLiteral("capture.mtf.scan_dpi"));
QWidget *resolutionField =
    resolutionResetButton ? resolutionResetButton->parentWidget() : nullptr;
QToolButton *redWavelengthResetButton = findParameterResetButton(
    QStringLiteral("capture.mtf.wavelength.red"));
QWidget *redWavelengthField = redWavelengthResetButton
                                  ? redWavelengthResetButton->parentWidget()
                                  : nullptr;
QDoubleSpinBox *redWavelengthSpin =
    redWavelengthField
        ? redWavelengthField->findChild<QDoubleSpinBox *>()
        : nullptr;
QPushButton *mtfFitButton = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfFitButton"));
QPushButton *detectCoordinatesButton = inspector->findChild<QPushButton *>(
    QStringLiteral("DetectScreenCoordinatesButton"));
QPushButton *screenSwapColorsButton = inspector->findChild<QPushButton *>(
    QStringLiteral("ScreenSwapColorsButton"));
QCheckBox *showRegistrationPointsBox = inspector->findChild<QCheckBox *>(
    QStringLiteral("showRegistrationPointsBox"));
QLabel *geometryOptimizationMessage = inspector->findChild<QLabel *>(
    QStringLiteral("GeometryOptimizationMessage"));
QLabel *geometryLensMessage = inspector->findChild<QLabel *>(
    QStringLiteral("GeometryLensMessage"));
QLabel *geometryTiltMessage = inspector->findChild<QLabel *>(
    QStringLiteral("GeometryTiltMessage"));
QLabel *geometryNonlinearMessage = inspector->findChild<QLabel *>(
    QStringLiteral("GeometryNonlinearMessage"));
QSplitter *documentMainSplitter = first->findChild<QSplitter *>(
    QStringLiteral("DocumentMainSplitter"));
QToolButton *scannerCameraToggle = inspector->findChild<QToolButton *>(
    QStringLiteral("ScannerCameraPropertiesToggle"));
QPushButton *imageLayerInfraredButton = inspector->findChild<QPushButton *>(
    QStringLiteral("ImageLayerSetByInfraredButton"));
QToolButton *imageLayerToggle = inspector->findChild<QToolButton *>(
    QStringLiteral("SimulatedImageLayerToggle"));
QCheckBox *imageLayerSourceChoice = inspector->findChild<QCheckBox *>(
    QStringLiteral("ImageLayerUseSimulatedRgbCheck"));
QWidget *imageLayerSection = inspector->findChild<QWidget *>(
    QStringLiteral("SimulatedImageLayerSection"));
QWidget *contactCopyFilmGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ContactCopyFilmCharacteristicsGroup"));
QWidget *contactCopyRichardsGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ContactCopyRichardsGroup"));
QWidget *contactCopyManualGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ContactCopyManualPointsGroup"));
QWidget *contactCopyDarkroomGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ContactCopyDarkroomGroup"));
QWidget *colorScreenDyesGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ColorScreenDyesGroup"));
QWidget *colorViewingCorrectionGroup = inspector->findChild<QWidget *>(
    QStringLiteral("ColorViewingCorrectionGroup"));
QWidget *colorSpectralChartRow = inspector->findChild<QWidget *>(
    QStringLiteral("ColorSpectralChartRow"));
QCheckBox *finalMirrorCheck = inspector->findChild<QCheckBox *>(
    QStringLiteral("GeometryFinalMirrorCheck"));
QCheckBox *geometryAutoFitCheck = inspector->findChild<QCheckBox *>(
    QStringLiteral("autoSolverBox"));
QCheckBox *geometryNonlinearCheck = inspector->findChild<QCheckBox *>(
    QStringLiteral("nonlinearBox"));
QWidget *mtfUseMeasuredRow =
    mtfUseMeasured ? mtfUseMeasured->parentWidget() : nullptr;
const bool hasMtfMeasurements =
    !first->documentStateSnapshot().rparams.sharpen.scanner_mtf.measurements.empty();
const auto profileCapture =
    first->documentStateSnapshot().rparams.get_capture_type(
        first->sharedImageData().get());
const bool profileApplicable =
    first->sharedImageData()->has_rgb() &&
    colorscreen::render_parameters::capture_supports_screen_detection_p(
        profileCapture);

if (!workflowSummary || !workflowToggle || !workflowStages ||
    !processSummary || !registrationSummary || !calibrationSummary ||
    !profileSummary || !nextStepSummary || !captureTypeCombo ||
    !workflowStages->text().contains(QStringLiteral("Capture")) ||
    !workflowStages->text().contains(QStringLiteral("Sharpen")) ||
    !workflowStages->text().contains(QStringLiteral("Register")) ||
    !processSummary->text().startsWith(QStringLiteral("Process:")) ||
    !registrationSummary->text().startsWith(
        QStringLiteral("Registration:")) ||
    !calibrationSummary->text().contains(QStringLiteral("Sharpening:")) ||
    !calibrationSummary->text().contains(
        QStringLiteral("Capture MTF:")) ||
    calibrationSummary->text().contains(QStringLiteral("Profile:")) ||
    profileSummary->property("workflowApplicable").toBool() !=
        profileApplicable ||
    (profileApplicable
         ? !profileSummary->text().startsWith(QStringLiteral("Profile:"))
         : !profileSummary->text().isEmpty()) ||
    !nextStepSummary->text().startsWith(QStringLiteral("Next:")) ||
    !captureChoicesCompatible ||
    captureTypeCombo->findData(
        (int)colorscreen::render_parameters::capture_unknown) < 0 ||
    captureTypeCombo->findData(
        (int)colorscreen::render_parameters::capture_plain_image) < 0 ||
    processSummary->font().weight() < QFont::DemiBold ||
    registrationSummary->font().weight() < QFont::DemiBold ||
    calibrationSummary->font().weight() < QFont::DemiBold ||
    profileSummary->font().weight() < QFont::DemiBold) {
  const QString detail = QStringLiteral(
      "Workspace churn source document lost the persistent workflow "
      "summary; stages=[%1], process=[%2], registration=[%3], "
      "calibration=[%4], next=[%5]")
                             .arg(workflowStages
                                      ? workflowStages->text()
                                      : QStringLiteral("<missing>"),
                                  processSummary
                                      ? processSummary->text()
                                      : QStringLiteral("<missing>"),
                                  registrationSummary
                                      ? registrationSummary->text()
                                      : QStringLiteral("<missing>"),
                                  calibrationSummary
                                      ? calibrationSummary->text()
                                      : QStringLiteral("<missing>"),
                                  nextStepSummary
                                      ? nextStepSummary->text()
                                      : QStringLiteral("<missing>"));
  if (retryOrFail(detail))
    return;
  return;
}

        auto ignoresHorizontalHint = [](QWidget *widget) {
          return widget &&
                 widget->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored &&
                 widget->minimumWidth() == 0;
        };
        if (!documentMainSplitter ||
            !ignoresHorizontalHint(processSummary) ||
            !ignoresHorizontalHint(registrationSummary) ||
            !ignoresHorizontalHint(calibrationSummary) ||
            !ignoresHorizontalHint(profileSummary) ||
            !ignoresHorizontalHint(nextStepSummary) ||
            !ignoresHorizontalHint(geometryOptimizationMessage) ||
            !ignoresHorizontalHint(geometryLensMessage) ||
            !ignoresHorizontalHint(geometryTiltMessage) ||
            !ignoresHorizontalHint(geometryNonlinearMessage)) {
          fail(QStringLiteral(
              "Dynamic workflow/geometry status text can resize the main inspector splitter"));
          return;
        }

        if (!profileCalibrationStatus || !profileOptimizeButton ||
            (profileApplicable
                 ? !profileCalibrationStatus->text().startsWith(
                       QStringLiteral("Profile:"))
                 : !profileCalibrationStatus->text().isEmpty()) ||
            !mtfCalibrationStatus || !mtfMeasurementSelector ||
            !mtfMeasurementProvenance || !mtfMeasurementLocate ||
            mtfMeasurementSelector->count() < 1 ||
            mtfMeasurementSelector->itemData(0).toInt() != -1 ||
            mtfMeasurementLocate->isEnabled()) {
          fail(QStringLiteral(
              "Workspace churn source document lost calibration/provenance controls"));
          return;
        }

        // Applicability is logical UI state, independent of section folding.
        // Expanding Scanner/Camera properties must never resurrect measured-MTF
        // controls when this document has no measurements.
        const QVariant useMeasuredApplicable =
            mtfUseMeasuredRow
                ? mtfUseMeasuredRow->property("parameterApplicable")
                : QVariant();
        const QVariant fitApplicable =
            mtfFitButton ? mtfFitButton->property("parameterApplicable")
                         : QVariant();
        if (!mtfUseMeasured || !mtfUseMeasuredRow || !mtfFitButton ||
            !scannerCameraToggle || !useMeasuredApplicable.isValid() ||
            !fitApplicable.isValid() ||
            useMeasuredApplicable.toBool() != hasMtfMeasurements ||
            fitApplicable.toBool() != hasMtfMeasurements) {
          fail(QStringLiteral(
              "Workspace churn lost measured-MTF applicability metadata"));
          return;
        }

        const bool scannerPropertiesWereExpanded =
            scannerCameraToggle->isChecked();
        scannerCameraToggle->setChecked(false);
        if (!mtfUseMeasuredRow->isHidden() || !mtfFitButton->isHidden()) {
          fail(QStringLiteral(
              "Collapsing Scanner/Camera properties left an applicable row visible"));
          return;
        }
        scannerCameraToggle->setChecked(true);
        if (mtfUseMeasuredRow->isHidden() != !hasMtfMeasurements ||
            mtfFitButton->isHidden() != !hasMtfMeasurements) {
          fail(QStringLiteral(
              "Expanding Scanner/Camera properties resurrected an inapplicable measured-MTF row"));
          return;
        }
        scannerCameraToggle->setChecked(scannerPropertiesWereExpanded);

        // Image Layer used to repair this row with a section-toggle callback.
        // It now follows the same applicability contract as measured-MTF rows:
        // folding is presentation state and cannot override logical availability.
        const ParameterState imageLayerState = first->documentStateSnapshot();
        const auto imageLayerImage = first->sharedImageData();
        const bool infraredActionApplicable =
            imageLayerImage && imageLayerImage->has_rgb() &&
            imageLayerImage->has_grayscale_or_ir() &&
            imageLayerState.rparams.ignore_infrared;
        const QVariant infraredApplicable =
            imageLayerInfraredButton
                ? imageLayerInfraredButton->property("parameterApplicable")
                : QVariant();
        if (!imageLayerInfraredButton || !imageLayerToggle ||
            !infraredApplicable.isValid() ||
            infraredApplicable.toBool() != infraredActionApplicable) {
          fail(QStringLiteral(
              "Workspace churn lost Image Layer applicability metadata"));
          return;
        }
        const bool imageLayerSourceApplicable =
            imageLayerImage && imageLayerImage->has_rgb() &&
            imageLayerImage->has_grayscale_or_ir();
        const QVariant imageLayerSourceApplicability =
            imageLayerSourceChoice
                ? imageLayerSourceChoice->property("parameterApplicable")
                : QVariant();
        if (!imageLayerSourceChoice ||
            !imageLayerSourceApplicability.isValid() ||
            imageLayerSourceApplicability.toBool() != imageLayerSourceApplicable ||
            imageLayerSourceChoice->isHidden() == imageLayerSourceApplicable) {
          fail(QStringLiteral(
              "Image Layer source choice bypassed row applicability semantics"));
          return;
        }

        const bool simulatedSectionApplicable =
            imageLayerImage && imageLayerImage->has_rgb() &&
            (!imageLayerImage->has_grayscale_or_ir() ||
             imageLayerState.rparams.ignore_infrared);
        const QVariant simulatedSectionApplicability =
            imageLayerSection
                ? imageLayerSection->property("parameterApplicable")
                : QVariant();
        if (!imageLayerSection || !simulatedSectionApplicability.isValid() ||
            simulatedSectionApplicability.toBool() != simulatedSectionApplicable ||
            imageLayerSection->isHidden() == simulatedSectionApplicable) {
          fail(QStringLiteral(
              "Image Layer simulated section bypassed applicability semantics"));
          return;
        }

        // Specialist section visibility now shares one logical row contract.
        const ParameterState sectionState = first->documentStateSnapshot();
        const bool contactCopyApplicable =
            sectionState.rparams.contact_copy.simulate;
        QWidget *contactCopyGroups[] = {
            contactCopyFilmGroup, contactCopyRichardsGroup,
            contactCopyManualGroup, contactCopyDarkroomGroup};
        for (QWidget *group : contactCopyGroups) {
          const QVariant applicable =
              group ? group->property("parameterApplicable") : QVariant();
          if (!group || !applicable.isValid() ||
              applicable.toBool() != contactCopyApplicable ||
              group->isHidden() == contactCopyApplicable) {
            fail(QStringLiteral(
                "Contact Copy section bypassed applicability semantics"));
            return;
          }
        }

        const auto sectionImage = first->sharedImageData();
        const auto sectionCapture =
            sectionImage ? sectionState.rparams.get_capture_type(sectionImage.get())
                         : sectionState.rparams.capture_type;
        const bool historicalColorApplicable =
            colorscreen::render_parameters::capture_has_screen_p(sectionCapture);
        QWidget *historicalColorGroups[] = {
            colorScreenDyesGroup, colorViewingCorrectionGroup};
        for (QWidget *group : historicalColorGroups) {
          const QVariant applicable =
              group ? group->property("parameterApplicable") : QVariant();
          if (!group || !applicable.isValid() ||
              applicable.toBool() != historicalColorApplicable ||
              group->isHidden() == historicalColorApplicable) {
            fail(QStringLiteral(
                "Color historical section bypassed applicability semantics"));
            return;
          }
        }
        const bool spectralApplicable =
            colorscreen::render_parameters::color_model_properties[
                sectionState.rparams.color_model]
                .flags & colorscreen::render_parameters::SPECTRA_BASED;
        const QVariant spectralRowApplicability =
            colorSpectralChartRow
                ? colorSpectralChartRow->property("parameterApplicable")
                : QVariant();
        if (!colorSpectralChartRow || !spectralRowApplicability.isValid() ||
            spectralRowApplicability.toBool() != spectralApplicable) {
          fail(QStringLiteral(
              "Color spectral row bypassed applicability semantics"));
          return;
        }

        const bool imageLayerWasExpanded = imageLayerToggle->isChecked();
        imageLayerToggle->setChecked(false);
        if (!imageLayerInfraredButton->isHidden()) {
          fail(QStringLiteral(
              "Collapsing Image Layer left the infrared-only action visible"));
          return;
        }
        imageLayerToggle->setChecked(true);
        if (imageLayerInfraredButton->isHidden() != !infraredActionApplicable) {
          fail(QStringLiteral(
              "Expanding Image Layer resurrected an inapplicable infrared action"));
          return;
        }
        imageLayerToggle->setChecked(imageLayerWasExpanded);

        // enabledCheck must never mean visibility. Final orientation remains
        // discoverable before screen geometry exists, but teaches the missing
        // prerequisite by being disabled; configuring a valid basis enables it.
        if (!finalMirrorCheck) {
          fail(QStringLiteral("Workspace churn lost final mirror checkbox"));
          return;
        }
        const ParameterState checkboxSemanticsBaseline =
            first->documentStateSnapshot();
        ParameterState withoutFinalGeometry = checkboxSemanticsBaseline;
        withoutFinalGeometry.scrToImg.type = colorscreen::Dufay;
        withoutFinalGeometry.scrToImg.mesh_trans = nullptr;
        withoutFinalGeometry.scrToImg.coordinate1 = {0, 0};
        withoutFinalGeometry.scrToImg.coordinate2 = {0, 0};
        first->applyState(withoutFinalGeometry);
        if (finalMirrorCheck->isHidden() || finalMirrorCheck->isEnabled()) {
          fail(QStringLiteral(
              "Checkbox prerequisite hid final orientation instead of disabling it"));
          return;
        }
        ParameterState withFinalGeometry = withoutFinalGeometry;
        withFinalGeometry.scrToImg.coordinate1 = {10, 0};
        withFinalGeometry.scrToImg.coordinate2 = {0, 10};
        first->applyState(withFinalGeometry);
        if (finalMirrorCheck->isHidden() || !finalMirrorCheck->isEnabled()) {
          fail(QStringLiteral(
              "Checkbox prerequisite did not enable final orientation"));
          return;
        }
        first->applyState(checkboxSemanticsBaseline);

        // Geometry is the next complete stable-key panel after Screen. Only
        // controls backed by ParameterState receive keys: Auto fit and the
        // nonlinear fit-request toggle are panel/operation state and must not
        // pretend to be persistent document parameters.
        const QStringList geometryParameterKeys = {
            QStringLiteral("geometry.fit.optimize_lens"),
            QStringLiteral("geometry.fit.lens_center_distance"),
            QStringLiteral("geometry.fit.optimize_tilt"),
            QStringLiteral("geometry.scanner_type"),
            QStringLiteral("geometry.final.rotation"),
            QStringLiteral("geometry.final.mirror")};
        for (const QString &key : geometryParameterKeys) {
          if (!hasParameterKey(key)) {
            fail(QStringLiteral(
                     "Workspace churn lost Geometry parameter key %1")
                     .arg(key));
            return;
          }
        }
        if (finalMirrorCheck->property("parameterKey").toString() !=
                QStringLiteral("geometry.final.mirror") ||
            !findParameterSpinBox(
                QStringLiteral("geometry.fit.lens_center_distance")) ||
            !findParameterSpinBox(QStringLiteral("geometry.final.rotation")) ||
            !geometryAutoFitCheck || !geometryNonlinearCheck ||
            geometryAutoFitCheck->property("parameterKey").isValid() ||
            geometryNonlinearCheck->property("parameterKey").isValid()) {
          fail(QStringLiteral(
              "Geometry stable keys crossed the document/UI-state boundary"));
          return;
        }

        if (!redStripWidth || !greenStripWidth || !screenTypeCombo ||
            redStripWidth->property("parameterKey").toString() !=
                QStringLiteral("screen.red_strip_width") ||
            greenStripWidth->property("parameterKey").toString() !=
                QStringLiteral("screen.green_strip_width") ||
            screenTypeCombo->property("parameterKey").toString() !=
                QStringLiteral("screen.type")) {
          fail(QStringLiteral(
              "Workspace churn lost stable Screen parameter-key metadata"));
          return;
        }

        const QStringList screenParameterKeys = {
            QStringLiteral("screen.type"),
            QStringLiteral("screen.red_strip_width"),
            QStringLiteral("screen.green_strip_width"),
            QStringLiteral("screen.collection_threshold"),
            QStringLiteral("screen.collection_quality"),
            QStringLiteral("screen.demosaic"),
            QStringLiteral("screen.demosaiced_scaling"),
            QStringLiteral("screen.denoise.pre.mode"),
            QStringLiteral("screen.denoise.pre.strength"),
            QStringLiteral("screen.denoise.pre.patch_radius"),
            QStringLiteral("screen.denoise.pre.search_radius"),
            QStringLiteral("screen.denoise.pre.bilateral_sigma_s"),
            QStringLiteral("screen.denoise.pre.bilateral_sigma_r"),
            QStringLiteral("screen.denoise.post.mode"),
            QStringLiteral("screen.denoise.post.strength"),
            QStringLiteral("screen.denoise.post.patch_radius"),
            QStringLiteral("screen.denoise.post.search_radius"),
            QStringLiteral("screen.denoise.post.bilateral_sigma_s"),
            QStringLiteral("screen.denoise.post.bilateral_sigma_r")};
        for (const QString &key : screenParameterKeys) {
          if (!hasParameterKey(key)) {
            fail(QStringLiteral(
                     "Workspace churn lost Screen parameter key %1")
                     .arg(key));
            return;
          }
        }

        // Pre- and post-demosaic denoising intentionally reuse visible labels.
        // Rapid changes in different stages must therefore remain separate Undo
        // gestures even when they happen inside the historical merge interval.
        QDoubleSpinBox *preDenoiseStrength = findParameterSpinBox(
            QStringLiteral("screen.denoise.pre.strength"));
        QDoubleSpinBox *postDenoiseStrength = findParameterSpinBox(
            QStringLiteral("screen.denoise.post.strength"));
        if (!preDenoiseStrength || !postDenoiseStrength ||
            !undoParametersAction) {
          fail(QStringLiteral(
              "Workspace churn could not find keyed Screen strength controls or Undo"));
          return;
        }
        const ParameterState screenUndoBaseline = first->documentStateSnapshot();
        ParameterState screenUndoReady = screenUndoBaseline;
        screenUndoReady.scrToImg.type = colorscreen::Dufay;
        screenUndoReady.rparams.screen_demosaic =
            colorscreen::render_parameters::default_demosaic;
        screenUndoReady.rparams.screen_denoise.mode =
            colorscreen::denoise_parameters::nl_means;
        screenUndoReady.rparams.demosaiced_denoise.mode =
            colorscreen::denoise_parameters::nl_means;
        first->applyState(screenUndoReady);
        if (!preDenoiseStrength->isEnabled() ||
            !postDenoiseStrength->isEnabled()) {
          fail(QStringLiteral(
              "Workspace churn could not enable both Screen strength controls"));
          return;
        }
        const double preStrengthBefore = preDenoiseStrength->value();
        const double postStrengthBefore = postDenoiseStrength->value();
        const double preStrengthAfter =
            preStrengthBefore <= 0.98 ? preStrengthBefore + 0.01
                                      : preStrengthBefore - 0.01;
        const double postStrengthAfter =
            postStrengthBefore <= 0.96 ? postStrengthBefore + 0.02
                                       : postStrengthBefore - 0.02;
        preDenoiseStrength->setValue(preStrengthAfter);
        postDenoiseStrength->setValue(postStrengthAfter);
        ParameterState afterScreenEdits = first->documentStateSnapshot();
        if (std::abs(afterScreenEdits.rparams.screen_denoise.strength -
                     preStrengthAfter) > 1e-8 ||
            std::abs(afterScreenEdits.rparams.demosaiced_denoise.strength -
                     postStrengthAfter) > 1e-8) {
          fail(QStringLiteral(
              "Keyed Screen strength controls did not update their stages"));
          return;
        }
        undoParametersAction->trigger();
        const ParameterState afterPostStrengthUndo =
            first->documentStateSnapshot();
        if (std::abs(afterPostStrengthUndo.rparams.screen_denoise.strength -
                     preStrengthAfter) > 1e-8 ||
            std::abs(afterPostStrengthUndo.rparams.demosaiced_denoise.strength -
                     postStrengthBefore) > 1e-8) {
          fail(QStringLiteral(
              "Undo merged duplicate Screen labels from different stages"));
          return;
        }
        undoParametersAction->trigger();
        const ParameterState afterPreStrengthUndo =
            first->documentStateSnapshot();
        if (std::abs(afterPreStrengthUndo.rparams.screen_denoise.strength -
                     preStrengthBefore) > 1e-8 ||
            std::abs(afterPreStrengthUndo.rparams.demosaiced_denoise.strength -
                     postStrengthBefore) > 1e-8) {
          fail(QStringLiteral(
              "Second Undo did not restore both Screen strength baselines"));
          return;
        }
        first->applyState(screenUndoBaseline);

        const QStringList captureDefaultKeys = {
            QStringLiteral("capture.gamma"),
            QStringLiteral("capture.mtf.scan_dpi"),
            QStringLiteral("capture.mtf.f_stop"),
            QStringLiteral("capture.mtf.pixel_pitch"),
            QStringLiteral("capture.mtf.sensor_fill_factor"),
            QStringLiteral("capture.mtf.wavelength.red"),
            QStringLiteral("capture.mtf.wavelength.green"),
            QStringLiteral("capture.mtf.wavelength.blue"),
            QStringLiteral("capture.mtf.wavelength.scalar")};
        for (const QString &key : captureDefaultKeys) {
          QToolButton *button = findParameterResetButton(key);
          if (!button || !button->property("parameterDefaultValue").isValid()) {
            fail(QStringLiteral(
                     "Workspace churn lost capture default/reset metadata for %1")
                     .arg(key));
            return;
          }
        }
        if (!resolutionResetButton || !resolutionField ||
            !redWavelengthResetButton || !redWavelengthField ||
            !redWavelengthSpin ||
            redWavelengthField->property("parameterSpecialStateValue")
                    .toDouble() != 0.0) {
          fail(QStringLiteral(
              "Workspace churn lost numeric default/sentinel rows"));
          return;
        }

        // Screen coordinates define the reference frame of solver points. Both
        // the visible button and the MainWindow slot must refuse a new basis
        // once any control point exists.
        if (!detectCoordinatesButton) {
          fail(QStringLiteral(
              "Workspace churn lost the Detect screen coordinates button"));
          return;
        }
        const ParameterState coordinateGuardBaseline =
            first->documentStateSnapshot();
        ParameterState coordinateNoPoints = coordinateGuardBaseline;
        coordinateNoPoints.solver.points.clear();
        first->applyState(coordinateNoPoints);
        if (!detectCoordinatesButton->isEnabled()) {
          fail(QStringLiteral(
              "Detect screen coordinates stayed disabled with no control points"));
          return;
        }
        ParameterState coordinateWithPoint = coordinateNoPoints;
        coordinateWithPoint.solver.add_point(
            {10, 10}, {0, 0}, colorscreen::solver_parameters::green);
        first->applyState(coordinateWithPoint);
        if (detectCoordinatesButton->isEnabled()) {
          fail(QStringLiteral(
              "Detect screen coordinates remained enabled with control points"));
          return;
        }
        first->onAutodetectCoordinatesRequested();
        if (first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral(
              "MainWindow started coordinate autodetection despite existing control points"));
          return;
        }
        first->applyState(coordinateGuardBaseline);
        first->statusBar()->clearMessage();

        // Detect Screen now owns the regular-screen workflow. The old Capture
        // "try luck" duplicate must stay gone, while screen-colour correction
        // belongs next to Detect screen. Once geometry is current and the
        // reconstruction mode is already selected, Workflow should explain
        // point visibility/editing rather than asking the user to select that
        // same mode again.
        QPushButton *legacyTryLuck = nullptr;
        for (QPushButton *button : inspector->findChildren<QPushButton *>()) {
          if (button && button->text() ==
                            QStringLiteral("Autodetect regular screen")) {
            legacyTryLuck = button;
            break;
          }
        }
        if (legacyTryLuck || !screenSwapColorsButton ||
            !showRegistrationPointsBox || !nextStepSummary ||
            !first->m_screenPanel ||
            !first->m_screenPanel->isAncestorOf(screenSwapColorsButton)) {
          fail(QStringLiteral(
              "Workspace churn lost the consolidated screen-registration workflow controls"));
          return;
        }

        const ParameterState workflowBaseline = first->documentStateSnapshot();
        const auto savedGeometryFitBaseline = first->m_geometryFitBaseline;
        const auto savedGeometryFitFailure = first->m_geometryFitFailureInputs;
        const auto savedGeometryFitPending = first->m_geometryFitPendingInputs;
        const auto savedGeometryFitPendingMesh =
            first->m_geometryFitPendingComputeMesh;
        const auto savedRenderType = first->m_renderTypeParams.type;
        const bool savedRegistrationVisibility =
            first->m_imageWidget->registrationPointsVisible();

        ParameterState workflowReady = workflowBaseline;
        workflowReady.rparams.capture_type =
            colorscreen::render_parameters::capture_transparency;
        workflowReady.scrToImg.type = colorscreen::Paget;
        workflowReady.scrToImg.center = {50, 50};
        workflowReady.scrToImg.coordinate1 = {6, 0};
        workflowReady.scrToImg.coordinate2 = {0, 6};
        workflowReady.scrToImg.mesh_trans = nullptr;
        workflowReady.solver.points.clear();
        workflowReady.solver.add_point(
            {40, 40}, {-1, -1}, colorscreen::solver_parameters::green);
        workflowReady.solver.add_point(
            {60, 40}, {1, -1}, colorscreen::solver_parameters::green);
        workflowReady.solver.add_point(
            {40, 60}, {-1, 1}, colorscreen::solver_parameters::green);
        first->applyState(workflowReady);
        first->m_geometryFitPendingInputs.reset();
        first->m_geometryFitPendingComputeMesh.reset();
        first->m_geometryFitFailureInputs.reset();
        first->m_geometryFitBaseline = first->documentStateSnapshot();
        first->m_renderTypeParams.type = colorscreen::render_type_interpolated;
        first->m_imageWidget->setShowRegistrationPoints(false);
        first->updateWorkflowSummary();

        const QString hiddenPointGuidance = nextStepSummary->text();
        if (hiddenPointGuidance.contains(QStringLiteral("choose Mode")) ||
            !hiddenPointGuidance.contains(
                QStringLiteral("Show Registration Points")) ||
            !hiddenPointGuidance.contains(QStringLiteral("Select (S)")) ||
            !hiddenPointGuidance.contains(QStringLiteral("Add Point (A)")) ||
            !hiddenPointGuidance.contains(QStringLiteral("Swap screen colors")) ||
            !screenSwapColorsButton->isEnabled()) {
          fail(QStringLiteral(
              "Workflow did not recognize the already-selected reconstruction mode or explain registration editing"));
          return;
        }

        first->m_imageWidget->setShowRegistrationPoints(true);
        QCoreApplication::processEvents();
        if (!nextStepSummary->text().contains(QStringLiteral("hide it"))) {
          fail(QStringLiteral(
              "Workflow did not refresh registration-point visibility guidance"));
          return;
        }

        first->m_geometryFitBaseline = savedGeometryFitBaseline;
        first->m_geometryFitFailureInputs = savedGeometryFitFailure;
        first->m_geometryFitPendingInputs = savedGeometryFitPending;
        first->m_geometryFitPendingComputeMesh = savedGeometryFitPendingMesh;
        first->m_renderTypeParams.type = savedRenderType;
        first->applyState(workflowBaseline);
        first->m_imageWidget->setShowRegistrationPoints(
            savedRegistrationVisibility);
        first->updateWorkflowSummary();

        // Exercise undo identity directly. Two edits deliberately use the
        // same human description: different keys must keep them separate,
        // while repeated updates carrying one key must coalesce.
        if (!undoParametersAction) {
          fail(QStringLiteral("Workspace churn lost the Undo action"));
          return;
        }
        const ParameterState undoBaseline = first->documentStateSnapshot();
        ParameterState firstKeyEdit = undoBaseline;
        firstKeyEdit.rparams.brightness = undoBaseline.rparams.brightness + 0.25;
        first->applySharedDocumentState(
            firstKeyEdit, QStringLiteral("Undo key smoke"),
            QStringLiteral("smoke.brightness"));
        ParameterState secondKeyEdit = first->documentStateSnapshot();
        secondKeyEdit.rparams.scan_mirror = !undoBaseline.rparams.scan_mirror;
        first->applySharedDocumentState(
            secondKeyEdit, QStringLiteral("Undo key smoke"),
            QStringLiteral("smoke.scan_mirror"));
        undoParametersAction->trigger();
        ParameterState afterDifferentKeyUndo = first->documentStateSnapshot();
        if (afterDifferentKeyUndo.rparams.brightness !=
                firstKeyEdit.rparams.brightness ||
            afterDifferentKeyUndo.rparams.scan_mirror !=
                undoBaseline.rparams.scan_mirror) {
          fail(QStringLiteral(
              "Undo merged distinct parameter keys sharing one label"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot() != undoBaseline) {
          fail(QStringLiteral(
              "Undo did not restore the baseline after distinct keyed edits"));
          return;
        }

        ParameterState sameKeyEdit1 = undoBaseline;
        sameKeyEdit1.rparams.brightness = undoBaseline.rparams.brightness + 0.5;
        first->applySharedDocumentState(
            sameKeyEdit1, QStringLiteral("Undo key smoke"),
            QStringLiteral("smoke.brightness"));
        ParameterState sameKeyEdit2 = first->documentStateSnapshot();
        sameKeyEdit2.rparams.brightness = undoBaseline.rparams.brightness + 0.75;
        first->applySharedDocumentState(
            sameKeyEdit2, QStringLiteral("Undo key smoke"),
            QStringLiteral("smoke.brightness"));
        undoParametersAction->trigger();
        if (first->documentStateSnapshot() != undoBaseline) {
          fail(QStringLiteral(
              "Undo failed to coalesce repeated updates for one parameter key"));
          return;
        }

        // A visible Reset is progressive disclosure: it appears only
        // after a keyed value differs from its real ParameterState default.
        // Reset is a separate undo gesture so one Undo restores the value
        // immediately before Reset, and a second Undo restores the baseline.
        const ParameterState resetBaseline = first->documentStateSnapshot();
        const ParameterState defaults;
        const double defaultResolution =
            defaults.rparams.sharpen.scanner_mtf.scan_dpi;
        double modifiedResolution = defaultResolution + 4321.0;
        if (std::abs(modifiedResolution -
                     resetBaseline.rparams.sharpen.scanner_mtf.scan_dpi) <
            0.1)
          modifiedResolution = defaultResolution + 3210.0;
        ParameterState modifiedCapture = resetBaseline;
        modifiedCapture.rparams.sharpen.scanner_mtf.scan_dpi =
            modifiedResolution;
        first->applySharedDocumentState(
            modifiedCapture, QStringLiteral("Resolution"),
            QStringLiteral("capture.mtf.scan_dpi"));
        if (std::abs(first->documentStateSnapshot()
                         .rparams.sharpen.scanner_mtf.scan_dpi -
                     modifiedResolution) >
                0.01 ||
            resolutionResetButton->isHidden() ||
            !resolutionField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Modified capture parameter did not expose Reset state"));
          return;
        }
        resolutionResetButton->click();
        if (std::abs(first->documentStateSnapshot()
                         .rparams.sharpen.scanner_mtf.scan_dpi -
                     defaultResolution) >
                0.01 ||
            !resolutionResetButton->isHidden() ||
            resolutionField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Capture Reset did not restore and hide the default state"));
          return;
        }
        undoParametersAction->trigger();
        if (std::abs(first->documentStateSnapshot()
                         .rparams.sharpen.scanner_mtf.scan_dpi -
                     modifiedResolution) >
                0.01 ||
            resolutionResetButton->isHidden() ||
            !resolutionField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Undo of capture Reset did not restore modified state"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot() != resetBaseline) {
          fail(QStringLiteral(
              "Capture default/reset smoke did not restore its baseline"));
          return;
        }

        // Zero wavelength means not explicitly configured. It must round-trip
        // as zero and occupy a distinct UI position from an explicit 380 nm,
        // even though 380 nm is the ordinary numeric minimum.
        const ParameterState wavelengthBaseline = first->documentStateSnapshot();
        if (wavelengthBaseline.rparams.sharpen.scanner_mtf.wavelengths[0] != 0 ||
            redWavelengthSpin->value() != 0 ||
            !redWavelengthSpin->text().contains(
                QStringLiteral("default"), Qt::CaseInsensitive) ||
            !redWavelengthResetButton->isHidden() ||
            redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Capture wavelength zero sentinel was not represented as default"));
          return;
        }

        ParameterState explicitWavelength = wavelengthBaseline;
        explicitWavelength.rparams.sharpen.scanner_mtf.wavelengths[0] = 380;
        first->applySharedDocumentState(
            explicitWavelength, QStringLiteral("Red wavelength"),
            QStringLiteral("capture.mtf.wavelength.red"));
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 380 ||
            redWavelengthSpin->value() != 380 ||
            redWavelengthSpin->text().contains(
                QStringLiteral("default"), Qt::CaseInsensitive) ||
            redWavelengthResetButton->isHidden() ||
            !redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Explicit minimum wavelength was confused with default sentinel"));
          return;
        }

        redWavelengthResetButton->click();
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 0 ||
            redWavelengthSpin->value() != 0 ||
            !redWavelengthResetButton->isHidden() ||
            redWavelengthField->property("parameterModified").toBool()) {
          fail(QStringLiteral(
              "Wavelength Reset did not restore explicit zero sentinel"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot()
                    .rparams.sharpen.scanner_mtf.wavelengths[0] != 380 ||
            redWavelengthSpin->value() != 380 ||
            redWavelengthResetButton->isHidden()) {
          fail(QStringLiteral(
              "Undo of wavelength Reset did not restore explicit minimum"));
          return;
        }
        undoParametersAction->trigger();
        if (first->documentStateSnapshot() != wavelengthBaseline) {
          fail(QStringLiteral(
              "Wavelength sentinel smoke did not restore its baseline"));
          return;
        }

        // Folding the guide is presentation state only. It must not hide the
        // processing tabs, and the smoke restores the user's previous fold
        // preference immediately after exercising the toggle.
        const bool wasExpanded = workflowToggle->isChecked();
        workflowToggle->click();
        if (workflowToggle->isChecked() == wasExpanded ||
            workflowStages->isHidden() == workflowToggle->isChecked() ||
            processingTabs->isHidden()) {
          fail(QStringLiteral(
              "Workspace churn could not fold the workflow guide independently of the processing tabs"));
          return;
        }
        workflowToggle->click();
        if (workflowToggle->isChecked() != wasExpanded ||
            workflowStages->isHidden() == workflowToggle->isChecked() ||
            processingTabs->isHidden()) {
          fail(QStringLiteral(
              "Workspace churn could not restore the workflow guide fold state"));
          return;
        }

        // Exercise the real non-dialog document persistence path on the second
        // (disposable) document before presentation churn begins.  This covers
        // dirty tracking, save-as-current semantics, Qt-only profile spots, a
        // non-dirty in-memory mutation, and full save -> reload restoration
        // without automating platform-native QFileDialog implementations.
        QTemporaryDir persistenceDir;
        if (!persistenceDir.isValid()) {
          fail(QStringLiteral(
              "Workspace churn could not create a temporary persistence directory"));
          return;
        }
        const QString persistenceFile =
            persistenceDir.filePath(QStringLiteral("workspace-roundtrip.par"));
        const ParameterState originalSecondState =
            second->documentStateSnapshot();
        ParameterState savedSecondState = originalSecondState;
        savedSecondState.rparams.scan_mirror =
            !savedSecondState.rparams.scan_mirror;
        savedSecondState.profileSpots.push_back({0.25, 0.5});
        savedSecondState.profileSpots.push_back({1.25, 1.5});
        second->applySharedDocumentState(
            savedSecondState, QStringLiteral("Persistence smoke edit"));
        if (second->documentStateSnapshot() != savedSecondState ||
            !second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Workspace churn persistence edit did not dirty the second document"));
          return;
        }
        if (!second->saveParametersToFile(persistenceFile) ||
            second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Workspace churn persistence save did not establish a clean document"));
          return;
        }

        ParameterState mutatedSecondState = savedSecondState;
        mutatedSecondState.rparams.scan_mirror =
            !mutatedSecondState.rparams.scan_mirror;
        mutatedSecondState.profileSpots.clear();
        second->applyState(mutatedSecondState);
        if (second->documentStateSnapshot() == savedSecondState ||
            second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Workspace churn direct mutation did not remain non-dirty"));
          return;
        }
        if (!second->loadParameterFile(persistenceFile) ||
            second->documentStateSnapshot() != savedSecondState ||
            second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Workspace churn save/reload did not restore the complete clean document state"));
          return;
        }

        // The workspace lifecycle test itself should start from the same
        // processing state as before the persistence probe.  The second
        // document is intentionally left clean; its temporary parameter-file
        // identity is irrelevant because this smoke process closes it later.
        second->applyState(originalSecondState);
        if (second->documentStateSnapshot() != originalSecondState ||
            second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Workspace churn could not restore the second document after persistence round trip"));
          return;
        }

        // Exercise the actual coordinate-result publisher. Previously it
        // mutated live parameters before changeParameters(), making the edit
        // look like a no-op and silently losing the undo/dirty transition.
        const ParameterState beforeCoordinates = first->getCurrentState();
        QUndoStack *coordinateUndo = first->findChild<QUndoStack *>();
        if (!coordinateUndo) {
          fail(QStringLiteral("Coordinate refinement smoke lost the undo stack"));
          return;
        }
        const int coordinateUndoIndex = coordinateUndo->index();
        colorscreen::finetune_result refinedCoordinates;
        refinedCoordinates.success = true;
        refinedCoordinates.center = beforeCoordinates.scrToImg.center;
        refinedCoordinates.center.x += 1;
        refinedCoordinates.coordinate1 = beforeCoordinates.scrToImg.coordinate1;
        refinedCoordinates.coordinate2 = beforeCoordinates.scrToImg.coordinate2;
        ParameterState expectedCoordinates = beforeCoordinates;
        expectedCoordinates.scrToImg.center = refinedCoordinates.center;
        expectedCoordinates.scrToImg.mesh_trans = nullptr;
        first->applyOptimizedCoordinates(refinedCoordinates);
        if (first->getCurrentState() != expectedCoordinates ||
            coordinateUndo->index() != coordinateUndoIndex + 1 ||
            !first->isDocumentModified()) {
          fail(QStringLiteral("Coordinate refinement did not create one undoable dirty edit"));
          return;
        }
        coordinateUndo->undo();
        if (first->getCurrentState() != beforeCoordinates ||
            coordinateUndo->index() != coordinateUndoIndex) {
          fail(QStringLiteral("Undo coordinate refinement did not restore its exact baseline"));
          return;
        }
        coordinateUndo->redo();
        if (first->getCurrentState() != expectedCoordinates) {
          fail(QStringLiteral("Redo coordinate refinement did not restore its result"));
          return;
        }
        coordinateUndo->undo();

        // A state-mutating one-shot must be cancelled as soon as the document
        // accepts another parameter snapshot. Its racing completion must still
        // execute cleanup but must never publish the stale result.
        MainWindow::OneShotOperation oneShotSmoke;
        oneShotSmoke.description = QStringLiteral("One-shot cancellation smoke");
        oneShotSmoke.progressTitle = QStringLiteral("One-shot progress smoke");
        oneShotSmoke.prerequisites = [first]() { return first != nullptr; };
        oneShotSmoke.onStart = [state](
            std::shared_ptr<colorscreen::progress_info>) {
          state->oneShotStarted = true;
        };
        oneShotSmoke.resultValid = []() { return true; };
        oneShotSmoke.applyResult = [state]() { state->oneShotApplied = true; };
        oneShotSmoke.onDone = [state]() { state->oneShotDone = true; };
        first->runOneShotOperation(
            std::move(oneShotSmoke),
            [state](colorscreen::progress_info *progress) {
              for (int i = 0; i < 100; ++i) {
                if (progress && progress->pool_cancel()) {
                  state->oneShotSawCancellation.store(true);
                  return;
                }
                QThread::msleep(2);
              }
            });
        if (!state->oneShotStarted) {
          fail(QStringLiteral(
              "Workspace churn one-shot request did not enter its start lifecycle"));
          return;
        }

        int oneShotProgressEntries = 0;
        for (const ProgressEntry &entry : first->m_activeProgresses) {
          if (entry.userVisible &&
              entry.title == QStringLiteral("One-shot progress smoke")) {
            ++oneShotProgressEntries;
            int matchingEntries = 0;
            for (const ProgressEntry &other : first->m_activeProgresses)
              if (other.info == entry.info)
                ++matchingEntries;
            if (!entry.row || !entry.rowActionButton || matchingEntries != 1) {
              fail(QStringLiteral("One-shot progress row duplicated its request or lost Cancel"));
              return;
            }
          }
        }
        if (oneShotProgressEntries != 1) {
          fail(QStringLiteral("One-shot did not register its dedicated progress row"));
          return;
        }

        // Give the source a distinctive processing-state sentinel without
        // dirtying the document (applyState is the same path used by
        // undo/redo). Track only the value this smoke test changes: under
        // sanitizers, unrelated post-load state may legitimately settle while
        // presentation churn is already running.
        ParameterState firstState = first->documentStateSnapshot();
        state->originalFirstScanMirror = firstState.rparams.scan_mirror;
        state->expectedFirstScanMirror = !state->originalFirstScanMirror;
        state->expectedSecondScanMirror =
            second->documentStateSnapshot().rparams.scan_mirror;
        firstState.rparams.scan_mirror = state->expectedFirstScanMirror;
        first->applyState(firstState);
        state->expectedStatesSet = true;
        if (!documentStatesPreserved()) {
          fail(QStringLiteral(
              "Workspace churn could not establish independent document processing-state sentinels"));
          return;
        }

        workspace->tileDocuments();
        schedule(1, 50, 40);
        return;
      }

      case 1: {
        if (!state->oneShotDone) {
          if (retryOrFail(QStringLiteral(
                  "Workspace churn one-shot cleanup did not finish after document-state cancellation")))
            return;
          return;
        }
        if (state->oneShotApplied ||
            !state->oneShotSawCancellation.load()) {
          fail(QStringLiteral(
              "Workspace churn allowed a cancelled one-shot result to publish"));
          return;
        }

        for (const ProgressEntry &entry : first->m_activeProgresses) {
          if (entry.title == QStringLiteral("One-shot progress smoke")) {
            fail(QStringLiteral("Cancelled one-shot retained its progress row"));
            return;
          }
        }

        if (!second || !view || workspace->isTabbedView() ||
            !hasExactWrappers()) {
          fail(QStringLiteral(
              "Workspace churn lost presentations while entering tiled MDI"));
          return;
        }
        const QList<QMdiSubWindow *> windows =
            mdiArea->subWindowList(QMdiArea::CreationOrder);
        for (int i = 0; i < windows.size(); ++i) {
          if (!windows[i] || !windows[i]->geometry().isValid()) {
            fail(QStringLiteral("Workspace churn produced an invalid tile"));
            return;
          }
          for (int j = i + 1; j < windows.size(); ++j) {
            if (!windows[i]->geometry()
                     .intersected(windows[j]->geometry())
                     .isEmpty()) {
              fail(QStringLiteral(
                  "Workspace churn produced overlapping MDI tiles"));
              return;
            }
          }
        }
        workspace->activateDocument(second);
        if (workspace->currentDocument() != second ||
            second->inspectorImageWidget() != second->primaryImageWidget()) {
          fail(QStringLiteral(
              "Tiled activation did not install the second document chrome"));
          return;
        }
        workspace->activateView(view);
        if (workspace->currentDocument() != first ||
            first->inspectorImageWidget() != view->imageWidget()) {
          fail(QStringLiteral(
              "Tiled activation did not route the shared inspector to the view"));
          return;
        }
        workspace->activateDocument(first);
        if (workspace->currentDocument() != first ||
            first->inspectorImageWidget() != first->primaryImageWidget()) {
          fail(QStringLiteral(
              "Tiled activation did not return the inspector to the primary view"));
          return;
        }
        // Cancel the real discovery request before delivering its completion.
        // Restoring the exact input snapshot must not resurrect cancelled work.
        const ParameterState focusBaseline = first->getCurrentState();
        first->onFindFocusAreasRequested();
        if (!first->m_focusAreaAnalysisRunning ||
            !first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral("Focus-area discovery bypassed the one-shot lifecycle"));
          return;
        }
        ParameterState edited = focusBaseline;
        edited.rparams.brightness += 0.125;
        first->applyState(edited);
        first->applyState(focusBaseline);
        schedule(100, 50, 200);
        return;
      }

      case 100: {
        if (first->m_focusAreaAnalysisRunning) {
          retryOrFail(QStringLiteral("Cancelled focus-area discovery did not finish cleanup"));
          return;
        }
        if (!first->m_focusAreaCandidates.empty() || first->m_focusAreaPrompt ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral("Cancelled focus-area discovery published stale output"));
          return;
        }

        // Run the real multi-area path too. A geometry edit clears the source
        // candidates; a late completion must not put its old vector back.
        const ParameterState focusBaseline = first->getCurrentState();
        first->m_focusAreaCandidates.resize(3);
        first->onAnalyzeFocusAreasRequested(
            colorscreen::finetune_scanner_mtf_sigma);
        if (!first->m_focusAreaAnalysisRunning ||
            !first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral("Multi-area focus fitting bypassed the one-shot lifecycle"));
          return;
        }
        ParameterState edited = focusBaseline;
        edited.scrToImg.center.x += 1;
        first->applyState(edited);
        first->applyState(focusBaseline);
        schedule(101, 50, 200);
        return;
      }

      case 101: {
        if (first->m_focusAreaAnalysisRunning) {
          retryOrFail(QStringLiteral("Cancelled multi-area fit did not finish cleanup"));
          return;
        }
        if (!first->m_focusAreaCandidates.empty() || first->m_focusAreaPrompt ||
            !first->m_focusAreaAnalysisResult.selected.empty() ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral("Cancelled multi-area fit restored stale diagnostics"));
          return;
        }
        for (const ProgressEntry &entry : first->m_activeProgresses) {
          if (entry.title == QStringLiteral("Find focus areas") ||
              entry.title == QStringLiteral("Analyze focus areas")) {
            fail(QStringLiteral("Cancelled focus-area operation retained its task row"));
            return;
          }
        }

        // Use synthetic successful diagnostics to exercise the actual Apply
        // dialog without requiring an expensive, numerically successful fit.
        const ParameterState baseline = first->getCurrentState();
        const auto scan = first->sharedImageData();
        constexpr uint64_t flags = colorscreen::finetune_scanner_mtf_sigma;
        colorscreen::finetune_focus_analysis_result analysis;
        analysis.success = true;
        analysis.joint_fit.success = true;
        analysis.joint_fit.scanner_mtf_sigma =
            baseline.rparams.sharpen.scanner_mtf.sigma + 0.25;
        auto showPrompt = [first, scan, baseline, analysis, flags]() -> QPushButton * {
          first->presentFocusAreaAnalysisResult(analysis, scan, baseline, flags);
          if (QMessageBox *box = first->m_focusAreaPrompt.data())
            for (QAbstractButton *button : box->buttons())
              if (box->buttonRole(button) == QMessageBox::AcceptRole)
                return qobject_cast<QPushButton *>(button);
          return nullptr;
        };

        ParameterState edited = baseline;
        edited.rparams.brightness += 0.125;
        first->presentFocusAreaAnalysisResult(analysis, scan, edited, flags);
        if (first->m_focusAreaPrompt) {
          fail(QStringLiteral("Focus Apply dialog accepted stale input parameters"));
          return;
        }
        first->presentFocusAreaAnalysisResult(
            analysis, second->sharedImageData(), baseline, flags);
        if (first->m_focusAreaPrompt) {
          fail(QStringLiteral("Focus Apply dialog accepted a different scan"));
          return;
        }

        QPointer<QPushButton> obsoleteApply = showPrompt();
        QPointer<QMessageBox> obsoletePrompt = first->m_focusAreaPrompt;
        if (!obsoleteApply || !obsoletePrompt) {
          fail(QStringLiteral("Focus analysis did not offer its Apply dialog"));
          return;
        }
        first->applyState(edited);
        first->applyState(baseline);
        if (first->m_focusAreaPrompt ||
            (obsoletePrompt && obsoletePrompt->isVisible())) {
          fail(QStringLiteral("Document edit did not dismiss obsolete focus approval"));
          return;
        }
        if (obsoleteApply)
          obsoleteApply->click();
        if (first->getCurrentState() != baseline) {
          fail(QStringLiteral("Old focus approval became publishable after restoring inputs"));
          return;
        }

        if (!showPrompt()) {
          fail(QStringLiteral("Focus approval could not be reopened"));
          return;
        }
        MainWindow::OneShotOperation replacement;
        replacement.description = QStringLiteral("Replace focus approval smoke");
        first->runOneShotOperation(std::move(replacement),
                                   [](colorscreen::progress_info *) {});
        if (first->m_focusAreaPrompt) {
          fail(QStringLiteral("New one-shot did not supersede pending focus approval"));
          return;
        }

        QPushButton *apply = showPrompt();
        QUndoStack *undo = first->findChild<QUndoStack *>();
        if (!apply || !undo) {
          fail(QStringLiteral("Focus approval lost its Apply control or undo stack"));
          return;
        }
        const int undoIndex = undo->index();
        ParameterState expected = baseline;
        expected.rparams.sharpen.scanner_mtf.sigma =
            analysis.joint_fit.scanner_mtf_sigma;
        apply->click();
        if (first->getCurrentState() != expected || first->m_focusAreaPrompt ||
            undo->index() != undoIndex + 1 || !first->isDocumentModified()) {
          fail(QStringLiteral("Accepted multi-area focus was not one undoable edit"));
          return;
        }
        undo->undo();
        if (first->getCurrentState() != baseline || undo->index() != undoIndex) {
          fail(QStringLiteral("Undo multi-area focus did not restore the exact baseline"));
          return;
        }
        undo->redo();
        if (first->getCurrentState() != expected) {
          fail(QStringLiteral("Redo multi-area focus did not restore the accepted fit"));
          return;
        }
        undo->undo();

        // A small Gaussian edge uses the same construction as the library's
        // blur-range regression, not an external image or a mocked result.
        state->beforeReference = first->getCurrentState();
        state->referenceDirectory = std::make_unique<QTemporaryDir>();
        colorscreen::image_data edge;
        if (!state->referenceDirectory->isValid() ||
            !edge.set_dimensions(256, 192, true, false)) {
          fail(QStringLiteral("Could not allocate reference MTF smoke fixture"));
          return;
        }
        edge.maxval = 65535;
        const double angle = 5.0 * std::acos(-1.0) / 180.0;
        for (int y = 0; y < edge.height; ++y)
          for (int x = 0; x < edge.width; ++x) {
            const double distance = (x - edge.width / 2.0) * std::cos(angle) +
                                    (y - edge.height / 2.0) * std::sin(angle);
            const auto value = static_cast<uint16_t>(std::lround(
                10000 + 20000 * (1 + std::erf(distance / std::sqrt(2.0)))));
            edge.put_rgb_pixel(x, y, {value, value, value});
          }
        const QString path = state->referenceDirectory->filePath(
            QStringLiteral("reference-edge.tif"));
        if (!edge.save_tiff(path.toUtf8().constData())) {
          fail(QStringLiteral("Could not write reference MTF smoke fixture"));
          return;
        }
        state->reference = app.createSlantedEdgeReference(first, path, true);
        schedule(200, 50, 200);
        return;
      }

      case 200: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || reference->m_referenceLoadPending ||
            !reference->sharedImageData()) {
          retryOrFail(QStringLiteral("Reference MTF smoke image did not load"));
          return;
        }
        state->referenceInputs = state->beforeReference;
        state->referenceInputs.rparams = colorscreen::render_parameters();
        state->referenceInputs.rparams.gamma = 1;
        state->referenceInputs.rparams.scan_mirror = state->expectedFirstScanMirror;
        first->applyState(state->referenceInputs);
        state->referenceUndoIndex = first->m_undoStack->index();
        for (int channel = 0; channel < 3; ++channel) {
          colorscreen::slanted_edge_parameters parameters;
          parameters.channel = channel;
          parameters.wavelength = 550;
          parameters.same_capture = channel != 0;
          parameters.name = "Reference smoke " + std::to_string(channel);
          parameters.source_filename = reference->referenceFile().toUtf8().toStdString();
          state->referenceParameters.push_back(parameters);
        }
        const auto area = reference->sharedImageData()->get_area();
        reference->startReferenceMtfMeasurement(area, {});
        if (!reference->m_referenceMtfProgress.expired()) {
          fail(QStringLiteral("Empty reference batch started work"));
          return;
        }
        reference->startReferenceMtfMeasurement(area, state->referenceParameters);
        auto progress = reference->m_referenceMtfProgress.lock();
        int progressEntries = 0;
        for (const ProgressEntry &entry : first->m_activeProgresses)
          if (entry.info == progress && entry.userVisible && entry.row &&
              entry.rowActionButton)
            ++progressEntries;
        if (!progress || !first->m_oneShotOperationQueue.hasActiveTasks() ||
            progressEntries != 1) {
          fail(QStringLiteral("Reference MTF did not register one document-owned Cancel row"));
          return;
        }
        schedule(201, 50, 400);
        return;
      }

      case 201: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || !reference->m_referenceMtfProgress.expired()) {
          retryOrFail(QStringLiteral("Reference MTF batch did not finish"));
          return;
        }
        const ParameterState measured = first->getCurrentState();
        const auto &curves = measured.rparams.sharpen.scanner_mtf.measurements;
        ParameterState withoutCurves = measured;
        withoutCurves.rparams.sharpen.scanner_mtf.measurements.clear();
        if (curves.size() != 3 || withoutCurves != state->referenceInputs ||
            first->m_undoStack->index() != state->referenceUndoIndex + 1 ||
            !first->isDocumentModified()) {
          fail(QStringLiteral("Reference MTF did not append one atomic undoable RGB batch"));
          return;
        }
        for (int channel = 0; channel < 3; ++channel)
          if (!curves[channel].size() || curves[channel].channel != channel ||
              curves[channel].same_capture != (channel != 0) ||
              curves[channel].source_filename !=
                  state->referenceParameters[channel].source_filename ||
              !curves[channel].has_spatial_metadata()) {
            fail(QStringLiteral("Reference MTF lost channel grouping or ROI provenance"));
            return;
          }
        first->m_undoStack->undo();
        if (first->getCurrentState() != state->referenceInputs) {
          fail(QStringLiteral("Undo reference MTF did not restore its exact inputs"));
          return;
        }
        first->m_undoStack->redo();
        if (first->getCurrentState() != measured) {
          fail(QStringLiteral("Redo reference MTF did not restore its exact batch"));
          return;
        }
        first->m_undoStack->undo();

        reference->startReferenceMtfMeasurement(
            reference->sharedImageData()->get_area(), state->referenceParameters);
        auto progress = reference->m_referenceMtfProgress.lock();
        ParameterState edited = state->referenceInputs;
        edited.rparams.brightness += 0.125;
        first->applyState(edited);
        first->applyState(state->referenceInputs);
        if (!progress || !progress->pool_cancel()) {
          fail(QStringLiteral("Document edit did not cancel reference MTF"));
          return;
        }
        schedule(202, 50, 400);
        return;
      }

      case 202: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || !reference->m_referenceMtfProgress.expired()) {
          retryOrFail(QStringLiteral("Stale reference MTF did not finish cleanup"));
          return;
        }
        if (first->getCurrentState() != state->referenceInputs ||
            reference->findChild<QMessageBox *>(QStringLiteral("ReferenceMtfError"))) {
          fail(QStringLiteral("Restoring inputs resurrected a stale reference result or error"));
          return;
        }
        // The first channel is valid. Failure in a later channel must discard it.
        auto invalidBatch = state->referenceParameters;
        invalidBatch[1].channel = 9;
        reference->startReferenceMtfMeasurement(
            reference->sharedImageData()->get_area(), std::move(invalidBatch));
        schedule(203, 50, 400);
        return;
      }

      case 203: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || !reference->m_referenceMtfProgress.expired()) {
          retryOrFail(QStringLiteral("Failed reference batch did not finish cleanup"));
          return;
        }
        auto *error = reference->findChild<QMessageBox *>(QStringLiteral("ReferenceMtfError"));
        if (!error || first->getCurrentState() != state->referenceInputs ||
            first->m_undoStack->index() != state->referenceUndoIndex) {
          fail(QStringLiteral("Failed reference batch applied a partial channel group"));
          return;
        }
        error->accept();
        reference->startReferenceMtfMeasurement(
            reference->sharedImageData()->get_area(), state->referenceParameters);
        auto progress = reference->m_referenceMtfProgress.lock();
        reference->reloadReferenceImage();
        if (!progress || !progress->pool_cancel() ||
            !reference->m_referenceMtfProgress.expired()) {
          fail(QStringLiteral("Reference reload did not cancel its measurement"));
          return;
        }
        schedule(204, 50, 400);
        return;
      }

      case 204: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || reference->m_referenceLoadPending ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Reference reload/cancellation did not settle"));
          return;
        }
        if (first->getCurrentState() != state->referenceInputs) {
          fail(QStringLiteral("Reloaded reference published its old measurement"));
          return;
        }
        const auto area = reference->sharedImageData()->get_area();
        reference->startReferenceMtfMeasurement(area, state->referenceParameters);
        auto oldProgress = reference->m_referenceMtfProgress.lock();
        reference->startReferenceMtfMeasurement(area, state->referenceParameters);
        auto newProgress = reference->m_referenceMtfProgress.lock();
        if (!oldProgress || !newProgress || oldProgress == newProgress ||
            !oldProgress->pool_cancel() || newProgress->pool_cancel()) {
          fail(QStringLiteral("Reference MTF replacement lost request-local cancellation"));
          return;
        }
        schedule(205, 50, 400);
        return;
      }

      case 205: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || !reference->m_referenceMtfProgress.expired() ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Replacement reference MTF did not settle"));
          return;
        }
        if (first->getCurrentState().rparams.sharpen.scanner_mtf.measurements.size() != 3) {
          fail(QStringLiteral("Superseded reference MTF published or reset the newer request"));
          return;
        }
        first->m_undoStack->undo();
        if (first->getCurrentState() != state->referenceInputs) {
          fail(QStringLiteral("Replacement reference MTF did not form one undo step"));
          return;
        }
        reference->startReferenceMtfMeasurement(
            reference->sharedImageData()->get_area(), state->referenceParameters);
        MainWindow::OneShotOperation replacement;
        replacement.description = QStringLiteral("Reference cancellation isolation smoke");
        replacement.onStart = [state](
            std::shared_ptr<colorscreen::progress_info> progress) {
          state->referenceReplacementProgress = progress;
        };
        replacement.onDone = [state]() { state->referenceReplacementDone = true; };
        first->runOneShotOperation(std::move(replacement),
                                   [](colorscreen::progress_info *) {});
        reference->cancelReferenceMtfMeasurement();
        if (!state->referenceReplacementProgress ||
            state->referenceReplacementProgress->pool_cancel()) {
          fail(QStringLiteral("Reference cancellation stopped another document request"));
          return;
        }
        schedule(206, 50, 400);
        return;
      }

      case 206: {
        ImageViewWindow *reference = state->reference.data();
        if (!reference || !state->referenceReplacementDone) {
          retryOrFail(QStringLiteral("Unrelated replacement did not finish"));
          return;
        }
        reference->startReferenceMtfMeasurement(
            reference->sharedImageData()->get_area(), state->referenceParameters);
        auto progress = reference->m_referenceMtfProgress.lock();
        if (!app.closeView(reference) || !progress || !progress->pool_cancel()) {
          fail(QStringLiteral("Closing the reference did not cancel its measurement"));
          return;
        }
        schedule(207, 50, 400);
        return;
      }

      case 207: {
        if (state->reference || first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Closed reference work did not settle"));
          return;
        }
        if (first->getCurrentState() != state->referenceInputs) {
          fail(QStringLiteral("Closed reference published a late measurement"));
          return;
        }
        for (const ProgressEntry &entry : first->m_activeProgresses)
          if (entry.title == QStringLiteral("Reference MTF measurement")) {
            fail(QStringLiteral("Reference MTF left a stale progress row"));
            return;
          }
        first->applyState(state->beforeReference);

        // Measured-MTF model fitting is now one document-owned final-result
        // operation. Use a tiny explicit no-variable empirical fit so the
        // smoke exercises real validation/objective/publication without
        // spending time in an optimizer. The dialog's edited sigma makes the
        // accepted result a visible, undoable state change.
        state->mtfFitBaseline = state->beforeReference;
        colorscreen::mtf_parameters &baselineMtf =
            state->mtfFitBaseline.rparams.sharpen.scanner_mtf;
        baselineMtf = colorscreen::mtf_parameters();
        baselineMtf.model = colorscreen::mtf_model::empirical_fallback;
        baselineMtf.sigma = 0.125;
        baselineMtf.blur_diameter = 1.0;
        baselineMtf.measured_mtf_idx = -1;
        colorscreen::mtf_measurement curve;
        curve.name = "Workspace MTF fit smoke";
        curve.channel = 1;
        curve.add_value(0.0, 100.0);
        curve.add_value(0.25, 78.0);
        curve.add_value(0.5, 52.0);
        baselineMtf.measurements.push_back(curve);
        first->applyState(state->mtfFitBaseline);

        state->mtfFitInput = baselineMtf;
        state->mtfFitInput.sigma = 0.25;
        state->mtfFitOptions = colorscreen::mtf_estimation_options();
        state->mtfFitOptions.model = colorscreen::mtf_model::empirical_fallback;
        state->mtfFitExpected = state->mtfFitBaseline;
        state->mtfFitExpected.rparams.sharpen.scanner_mtf = state->mtfFitInput;
        state->mtfFitUndoIndex = first->m_undoStack->index();
        if (!first->requestMtfModelFit(state->mtfFitBaseline, state->mtfFitInput,
                                      state->mtfFitOptions, 0) ||
            !first->m_mtfFitRunning || first->m_mtfFitProgress.expired()) {
          fail(QStringLiteral("Measured-MTF model fit did not start as a document operation"));
          return;
        }
        int fitRows = 0;
        for (const ProgressEntry &entry : first->m_activeProgresses)
          if (entry.title == QStringLiteral("MTF model fit")) {
            ++fitRows;
            if (!entry.userVisible || !entry.row || !entry.rowActionButton) {
              fail(QStringLiteral("MTF model fit lost its dedicated Cancel row"));
              return;
            }
          }
        if (fitRows != 1) {
          fail(QStringLiteral("MTF model fit registered duplicate/missing progress rows"));
          return;
        }
        schedule(208, 50, 200);
        return;
      }

      case 208: {
        if (first->m_mtfFitRunning ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Measured-MTF model fit did not finish"));
          return;
        }
        auto *resultDialog = first->findChild<QMessageBox *>(
            QStringLiteral("MtfFitResultDialog"));
        if (!resultDialog || first->getCurrentState() != state->mtfFitExpected ||
            first->m_undoStack->index() != state->mtfFitUndoIndex + 1 ||
            !first->m_mtfFitBaseline ||
            !first->m_mtfFitBaseline->fit_inputs_equal_p(state->mtfFitInput) ||
            first->m_mtfFitRms < 0 ||
            !first->mtfCalibrationSummary().contains(QStringLiteral("model current"))) {
          fail(QStringLiteral("Successful measured-MTF fit lost state, provenance, or Undo"));
          return;
        }
        resultDialog->accept();
        first->m_undoStack->undo();
        if (first->getCurrentState() != state->mtfFitBaseline ||
            first->m_undoStack->index() != state->mtfFitUndoIndex ||
            !first->mtfCalibrationSummary().contains(QStringLiteral("model stale"))) {
          fail(QStringLiteral("Undo measured-MTF fit did not restore its exact baseline"));
          return;
        }
        first->m_undoStack->redo();
        if (first->getCurrentState() != state->mtfFitExpected ||
            !first->mtfCalibrationSummary().contains(QStringLiteral("model current"))) {
          fail(QStringLiteral("Redo measured-MTF fit did not restore its fitted model"));
          return;
        }
        first->m_undoStack->undo();

        // Editing and then restoring the exact inputs must not resurrect a
        // completion that was cancelled while those inputs were stale.
        if (!first->requestMtfModelFit(state->mtfFitBaseline, state->mtfFitInput,
                                       state->mtfFitOptions, 0)) {
          fail(QStringLiteral("Could not start stale measured-MTF fit smoke"));
          return;
        }
        state->mtfFitCancelledProgress = first->m_mtfFitProgress.lock();
        ParameterState edited = state->mtfFitBaseline;
        edited.rparams.brightness += 0.125;
        first->applyState(edited);
        first->applyState(state->mtfFitBaseline);
        if (!state->mtfFitCancelledProgress ||
            !state->mtfFitCancelledProgress->pool_cancel()) {
          fail(QStringLiteral("Document edit did not cancel measured-MTF fitting"));
          return;
        }
        schedule(209, 50, 200);
        return;
      }

      case 209: {
        if (first->m_mtfFitRunning ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Cancelled measured-MTF fit did not clean up"));
          return;
        }
        if (first->getCurrentState() != state->mtfFitBaseline ||
            first->findChild<QMessageBox *>(QStringLiteral("MtfFitResultDialog")) ||
            first->findChild<QMessageBox *>(QStringLiteral("MtfFitErrorDialog"))) {
          fail(QStringLiteral("Restoring inputs resurrected a cancelled measured-MTF fit"));
          return;
        }
        for (const ProgressEntry &entry : first->m_activeProgresses)
          if (entry.title == QStringLiteral("MTF model fit")) {
            fail(QStringLiteral("Cancelled measured-MTF fit left a stale progress row"));
            return;
          }

        // A validation failure is a completed current request: record failure
        // provenance and report it, but leave the document and Undo stack alone.
        colorscreen::mtf_estimation_options invalid = state->mtfFitOptions;
        invalid.include_measurements = {false};
        if (!first->requestMtfModelFit(state->mtfFitBaseline, state->mtfFitInput,
                                       invalid, 0)) {
          fail(QStringLiteral("Could not start invalid measured-MTF fit smoke"));
          return;
        }
        schedule(210, 50, 200);
        return;
      }

      case 210: {
        if (first->m_mtfFitRunning ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Failed measured-MTF fit did not settle"));
          return;
        }
        auto *errorDialog = first->findChild<QMessageBox *>(
            QStringLiteral("MtfFitErrorDialog"));
        if (!errorDialog || first->getCurrentState() != state->mtfFitBaseline ||
            first->m_undoStack->index() != state->mtfFitUndoIndex ||
            !first->m_mtfFitFailureInputs ||
            !first->m_mtfFitFailureInputs->fit_inputs_equal_p(
                state->mtfFitBaseline.rparams.sharpen.scanner_mtf) ||
            !first->mtfCalibrationSummary().contains(QStringLiteral("fit failed"))) {
          fail(QStringLiteral("Failed measured-MTF fit changed state or lost failure provenance"));
          return;
        }
        errorDialog->accept();

        // Setup-dialog acceptance is snapshot-bound too: a document edit while
        // the dialog is open must reject the old settings before any worker starts.
        QToolButton *scannerProperties = first->m_sharpnessPanel
            ? first->m_sharpnessPanel->findChild<QToolButton *>(
                  QStringLiteral("ScannerCameraPropertiesToggle"))
            : nullptr;
        if (scannerProperties && !scannerProperties->isChecked())
          scannerProperties->click();
        QPushButton *fitButton = first->m_sharpnessPanel
            ? first->m_sharpnessPanel->findChild<QPushButton *>(
                  QStringLiteral("MtfFitButton"))
            : nullptr;
        if (!fitButton || !fitButton->isEnabled()) {
          fail(QStringLiteral("Measured-MTF fit button is unavailable for stale-dialog smoke"));
          return;
        }
        fitButton->click();
        QDialog *fitDialog = first->m_sharpnessPanel->findChild<QDialog *>(
            QStringLiteral("MtfFitDialog"));
        if (!fitDialog) {
          fail(QStringLiteral("Measured-MTF fit dialog did not open"));
          return;
        }
        ParameterState edited = state->mtfFitBaseline;
        edited.rparams.brightness += 0.25;
        first->applyState(edited);
        fitDialog->accept();
        first->applyState(state->mtfFitBaseline);
        auto *staleDialog = first->m_sharpnessPanel->findChild<QMessageBox *>(
            QStringLiteral("MtfFitStaleDialog"));
        if (!staleDialog || first->m_mtfFitRunning ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          fail(QStringLiteral("Stale measured-MTF setup dialog started background work"));
          return;
        }
        staleDialog->accept();

        // A newer final-result operation must supersede a model fit through the
        // same shared queue, without publishing a late result or error.
        if (!first->requestMtfModelFit(state->mtfFitBaseline, state->mtfFitInput,
                                       state->mtfFitOptions, 0)) {
          fail(QStringLiteral("Could not start measured-MTF supersession smoke"));
          return;
        }
        auto fitProgress = first->m_mtfFitProgress.lock();
        MainWindow::OneShotOperation replacement;
        replacement.description = QStringLiteral("Supersede MTF model fit smoke");
        first->runOneShotOperation(std::move(replacement),
                                   [](colorscreen::progress_info *) {});
        if (!fitProgress || !fitProgress->pool_cancel()) {
          fail(QStringLiteral("New final-result operation did not cancel MTF model fit"));
          return;
        }
        schedule(211, 50, 200);
        return;
      }

      case 211: {
        if (first->m_mtfFitRunning ||
            first->m_oneShotOperationQueue.hasActiveTasks()) {
          retryOrFail(QStringLiteral("Superseded measured-MTF fit did not clean up"));
          return;
        }
        if (first->getCurrentState() != state->mtfFitBaseline ||
            first->findChild<QMessageBox *>(QStringLiteral("MtfFitResultDialog")) ||
            first->findChild<QMessageBox *>(QStringLiteral("MtfFitErrorDialog"))) {
          fail(QStringLiteral("Superseded measured-MTF fit published a late result"));
          return;
        }
        for (const ProgressEntry &entry : first->m_activeProgresses)
          if (entry.title == QStringLiteral("MTF model fit")) {
            fail(QStringLiteral("Superseded measured-MTF fit left a progress row"));
            return;
          }

        first->applyState(state->beforeReference);
        state->referenceDirectory.reset();
        workspace->activateDocument(first);
        workspace->cascadeDocuments();
        schedule(2, 50, 40);
        return;
      }

      case 2: {
        if (!second || !view || workspace->isTabbedView() ||
            !hasExactWrappers()) {
          fail(QStringLiteral(
              "Workspace churn lost presentations while cascading MDI"));
          return;
        }
        const QList<QMdiSubWindow *> windows =
            mdiArea->subWindowList(QMdiArea::CreationOrder);
        bool offset = false;
        for (int i = 1; i < windows.size(); ++i)
          offset = offset || windows[i]->pos() != windows[0]->pos();
        if (!offset) {
          fail(QStringLiteral(
              "Workspace churn cascade did not offset any subwindow"));
          return;
        }
        workspace->showTabbedDocuments();
        schedule(3, 50, 40);
        return;
      }

      case 3:
        if (!second || !view || !workspace->isTabbedView() ||
            !workspace->isTabBarVisible() || app.tabCount() != 3 ||
            !hasExactWrappers()) {
          fail(QStringLiteral(
              "Workspace churn did not restore the three-tab presentation"));
          return;
        }
        app.detachView(view);
        schedule(4, 50, 40);
        return;

      case 4:
        if (!second || !view || !view->isWindow() ||
            view->isWorkspaceEmbedded() || workspace->containsView(view) ||
            app.tabCount() != 2 || !workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            view->statusBar() != view->standaloneStatusBar() ||
            !view->standaloneStatusBar()->isVisible() ||
            first->statusBar() != workspace->statusBar()) {
          fail(QStringLiteral(
              "Workspace churn did not detach the ordinary view cleanly"));
          return;
        }
        app.attachView(view);
        schedule(5, 50, 40);
        return;

      case 5:
        if (!second || !view || !view->isWorkspaceEmbedded() ||
            !workspace->containsView(view) || app.tabCount() != 3 ||
            view->statusBar() != workspace->statusBar() ||
            view->standaloneStatusBar()->isVisible() ||
            workspace->currentDocument() != first ||
            first->inspectorImageWidget() != view->imageWidget()) {
          fail(QStringLiteral(
              "Workspace churn did not reattach the ordinary view cleanly"));
          return;
        }
        app.detachDocument(first);
        schedule(6, 50, 40);
        return;

      case 6:
        if (!second || !view || !first->isWindow() ||
            first->isWorkspaceEmbedded() || workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            !workspace->containsView(view) || app.tabCount() != 2 ||
            first->statusBar() != first->standaloneStatusBar() ||
            !first->standaloneStatusBar()->isVisible() ||
            view->statusBar() != workspace->statusBar() ||
            view->sourceDocument() != first ||
            view->sharedImageData() != first->sharedImageData()) {
          fail(QStringLiteral(
              "Workspace churn did not preserve a view across source detachment"));
          return;
        }
        workspace->activateView(view);
        schedule(7, 0, 40);
        return;

      case 7:
        if (!view || workspace->currentDocument() != first ||
            first->inspectorImageWidget() != view->imageWidget()) {
          fail(QStringLiteral(
              "Attached peer view could not control its detached source document"));
          return;
        }
        app.attachDocument(first);
        schedule(8, 50, 40);
        return;

      case 8:
        if (!second || !view || !first->isWorkspaceEmbedded() ||
            !workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            !workspace->containsView(view) || app.tabCount() != 3 ||
            first->statusBar() != workspace->statusBar()) {
          fail(QStringLiteral(
              "Workspace churn did not restore the detached source document"));
          return;
        }
        app.detachDocument(second);
        app.detachView(view);
        schedule(9, 50, 40);
        return;

      case 9:
        if (!second || !view || !workspace->containsDocument(first) ||
            workspace->containsDocument(second) || workspace->containsView(view) ||
            app.tabCount() != 1 || !workspace->isTabBarVisible() ||
            !workspace->isVisible() || !second->isWindow() || !view->isWindow()) {
          fail(QStringLiteral(
              "Workspace churn did not keep one normal tab during split presentation"));
          return;
        }
        app.attachAllDocuments();
        app.attachAllViews();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        schedule(10, 50, 40);
        return;

      case 10:
        if (!second || !view || app.tabCount() != 3 ||
            !workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            !workspace->containsView(view) || !first->isWorkspaceEmbedded() ||
            !second->isWorkspaceEmbedded() || !view->isWorkspaceEmbedded() ||
            first->statusBar() != workspace->statusBar() ||
            second->statusBar() != workspace->statusBar() ||
            view->statusBar() != workspace->statusBar()) {
          fail(QStringLiteral(
              "Workspace churn could not consolidate all presentations"));
          return;
        }
        if (!hasExactWrappers()) {
          if (retryOrFail(QStringLiteral(
                  "Workspace churn left stale MDI wrappers after consolidation")))
            return;
          return;
        }
        workspace->tileDocuments();
        schedule(11, 50, 40);
        return;

      case 11:
        if (!second || !view || workspace->isTabbedView() ||
            !hasExactWrappers()) {
          fail(QStringLiteral(
              "Workspace churn second MDI transition lost a presentation"));
          return;
        }
        workspace->showTabbedDocuments();
        schedule(12, 50, 40);
        return;

      case 12:
        if (!second || !view || !workspace->isTabbedView() ||
            app.tabCount() != 3 || !hasExactWrappers()) {
          fail(QStringLiteral(
              "Workspace churn second tabbed transition was incomplete"));
          return;
        }
        if (!app.closeView(view)) {
          fail(QStringLiteral(
              "Workspace churn could not close the attached peer view"));
          return;
        }
        schedule(13, 0, 40);
        return;

      case 13:
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (state->view || !app.viewWindows().isEmpty()) {
          if (retryOrFail(QStringLiteral(
                  "Workspace churn peer view did not finish closing")))
            return;
          return;
        }
        if (!second || app.documentWindows().size() != 2 ||
            app.tabCount() != 2 || !workspace->containsDocument(first) ||
            !workspace->containsDocument(second) ||
            !app.isDocumentPresentationOpen(first)) {
          fail(QStringLiteral(
              "Closing the churn peer damaged either source document"));
          return;
        }
        if (!second->close()) {
          fail(QStringLiteral(
              "Workspace churn second document rejected an ordinary close"));
          return;
        }
        schedule(14, 0, 40);
        return;

      case 14:
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (state->second || app.documentWindows().size() != 1) {
          if (retryOrFail(QStringLiteral(
                  "Workspace churn second document did not finish closing")))
            return;
          return;
        }
        if (app.tabCount() != 1 || !workspace->containsDocument(first) ||
            !workspace->isVisible() || !workspace->isTabBarVisible() ||
            !app.isDocumentPresentationOpen(first)) {
          fail(QStringLiteral(
              "Workspace churn did not leave one healthy source tab"));
          return;
        }
        app.detachDocument(first);
        schedule(15, 50, 40);
        return;

      case 15:
        if (!first->isWindow() || first->isWorkspaceEmbedded() ||
            app.tabCount() != 0 || workspace->isVisible()) {
          fail(QStringLiteral(
              "Workspace churn final sole-document detachment was incomplete"));
          return;
        }
        app.attachDocument(first);
        schedule(16, 50, 40);
        return;

      case 16: {
        if (!first->isWorkspaceEmbedded() ||
            !workspace->containsDocument(first) || app.tabCount() != 1 ||
            !workspace->isVisible() || !workspace->isTabBarVisible() ||
            first->statusBar() != workspace->statusBar()) {
          fail(QStringLiteral(
              "Workspace churn final sole-document reattachment was incomplete"));
          return;
        }
        // Restore only the sentinel changed by this smoke test. Reapplying the
        // whole pre-load snapshot here could overwrite unrelated parameters
        // that legitimately finished settling while sanitizers slowed the UI.
        ParameterState restoredState = first->documentStateSnapshot();
        restoredState.rparams.scan_mirror = state->originalFirstScanMirror;
        first->applyState(restoredState);
        state->expectedStatesSet = false;
        if (state->completed)
          state->completed();
        return;
      }

      default:
        fail(QStringLiteral("Workspace churn reached an invalid phase"));
        return;
      }
    };

    QTimer::singleShot(0, &app, [runPhase]() { (*runPhase)(0, 40); });
  };
  QTimer::singleShot(300, &app, [startWorkspaceChurn]() {
    (*startWorkspaceChurn)(80);
  });
}
