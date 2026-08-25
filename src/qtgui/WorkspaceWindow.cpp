#include "WorkspaceWindow.h"

#include "ColorScreenApplication.h"
#include "ImageViewWindow.h"
#include "ImageWidget.h"
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

/** Return the Color-Screen application-level document manager. */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QApplication::instance());
}

/** MDI wrapper that delegates close requests to the document's save policy. */
class DocumentSubWindow final : public QMdiSubWindow {
public:
  explicit DocumentSubWindow(MainWindow *document, QWidget *parent = nullptr)
      : QMdiSubWindow(parent), m_document(document) {
    setAttribute(Qt::WA_DeleteOnClose, false);
  }

protected:
  /** Request normal document closure after the current MDI event completes. */
  void closeEvent(QCloseEvent *event) override {
    event->ignore();
    if (m_closePending || !m_document)
      return;

    m_closePending = true;
    QPointer<MainWindow> document = m_document;
    QTimer::singleShot(0, this, [this, document]() {
      m_closePending = false;
      if (document)
        document->close();
    });
  }

private:
  QPointer<MainWindow> m_document;
  bool m_closePending = false;
};

/** MDI wrapper that closes only the lightweight secondary view. */
class ViewSubWindow final : public QMdiSubWindow {
public:
  explicit ViewSubWindow(WorkspaceWindow *workspace, ImageViewWindow *view,
                         QWidget *parent = nullptr)
      : QMdiSubWindow(parent), m_workspace(workspace), m_view(view) {
    // Attached secondary views are ordinary MDI children.  The wrapper owns
    // and deletes the view when the tab/subwindow is closed; detached views
    // switch back to WA_DeleteOnClose on the ImageViewWindow itself.
    setAttribute(Qt::WA_DeleteOnClose, true);
  }

protected:
  /** Ask the application to preserve the document lifetime, then close. */
  void closeEvent(QCloseEvent *event) override {
    if (m_view) {
      if (ColorScreenApplication *application = documentApplication()) {
        if (!application->requestViewClose(m_view)) {
          event->ignore();
          return;
        }
      } else if (m_workspace) {
        m_workspace->prepareViewForClose(m_view);
      }
    }
    QMdiSubWindow::closeEvent(event);
  }

private:
  QPointer<WorkspaceWindow> m_workspace;
  QPointer<ImageViewWindow> m_view;
};

} // namespace

/** Construct the primary Photoshop/Krita-style multiple-document workspace. */
WorkspaceWindow::WorkspaceWindow(QWidget *parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("workspaceWindow"));
  setWindowTitle(tr("Color-Screen"));
  setDockNestingEnabled(true);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks);

  m_mdiArea = new QMdiArea(this);
  m_mdiArea->setObjectName(QStringLiteral("documentMdiArea"));
  m_mdiArea->setActivationOrder(QMdiArea::CreationOrder);
  m_mdiArea->setDocumentMode(true);
  m_mdiArea->setTabsClosable(true);
  m_mdiArea->setTabsMovable(true);
  m_mdiArea->setTabPosition(QTabWidget::North);
  m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, false);
  m_mdiArea->setViewMode(QMdiArea::TabbedView);
  setCentralWidget(m_mdiArea);

  statusBar()->setObjectName(QStringLiteral("WorkspaceStatusBar"));

  // The ordinary status bar is always a single bottom line.  Only the active
  // document's transient progress may occupy it.  Dedicated long-running tasks
  // live in a frameless bottom dock above the status bar, so rapid transient
  // progress cannot repeatedly change the window's bottom-line height.
  m_workspaceProgressArea = new QWidget(statusBar());
  m_workspaceProgressArea->setObjectName(
      QStringLiteral("WorkspaceProgressArea"));
  m_workspaceProgressLayout = new QVBoxLayout(m_workspaceProgressArea);
  m_workspaceProgressLayout->setContentsMargins(0, 0, 0, 0);
  m_workspaceProgressLayout->setSpacing(0);
  statusBar()->addPermanentWidget(m_workspaceProgressArea, 1);

  m_userVisibleProgressStack = new QWidget();
  m_userVisibleProgressStack->setObjectName(
      QStringLiteral("WorkspaceUserVisibleProgressStack"));
  m_userVisibleProgressLayout = new QVBoxLayout(m_userVisibleProgressStack);
  m_userVisibleProgressLayout->setContentsMargins(4, 2, 4, 2);
  m_userVisibleProgressLayout->setSpacing(2);

  m_userVisibleProgressDock = new QDockWidget(this);
  m_userVisibleProgressDock->setObjectName(
      QStringLiteral("WorkspaceUserVisibleProgressDock"));
  m_userVisibleProgressDock->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_userVisibleProgressDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  auto *taskDockTitle = new QWidget(m_userVisibleProgressDock);
  taskDockTitle->setFixedHeight(0);
  m_userVisibleProgressDock->setTitleBarWidget(taskDockTitle);
  m_userVisibleProgressDock->setWidget(m_userVisibleProgressStack);
  addDockWidget(Qt::BottomDockWidgetArea, m_userVisibleProgressDock);
  m_userVisibleProgressDock->hide();

  m_inspectorStack = new QStackedWidget(this);
  m_inspectorStack->setObjectName(QStringLiteral("documentInspectorStack"));
  m_inspectorDock = new QDockWidget(tr("Document Controls"), this);
  m_inspectorDock->setObjectName(QStringLiteral("DocumentControlsDock"));
  m_inspectorDock->setWidget(m_inspectorStack);
  m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                   Qt::RightDockWidgetArea);
  m_inspectorDock->setMinimumWidth(280);
  addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
  m_inspectorDock->hide();

  connect(m_mdiArea, &QMdiArea::subWindowActivated, this,
          [this](QMdiSubWindow *window) { onSubWindowActivated(window); });

  configureTabBar();
  restoreWorkspaceGeometry();
}

