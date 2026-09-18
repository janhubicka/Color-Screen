#pragma once

#include "../libcolorscreen/include/progress-info.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QDockWidget;
class QLabel;
class QMainWindow;
class QProgressBar;
class QPushButton;
class QStatusBar;
class QTimer;
class QVBoxLayout;
class QWidget;

/** Action offered for a user-visible long-running progress task. */
enum class ProgressAction { Cancel, Stop };

/** One registered background operation and its optional dedicated status row. */
struct ProgressEntry {
  std::shared_ptr<colorscreen::progress_info> info;
  QElapsedTimer startTime;
  bool userVisible = false;
  ProgressAction action = ProgressAction::Cancel;
  QString title;
  QWidget *row = nullptr;
  QLabel *rowLabel = nullptr;
  QProgressBar *rowProgressBar = nullptr;
  QPushButton *rowActionButton = nullptr;
};

/** Own one document's transient and dedicated background-progress presentation.

    MainWindow keeps document/workspace policy. This controller owns the widget
    trees, task entries, delayed transient visibility, task switching, periodic
    status formatting, and cooperative Cancel/Stop UI. */
class DocumentProgressController final : public QObject {
public:
  /** User-facing strings translated in the owning MainWindow context. */
  struct Labels {
    QString stop;
    QString cancel;
    QString stopping;
    QString cancelling;
    QString working;
  };

  /** Policy hooks supplied by the owning document. */
  struct Callbacks {
    std::function<bool(
        const std::shared_ptr<colorscreen::progress_info> &, ProgressAction)>
        confirmTermination;
    std::function<void(QWidget *)> releaseFocus;
    std::function<void(bool)> transientVisibilityChanged;
    std::function<void(bool)> userVisibleVisibilityChanged;
  };

  /** Construct an unbound controller; initialize() creates its UI. */
  DocumentProgressController() = default;

  /** Build the progress widgets in STATUSBAR and HOST's bottom dock. */
  void initialize(QMainWindow *host, QStatusBar *statusBar, Labels labels,
                  Callbacks callbacks);

  /** Register ordinary transient background progress. */
  void addProgress(std::shared_ptr<colorscreen::progress_info> info);

  /** Register a long-running task with its own progress row. */
  void addUserVisibleProgress(
      std::shared_ptr<colorscreen::progress_info> info, const QString &title,
      ProgressAction action = ProgressAction::Cancel);

  /** Remove a completed or terminated task from presentation. */
  void removeProgress(std::shared_ptr<colorscreen::progress_info> info);

  /** Request cooperative cancellation of every registered task. */
  void cancelAll();

  /** Return the one-line transient progress widget. */
  QWidget *transientWidget() const { return m_progressContainer; }

  /** Return the container holding dedicated long-running task rows. */
  QWidget *userVisibleWidget() const { return m_userVisibleProgressContainer; }

  /** Return the local task dock used by a detached document window. */
  QDockWidget *userVisibleDock() const { return m_userVisibleProgressDock; }

  /** Return whether transient progress has passed the display delay. */
  bool hasVisibleTransientProgress() const { return m_transientProgressVisible; }

  /** Remove transient progress from STATUSBAR for workspace hosting. */
  QWidget *takeTransientWidget(QStatusBar *statusBar);

  /** Return transient progress to STATUSBAR after leaving the workspace. */
  void restoreTransientWidget(QStatusBar *statusBar);

  /** Remove dedicated rows from the local dock for workspace hosting. */
  QWidget *takeUserVisibleWidget();

  /** Return dedicated rows to the local document dock. */
  void restoreUserVisibleWidget();

  /** Expose registered entries to the lifecycle smoke probe. */
  const std::vector<ProgressEntry> &entries() const { return m_activeProgresses; }

private:
  /** Register INFO with transient or dedicated-row presentation. */
  void registerProgress(std::shared_ptr<colorscreen::progress_info> info,
                        bool userVisible, const QString &title,
                        ProgressAction action);

  /** Return active entries sharing the transient status row. */
  std::vector<ProgressEntry *> transientProgresses();

  /** Return the most relevant transient task for automatic selection. */
  ProgressEntry *longestRunningTask();

  /** Format ENTRY's nested progress state into LABEL and BAR. */
  void updateProgressWidgets(const ProgressEntry &entry, QLabel *label,
                             QProgressBar *bar, const QString &title);

  /** Request cooperative cancellation/stopping of INFO. */
  void requestTermination(
      const std::shared_ptr<colorscreen::progress_info> &info,
      ProgressAction action);

  /** Periodically refresh transient and dedicated progress presentation. */
  void onTimer();

  /** Select the previous transient task. */
  void selectPrevious();

  /** Select the next transient task. */
  void selectNext();

  /** Set delayed transient visibility and notify the owning document. */
  void setTransientVisible(bool visible);

  /** Synchronize dedicated-row container/dock visibility. */
  void updateContainerVisibility();

  /** Return focus through the document policy hook before ROW disappears. */
  void releaseFocus(QWidget *row);

  QMainWindow *m_host = nullptr;
  Labels m_labels;
  Callbacks m_callbacks;

  QWidget *m_progressContainer = nullptr;
  QWidget *m_transientProgressRow = nullptr;
  QLabel *m_statusLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QLabel *m_progressCountLabel = nullptr;
  QPushButton *m_prevProgressButton = nullptr;
  QPushButton *m_nextProgressButton = nullptr;
  QPushButton *m_cancelButton = nullptr;

  QWidget *m_userVisibleProgressContainer = nullptr;
  QVBoxLayout *m_userVisibleProgressLayout = nullptr;
  QDockWidget *m_userVisibleProgressDock = nullptr;

  QTimer *m_timer = nullptr;
  std::vector<ProgressEntry> m_activeProgresses;
  std::shared_ptr<colorscreen::progress_info> m_currentlyDisplayedProgress;
  int m_manuallySelectedProgressIndex = -1;
  bool m_transientProgressVisible = false;
};
