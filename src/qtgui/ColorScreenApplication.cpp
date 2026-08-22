#include "ColorScreenApplication.h"

#include "MainWindow.h"
#include "ImageViewWindow.h"
#include "WorkspaceWindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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

/** Create one independently owned document and present it as a tab by default. */
MainWindow *ColorScreenApplication::createDocumentWindow(
    const QString &recoveryDirectory, bool detached) {
  pruneDocumentWindows();

  const QString assignedRecoveryDirectory =
      recoveryDirectory.isEmpty() ? newRecoveryDirectory()
                                  : recoveryDirectory;
  auto *document = new MainWindow(assignedRecoveryDirectory);
  document->setAttribute(Qt::WA_DeleteOnClose);
  m_documentWindows.append(document);

  connect(document, &QObject::destroyed, this, [this]() {
    QTimer::singleShot(0, this, [this]() {
      pruneDocumentWindows();
      refreshWindowMenus();
      if (!m_workspaceShutdown && m_documentWindows.isEmpty())
        createDocumentWindow();
    });
  });

  if (detached) {
    document->setWindowFlags(Qt::Window);
    document->show();
    document->raise();
    document->activateWindow();
  } else {
    workspaceWindow()->addDocument(document);
  }

  refreshWindowMenus();
  return document;
}

/** Create an additional display-only view of SOURCE. */
ImageViewWindow *ColorScreenApplication::createViewWindow(MainWindow *source,
                                                           bool detached) {
  if (!source || !source->sharedImageData())
    return nullptr;

  pruneViewWindows();
  int viewNumber = 2;
  for (ImageViewWindow *view : viewWindows()) {
    if (view->sourceDocument() == source)
      viewNumber = qMax(viewNumber, view->viewNumber() + 1);
  }

  auto *view = new ImageViewWindow(source, viewNumber);
  view->setAttribute(Qt::WA_DeleteOnClose);
  m_viewWindows.append(view);
  connect(view, &QObject::destroyed, this, [this]() {
    QTimer::singleShot(0, this, [this]() {
      pruneViewWindows();
      refreshWindowMenus();
    });
  });

  if (detached) {
    view->setWindowFlags(Qt::Window);
    view->show();
    view->raise();
    view->activateWindow();
  } else {
    workspaceWindow()->addView(view);
  }

  refreshWindowMenus();
  return view;
}

/** Create a slanted-edge reference view whose parameters belong to SOURCE. */
ImageViewWindow *ColorScreenApplication::createSlantedEdgeReference(
    MainWindow *source, const QString &referenceFile, bool detached) {
  if (!source || referenceFile.trimmed().isEmpty())
    return nullptr;

  pruneViewWindows();
  int viewNumber = 2;
  for (ImageViewWindow *view : viewWindows()) {
    if (view->sourceDocument() == source)
      viewNumber = qMax(viewNumber, view->viewNumber() + 1);
  }

  auto *view = new ImageViewWindow(
      source, viewNumber, QFileInfo(referenceFile).absoluteFilePath());
  view->setAttribute(Qt::WA_DeleteOnClose);
  m_viewWindows.append(view);
  connect(view, &QObject::destroyed, this, [this]() {
    QTimer::singleShot(0, this, [this]() {
      pruneViewWindows();
      refreshWindowMenus();
    });
  });

  if (detached) {
    view->setWindowFlags(Qt::Window);
    view->show();
    view->raise();
    view->activateWindow();
  } else {
    workspaceWindow()->addView(view);
  }

  refreshWindowMenus();
  return view;
}

/** Ask the user for another scan to use as a slanted-edge MTF reference. */
ImageViewWindow *ColorScreenApplication::openSlantedEdgeReference(
    MainWindow *source, QWidget *dialogParent) {
  if (!source)
    return nullptr;

  const QString fileName = QFileDialog::getOpenFileName(
      dialogParent ? dialogParent : source,
      tr("Open slanted edge reference"), QString(),
      tr("Images (*.tif *.tiff *.jpg *.jpeg *.jp2 *.j2k *.jpc *.jpf *.jpx "
         "*.png *.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq);;"
         "All Files (*)"));
  if (fileName.isEmpty())
    return nullptr;
  return createSlantedEdgeReference(source, fileName);
}