/** Embed DOCUMENT in the shared MDI area without recreating its state. */
void WorkspaceWindow::addDocument(MainWindow *document) {
  if (!document)
    return;
  if (containsDocument(document)) {
    activateDocument(document);
    return;
  }

  document->hide();
  document->prepareForWorkspaceEmbedding();

  if (QWidget *inspector = document->workspaceInspectorWidget()) {
    m_inspectorStack->addWidget(inspector);
    inspector->hide();
  }

  attachUserVisibleProgress(document);
  connect(document, &MainWindow::userVisibleProgressVisibilityChanged, this,
          [this](bool) { updateUserVisibleProgressDockVisibility(); });

  document->setWindowFlags(Qt::Widget);
  auto *subWindow = new DocumentSubWindow(document);
  subWindow->setObjectName(QStringLiteral("documentSubWindow"));
  subWindow->setWindowTitle(document->documentDisplayName());
  subWindow->setWidget(document);
  m_mdiArea->addSubWindow(subWindow);

  QPointer<MainWindow> guardedDocument(document);
  connect(document->statusBar(), &QStatusBar::messageChanged, this,
          [this, guardedDocument](const QString &message) {
            if (!guardedDocument || m_chromeDocument != guardedDocument)
              return;
            if (message.isEmpty())
              statusBar()->clearMessage();
            else
              statusBar()->showMessage(message);
          });

  QPointer<QMdiSubWindow> guardedSubWindow(subWindow);
  connect(document, &QObject::destroyed, this,
          [this, guardedSubWindow]() {
            // The wrapper is owned by the MDI area. Scheduling its deletion is
            // sufficient; removeSubWindow(QMdiSubWindow *) may delete it
            // immediately and must not be followed by deleteLater().
            if (guardedSubWindow)
              guardedSubWindow->deleteLater();
            QTimer::singleShot(0, this, [this]() {
              onSubWindowActivated(m_mdiArea->currentSubWindow());
              configureTabBar();
              scheduleCloseIfEmpty();
            });
          });

  document->show();
  subWindow->show();
  if (m_mdiArea->viewMode() == QMdiArea::TabbedView)
    subWindow->showMaximized();
  m_mdiArea->setActiveSubWindow(subWindow);
  onSubWindowActivated(subWindow);
  configureTabBar();

  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();
}

/** Remove DOCUMENT from the MDI area and restore standalone presentation. */
void WorkspaceWindow::removeDocument(MainWindow *document) {
  if (!document || !containsDocument(document))
    return;

  takeDocumentFromWorkspace(document);
  document->restoreFromWorkspaceEmbedding();
  document->show();

  onSubWindowActivated(m_mdiArea->currentSubWindow());
  configureTabBar();
  scheduleCloseIfEmpty();
}

