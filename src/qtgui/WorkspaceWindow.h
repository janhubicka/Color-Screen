#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QPointer>

class MainWindow;
class QCloseEvent;
class QDockWidget;
class QEvent;
class QMdiArea;
class QMdiSubWindow;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;
class QWidget;

/** Top-level multiple-document workspace for Color-Screen.

    MainWindow remains the complete owner of one independent image document.
    This shell uses Qt's QMdiArea to provide the conventional image-editor
    presentation: tabs by default, MDI subwindows for tile/cascade layouts, and
    explicit detachment into normal top-level windows.  The active document
    supplies the shared menu bar, toolbar, and inspector so document tabs appear
    directly below the toolbar. */
class WorkspaceWindow final : public QMainWindow {
public:
  explicit WorkspaceWindow(QWidget *parent = nullptr);

  /** Add DOCUMENT to the workspace and make it active. */
  void addDocument(MainWindow *document);

  /** Remove DOCUMENT from the workspace without closing it. */
  void removeDocument(MainWindow *document);

  /** Return the active attached document, or nullptr when none is active. */
  MainWindow *currentDocument() const;

  /** Return the number of documents attached to this workspace. */
  int tabCount() const;

  /** Return true when DOCUMENT is managed by this workspace. */
  bool containsDocument(MainWindow *document) const;

  /** Activate DOCUMENT when it is attached. */
  void activateDocument(MainWindow *document);

  /** Refresh DOCUMENT's MDI title, tab text, and shared window title. */
  void refreshDocument(MainWindow *document);

  /** Return all workspace-hosted UI to DOCUMENT before it closes. */
  void prepareDocumentForClose(MainWindow *document);

  /** Consolidate attached documents into the default tabbed presentation. */
  void showTabbedDocuments();

  /** Arrange every attached document as equally sized MDI tiles. */
  void tileDocuments();

  /** Arrange every attached document as cascading MDI subwindows. */
  void cascadeDocuments();

  /** Return true when the workspace currently uses document tabs. */
  bool isTabbedView() const;

  /** Return whether the auto-hiding document tab bar is currently visible. */
  bool isTabBarVisible() const;

  /** Restore/save only the outer workspace geometry. */
  void restoreWorkspaceGeometry();
  void saveWorkspaceGeometry() const;

protected:
  /** Close all documents through their normal save/cancel policy. */
  void closeEvent(QCloseEvent *event) override;

  /** Implement drag-out tab detachment while preserving ordinary tab moves. */
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  /** Return the MDI wrapper for DOCUMENT, or nullptr when detached. */
  QMdiSubWindow *subWindowForDocument(MainWindow *document) const;

  /** Return the document contained in WINDOW. */
  MainWindow *documentForSubWindow(QMdiSubWindow *window) const;

  /** Return the internal QMdiArea tab bar in tabbed view. */
  QTabBar *documentTabBar() const;

  /** Activate and return the document represented by tab INDEX. */
  MainWindow *documentAtTab(int index) const;

  /** Apply tab behavior that QMdiArea does not expose directly. */
  void configureTabBar();

  /** Keep DOCUMENT's user-visible long tasks in the global status area. */
  void attachUserVisibleProgress(MainWindow *document);

  /** Return DOCUMENT's user-visible rows before detaching it. */
  void detachUserVisibleProgress(MainWindow *document);

  /** Show the task-progress dock iff any attached document has visible rows. */
  void updateUserVisibleProgressDockVisibility();

  /** Show DOCUMENT's menus, toolbar, inspector, and transient status as the
      active workspace chrome. */
  void installDocumentChrome(MainWindow *document);

  /** Return DOCUMENT's toolbar/menu visibility to its own window.

      SHOWINWINDOW is true when the document is becoming detached or closing,
      and false when it remains an inactive MDI child. */
  void releaseDocumentChrome(MainWindow *document, bool showInWindow);

  /** Detach DOCUMENT into a top-level window through the application manager. */
  void detachDocument(MainWindow *document);

  /** Refresh shared chrome after the active MDI subwindow changes. */
  void onSubWindowActivated(QMdiSubWindow *window);

  /** Remove DOCUMENT's MDI wrapper and inspector without showing it. */
  void takeDocumentFromWorkspace(MainWindow *document);

  QMdiArea *m_mdiArea = nullptr;
  QDockWidget *m_inspectorDock = nullptr;
  QStackedWidget *m_inspectorStack = nullptr;
  QWidget *m_workspaceProgressArea = nullptr;
  QVBoxLayout *m_workspaceProgressLayout = nullptr;
  QWidget *m_userVisibleProgressStack = nullptr;
  QVBoxLayout *m_userVisibleProgressLayout = nullptr;
  QDockWidget *m_userVisibleProgressDock = nullptr;
  QPointer<MainWindow> m_chromeDocument;
  QPointer<QTabBar> m_tabBar;
  QPointer<MainWindow> m_dragDocument;
  QPoint m_dragStartGlobal;
  bool m_closing = false;
};
