#pragma once

#include <QApplication>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class MainWindow;
class ImageViewWindow;
class QEvent;
class QMenu;
class QWidget;
class WorkspaceWindow;

/** Application-level owner of Color-Screen document windows.

    A MainWindow is one complete, independent image document: it owns the scan,
    parameter state, undo stack, background workers, progress reporting, and
    recovery data.  ColorScreenApplication coordinates those windows without
    sharing mutable document state between them.  */
class ColorScreenApplication final : public QApplication {
public:
  /** Construct the Qt application and initialize document-window tracking. */
  ColorScreenApplication(int &argc, char **argv);

  /** Create, show, and register an empty document window.

      RECOVERYDIRECTORY identifies the per-document crash-recovery directory.
      When it is empty, a new unique directory is assigned.  */
  MainWindow *createDocumentWindow(
      const QString &recoveryDirectory = QString(), bool detached = false);

  /** Create a lightweight secondary view of SOURCE.

      The source MainWindow remains the sole editable document owner; the new
      view shares its image and follows document parameter changes while
      keeping render mode, zoom, and pan independent.  New views attach to the
      primary workspace by default. */
  ImageViewWindow *createViewWindow(MainWindow *source, bool detached = false);

  /** Create a specialized view of REFERENCEFILE for measuring scanner MTF.

      SOURCE remains the owner of the parameter/undo/recovery state; only the
      displayed scan belongs to the reference view. */
  ImageViewWindow *createSlantedEdgeReference(MainWindow *source,
                                              const QString &referenceFile,
                                              bool detached = false);

  /** Prompt for a slanted-edge reference image and open it for SOURCE. */
  ImageViewWindow *openSlantedEdgeReference(MainWindow *source,
                                            QWidget *dialogParent = nullptr);

  /** Move VIEW from the workspace into a standalone top-level window. */
  void detachView(ImageViewWindow *view);

  /** Move VIEW back into the primary workspace without recreating it. */
  void attachView(ImageViewWindow *view);

  /** Attach every detached secondary view to the primary workspace. */
  void attachAllViews();

  /** Close VIEW through its MDI wrapper when attached, or directly otherwise. */
  bool closeView(ImageViewWindow *view);

  /** Return true when DOCUMENT's original MainWindow presentation is still open. */
  bool isDocumentPresentationOpen(MainWindow *document) const;

  /** Close only DOCUMENT's original presentation while peer views remain.

      Returns true when the close event must be ignored because DOCUMENT has been
      retained as the hidden state owner of one or more secondary views. */
  bool retainDocumentForOpenViews(MainWindow *document);

  /** Approve closing VIEW and close its hidden document owner when it is last. */
  bool requestViewClose(ImageViewWindow *view);

  /** Return all live secondary views. */
  QList<ImageViewWindow *> viewWindows();

  /** Reload every slanted-edge reference associated with SOURCE using the
      document's current demosaic setting. */
  void reloadSlantedEdgeReferences(MainWindow *source);

  /** Recreate SOURCE's slanted-edge references from crash-recovery metadata.

      Restored references are attached to the workspace; ordinary New Views and
      view-local presentation state are intentionally not recovered.  Returns
      the number of reference views recreated, or -1 for unreadable metadata. */
  int restoreSlantedEdgeReferencesFromRecovery(MainWindow *source);

  /** Open every path in FILENAMES as an independent image document.

      PREFERREDWINDOW is reused for the first file only when it is still an
      untouched empty window; every remaining file receives a new MainWindow.
      SUPPRESSPARAMPROMPT is used by crash recovery and automated smoke tests to avoid interactive post-load prompts.  */
  void openFiles(const QStringList &fileNames,
                 MainWindow *preferredWindow = nullptr,
                 bool suppressParamPrompt = false);

  /** Restore all documents left by an unclean shutdown.

      The user is prompted once for the whole recovered session.  Returns true
      when at least one recovery window was created.  */
  bool restoreRecoverySession();