/** Embed secondary VIEW in the same MDI area as ordinary documents. */
void WorkspaceWindow::addView(ImageViewWindow *view) {
  if (!view)
    return;
  if (containsView(view)) {
    activateView(view);
    return;
  }

  view->hide();
  view->prepareForWorkspaceEmbedding();
  if (QWidget *inspector = view->workspaceInspectorWidget()) {
    if (m_inspectorStack->indexOf(inspector) < 0)
      m_inspectorStack->addWidget(inspector);
    inspector->hide();
  }
  view->setAttribute(Qt::WA_DeleteOnClose, false);
  view->setWindowFlags(Qt::Widget);

  auto *subWindow = new ViewSubWindow(this, view);
  subWindow->setObjectName(QStringLiteral("imageViewSubWindow"));
  subWindow->setWindowTitle(view->windowTitle());
  subWindow->setWidget(view);
  m_mdiArea->addSubWindow(subWindow);

  QPointer<ImageViewWindow> guardedView(view);
  QPointer<QMdiSubWindow> guardedSubWindow(subWindow);
  connect(view, &QWidget::windowTitleChanged, this,
          [this, guardedView, guardedSubWindow](const QString &title) {
            if (!guardedView || !guardedSubWindow)
              return;
            guardedSubWindow->setWindowTitle(title);
            if (m_chromeView == guardedView)
              setWindowTitle(title + tr(" — Color-Screen"));
            configureTabBar();
          });
  connect(view->statusBar(), &QStatusBar::messageChanged, this,
          [this, guardedView](const QString &message) {
            if (!guardedView || m_chromeView != guardedView)
              return;
            if (message.isEmpty())
              statusBar()->clearMessage();
            else
              statusBar()->showMessage(message);
          });

  connect(subWindow, &QObject::destroyed, this, [this]() {
    QTimer::singleShot(0, this, [this]() {
      onSubWindowActivated(m_mdiArea->currentSubWindow());
      configureTabBar();
      scheduleCloseIfEmpty();
    });
  });

  view->show();
  subWindow->show();
  if (m_mdiArea->viewMode() == QMdiArea::TabbedView)
    subWindow->showMaximized();
  m_mdiArea->setActiveSubWindow(subWindow);
  onSubWindowActivated(subWindow);
  configureTabBar();

  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();
}

/** Remove secondary VIEW from the MDI area and show it standalone. */
void WorkspaceWindow::removeView(ImageViewWindow *view) {
  if (!view || !containsView(view))
    return;

  takeViewFromWorkspace(view);
  onSubWindowActivated(m_mdiArea->currentSubWindow());
  configureTabBar();
  scheduleCloseIfEmpty();

  view->restoreFromWorkspaceEmbedding();
  view->show();
  view->raise();
  view->activateWindow();
}

/** Return the active document, resolving secondary views to their owner. */
MainWindow *WorkspaceWindow::currentDocument() const {
  QMdiSubWindow *window = m_mdiArea->currentSubWindow();
  if (!window)
    window = m_mdiArea->activeSubWindow();
  if (MainWindow *document = documentForSubWindow(window))
    return document;
  if (ImageViewWindow *view = viewForSubWindow(window))
    return view->sourceDocument();
  return nullptr;
}

/** Return the number of documents attached to the MDI workspace. */
int WorkspaceWindow::tabCount() const {
  return m_mdiArea->subWindowList().size();
}

/** Return whether DOCUMENT is attached to this workspace. */
bool WorkspaceWindow::containsDocument(MainWindow *document) const {
  return subWindowForDocument(document) != nullptr;
}

/** Return whether secondary VIEW is attached to this workspace. */
bool WorkspaceWindow::containsView(ImageViewWindow *view) const {
  return subWindowForView(view) != nullptr;
}

/** Select DOCUMENT in either tabbed or subwindow presentation. */
void WorkspaceWindow::activateDocument(MainWindow *document) {
  QMdiSubWindow *subWindow = subWindowForDocument(document);
  if (!subWindow)
    return;

  m_mdiArea->setActiveSubWindow(subWindow);
  onSubWindowActivated(subWindow);
  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();
  document->setFocus();
}

/** Select secondary VIEW in either tabbed or subwindow presentation. */
void WorkspaceWindow::activateView(ImageViewWindow *view) {
  QMdiSubWindow *subWindow = subWindowForView(view);
  if (!subWindow)
    return;

  m_mdiArea->setActiveSubWindow(subWindow);
  onSubWindowActivated(subWindow);
  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();
  view->setFocus();
}

/** Update tab/subwindow text after filename or modified-state changes. */
void WorkspaceWindow::refreshDocument(MainWindow *document) {
  QMdiSubWindow *subWindow = subWindowForDocument(document);
  if (!subWindow)
    return;

  subWindow->setWindowTitle(document->documentDisplayName());
  if (m_chromeDocument == document)
    setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
  configureTabBar();
}

/** Restore document-owned widgets before the logical owner can be destroyed. */
void WorkspaceWindow::prepareDocumentForClose(MainWindow *document) {
  if (!document)
    return;

  const bool wasAttached = containsDocument(document);
  if (wasAttached) {
    takeDocumentFromWorkspace(document);
    document->restoreFromWorkspaceEmbedding();
  } else {
    // A closed primary presentation can still own the state used by secondary
    // views. Its inspector may therefore be borrowed by the workspace or a
    // detached view when the last peer finally closes.
    if (QWidget *inspector = document->workspaceInspectorWidget()) {
      if (m_inspectorStack->indexOf(inspector) >= 0)
        m_inspectorStack->removeWidget(inspector);
    }
    document->restoreWorkspaceInspector();
  }

  // When the primary presentation was already closed, the current MDI child is
  // the last secondary view being torn down. Reinstalling its chrome here would
  // immediately borrow the document inspector again after we reclaimed it.
  if (wasAttached)
    onSubWindowActivated(m_mdiArea->currentSubWindow());
  configureTabBar();
  scheduleCloseIfEmpty();
}

