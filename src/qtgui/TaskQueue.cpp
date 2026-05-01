#include "TaskQueue.h"
#include <QDebug>
#include "Logging.h"

TaskQueue::TaskQueue(QObject *parent) : QObject(parent)
{
}

TaskQueue::~TaskQueue()
{
    cancelAll();
}

int TaskQueue::requestRender(const QVariant &userData)
{
    int newReqId = m_nextReqId++;
    
    // qDebug() << "TaskQueue::requestRender - Requesting ID:" << newReqId << " Queue Size:" << m_tasks.size();

    // 1. Cancel tasks running too long (> 5000ms)
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
        if (it->active && it->startTime.elapsed() > 5000) {
            qCDebug(lcRenderSync) << "  Task ID:" << it->reqId << " timed out (>5000ms). Cancelling. State:" << formatQueueState();
            if (it->progress && !it->progress->pool_cancel()) {
                it->progress->cancel();
                emit progressFinished(it->progress);
                it = m_tasks.erase(it);
                continue;
            }
        }
        ++it;
    }

    // 2. Check concurrency limit (max 2 active)
    if (m_tasks.size() >= 2) {
        // Are we already cancelling something?
        bool alreadyCancelling = false;
        for (const auto &info : m_tasks) {
            if (info.progress && info.progress->pool_cancel()) {
                alreadyCancelling = true;
                break;
            }
        }

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
        return newReqId;
    }

    // 3. Start immediately if slot available
    startTask(newReqId, userData);
    return newReqId;
}

void TaskQueue::startTask(int reqId, const QVariant &userData)
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
    emit triggerRender(reqId, info.progress, userData);
}

void TaskQueue::reportFinished(int reqId, bool success)
{
    qCDebug(lcRenderSync) << "TaskQueue::reportFinished - Finished ID:" << reqId << " Success:" << success;
    auto it = m_tasks.find(reqId);
    if (it != m_tasks.end()) {
        emit progressFinished(it->progress);
        m_tasks.erase(it);
    }
    
    // "If task finishes and there are older tasks in queue, they should be cancelled."
    // (Older active tasks)
    if (success)
      for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
	  if (it->reqId < reqId) {
	       qCDebug(lcRenderSync) << "  Cancelling older task ID:" << it->reqId << " as newer task finished. State:" << formatQueueState();
	       if (it->progress) it->progress->cancel();
	       emit progressFinished(it->progress); // Optimistic cleanup
	       it = m_tasks.erase(it);
	  } else {
	       ++it;
	  }
      }

    processPending();
}

void TaskQueue::processPending()
{
    // If we have a pending task and a slot is free (size < 2)
    if (m_pendingReqId.has_value() && m_tasks.size() < 2) {
        int reqId = m_pendingReqId.value();
        QVariant userData = m_pendingUserData;
        qCDebug(lcRenderSync) << "TaskQueue::processPending - Starting pending task ID:" << reqId;
        m_pendingReqId.reset();
        m_pendingUserData = QVariant();
        startTask(reqId, userData);
    }
}

void TaskQueue::cancelAll()
{
    // qDebug() << "TaskQueue::cancelAll - Cancelling all tasks.";
    m_pendingReqId.reset();
    m_pendingUserData = QVariant();
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->progress) {
             it->progress->cancel();
             emit progressFinished(it->progress);
        }
    }
    m_tasks.clear();
}

bool TaskQueue::hasActiveTasks() const {
  return !m_tasks.isEmpty() || m_pendingReqId.has_value();
}

void TaskQueue::runAsync (std::function<void (colorscreen::progress_info *)> worker,
                          std::function<void ()> done,
                          const QVariant &userData)
{
  /* Pre-allocate the request ID so we can capture it in the lambda BEFORE
     calling requestRender().  requestRender() → startTask() → emit
     triggerRender() happens synchronously, so the connection must already
     be live when that emit fires.

     Qt::QueuedConnection guarantees the lambda runs from the event loop
     even when sender and receiver share a thread, so it always executes
     after this function returns and the connection is fully established.  */
  int reqId = m_nextReqId;  /* peek — requestRender() will assign the same ID */

  auto connHolder = std::make_shared<QMetaObject::Connection> ();

  *connHolder = connect (
      this, &TaskQueue::triggerRender, this,
      [this, reqId,
       worker = std::move (worker),
       done   = std::move (done),
       connHolder]
      (int firedId, std::shared_ptr<colorscreen::progress_info> progress,
       const QVariant &) mutable
      {
        if (firedId != reqId)
          return;

        /* One-shot: disconnect immediately so later tasks don't re-trigger.  */
        disconnect (*connHolder);
        connHolder.reset ();

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
      },
      Qt::QueuedConnection);

  /* Now request the render — this will emit triggerRender(reqId, ...) which
     is queued and delivered on the next event-loop iteration.  */
  requestRender (userData);
}

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
