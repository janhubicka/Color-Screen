#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <memory>
#include <list>
#include "../libcolorscreen/include/progress-info.h"

class RenderQueue : public QObject
{
    Q_OBJECT
public:
    explicit RenderQueue(QObject *parent = nullptr);
    ~RenderQueue();

    // Call this to indicate pending changes (view/parameters)
    void requestRender();

    // Call this when a render job completes (success or failure)
    void reportFinished(int reqId, bool success);
    
    void cancelAll();

signals:
    // Emitted when logic decides a new render should start.
    // The connected slot should gather current parameters and invoke the renderer.
    // If invocation fails, the slot MUST call reportFinished(reqId, false) immediately.
    void triggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress);

    // Proxy signals for MainWindow status bar
    void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
    void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);

private:
    void process();

    struct ActiveRender {
        int reqId;
        QElapsedTimer startTime;
        std::shared_ptr<colorscreen::progress_info> progress;
        bool operator==(const ActiveRender& other) const { return reqId == other.reqId; }
    };

    std::list<ActiveRender> m_activeRenders;
    bool m_hasPendingRender = false;
    int m_requestCounter = 0;
    int m_lastCompletedReqId = -1;
};