/** Restore view-owned chrome before WA_DeleteOnClose destroys an MDI view. */
void WorkspaceWindow::prepareViewForClose(ImageViewWindow *view) {
  if (!view || !containsView(view))
    return;

  // Do not remove or delete the QMdiSubWindow from inside its close event.
  // Only return widgets that the shared shell borrowed from the view.  The
  // wrapper then performs the normal Qt MDI close and owns destruction of its
  // child view.
  releaseViewChrome(view, false);
  if (view->ownsWorkspaceInspector()) {
    if (QWidget *inspector = view->workspaceInspectorWidget()) {
      m_inspectorStack->removeWidget(inspector);
      inspector->hide();
      inspector->setParent(view);
    }
  } else if (MainWindow *document = view->sourceDocument()) {
    document->setInspectorImageWidget(document->primaryImageWidget());
  }
}

/** Close attached VIEW through its owning MDI subwindow. */
bool WorkspaceWindow::closeView(ImageViewWindow *view) {
  QMdiSubWindow *subWindow = subWindowForView(view);
  return subWindow ? subWindow->close() : false;
}

/** Present attached images as standard Qt MDI tabs. */
void WorkspaceWindow::showTabbedDocuments() {
  m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, false);
  m_mdiArea->setViewMode(QMdiArea::TabbedView);
  m_mdiArea->setTabsClosable(true);
  m_mdiArea->setTabsMovable(true);
  if (QMdiSubWindow *active = m_mdiArea->currentSubWindow()) {
    active->showMaximized();
    m_mdiArea->setActiveSubWindow(active);
  }
  configureTabBar();
}

/** Present attached documents as equally sized MDI tiles. */
void WorkspaceWindow::tileDocuments() {
  // In subwindow mode an activation must not promote a tile back to the
  // maximized state inherited from tabbed presentation.
  m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
  m_mdiArea->setViewMode(QMdiArea::SubWindowView);
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList())
    subWindow->showNormal();
  m_mdiArea->tileSubWindows();
}

/** Present attached documents as cascading MDI subwindows. */
void WorkspaceWindow::cascadeDocuments() {
  // Cascaded windows use the same ordinary subwindow activation policy as
  // tiled windows.
  m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
  m_mdiArea->setViewMode(QMdiArea::SubWindowView);
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList())
    subWindow->showNormal();
  m_mdiArea->cascadeSubWindows();
}

/** Return whether Qt's tabbed MDI presentation is active. */
bool WorkspaceWindow::isTabbedView() const {
  return m_mdiArea->viewMode() == QMdiArea::TabbedView;
}

/** Return whether Qt's standard document tab bar is currently visible. */
bool WorkspaceWindow::isTabBarVisible() const {
  QTabBar *tabBar = documentTabBar();
  return tabBar && tabBar->isVisible();
}

/** Restore the workspace geometry only when it still fits the current desktop. */
void WorkspaceWindow::restoreWorkspaceGeometry() {
  QSettings settings;
  bool compatibleDesktop = true;
  if (QScreen *screen = QApplication::primaryScreen()) {
    const QSize saved = settings.value("workspaceDesktopSize").toSize();
    const QSize current = screen->availableGeometry().size();
    if (saved.isValid() &&
        (qAbs(saved.width() - current.width()) > 100 ||
         qAbs(saved.height() - current.height()) > 100))
      compatibleDesktop = false;
  }

  if (compatibleDesktop && settings.contains("workspaceGeometry")) {
    restoreGeometry(settings.value("workspaceGeometry").toByteArray());
  } else if (QScreen *screen = QApplication::primaryScreen()) {
    const QRect area = screen->availableGeometry();
    resize(qMin(1400, qMax(900, area.width() * 4 / 5)),
           qMin(950, qMax(650, area.height() * 4 / 5)));
    move(area.center() - rect().center());
  } else {
    resize(1200, 800);
  }
}

/** Persist the outer workspace geometry independently of document state. */
void WorkspaceWindow::saveWorkspaceGeometry() const {
  QSettings settings;
  settings.setValue("workspaceGeometry", saveGeometry());
  if (QScreen *screen = QApplication::primaryScreen())
    settings.setValue("workspaceDesktopSize", screen->availableGeometry().size());
}

/** Return the MDI wrapper associated with DOCUMENT. */
QMdiSubWindow *
WorkspaceWindow::subWindowForDocument(MainWindow *document) const {
  if (!document)
    return nullptr;
  for (QMdiSubWindow *window : m_mdiArea->subWindowList()) {
    if (window && window->widget() == document)
      return window;
  }
  return nullptr;
}

