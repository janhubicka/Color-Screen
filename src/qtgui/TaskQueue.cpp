#include "TaskQueue.h"
#include <QDebug>
#include <QFutureWatcher>
#include <QMutexLocker>
#include <QPromise>
#include <QRunnable>
#include <QThreadPool>
#include <mutex>
#include "Logging.h"

namespace {

/** Fire-and-forget runnable with an explicitly synchronized worker payload.

    The runnable must be fully constructed before QThreadPool sees it, and the
    closure itself must cross a C++ synchronization edge before run() reads it.
    Qt's pool submission is thread-safe, but ThreadSanitizer does not model that
    internal handoff as publication of arbitrary QRunnable members. publish()
    therefore stores the closure under a std::mutex before submission; run()
    takes it under the same mutex and executes the local copy after unlocking. */
class TaskQueueRunnable final : public QRunnable {
public:
  TaskQueueRunnable() { setAutoDelete(true); }

  /** Publish FUNCTION before this runnable is submitted to QThreadPool. */
  void publish(std::function<void()> function) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_function = std::move(function);
    m_published = true;
  }

  /** Take the published closure on the pool thread and execute it unlocked. */
  void run() override {
    std::function<void()> function;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_published)
        return;
      function = std::move(m_function);
    }
    if (function)
      function();
  }

private:
  std::mutex m_mutex;
  std::function<void()> m_function;
  bool m_published = false;
};

} // namespace

TaskQueue::TaskQueue(QObject *parent) : QObject(parent)
{
}

TaskQueue::~TaskQueue()
{
    cancelAll();
}

/**
 * @brief Handles a request for a new render task.
 * 
 * This method implements the core of the "Two-Task Scheme":
 * 1. Checks for tasks that have exceeded TASK_TIMEOUT_MS and cancels them.
 * 2. If the active task list is at MAX_CONCURRENT_TASKS, it either:
 *    - Replaces the current pending request if one exists.
 *    - Cancels the youngest active task to make room for the new one (if not already cancelling).
 * 3. Starts the task immediately if a slot is available.
 */
int TaskQueue::requestRender(const QVariant &userData, 
                             std::function<void(int reqId, std::shared_ptr<colorscreen::progress_info>)> onStart)
{
    QMutexLocker locker(&m_mutex);
    int newReqId = m_nextReqId++;
    m_latestRequestedReqId = newReqId;
    
    // 1. Cancel tasks running too long
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
        if (it->active && it->startTime.elapsed() > TASK_TIMEOUT_MS) {
            qCDebug(lcRenderSync) << "  Task ID:" << it->reqId << " timed out. Cancelling. State:" << formatQueueState();
            if (it->progress && !it->progress->pool_cancel()) {
                it->progress->cancel();
                emit progressFinished(it->progress);
                it = m_tasks.erase(it);
                continue;
            }
        }
        ++it;
    }

    // 2. Check concurrency limit
    if (m_tasks.size() >= MAX_CONCURRENT_TASKS) {
        // Are we already cancelling something?
        bool alreadyCancelling = std::any_of(m_tasks.begin(), m_tasks.end(), [](const TaskInfo &info) {
            return info.progress && info.progress->pool_cancel();
        });

        if (!alreadyCancelling) {
            auto lastIt = m_tasks.end();
            if (!m_tasks.empty()) {
                lastIt--; 
                
                if (lastIt->active && lastIt->progress && !lastIt->progress->pool_cancel()) {
                    qCDebug(lcRenderSync) << "  Queue Full. Cancelling younger task ID:" << lastIt->reqId << " to make room. State:" << formatQueueState();
                    lastIt->progress->cancel();
                }
            }
        } else {
             qCDebug(lcRenderSync) << "  Queue Full, but already cancelling a task. Updating pending request only. State:" << formatQueueState();
        }

        qCDebug(lcRenderSync) << "  Queueing new task ID:" << newReqId;
        m_pendingReqId = newReqId;
        m_pendingUserData = userData;
        m_pendingOnStart = std::move(onStart);
        return newReqId;
    }

    // 3. Start immediately if slot available
    startTask(newReqId, userData, onStart);
    return newReqId;
}

/**
 * @brief Internal helper to initialize and start a task.
 * @note Must be called with m_mutex held.
 */
void TaskQueue::startTask(int reqId, const QVariant &userData, 
                          std::function<void(int reqId, std::shared_ptr<colorscreen::progress_info>)> onStart)
{
    qCDebug(lcRenderSync) << "TaskQueue::startTask - Starting ID:" << reqId << "Active:" << formatQueueState();
    TaskInfo info;
    info.reqId = reqId;
    info.progress = std::make_shared<colorscreen::progress_info>();
    info.progress->set_task("Waiting to be enqueued", 1);
    info.startTime.start();
    info.active = true;
    info.userData = userData;
    
    m_tasks[reqId] = info;
    
    emit progressStarted(info.progress);
    
    if (onStart) {
        onStart(reqId, info.progress);
    } else {
        emit triggerRender(reqId, info.progress, userData);
    }
}

