#include "DocumentLifecycleSmoke.h"

#include "ColorScreenApplication.h"
#include "ImageViewWindow.h"
#include "MainWindow.h"
#include "TaskQueue.h"
#include "WorkspaceWindow.h"
#include "../libcolorscreen/parameter-archive.h"

#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUndoStack>

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr int documentLifecycleFailure = 19;

struct DialogResponse {
  QString title;
  QMessageBox::StandardButton button = QMessageBox::NoButton;
};

struct DocumentLifecycleState {
  QPointer<WorkspaceWindow> workspace;
  QPointer<MainWindow> first;
  QPointer<MainWindow> second;
  QPointer<ImageViewWindow> view;
  QPointer<MainWindow> recoveryProbe;
  QPointer<QMessageBox> obsoleteImageLoadFailure;
  std::shared_ptr<colorscreen::image_data> reloadOutgoingScan;
  ParameterState failedOpenBaseline;
  QString failedOpenParameterPath;
  QString failedOpenImagePath;
  bool failedOpenParameterSuggested = false;
  bool failedOpenParameterJson = false;
  bool failedOpenRecoveryDirty = false;
  std::unique_ptr<QTemporaryDir> temporaryDirectory;
  QString recoveryProbeDirectory;
  QString recoveryExpectedImage;
  QString recoveryExpectedParameterPath;
  ParameterState recoveryExpectedState;
  bool recoveryStarted = false;
  QString firstParameters;
  QString firstImageFile;
  QString secondParameters;
  QByteArray firstBaselineParameters;
  bool firstInitialMirror = false;
  bool secondInitialMirror = false;
  bool workspaceCancelTested = false;
  std::function<void()> completed;
};

/** Locate the QMessageBox currently implementing TITLE.

    QApplication::activeModalWidget() is not reliable for native/offscreen
    dialogs on every Qt platform plugin.  Conversely, macOS is explicitly
    allowed to ignore QMessageBox window titles, so TITLE is only a preferred
    discriminator: the unique new active/visible QMessageBox is authoritative. */
QMessageBox *findMessageBox(const QString &title, QMessageBox *previousBox) {
  auto isNew = [previousBox](QMessageBox *box) {
    return box && box != previousBox;
  };
  auto hasExpectedTitle = [&title](QMessageBox *box) {
    return box && box->windowTitle() == title;
  };

  if (auto *active =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
    if (isNew(active))
      return active;
  }

  QMessageBox *titledHiddenFallback = nullptr;
  QMessageBox *visibleFallback = nullptr;
  QMessageBox *hiddenFallback = nullptr;
  for (QWidget *widget : QApplication::allWidgets()) {
    auto *candidate = qobject_cast<QMessageBox *>(widget);
    if (!isNew(candidate))
      continue;
    if (hasExpectedTitle(candidate)) {
      if (candidate->isVisible())
        return candidate;
      titledHiddenFallback = candidate;
      continue;
    }
    if (candidate->isVisible()) {
      if (!visibleFallback)
        visibleFallback = candidate;
    } else if (!hiddenFallback) {
      hiddenFallback = candidate;
    }
  }
  if (titledHiddenFallback)
    return titledHiddenFallback;
  return visibleFallback ? visibleFallback : hiddenFallback;
}

/** Click a known sequence of modal QMessageBox buttons as they appear.

    QFileDialog is intentionally not automated here.  The smoke establishes a
    real current parameter file first, so choosing Save exercises the ordinary
    synchronous save path rather than a platform-native Save As dialog. */
void queueDialogResponses(ColorScreenApplication &app,
                          std::vector<DialogResponse> responses) {
  struct ResponseState {
    std::vector<DialogResponse> responses;
    size_t index = 0;
    QPointer<QMessageBox> previousBox;
  };

  auto state = std::make_shared<ResponseState>();
  state->responses = std::move(responses);
  auto poll = std::make_shared<std::function<void(int)>>();
  const std::weak_ptr<std::function<void(int)>> weakPoll = poll;
  *poll = [&app, state, weakPoll](int attemptsLeft) {
    if (state->index >= state->responses.size())
      return;

    const DialogResponse &expected = state->responses[state->index];
    QMessageBox *box =
        findMessageBox(expected.title, state->previousBox.data());
    if (box) {
      bool answered = false;
      if (QAbstractButton *button = box->button(expected.button)) {
        button->click();
        answered = true;
      } else if (box->standardButtons().testFlag(expected.button)) {
        // Native platform helpers do not always expose their standard buttons
        // as clickable QWidget objects. QDialog::done() drives the same
        // QMessageBox result synchronously and also closes the native helper.
        box->done(expected.button);
        answered = true;
      }
      if (answered) {
        state->previousBox = box;
        ++state->index;
        if (auto retry = weakPoll.lock())
          QTimer::singleShot(0, &app, [retry]() { (*retry)(200); });
        return;
      }
    }

    if (attemptsLeft > 0) {
      if (auto retry = weakPoll.lock())
        QTimer::singleShot(10, &app,
                           [retry, attemptsLeft]() {
                             (*retry)(attemptsLeft - 1);
                           });
      return;
    }

    qCritical() << "Document lifecycle smoke did not see expected dialog"
                << expected.title << "button" << expected.button;
    if (box)
      box->reject();
    app.exit(documentLifecycleFailure);
  };
  QTimer::singleShot(0, &app, [poll]() { (*poll)(200); });
}

QByteArray readFile(const QString &fileName) {
  QFile file(fileName);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/** Verify the queue's publication gate without starting real image work.

    Two active requests are enough to reproduce both stale-result orderings:
    an older completion arriving before the newest result, and an older worker
    racing in after the newest result has already superseded it. */
bool taskQueueLatestRequestWins() {
  {
    TaskQueue queue;
    const int older = queue.requestRender();
    const int newer = queue.requestRender();
    if (queue.reportFinished(older, true))
      return false;
    if (!queue.reportFinished(newer, true))
      return false;
  }

  {
    TaskQueue queue;
    const int older = queue.requestRender();
    const int newer = queue.requestRender();
    if (!queue.reportFinished(newer, true))
      return false;
    if (queue.reportFinished(older, true))
      return false;
  }

  {
    TaskQueue queue;
    const int cancelled = queue.requestRender();
    queue.cancelAll();
    if (queue.reportFinished(cancelled, true))
      return false;
  }

  return true;
}

/** Exercise the real asynchronous queue completion path.

    Only the older request needs a QtConcurrent worker. Once that worker is
    definitely running, publish a newer request synchronously through the same
    TaskQueue. This cancels/supersedes the older request without requiring a
    second global-thread-pool slot. The older runAsync callback must still run
    for cleanup but receive publishResult == false. */
void startTaskQueueAsyncPublicationSmoke(ColorScreenApplication &app,
                                         std::function<void()> completed) {
  struct State {
    QPointer<TaskQueue> queue;
    std::shared_ptr<std::atomic_bool> olderStarted =
        std::make_shared<std::atomic_bool>(false);
    bool olderDone = false;
    bool exceptionDone = false;
    bool timeoutDone = false;
    int timeoutPendingReqId = 0;
    int timeoutFinishedCount = 0;
    std::vector<std::shared_ptr<colorscreen::progress_info>>
        timeoutStartedProgress;
    bool failed = false;
    std::function<void()> completed;
  };

  auto state = std::make_shared<State>();
  state->completed = std::move(completed);
  state->queue = new TaskQueue(&app);

  auto fail = [&app, state](const char *message) {
    if (state->failed)
      return;
    state->failed = true;
    if (state->queue)
      state->queue->cancelAll();
    qCritical() << message;
    app.exit(documentLifecycleFailure);
  };

  state->queue->runAsync(
      [started = state->olderStarted](colorscreen::progress_info *progress) {
        started->store(true, std::memory_order_release);
        while (!progress->pool_cancel())
          QThread::msleep(1);
      },
      [&app, state, fail](bool publishResult) {
        if (publishResult) {
          fail("Document lifecycle smoke published a superseded async result");
          return;
        }
        if (state->failed)
          return;
        state->olderDone = true;
        if (!state->queue) {
          fail("Document lifecycle smoke lost its TaskQueue before exception test");
          return;
        }

        // An exception escaping a generic one-shot worker must fail only that
        // request. It must not unwind out of QRunnable::run() and terminate the
        // process, and DONE must still run for cleanup with publishResult=false.
        state->queue->runAsync(
            [](colorscreen::progress_info *) {
              throw std::runtime_error(
                  "intentional TaskQueue worker exception smoke");
            },
            [&app, state, fail](bool exceptionPublishResult) {
              if (exceptionPublishResult) {
                fail("Document lifecycle smoke published a throwing async result");
                return;
              }
              if (state->failed)
                return;
              if (!state->queue || state->queue->hasActiveTasks()) {
                fail("Document lifecycle smoke retained a throwing async task");
                return;
              }
              state->exceptionDone = true;

              TaskQueue *throwingQueue = state->queue.data();
              state->queue = new TaskQueue(&app, 20);
              throwingQueue->deleteLater();

              // Queue two deliberately non-completing tasks and cancellation-
              // request both before adding a pending third request. The timeout
              // must evict already-cancelled workers by itself; no fourth
              // request is allowed to prod the queue.
              QObject::connect(
                  state->queue, &TaskQueue::progressStarted, &app,
                  [state](std::shared_ptr<colorscreen::progress_info> progress) {
                    state->timeoutStartedProgress.push_back(
                        std::move(progress));
                  });
              QObject::connect(
                  state->queue, &TaskQueue::progressFinished, &app,
                  [state](std::shared_ptr<colorscreen::progress_info>) {
                    ++state->timeoutFinishedCount;
                  });
              QObject::connect(
                  state->queue, &TaskQueue::triggerRender, &app,
                  [state, fail](
                      int reqId,
                      std::shared_ptr<colorscreen::progress_info>,
                      const QVariant &) {
                    if (reqId != state->timeoutPendingReqId)
                      return;
                    if (state->timeoutFinishedCount != 2) {
                      fail("Document lifecycle smoke did not evict both cancelled timed-out tasks");
                      return;
                    }
                    if (!state->queue->reportFinished(reqId, true)) {
                      fail("Document lifecycle smoke rejected timeout-started request");
                      return;
                    }
                    if (state->queue->hasActiveTasks()) {
                      fail("Document lifecycle smoke retained stale timed-out tasks");
                      return;
                    }
                    state->timeoutDone = true;
                    state->queue->deleteLater();
                    state->completed();
                  });

              state->queue->requestRender();
              state->queue->requestRender();
              if (state->timeoutStartedProgress.size() != 2) {
                fail("Document lifecycle smoke did not start timeout setup tasks");
                return;
              }
              for (const auto &progress : state->timeoutStartedProgress)
                progress->cancel();
              state->timeoutPendingReqId = state->queue->requestRender();
            });
      });

  auto supersede = std::make_shared<std::function<void(int)>>();
  const std::weak_ptr<std::function<void(int)>> weakSupersede = supersede;
  *supersede = [&app, state, fail, weakSupersede](int attemptsLeft) {
    if (state->failed || state->olderDone)
      return;
    if (!state->olderStarted->load(std::memory_order_acquire)) {
      if (attemptsLeft > 0) {
        if (auto retry = weakSupersede.lock())
          QTimer::singleShot(10, &app, [retry, attemptsLeft]() {
            (*retry)(attemptsLeft - 1);
          });
        return;
      }
      fail("Document lifecycle smoke did not start TaskQueue::runAsync worker");
      return;
    }

    if (!state->queue) {
      fail("Document lifecycle smoke lost its TaskQueue");
      return;
    }
    const int newer = state->queue->requestRender();
    if (!state->queue->reportFinished(newer, true)) {
      fail("Document lifecycle smoke rejected the newest queue result");
      return;
    }
  };

  QTimer::singleShot(0, &app, [supersede]() { (*supersede)(1000); });
  QTimer::singleShot(20000, &app, [state, fail]() {
    if (!state->failed && !state->timeoutDone)
      fail("Document lifecycle smoke timed out in TaskQueue robustness checks");
  });
}

} // namespace