/** Return the MDI wrapper associated with secondary VIEW. */
QMdiSubWindow *WorkspaceWindow::subWindowForView(ImageViewWindow *view) const {
  if (!view)
    return nullptr;
  for (QMdiSubWindow *window : m_mdiArea->subWindowList()) {
    if (window && window->widget() == view)
      return window;
  }
  return nullptr;
}

/** Return the MainWindow hosted by WINDOW. */
MainWindow *
WorkspaceWindow::documentForSubWindow(QMdiSubWindow *window) const {
  return window ? qobject_cast<MainWindow *>(window->widget()) : nullptr;
}

/** Return the secondary view hosted by WINDOW. */
ImageViewWindow *
WorkspaceWindow::viewForSubWindow(QMdiSubWindow *window) const {
  return window ? qobject_cast<ImageViewWindow *>(window->widget()) : nullptr;
}

/** Return Qt's internal MDI tab bar, when tabbed view has created one. */
QTabBar *WorkspaceWindow::documentTabBar() const {
  return m_mdiArea->findChild<QTabBar *>(
      QString(), Qt::FindDirectChildrenOnly);
}

/** Activate and return the hosted widget represented by tab INDEX. */
QWidget *WorkspaceWindow::windowAtTab(int index) const {
  QTabBar *tabBar = documentTabBar();
  if (!tabBar || index < 0 || index >= tabBar->count())
    return nullptr;

  tabBar->setCurrentIndex(index);
  QMdiSubWindow *window = m_mdiArea->currentSubWindow();
  return window ? window->widget() : nullptr;
}

/** Configure standard tabs, context actions, and drag-out detachment. */
void WorkspaceWindow::configureTabBar() {
  QTimer::singleShot(0, this, [this]() {
    QTabBar *tabBar = documentTabBar();
    if (!tabBar)
      return;

    tabBar->setDocumentMode(true);
    tabBar->setMovable(true);
    tabBar->setTabsClosable(true);
    tabBar->setElideMode(Qt::ElideMiddle);
    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

    if (m_tabBarEventTarget.data() != tabBar) {
      if (m_tabBarEventTarget)
        m_tabBarEventTarget->removeEventFilter(this);
      m_tabBarEventTarget = tabBar;
      tabBar->installEventFilter(this);
    }

    if (tabBar->property("colorscreenConfigured").toBool())
      return;
    tabBar->setProperty("colorscreenConfigured", true);

    connect(tabBar, &QWidget::customContextMenuRequested, this,
            [this, tabBar](const QPoint &position) {
              QWidget *hosted = windowAtTab(tabBar->tabAt(position));
              if (!hosted)
                return;

              auto *document = qobject_cast<MainWindow *>(hosted);
              auto *view = qobject_cast<ImageViewWindow *>(hosted);
              if (!document && !view)
                return;

              QMenu menu(this);
              QAction *detach = menu.addAction(
                  view ? tr("Detach View") : tr("Detach Image"));
              menu.addSeparator();
              QAction *tabbed = menu.addAction(tr("Tabbed Documents"));
              QAction *tile = menu.addAction(tr("Tile Documents"));
              QAction *cascade = menu.addAction(tr("Cascade Documents"));
              QAction *selected = menu.exec(tabBar->mapToGlobal(position));
              if (selected == detach) {
                if (view)
                  detachView(view);
                else
                  detachDocument(document);
              } else if (selected == tabbed)
                showTabbedDocuments();
              else if (selected == tile)
                tileDocuments();
              else if (selected == cascade)
                cascadeDocuments();
            });

    connect(tabBar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
      QWidget *hosted = windowAtTab(index);
      if (auto *view = qobject_cast<ImageViewWindow *>(hosted))
        detachView(view);
      else if (auto *document = qobject_cast<MainWindow *>(hosted))
        detachDocument(document);
    });
  });
}

/** Close an empty workspace shell on the next event-loop turn. */
void WorkspaceWindow::scheduleCloseIfEmpty() {
  if (m_closing || !m_mdiArea || !m_mdiArea->subWindowList().isEmpty())
    return;

  QTimer::singleShot(0, this, [this]() {
    if (!m_closing && m_mdiArea && m_mdiArea->subWindowList().isEmpty() &&
        isVisible())
      close();
  });
}

/** Keep DOCUMENT's persistent long-running task rows globally visible. */
void WorkspaceWindow::attachUserVisibleProgress(MainWindow *document) {
  if (!document || !m_userVisibleProgressLayout)
    return;

  QWidget *progress = document->workspaceUserVisibleStatusWidget();
  if (!progress || progress->parentWidget() == m_userVisibleProgressStack) {
    updateUserVisibleProgressDockVisibility();
    return;
  }

  progress = document->takeUserVisibleStatusWidget();
  if (!progress)
    return;
  progress->setParent(m_userVisibleProgressStack);
  m_userVisibleProgressLayout->addWidget(progress);
  updateUserVisibleProgressDockVisibility();
}

