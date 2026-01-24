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
                      std::shared_ptr<colorscreen::progress_info> progress,
                      const char* taskName) // Changed to const char*
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
    int angleIdx = (int)(m_rparams.scan_rotation) % 4;
    if (angleIdx < 0) angleIdx += 4;
    bool mirror = m_rparams.scan_mirror;

    double step = 1.0 / scale;
    // Physical dimensions of the full view in scan units
    int full_w = m_scan->width;
    int full_h = m_scan->height;
    if (angleIdx == 1 || angleIdx == 3) {
        std::swap(full_w, full_h);
    }

    // View coordinates (xOffset, yOffset, width, height) are in ROTATED + MIRRORED space.
    // We need to map the requested view rectangle back to original scan coordinates (sx, sy).
    
    // Normalized View-to-Scan mapping (consistent with DeformationChartWidget and ImageWidget)
    auto mapToScan = [&](double x_view, double y_view, colorscreen::point_t &p_scan) {
        double u = x_view / full_w;
        double v = y_view / full_h;
        
        // 1. Un-Mirror
        if (mirror) u = 1.0 - u;
        
        // 2. Un-Rotate
        
        if (angleIdx == 0) {
            p_scan.x = u * m_scan->width;
            p_scan.y = v * m_scan->height;
        } else if (angleIdx == 1) { // 90 CW: View (0,0) -> Scan (0, H)
            p_scan.x = v * m_scan->width;
            p_scan.y = (1.0 - u) * m_scan->height;
        } else if (angleIdx == 2) { // 180: View (0,0) -> Scan (W, H)
            p_scan.x = (1.0 - u) * m_scan->width;
            p_scan.y = (1.0 - v) * m_scan->height;
        } else if (angleIdx == 3) { // 270 CW: View (0,0) -> Scan (W, 0)
            p_scan.x = (1.0 - v) * m_scan->width;
            p_scan.y = u * m_scan->height;
        }
    };

    colorscreen::point_t p0, p1;
    mapToScan(xOffset, yOffset, p0);
    mapToScan(xOffset + (double)width / scale, yOffset + (double)height / scale, p1);
    
    // The rendered scan rect is the bounding box of these points
    double sx_unit = std::min(p0.x, p1.x);
    double sy_unit = std::min(p0.y, p1.y);
    
    // Prep output buffer logic
    int tw = width;  
    int th = height;

    if (angleIdx == 1 || angleIdx == 3) {
        std::swap(tw, th);
    }

    tile.pos.x = sx_unit * scale;
    tile.pos.y = sy_unit * scale;
    tile.step = step;
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
        // We rendered "Upright" scan tile.
        // We need to rotate it by 'angle'.
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


