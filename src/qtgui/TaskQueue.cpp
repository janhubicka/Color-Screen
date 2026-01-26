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

QString TaskQueue::formatQueueState() const
{
    QString state;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        const char *t;
	float s;
	it.value().progress->get_status (&t, &s);
        state += QString(" [%1: %2ms %3 %4]").arg(it.key()).arg(it.value().startTime.elapsed()).arg(t).arg(s);
    }
    if (m_pendingReqId.has_value()) {
        state += QString(" [Pending: %1]").arg(m_pendingReqId.value());
    }
    return state;
}
