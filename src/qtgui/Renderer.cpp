#include "Renderer.h"
#include <QImage>
#include "Logging.h"
#include "SynchronizedRunnable.h"
#include <QColorSpace>

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include "CoordinateTransformer.h"

#include "../libcolorscreen/render-tile.h"

Renderer::Renderer(std::shared_ptr<colorscreen::image_data> scan,
                   const colorscreen::render_parameters &rparams,
                   const colorscreen::scr_to_img_parameters &scrToImg,
                   const colorscreen::scr_detect_parameters &scrDetect,
                   const colorscreen::render_type_parameters &renderType)
    : m_scan(scan), m_rparams(rparams), m_scrToImg(scrToImg),
      m_scrDetect(scrDetect), m_renderType(renderType)
{
}

Renderer::~Renderer()
{
    // Render tasks use this object while emitting their completion signal.
    // Keep it alive until every project-published QThreadPool task has left.
    std::unique_lock<std::mutex> locker(m_activeMutex);
    m_activeCondition.wait(locker, [this]() { return m_activeTasks == 0; });
}

bool
Renderer::enqueueRender(
    int reqId, double xOffset, double yOffset, double scale, int width,
    int height, int coordinateSpace,
    const colorscreen::render_parameters &frameParams,
    std::shared_ptr<colorscreen::progress_info> progress, const char *taskName)
{
    RenderRequest request;
    request.xOffset = xOffset;
    request.yOffset = yOffset;
    request.scale = scale;
    request.width = width;
    request.height = height;
    request.coordinateSpace = coordinateSpace;
    request.frameParams = frameParams;
    request.progress = std::move(progress);
    request.taskName = taskName;

    {
        std::lock_guard<std::mutex> locker(m_mutex);
        // Snapshot the cached renderer state together with the request.  A
        // later GUI update must not overtake an already-enqueued frame and mix
        // parameter generations before the renderer thread consumes its id.
        request.scrToImg = m_scrToImg;
        request.scrDetect = m_scrDetect;
        request.renderType = m_renderType;
        m_pendingRenders.insert_or_assign(reqId, std::move(request));
    }

    // Only the trivially-copyable request id crosses Qt's queued metacall.
    const bool queued =
        QMetaObject::invokeMethod(this, "render", Qt::QueuedConnection,
                                  Q_ARG(int, reqId));
    if (!queued) {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_pendingRenders.erase(reqId);
    }
    return queued;
}

void
Renderer::updateParameters(
    const colorscreen::render_parameters &rparams,
    const colorscreen::scr_to_img_parameters &scrToImg,
    const colorscreen::scr_detect_parameters &scrDetect,
    const colorscreen::render_type_parameters &renderType)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    m_rparams = rparams;
    m_scrToImg = scrToImg;
    m_scrDetect = scrDetect;
    m_renderType = renderType;
}

void Renderer::finishRenderTask()
{
    std::lock_guard<std::mutex> locker(m_activeMutex);
    if (m_activeTasks > 0)
        --m_activeTasks;
    m_activeCondition.notify_all();
}

