#!/usr/bin/env python3
"""Apply the renderer handoff fix in a validation checkout.

This is temporary source transport for CI validation.  The real review branch is
constructed from the validated source files and does not retain this script.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

RENDERER_H = r'''#pragma once

#include <QObject>
#include <QImage>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <QFuture>
#include <QList>
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

class Renderer : public QObject
{
    Q_OBJECT
public:
    explicit Renderer(std::shared_ptr<colorscreen::image_data> scan,
                      const colorscreen::render_parameters &rparams,
                      const colorscreen::scr_to_img_parameters &scrToImg,
                      const colorscreen::scr_detect_parameters &scrDetect,
                      const colorscreen::render_type_parameters &renderType);
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
                       std::shared_ptr<colorscreen::progress_info> progress,
                       const char *taskName = nullptr);

    /** Thread-safe update of the renderer's cached parameter snapshot. */
    void updateParameters(const colorscreen::render_parameters &rparams,
                          const colorscreen::scr_to_img_parameters &scrToImg,
                          const colorscreen::scr_detect_parameters &scrDetect,
                          const colorscreen::render_type_parameters &renderType);

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
        std::shared_ptr<colorscreen::progress_info> progress;
        const char *taskName = nullptr;
    };

    std::shared_ptr<colorscreen::image_data> m_scan;

    // Protect both cached renderer state and the cross-thread request handoff.
    mutable std::mutex m_mutex;
    colorscreen::render_parameters m_rparams;
    colorscreen::scr_to_img_parameters m_scrToImg;
    colorscreen::scr_detect_parameters m_scrDetect;
    colorscreen::render_type_parameters m_renderType;
    std::unordered_map<int, RenderRequest> m_pendingRenders;

    // Futures of active rendering tasks; only touched by the renderer thread.
    QList<QFuture<void>> m_activeFutures;
};
'''

RENDERER_CPP = r'''#include "Renderer.h"
#include <QImage>
#include <QtConcurrent>
#include "Logging.h"
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
    // Wait for all running tasks to complete before destruction.
    for (const auto &future : m_activeFutures) {
        if (!future.isFinished()) {
            const_cast<QFuture<void> &>(future).waitForFinished();
        }
    }
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

void Renderer::render(int reqId)
{
    RenderRequest request;
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::scr_detect_parameters scrDetect;
    colorscreen::render_type_parameters renderType;

    {
        std::lock_guard<std::mutex> locker(m_mutex);
        auto it = m_pendingRenders.find(reqId);
        if (it == m_pendingRenders.end())
            return;

        request = std::move(it->second);
        m_pendingRenders.erase(it);
        scrToImg = m_scrToImg;
        scrDetect = m_scrDetect;
        renderType = m_renderType;
    }

    const double xOffset = request.xOffset;
    const double yOffset = request.yOffset;
    const double scale = request.scale;
    const int width = request.width;
    const int height = request.height;
    const int coordinateSpace = request.coordinateSpace;
    auto frameParams = std::move(request.frameParams);
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

    // Clean up finished futures to prevent unbounded growth.
    m_activeFutures.removeIf(
        [](const QFuture<void> &future) { return future.isFinished(); });

    QFuture<void> future = QtConcurrent::run(
        [this, reqId, xOffset, yOffset, scale, width, height, coordinateSpace,
         frameParams = std::move(frameParams), progress = std::move(progress),
         taskName, scrToImg, scrDetect, renderType]() mutable {
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

    m_activeFutures.append(future);
}
'''


def replace_once(relative, old, new):
    path = ROOT / relative
    text = path.read_text()
    if new in text:
        print(f"{relative}: already patched")
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative}: expected one match, found {count}")
    path.write_text(text.replace(old, new))
    print(f"{relative}: patched")


(ROOT / "src/qtgui/Renderer.h").write_text(RENDERER_H)
(ROOT / "src/qtgui/Renderer.cpp").write_text(RENDERER_CPP)

replace_once(
    "src/qtgui/ImageWidget.cpp",
    '''    bool result = QMetaObject::invokeMethod(m_renderer, "render", Qt::QueuedConnection,
                              Q_ARG(int, reqId),
                              Q_ARG(double, data.xOffset),
                              Q_ARG(double, data.yOffset),
                              Q_ARG(double, data.scale),
                              Q_ARG(int, data.w),
                              Q_ARG(int, data.h),
                              Q_ARG(int, (int)m_coordinateSpace),
                              Q_ARG(colorscreen::render_parameters, data.params),
                              Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
                              Q_ARG(const char*, "Rendering image"));
''',
    '''    bool result = m_renderer->enqueueRender(
        reqId, data.xOffset, data.yOffset, data.scale, data.w, data.h,
        (int)m_coordinateSpace, data.params, progress, "Rendering image");
''',
)

replace_once(
    "src/qtgui/ImageWidget.cpp",
    '''  if (m_renderer) {
    QMetaObject::invokeMethod(
        m_renderer, "updateParameters", Qt::QueuedConnection,
        Q_ARG(colorscreen::render_parameters,
              m_rparams ? *m_rparams : colorscreen::render_parameters()),
        Q_ARG(colorscreen::scr_to_img_parameters,
              m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters()),
        Q_ARG(colorscreen::scr_detect_parameters,
              m_scrDetect ? *m_scrDetect
                          : colorscreen::scr_detect_parameters()),
        Q_ARG(colorscreen::render_type_parameters,
              m_renderType ? *m_renderType
                           : colorscreen::render_type_parameters()));
  }
''',
    '''  if (m_renderer) {
    m_renderer->updateParameters(
        m_rparams ? *m_rparams : colorscreen::render_parameters(),
        m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters(),
        m_scrDetect ? *m_scrDetect : colorscreen::scr_detect_parameters(),
        m_renderType ? *m_renderType : colorscreen::render_type_parameters());
  }
''',
)

replace_once(
    "src/qtgui/NavigationView.cpp",
    '''    bool result = QMetaObject::invokeMethod(
      m_renderer, "render", Qt::QueuedConnection,
      Q_ARG(int, reqId),      
      Q_ARG(double, 0.0), 
      Q_ARG(double, 0.0), 
      Q_ARG(double, scale), Q_ARG(int, targetW), Q_ARG(int, targetH),
      Q_ARG(int, (int)m_coordinateSpace),
      Q_ARG(colorscreen::render_parameters, data.params),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
      Q_ARG(const char*, "Rendering navigation"));
''',
    '''    bool result = m_renderer->enqueueRender(
        reqId, 0.0, 0.0, scale, targetW, targetH, (int)m_coordinateSpace,
        data.params, progress, "Rendering navigation");
''',
)

replace_once(
    "src/qtgui/NavigationView.cpp",
    '''  if (m_renderer) {
    QMetaObject::invokeMethod(
        m_renderer, "updateParameters", Qt::QueuedConnection,
        Q_ARG(colorscreen::render_parameters, *m_rparams),
        Q_ARG(colorscreen::scr_to_img_parameters,
              m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters()),
        Q_ARG(colorscreen::scr_detect_parameters,
              m_scrDetect ? *m_scrDetect
                          : colorscreen::scr_detect_parameters()),
        Q_ARG(colorscreen::render_type_parameters, m_renderType));
  }
''',
    '''  if (m_renderer) {
    m_renderer->updateParameters(
        *m_rparams,
        m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters(),
        m_scrDetect ? *m_scrDetect : colorscreen::scr_detect_parameters(),
        m_renderType);
  }
''',
)