  /** Return the currently live documents in creation order. */
  QList<MainWindow *> documentWindows();

  /** Return the primary tabbed workspace, creating it when necessary. */
  WorkspaceWindow *workspaceWindow();

  /** Move DOCUMENT from the tabbed workspace into a top-level window. */
  void detachDocument(MainWindow *document);

  /** Move DOCUMENT into the primary tabbed workspace without recreating it. */
  void attachDocument(MainWindow *document);

  /** Attach every detached document to the primary workspace. */
  void attachAllDocuments();

  /** Refresh tab/window presentation after DOCUMENT's title changes. */
  void refreshDocumentPresentation(MainWindow *document);

  /** Return workspace-owned UI to DOCUMENT before its close event finishes. */
  void prepareDocumentForClose(MainWindow *document);

  /** Return the number of documents currently attached as tabs. */
  int tabCount() const;

  /** Rebuild MENU with document/view creation, arrangement, and activation
      actions.  CURRENTWINDOW is the owning document and CURRENTVIEW, when
      non-null, identifies the active secondary view. */
  void populateWindowMenu(QMenu *menu, MainWindow *currentWindow,
                          ImageViewWindow *currentView = nullptr);

  /** Activate the presentation OFFSET positions from the current view/window. */
  void activateRelativeWindow(MainWindow *currentWindow,
                              ImageViewWindow *currentView, int offset);

  /** Ask every document to close.  Used by smoke tests and application exit. */
  void closeAllDocumentWindows();

protected:
  /** Handle operating-system file-open events by opening another independent
      document; delegate all other events to QApplication.  */
  bool event(QEvent *event) override;

private:
  /** Remove deleted QPointer entries from m_documentWindows. */
  void pruneDocumentWindows();

  /** Remove deleted secondary-view pointers. */
  void pruneViewWindows();

  /** Return recovery directories containing usable per-document state. */
  QStringList recoveryDirectories() const;

  /** Return true when DIRECTORY contains at least one recovery payload file. */
  bool recoveryDirectoryHasData(const QString &directory) const;

  /** Persist SOURCE's currently open slanted-edge reference filenames. */
  void saveSlantedEdgeReferenceRecovery(MainWindow *source);

  /** Read SOURCE's slanted-edge reference filenames from recovery metadata. */
  bool readSlantedEdgeReferenceRecovery(MainWindow *source,
                                        QStringList *references) const;

  /** Migrate the legacy single-document recovery payload into a new session
      directory.  Returns the new directory or an empty string on failure.  */
  QString migrateLegacyRecovery() const;

  /** Delete all supplied session directories and any legacy recovery files. */
  void discardRecoveryData(const QStringList &directories) const;

  /** Rebuild every live MainWindow's Window menu after the set changes. */
  void refreshWindowMenus();

  /** Return a reusable empty document, preferring PREFERREDWINDOW. */
  MainWindow *reusableWindow(MainWindow *preferredWindow);

  /** Return the active image document from either a tab or detached window. */
  MainWindow *activeDocument() const;

  /** Return whether DOCUMENT has a secondary view not already closing. */
  bool hasOpenViews(MainWindow *document,
                    ImageViewWindow *excluding = nullptr);

  /** Close a hidden document owner after an unexpectedly destroyed last view. */
  void closeHiddenDocumentWithoutViews(MainWindow *document);

  QList<QPointer<MainWindow>> m_documentWindows;
  QList<QPointer<ImageViewWindow>> m_viewWindows;
  QPointer<WorkspaceWindow> m_workspaceWindow;
  QSet<MainWindow *> m_restoringReferenceRecovery;
  QSet<MainWindow *> m_hiddenDocumentPresentations;
  QSet<MainWindow *> m_finalizingDocuments;
  QSet<MainWindow *> m_closingDocuments;
  QSet<ImageViewWindow *> m_closingViews;
};
