#include "DocumentLifecycleSmoke.h"

#include "ColorScreenApplication.h"
#include "ImageViewWindow.h"
#include "MainWindow.h"
#include "WorkspaceWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <memory>
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
  std::unique_ptr<QTemporaryDir> temporaryDirectory;
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
    dialogs on every Qt platform plugin (notably macOS).  The QMessageBox
    object itself still exists, so fall back to the application's widget list. */
QMessageBox *findMessageBox(const QString &title, QMessageBox *previousBox) {
  auto matches = [&title, previousBox](QMessageBox *box) {
    return box && box != previousBox && box->windowTitle() == title;
  };

  if (auto *active =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
    if (matches(active))
      return active;
  }

  QMessageBox *hiddenFallback = nullptr;
  for (QWidget *widget : QApplication::allWidgets()) {
    auto *candidate = qobject_cast<QMessageBox *>(widget);
    if (!matches(candidate))
      continue;
    if (candidate->isVisible())
      return candidate;
    hiddenFallback = candidate;
  }
  return hiddenFallback;
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

} // namespace

/** Exercise transactional application exit and individual close decisions. */
void startDocumentLifecycleSmoke(ColorScreenApplication &app,
                                 std::function<void()> completed) {
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
            app, {{QStringLiteral("Unsaved Changes"), QMessageBox::Save},
                  {QStringLiteral("Error"), QMessageBox::Ok}});
        if (second->close() || !state->second ||
            !second->documentDisplayName().endsWith(QLatin1Char('*')) ||
            app.documentWindows().size() != 1 || app.tabCount() != 1) {
          fail(QStringLiteral(
              "Failed Save on close did not keep the dirty document open"));
          return;
        }
        schedule(5, 0, 40);
        return;
      }

      case 5: {
        // Explicit Discard is the final supported close outcome.
        queueDialogResponses(app, {{QStringLiteral("Unsaved Changes"),
                                    QMessageBox::Discard}});
        if (!second || !second->close()) {
          fail(QStringLiteral("Document lifecycle Discard did not allow close"));
          return;
        }
        schedule(6, 0, 80);
        return;
      }

      case 6:
        if (state->second || !app.documentWindows().isEmpty() ||
            !app.viewWindows().isEmpty() || app.tabCount() != 0 ||
            (workspace && workspace->isVisible())) {
          if (retryOrFail(QStringLiteral(
                  "Document lifecycle close did not drain the application")))
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

    QTimer::singleShot(0, &app, [runPhase]() { (*runPhase)(0, 40); });
  };

  QTimer::singleShot(100, &app, [start]() { (*start)(100); });
}
