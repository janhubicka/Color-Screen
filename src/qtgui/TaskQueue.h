#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <QObject>
#include <QMap>
#include <QRecursiveMutex>
#include <QElapsedTimer>
#include <QVariant>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>
#include <memory>
#include <optional>
#include "../libcolorscreen/include/progress-info.h"

/**
 * @brief TaskQueue manages concurrent render and solver requests for the GUI.
 *
 * It implements a specialized queuing strategy designed for interactive image
 * processing. The queue follows a "Two-Task Scheme":
 * 1. It allows up to two concurrent tasks (e.g., one active render and one
 *    newer request that can cancel the active one if it takes too long).
 * 2. Rapid successive requests (e.g., from slider movement) automatically
 *    cancel or supersede older pending requests to ensure the user always
 *    sees the result of the freshest parameters.
 * 3. It provides progress tracking integration via libcolorscreen's
 *    progress_info system.
 */
class TaskQueue : public QObject {
  Q_OBJECT
public:
  /** Maximum number of tasks allowed to run or be queued simultaneously. */
  static constexpr int MAX_CONCURRENT_TASKS = 2;
  /** Timeout in milliseconds after which an active task is considered stale and can be cancelled. */
  static constexpr int TASK_TIMEOUT_MS = 5000;

  explicit TaskQueue(QObject *parent = nullptr);
  ~TaskQueue() override;

  /**
   * @brief Requests a new render task.
   * @param userData Optional metadata associated with the request (e.g., tile coordinates).
   * @param onStart Optional callback invoked when the task officially starts. If null, triggerRender is emitted.
   * @return The assigned request ID.
   *
   * If the queue is at capacity, the request will be queued as "pending",
   * potentially cancelling an older active task if it has timed out.
   */
  int requestRender(const QVariant &userData = QVariant(), 
                    std::function<void(int reqId, std::shared_ptr<colorscreen::progress_info>)> onStart = nullptr);

  /**
   * @brief Reports that a task has finished.
   * @param reqId The ID of the completed task.
   * @param success True if the task completed successfully.
   *
   * This method cleans up the task state and triggers the next pending task.
   */
  void reportFinished(int reqId, bool success);

  /**
   * @brief Cancels all pending and active tasks.
   *
   * Clears the queue and signals cancellation to all running workers.
   */
  void cancelAll();

  /**
   * @brief Submit a one-shot task to run in the background.
   * @param worker A function that performs the work; it receives a progress_info pointer.
   * @param done A callback invoked on the GUI thread after completion.
   * @param userData Optional metadata.
   *
   * The task is managed by the queue's concurrency and timeout rules.
   */
  void runAsync (std::function<void (colorscreen::progress_info *)> worker,
                 std::function<void ()> done,
                 const QVariant &userData = {});

  /** @return True if there are any active or pending tasks in the queue. */
  bool hasActiveTasks() const;

signals:
  /** @brief Emitted when a task is officially started. */
  void triggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);

  /** @brief Emitted when a task's progress tracking should be shown in the UI. */
  void progressStarted(std::shared_ptr<colorscreen::progress_info> info);
  /** @brief Emitted when a task's progress tracking should be removed from the UI. */
  void progressFinished(std::shared_ptr<colorscreen::progress_info> info);

private:
  /** Internal helper to initialize and start a task. */
  void startTask(int reqId, const QVariant &userData, std::function<void(int reqId, std::shared_ptr<colorscreen::progress_info>)> onStart);
  /** Internal helper to move a pending request into the active slot if available. */
  void processPending();
  /** Formats the current queue state for debugging purposes. */
  QString formatQueueState() const;

  int m_nextReqId = 1;

  /** @brief Internal state for a managed task. */
  struct TaskInfo {
    int reqId;
    std::shared_ptr<colorscreen::progress_info> progress;
    QElapsedTimer startTime;
    QVariant userData;
    bool active = false;
  };

  QMap<int, TaskInfo> m_tasks;
  
  std::optional<int> m_pendingReqId;
  QVariant m_pendingUserData;
  std::function<void(int reqId, std::shared_ptr<colorscreen::progress_info>)> m_pendingOnStart;

  /** Recursive mutex to protect internal state across threads and handle re-entrant calls. */
  mutable QRecursiveMutex m_mutex;
};

#endif // TASK_QUEUE_H