/** Detach VIEW from the primary workspace without recreating it. */
void ColorScreenApplication::detachView(ImageViewWindow *view) {
  if (!view || !m_workspaceWindow || !m_workspaceWindow->containsView(view))
    return;
  m_workspaceWindow->removeView(view);
  refreshWindowMenus();
}

/** Attach VIEW to the primary workspace without changing view-local state. */
void ColorScreenApplication::attachView(ImageViewWindow *view) {
  if (!view)
    return;
  if (m_workspaceWindow && m_workspaceWindow->containsView(view)) {
    m_workspaceWindow->activateView(view);
    return;
  }
  workspaceWindow()->addView(view);
  refreshWindowMenus();
}

/** Consolidate every detached secondary view into the workspace. */
void ColorScreenApplication::attachAllViews() {
  const QList<ImageViewWindow *> views = viewWindows();
  for (ImageViewWindow *view : views)
    attachView(view);
}

/** Return all live secondary views. */
QList<ImageViewWindow *> ColorScreenApplication::viewWindows() {
  pruneViewWindows();
  QList<ImageViewWindow *> views;
  views.reserve(m_viewWindows.size());
  for (const QPointer<ImageViewWindow> &view : m_viewWindows) {
    if (view)
      views.append(view.data());
  }
  return views;
}