/** Return DOCUMENT's persistent task rows to a detached document window. */
void WorkspaceWindow::detachUserVisibleProgress(MainWindow *document) {
  if (!document)
    return;

  QWidget *progress = document->workspaceUserVisibleStatusWidget();
  if (progress && progress->parentWidget() == m_userVisibleProgressStack)
    m_userVisibleProgressLayout->removeWidget(progress);
  document->restoreUserVisibleStatusWidget();
  updateUserVisibleProgressDockVisibility();
}

/** Show the dedicated task strip only while at least one row is visible. */
void WorkspaceWindow::updateUserVisibleProgressDockVisibility() {
  if (!m_userVisibleProgressDock || !m_userVisibleProgressLayout)
    return;

  bool hasVisibleRows = false;
  for (int i = 0; i < m_userVisibleProgressLayout->count(); ++i) {
    QLayoutItem *item = m_userVisibleProgressLayout->itemAt(i);
    QWidget *container = item ? item->widget() : nullptr;
    if (container && !container->isHidden()) {
      hasVisibleRows = true;
      break;
    }
  }
  m_userVisibleProgressDock->setVisible(hasVisibleRows);
}


/** Present DOCUMENT's one inspector in the workspace for IMAGEWIDGET. */
void WorkspaceWindow::installDocumentInspector(MainWindow *document,
                                               ImageWidget *imageWidget) {
  if (!document) {
    m_inspectorDock->hide();
    return;
  }

  QWidget *inspector = document->workspaceInspectorWidget();
  if (!inspector) {
    m_inspectorDock->hide();
    return;
  }

  if (m_inspectorStack->indexOf(inspector) < 0) {
    document->takeWorkspaceInspector();
    m_inspectorStack->addWidget(inspector);
  }
  document->setInspectorImageWidget(imageWidget);
  m_inspectorStack->setCurrentWidget(inspector);
  inspector->show();
  m_inspectorDock->setWindowTitle(tr("Document Controls"));
  m_inspectorDock->show();
}

/** Present DOCUMENT's menus, toolbar, inspector, and transient status row. */
void WorkspaceWindow::installDocumentChrome(MainWindow *document) {
  if (!document)
    return;

  menuBar()->clear();
  for (QAction *action : document->menuBar()->actions())
    menuBar()->addAction(action);

  if (QToolBar *toolbar = document->workspaceToolBar()) {
    document->removeToolBar(toolbar);
    if (toolbar->parentWidget() != this)
      toolbar->setParent(this);
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->show();
  }

  installDocumentInspector(document, document->primaryImageWidget());

  if (QWidget *statusWidget = document->workspaceStatusWidget()) {
    if (statusWidget->parentWidget() != m_workspaceProgressArea) {
      const bool explicitlyHidden = statusWidget->isHidden();
      document->statusBar()->removeWidget(statusWidget);
      statusWidget->setParent(m_workspaceProgressArea);
      m_workspaceProgressLayout->addWidget(statusWidget);
      statusWidget->setVisible(!explicitlyHidden);
    }
  }

  const QString message = document->statusBar()->currentMessage();
  if (message.isEmpty())
    statusBar()->clearMessage();
  else
    statusBar()->showMessage(message);

  document->refreshWindowMenu();
  setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
}

/** Remove DOCUMENT's shared chrome and optionally show it in its own window. */
void WorkspaceWindow::releaseDocumentChrome(MainWindow *document,
                                             bool showInWindow) {
  if (!document)
    return;

  if (QToolBar *toolbar = document->workspaceToolBar()) {
    if (toolbar->parentWidget() == this)
      removeToolBar(toolbar);
    if (toolbar->parentWidget() != document)
      toolbar->setParent(document);
    document->addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->setVisible(showInWindow);
  }

  if (QWidget *statusWidget = document->workspaceStatusWidget()) {
    if (statusWidget->parentWidget() == m_workspaceProgressArea) {
      const bool explicitlyHidden = statusWidget->isHidden();
      m_workspaceProgressLayout->removeWidget(statusWidget);
      document->statusBar()->addPermanentWidget(statusWidget, 1);
      statusWidget->setVisible(!explicitlyHidden);
    }
    document->statusBar()->setVisible(showInWindow);
  }

  document->menuBar()->setVisible(showInWindow);
  if (m_chromeDocument == document) {
    statusBar()->clearMessage();
    menuBar()->clear();
    m_chromeDocument.clear();
  }
}

