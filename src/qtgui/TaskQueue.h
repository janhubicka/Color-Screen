#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <QObject>
#include <QMap>
#include <QMutex>
#include <QElapsedTimer>
#include <QVariant>
#include <memory>
#include <optional>
#include "../libcolorscreen/include/progress-info.h"

// TaskQueue manages concurrent render/solver requests.
class TaskQueue : public QObject {
  Q_OBJECT
public:
  explicit TaskQueue(QObject *parent = nullptr);
  ~TaskQueue() override;

  // Requests a new task. Returns the assigned request ID.
  // Note: The task might be queued if concurrency limit is reached.
  int requestRender(const QVariant &userData = QVariant());

  // Reports that a task has finished.
  void reportFinished(int reqId, bool success);

  // Cancels all pending and active tasks.
  void cancelAll();

  // Returns true if there are any active or pending tasks.
  bool hasActiveTasks() const;

signals:
  // Emitted when the queue decides to start a task.
  void triggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);

  // Progress integration
  void progressStarted(std::shared_ptr<colorscreen::progress_info> info);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> info);

private:
  void startTask(int reqId, const QVariant &userData);
  void processPending();

  int m_nextReqId = 1;

  struct TaskInfo {
    int reqId;
    std::shared_ptr<colorscreen::progress_info> progress;
    QElapsedTimer startTime;
    QVariant userData;
    bool active = false;
  };

  QMap<int, TaskInfo> m_tasks;
  
  // Pending request (waiting for a slot)
  std::optional<int> m_pendingReqId;
  QVariant m_pendingUserData;
};

#endif // TASK_QUEUE_H