/** Reload every slanted-edge reference associated with SOURCE. */
void ColorScreenApplication::reloadSlantedEdgeReferences(MainWindow *source) {
  if (!source)
    return;
  for (ImageViewWindow *view : viewWindows()) {
    if (view && view->sourceDocument() == source &&
        view->isSlantedEdgeReference())
      view->reloadReferenceImage();
  }
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

/** Return the primary tabbed workspace, creating it on first use. */
WorkspaceWindow *ColorScreenApplication::workspaceWindow() {
  if (!m_workspaceWindow) {
    m_workspaceWindow = new WorkspaceWindow();
    m_workspaceWindow->setAttribute(Qt::WA_DeleteOnClose, false);
  }
  return m_workspaceWindow;
}

/** Detach DOCUMENT into a top-level window without recreating it. */
void ColorScreenApplication::detachDocument(MainWindow *document) {
  if (!document || !m_workspaceWindow ||
      !m_workspaceWindow->containsDocument(document))
    return;

  m_workspaceWindow->removeDocument(document);
  document->show();
  document->raise();
  document->activateWindow();
  refreshWindowMenus();
}

/** Attach DOCUMENT to the primary workspace without changing its state. */
void ColorScreenApplication::attachDocument(MainWindow *document) {
  if (!document)
    return;
  if (m_workspaceWindow && m_workspaceWindow->containsDocument(document)) {
    m_workspaceWindow->activateDocument(document);
    return;
  }
  workspaceWindow()->addDocument(document);
  refreshWindowMenus();
}

/** Attach every detached document to the primary workspace. */
void ColorScreenApplication::attachAllDocuments() {
  const QList<MainWindow *> documents = documentWindows();
  for (MainWindow *document : documents)
    attachDocument(document);
}

/** Refresh a document's tab or detached-window presentation. */
void ColorScreenApplication::refreshDocumentPresentation(MainWindow *document) {
  if (!document)
    return;
  if (m_workspaceWindow)
    m_workspaceWindow->refreshDocument(document);
  refreshWindowMenus();
}

/** Return workspace-owned presentation widgets before DOCUMENT closes. */
void ColorScreenApplication::prepareDocumentForClose(MainWindow *document) {
  if (!document || !m_workspaceWindow ||
      !m_workspaceWindow->containsDocument(document))
    return;
  m_workspaceWindow->prepareDocumentForClose(document);
}

/** Return shared workspace chrome before an embedded secondary view closes. */
void ColorScreenApplication::prepareViewForClose(ImageViewWindow *view) {
  if (!view || !m_workspaceWindow || !m_workspaceWindow->containsView(view))
    return;
  m_workspaceWindow->prepareViewForClose(view);
}

/** Return the number of attached document tabs. */
int ColorScreenApplication::tabCount() const {
  return m_workspaceWindow ? m_workspaceWindow->tabCount() : 0;
}

/** Close all documents while the workspace itself is shutting down. */
bool ColorScreenApplication::closeAllDocumentsForWorkspace() {
  m_workspaceShutdown = true;
  const QList<MainWindow *> documents = documentWindows();
  for (MainWindow *document : documents) {
    if (document && !document->close()) {
      m_workspaceShutdown = false;
      return false;
    }
  }
  return true;
}

/** Populate the Window menu with tab and detached-window controls. */
void ColorScreenApplication::populateWindowMenu(QMenu *menu,
                                                 MainWindow *currentWindow,
                                                 ImageViewWindow *currentView) {
  if (!menu)
    return;
  menu->clear();

  QAction *newTabAction = menu->addAction(tr("&New Image Tab"));
  newTabAction->setShortcut(QKeySequence::New);
  newTabAction->setShortcutContext(Qt::WindowShortcut);
  connect(newTabAction, &QAction::triggered, menu, [this]() {
    QTimer::singleShot(0, this, [this]() { createDocumentWindow(); });
  });

  QAction *newWindowAction = menu->addAction(tr("New &Detached Window"));
  newWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  newWindowAction->setShortcutContext(Qt::WindowShortcut);
  connect(newWindowAction, &QAction::triggered, menu, [this]() {
    QTimer::singleShot(0, this, [this]() { createDocumentWindow(QString(), true); });
  });

  QAction *newViewAction = menu->addAction(tr("New &View"));
  newViewAction->setToolTip(
      tr("Open another view of this image with independent render mode and zoom."));
  newViewAction->setEnabled(currentWindow && currentWindow->sharedImageData());
  connect(newViewAction, &QAction::triggered, menu, [this, currentWindow]() {
    if (currentWindow)
      createViewWindow(currentWindow);
  });

  QAction *nextAction = menu->addAction(tr("Next Image"));
  nextAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
  nextAction->setShortcutContext(Qt::WindowShortcut);
  connect(nextAction, &QAction::triggered, menu, [this, currentWindow]() {
    activateRelativeWindow(currentWindow, 1);
  });

  QAction *previousAction = menu->addAction(tr("Previous Image"));
  previousAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
  previousAction->setShortcutContext(Qt::WindowShortcut);
  connect(previousAction, &QAction::triggered, menu, [this, currentWindow]() {
    activateRelativeWindow(currentWindow, -1);
  });

  menu->addSeparator();
  QAction *detachAction = menu->addAction(
      currentView ? tr("Detach Current View") : tr("Detach Current Image"));
  detachAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  detachAction->setShortcutContext(Qt::WindowShortcut);
  detachAction->setEnabled(
      currentView ? (m_workspaceWindow && m_workspaceWindow->containsView(currentView))
                  : (currentWindow && m_workspaceWindow &&
                     m_workspaceWindow->containsDocument(currentWindow)));
  connect(detachAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            if (currentView)
              detachView(currentView);
            else
              detachDocument(currentWindow);
          });

  QAction *attachAction = menu->addAction(
      currentView ? tr("Move Current View to Tabs")
                  : tr("Move Current Image to Tabs"));
  attachAction->setEnabled(
      currentView ? (!m_workspaceWindow || !m_workspaceWindow->containsView(currentView))
                  : (currentWindow &&
                     (!m_workspaceWindow ||
                      !m_workspaceWindow->containsDocument(currentWindow))));
  connect(attachAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            if (currentView)
              attachView(currentView);
            else
              attachDocument(currentWindow);
          });

  QAction *attachAllAction = menu->addAction(tr("Attach All Images and Views as Tabs"));
  bool hasDetached = false;
  for (MainWindow *document : documentWindows()) {
    if (!m_workspaceWindow || !m_workspaceWindow->containsDocument(document)) {
      hasDetached = true;
      break;
    }
  }
  if (!hasDetached) {
    for (ImageViewWindow *view : viewWindows()) {
      if (!m_workspaceWindow || !m_workspaceWindow->containsView(view)) {
        hasDetached = true;
        break;
      }
    }
  }
  attachAllAction->setEnabled(hasDetached);
  connect(attachAllAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
  });

  QMenu *arrangeMenu = menu->addMenu(tr("&Arrange Images"));

  QAction *tabbedAction = arrangeMenu->addAction(tr("Consolidate All to &Tabs"));
  tabbedAction->setCheckable(true);
  tabbedAction->setChecked(m_workspaceWindow &&
                           m_workspaceWindow->isTabbedView());
  connect(tabbedAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->showTabbedDocuments();
  });

  QAction *tileAction = arrangeMenu->addAction(tr("&Tile All"));
  tileAction->setEnabled(documentWindows().size() + viewWindows().size() > 1);
  connect(tileAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->tileDocuments();
  });

  QAction *cascadeAction = arrangeMenu->addAction(tr("&Cascade"));
  cascadeAction->setEnabled(documentWindows().size() + viewWindows().size() > 1);
  connect(cascadeAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->cascadeDocuments();
  });

  const QList<MainWindow *> documents = documentWindows();
  const bool multiple = documents.size() > 1;
  nextAction->setEnabled(multiple);
  previousAction->setEnabled(multiple);

  menu->addSeparator();
  for (int i = 0; i < documents.size(); ++i) {
    QPointer<MainWindow> guarded(documents[i]);
    QString name = documents[i]->documentDisplayName();
    name.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QAction *action = menu->addAction(tr("&%1 %2").arg(i + 1).arg(name));
    action->setCheckable(true);
    action->setChecked(!currentView && documents[i] == currentWindow);
    connect(action, &QAction::triggered, menu, [this, guarded]() {
      if (!guarded)
        return;
      if (m_workspaceWindow && m_workspaceWindow->containsDocument(guarded))
        m_workspaceWindow->activateDocument(guarded);
      else {
        if (guarded->isMinimized())
          guarded->showNormal();
        guarded->raise();
        guarded->activateWindow();
      }
    });
  }

  const QList<ImageViewWindow *> views = viewWindows();
  if (!views.isEmpty()) {
    menu->addSeparator();
    for (ImageViewWindow *view : views) {
      QPointer<ImageViewWindow> guarded(view);
      QAction *action = menu->addAction(view->windowTitle());
      action->setCheckable(true);
      action->setChecked(view == currentView);
      connect(action, &QAction::triggered, menu, [this, guarded]() {
        if (!guarded)
          return;
        if (m_workspaceWindow && m_workspaceWindow->containsView(guarded))
          m_workspaceWindow->activateView(guarded);
        else {
          if (guarded->isMinimized())
            guarded->showNormal();
          guarded->raise();
          guarded->activateWindow();
        }
      });
    }
  }
}