/** Detach DOCUMENT through ColorScreenApplication without copying its state. */
void WorkspaceWindow::detachDocument(MainWindow *document) {
  if (!document)
    return;
  if (ColorScreenApplication *application = documentApplication())
    application->detachDocument(document);
}

/** Detach secondary VIEW through the application manager. */
void WorkspaceWindow::detachView(ImageViewWindow *view) {
  if (!view)
    return;
  if (ColorScreenApplication *application = documentApplication())
    application->detachView(view);
}

/** Present VIEW's standard menu/toolbar chrome in the shared workspace. */
void WorkspaceWindow::installViewChrome(ImageViewWindow *view) {
  if (!view)
    return;

  menuBar()->clear();
  for (QAction *action : view->menuBar()->actions())
    menuBar()->addAction(action);

  if (QToolBar *toolbar = view->workspaceToolBar()) {
    if (auto *owner = qobject_cast<QMainWindow *>(toolbar->parentWidget()))
      owner->removeToolBar(toolbar);
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->show();
  }

  if (view->isSlantedEdgeReference()) {
    if (QWidget *inspector = view->workspaceInspectorWidget()) {
      if (m_inspectorStack->indexOf(inspector) < 0)
        m_inspectorStack->addWidget(inspector);
      m_inspectorStack->setCurrentWidget(inspector);
      inspector->show();
      m_inspectorDock->setWindowTitle(tr("Reference Controls"));
      m_inspectorDock->show();
    } else {
      m_inspectorDock->hide();
    }
  } else {
    installDocumentInspector(view->sourceDocument(), view->imageWidget());
  }
  const QString message = view->statusBar()->currentMessage();
  if (message.isEmpty())
    statusBar()->clearMessage();
  else
    statusBar()->showMessage(message);
  setWindowTitle(view->windowTitle() + tr(" — Color-Screen"));
}

/** Return VIEW's toolbar/menu/status presentation to its own window. */
void WorkspaceWindow::releaseViewChrome(ImageViewWindow *view,
                                        bool showInWindow) {
  if (!view)
    return;

  if (QToolBar *toolbar = view->workspaceToolBar()) {
    if (auto *owner = qobject_cast<QMainWindow *>(toolbar->parentWidget()))
      owner->removeToolBar(toolbar);
    view->addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->setVisible(showInWindow);
  }

  view->menuBar()->setVisible(showInWindow);
  view->statusBar()->setVisible(showInWindow);
  if (m_chromeView == view) {
    statusBar()->clearMessage();
    menuBar()->clear();
    m_inspectorDock->setWindowTitle(tr("Document Controls"));
    m_chromeView.clear();
  }
}

/** Switch the shared shell to WINDOW's document or secondary view. */
void WorkspaceWindow::onSubWindowActivated(QMdiSubWindow *window) {
  MainWindow *document = documentForSubWindow(window);
  ImageViewWindow *view = viewForSubWindow(window);

  if ((document && m_chromeDocument == document && !m_chromeView) ||
      (view && m_chromeView == view && !m_chromeDocument)) {
    if (document) {
      installDocumentInspector(document, document->primaryImageWidget());
      setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
    } else if (view) {
      if (!view->isSlantedEdgeReference())
        installDocumentInspector(view->sourceDocument(), view->imageWidget());
      setWindowTitle(view->windowTitle() + tr(" — Color-Screen"));
    }
    return;
  }

  if (m_chromeDocument)
    releaseDocumentChrome(m_chromeDocument, false);
  if (m_chromeView)
    releaseViewChrome(m_chromeView, false);

  m_chromeDocument = document;
  m_chromeView = view;
  if (document) {
    installDocumentChrome(document);
  } else if (view) {
    installViewChrome(view);
  } else {
    menuBar()->clear();
    m_inspectorDock->hide();
    statusBar()->clearMessage();
    setWindowTitle(tr("Color-Screen"));
  }
}

/** Remove DOCUMENT's wrapper and inspector while keeping DOCUMENT alive. */
void WorkspaceWindow::takeDocumentFromWorkspace(MainWindow *document) {
  QMdiSubWindow *subWindow = subWindowForDocument(document);
  if (!subWindow)
    return;

  releaseDocumentChrome(document, true);
  detachUserVisibleProgress(document);

  if (QWidget *inspector = document->workspaceInspectorWidget()) {
    m_inspectorStack->removeWidget(inspector);
    document->takeWorkspaceInspector();
  }

  document->hide();
  // Removing the hosted widget leaves its QMdiSubWindow wrapper alive.
  // Delete that wrapper exactly once below; removing the wrapper itself
  // here would delete it immediately and make deleteLater() unsafe.
  m_mdiArea->removeSubWindow(document);
  subWindow->deleteLater();

  document->setParent(nullptr);
  document->setWindowFlags(Qt::Window);
}

