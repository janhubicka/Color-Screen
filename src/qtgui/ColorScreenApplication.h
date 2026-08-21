#pragma once

#include <QApplication>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

class MainWindow;
class QEvent;
class QMenu;

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
      const QString &recoveryDirectory = QString());

  /** Open every path in FILENAMES as an independent image document.

      PREFERREDWINDOW is reused for the first file only when it is still an
      untouched empty window; every remaining file receives a new MainWindow.
      SUPPRESSPARAMPROMPT is used only by crash recovery.  */
  void openFiles(const QStringList &fileNames,
                 MainWindow *preferredWindow = nullptr,
                 bool suppressParamPrompt = false);

  /** Restore all documents left by an unclean shutdown.

      The user is prompted once for the whole recovered session.  Returns true
      when at least one recovery window was created.  */
  bool restoreRecoverySession();

  /** Return the currently live document windows in creation order. */
  QList<MainWindow *> documentWindows();

  /** Rebuild MENU with document-window creation, cycling, and activation
      actions.  CURRENTWINDOW is marked as the active document.  */
  void populateWindowMenu(QMenu *menu, MainWindow *currentWindow);

  /** Activate the document OFFSET positions from CURRENTWINDOW, wrapping at
      either end of the current document list.  */
  void activateRelativeWindow(MainWindow *currentWindow, int offset);

  /** Ask every document window to close, stopping if a window rejects its
      close event because of unsaved work or an active export.  */
  void closeAllDocumentWindows();

protected:
  /** Handle operating-system file-open events by opening another independent
      document; delegate all other events to QApplication.  */
  bool event(QEvent *event) override;

private:
  /** Remove deleted QPointer entries from m_documentWindows. */
  void pruneDocumentWindows();

  /** Return recovery directories containing usable per-document state. */
  QStringList recoveryDirectories() const;

  /** Return true when DIRECTORY contains at least one recovery payload file. */
  bool recoveryDirectoryHasData(const QString &directory) const;

  /** Migrate the legacy single-document recovery payload into a new session
      directory.  Returns the new directory or an empty string on failure.  */
  QString migrateLegacyRecovery() const;

  /** Delete all supplied session directories and any legacy recovery files. */
  void discardRecoveryData(const QStringList &directories) const;

  /** Rebuild every live MainWindow's Window menu after the set changes. */
  void refreshWindowMenus();

  /** Return a reusable empty document, preferring PREFERREDWINDOW. */
  MainWindow *reusableWindow(MainWindow *preferredWindow);

  QList<QPointer<MainWindow>> m_documentWindows;
};
