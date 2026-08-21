#include "WorkspaceWindow.h"

#include "ColorScreenApplication.h"
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
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
  m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
  m_mdiArea->setDocumentMode(true);
  m_mdiArea->setTabsClosable(true);
  m_mdiArea->setTabsMovable(true);
  m_mdiArea->setTabPosition(QTabWidget::North);
  m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, false);
  m_mdiArea->setViewMode(QMdiArea::TabbedView);
  setCentralWidget(m_mdiArea);

  statusBar()->setObjectName(QStringLiteral("WorkspaceStatusBar"));

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
            if (guardedSubWindow) {
              m_mdiArea->removeSubWindow(guardedSubWindow);
              guardedSubWindow->deleteLater();
            }
            QTimer::singleShot(0, this, [this]() {
              onSubWindowActivated(m_mdiArea->currentSubWindow());
              configureTabBar();
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
}

/** Return the active MDI document while toolbar interactions retain selection. */
MainWindow *WorkspaceWindow::currentDocument() const {
  QMdiSubWindow *window = m_mdiArea->currentSubWindow();
  if (!window)
    window = m_mdiArea->activeSubWindow();
  return documentForSubWindow(window);
}

/** Return the number of documents attached to the MDI workspace. */
int WorkspaceWindow::tabCount() const {
  return m_mdiArea->subWindowList().size();
}

/** Return whether DOCUMENT is attached to this workspace. */
bool WorkspaceWindow::containsDocument(MainWindow *document) const {
  return subWindowForDocument(document) != nullptr;
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

/** Restore workspace-owned widgets before DOCUMENT's destructor runs. */
void WorkspaceWindow::prepareDocumentForClose(MainWindow *document) {
  if (!document || !containsDocument(document))
    return;

  takeDocumentFromWorkspace(document);
  document->restoreFromWorkspaceEmbedding();
  onSubWindowActivated(m_mdiArea->currentSubWindow());
  configureTabBar();
}

/** Present attached documents as tabs and hide the bar for a single image. */
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
  m_mdiArea->setViewMode(QMdiArea::SubWindowView);
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList())
    subWindow->showNormal();
  m_mdiArea->tileSubWindows();
}

/** Present attached documents as cascading MDI subwindows. */
void WorkspaceWindow::cascadeDocuments() {
  m_mdiArea->setViewMode(QMdiArea::SubWindowView);
  for (QMdiSubWindow *subWindow : m_mdiArea->subWindowList())
    subWindow->showNormal();
  m_mdiArea->cascadeSubWindows();
}

/** Return whether Qt's tabbed MDI presentation is active. */
bool WorkspaceWindow::isTabbedView() const {
  return m_mdiArea->viewMode() == QMdiArea::TabbedView;
}

/** Return whether the auto-hiding document tab bar is currently visible. */
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

/** Return the MainWindow hosted by WINDOW. */
MainWindow *
WorkspaceWindow::documentForSubWindow(QMdiSubWindow *window) const {
  return window ? qobject_cast<MainWindow *>(window->widget()) : nullptr;
}

/** Return Qt's internal MDI tab bar, when tabbed view has created one. */
QTabBar *WorkspaceWindow::documentTabBar() const {
  return m_mdiArea->findChild<QTabBar *>(
      QString(), Qt::FindDirectChildrenOnly);
}

/** Activate and return the document represented by tab INDEX. */
MainWindow *WorkspaceWindow::documentAtTab(int index) const {
  QTabBar *tabBar = documentTabBar();
  if (!tabBar || index < 0 || index >= tabBar->count())
    return nullptr;

  tabBar->setCurrentIndex(index);
  return currentDocument();
}

/** Configure auto-hiding tabs, context actions, and drag-out detachment. */
void WorkspaceWindow::configureTabBar() {
  QTimer::singleShot(0, this, [this]() {
    QTabBar *tabBar = documentTabBar();
    if (!tabBar)
      return;

    tabBar->setAutoHide(true);
    tabBar->setDocumentMode(true);
    tabBar->setMovable(true);
    tabBar->setTabsClosable(true);
    tabBar->setElideMode(Qt::ElideMiddle);
    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

    if (m_tabBar != tabBar) {
      if (m_tabBar)
        m_tabBar->removeEventFilter(this);
      m_tabBar = tabBar;
      m_tabBar->installEventFilter(this);
    }

    if (tabBar->property("colorscreenConfigured").toBool())
      return;
    tabBar->setProperty("colorscreenConfigured", true);

    connect(tabBar, &QWidget::customContextMenuRequested, this,
            [this, tabBar](const QPoint &position) {
              MainWindow *document = documentAtTab(tabBar->tabAt(position));
              if (!document)
                return;

              QMenu menu(this);
              QAction *detach = menu.addAction(tr("Detach Image"));
              menu.addSeparator();
              QAction *tabbed = menu.addAction(tr("Tabbed Documents"));
              QAction *tile = menu.addAction(tr("Tile Documents"));
              QAction *cascade = menu.addAction(tr("Cascade Documents"));
              QAction *selected = menu.exec(tabBar->mapToGlobal(position));
              if (selected == detach)
                detachDocument(document);
              else if (selected == tabbed)
                showTabbedDocuments();
              else if (selected == tile)
                tileDocuments();
              else if (selected == cascade)
                cascadeDocuments();
            });

    connect(tabBar, &QTabBar::tabBarDoubleClicked, this,
            [this](int index) {
              if (MainWindow *document = documentAtTab(index))
                detachDocument(document);
            });
  });
}

