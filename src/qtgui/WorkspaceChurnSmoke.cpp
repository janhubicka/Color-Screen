#include "WorkspaceChurnSmoke.h"

#include "ColorScreenApplication.h"
#include "ImageViewWindow.h"
#include "MainWindow.h"
#include "MultiLineTabWidget.h"
#include "WorkspaceWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

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
