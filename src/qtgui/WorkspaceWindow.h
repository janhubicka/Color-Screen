#pragma once

#include <QMainWindow>
#include <QPointer>

class MainWindow;
class QCloseEvent;
class QTabWidget;

/** Top-level tabbed workspace for Color-Screen image documents.

    MainWindow remains the complete owner of one independent document.  This
    shell only controls presentation: by default documents are embedded as
    movable tabs, while any tab can be detached into a normal top-level
    MainWindow and later reattached without copying document state.  */
class WorkspaceWindow final : public QMainWindow {
public:
  explicit WorkspaceWindow(QWidget *parent = nullptr);

  /** Add DOCUMENT as a tab and make it current. */
  void addDocument(MainWindow *document);

  /** Remove DOCUMENT from the tab bar without closing it. */
  void removeDocument(MainWindow *document);

  /** Return the active tab document, or nullptr when there is no tab. */
  MainWindow *currentDocument() const;

  /** Return the number of attached document tabs. */
  int tabCount() const;

  /** Return true when DOCUMENT is currently attached to this workspace. */
  bool containsDocument(MainWindow *document) const;

  /** Select DOCUMENT when it is attached. */
  void activateDocument(MainWindow *document);

  /** Refresh DOCUMENT's tab text and the workspace title. */
  void refreshDocument(MainWindow *document);

  /** Restore/save only the outer workspace geometry. */
  void restoreWorkspaceGeometry();
  void saveWorkspaceGeometry() const;

protected:
  /** Close all documents through their normal save/cancel policy. */
  void closeEvent(QCloseEvent *event) override;

private:
  /** Request normal document closure for tab INDEX. */
  void closeTab(int index);

  /** Detach the document at INDEX into a top-level window. */
  void detachTab(int index);

  QTabWidget *m_tabs = nullptr;
  bool m_closing = false;
};
