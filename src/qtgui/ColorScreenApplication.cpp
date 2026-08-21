#include "ColorScreenApplication.h"

#include "MainWindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

/** Return the cache directory historically used by the Qt GUI. */
QString applicationCacheDirectory() {
  QString path = QDir::fromNativeSeparators(
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

  // Preserve the pre-existing shared "colorscreen" cache location.  On some
  // platforms QStandardPaths appends an extra "cache" component, so replace
  // the application-name path component rather than assuming it is last.
  const QString oldComponent = QStringLiteral("/colorscreen-qt");
  const int componentStart = path.lastIndexOf(oldComponent);
  if (componentStart >= 0) {
    const int componentEnd = componentStart + oldComponent.size();
    if (componentEnd == path.size() || path.at(componentEnd) == QLatin1Char('/'))
      path.replace(componentStart + 1, oldComponent.size() - 1,
                   QStringLiteral("colorscreen"));
  }
  return QDir::cleanPath(path);
}

/** Return the root that contains one crash-recovery directory per document. */
QString recoveryRootDirectory() {
  return QDir(applicationCacheDirectory())
      .filePath(QStringLiteral("recovery-sessions"));
}

/** Return a fresh path for one document's crash-recovery payload. */
QString newRecoveryDirectory() {
  return QDir(recoveryRootDirectory())
      .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

/** Return the legacy pre-multi-document recovery file path NAME. */
QString legacyRecoveryFile(const QString &name) {
  return QDir(applicationCacheDirectory()).filePath(name);
}

/** Return the path of recovery payload NAME inside DIRECTORY. */
QString recoveryFile(const QString &directory, const QString &name) {
  return QDir(directory).filePath(name);
}

/** Copy SOURCE to DESTINATION without removing the legacy payload. */
bool copyRecoveryFile(const QString &source, const QString &destination) {
  if (!QFile::exists(source))
    return true;
  return QFile::copy(source, destination);
}

} // namespace

/** Construct the application-level document manager. */
ColorScreenApplication::ColorScreenApplication(int &argc, char **argv)
    : QApplication(argc, argv) {
  setQuitOnLastWindowClosed(true);
}

/** Create and show one independently owned document window. */
MainWindow *ColorScreenApplication::createDocumentWindow(
    const QString &recoveryDirectory) {
  pruneDocumentWindows();

  MainWindow *referenceWindow = nullptr;
  if (QWidget *active = activeWindow())
    referenceWindow = dynamic_cast<MainWindow *>(active);
  if (!referenceWindow && !m_documentWindows.isEmpty())
    referenceWindow = m_documentWindows.constLast();

  const QString assignedRecoveryDirectory =
      recoveryDirectory.isEmpty() ? newRecoveryDirectory()
                                  : recoveryDirectory;
  auto *window = new MainWindow(assignedRecoveryDirectory);
  window->setAttribute(Qt::WA_DeleteOnClose);
  m_documentWindows.append(window);

  connect(window, &QObject::destroyed, this, [this]() {
    // Defer pruning until QObject destruction has completed.  Calling methods
    // through a QPointer from inside destroyed() could otherwise reach a
    // partially destructed MainWindow.
    QTimer::singleShot(0, this, [this]() {
      pruneDocumentWindows();
      refreshWindowMenus();
    });
  });

  window->show();

  // Every MainWindow restores the preferred layout.  Offset additional
  // documents so that opening several files does not leave them perfectly
  // superimposed while still preserving the user's saved size and dock state.
  if (referenceWindow && referenceWindow != window) {
    constexpr int cascadeStep = 28;
    const QPoint offset(cascadeStep, cascadeStep);
    window->move(referenceWindow->pos() + offset);
  }

  refreshWindowMenus();
  return window;
}

/** Open a list of images, assigning one complete MainWindow to each image. */
void ColorScreenApplication::openFiles(const QStringList &fileNames,
                                       MainWindow *preferredWindow,
                                       bool suppressParamPrompt) {
  MainWindow *target = reusableWindow(preferredWindow);
  bool openedAny = false;

  for (const QString &fileName : fileNames) {
    if (fileName.trimmed().isEmpty())
      continue;

    if (!target)
      target = createDocumentWindow();
    else {
      target->show();
      target->raise();
      target->activateWindow();
    }

    target->loadFile(QFileInfo(fileName).absoluteFilePath(),
                     suppressParamPrompt);
    openedAny = true;
    target = nullptr; // Only the first image may reuse an existing blank window.
  }

  if (!openedAny && documentWindows().isEmpty())
    createDocumentWindow();
}

/** Offer to restore every recoverable document from the previous session. */
bool ColorScreenApplication::restoreRecoverySession() {
  QStringList directories = recoveryDirectories();
  const bool hasLegacyRecovery =
      QFile::exists(legacyRecoveryFile(QStringLiteral("recovery_image.txt"))) ||
      QFile::exists(legacyRecoveryFile(QStringLiteral("recovery_params.par")));
  const int recoveryCount = directories.size() + (hasLegacyRecovery ? 1 : 0);

  if (recoveryCount == 0)
    return false;

  const QString message =
      recoveryCount == 1
          ? tr("Color-Screen found an image document left by an unclean "
               "shutdown.\nWould you like to restore it?")
          : tr("Color-Screen found %1 image documents left by an unclean "
               "shutdown.\nWould you like to restore them?")
                .arg(recoveryCount);
  const QMessageBox::StandardButton reply = QMessageBox::question(
      nullptr, tr("Crash Recovery"), message,
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

  if (reply != QMessageBox::Yes) {
    discardRecoveryData(directories);
    return false;
  }

  if (hasLegacyRecovery) {
    const QString migrated = migrateLegacyRecovery();
    if (!migrated.isEmpty()) {
      directories.prepend(migrated);
    } else {
      QMessageBox::warning(
          nullptr, tr("Crash Recovery"),
          tr("The legacy recovery document could not be migrated. Its "
             "original recovery files were left untouched."));
    }
  }

  bool restoredAny = false;
  for (const QString &directory : directories) {
    MainWindow *window = createDocumentWindow(directory);
    if (window->restoreRecoveryState()) {
      restoredAny = true;
    } else {
      // Invalid or incomplete recovery data should not leave an extra blank
      // document.  closeEvent removes that window's recovery directory.  Drop
      // it from the manager immediately because WA_DeleteOnClose destruction is
      // deferred until the event loop runs.
      if (window->close())
        m_documentWindows.removeAll(QPointer<MainWindow>(window));
    }
  }
  return restoredAny;
}

/** Return all live document windows and discard stale guarded pointers. */
QList<MainWindow *> ColorScreenApplication::documentWindows() {
  pruneDocumentWindows();
  QList<MainWindow *> windows;
  windows.reserve(m_documentWindows.size());
  for (const QPointer<MainWindow> &window : m_documentWindows) {
    if (window)
      windows.append(window.data());
  }
  return windows;
}

/** Populate the Window menu from the current live document set. */
void ColorScreenApplication::populateWindowMenu(QMenu *menu,
                                                 MainWindow *currentWindow) {
  if (!menu)
    return;

  menu->clear();

  QAction *newWindowAction = menu->addAction(tr("&New Window"));
  newWindowAction->setShortcut(QKeySequence::New);
  newWindowAction->setShortcutContext(Qt::WindowShortcut);
  newWindowAction->setToolTip(
      tr("Create an empty window for another image document."));
  connect(newWindowAction, &QAction::triggered, menu, [this]() {
    // Creating a window rebuilds every Window menu.  Defer the operation so
    // the QAction that emitted triggered() is not deleted mid-emission.
    QTimer::singleShot(0, this, [this]() { createDocumentWindow(); });
  });

  QAction *nextWindowAction = menu->addAction(tr("Next Image"));
  nextWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
  nextWindowAction->setShortcutContext(Qt::WindowShortcut);
  connect(nextWindowAction, &QAction::triggered, menu,
          [this, currentWindow]() {
            activateRelativeWindow(currentWindow, 1);
          });

  QAction *previousWindowAction = menu->addAction(tr("Previous Image"));
  previousWindowAction->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
  previousWindowAction->setShortcutContext(Qt::WindowShortcut);
  connect(previousWindowAction, &QAction::triggered, menu,
          [this, currentWindow]() {
            activateRelativeWindow(currentWindow, -1);
          });

  const QList<MainWindow *> windows = documentWindows();
  const bool multipleWindows = windows.size() > 1;
  nextWindowAction->setEnabled(multipleWindows);
  previousWindowAction->setEnabled(multipleWindows);

  menu->addSeparator();
  for (int i = 0; i < windows.size(); ++i) {
    QPointer<MainWindow> guardedWindow(windows[i]);
    QString documentName = windows[i]->documentDisplayName();
    documentName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QAction *action =
        menu->addAction(tr("&%1 %2").arg(i + 1).arg(documentName));
    action->setCheckable(true);
    action->setChecked(windows[i] == currentWindow);
    action->setToolTip(windows[i]->currentImageFile().isEmpty()
                           ? tr("Empty image document")
                           : windows[i]->currentImageFile());
    connect(action, &QAction::triggered, menu, [guardedWindow]() {
      if (!guardedWindow)
        return;
      if (guardedWindow->isMinimized())
        guardedWindow->showNormal();
      guardedWindow->raise();
      guardedWindow->activateWindow();
    });
  }
}

/** Activate the next or previous document window. */
void ColorScreenApplication::activateRelativeWindow(MainWindow *currentWindow,
                                                     int offset) {
  const QList<MainWindow *> windows = documentWindows();
  if (windows.isEmpty())
    return;

  int currentIndex = windows.indexOf(currentWindow);
  if (currentIndex < 0)
    currentIndex = 0;
  const int count = windows.size();
  const int targetIndex = ((currentIndex + offset) % count + count) % count;
  MainWindow *target = windows[targetIndex];
  if (target->isMinimized())
    target->showNormal();
  target->raise();
  target->activateWindow();
}

/** Close every document window using each window's normal close policy. */
void ColorScreenApplication::closeAllDocumentWindows() {
  closeAllWindows();
}

/** Open files delivered by Finder, Explorer, or the desktop shell. */
bool ColorScreenApplication::event(QEvent *event) {
  if (event && event->type() == QEvent::FileOpen) {
    auto *openEvent = static_cast<QFileOpenEvent *>(event);
    QString fileName = openEvent->file();
    if (fileName.isEmpty() && openEvent->url().isLocalFile())
      fileName = openEvent->url().toLocalFile();
    if (!fileName.isEmpty()) {
      openFiles({fileName});
      return true;
    }
  }
  return QApplication::event(event);
}

/** Remove null guarded pointers after document windows are destroyed. */
void ColorScreenApplication::pruneDocumentWindows() {
  for (auto it = m_documentWindows.begin(); it != m_documentWindows.end();) {
    if (it->isNull())
      it = m_documentWindows.erase(it);
    else
      ++it;
  }
}

/** Find all per-document recovery directories that contain payload data. */
QStringList ColorScreenApplication::recoveryDirectories() const {
  QDir root(recoveryRootDirectory());
  if (!root.exists())
    return {};

  QStringList result;
  const QFileInfoList entries = root.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
  for (const QFileInfo &entry : entries) {
    if (recoveryDirectoryHasData(entry.absoluteFilePath()))
      result.append(entry.absoluteFilePath());
    else
      QDir(entry.absoluteFilePath()).removeRecursively();
  }
  return result;
}

/** Test whether a directory contains image or parameter recovery data. */
bool ColorScreenApplication::recoveryDirectoryHasData(
    const QString &directory) const {
  return QFile::exists(
             recoveryFile(directory, QStringLiteral("recovery_image.txt"))) ||
         QFile::exists(
             recoveryFile(directory, QStringLiteral("recovery_params.par")));
}

/** Migrate the old single recovery slot to a unique document directory. */
QString ColorScreenApplication::migrateLegacyRecovery() const {
  const QString targetDirectory = newRecoveryDirectory();
  if (!QDir().mkpath(targetDirectory))
    return {};

  const QStringList names = {QStringLiteral("recovery_image.txt"),
                             QStringLiteral("recovery_params.par"),
                             QStringLiteral("recovery_params_meta.txt")};
  for (const QString &name : names) {
    if (!copyRecoveryFile(legacyRecoveryFile(name),
                          recoveryFile(targetDirectory, name))) {
      QDir(targetDirectory).removeRecursively();
      return {};
    }
  }

  // Remove the old single-document slot only after every existing payload
  // file has been copied successfully, so a partial migration cannot lose the
  // user's recovery data.
  for (const QString &name : names)
    QFile::remove(legacyRecoveryFile(name));
  return targetDirectory;
}

/** Delete recovery data after the user declines session restoration. */
void ColorScreenApplication::discardRecoveryData(
    const QStringList &directories) const {
  for (const QString &directory : directories)
    QDir(directory).removeRecursively();

  const QStringList legacyNames = {
      QStringLiteral("recovery_image.txt"),
      QStringLiteral("recovery_params.par"),
      QStringLiteral("recovery_params_meta.txt")};
  for (const QString &name : legacyNames)
    QFile::remove(legacyRecoveryFile(name));
}

/** Rebuild Window menus after a document is added or removed. */
void ColorScreenApplication::refreshWindowMenus() {
  const QList<MainWindow *> windows = documentWindows();
  for (MainWindow *window : windows)
    window->refreshWindowMenu();
}

/** Find an untouched empty window that can host the first requested image. */
MainWindow *ColorScreenApplication::reusableWindow(
    MainWindow *preferredWindow) {
  if (preferredWindow && preferredWindow->canReuseForOpen())
    return preferredWindow;

  if (QWidget *active = activeWindow()) {
    if (auto *window = dynamic_cast<MainWindow *>(active)) {
      if (window->canReuseForOpen())
        return window;
    }
  }

  for (MainWindow *window : documentWindows()) {
    if (window->canReuseForOpen())
      return window;
  }
  return nullptr;
}
