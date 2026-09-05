#pragma once

#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QPointer>
#include <QShowEvent>
#include <QStatusBar>

class MainWindow;
class ImageViewWindow;
class QCloseEvent;
class QDockWidget;
class QEvent;
class ImageWidget;
class QMdiArea;
class QMdiSubWindow;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QToolBar;
class QToolButton;
class QTabBar;
class QVBoxLayout;
class QWidget;

/** Top-level multiple-document workspace for Color-Screen.

    MainWindow remains the complete owner of one independent image document.
    This shell uses Qt's QMdiArea to provide the conventional image-editor
    presentation: tabs by default, MDI subwindows for tile/cascade layouts, and
    explicit detachment into normal top-level windows.  The active document
    supplies the shared menu bar, toolbar, and inspector so document tabs appear
    directly below the toolbar; the shell owns one status bar shared by every
    attached tab. */
class WorkspaceWindow final : public QMainWindow {
public:
  explicit WorkspaceWindow(QWidget *parent = nullptr);

  /** Stop descendant callbacks before derived workspace state is destroyed. */
  ~WorkspaceWindow() override {
    m_closing = true;
    if (m_tabBar)
      m_tabBar->removeEventFilter(this);
    const QObjectList descendants = findChildren<QObject *>();
    for (QObject *object : descendants)
      QObject::disconnect(object, nullptr, this, nullptr);
  }

  /** Add DOCUMENT to the workspace and make it active. */
  void addDocument(MainWindow *document);

  /** Remove DOCUMENT from the workspace without closing it. */
  void removeDocument(MainWindow *document);

  /** Add a secondary VIEW as another MDI tab/subwindow. */
  void addView(ImageViewWindow *view);

  /** Remove VIEW from the workspace without closing it. */
  void removeView(ImageViewWindow *view);

  /** Return the active attached document, or nullptr when none is active. */
  MainWindow *currentDocument() const;

  /** Return the number of documents attached to this workspace. */
  int tabCount() const;

  /** Return true when DOCUMENT is managed by this workspace. */
  bool containsDocument(MainWindow *document) const;

  /** Return true when VIEW is managed by this workspace. */
  bool containsView(ImageViewWindow *view) const;

  /** Activate DOCUMENT when it is attached. */
  void activateDocument(MainWindow *document);

  /** Activate VIEW when it is attached. */
  void activateView(ImageViewWindow *view);

  /** Refresh DOCUMENT's MDI title, tab text, and shared window title. */
  void refreshDocument(MainWindow *document);

  /** Return all workspace-hosted UI to DOCUMENT before it closes. */
  void prepareDocumentForClose(MainWindow *document);

  /** Return shared chrome and the MDI wrapper to VIEW before it closes. */
  void prepareViewForClose(ImageViewWindow *view);

  /** Close VIEW through its owning QMdiSubWindow. */
  bool closeView(ImageViewWindow *view);

  /** Consolidate attached documents into the default tabbed presentation. */
  void showTabbedDocuments();

  /** Arrange every attached document as equally sized MDI tiles. */
  void tileDocuments();

  /** Arrange every attached document as cascading MDI subwindows. */
  void cascadeDocuments();

  /** Return true when the workspace currently uses document tabs. */
  bool isTabbedView() const;

  /** Return whether Qt's standard document tab bar is currently visible. */
  bool isTabBarVisible() const;

  /** Move focus from CONTROL in the global task strip back to the currently
      active image presentation without changing the active MDI child. */
  bool restoreFocusFromTaskControl(QWidget *control);

  /** Return the document whose transient progress the shell displays. */
  MainWindow *displayedProgressDocument() const {
    return m_displayedProgressDocument.data();
  }

  /** Restore/save only the outer workspace geometry. */
  void restoreWorkspaceGeometry();
  void saveWorkspaceGeometry() const;

protected:
  /** Close only presentations hosted by this workspace. */
  void closeEvent(QCloseEvent *event) override;

  /** Reclaim the active document inspector when the workspace gets focus. */
  void changeEvent(QEvent *event) override;

  /** Implement drag-out tab detachment while preserving ordinary tab moves. */
  bool eventFilter(QObject *watched, QEvent *event) override;

  /** Keep the one shared status line at a stable one-row height.

      Transient progress pages are permanently owned by the workspace and tab
      activation never adds or removes them. */
  void showEvent(QShowEvent *event) override {
    QMainWindow::showEvent(event);
    QStatusBar *bar = QMainWindow::statusBar();
    if (!bar)
      return;
    const int stableHeight = qMax(bar->height(), bar->sizeHint().height());
    if (stableHeight > bar->minimumHeight())
      bar->setMinimumHeight(stableHeight);
  }

private:
  /** Return the MDI wrapper for DOCUMENT, or nullptr when detached. */
  QMdiSubWindow *subWindowForDocument(MainWindow *document) const;

  /** Return the MDI wrapper for VIEW, or nullptr when detached. */
  QMdiSubWindow *subWindowForView(ImageViewWindow *view) const;

