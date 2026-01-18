#include "Renderer.h"
#include <QImage>

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

#include "../libcolorscreen/render-tile.h"

Renderer::Renderer(std::shared_ptr<colorscreen::image_data> scan, 
                   const colorscreen::render_parameters &rparams,
                   const colorscreen::scr_to_img_parameters &scrToImg,
                   const colorscreen::scr_detect_parameters &scrDetect,
                   const colorscreen::render_type_parameters &renderType)
    : m_scan(scan), m_rparams(rparams), m_scrToImg(scrToImg), m_scrDetect(scrDetect), m_renderType(renderType)
{
}

Renderer::~Renderer() = default;

void Renderer::updateParameters(const colorscreen::render_parameters &rparams,
                                const colorscreen::scr_to_img_parameters &scrToImg,
                                const colorscreen::scr_detect_parameters &scrDetect,
                                const colorscreen::render_type_parameters &renderType)
{
    m_rparams = rparams;
    m_scrToImg = scrToImg;
    m_scrDetect = scrDetect;
    m_renderType = renderType;
}



void Renderer::render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                      colorscreen::render_parameters frameParams,
                      std::shared_ptr<colorscreen::progress_info> progress)
{
    if (!m_scan || (!m_scan->data && !m_scan->rgbdata)) {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
        return;
    }
    
    
    // Use passed params (which should be current from GUI thread)
    m_rparams = frameParams; 
    
    // Setup tile info
    colorscreen::tile_parameters tile;
    // Rotation handling
    int angle = (int)m_scrToImg.final_rotation % 360;
    if (angle < 0) angle += 360;

    double step = 1.0 / scale;
    double tx = xOffset * scale;
    double ty = yOffset * scale;
    double sw = (double)m_scan->width * scale;
    double sh = (double)m_scan->height * scale;
    int tw = width;
    int th = height;
    double sx, sy;

    if (angle == 90) { // CW
        sx = yOffset;
        sy = (double)m_scan->height - (xOffset + width / scale);
        std::swap(tw, th);
    } else if (angle == 180) {
        sx = (double)m_scan->width - (xOffset + width / scale);
        sy = (double)m_scan->height - (yOffset + height / scale);
    } else if (angle == 270) {
        sx = (double)m_scan->width - (yOffset + height / scale);
        sy = xOffset;
        std::swap(tw, th);
    } else {
        sx = xOffset;
        sy = yOffset;
    }

    tile.pos.x = sx * scale;
    tile.pos.y = sy * scale;
    tile.step = step;

    // Buffer
    QImage image(tw, th, QImage::Format_RGB888);
    tile.pixels = image.bits();
    tile.rowstride = image.bytesPerLine();
    tile.pixelbytes = 3; // RGB888
    tile.width = tw;
    tile.height = th;

    // Call the actual rendering function
    bool success = false;
    colorscreen::render_type_parameters rtparams = m_renderType;
    
    try {
        colorscreen::render_tile(*m_scan, m_scrToImg, m_scrDetect, m_rparams, rtparams, tile, progress.get());
        
        if (progress && progress->cancelled()) {
            success = false;
        } else {
            success = true;
        }
    } catch (const std::exception& e) {
        success = false;
    }
    
    // Rotate if needed
    if (success && angle != 0) {
        QTransform transform;
        transform.rotate(-angle);  // CCW to undo CW rotation in coords
        image = image.transformed(transform);
    }
    
    if (success) {
        emit imageReady(reqId, image, xOffset, yOffset, scale, true);
    } else {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
    }
}


