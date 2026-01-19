#include "TaskQueue.h"
#include <QDebug>

TaskQueue::TaskQueue(QObject *parent) : QObject(parent)
{
}

TaskQueue::~TaskQueue()
{
    cancelAll();
}

int TaskQueue::requestRender()
{
    int newReqId = m_nextReqId++;
    
    qDebug() << "TaskQueue::requestRender - Requesting ID:" << newReqId << " Queue Size:" << m_tasks.size();
    if (m_pendingReqId.has_value()) {
        qDebug() << "  Pending ID:" << m_pendingReqId.value();
    }
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        qDebug() << "  Active Task ID:" << it->reqId << " Running for:" << it->startTime.elapsed() << "ms";
    }

    // 1. Cancel tasks running too long (> 5000ms)
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
        if (it->active && it->startTime.elapsed() > 5000) {
            qDebug() << "  Task ID:" << it->reqId << " timed out (>5000ms). Cancelling.";
            if (it->progress && !it->progress->cancel_requested()) {
                it->progress->cancel();
                // Emit finished immediately so we don't count it as active
                // (Assuming strict cancellation where we don't wait for worker acknowledgement for queue logic)
                emit progressFinished(it->progress);
                it = m_tasks.erase(it);
                continue;
            }
        }
        ++it;
    }

    // 2. Check concurrency limit (max 2 active)
    if (m_tasks.size() >= 2) {
        // "If the two tasks are running the younger is cancelled."
        // Younger = Higher ID (Start time is similar proxy, looking at map end is easiest)
        
        // Find the task with the highest ID (last in map)
        auto lastIt = m_tasks.end();
        if (!m_tasks.empty()) {
            lastIt--; 
            
            if (lastIt->active && lastIt->progress && !lastIt->progress->cancel_requested()) {
                qDebug() << "  Queue Full. Cancelling younger task ID:" << lastIt->reqId << " to make room.";
                lastIt->progress->cancel();
                // We do NOT remove it here. "New tasks should not start before the cancellation takes effect"
                // We wait for reportFinished to clear it.
            }
        }

        // Queue the new task
        qDebug() << "  Queueing new task ID:" << newReqId;
        m_pendingReqId = newReqId;
        return newReqId;
    }

    // 3. Start immediately if slot available
    startTask(newReqId);
    return newReqId;
}

void TaskQueue::startTask(int reqId)
{
    qDebug() << "TaskQueue::startTask - Starting ID:" << reqId;
    TaskInfo info;
    info.reqId = reqId;
    info.progress = std::make_shared<colorscreen::progress_info>();
    info.startTime.start();
    info.active = true;
    
    m_tasks[reqId] = info;
    
    emit progressStarted(info.progress);
    emit triggerRender(reqId, info.progress);
}

void TaskQueue::reportFinished(int reqId, bool success)
{
    qDebug() << "TaskQueue::reportFinished - Finished ID:" << reqId << " Success:" << success;
    auto it = m_tasks.find(reqId);
    if (it != m_tasks.end()) {
        emit progressFinished(it->progress);
        m_tasks.erase(it);
    }
    
    // "If task finishes and there are older tasks in queue, they should be cancelled."
    // (Older active tasks)
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ) {
        if (it->reqId < reqId) {
             qDebug() << "  Cancelling older task ID:" << it->reqId << " as newer task finished.";
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
        qDebug() << "TaskQueue::processPending - Starting pending task ID:" << reqId;
        m_pendingReqId.reset();
        startTask(reqId);
    }
}

void TaskQueue::cancelAll()
{
    qDebug() << "TaskQueue::cancelAll - Cancelling all tasks.";
    m_pendingReqId.reset();
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->progress) {
             it->progress->cancel();
             emit progressFinished(it->progress);
        }
    }
    m_tasks.clear();
}