/**
 * @brief Cleans up a finished task and potentially starts a pending one.
 */
bool TaskQueue::reportFinished(int reqId, bool success)
{
    QMutexLocker locker(&m_mutex);
    qCDebug(lcRenderSync) << "TaskQueue::reportFinished - Finished ID:" << reqId << " Success:" << success;
    auto it = m_tasks.find(reqId);
    if (it == m_tasks.end()) {
        qCDebug(lcRenderSync) << "  Ignoring completion for stale/cancelled task ID:" << reqId;
        return false;
    }

    // Only the newest request may publish. A cancellation request also makes
    // the result stale immediately, even if the worker races to a nominally
    // successful completion before observing it.
    const bool publishResult =
        reqId == m_latestRequestedReqId &&
        (!it->progress || !it->progress->pool_cancel());
    emit progressFinished(it->progress);
    m_tasks.erase(it);

    // A successful newest task supersedes every older request.  Their workers
    // may still emit completion later, but the missing queue entry above makes
    // those results unpublishable.
    if (success && publishResult)
      for (auto older = m_tasks.begin(); older != m_tasks.end(); ) {
          if (older->reqId < reqId) {
               qCDebug(lcRenderSync) << "  Cancelling older task ID:" << older->reqId << " as newer task finished. State:" << formatQueueState();
               if (older->progress) older->progress->cancel();
               emit progressFinished(older->progress);
               older = m_tasks.erase(older);
          } else {
               ++older;
          }
      }

    processPending();
    return publishResult;
}

/**
 * @brief Internal helper to promote a pending task to active status.
 * @note Must be called with m_mutex held.
 */
void TaskQueue::processPending()
{
    if (m_pendingReqId.has_value() && m_tasks.size() < MAX_CONCURRENT_TASKS) {
        int reqId = m_pendingReqId.value();
        QVariant userData = m_pendingUserData;
        auto onStart = std::move(m_pendingOnStart);
        qCDebug(lcRenderSync) << "TaskQueue::processPending - Starting pending task ID:" << reqId;
        m_pendingReqId.reset();
        m_pendingUserData = QVariant();
        m_pendingOnStart = nullptr;
        startTask(reqId, userData, onStart);
    }
}

/**
 * @brief Cancels everything in the queue.
 */
void TaskQueue::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    m_pendingReqId.reset();
    m_pendingUserData = QVariant();
    m_pendingOnStart = nullptr;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->progress) {
             it->progress->cancel();
             emit progressFinished(it->progress);
        }
    }
    m_tasks.clear();
}

/** @return True if there is any work being tracked. */
bool TaskQueue::hasActiveTasks() const {
  QMutexLocker locker(&m_mutex);
  return !m_tasks.isEmpty() || m_pendingReqId.has_value();
}

/**
 * @brief Runs a one-shot worker task through the queue.
 */
void TaskQueue::runAsync (std::function<void (colorscreen::progress_info *)> worker,
                          std::function<void (bool publishResult)> done,
                          const QVariant &userData,
                          std::function<void ()> started)
{
  requestRender(userData, [this, worker = std::move(worker), done = std::move(done),
                           started = std::move(started)](int reqId, std::shared_ptr<colorscreen::progress_info> progress) mutable {
    if (started)
      started();
    /* Keep completion on this object's GUI thread, but submit a runnable only
       after its construction is complete. QPromise preserves QFutureWatcher
       lifetime/disconnect behavior when TaskQueue is destroyed mid-operation. */
    auto *watcher = new QFutureWatcher<void>(this);
    auto promise = std::make_shared<QPromise<void>>();
    const QFuture<void> future = promise->future();
    promise->start();
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, reqId, watcher, done = std::move(done)]() mutable {
              watcher->deleteLater();
              const bool publishResult = reportFinished(reqId, true);
              done(publishResult);
            });
    watcher->setFuture(future);

    auto *runnable = new TaskQueueRunnable;
    runnable->publish(
        [w = std::move(worker), progress, promise]() mutable {
          w(progress.get());
          promise->finish();
        });
    QThreadPool::globalInstance()->start(runnable);
  });
}

/**
 * @brief Formats the current queue contents into a debug string.
 */
QString TaskQueue::formatQueueState() const
{
    QString state;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        auto status = it.value().progress->get_status();
        const char *t = status.empty() ? "" : status.back().task.c_str();
        float s = status.empty() ? -1 : status.back().progress;
        state += QString(" [%1: %2ms %3 %4%%%5]").arg(it.key()).arg(it.value().startTime.elapsed()).arg(t).arg(s).arg(it.value().progress->pool_cancel () ? "canceling" : "");
    }
    if (m_pendingReqId.has_value()) {
        state += QString(" [Pending: %1]").arg(m_pendingReqId.value());
    }
    return state;
}

