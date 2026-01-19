#include "RenderQueue.h"
#include <QDebug>
#include <algorithm>

RenderQueue::RenderQueue(QObject *parent) : QObject(parent)
{
}

RenderQueue::~RenderQueue() = default;

void RenderQueue::requestRender()
{
    m_hasPendingRender = true;
    process();
}

void RenderQueue::reportFinished(int reqId, bool success)
{
    // Find remove from active queue
    auto it = std::find_if(m_activeRenders.begin(), m_activeRenders.end(), [reqId](const ActiveRender& r){ return r.reqId == reqId; });
    if(it != m_activeRenders.end()) {
        if (it->progress) emit progressFinished(it->progress); 
        m_activeRenders.erase(it);
    }
    
    // Logic: If render finishes and older is running cancel old one.
    if (success && reqId > m_lastCompletedReqId) {
        m_lastCompletedReqId = reqId; 

        // Check active renders. Any with reqId < m_lastCompletedReqId are stale.
        for (auto &render : m_activeRenders) {
            if (render.reqId < m_lastCompletedReqId) {
                 if (render.progress && !render.progress->cancel_requested()) {
                     render.progress->cancel();
                     emit progressFinished(render.progress);
                 }
            }
        }
    }

    // Process next
    process();
}

void RenderQueue::cancelAll()
{
    for (auto &render : m_activeRenders) {
        if (render.progress && !render.progress->cancel_requested()) {
            render.progress->cancel();
            emit progressFinished(render.progress);
        }
    }
    m_activeRenders.clear();
    m_hasPendingRender = false;
}

void RenderQueue::process()
{
    // 1. Check for slow running jobs (> 5000ms) and cancel them if we have pending work
    if (m_hasPendingRender) {
        for (auto &render : m_activeRenders) {
            if (render.startTime.elapsed() > 5000) {
                 if (render.progress && !render.progress->cancel_requested()) {
                     render.progress->cancel();
                     emit progressFinished(render.progress); 
                 }
            }
        }
    }

    // 2. Enforce max 2 concurrent jobs
    if (m_hasPendingRender && m_activeRenders.size() >= 2) {
        // Cancel the latest one (back of the list) to make room
        ActiveRender &latest = m_activeRenders.back();
        
        if (latest.progress && !latest.progress->cancel_requested()) {
             latest.progress->cancel();
             emit progressFinished(latest.progress);
        }
        return;
    }
    
    // 3. Start new render if slot available and we have pending work
    if (m_hasPendingRender && m_activeRenders.size() < 2) {
        // We assume the renderer is ready or the slot will verify.
        
        m_hasPendingRender = false;

        ActiveRender newRender;
        newRender.reqId = ++m_requestCounter;
        newRender.startTime.start();
        newRender.progress = std::make_shared<colorscreen::progress_info>();

        emit progressStarted(newRender.progress); 
        
        // Add to list optimistically
        m_activeRenders.push_back(newRender);
        
        emit triggerRender(newRender.reqId, newRender.progress);
    }
}
