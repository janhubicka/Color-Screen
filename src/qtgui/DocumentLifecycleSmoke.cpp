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
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <atomic>
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
  bool failedOpenParameterArchive = false;
  bool failedOpenRecoveryDirty = false;
  std::unique_ptr<QTemporaryDir> temporaryDirectory;
  QString recoveryProbeDirectory;
  QString recoveryExpectedImage;
  QString recoveryExpectedParameterPath;
  ParameterState recoveryExpectedState;
  bool recoveryStarted = false;
  QString firstParameters;
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
    real current .par file first, so choosing Save exercises the ordinary
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
    state->firstParameters = state->temporaryDirectory->filePath(
        QStringLiteral("first.par"));
    state->secondParameters = state->temporaryDirectory->filePath(
        QStringLiteral("second.par"));

    MainWindow *first = state->first.data();
    MainWindow *second = state->second.data();
    state->firstInitialMirror =
        first->documentStateSnapshot().rparams.scan_mirror;
    state->secondInitialMirror =
        second->documentStateSnapshot().rparams.scan_mirror;

    if (!first->saveParametersToFile(state->firstParameters) ||
        !second->saveParametersToFile(state->secondParameters)) {
      qCritical() << "Document lifecycle smoke could not establish clean parameter files";
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
          fixture.rparams.ignore_infrared = true;
          fixture.rparams.demosaiced_scaling =
              colorscreen::render_parameters::lanczos3_scaling;
          fixture.rparams.observer_whitepoint = colorscreen::xy_t(0.34, 0.35);
          fixture.rparams.output_profile =
              colorscreen::render_parameters::output_profile_xyz;
          fixture.rparams.output_gamma = 1.85;
          fixture.rparams.gamut_warning = true;
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
            recovered.rparams.gamma != 2.0 ||
            recovered.rparams.ignore_infrared !=
                expected.rparams.ignore_infrared ||
            recovered.rparams.demosaiced_scaling !=
                expected.rparams.demosaiced_scaling ||
            recovered.rparams.observer_whitepoint !=
                expected.rparams.observer_whitepoint ||
            recovered.rparams.output_profile !=
                expected.rparams.output_profile ||
            recovered.rparams.output_gamma !=
                expected.rparams.output_gamma ||
            recovered.rparams.gamut_warning !=
                expected.rparams.gamut_warning ||
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

        // Extract the archive's exact legacy mirror for a backward-compatible
        // old-session recovery test. A legacy-only directory must remain usable
        // after this migration.
        const QString validParams =
            QDir(state->recoveryProbeDirectory)
                .filePath(QStringLiteral("recovery_params.cspar"));
        std::string legacyPayload;
        std::string archiveError;
        if (!colorscreen::read_parameter_archive(
                validParams.toUtf8().constData(), &legacyPayload, nullptr,
                &archiveError)) {
          fail(QStringLiteral("Recovery smoke could not extract legacy mirror: %1")
                   .arg(QString::fromStdString(archiveError)));
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
            QByteArray("legacy-target.cspar\n0\n0\narchive\n");
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
                MainWindow::ParameterFileState::Format::Archive ||
            !legacyProbe->isDocumentModified()) {
          delete legacyProbe;
          fail(QStringLiteral(
              "Recovery migration lost compatibility with legacy-only snapshots"));
          return;
        }
        delete legacyProbe;

        // Truncate the new archive, while leaving a valid but stale legacy
        // snapshot alongside it. The corrupt archive must never cause an
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
          fail(QStringLiteral("Recovery archive fixture is unexpectedly small"));
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
              "Recovery corruption smoke could not write truncated archive"));
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
              "Corrupt archive partially mutated state, adopted stale legacy "
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
        const QString missingArchiveSidecarPath =
            state->temporaryDirectory->filePath(
                QStringLiteral("missing-image-load.cspar"));
        QFile::remove(missingLegacySidecarPath);
        QFile::remove(missingArchiveSidecarPath);
        if (!QFile::copy(state->firstParameters, missingLegacySidecarPath)) {
          delete loadProbe;
          state->recoveryProbe = nullptr;
          fail(QStringLiteral(
              "Image-load failure smoke could not create a valid legacy sidecar"));
          return;
        }

        QFile archivePayloadFile(state->firstParameters);
        if (!archivePayloadFile.open(QIODevice::ReadOnly)) {
          delete loadProbe;
          state->recoveryProbe = nullptr;
          fail(QStringLiteral(
              "Image-load failure smoke could not read its sidecar payload"));
          return;
        }
        const QByteArray archivePayload = archivePayloadFile.readAll();
        archivePayloadFile.close();
        std::string archiveError;
        if (!colorscreen::write_parameter_archive(
                missingArchiveSidecarPath.toUtf8().constData(),
                std::string(archivePayload.constData(),
                            static_cast<size_t>(archivePayload.size())),
                "document-lifecycle-smoke", &archiveError)) {
          delete loadProbe;
          state->recoveryProbe = nullptr;
          fail(QStringLiteral(
                   "Image-load failure smoke could not create archive sidecar: %1")
                   .arg(QString::fromUtf8(archiveError)));
          return;
        }

        state->failedOpenBaseline = loadProbe->getCurrentState();
        state->failedOpenParameterPath = loadProbe->m_parameterFile.path;
        state->failedOpenParameterSuggested =
            loadProbe->m_parameterFile.suggested;
        state->failedOpenParameterArchive =
            loadProbe->m_parameterFile.format ==
            MainWindow::ParameterFileState::Format::Archive;
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
             MainWindow::ParameterFileState::Format::Archive) !=
                state->failedOpenParameterArchive ||
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
        // A successful Save must synchronously update the current .par file and
        // allow the document to close.
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

      case 6:
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (state->second ||
            !app.documentWindows().isEmpty() || !app.viewWindows().isEmpty() ||
            app.tabCount() != 0 || (workspace && workspace->isVisible())) {
          if (retryOrFail(QStringLiteral(
                  "Document lifecycle close did not cancel/join background work")))
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
