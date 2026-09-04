#pragma once

#include <QObject>
#include <QImage>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

class QThreadPool;

class Renderer : public QObject
{
    Q_OBJECT
public:
    explicit Renderer(std::shared_ptr<colorscreen::image_data> scan);
    ~Renderer() override;

    /**
     * Publish a complete render request before waking the renderer thread.
     *
     * Qt's queued metacall storage lives in an uninstrumented Qt library.  If
     * a non-trivial render_parameters object is copied through that storage,
     * ThreadSanitizer cannot see the event-queue synchronization and reports a
     * race between constructing the queued argument and consuming it.  Keep
     * the actual request in our own mutex-protected storage and queue only the
     * integer request id instead.
     */
    bool enqueueRender(int reqId, double xOffset, double yOffset, double scale,
                       int width, int height, int coordinateSpace,
                       const colorscreen::render_parameters &frameParams,
                       const colorscreen::scr_to_img_parameters &scrToImg,
                       const colorscreen::scr_detect_parameters &scrDetect,
                       const colorscreen::render_type_parameters &renderType,
                       std::shared_ptr<colorscreen::progress_info> progress,
                       const char *taskName = nullptr);


public slots:
    /** Consume a request previously published by enqueueRender(). */
    void render(int reqId);

signals:
    void imageReady(int reqId, QImage image, double xOffset, double yOffset,
                    double scale, bool success);

private:
    struct RenderRequest {
        double xOffset = 0;
        double yOffset = 0;
        double scale = 1;
        int width = 0;
        int height = 0;
        int coordinateSpace = 0;
        colorscreen::render_parameters frameParams;
        colorscreen::scr_to_img_parameters scrToImg;
        colorscreen::scr_detect_parameters scrDetect;
        colorscreen::render_type_parameters renderType;
        std::shared_ptr<colorscreen::progress_info> progress;
        const char *taskName = nullptr;
    };

    std::shared_ptr<colorscreen::image_data> m_scan;

    // Protect the cross-thread request handoff. Parameter snapshots become
    // immutable once inserted, so the GUI thread never mutates Renderer state.
    mutable std::mutex m_mutex;
    std::unordered_map<int, RenderRequest> m_pendingRenders;

    // A render already parallelizes internally with OpenMP.  Keep outer frame
    // requests serialized per Renderer instead of stacking cancelled/current
    // OpenMP teams in Qt's process-global pool.
    QThreadPool *m_renderPool = nullptr;

    void finishRenderTask();
    std::mutex m_activeMutex;
    std::condition_variable m_activeCondition;
    std::size_t m_activeTasks = 0;
};
