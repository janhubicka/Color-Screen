#include "TaskQueue.h"
#include <QDebug>
#include <QMutexLocker>
#include "Logging.h"

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
void TaskQueue::reportFinished(int reqId, bool success)
{
    QMutexLocker locker(&m_mutex);
    qCDebug(lcRenderSync) << "TaskQueue::reportFinished - Finished ID:" << reqId << " Success:" << success;
    auto it = m_tasks.find(reqId);
    if (it != m_tasks.end()) {
        emit progressFinished(it->progress);
        m_tasks.erase(it);
    }
    
    // If task finishes and there are older tasks in queue, they should be cancelled.
    if (success)
      for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
	  if (it->reqId < reqId) {
	       qCDebug(lcRenderSync) << "  Cancelling older task ID:" << it->reqId << " as newer task finished. State:" << formatQueueState();
	       if (it->progress) it->progress->cancel();
	       emit progressFinished(it->progress);
	       it = m_tasks.erase(it);
	  } else {
	       ++it;
	  }
      }

    processPending();
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
                          std::function<void ()> done,
                          const QVariant &userData)
{
  requestRender(userData, [this, worker = std::move(worker), done = std::move(done)](int reqId, std::shared_ptr<colorscreen::progress_info> progress) mutable {
    /* Launch worker on Qt thread-pool; progress is read-only in worker.  */
    auto *watcher = new QFutureWatcher<void> (this);
    connect (watcher, &QFutureWatcher<void>::finished, this,
             [this, reqId, watcher, done = std::move (done)] () mutable
             {
               watcher->deleteLater ();
               done ();
               reportFinished (reqId, true);
             });
    watcher->setFuture (
        QtConcurrent::run (
            [w = std::move (worker), progress] () mutable
            { w (progress.get ()); }));
  });
}

/**
 * @brief Formats the current queue contents into a debug string.
 */
QString TaskQueue::formatQueueState() const
{
    QString state;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        const char *t;
	float s;
	it.value().progress->get_status (&t, &s);
        state += QString(" [%1: %2ms %3 %4%%%5]").arg(it.key()).arg(it.value().startTime.elapsed()).arg(t).arg(s).arg(it.value().progress->pool_cancel () ? "canceling" : "");
    }
    if (m_pendingReqId.has_value()) {
        state += QString(" [Pending: %1]").arg(m_pendingReqId.value());
    }
    return state;
}