/** Activate the next or previous document, whether tabbed or detached. */
void ColorScreenApplication::activateRelativeWindow(MainWindow *currentWindow,
                                                     int offset) {
  const QList<MainWindow *> documents = documentWindows();
  if (documents.isEmpty())
    return;
  int currentIndex = documents.indexOf(currentWindow);
  if (currentIndex < 0)
    currentIndex = documents.indexOf(activeDocument());
  if (currentIndex < 0)
    currentIndex = 0;
  const int count = documents.size();
  MainWindow *target = documents[((currentIndex + offset) % count + count) % count];
  if (m_workspaceWindow && m_workspaceWindow->containsDocument(target))
    m_workspaceWindow->activateDocument(target);
  else {
    if (target->isMinimized())
      target->showNormal();
    target->raise();
    target->activateWindow();
  }
}

/** Close every document window using each window's normal close policy. */
void ColorScreenApplication::closeAllDocumentWindows() {
  if (m_workspaceWindow) {
    m_workspaceWindow->close();
    return;
  }
  closeAllDocumentsForWorkspace();
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

/** Remove stale secondary-view pointers. */
void ColorScreenApplication::pruneViewWindows() {
  for (auto it = m_viewWindows.begin(); it != m_viewWindows.end();) {
    if (it->isNull())
      it = m_viewWindows.erase(it);
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

/** Return the active document from the workspace or a detached window. */
MainWindow *ColorScreenApplication::activeDocument() const {
  if (QWidget *active = activeWindow()) {
    if (auto *document = dynamic_cast<MainWindow *>(active))
      return document;
    if (auto *view = dynamic_cast<ImageViewWindow *>(active))
      return view->sourceDocument();
    if (m_workspaceWindow && active == m_workspaceWindow)
      return m_workspaceWindow->currentDocument();
  }
  return m_workspaceWindow ? m_workspaceWindow->currentDocument() : nullptr;
}

/** Find an untouched empty window that can host the first requested image. */
MainWindow *ColorScreenApplication::reusableWindow(
    MainWindow *preferredWindow) {
  if (preferredWindow && preferredWindow->canReuseForOpen())
    return preferredWindow;

  if (MainWindow *window = activeDocument()) {
    if (window->canReuseForOpen())
      return window;
  }

  for (MainWindow *window : documentWindows()) {
    if (window->canReuseForOpen())
      return window;
  }
  return nullptr;
}