  /** Return the document contained in WINDOW. */
  MainWindow *documentForSubWindow(QMdiSubWindow *window) const;

  /** Return the secondary view contained in WINDOW. */
  ImageViewWindow *viewForSubWindow(QMdiSubWindow *window) const;

  /** Return the internal QMdiArea tab bar in tabbed view. */
  QTabBar *documentTabBar() const;

  /** Return the hosted widget represented by tab INDEX and activate it. */
  QWidget *windowAtTab(int index) const;

  /** Apply tab behavior that QMdiArea does not expose directly. */
  void configureTabBar();

  /** Close the shell after its final hosted presentation has left. */
  void scheduleCloseIfEmpty();

  /** Attach both transient and long-running progress for DOCUMENT. */
  void attachDocumentProgress(MainWindow *document);

  /** Detach progress only after DOCUMENT has no workspace presentation. */
  void detachDocumentProgressIfUnused(MainWindow *document);

  /** Return whether DOCUMENT still has a primary or secondary MDI view. */
  bool hasAttachedPresentation(MainWindow *document) const;

  /** Keep DOCUMENT's user-visible long tasks in the global task strip. */
  void attachUserVisibleProgress(MainWindow *document);

  /** Return DOCUMENT's user-visible rows before detaching it. */
  void detachUserVisibleProgress(MainWindow *document);

  /** Show the task-progress dock iff any attached document has visible rows. */
  void updateUserVisibleProgressDockVisibility();

  /** Select one visible transient document without following tab changes. */
  void updateWorkspaceProgressPresentation();
  void cycleWorkspaceProgress(int offset);

  /** Put DOCUMENT's shared inspector in the workspace and target IMAGEWIDGET. */
  void installDocumentInspector(MainWindow *document, ImageWidget *imageWidget);

  /** Move TOOLBAR from OWNER into the permanent workspace toolbar slot. */
  void installWorkspaceToolBar(QMainWindow *owner, QToolBar *toolbar);

  /** Return TOOLBAR to OWNER without changing the workspace toolbar topology. */
  void releaseWorkspaceToolBar(QMainWindow *owner, QToolBar *toolbar,
                               bool showInWindow);

  /** Show DOCUMENT's menus, toolbar, and inspector as active chrome. */
  void installDocumentChrome(MainWindow *document);

  /** Return DOCUMENT's toolbar/menu visibility to its own window.

      SHOWINWINDOW is true when the document is becoming detached or closing,
      and false when it remains an inactive MDI child. */
  void releaseDocumentChrome(MainWindow *document, bool showInWindow);

  /** Detach DOCUMENT into a top-level window through the application manager. */
  void detachDocument(MainWindow *document);

  /** Detach VIEW into a top-level window through the application manager. */
  void detachView(ImageViewWindow *view);

  /** Show VIEW's menu and toolbar in the shared shell. */
  void installViewChrome(ImageViewWindow *view);

  /** Return VIEW's shared chrome to its own window. */
  void releaseViewChrome(ImageViewWindow *view, bool showInWindow);

  /** Refresh shared chrome after the active MDI subwindow changes. */
  void onSubWindowActivated(QMdiSubWindow *window);

  /** Remove DOCUMENT's MDI wrapper and inspector without showing it. */
  void takeDocumentFromWorkspace(MainWindow *document);

  /** Remove VIEW's MDI wrapper without closing the view. */
  void takeViewFromWorkspace(ImageViewWindow *view);

  QMdiArea *m_mdiArea = nullptr;
  QToolBar *m_workspaceToolBar = nullptr;
  QWidget *m_workspaceToolBarHost = nullptr;
  QHBoxLayout *m_workspaceToolBarLayout = nullptr;
  QDockWidget *m_inspectorDock = nullptr;
  QStackedWidget *m_inspectorStack = nullptr;
  QWidget *m_workspaceProgressArea = nullptr;
  QHBoxLayout *m_workspaceProgressLayout = nullptr;
  QStackedWidget *m_workspaceProgressStack = nullptr;
  QLabel *m_workspaceProgressDocumentLabel = nullptr;
  QToolButton *m_workspaceProgressPreviousButton = nullptr;
  QToolButton *m_workspaceProgressNextButton = nullptr;
  QList<QPointer<MainWindow>> m_progressDocuments;
  QList<QPointer<MainWindow>> m_progressSignalDocuments;
  QPointer<MainWindow> m_displayedProgressDocument;
  QWidget *m_userVisibleProgressStack = nullptr;
  QVBoxLayout *m_userVisibleProgressLayout = nullptr;
  QDockWidget *m_userVisibleProgressDock = nullptr;
  QPointer<MainWindow> m_chromeDocument;
  QPointer<ImageViewWindow> m_chromeView;
  QPointer<QObject> m_tabBar;
  QPointer<QWidget> m_dragWindow;
  QPoint m_dragStartGlobal;
  // QMdiArea can emit subWindowActivated synchronously while a child is still
  // being inserted. Delay shared-chrome handoff until that child is shown.
  int m_chromeActivationBlockDepth = 0;
  bool m_closing = false;
};
