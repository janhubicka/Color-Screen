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


void Renderer::render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                      colorscreen::render_parameters frameParams,
                      std::shared_ptr<colorscreen::progress_info> progress)
{
    if (!m_scan || (!m_scan->data && !m_scan->rgbdata)) {
        // Nothing to render
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, true);
        return;
    }

    // Use passed params (which should be current from GUI thread)
    // Or update member?
    // User requested "Make it use local copy... This prevents race conditions".
    // Passing by value to `render` ensures the parameters used for *this* rendering are consistent.
    // We can also update `m_rparams` if we want to persist state, but `render` is stateless w.r.t params if passed in.
    // Let's use `frameParams` for rendering.
    
    // We update m_rparams just in case, though strictly not needed if we use frameParams.
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
    // QImage rowstride might differ from width*3 with padding (though unlikely for RGB888 usually, but safest to check)
    tile.rowstride = image.bytesPerLine();

    // Render type
    // Use member render type params
    // We need a non-const reference for render_tile? No, render_tile takes reference but likely non-const?
    // Let's check: render_tile(..., render_type_parameters &rtparam, ...)
    // So we need to copy it to modify or just pass it if it's not modified.
    // It's passed as non-const ref, so we should make a copy to be safe thread-wise if it modifies it.
    colorscreen::render_type_parameters rtparams = m_renderType;

    // Render
    // Use m_rparams (updated from frameParams)
    bool success = colorscreen::render_tile(*m_scan, m_scrToImg, m_scrDetect, m_rparams, rtparams, tile, progress.get());
    
    // Check if render was cancelled
    if (progress && progress->cancelled()) {
        // Don't emit anything if cancelled
        return;
    }
    
    // Rotate result if successful
    if (success && angle != 0) {
        QTransform trans;
        trans.rotate(angle);
        image = image.transformed(trans);
    }

    // Emit result with success status
    if (success) {
        emit imageReady(reqId, image, xOffset, yOffset, scale, true);
    } else {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale, false);
    }
}
