#include "Renderer.h"
#include <QImage>

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
                      std::shared_ptr<colorscreen::progress_info> progress,
                      const char* taskName) 
{
    if (!m_scan || (!m_scan->data && !m_scan->rgbdata)) {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
        return;
    }
    
    // Use passed params (which should be current from GUI thread)
    m_rparams = frameParams; 

    CoordinateTransformer transformer(m_scan.get(), m_rparams);
    
    // View coordinates (xOffset, yOffset, width, height) are in TRANSORMED space relative to Viewport
    colorscreen::point_t p0 = transformer.transformedToScan({xOffset, yOffset});
    colorscreen::point_t p1 = transformer.transformedToScan({xOffset + (double)width / scale, yOffset + (double)height / scale});
    
    // The rendered scan rect is the bounding box of these points
    double sx_unit = std::min(p0.x, p1.x);
    double sy_unit = std::min(p0.y, p1.y);
    
    // Scan dimensions for buffer creation logic (swapped if 90/270 rot)
    int angleIdx = (int)(m_rparams.scan_rotation) % 4;
    if (angleIdx < 0) angleIdx += 4;
    bool mirror = m_rparams.scan_mirror;

    int tw = width;
    int th = height;
    
    if (angleIdx == 1 || angleIdx == 3) {
        std::swap(tw, th);
    }
    
    colorscreen::tile_parameters tile;
    tile.pos.x = sx_unit * scale;
    tile.pos.y = sy_unit * scale;
    tile.step = 1.0 / scale;
    tile.width = tw;
    tile.height = th;

    // Buffer
    QImage image(tw, th, QImage::Format_RGB888);
    tile.pixels = image.bits();
    tile.rowstride = image.bytesPerLine();
    tile.pixelbytes = 3; // RGB888

    // Call the actual rendering function
    bool success = false;
    colorscreen::render_type_parameters rtparams = m_renderType;
    
    if (progress) {
       progress->set_task(taskName, 1);
    }

    colorscreen::sub_task task (progress.get ());
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
    
    // Rotate and Mirror if needed to match requested view
    if (success) {
        QTransform transform;
        bool transformed = false;
        
        // 1. Rotate result to match View (un-mirrored)
        if (angleIdx != 0) {
           transform.rotate(angleIdx * 90); 
           transformed = true;
        }
        
        // 2. Mirror result if needed
        if (mirror) {
           transform.scale(-1, 1);
           transformed = true;
        }
        
        if (transformed) {
             image = image.transformed(transform);
        }
    }
    
    if (success) {
        emit imageReady(reqId, image, xOffset, yOffset, scale, true);
    } else {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
    }
}
