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
#include <QHBoxLayout>
#include <QLabel>
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
#include <QToolButton>
#include <utility>
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

  // Keep the outer QMainWindow's toolbar-area topology fixed for its complete
  // lifetime.  Active document/view toolbars are ordinary child widgets in
  // this host, never QToolBars added to or removed from the workspace layout.
  m_workspaceToolBar = new QToolBar(tr("Main Toolbar"), this);
  m_workspaceToolBar->setObjectName(QStringLiteral("WorkspaceToolbar"));
  m_workspaceToolBar->setMovable(false);
  m_workspaceToolBarHost = new QWidget(m_workspaceToolBar);
  m_workspaceToolBarHost->setObjectName(QStringLiteral("WorkspaceToolbarHost"));
  m_workspaceToolBarLayout = new QHBoxLayout(m_workspaceToolBarHost);
  m_workspaceToolBarLayout->setContentsMargins(0, 0, 0, 0);
  m_workspaceToolBarLayout->setSpacing(0);
  m_workspaceToolBar->addWidget(m_workspaceToolBarHost);
  addToolBar(Qt::TopToolBarArea, m_workspaceToolBar);
  m_workspaceToolBar->hide();

  statusBar()->setObjectName(QStringLiteral("WorkspaceStatusBar"));

  // The status bar is workspace-global. Every attached logical document
  // contributes one transient-progress page, and a small outer switcher
  // selects among concurrently working documents without following tabs.
  m_workspaceProgressArea = new QWidget(statusBar());
  m_workspaceProgressArea->setObjectName(
      QStringLiteral("WorkspaceProgressArea"));
  m_workspaceProgressLayout = new QHBoxLayout(m_workspaceProgressArea);
  m_workspaceProgressLayout->setContentsMargins(0, 0, 0, 0);
  m_workspaceProgressLayout->setSpacing(6);

  m_workspaceProgressStack = new QStackedWidget(m_workspaceProgressArea);
  m_workspaceProgressStack->setObjectName(
      QStringLiteral("WorkspaceTransientProgressStack"));
  m_workspaceProgressLayout->addWidget(m_workspaceProgressStack, 1);

  m_workspaceProgressDocumentLabel =
      new QLabel(m_workspaceProgressArea);
  m_workspaceProgressDocumentLabel->setObjectName(
      QStringLiteral("WorkspaceProgressDocumentLabel"));
  m_workspaceProgressLayout->addWidget(m_workspaceProgressDocumentLabel);

  m_workspaceProgressPreviousButton =
      new QToolButton(m_workspaceProgressArea);
  m_workspaceProgressPreviousButton->setObjectName(
      QStringLiteral("WorkspaceProgressPreviousButton"));
  m_workspaceProgressPreviousButton->setText(QStringLiteral("<"));
  m_workspaceProgressPreviousButton->setToolTip(
      tr("Previous document progress"));
  connect(m_workspaceProgressPreviousButton, &QToolButton::clicked, this,
          [this]() { cycleWorkspaceProgress(-1); });
  m_workspaceProgressLayout->addWidget(
      m_workspaceProgressPreviousButton);

  m_workspaceProgressNextButton = new QToolButton(m_workspaceProgressArea);
  m_workspaceProgressNextButton->setObjectName(
      QStringLiteral("WorkspaceProgressNextButton"));
  m_workspaceProgressNextButton->setText(QStringLiteral(">"));
  m_workspaceProgressNextButton->setToolTip(
      tr("Next document progress"));
  connect(m_workspaceProgressNextButton, &QToolButton::clicked, this,
          [this]() { cycleWorkspaceProgress(1); });
  m_workspaceProgressLayout->addWidget(m_workspaceProgressNextButton);
  m_workspaceProgressArea->hide();
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
  document->setWorkspaceStatusBar(statusBar());

  if (QWidget *inspector = document->workspaceInspectorWidget()) {
    m_inspectorStack->addWidget(inspector);
    inspector->hide();
  }

  attachDocumentProgress(document);

  document->setWindowFlags(Qt::Widget);
  auto *subWindow = new DocumentSubWindow(document);
  subWindow->setObjectName(QStringLiteral("documentSubWindow"));
  subWindow->setWindowTitle(document->documentDisplayName());
  subWindow->setWidget(document);
  // addSubWindow() may activate synchronously. Keep shared chrome handoff
  // blocked until the embedded child and outer workspace have completed their
  // first show/layout pass. The child toolbar stays in the child's original
  // QMainWindowLayout until that first layout is complete.
  ++m_chromeActivationBlockDepth;
  m_mdiArea->addSubWindow(subWindow);

  QPointer<MainWindow> guardedDocument(document);
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

  // Let both QMainWindows establish their initial layout before the active
  // child's toolbar leaves its original layout.  The workspace toolbar-area
  // itself is already permanent and will not change during the handoff.
  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();

  --m_chromeActivationBlockDepth;
  onSubWindowActivated(subWindow);
  configureTabBar();
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
  view->setWorkspaceStatusBar(statusBar());
  if (QWidget *inspector = view->workspaceInspectorWidget()) {
    if (m_inspectorStack->indexOf(inspector) < 0)
      m_inspectorStack->addWidget(inspector);
    inspector->hide();
  }
  attachDocumentProgress(view->sourceDocument());
  view->setAttribute(Qt::WA_DeleteOnClose, false);
  view->setWindowFlags(Qt::Widget);

  auto *subWindow = new ViewSubWindow(this, view);
  subWindow->setObjectName(QStringLiteral("imageViewSubWindow"));
  subWindow->setWindowTitle(view->windowTitle());
  subWindow->setWidget(view);
  // Use the same first-show ordering for secondary views: the source toolbar
  // remains in the view's QMainWindowLayout until the embedded layout settles.
  ++m_chromeActivationBlockDepth;
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
  QPointer<MainWindow> guardedSource(view->sourceDocument());
  connect(subWindow, &QObject::destroyed, this, [this, guardedSource]() {
    QTimer::singleShot(0, this, [this, guardedSource]() {
      detachDocumentProgressIfUnused(guardedSource);
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

  // As with documents, finish both first layouts before moving the child
  // toolbar into the permanent workspace slot.
  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();

  --m_chromeActivationBlockDepth;
  onSubWindowActivated(subWindow);
  configureTabBar();
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
  int count = 0;
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList()) {
    if (documentForSubWindow(subWindow) || viewForSubWindow(subWindow))
      ++count;
  }
  return count;
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

    if (m_tabBar.data() != tabBar) {
      if (auto *oldTabBar = qobject_cast<QTabBar *>(m_tabBar.data()))
        oldTabBar->removeEventFilter(this);
      m_tabBar = tabBar;
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

/** Hide an empty workspace immediately, then finalize the close asynchronously. */
void WorkspaceWindow::scheduleCloseIfEmpty() {
  if (m_closing || !m_mdiArea)
    return;

  auto hasHostedWindow = [this]() {
    for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList()) {
      if (documentForSubWindow(subWindow) || viewForSubWindow(subWindow))
        return true;
    }
    return false;
  };

  if (hasHostedWindow())
    return;

  // A detached presentation can leave an empty QMdiSubWindow wrapper pending
  // deleteLater(). Hide based on hosted content, not wrapper lifetime.
  hide();
  QTimer::singleShot(0, this, [this]() {
    if (m_closing || !m_mdiArea)
      return;
    for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList()) {
      if (documentForSubWindow(subWindow) || viewForSubWindow(subWindow))
        return;
    }
    close();
  });
}

/** Return whether DOCUMENT still has a presentation in this workspace. */
bool WorkspaceWindow::hasAttachedPresentation(MainWindow *document) const {
  if (!document || !m_mdiArea)
    return false;
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList()) {
    if (documentForSubWindow(subWindow) == document)
      return true;
    if (ImageViewWindow *view = viewForSubWindow(subWindow)) {
      if (view->sourceDocument() == document)
        return true;
    }
  }
  return false;
}

/** Permanently attach one logical document's progress to the workspace shell. */
void WorkspaceWindow::attachDocumentProgress(MainWindow *document) {
  if (!document || !m_workspaceProgressStack)
    return;

  bool alreadyAttached = false;
  for (const QPointer<MainWindow> &candidate : std::as_const(m_progressDocuments)) {
    if (candidate == document) {
      alreadyAttached = true;
      break;
    }
  }

  if (!alreadyAttached) {
    QWidget *progress = document->takeWorkspaceStatusWidget();
    if (progress) {
      progress->setParent(m_workspaceProgressStack);
      m_workspaceProgressStack->addWidget(progress);
      m_progressDocuments.append(document);
      const int stableHeight = qMax(
          statusBar()->minimumHeight(),
          qMax(progress->minimumHeight(), progress->sizeHint().height()));
      statusBar()->setMinimumHeight(stableHeight);
    }
  }

  bool signalsConnected = false;
  for (const QPointer<MainWindow> &candidate :
       std::as_const(m_progressSignalDocuments)) {
    if (candidate == document) {
      signalsConnected = true;
      break;
    }
  }

  if (!signalsConnected) {
    m_progressSignalDocuments.append(document);
    QPointer<MainWindow> guardedDocument(document);
    connect(document, &MainWindow::transientProgressVisibilityChanged, this,
            [this, guardedDocument](bool visible) {
              if (visible)
                m_displayedProgressDocument = guardedDocument;
              updateWorkspaceProgressPresentation();
            });
    connect(document, &MainWindow::userVisibleProgressVisibilityChanged, this,
            [this](bool) { updateUserVisibleProgressDockVisibility(); });
    connect(document, &QWidget::windowTitleChanged, this,
            [this, guardedDocument](const QString &) {
              if (guardedDocument == m_displayedProgressDocument)
                updateWorkspaceProgressPresentation();
            });
    connect(document, &QObject::destroyed, this, [this]() {
      for (auto it = m_progressDocuments.begin();
           it != m_progressDocuments.end();) {
        if (it->isNull())
          it = m_progressDocuments.erase(it);
        else
          ++it;
      }
      for (auto it = m_progressSignalDocuments.begin();
           it != m_progressSignalDocuments.end();) {
        if (it->isNull())
          it = m_progressSignalDocuments.erase(it);
        else
          ++it;
      }
      if (!m_displayedProgressDocument)
        m_displayedProgressDocument.clear();
      updateWorkspaceProgressPresentation();
      updateUserVisibleProgressDockVisibility();
    });
  }

  attachUserVisibleProgress(document);
  if (document->hasVisibleTransientProgress())
    m_displayedProgressDocument = document;
  updateWorkspaceProgressPresentation();
}

/** Return progress to DOCUMENT only after its final attached presentation leaves. */
void WorkspaceWindow::detachDocumentProgressIfUnused(MainWindow *document) {
  if (!document || hasAttachedPresentation(document))
    return;

  detachUserVisibleProgress(document);
  QWidget *progress = document->workspaceStatusWidget();
  if (progress && m_workspaceProgressStack->indexOf(progress) >= 0)
    m_workspaceProgressStack->removeWidget(progress);
  document->restoreWorkspaceStatusWidget();

  for (auto it = m_progressDocuments.begin(); it != m_progressDocuments.end();) {
    if (it->isNull() || it->data() == document)
      it = m_progressDocuments.erase(it);
    else
      ++it;
  }
  if (m_displayedProgressDocument == document)
    m_displayedProgressDocument.clear();
  updateWorkspaceProgressPresentation();
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

/** Show one working document in the shared one-line status presentation. */
void WorkspaceWindow::updateWorkspaceProgressPresentation() {
  if (!m_workspaceProgressArea || !m_workspaceProgressStack)
    return;

  QList<MainWindow *> visible;
  for (auto it = m_progressDocuments.begin(); it != m_progressDocuments.end();) {
    if (it->isNull()) {
      it = m_progressDocuments.erase(it);
      continue;
    }
    if ((*it)->hasVisibleTransientProgress())
      visible.append(it->data());
    ++it;
  }

  if (visible.isEmpty()) {
    m_displayedProgressDocument.clear();
    m_workspaceProgressDocumentLabel->clear();
    m_workspaceProgressArea->hide();
    return;
  }

  if (!visible.contains(m_displayedProgressDocument.data()))
    m_displayedProgressDocument = visible.constLast();

  MainWindow *document = m_displayedProgressDocument.data();
  QWidget *progress = document ? document->workspaceStatusWidget() : nullptr;
  if (progress && m_workspaceProgressStack->indexOf(progress) >= 0)
    m_workspaceProgressStack->setCurrentWidget(progress);

  const bool multiple = visible.size() > 1;
  m_workspaceProgressDocumentLabel->setText(
      document ? document->documentDisplayName() : QString());
  m_workspaceProgressDocumentLabel->setVisible(multiple);
  m_workspaceProgressPreviousButton->setVisible(multiple);
  m_workspaceProgressNextButton->setVisible(multiple);
  m_workspaceProgressArea->show();
}

/** Cycle among documents with visible transient progress. */
void WorkspaceWindow::cycleWorkspaceProgress(int offset) {
  QList<MainWindow *> visible;
  for (const QPointer<MainWindow> &document : std::as_const(m_progressDocuments)) {
    if (document && document->hasVisibleTransientProgress())
      visible.append(document.data());
  }
  if (visible.isEmpty())
    return;

  int index = visible.indexOf(m_displayedProgressDocument.data());
  if (index < 0)
    index = 0;
  index = (index + offset) % visible.size();
  if (index < 0)
    index += visible.size();
  m_displayedProgressDocument = visible[index];
  updateWorkspaceProgressPresentation();
}

/** Return keyboard focus from CONTROL in the global task strip to the current
    image without allowing focus fallback to select another MDI child. */
bool WorkspaceWindow::restoreFocusFromTaskControl(QWidget *control) {
  if (!control || !m_mdiArea || !m_userVisibleProgressStack ||
      (control != m_userVisibleProgressStack &&
       !m_userVisibleProgressStack->isAncestorOf(control)))
    return false;

  // currentSubWindow() is the MDI presentation the user selected. Prefer it
  // over Qt's focus-derived activeSubWindow(), which may already be changing
  // while focus moves out of the task strip.
  QMdiSubWindow *current = m_mdiArea->currentSubWindow();
  if (!current)
    current = m_mdiArea->activeSubWindow();
  if (!current)
    return false;

  ImageWidget *image = nullptr;
  if (MainWindow *document = documentForSubWindow(current))
    image = document->primaryImageWidget();
  else if (ImageViewWindow *view = viewForSubWindow(current))
    image = view->imageWidget();

  if (image) {
    image->setFocus(Qt::OtherFocusReason);
    return true;
  }
  if (QWidget *hosted = current->widget()) {
    hosted->setFocus(Qt::OtherFocusReason);
    return true;
  }
  return false;
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

/** Move TOOLBAR into the one permanent outer toolbar slot. */
void WorkspaceWindow::installWorkspaceToolBar(QMainWindow *owner,
                                               QToolBar *toolbar) {
  if (!owner || !toolbar || !m_workspaceToolBar ||
      !m_workspaceToolBarHost || !m_workspaceToolBarLayout)
    return;

  if (toolbar->parentWidget() == m_workspaceToolBarHost) {
    toolbar->show();
    m_workspaceToolBar->show();
    return;
  }

  // This handoff runs only after the child and workspace have completed their
  // first show/layout.  Removing the child toolbar at that point was already
  // proven stable by #267; unlike the old model, the outer QMainWindow never
  // adds or removes a QToolBar here.
  if (auto *currentOwner = qobject_cast<QMainWindow *>(toolbar->parentWidget())) {
    if (currentOwner->toolBarArea(toolbar) != Qt::NoToolBarArea)
      currentOwner->removeToolBar(toolbar);
  }

  toolbar->hide();
  toolbar->setParent(m_workspaceToolBarHost);
  m_workspaceToolBarLayout->addWidget(toolbar);
  toolbar->show();
  m_workspaceToolBar->show();
}

/** Return TOOLBAR to OWNER while leaving the outer toolbar registered. */
void WorkspaceWindow::releaseWorkspaceToolBar(QMainWindow *owner,
                                               QToolBar *toolbar,
                                               bool showInWindow) {
  if (!owner || !toolbar)
    return;

  if (m_workspaceToolBarLayout && m_workspaceToolBarHost &&
      toolbar->parentWidget() == m_workspaceToolBarHost) {
    m_workspaceToolBarLayout->removeWidget(toolbar);
  } else if (!showInWindow && toolbar->parentWidget() == owner &&
             owner->toolBarArea(toolbar) != Qt::NoToolBarArea) {
    // This only occurs after the child's first layout (for example if chrome
    // is released before it was ever borrowed).  Keep inactive MDI children
    // free of toolbar-area ownership once that initial layout has completed.
    owner->removeToolBar(toolbar);
  }

  toolbar->hide();
  if (toolbar->parentWidget() != owner)
    toolbar->setParent(owner);

  if (showInWindow) {
    if (owner->toolBarArea(toolbar) == Qt::NoToolBarArea)
      owner->addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->show();
  }

  if (m_workspaceToolBar && m_workspaceToolBarLayout &&
      m_workspaceToolBarLayout->count() == 0)
    m_workspaceToolBar->hide();
}

/** Present DOCUMENT's menus, toolbar, inspector, and transient status row. */
void WorkspaceWindow::installDocumentChrome(MainWindow *document) {
  if (!document)
    return;

  menuBar()->clear();
  for (QAction *action : document->menuBar()->actions())
    menuBar()->addAction(action);

  if (QToolBar *toolbar = document->workspaceToolBar())
    installWorkspaceToolBar(document, toolbar);

  installDocumentInspector(document, document->primaryImageWidget());

  document->refreshWindowMenu();
  setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
}

/** Remove DOCUMENT's shared chrome and optionally show it in its own window. */
void WorkspaceWindow::releaseDocumentChrome(MainWindow *document,
                                             bool showInWindow) {
  if (!document)
    return;

  if (QToolBar *toolbar = document->workspaceToolBar())
    releaseWorkspaceToolBar(document, toolbar, showInWindow);

  document->standaloneStatusBar()->setVisible(showInWindow);
  if (showInWindow)
    document->setWorkspaceStatusBar(nullptr);

  document->menuBar()->setVisible(showInWindow);
  if (m_chromeDocument == document) {
    if (showInWindow)
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

  if (QToolBar *toolbar = view->workspaceToolBar())
    installWorkspaceToolBar(view, toolbar);

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
  setWindowTitle(view->windowTitle() + tr(" — Color-Screen"));
}

/** Return VIEW's toolbar/menu/status presentation to its own window. */
void WorkspaceWindow::releaseViewChrome(ImageViewWindow *view,
                                        bool showInWindow) {
  if (!view)
    return;

  if (QToolBar *toolbar = view->workspaceToolBar())
    releaseWorkspaceToolBar(view, toolbar, showInWindow);

  view->menuBar()->setVisible(showInWindow);
  view->standaloneStatusBar()->setVisible(showInWindow);
  if (showInWindow)
    view->setWorkspaceStatusBar(nullptr);
  if (m_chromeView == view) {
    if (showInWindow)
      statusBar()->clearMessage();
    menuBar()->clear();
    m_inspectorDock->setWindowTitle(tr("Document Controls"));
    m_chromeView.clear();
  }
}

/** Switch the shared shell to WINDOW's document or secondary view. */
void WorkspaceWindow::onSubWindowActivated(QMdiSubWindow *window) {
  // QMdiArea emits this signal from addSubWindow()/setActiveSubWindow(). During
  // insertion the child QMainWindow has not necessarily completed show/layout,
  // so moving its QToolBar yet can leave Qt's toolbar-area bookkeeping stale.
  if (m_chromeActivationBlockDepth > 0)
    return;

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
    if (m_workspaceToolBar)
      m_workspaceToolBar->hide();
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

  releaseDocumentChrome(document, false);
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
  detachDocumentProgressIfUnused(document);

  document->setParent(nullptr);
  document->setWindowFlags(Qt::Window);
  // Only after the document is top-level again may its private status bar and
  // toolbar-area ownership be restored.
  document->setWorkspaceStatusBar(nullptr);
  if (QToolBar *toolbar = document->workspaceToolBar()) {
    if (document->toolBarArea(toolbar) == Qt::NoToolBarArea)
      document->addToolBar(Qt::TopToolBarArea, toolbar);
  }
}

/** Remove secondary VIEW's MDI wrapper while keeping VIEW alive. */
void WorkspaceWindow::takeViewFromWorkspace(ImageViewWindow *view) {
  MainWindow *sourceDocument = view ? view->sourceDocument() : nullptr;
  QMdiSubWindow *subWindow = subWindowForView(view);
  if (!subWindow)
    return;

  releaseViewChrome(view, false);
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
  detachDocumentProgressIfUnused(sourceDocument);

  view->setParent(nullptr);
  view->setWindowFlags(Qt::Window);
  // Match document detach ordering: only restore private chrome ownership
  // after the view has left the MDI hierarchy.
  view->setWorkspaceStatusBar(nullptr);
  if (QToolBar *toolbar = view->workspaceToolBar()) {
    if (view->toolBarArea(toolbar) == Qt::NoToolBarArea)
      view->addToolBar(Qt::TopToolBarArea, toolbar);
  }
  view->setAttribute(Qt::WA_DeleteOnClose, true);
}

/** Detach a tab when it is dragged beyond the tab-bar docking margin. */
bool WorkspaceWindow::eventFilter(QObject *watched, QEvent *event) {
  // QMdiArea destroys and recreates its private QTabBar when switching view
  // modes. During QWidget destruction the event filter can still run after
  // the object's dynamic type is no longer QTabBar. Track only QObject
  // identity in m_tabBar and cast the object being filtered while it is live.
  auto *tabBar = qobject_cast<QTabBar *>(watched);
  if (!tabBar || watched != m_tabBar.data())
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

  QList<QPointer<MainWindow>> preparedDocuments;
  auto cancelPreparedCloses = [&preparedDocuments]() {
    for (const QPointer<MainWindow> &document : preparedDocuments) {
      if (document)
        document->cancelPreparedApplicationClose();
    }
  };

  // Resolve every close veto before removing any tab.  A document that still
  // has a detached peer view is not actually leaving the application, so keep
  // the historical behavior of deferring its save policy until that final
  // presentation closes.
  for (const QPointer<MainWindow> &document : documents) {
    if (!document)
      continue;

    bool hasExternalView = false;
    if (application) {
      for (ImageViewWindow *view : application->viewWindows()) {
        if (view && view->sourceDocument() == document && !containsView(view)) {
          hasExternalView = true;
          break;
        }
      }
    }
    if (!hasExternalView && !document->prepareForApplicationClose()) {
      cancelPreparedCloses();
      m_closing = false;
      event->ignore();
      return;
    }
    if (!hasExternalView)
      preparedDocuments.append(document);
  }

  // Close secondary presentations only after every document that will really
  // leave has approved closure.  This keeps Cancel transactional for the
  // workspace just as it is for File -> Exit.
  for (const QPointer<ImageViewWindow> &view : views) {
    if (view && !closeView(view)) {
      cancelPreparedCloses();
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
      cancelPreparedCloses();
      m_closing = false;
      event->ignore();
      return;
    }
  }

  saveWorkspaceGeometry();
  m_closing = false;
  event->accept();
}