/** Exercise transactional application exit and individual close decisions. */
void startDocumentLifecycleSmoke(ColorScreenApplication &app,
                                 std::function<void()> completed) {
  if (!taskQueueLatestRequestWins()) {
    qCritical() << "Document lifecycle smoke detected stale TaskQueue publication";
    app.exit(documentLifecycleFailure);
    return;
  }

  auto start = std::make_shared<std::function<void(int)>>();
  const std::weak_ptr<std::function<void(int)>> weakStart = start;
  *start = [&app, completed = std::move(completed), weakStart](int attemptsLeft) {
    const QList<MainWindow *> documents = app.documentWindows();
    WorkspaceWindow *workspace = app.workspaceWindow();
    bool ready = documents.size() == 2 && workspace && app.tabCount() == 2;
    for (MainWindow *document : documents) {
      if (!document || !document->sharedImageData() ||
          !workspace->containsDocument(document)) {
        ready = false;
        break;
      }
    }
    if (!ready) {
      if (attemptsLeft > 0) {
        if (auto retry = weakStart.lock()) {
          QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
            (*retry)(attemptsLeft - 1);
          });
          return;
        }
      }
      qCritical() << "Document lifecycle smoke requires two loaded document tabs";
      app.exit(documentLifecycleFailure);
      return;
    }

    auto state = std::make_shared<DocumentLifecycleState>();
    state->workspace = workspace;
    state->first = documents[0];
    state->second = documents[1];
    state->temporaryDirectory = std::make_unique<QTemporaryDir>();
    state->completed = completed;
    if (!state->temporaryDirectory->isValid()) {
      qCritical() << "Document lifecycle smoke could not create a temporary directory";
      app.exit(documentLifecycleFailure);
      return;
    }
    // The recovery fixture assigns a photographic image area to the first
    // document.  Only .cspar can retain that field when Save is chosen from
    // the unsaved-changes prompt.  Keep the peer on the legacy .par path to
    // preserve its independent failure/atomic-save coverage.
    state->firstParameters = state->temporaryDirectory->filePath(
        QStringLiteral("first.cspar"));
    state->secondParameters = state->temporaryDirectory->filePath(
        QStringLiteral("second.par"));

    MainWindow *first = state->first.data();
    MainWindow *second = state->second.data();
    state->firstImageFile = QFileInfo(first->currentImageFile())
                                .absoluteFilePath();
    state->firstInitialMirror =
        first->documentStateSnapshot().rparams.scan_mirror;
    state->secondInitialMirror =
        second->documentStateSnapshot().rparams.scan_mirror;

    // All structured parameter files now use JSON; legacy .par import and
    // failed JSON replacement are covered by the remaining lifecycle tests.
    if (!first->saveParametersToFile(
            state->firstParameters,
            MainWindow::ParameterSaveFormat::JsonV3) ||
        !second->saveParametersToFile(state->secondParameters)) {
      qCritical() << "Document lifecycle smoke could not establish clean parameter files";
      app.exit(documentLifecycleFailure);
      return;
    }
    // The current scan is external provenance, not a render parameter.
    // A fresh .cspar must be double-clickable even after Save As to another
    // directory. Verify the absolute scan identity and decoded dimensions.
    {
      colorscreen::scr_to_img_parameters g;
      colorscreen::scr_detect_parameters d;
      colorscreen::render_parameters r;
      colorscreen::solver_parameters sol;
      std::vector<colorscreen::point_t> spots;
      colorscreen::parameter_source_image source;
      std::string error;
      const QString expectedScan = QFileInfo(first->currentImageFile())
                                       .absoluteFilePath();
      if (!colorscreen::read_parameter_json_v3_file(
              state->firstParameters.toUtf8().constData(), &g, &d,
              &r, &sol, &spots, &error, &source) ||
          source.path.empty() ||
          source.width_px != first->m_scan->width ||
          source.height_px != first->m_scan->height ||
          QDir::cleanPath(
              QDir(QFileInfo(state->firstParameters).absolutePath())
                  .absoluteFilePath(
                      QString::fromStdString(source.path))) !=
              QDir::cleanPath(expectedScan)) {
        qCritical() << "Native .cspar did not link its original scan"
                    << QString::fromStdString(error);
        app.exit(documentLifecycleFailure);
        return;
      }
    }

    // Both fresh .cspar targets and subsequent ordinary Saves use native JSON.
    const QString defaultJsonParams = state->temporaryDirectory->filePath(
        QStringLiteral("default-native-json-v3.cspar"));
    if (first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3 ||
        !first->saveParametersToFile(defaultJsonParams) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3 ||
        !colorscreen::parameter_json_signature_p(
            defaultJsonParams.toUtf8().constData())) {
      qCritical() << "New .cspar target did not default to plain JSON v3";
      app.exit(documentLifecycleFailure);
      return;
    }
    const QByteArray initialDefaultJson = readFile(defaultJsonParams);
    if (initialDefaultJson.isEmpty() || initialDefaultJson.at(0) != '{' ||
        !first->saveParametersToFile(defaultJsonParams) ||
        readFile(defaultJsonParams) != initialDefaultJson ||
        !first->saveParametersToFile(state->firstParameters) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3) {
      qCritical() << "Ordinary Save did not preserve native JSON targets";
      app.exit(documentLifecycleFailure);
      return;
    }

    // Signature probes must never authorize replacement of unrelated,
    // malformed or future-schema JSON (or binary data) on an implicit Save.
    const QString unknownParams = state->temporaryDirectory->filePath(
        QStringLiteral("unknown-existing-version.cspar"));
    const ParameterState beforeUnknownSave = first->documentStateSnapshot();
    const QString targetBeforeUnknownSave = first->m_parameterFile.path;
    const auto formatBeforeUnknownSave = first->m_parameterFile.format;
    const int undoBeforeUnknownSave = first->m_undoStack->index();
    QByteArray unsupportedCompleteJson = initialDefaultJson;
    unsupportedCompleteJson.insert(1, "\"future_setting\": true,");
    const std::vector<QByteArray> unknownInputs = {
      unsupportedCompleteJson,
      QByteArray("future schema: do not overwrite\n"), QByteArray("{}"),
      QByteArray("{broken"), QByteArray("unexpected binary bytes"),
      QByteArray(R"json({"format":"org.colorscreen.parameters","schema_version":999})json"),
      QByteArray(R"json({"format":"org.colorscreen.parameters","schema_version":2})json")
    };
    for (const QByteArray &unknownBytes : unknownInputs) {
      QFile unknownFile(unknownParams);
      if (!unknownFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          unknownFile.write(unknownBytes) != unknownBytes.size()) {
        qCritical() << "Could not stage unknown .cspar save-guard fixture";
        app.exit(documentLifecycleFailure);
        return;
      }
      unknownFile.close();
      if (first->saveParametersToFile(unknownParams) ||
          readFile(unknownParams) != unknownBytes ||
          first->documentStateSnapshot() != beforeUnknownSave ||
          first->m_parameterFile.path != targetBeforeUnknownSave ||
          first->m_parameterFile.format != formatBeforeUnknownSave ||
          first->m_undoStack->index() != undoBeforeUnknownSave) {
        qCritical() << "Implicit Save overwrote invalid .cspar or changed state";
        app.exit(documentLifecycleFailure);
        return;
      }
      QMessageBox *warning = first->m_parameterSaveFailurePrompt.data();
      if (!warning || !warning->text().contains(QStringLiteral("unrecognized format"))) {
        qCritical() << "Unknown .cspar Save refusal lacks a useful warning";
        app.exit(documentLifecycleFailure);
        return;
      }
      warning->close();
    }
    // An explicit Save As selection still authorizes deliberate replacement.
    if (!first->saveParametersToFile(unknownParams, MainWindow::ParameterSaveFormat::JsonV3) ||
        !first->saveParametersToFile(state->firstParameters)) {
      qCritical() << "Explicit JSON replacement of an invalid destination failed";
      app.exit(documentLifecycleFailure);
      return;
    }

    // Explicit JSON Save As is also supported; Open and ordinary Save must
    // preserve the complete native state and use the same physical encoding.
    const QString jsonParams = state->temporaryDirectory->filePath(
        QStringLiteral("explicit-native-json-v3.cspar"));
    const ParameterState originalV3State = first->documentStateSnapshot();
    if (!first->saveParametersToFile(
            jsonParams, MainWindow::ParameterSaveFormat::JsonV3)) {
      qCritical() << "Document lifecycle smoke could not export native v3 JSON";
      app.exit(documentLifecycleFailure);
      return;
    }
    const QByteArray nativeBefore = readFile(jsonParams);
    if (nativeBefore.isEmpty() || nativeBefore.at(0) != '{' ||
        nativeBefore.contains("screen_alignment_version:") ||
        !colorscreen::parameter_json_signature_p(
            jsonParams.toUtf8().constData()) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3 ||
        !first->loadParameterFile(jsonParams) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3 ||
        first->documentStateSnapshot().scrToImg.final_angle !=
            originalV3State.scrToImg.final_angle ||
        first->documentStateSnapshot().rparams.gamma !=
            originalV3State.rparams.gamma ||
        !first->saveParametersToFile(jsonParams) ||
        readFile(jsonParams) != nativeBefore) {
      qCritical() << "Native v3 explicit Save As/Open/ordinary Save failed";
      app.exit(documentLifecycleFailure);
      return;
    }

    // The canonical parameter file owns relative MTF source paths. Moving
    // it with Save As must not relink a measurement to the new directory,
    // and both in-memory state and the reopened document must agree.
    const QString mtfOldDir =
        state->temporaryDirectory->filePath(QStringLiteral("mtf-old"));
    const QString mtfNewDir =
        state->temporaryDirectory->filePath(QStringLiteral("mtf-new"));
    const QString mtfOldFile =
        QDir(mtfOldDir).filePath(QStringLiteral("capture.cspar"));
    const QString mtfNewFile =
        QDir(mtfNewDir).filePath(QStringLiteral("capture.cspar"));
    if (!QDir().mkpath(mtfOldDir) || !QDir().mkpath(mtfNewDir) ||
        !first->saveParametersToFile(
            mtfOldFile, MainWindow::ParameterSaveFormat::JsonV3)) {
      qCritical() << "MTF Save As smoke could not establish an old document";
      app.exit(documentLifecycleFailure);
      return;
    }
    ParameterState withMtf = first->documentStateSnapshot();
    auto &mtfSources = withMtf.rparams.sharpen.scanner_mtf;
    mtfSources.measurements.clear();
    mtfSources.measured_mtf_idx = -1;
    colorscreen::mtf_measurement sourceRecord;
    sourceRecord.name = "MTF Save As evidence";
    sourceRecord.source_filename = "reference scan.tif";
    sourceRecord.channel = 1;
    sourceRecord.wavelength = 540;
    sourceRecord.add_value(0, 100);
    sourceRecord.add_value(0.25, 70);
    sourceRecord.add_value(0.5, 25);
    mtfSources.measurements.push_back(std::move(sourceRecord));
    if (!mtfSources.assign_missing_measurement_ids()) {
      qCritical() << "MTF Save As fixture could not assign document IDs";
      app.exit(documentLifecycleFailure);
      return;
    }
    const std::string expectedRecordId = mtfSources.measurements[0].id;
    first->applySharedDocumentState(withMtf, QStringLiteral("MTF source reference"));
    if (!first->saveParametersToFile(mtfOldFile)) {
      qCritical() << "MTF Save As smoke could not save the initial evidence";
      app.exit(documentLifecycleFailure);
      return;
    }
    /* This pre-Save-As Undo command contains relative MTF paths in BOTH
       snapshots. Undo/Redo after moving the document must rebase them too. */
    ParameterState renamedMtf = first->documentStateSnapshot();
    renamedMtf.rparams.sharpen.scanner_mtf.measurements[0].name =
        "MTF Save As renamed";
    first->applySharedDocumentState(renamedMtf,
                                    QStringLiteral("Rename MTF evidence"));
    if (!first->saveParametersToFile(
            mtfNewFile, MainWindow::ParameterSaveFormat::JsonV3)) {
      qCritical() << "MTF Save As smoke could not move the parameter file";
      app.exit(documentLifecycleFailure);
      return;
    }
    const ParameterState rebasedState = first->documentStateSnapshot();
    const auto &rebasedMtf = rebasedState.rparams.sharpen.scanner_mtf;
    const std::string expectedRebasedPath =
        rebasedMtf.measurements.empty()
            ? std::string()
            : rebasedMtf.measurements[0].source_filename;
    if (!first->m_undoStack || !first->m_undoStack->canUndo()) {
      qCritical() << "Save As unexpectedly discarded a valid MTF Undo edit";
      app.exit(documentLifecycleFailure);
      return;
    }
    first->m_undoStack->undo();
    const auto undoMtf =
        first->documentStateSnapshot().rparams.sharpen.scanner_mtf;
    if (undoMtf.measurements.size() != 1 ||
        undoMtf.measurements[0].name != "MTF Save As evidence" ||
        undoMtf.measurements[0].source_filename != expectedRebasedPath) {
      qCritical() << "Undo after Save As returned stale MTF source location";
      app.exit(documentLifecycleFailure);
      return;
    }
    first->m_undoStack->redo();
    const auto redoMtf =
        first->documentStateSnapshot().rparams.sharpen.scanner_mtf;
    if (redoMtf.measurements.size() != 1 ||
        redoMtf.measurements[0].name != "MTF Save As renamed" ||
        redoMtf.measurements[0].source_filename != expectedRebasedPath) {
      qCritical() << "Redo after Save As returned stale MTF source location";
      app.exit(documentLifecycleFailure);
      return;
    }
    if (rebasedMtf.measurements.size() != 1 ||
        rebasedMtf.measurements[0].id != expectedRecordId ||
        QString::fromStdString(rebasedMtf.measurements[0].source_filename)
                .replace(QLatin1Char('\\'), QLatin1Char('/')) !=
            QStringLiteral("../mtf-old/reference scan.tif") ||
        !first->loadParameterFile(mtfNewFile) ||
        first->documentStateSnapshot()
                .rparams.sharpen.scanner_mtf.measurements[0].source_filename !=
            rebasedMtf.measurements[0].source_filename) {
      qCritical() << "MTF Save As changed record identity or source location";
      app.exit(documentLifecycleFailure);
      return;
    }
    // Leave subsequent malformed-load/recovery assertions on their original
    // clean canonical state and target.
    if (!first->loadParameterFile(jsonParams)) {
      qCritical() << "MTF Save As test could not restore baseline document";
      app.exit(documentLifecycleFailure);
      return;
    }

    // First Save As is also a move of the MTF provenance base: before a
    // parameter file is established, source references resolve from the image
    // directory. Check both the saved document and an existing Undo gesture.
    const QString firstSaveImage = first->currentImageFile();
    const QString firstSaveDirectory =
        state->temporaryDirectory->filePath(QStringLiteral("mtf-first-save"));
    const QString firstSaveTarget =
        QDir(firstSaveDirectory).filePath(QStringLiteral("new.cspar"));
    if (firstSaveImage.isEmpty() || !QDir().mkpath(firstSaveDirectory)) {
      qCritical() << "MTF first-save smoke has no source image/directory";
      app.exit(documentLifecycleFailure);
      return;
    }
    const QString originalMtfSource =
        QDir(QFileInfo(firstSaveImage).absolutePath())
            .absoluteFilePath(QStringLiteral("reference scan.tif"));
    first->m_parameterFile.setSuggested(
        QDir(QFileInfo(firstSaveImage).absolutePath())
            .filePath(QStringLiteral("suggested.cspar")),
        MainWindow::ParameterFileState::Format::JsonV3);
    first->applySharedDocumentState(
        withMtf, QStringLiteral("Unsaved MTF measurement"));
    if (!first->m_undoStack || !first->m_undoStack->canUndo() ||
        !first->saveParametersToFile(
            firstSaveTarget, MainWindow::ParameterSaveFormat::JsonV3)) {
      qCritical() << "MTF first-save smoke failed to write a new parameter file";
      app.exit(documentLifecycleFailure);
      return;
    }
    const auto firstSavedMtf =
        first->documentStateSnapshot().rparams.sharpen.scanner_mtf;
    if (firstSavedMtf.measurements.size() != 1 ||
        firstSavedMtf.measurements[0].id != expectedRecordId ||
        QDir::cleanPath(
            QDir(QFileInfo(firstSaveTarget).absolutePath())
                .absoluteFilePath(
                    QString::fromStdString(
                        firstSavedMtf.measurements[0].source_filename))) !=
            QDir::cleanPath(originalMtfSource)) {
      qCritical() << "First Save As silently changed MTF source identity";
      app.exit(documentLifecycleFailure);
      return;
    }
    first->m_undoStack->undo();
    const auto firstSaveUndo =
        first->documentStateSnapshot().rparams.sharpen.scanner_mtf;
    first->m_undoStack->redo();
    const auto firstSaveRedo =
        first->documentStateSnapshot().rparams.sharpen.scanner_mtf;
    if (!firstSaveUndo.measurements.empty() ||
        firstSaveRedo.measurements.size() != 1 ||
        firstSaveRedo.measurements[0].source_filename !=
            firstSavedMtf.measurements[0].source_filename ||
        !first->loadParameterFile(firstSaveTarget) ||
        first->documentStateSnapshot()
                .rparams.sharpen.scanner_mtf.measurements[0].source_filename !=
            firstSavedMtf.measurements[0].source_filename ||
        !first->loadParameterFile(jsonParams)) {
      qCritical() << "First Save As MTF Undo/Redo/reopen lost its source base";
      app.exit(documentLifecycleFailure);
      return;
    }

    // A malformed v3 file is rejected transactionally. Do not accept it as
    // legacy CSP, overwrite the live document, or change its loaded target.
    const QString invalidJsonParams =
        state->temporaryDirectory->filePath(
            QStringLiteral("invalid-native-json-v3.cspar"));
    QByteArray truncatedNative = nativeBefore;
    truncatedNative.truncate(truncatedNative.size() / 2);
    QFile malformed(invalidJsonParams);
    if (!malformed.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        malformed.write(truncatedNative) != truncatedNative.size() ||
        !malformed.flush()) {
      qCritical() << "Native v3 smoke could not create truncated fixture";
      app.exit(documentLifecycleFailure);
      return;
    }
    malformed.close();
    const ParameterState acceptedBeforeFailure = first->documentStateSnapshot();
    if (first->loadParameterFile(invalidJsonParams) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3 ||
        first->m_parameterFile.path != QFileInfo(jsonParams).absoluteFilePath() ||
        first->documentStateSnapshot() != acceptedBeforeFailure) {
      qCritical() << "Invalid native v3 JSON partially changed Qt document";
      app.exit(documentLifecycleFailure);
      return;
    }
    if (QMessageBox *warning = first->findChild<QMessageBox *>(
            QStringLiteral("ParameterLoadFailureDialog"))) {
      warning->close();
    }

    // Restore the primary JSON target for the remaining lifecycle checks.
    if (!first->saveParametersToFile(
            state->firstParameters,
            MainWindow::ParameterSaveFormat::JsonV3) ||
        first->m_parameterFile.format !=
            MainWindow::ParameterFileState::Format::JsonV3) {
      qCritical() << "Native v3 smoke could not restore JSON target";
      app.exit(documentLifecycleFailure);
      return;
    }

    state->firstBaselineParameters = readFile(state->firstParameters);
    if (state->firstBaselineParameters.isEmpty()) {
      qCritical() << "Document lifecycle smoke could not read its baseline parameters";
      app.exit(documentLifecycleFailure);
      return;
    }

    state->view = app.createViewWindow(second);
    if (!state->view) {
      qCritical() << "Document lifecycle smoke could not create a peer view";
      app.exit(documentLifecycleFailure);
      return;
    }

    first->setDocumentMirror(!state->firstInitialMirror);
    second->setDocumentMirror(!state->secondInitialMirror);
    if (!first->documentDisplayName().endsWith(QLatin1Char('*')) ||
        !second->documentDisplayName().endsWith(QLatin1Char('*'))) {
      qCritical() << "Document lifecycle smoke could not dirty both documents";
      app.exit(documentLifecycleFailure);
      return;
    }

    auto runPhase = std::make_shared<std::function<void(int, int)>>();
    const std::weak_ptr<std::function<void(int, int)>> weakRunPhase = runPhase;
    *runPhase = [&app, state, weakRunPhase](int phase, int attemptsLeft) {
      auto fail = [&app](const QString &message) {
        qCritical().noquote() << message;
        app.exit(documentLifecycleFailure);
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
        qCritical() << "Document lifecycle smoke callback expired";
        app.exit(documentLifecycleFailure);
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

      MainWindow *first = state->first.data();
      MainWindow *second = state->second.data();
      ImageViewWindow *view = state->view.data();
      WorkspaceWindow *workspace = state->workspace.data();

      switch (phase) {
      case -1: {
        if (!first || !second || !view || !workspace) {
          fail(QStringLiteral(
              "Recovery round-trip smoke lost its source document setup"));
          return;
        }

        if (!state->recoveryStarted) {
          // Exercise ordinary legacy state, the Qt profile-spot postamble,
          // and every new structured render field that a .par recovery would
          // otherwise silently lose.
          ParameterState fixture = first->documentStateSnapshot();
          fixture.rparams.gamma = 2.0;
          fixture.rparams.image_area =
              colorscreen::int_optional_image_area(
                  colorscreen::int_image_area(1, 1, 4, 4));
          fixture.scrToImg.final_angle = 108.375;
          fixture.scrToImg.final_ratio = 0.803158;
          fixture.rparams.ignore_infrared = true;
          fixture.rparams.demosaiced_scaling =
              colorscreen::render_parameters::lanczos3_scaling;
          fixture.rparams.observer_whitepoint = colorscreen::xy_t(0.34, 0.35);
          // View-only output colourspace and gamut overlays do not belong in
          // a crash-recovery parameter snapshot. Recovery preserves actual
          // reconstruction inputs, not a particular canvas presentation.
          fixture.profileSpots.push_back({1.25, -2.5});
          first->applySharedDocumentState(
              fixture, QStringLiteral("Recovery round-trip fixture"));

          state->recoveryExpectedState = first->documentStateSnapshot();
          state->recoveryExpectedImage =
              QFileInfo(first->currentImageFile()).absoluteFilePath();
          state->recoveryExpectedParameterPath =
              QFileInfo(state->firstParameters).absoluteFilePath();
          state->recoveryProbeDirectory =
              state->temporaryDirectory->filePath(
                  QStringLiteral("recovery-roundtrip"));
          if (!QDir().mkpath(state->recoveryProbeDirectory)) {
            fail(QStringLiteral(
                "Recovery round-trip smoke could not create its payload directory"));
            return;
          }

          // Exercise the production writer without stealing the live
          // document's real crash-recovery directory.
          const QString originalRecoveryDirectory = first->m_recoveryDir;
          first->m_recoveryDir = state->recoveryProbeDirectory;
          first->saveRecoveryState();
          first->m_recoveryDir = originalRecoveryDirectory;

          const QDir recoveryDirectory(state->recoveryProbeDirectory);
          for (const QString &name :
               {QStringLiteral("recovery_image.txt"),
                QStringLiteral("recovery_params.cspar"),
                QStringLiteral("recovery_params_meta.txt")}) {
            if (!QFile::exists(recoveryDirectory.filePath(name))) {
              fail(QStringLiteral(
                       "Recovery round-trip smoke did not write %1")
                       .arg(name));
              return;
            }
          }

          auto *probe = new MainWindow(state->recoveryProbeDirectory);
          probe->hide();
          state->recoveryProbe = probe;
          if (!probe->restoreRecoveryState()) {
            delete probe;
            state->recoveryProbe = nullptr;
            fail(QStringLiteral(
                "Recovery round-trip smoke rejected its saved payload"));
            return;
          }

          state->recoveryStarted = true;
          schedule(-1, 50, 120);
          return;
        }

        MainWindow *probe = state->recoveryProbe.data();
        if (!probe || probe->m_imageLoad.pending || !probe->sharedImageData()) {
          if (retryOrFail(QStringLiteral(
                  "Recovery round-trip smoke did not finish restoring its image")))
            return;
          return;
        }

        const ParameterState recovered = probe->documentStateSnapshot();
        const auto &expected = state->recoveryExpectedState;
        const bool profileSpotMatches =
            recovered.profileSpots.size() == expected.profileSpots.size() &&
            !recovered.profileSpots.empty() &&
            recovered.profileSpots.back().x == 1.25 &&
            recovered.profileSpots.back().y == -2.5;
        if (QFileInfo(probe->currentImageFile()).absoluteFilePath() !=
                state->recoveryExpectedImage ||
            recovered.rparams.scan_mirror !=
                expected.rparams.scan_mirror ||
            !(recovered.rparams.image_area ==
              expected.rparams.image_area) ||
            recovered.scrToImg.final_angle !=
                expected.scrToImg.final_angle ||
            recovered.scrToImg.final_ratio !=
                expected.scrToImg.final_ratio ||
            recovered.rparams.gamma != 2.0 ||
            recovered.rparams.ignore_infrared !=
                expected.rparams.ignore_infrared ||
            recovered.rparams.demosaiced_scaling !=
                expected.rparams.demosaiced_scaling ||
            recovered.rparams.observer_whitepoint !=
                expected.rparams.observer_whitepoint ||
            !profileSpotMatches ||
            QFileInfo(probe->m_parameterFile.path).absoluteFilePath() !=
                state->recoveryExpectedParameterPath ||
            probe->m_parameterFile.suggested ||
            !probe->isDocumentModified()) {
          fail(QStringLiteral(
              "Recovery round-trip smoke did not restore document state/metadata"));
          return;
        }

        delete probe;
        state->recoveryProbe = nullptr;

        // Recovery now emits a complete plain JSON snapshot. For the
        // independent old-session compatibility probe, synthesize an actual
        // .par payload from the same fixture rather than pretending that
        // native JSON has a hidden CSP mirror.
        const QString validParams =
            QDir(state->recoveryProbeDirectory)
                .filePath(QStringLiteral("recovery_params.cspar"));
        if (!colorscreen::parameter_json_signature_p(
                validParams.toUtf8().constData())) {
          fail(QStringLiteral(
              "Recovery writer did not emit native JSON v3 parameters"));
          return;
        }

        FILE *legacyStream = std::tmpfile();
        if (!legacyStream) {
          fail(QStringLiteral("Recovery legacy compatibility stream failed"));
          return;
        }
        const auto &fixture = state->recoveryExpectedState;
        bool legacySerialized = colorscreen::save_csp_with_profile_spots(
            legacyStream, &fixture.scrToImg, &fixture.detect,
            &fixture.rparams, &fixture.solver, fixture.profileSpots);
        if (legacySerialized && std::fflush(legacyStream) != 0)
          legacySerialized = false;
        if (legacySerialized && std::fseek(legacyStream, 0, SEEK_SET) != 0)
          legacySerialized = false;
        std::string legacyPayload;
        char legacyBuffer[8192];
        while (legacySerialized) {
          const size_t count =
              std::fread(legacyBuffer, 1, sizeof(legacyBuffer), legacyStream);
          legacyPayload.append(legacyBuffer, count);
          if (count < sizeof(legacyBuffer)) {
            if (std::ferror(legacyStream))
              legacySerialized = false;
            break;
          }
        }
        std::fclose(legacyStream);
        if (!legacySerialized || legacyPayload.empty()) {
          fail(QStringLiteral("Recovery legacy compatibility serialization failed"));
          return;
        }
        const QByteArray legacyBytes(legacyPayload.data(),
                                     static_cast<qsizetype>(legacyPayload.size()));
        const QString legacyDirectory =
            state->temporaryDirectory->filePath(QStringLiteral("recovery-legacy"));
        if (!QDir().mkpath(legacyDirectory)) {
          fail(QStringLiteral("Recovery legacy compatibility directory failed"));
          return;
        }
        const QString legacyPath =
            QDir(legacyDirectory).filePath(QStringLiteral("recovery_params.par"));
        QFile legacyFile(legacyPath);
        if (!legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            legacyFile.write(legacyBytes) != legacyBytes.size() ||
            !legacyFile.flush()) {
          fail(QStringLiteral("Recovery legacy compatibility fixture failed"));
          return;
        }
        legacyFile.close();

        // Deliberately claim the original user archive was clean. A recovered
        // legacy-only snapshot cannot preserve the new structured fields and
        // therefore must still be marked dirty.
        const QString legacyMetaPath =
            QDir(legacyDirectory)
                .filePath(QStringLiteral("recovery_params_meta.txt"));
        const QByteArray legacyMeta =
            QByteArray("legacy-target.cspar\n0\n0\njson-v3\n");
        QFile legacyMetaFile(legacyMetaPath);
        if (!legacyMetaFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            legacyMetaFile.write(legacyMeta) != legacyMeta.size() ||
            !legacyMetaFile.flush()) {
          fail(QStringLiteral(
              "Recovery legacy compatibility metadata fixture failed"));
          return;
        }
        legacyMetaFile.close();

        auto *legacyProbe = new MainWindow(legacyDirectory);
        legacyProbe->hide();
        const bool legacyRestored = legacyProbe->restoreRecoveryState();
        const ParameterState legacyState = legacyProbe->documentStateSnapshot();
        if (!legacyRestored || legacyState.rparams.gamma != 2.0 ||
            legacyState.profileSpots != state->recoveryExpectedState.profileSpots ||
            legacyProbe->m_parameterFile.format !=
                MainWindow::ParameterFileState::Format::JsonV3 ||
            !legacyProbe->isDocumentModified()) {
          delete legacyProbe;
          fail(QStringLiteral(
              "Recovery migration lost compatibility with legacy-only snapshots"));
          return;
        }
        delete legacyProbe;

        // Truncate the native JSON recovery document, while leaving a valid
        // but stale legacy .par snapshot. Corruption must never cause an
        // implicit fallback to that older state.
        const QString corruptDirectory =
            state->temporaryDirectory->filePath(
                QStringLiteral("recovery-corrupt"));
        if (!QDir().mkpath(corruptDirectory)) {
          fail(QStringLiteral(
              "Recovery corruption smoke could not create its payload directory"));
          return;
        }
        const QByteArray completeArchive = readFile(validParams);
        if (completeArchive.size() < 16) {
          fail(QStringLiteral("Recovery JSON fixture is unexpectedly small"));
          return;
        }
        QByteArray corruptPayload = completeArchive;
        corruptPayload.truncate(corruptPayload.size() / 2);
        const QString corruptParams =
            QDir(corruptDirectory)
                .filePath(QStringLiteral("recovery_params.cspar"));
        QFile corruptFile(corruptParams);
        if (!corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            corruptFile.write(corruptPayload) != corruptPayload.size() ||
            !corruptFile.flush()) {
          fail(QStringLiteral(
              "Recovery corruption smoke could not write truncated JSON"));
          return;
        }
        corruptFile.close();
        const QString staleLegacyPath =
            QDir(corruptDirectory)
                .filePath(QStringLiteral("recovery_params.par"));
        QFile staleLegacyFile(staleLegacyPath);
        if (!staleLegacyFile.open(
                QIODevice::WriteOnly | QIODevice::Truncate) ||
            staleLegacyFile.write(legacyBytes) != legacyBytes.size() ||
            !staleLegacyFile.flush()) {
          fail(QStringLiteral(
              "Recovery corruption smoke could not create fallback trap"));
          return;
        }
        staleLegacyFile.close();

        auto *corruptProbe = new MainWindow(corruptDirectory);
        corruptProbe->hide();
        const ParameterState corruptBaseline =
            corruptProbe->documentStateSnapshot();
        if (!corruptProbe->restoreRecoveryState()) {
          delete corruptProbe;
          fail(QStringLiteral(
              "Recovery corruption smoke rejected the recovery directory"));
          return;
        }
        QMessageBox *recoveryWarning =
            corruptProbe->findChild<QMessageBox *>(
                QStringLiteral("RecoveryWarningDialog"));
        if (corruptProbe->documentStateSnapshot() != corruptBaseline ||
            !corruptProbe->isDocumentModified() ||
            !QFile::exists(corruptParams) ||
            !QFile::exists(staleLegacyPath) || !recoveryWarning ||
            !recoveryWarning->text().contains(
                QStringLiteral("No recovered parameters were applied"))) {
          if (recoveryWarning)
            recoveryWarning->close();
          delete corruptProbe;
          fail(QStringLiteral(
              "Corrupt native JSON partially mutated state, adopted stale legacy "
              "data, or lost its warning/payload"));
          return;
        }
        recoveryWarning->close();
        delete corruptProbe;

        // Main image loading is asynchronous. A missing file must leave the
        // hidden probe empty and publish a parent-owned warning without entering
        // a nested static QMessageBox event loop.
        const QString loadProbeDirectory =
            state->temporaryDirectory->filePath(
                QStringLiteral("image-load-failure"));
        if (!QDir().mkpath(loadProbeDirectory)) {
          fail(QStringLiteral(
              "Image-load failure smoke could not create its probe directory"));
          return;
        }
        auto *loadProbe = new MainWindow(loadProbeDirectory);
        loadProbe->hide();
        state->recoveryProbe = loadProbe;

        // Pair the missing image with a valid sidecar. The sidecar may guide
        // decoding (notably demosaic selection), but it belongs to the image
        // transaction and must not publish if the image itself fails.
        const QString missingImagePath =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-image-load.tif"));
        const QString missingLegacySidecarPath =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-image-load.par"));
        const QString missingJsonSidecarPath =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-image-load.cspar"));
        QFile::remove(missingLegacySidecarPath);
        QFile::remove(missingJsonSidecarPath);
        // The first document is now stored as an authoritative structured
        // archive because its photographic image area cannot be represented
        // in legacy CSP.  The second document retains a genuine legacy .par
        // target. Copy each sidecar in its native format.
        if (!QFile::copy(state->secondParameters, missingLegacySidecarPath) ||
            !QFile::copy(state->firstParameters, missingJsonSidecarPath)) {
          delete loadProbe;
          state->recoveryProbe = nullptr;
          fail(QStringLiteral(
              "Image-load failure smoke could not create legacy/JSON sidecars"));
          return;
        }

        state->failedOpenBaseline = loadProbe->getCurrentState();
        state->failedOpenParameterPath = loadProbe->m_parameterFile.path;
        state->failedOpenParameterSuggested =
            loadProbe->m_parameterFile.suggested;
        state->failedOpenParameterJson =
            loadProbe->m_parameterFile.format ==
            MainWindow::ParameterFileState::Format::JsonV3;
        state->failedOpenRecoveryDirty = loadProbe->m_recoveryDirty;
        state->failedOpenImagePath = loadProbe->m_currentImageFile;

        // Image replacement itself must invalidate progressive publication,
        // before the asynchronous loader can finish and change m_scan. Seed
        // synthetic current owners so this missing-file probe exercises the
        // synchronous replacement boundary without running expensive workers.
        const ParameterState outgoingState = loadProbe->getCurrentState();
        auto adaptiveProgress =
            std::make_shared<colorscreen::progress_info>();
        ++loadProbe->m_adaptiveSharpening.generation;
        loadProbe->m_adaptiveSharpening.baseline = outgoingState;
        loadProbe->m_adaptiveSharpening.scan = loadProbe->m_scan;
        loadProbe->m_adaptiveSharpening.progress = adaptiveProgress;

        // A superseded RAW/file decode is cancelled at the start of a new
        // request, not only ignored when its background work completes.
        auto outgoingImageProgress =
            std::make_shared<colorscreen::progress_info>();
        loadProbe->m_imageLoad.activeProgress = outgoingImageProgress;

        auto registrationProgress =
            std::make_shared<colorscreen::progress_info>();
        ++loadProbe->m_registrationDiscovery.generation;
        loadProbe->m_registrationDiscovery.expectedState = outgoingState;
        loadProbe->m_registrationDiscovery.scan = loadProbe->m_scan;
        loadProbe->m_registrationDiscovery.progress = registrationProgress;

        queueDialogResponses(
            app, {{QStringLiteral("Load Parameters?"), QMessageBox::Yes}});
        loadProbe->loadFile(missingImagePath, false);
        QMessageBox *sidecarPrompt =
            loadProbe->m_imageLoad.sidecarPrompt.data();
        if (!loadProbe->m_imageLoad.pending || !sidecarPrompt ||
            sidecarPrompt->objectName() !=
                QStringLiteral("ImageSidecarLoadPrompt") ||
            !sidecarPrompt->text().contains(
                QStringLiteral("missing-image-load.cspar")) ||
            sidecarPrompt->text().contains(
                QStringLiteral("missing-image-load.par")) ||
            !adaptiveProgress->pool_cancel() ||
            !outgoingImageProgress->pool_cancel() ||
            !loadProbe->m_imageLoad.activeProgress.expired() ||
            loadProbe->m_adaptiveSharpening.baseline ||
            loadProbe->m_adaptiveSharpening.scan ||
            !loadProbe->m_adaptiveSharpening.progress.expired() ||
            !registrationProgress->pool_cancel() ||
            loadProbe->m_registrationDiscovery.expectedState ||
            loadProbe->m_registrationDiscovery.scan ||
            !loadProbe->m_registrationDiscovery.progress.expired()) {
          delete loadProbe;
          state->recoveryProbe = nullptr;
          fail(QStringLiteral(
              "Image replacement did not synchronously invalidate progressive analysis"));
          return;
        }
        schedule(-2, 50, 120);
        return;
      }

      case -2: {
        MainWindow *probe = state->recoveryProbe.data();
        if (!probe) {
          fail(QStringLiteral(
              "Image-load failure smoke lost its hidden document"));
          return;
        }
        if (probe->m_imageLoad.pending) {
          retryOrFail(QStringLiteral("Missing image load did not finish"));
          return;
        }
        QMessageBox *loadFailure = probe->findChild<QMessageBox *>(
            QStringLiteral("ImageLoadFailureDialog"));
        if (!loadFailure || probe->m_imageLoad.sidecarPrompt ||
            probe->sharedImageData() ||
            probe->m_imageLoad.failurePrompt != loadFailure ||
            probe->getCurrentState() != state->failedOpenBaseline ||
            probe->m_parameterFile.path != state->failedOpenParameterPath ||
            probe->m_parameterFile.suggested !=
                state->failedOpenParameterSuggested ||
            (probe->m_parameterFile.format ==
             MainWindow::ParameterFileState::Format::JsonV3) !=
                state->failedOpenParameterJson ||
            probe->findChild<QMessageBox *>(
                QStringLiteral("ParameterLoadFailureDialog")) ||
            probe->m_recoveryDirty != state->failedOpenRecoveryDirty ||
            probe->m_currentImageFile != state->failedOpenImagePath ||
            !probe->canReuseForOpen()) {
          fail(QStringLiteral(
              "Failed image open leaked staged sidecar/target state or lost blank-window reuse"));
          return;
        }

        state->obsoleteImageLoadFailure = loadFailure;
        probe->loadFile(
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-image-load-retry.tif")),
            true);
        if (!probe->m_imageLoad.pending || probe->m_imageLoad.failurePrompt) {
          fail(QStringLiteral(
              "Retrying a failed image load did not supersede its old warning"));
          return;
        }
        schedule(-3, 50, 120);
        return;
      }

      case -3: {
        MainWindow *probe = state->recoveryProbe.data();
        if (!probe) {
          fail(QStringLiteral(
              "Retried image-load failure smoke lost its document"));
          return;
        }
        if (probe->m_imageLoad.pending) {
          retryOrFail(QStringLiteral("Retried missing image load did not finish"));
          return;
        }
        QMessageBox *loadFailure = probe->m_imageLoad.failurePrompt.data();
        if (!loadFailure || probe->sharedImageData() ||
            loadFailure->objectName() != QStringLiteral("ImageLoadFailureDialog") ||
            (state->obsoleteImageLoadFailure &&
             loadFailure == state->obsoleteImageLoadFailure)) {
          fail(QStringLiteral(
              "Retried missing image load did not replace its obsolete warning"));
          return;
        }
        loadFailure->accept();
        state->obsoleteImageLoadFailure.clear();
        delete probe;
        state->recoveryProbe = nullptr;

        // A failed current-image reload must restore the outgoing scan to the
        // primary ImageWidget. This is distinct from an ordinary missing-file
        // open above, which correctly leaves a fresh document empty.
        const QString reloadProbeDirectory =
            state->temporaryDirectory->filePath(
                QStringLiteral("image-reload-failure"));
        if (!QDir().mkpath(reloadProbeDirectory)) {
          fail(QStringLiteral(
              "Image-reload failure smoke could not create its probe directory"));
          return;
        }
        auto *reloadProbe = new MainWindow(reloadProbeDirectory);
        reloadProbe->hide();
        const QString missingReloadPath =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-current-image-reload.tif"));
        auto outgoingScan = std::make_shared<colorscreen::image_data>();
        if (!outgoingScan->set_dimensions(8, 8, false, true)) {
          delete reloadProbe;
          fail(QStringLiteral(
              "Image-reload failure smoke could not create its outgoing scan"));
          return;
        }
        reloadProbe->m_scan = outgoingScan;
        reloadProbe->m_currentImageFile =
            QFileInfo(missingReloadPath).absoluteFilePath();
        reloadProbe->m_imageWidget->setImage(
            outgoingScan, &reloadProbe->m_rparams, &reloadProbe->m_scrToImgParams,
            &reloadProbe->m_detectParams, &reloadProbe->m_renderTypeParams,
            &reloadProbe->m_solverParams);
        if (reloadProbe->m_imageWidget->sharedImageData() != outgoingScan) {
          delete reloadProbe;
          fail(QStringLiteral(
              "Image-reload failure smoke did not establish outgoing presentation"));
          return;
        }

        state->reloadOutgoingScan = outgoingScan;
        state->recoveryProbe = reloadProbe;
        reloadProbe->loadFile(missingReloadPath, true);
        if (!reloadProbe->m_imageLoad.pending ||
            reloadProbe->m_imageWidget->sharedImageData()) {
          fail(QStringLiteral(
              "Image-reload failure smoke did not enter replacement presentation"));
          return;
        }
        schedule(-4, 50, 120);
        return;
      }

      case -4: {
        MainWindow *probe = state->recoveryProbe.data();
        if (!probe || !state->reloadOutgoingScan) {
          fail(QStringLiteral(
              "Image-reload failure smoke lost its document or outgoing scan"));
          return;
        }
        if (probe->m_imageLoad.pending) {
          retryOrFail(QStringLiteral("Failed current-image reload did not finish"));
          return;
        }
        QMessageBox *loadFailure = probe->m_imageLoad.failurePrompt.data();
        if (!loadFailure ||
            probe->sharedImageData() != state->reloadOutgoingScan ||
            probe->m_imageWidget->sharedImageData() !=
                state->reloadOutgoingScan ||
            !loadFailure->text().contains(
                QStringLiteral("previous image remains open"),
                Qt::CaseInsensitive)) {
          fail(QStringLiteral(
              "Failed current-image reload did not restore outgoing presentation"));
          return;
        }
        loadFailure->accept();
        delete probe;
        state->recoveryProbe = nullptr;
        state->reloadOutgoingScan.reset();

        schedule(0, 0, 40);
        return;
      }

      case 0: {
        if (!first || !second || !view || !workspace || app.tabCount() != 3) {
          if (retryOrFail(QStringLiteral(
                  "Document lifecycle smoke did not reach the three-presentation setup")))
            return;
          return;
        }

        // Closing the workspace shell itself must also be transactional.  The
        // old ordering destroyed secondary tabs before a document could veto.
        if (!state->workspaceCancelTested) {
          queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                      QMessageBox::Cancel}});
          if (workspace->close()) {
            fail(QStringLiteral(
                "Workspace close ignored an unsaved-document Cancel"));
            return;
          }
          if (!state->first || !state->second || !state->view ||
              app.documentWindows().size() != 2 ||
              app.viewWindows().size() != 1 || app.tabCount() != 3 ||
              !workspace->isVisible() || !workspace->containsView(state->view)) {
            fail(QStringLiteral(
                "Cancelling workspace close destroyed a document or secondary view"));
            return;
          }
          state->workspaceCancelTested = true;
          schedule(0, 0, 40);
          return;
        }

        // Regression for the old File -> Exit ordering bug: cancelling the
        // first unsaved prompt must leave every presentation untouched.
        queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                    QMessageBox::Cancel}});
        app.closeAllDocumentWindows();
        if (!state->first || !state->second || !state->view ||
            app.documentWindows().size() != 2 || app.viewWindows().size() != 1 ||
            app.tabCount() != 3 || !workspace->containsView(state->view)) {
          fail(QStringLiteral(
              "Cancelling File -> Exit destroyed a document or secondary view"));
          return;
        }
        schedule(1, 0, 40);
        return;
      }

      case 1: {
        // Approve Discard for the first document, then cancel on the second.
        // No presentation may be destroyed, and the first document's one-shot
        // preflight approval must be rolled back.
        queueDialogResponses(
            app, {{QStringLiteral("Unsaved Changes"), QMessageBox::Discard},
                  {QStringLiteral("Unsaved Changes"), QMessageBox::Cancel}});
        app.closeAllDocumentWindows();
        if (!state->first || !state->second || !state->view ||
            app.documentWindows().size() != 2 || app.viewWindows().size() != 1 ||
            app.tabCount() != 3 ||
            !first->documentDisplayName().endsWith(QLatin1Char('*')) ||
            !second->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "A later Exit cancellation did not roll back the preflight cleanly"));
          return;
        }

        queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                    QMessageBox::Cancel}});
        if (first->close() || !state->first ||
            !first->documentDisplayName().endsWith(QLatin1Char('*'))) {
          fail(QStringLiteral(
              "Aborted Exit left a stale close approval on an earlier document"));
          return;
        }
        schedule(2, 0, 40);
        return;
      }

      case 2: {
        // A successful Save must synchronously update the current .cspar
        // archive, including the inner image area, and allow closing.
        queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                    QMessageBox::Save}});
        if (!first || !first->close()) {
          fail(QStringLiteral("Document lifecycle Save did not allow close"));
          return;
        }
        if (readFile(state->firstParameters) == state->firstBaselineParameters) {
          fail(QStringLiteral(
              "Document lifecycle Save did not update the parameter file"));
          return;
        }
        schedule(3, 0, 60);
        return;
      }

      case 3: {
        if (state->first || app.documentWindows().size() != 1 || !second ||
            !view || app.viewWindows().size() != 1 || app.tabCount() != 2) {
          if (retryOrFail(QStringLiteral(
                  "Saved document did not leave exactly its peer document/view")))
            return;
          return;
        }

        // Remove the peer view while the primary remains open; this must not
        // trigger the document save policy.
        if (!app.closeView(view)) {
          fail(QStringLiteral("Document lifecycle could not close peer view"));
          return;
        }
        schedule(4, 0, 60);
        return;
      }

      case 4: {
        if (state->view || app.viewWindows().size() != 0 || !second ||
            app.documentWindows().size() != 1 || app.tabCount() != 1) {
          if (retryOrFail(QStringLiteral(
                  "Peer view did not close while preserving its document")))
            return;
          return;
        }

        // Establish a current filename whose parent is then removed.  Save on
        // close must fail, show the error, and leave the dirty document alive.
        const QString invalidDirectory = state->temporaryDirectory->filePath(
            QStringLiteral("removed-save-directory"));
        if (!QDir().mkpath(invalidDirectory)) {
          fail(QStringLiteral(
              "Document lifecycle could not create failed-save fixture"));
          return;
        }
        const QString invalidParameters =
            QDir(invalidDirectory).filePath(QStringLiteral("missing.par"));
        if (!second->saveParametersToFile(invalidParameters)) {
          fail(QStringLiteral(
              "Document lifecycle could not establish failed-save current file"));
          return;
        }
        second->setDocumentMirror(
            !second->documentStateSnapshot().rparams.scan_mirror);
        if (!second->documentDisplayName().endsWith(QLatin1Char('*')) ||
            !QDir(invalidDirectory).removeRecursively()) {
          fail(QStringLiteral(
              "Document lifecycle could not prepare a dirty failed-save state"));
          return;
        }

        queueDialogResponses(
            app, {{QStringLiteral("Unsaved Changes"), QMessageBox::Save}});
        if (second->close() || !state->second ||
            !second->documentDisplayName().endsWith(QLatin1Char('*')) ||
            app.documentWindows().size() != 1 || app.tabCount() != 1) {
          fail(QStringLiteral(
              "Failed Save on close did not keep the dirty document open"));
          return;
        }

        QMessageBox *saveFailure = second->findChild<QMessageBox *>(
            QStringLiteral("ParameterSaveFailureDialog"));
        if (!saveFailure ||
            second->m_parameterSaveFailurePrompt != saveFailure ||
            !saveFailure->text().contains(
                QStringLiteral("previous file was left unchanged"))) {
          fail(QStringLiteral(
              "Failed Save on close did not publish its asynchronous warning"));
          return;
        }
        saveFailure->accept();
        schedule(5, 0, 40);
        return;
      }

      case 5: {
        // Start a real production final-result operation. Screen detection now
        // uses the shared TaskQueue/QThreadPool one-shot lifecycle rather than
        // owning a dedicated child QThread, so test the queue contract instead
        // of the removed implementation detail.
        if (!QMetaObject::invokeMethod(second, "onAutodetectScreen",
                                       Qt::DirectConnection)) {
          fail(QStringLiteral(
              "Document lifecycle smoke could not start screen detection"));
          return;
        }
        if (!second->m_oneShotOperations.hasActiveTasks()) {
          fail(QStringLiteral(
              "Document lifecycle smoke did not enqueue one-shot screen detection"));
          return;
        }

        // Explicit Discard is the final supported close outcome. Close while
        // production one-shot work is active: teardown must cancel publication
        // and detach it safely from document state. The top-level smoke cleanup
        // drains QThreadPool before process exit, allowing cooperative work that
        // owns only captured snapshots to wind down.
        queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                    QMessageBox::Discard}});
        if (!second->close()) {
          fail(QStringLiteral("Document lifecycle Discard did not allow close"));
          return;
        }
        schedule(6, 0, 80);
        return;
      }

      case 6: {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (state->second ||
            !app.documentWindows().isEmpty() || !app.viewWindows().isEmpty() ||
            app.tabCount() != 0 || (workspace && workspace->isVisible())) {
          if (retryOrFail(QStringLiteral(
                  "Document lifecycle close did not cancel/join background work")))
            return;
          return;
        }
        // Opening a moved/missing source must present a nonblocking file
        // chooser and must not decode JSON as if it contained image pixels.
        colorscreen::scr_to_img_parameters g;
        colorscreen::scr_detect_parameters d;
        colorscreen::render_parameters rp;
        colorscreen::solver_parameters solver;
        std::vector<colorscreen::point_t> spots;
        colorscreen::parameter_source_image missingSource;
        std::string problem;
        if (!colorscreen::read_parameter_json_v3_file(
                state->firstParameters.toUtf8().constData(),
                &g, &d, &rp, &solver, &spots, &problem, &missingSource) ||
            missingSource.path.empty()) {
          fail(QStringLiteral("Missing-source probe cannot read its saved scan"));
          return;
        }
        // Opening a new .cspar must revoke an earlier size-mismatch
        // confirmation even if neither request has decoded an image yet.
        // Otherwise a delayed click can replace an unrelated newer document.
        colorscreen::parameter_source_image mismatched = missingSource;
        const qint64 actualBytes =
            QFileInfo(state->firstImageFile).size();
        if (actualBytes <= 0) {
          fail(QStringLiteral("Linked-source smoke input has no file size"));
          return;
        }
        mismatched.size_bytes = static_cast<uint64_t>(actualBytes) + 1;
        const QString mismatchDocument =
            state->temporaryDirectory->filePath(
                QStringLiteral("stale-source-confirmation.cspar"));
        // This fixture moves a linked document to a new directory: its scan
        // reference must be rebased exactly as production Save As would do.
        // Otherwise the open correctly chooses Locate image instead of the
        // file-size confirmation that this test intends to exercise.
        if (!colorscreen::rebase_parameter_source_image(
                &mismatched,
                QFileInfo(state->firstParameters).absoluteFilePath()
                    .toUtf8().toStdString(),
                QFileInfo(mismatchDocument).absoluteFilePath()
                    .toUtf8().toStdString(), &problem)) {
          fail(QStringLiteral("Could not rebase size-mismatch test source: %1")
                   .arg(QString::fromStdString(problem)));
          return;
        }
        const QString recordedForMismatch =
            QString::fromStdString(mismatched.path);
        const QString resolvedForMismatch =
            QFileInfo(recordedForMismatch).isAbsolute()
                ? QFileInfo(recordedForMismatch).absoluteFilePath()
                : QDir(QFileInfo(mismatchDocument).absolutePath())
                      .absoluteFilePath(recordedForMismatch);
        if (QDir::cleanPath(resolvedForMismatch) !=
            QDir::cleanPath(QFileInfo(state->firstImageFile)
                                .absoluteFilePath())) {
          fail(QStringLiteral("Size-mismatch fixture lost its source image"));
          return;
        }
        if (!colorscreen::write_parameter_json_v3_file(
                mismatchDocument.toUtf8().constData(), g, d, rp, solver,
                spots, &problem, &mismatched)) {
          fail(QStringLiteral("Could not create linked-source mismatch fixture"));
          return;
        }

        missingSource.path = QStringLiteral("lost/missing-original-image.tif")
                                 .toStdString();
        const QString missingDocument =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-source.cspar"));
        if (!colorscreen::write_parameter_json_v3_file(
                missingDocument.toUtf8().constData(), g, d, rp, solver,
                spots, &problem, &missingSource)) {
          fail(QStringLiteral("Could not create missing-source .cspar fixture"));
          return;
        }
        app.openFiles({mismatchDocument});
        const auto incompleteDocs = app.documentWindows();
        if (incompleteDocs.size() != 1 || !incompleteDocs.constFirst()) {
          fail(QStringLiteral("Mismatch probe failed to open a blank document"));
          return;
        }
        MainWindow *pending = incompleteDocs.constFirst();
        QPointer<QMessageBox> obsoleteWarning =
            pending->m_sourceMismatchPrompt;
        if (!obsoleteWarning) {
          fail(QStringLiteral("Linked-source size mismatch did not warn"));
          return;
        }
        pending->openParameterDocument(missingDocument);
        if (pending->m_sourceMismatchPrompt ||
            (obsoleteWarning && !obsoleteWarning->isHidden()) ||
            !pending->m_sourceLocateDialog ||
            pending->m_imageLoad.pending) {
          fail(QStringLiteral(
              "A newer .cspar did not revoke the stale mismatch confirmation"));
          return;
        }
        schedule(7, 80, 100);
        return;
      }

      case 7: {
        const auto docs = app.documentWindows();
        if (docs.size() != 1 || !docs.constFirst()) {
          if (retryOrFail(QStringLiteral("Missing-source document did not open")))
            return;
          return;
        }
        MainWindow *missing = docs.constFirst();
        QFileDialog *chooser = missing->findChild<QFileDialog *>(
            QStringLiteral("LinkedSourceImageDialog"));
        if (!chooser || missing->m_imageLoad.pending ||
            missing->sharedImageData() ||
            !missing->m_parameterFile.path.isEmpty()) {
          if (retryOrFail(QStringLiteral(
                  "Missing-source .cspar did not offer clean Locate image")))
            return;
          return;
        }
        chooser->reject();
        app.closeAllDocumentWindows();
        schedule(8, 70, 100);
        return;
      }

      case 8:
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!app.documentWindows().isEmpty() || app.tabCount() != 0) {
          if (retryOrFail(QStringLiteral(
                  "Canceling Locate image left an incomplete document open")))
            return;
          return;
        }
        // The original .cspar saved through Qt must open as a complete
        // document from a desktop/CLI positional path, not as a JSON image.
        app.openFiles({state->firstParameters});
        schedule(9, 100, 150);
        return;

      case 9: {
        const auto docs = app.documentWindows();
        if (docs.size() != 1 || !docs.constFirst() ||
            docs.constFirst()->m_imageLoad.pending ||
            !docs.constFirst()->sharedImageData()) {
          if (retryOrFail(QStringLiteral(
                  "Direct .cspar open did not load its linked image")))
            return;
          return;
        }
        MainWindow *linked = docs.constFirst();
        colorscreen::scr_to_img_parameters g;
        colorscreen::scr_detect_parameters d;
        colorscreen::render_parameters r;
        colorscreen::solver_parameters sol;
        std::vector<colorscreen::point_t> spots;
        colorscreen::parameter_source_image original;
        std::string problem;
        if (!colorscreen::read_parameter_json_v3_file(
                state->firstParameters.toUtf8().constData(),
                &g, &d, &r, &sol, &spots, &problem, &original) ||
            !linked->m_scan ||
            linked->currentImageFile() != state->firstImageFile ||
            linked->m_parameterFile.path !=
                QFileInfo(state->firstParameters).absoluteFilePath() ||
            linked->m_parameterFile.suggested ||
            linked->m_parameterFile.format !=
                MainWindow::ParameterFileState::Format::JsonV3 ||
            linked->m_rparams.scan_mirror != r.scan_mirror ||
            linked->m_scrToImgParams.type != g.type ||
            linked->m_scan->width != original.width_px ||
            linked->m_scan->height != original.height_px) {
          fail(QStringLiteral(
              "Desktop .cspar open lost its image, source dimensions, "
              "or saved processing state"));
          return;
        }
        // The ordinary File -> Open chooser must also expose linked .cspar
        // documents. Without this, Explorer worked but Ctrl+O silently
        // filtered out the format unless the user selected "All Files".
        linked->onOpenImage();
        QFileDialog *openChooser = linked->findChild<QFileDialog *>(
            QStringLiteral("ImageOpenFileDialog"));
        if (!openChooser ||
            !openChooser->nameFilters().join(QStringLiteral(";;")).contains(
                QStringLiteral("*.cspar"))) {
          fail(QStringLiteral(
              "File Open chooser excluded source-linked .cspar documents"));
          return;
        }
        openChooser->reject();
        app.closeAllDocumentWindows();
        schedule(10, 80, 120);
        return;
      }

      case 10: {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!app.documentWindows().isEmpty() || app.tabCount() != 0) {
          if (retryOrFail(QStringLiteral(
                  "Linked document did not close without dirty-state prompt")))
            return;
          return;
        }

        // Relink a moved source via the user's Locate-image chooser, then
        // save the document and prove that the new relative reference points
        // at the chosen scan. This must not silently alter the on-disk .cspar
        // merely by opening it.
        colorscreen::scr_to_img_parameters g;
        colorscreen::scr_detect_parameters d;
        colorscreen::render_parameters r;
        colorscreen::solver_parameters solver;
        std::vector<colorscreen::point_t> spots;
        colorscreen::parameter_source_image moved;
        std::string problem;
        if (!colorscreen::read_parameter_json_v3_file(
                state->firstParameters.toUtf8().constData(),
                &g, &d, &r, &solver, &spots, &problem, &moved) ||
            moved.path.empty()) {
          fail(QStringLiteral("Cannot construct relocated scan document"));
          return;
        }
        moved.path = "old-location/relocated-only-archival-scan.tif";
        const QString relocatedDocument =
            state->temporaryDirectory->filePath(
                QStringLiteral("relocated-linked-source.cspar"));
        if (!colorscreen::write_parameter_json_v3_file(
                relocatedDocument.toUtf8().constData(),
                g, d, r, solver, spots, &problem, &moved)) {
          fail(QStringLiteral("Cannot save relocated scan test document"));
          return;
        }
        app.openFiles({relocatedDocument});
        schedule(11, 60, 100);
        return;
      }

      case 11: {
        const auto docs = app.documentWindows();
        if (docs.size() != 1 || !docs.constFirst()) {
          if (retryOrFail(QStringLiteral(
                  "Relink test did not create a document")))
            return;
          return;
        }
        MainWindow *relocated = docs.constFirst();
        QFileDialog *chooser = relocated->findChild<QFileDialog *>(
            QStringLiteral("LinkedSourceImageDialog"));
        if (!chooser || relocated->m_imageLoad.pending ||
            relocated->sharedImageData()) {
          if (retryOrFail(QStringLiteral(
                  "Relink test did not offer source-image selection")))
            return;
          return;
        }
        chooser->selectFile(QFileInfo(state->firstImageFile).absoluteFilePath());
        // QFileDialog::accept is protected; invoke the public virtual QDialog slot.
        static_cast<QDialog *>(chooser)->accept();
        schedule(12, 140, 150);
        return;
      }

      case 12: {
        const auto docs = app.documentWindows();
        if (docs.size() != 1 || !docs.constFirst()) {
          if (retryOrFail(QStringLiteral(
                  "Selected source image did not open")))
            return;
          return;
        }
        MainWindow *relocated = docs.constFirst();
        if (relocated->m_imageLoad.pending || !relocated->sharedImageData()) {
          if (retryOrFail(QStringLiteral(
                  "Relocated image decode has not completed")))
            return;
          return;
        }
        const QString relocatedDocument =
            state->temporaryDirectory->filePath(
                QStringLiteral("relocated-linked-source.cspar"));
        if (QFileInfo(relocated->currentImageFile()).absoluteFilePath() !=
                QFileInfo(state->firstImageFile).absoluteFilePath() ||
            relocated->m_parameterFile.path !=
                QFileInfo(relocatedDocument).absoluteFilePath() ||
            relocated->m_parameterFile.suggested ||
            !relocated->isDocumentModified()) {
          fail(QStringLiteral(
              "Relinked source was not opened as a dirty linked document"));
          return;
        }
        if (!relocated->saveParametersToFile(
                relocatedDocument, MainWindow::ParameterSaveFormat::JsonV3) ||
            relocated->isDocumentModified()) {
          fail(QStringLiteral(
              "Saving relocated source failed to adopt its new path"));
          return;
        }
        colorscreen::scr_to_img_parameters g;
        colorscreen::scr_detect_parameters d;
        colorscreen::render_parameters r;
        colorscreen::solver_parameters solver;
        std::vector<colorscreen::point_t> spots;
        colorscreen::parameter_source_image saved;
        std::string problem;
        if (!colorscreen::read_parameter_json_v3_file(
                relocatedDocument.toUtf8().constData(),
                &g, &d, &r, &solver, &spots, &problem, &saved) ||
            saved.path.empty()) {
          fail(QStringLiteral(
              "Saved relocated document lost source image metadata"));
          return;
        }
        const QString spelling = QString::fromStdString(saved.path);
        const QString absoluteSource = QFileInfo(spelling).isAbsolute()
            ? QFileInfo(spelling).absoluteFilePath()
            : QDir(QFileInfo(relocatedDocument).absolutePath())
                  .absoluteFilePath(spelling);
        if (QDir::cleanPath(absoluteSource) !=
                QDir::cleanPath(QFileInfo(state->firstImageFile)
                                    .absoluteFilePath()) ||
            saved.width_px != relocated->m_scan->width ||
            saved.height_px != relocated->m_scan->height) {
          fail(QStringLiteral(
              "Saved relocated source no longer identifies the chosen scan"));
          return;
        }
        app.closeAllDocumentWindows();
        schedule(13, 80, 120);
        return;
      }

      case 13:
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!app.documentWindows().isEmpty() || app.tabCount() != 0) {
          if (retryOrFail(QStringLiteral(
                  "Relocated document did not close cleanly after Save")))
            return;
          return;
        }
        state->completed();
        return;

      default:
        fail(QStringLiteral("Document lifecycle smoke reached invalid phase"));
        return;
      }
    };

    QTimer::singleShot(0, &app, [runPhase]() { (*runPhase)(-1, 120); });
  };

  startTaskQueueAsyncPublicationSmoke(
      app, [&app, start]() {
        QTimer::singleShot(100, &app, [start]() { (*start)(100); });
      });
}
