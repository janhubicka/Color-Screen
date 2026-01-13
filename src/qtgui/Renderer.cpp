#include "Renderer.h"
#include <QDebug>
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
    // Normalize to 0, 90, 180, 270
    // Round to nearest 90? .par files might have arbitrary, but UI does 90.
    // Assuming 90 deg steps for now as per UI.
    
    // Transform request coordinates (Screen) to Scan coordinates (Pre-rotation)
    // Screen Tile: (xOffset, yOffset) size (width, height)
    // Scan Dimensions (Scaled): sw = m_scan->width * scale, sh = m_scan->height * scale
    
    double sw = (double)m_scan->width / (1.0 / scale); // tile.step = 1/scale. So width * scale.
    double sh = (double)m_scan->height / (1.0 / scale);
    
    double tx = xOffset;
    double ty = yOffset;
    int tw = width;
    int th = height;
    
    if (angle == 90) { // CW
        // Screen X -> Scan Y
        // Screen Y -> Scan Inv X
        // (x, y) -> (y, sh - x - w) NOT (y, sw...)?
        // Wait, if 90 CW: Top-Left View (0,0) -> Bottom-Left Scaled Scan (0, H)? No.
        // View(0,0) -> Scan(0,0) is 270 CW?
        // 90 CW: View(0,0) -> Scan(0,0) ??
        // Let's use QTransform logic to visualize.
        // QTransform().rotate(90) maps (1,0) to (0,1). (x axis to y axis).
        // (0,1) to (-1,0). (y axis to -x axis).
        // So x -> y, y -> -x.
        // Origin shift required.
        // (x, y) -> (y, H_scan - x). (Assuming H_scan is the bounds in Y).
        
        // So tile pos:
        double newX = ty;
        double newY = sh - tx - width; // Note: -width because we want top-left of scan tile.
        
        tx = newX;
        ty = newY;
        std::swap(tw, th);
        
    } else if (angle == 180) {
        // (x, y) -> (W-x, H-y)
        tx = sw - tx - width;
        ty = sh - ty - height;
    } else if (angle == 270) {
        // (x, y) -> (-y, x) -> (W-y, x)
        double newX = sw - ty - height;
        double newY = tx;
        
        tx = newX;
        ty = newY;
        std::swap(tw, th);
    }
    
    tile.width = tw;
    tile.height = th;
    tile.rowstride = tw * 3;
    tile.pixelbytes = 3;
    tile.pos.x = tx;
    tile.pos.y = ty;
    tile.step = 1.0 / scale;

    // Buffer
    QImage image(tw, th, QImage::Format_RGB888);
    tile.pixels = image.bits();
    // QImage rowstride might differ from width*3 with padding (though unlikely for RGB888 usually,    
    tile.pixels = image.bits();
    tile.rowstride = image.bytesPerLine();

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