/** Present DOCUMENT's menus, toolbar, and inspector in the shared shell. */
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

  if (QWidget *inspector = document->workspaceInspectorWidget()) {
    if (m_inspectorStack->indexOf(inspector) < 0)
      m_inspectorStack->addWidget(inspector);
    m_inspectorStack->setCurrentWidget(inspector);
    inspector->show();
    m_inspectorDock->show();
  } else {
    m_inspectorDock->hide();
  }

  if (QWidget *statusWidget = document->workspaceStatusWidget()) {
    if (statusWidget->parentWidget() != statusBar()) {
      const bool explicitlyHidden = statusWidget->isHidden();
      document->statusBar()->removeWidget(statusWidget);
      statusBar()->addPermanentWidget(statusWidget);
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
    if (statusWidget->parentWidget() == statusBar()) {
      const bool explicitlyHidden = statusWidget->isHidden();
      statusBar()->removeWidget(statusWidget);
      document->statusBar()->addPermanentWidget(statusWidget);
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

/** Switch the shared shell to WINDOW's document. */
void WorkspaceWindow::onSubWindowActivated(QMdiSubWindow *window) {
  MainWindow *document = documentForSubWindow(window);
  if (m_chromeDocument == document) {
    if (document)
      setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
    return;
  }

  if (m_chromeDocument)
    releaseDocumentChrome(m_chromeDocument, false);

  m_chromeDocument = document;
  if (document) {
    installDocumentChrome(document);
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

  if (QWidget *inspector = document->workspaceInspectorWidget()) {
    m_inspectorStack->removeWidget(inspector);
    inspector->hide();
    inspector->setParent(nullptr);
  }

  document->hide();
  m_mdiArea->removeSubWindow(document);
  m_mdiArea->removeSubWindow(subWindow);
  subWindow->deleteLater();

  document->setParent(nullptr);
  document->setWindowFlags(Qt::Window);
}

/** Detach a tab when it is dragged beyond the tab-bar docking margin. */
bool WorkspaceWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched != m_tabBar || !m_tabBar)
    return QMainWindow::eventFilter(watched, event);

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      m_dragDocument =
          documentAtTab(m_tabBar->tabAt(mouseEvent->position().toPoint()));
      m_dragStartGlobal = mouseEvent->globalPosition().toPoint();
    }
    break;
  }
  case QEvent::MouseMove: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (!m_dragDocument || !(mouseEvent->buttons() & Qt::LeftButton))
      break;

    const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
    if ((globalPosition - m_dragStartGlobal).manhattanLength() <
        QApplication::startDragDistance())
      break;

    const QPoint localPosition = m_tabBar->mapFromGlobal(globalPosition);
    const QRect dockingMargin = m_tabBar->rect().adjusted(-24, -32, 24, 32);
    if (dockingMargin.contains(localPosition))
      break;

    QPointer<MainWindow> document = m_dragDocument;
    m_dragDocument.clear();
    QTimer::singleShot(0, this, [this, document]() {
      if (document)
        detachDocument(document);
    });
    return true;
  }
  case QEvent::MouseButtonRelease:
    m_dragDocument.clear();
    break;
  default:
    break;
  }

  return QMainWindow::eventFilter(watched, event);
}

/** Close the whole session, respecting every document's close veto. */
void WorkspaceWindow::closeEvent(QCloseEvent *event) {
  if (m_closing) {
    event->accept();
    return;
  }

  m_closing = true;
  bool accepted = true;
  if (ColorScreenApplication *application = documentApplication())
    accepted = application->closeAllDocumentsForWorkspace();
  if (!accepted) {
    m_closing = false;
    event->ignore();
    return;
  }

  saveWorkspaceGeometry();
  event->accept();
}
