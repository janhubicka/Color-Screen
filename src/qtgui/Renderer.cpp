#include "Renderer.h"
#include <QImage>
#include <QtConcurrent>
#include "Logging.h"

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/progress-info.h"
#include "CoordinateTransformer.h"

#include "../libcolorscreen/render-tile.h"

Renderer::Renderer(std::shared_ptr<colorscreen::image_data> scan, 
                   const colorscreen::render_parameters &rparams,
                   const colorscreen::scr_to_img_parameters &scrToImg,
                   const colorscreen::scr_detect_parameters &scrDetect,
                   const colorscreen::render_type_parameters &renderType)
    : m_scan(scan), m_rparams(rparams), m_scrToImg(scrToImg), m_scrDetect(scrDetect), m_renderType(renderType)
{
}

Renderer::~Renderer() 
{
    // Wait for all running tasks to complete before destruction
    // This prevents use-after-free where a task tries to emit a signal on a destroyed Renderer
    for (const auto& future : m_activeFutures) {
        if (!future.isFinished()) {
            // Although tasks can be cancelled using progress->cancel(), the cancellation 
            // is cooperative and we must wait for the running task to return to ensure 
            // no use-after-free occurs.
            const_cast<QFuture<void>&>(future).waitForFinished();
        }
    }
}

void Renderer::updateParameters(const colorscreen::render_parameters &rparams,
                                const colorscreen::scr_to_img_parameters &scrToImg,
                                const colorscreen::scr_detect_parameters &scrDetect,
                                const colorscreen::render_type_parameters &renderType)
{
    QMutexLocker locker(&m_mutex);
    m_rparams = rparams;
    m_scrToImg = scrToImg;
    m_scrDetect = scrDetect;
    m_renderType = renderType;
}

void Renderer::render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                      colorscreen::render_parameters frameParams,
                      std::shared_ptr<colorscreen::progress_info> progress,
                      const char* taskName) 
{
    // Capture necessary state under lock
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::scr_detect_parameters scrDetect;
    colorscreen::render_type_parameters renderType;
    
    {
        QMutexLocker locker(&m_mutex);
        scrToImg = m_scrToImg;
        scrDetect = m_scrDetect;
        renderType = m_renderType;
    }

    // Run actual rendering in thread pool
    // Clean up finished futures to prevent unbounded growth
    m_activeFutures.removeIf([](const QFuture<void>& f) { return f.isFinished(); });

    // Run actual rendering in thread pool and track the future
    QFuture<void> future = QtConcurrent::run([this, reqId, xOffset, yOffset, scale, width, height, 
                       frameParams, progress, taskName,
                       scrToImg, scrDetect, renderType]() mutable {
        
        if (!m_scan || (!m_scan->data && !m_scan->rgbdata)) {
            emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
            return;
        }

        CoordinateTransformer transformer(m_scan.get(), frameParams);
        QSize transformedSize = transformer.getTransformedCropSize();

        QRectF visibleRect(0, 0, transformedSize.width(), transformedSize.height());
        QRectF requestRect(xOffset, yOffset, (double)width / scale, (double)height / scale);
        QRectF intersection = requestRect.intersected(visibleRect);

        if (intersection.isEmpty()) {
            emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
            return;
        }

        colorscreen::point_t p0 = transformer.transformedToScanCrop({intersection.left(), intersection.top()});
        colorscreen::point_t p1 = transformer.transformedToScanCrop({intersection.right(), intersection.bottom()});

        double sx_unit = std::min(p0.x, p1.x);
        double sy_unit = std::min(p0.y, p1.y);

        int tw = (int)std::round(intersection.width() * scale);
        int th = (int)std::round(intersection.height() * scale);

        if (tw <= 0) tw = 1;
        if (th <= 0) th = 1;

        int angleIdx = (int)(frameParams.scan_rotation) % 4;
        if (angleIdx < 0) angleIdx += 4;
        bool mirror = frameParams.scan_mirror;

        int renderW = tw;
        int renderH = th;
        if (angleIdx == 1 || angleIdx == 3) {
            std::swap(renderW, renderH);
        }

        colorscreen::tile_parameters tile;
        tile.pos.x = sx_unit * scale;
        tile.pos.y = sy_unit * scale;
        tile.step = 1.0 / scale;
        tile.width = renderW;
        tile.height = renderH;

        QImage image(renderW, renderH, QImage::Format_RGB888);
        tile.pixels = image.bits();
        tile.rowstride = image.bytesPerLine();
        tile.pixelbytes = 3;

        double outX = intersection.left();
        double outY = intersection.top();

        bool success = false;
        
        if (progress) {
           progress->set_task(taskName, 1);
        }

        colorscreen::sub_task task (progress.get ());
        try {
            qCDebug(lcRenderSync) << "  Task ID:" << reqId << " starts rendering tile";
            if (colorscreen::render_tile(*m_scan, scrToImg, scrDetect, frameParams, renderType, tile, progress.get()))
                success = true;
            qCDebug(lcRenderSync) << "  Task ID:" << reqId << " finished rendering tile " << success;
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
               if (progress) progress->set_task("transforming final image", 1);
               if (!(progress && progress->cancel_requested()))
                    image = image.transformed(transform);
               else
	         {
                   qCDebug(lcRenderSync) << "  Task ID:" << reqId << " cancelled before transformation";
                   success = false;
	         }
            }
        }
        
        qCDebug(lcRenderSync) << "  Task ID:" << reqId << " finished with " << success;
        if (success) {
            emit imageReady(reqId, image, outX, outY, scale, true);
        } else {
            emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
        }
    });

    m_activeFutures.append(future);
}