void Renderer::render(int reqId)
{
    RenderRequest request;

    {
        std::lock_guard<std::mutex> locker(m_mutex);
        auto it = m_pendingRenders.find(reqId);
        if (it == m_pendingRenders.end())
            return;

        request = std::move(it->second);
        m_pendingRenders.erase(it);
    }

    const double xOffset = request.xOffset;
    const double yOffset = request.yOffset;
    const double scale = request.scale;
    const int width = request.width;
    const int height = request.height;
    const int coordinateSpace = request.coordinateSpace;
    auto frameParams = std::move(request.frameParams);
    auto scrToImg = std::move(request.scrToImg);
    auto scrDetect = std::move(request.scrDetect);
    auto renderType = std::move(request.renderType);
    auto progress = std::move(request.progress);
    const char *taskName = request.taskName;

    if (progress && progress->cancel_requested()) {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
        return;
    }
    if (progress)
        progress->set_task("Queuing render task", 1);

    if (progress)
        progress->set_task("Queuing to thread pool", 1);

    {
        std::lock_guard<std::mutex> locker(m_activeMutex);
        ++m_activeTasks;
    }

    runSynchronized(
        [this, reqId, xOffset, yOffset, scale, width, height, coordinateSpace,
         frameParams = std::move(frameParams), progress = std::move(progress),
         taskName, scrToImg = std::move(scrToImg),
         scrDetect = std::move(scrDetect), renderType = std::move(renderType)]() mutable {
            struct CompletionGuard {
                Renderer *renderer;
                ~CompletionGuard() { renderer->finishRenderTask(); }
            } completion{this};

            if (progress && progress->cancel_requested()) {
                emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
                return;
            }
            if (progress)
                progress->set_task("Render task started", 1);
            if (progress)
                progress->set_task("Calculating transformation", 1);

            if (!m_scan ||
                (!m_scan->has_grayscale_or_ir() && !m_scan->has_rgb())) {
                emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
                return;
            }

            colorscreen::render_coordinate_space requestedSpace =
                coordinateSpace == (int)colorscreen::render_final_coordinates
                    ? colorscreen::render_final_coordinates
                    : colorscreen::render_scan_coordinates;
            CoordinateTransformer transformer(m_scan.get(), frameParams,
                                              &scrToImg, requestedSpace);
            QSize transformedSize = transformer.getTransformedCropSize();

            QRectF visibleRect(0, 0, transformedSize.width(),
                               transformedSize.height());
            QRectF requestRect(xOffset, yOffset, (double)width / scale,
                               (double)height / scale);
            QRectF intersection = requestRect.intersected(visibleRect);

            if (intersection.isEmpty()) {
                emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
                return;
            }

            colorscreen::point_t p0 = transformer.transformedToRenderCrop(
                {intersection.left(), intersection.top()});
            colorscreen::point_t p1 = transformer.transformedToRenderCrop(
                {intersection.right(), intersection.bottom()});

            double sx_unit = std::min(p0.x, p1.x);
            double sy_unit = std::min(p0.y, p1.y);

            int tw = (int)std::round(intersection.width() * scale);
            int th = (int)std::round(intersection.height() * scale);

            if (tw <= 0)
                tw = 1;
            if (th <= 0)
                th = 1;

            /* Scan rotation/mirroring are presentation transforms.  Final
               views already contain their complete orientation in scr_to_img
               final geometry, so applying the scan transform here a second
               time changes tile dimensions/placement and clips the zero-based
               final canvas. */
            const bool applyScanPresentation =
                transformer.coordinateSpace() ==
                colorscreen::render_scan_coordinates;
            int angleIdx = 0;
            bool mirror = false;
            if (applyScanPresentation) {
                angleIdx = (int)(frameParams.scan_rotation) % 4;
                if (angleIdx < 0)
                    angleIdx += 4;
                mirror = frameParams.scan_mirror;
            }

            int renderW = tw;
            int renderH = th;
            if (angleIdx == 1 || angleIdx == 3)
                std::swap(renderW, renderH);

            colorscreen::tile_parameters tile;
            tile.pos.x = sx_unit * scale;
            tile.pos.y = sy_unit * scale;
            tile.step = 1.0 / scale;
            tile.width = renderW;
            tile.height = renderH;

            QImage image(renderW, renderH, QImage::Format_RGB888);
            image.setColorSpace(QColorSpace(QColorSpace::SRgb));
            tile.pixels = image.bits();
            tile.rowstride = image.bytesPerLine();
            tile.pixelbytes = 3;

            double outX = intersection.left();
            double outY = intersection.top();

            bool success = false;

            if (progress)
                progress->set_task(taskName, 1);

            if (progress && progress->cancel_requested()) {
                emit imageReady(reqId, QImage(), xOffset, yOffset, scale,
                                false);
                return;
            }
            colorscreen::sub_task task(progress.get());
            try {
                qCDebug(lcRenderSync)
                    << "  Task ID:" << reqId << " starts rendering tile";
                if (colorscreen::render_tile(
                        *m_scan, scrToImg, scrDetect, frameParams, renderType,
                        tile, transformer.coordinateSpace(), progress.get()))
                    success = true;
                qCDebug(lcRenderSync)
                    << "  Task ID:" << reqId << " finished rendering tile "
                    << success;
            } catch (...) {
                success = false;
            }

            if (success) {
                QTransform transform;
                bool transformed = false;

                if (angleIdx != 0) {
                    transform.rotate(angleIdx * 90);
                    transformed = true;
                }

                if (mirror) {
                    transform.scale(-1, 1);
                    transformed = true;
                }

                if (transformed) {
                    if (progress)
                        progress->set_task("transforming final image", 1);
                    if (!(progress && progress->cancel_requested()))
                        image = image.transformed(transform);
                    else {
                        qCDebug(lcRenderSync)
                            << "  Task ID:" << reqId
                            << " cancelled before transformation";
                        success = false;
                    }
                }
            }

            qCDebug(lcRenderSync)
                << "  Task ID:" << reqId << " finished with " << success;
            if (success)
                emit imageReady(reqId, image, outX, outY, scale, true);
            else
                emit imageReady(reqId, QImage(), xOffset, yOffset, scale,
                                false);
        });
}