/** Remove secondary VIEW's MDI wrapper while keeping VIEW alive. */
void WorkspaceWindow::takeViewFromWorkspace(ImageViewWindow *view) {
  QMdiSubWindow *subWindow = subWindowForView(view);
  if (!subWindow)
    return;

  releaseViewChrome(view, true);
  if (view->ownsWorkspaceInspector()) {
    if (QWidget *inspector = view->workspaceInspectorWidget()) {
      m_inspectorStack->removeWidget(inspector);
      inspector->hide();
      inspector->setParent(nullptr);
    }
  } else if (MainWindow *document = view->sourceDocument()) {
    if (QWidget *inspector = document->workspaceInspectorWidget()) {
      m_inspectorStack->removeWidget(inspector);
      document->takeWorkspaceInspector();
    }
  }
  view->hide();
  m_mdiArea->removeSubWindow(view);
  subWindow->deleteLater();

  view->setParent(nullptr);
  view->setWindowFlags(Qt::Window);
  view->setAttribute(Qt::WA_DeleteOnClose, true);
}

/** Detach a tab when it is dragged beyond the tab-bar docking margin. */
bool WorkspaceWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched != m_tabBarEventTarget.data())
    return QMainWindow::eventFilter(watched, event);

  // QMdiArea destroys and recreates its internal QTabBar when switching view
  // modes. Event filters can still run after QTabBar's derived destructor has
  // entered QWidget::~QWidget(), so a typed QPointer<QTabBar> downcast is
  // undefined at that point. Keep only QObject identity persistently and cast
  // the live event target back to QTabBar for real tab events.
  QTabBar *tabBar = qobject_cast<QTabBar *>(watched);
  if (!tabBar)
    return QMainWindow::eventFilter(watched, event);

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      m_dragWindow =
          windowAtTab(tabBar->tabAt(mouseEvent->position().toPoint()));
      m_dragStartGlobal = mouseEvent->globalPosition().toPoint();
    }
    break;
  }
  case QEvent::MouseMove: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (!m_dragWindow || !(mouseEvent->buttons() & Qt::LeftButton))
      break;

    const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
    if ((globalPosition - m_dragStartGlobal).manhattanLength() <
        QApplication::startDragDistance())
      break;

    const QPoint localPosition = tabBar->mapFromGlobal(globalPosition);
    const QRect dockingMargin = tabBar->rect().adjusted(-24, -32, 24, 32);
    if (dockingMargin.contains(localPosition))
      break;

    QPointer<QWidget> hosted = m_dragWindow;
    m_dragWindow.clear();
    QTimer::singleShot(0, this, [this, hosted]() {
      if (auto *view = qobject_cast<ImageViewWindow *>(hosted.data()))
        detachView(view);
      else if (auto *document = qobject_cast<MainWindow *>(hosted.data()))
        detachDocument(document);
    });
    return true;
  }
  case QEvent::MouseButtonRelease:
    m_dragWindow.clear();
    break;
  default:
    break;
  }

  return QMainWindow::eventFilter(watched, event);
}


/** Reclaim the active inspector after focus returns from a detached view. */
void WorkspaceWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (!event || event->type() != QEvent::WindowActivate)
    return;

  if (m_chromeDocument)
    installDocumentInspector(m_chromeDocument,
                             m_chromeDocument->primaryImageWidget());
  else if (m_chromeView && !m_chromeView->isSlantedEdgeReference())
    installDocumentInspector(m_chromeView->sourceDocument(),
                             m_chromeView->imageWidget());
}

/** Close only the presentations hosted by this workspace shell. */
void WorkspaceWindow::closeEvent(QCloseEvent *event) {
  if (m_closing) {
    event->accept();
    return;
  }

  m_closing = true;
  ColorScreenApplication *application = documentApplication();
  QList<QPointer<ImageViewWindow>> views;
  QList<QPointer<MainWindow>> documents;
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList()) {
    if (ImageViewWindow *view = viewForSubWindow(subWindow))
      views.append(view);
    else if (MainWindow *document = documentForSubWindow(subWindow))
      documents.append(document);
  }

  // Close secondary presentations first. A primary in this same workspace can
  // then close normally; detached peers still cause it to become a hidden
  // document owner rather than being destroyed.
  for (const QPointer<ImageViewWindow> &view : views) {
    if (view && !closeView(view)) {
      m_closing = false;
      event->ignore();
      return;
    }
  }

  for (const QPointer<MainWindow> &document : documents) {
    if (!document)
      continue;
    const bool closed = document->close();
    if (!closed &&
        (!application || application->isDocumentPresentationOpen(document))) {
      m_closing = false;
      event->ignore();
      return;
    }
  }

  saveWorkspaceGeometry();
  m_closing = false;
  event->accept();
}
