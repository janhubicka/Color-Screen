#include "Renderer.h"
#include <QDebug>
#include <QImage>

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

Renderer::Renderer(std::shared_ptr<colorscreen::image_data> scan, 
                   const colorscreen::render_parameters &rparams,
                   const colorscreen::scr_to_img_parameters &scrToImg,
                   const colorscreen::scr_detect_parameters &scrDetect)
    : m_scan(scan), m_rparams(rparams), m_scrToImg(scrToImg), m_scrDetect(scrDetect)
{
}

Renderer::~Renderer() = default;


void Renderer::render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                      colorscreen::render_parameters frameParams,
                      std::shared_ptr<colorscreen::progress_info> progress)
{
    if (!m_scan || (!m_scan->data && !m_scan->rgbdata)) {
        // Nothing to render
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale);
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
    tile.width = width;
    tile.height = height;
    tile.rowstride = width * 3; // RGB
    tile.pixelbytes = 3;
    tile.pos.x = xOffset;
    tile.pos.y = yOffset;
    tile.step = 1.0 / scale;

    // Buffer
    QImage image(width, height, QImage::Format_RGB888);
    tile.pixels = image.bits();
    // QImage rowstride might differ from width*3 with padding (though unlikely for RGB888 usually, but safest to check)
    tile.rowstride = image.bytesPerLine();

    // Render type
    colorscreen::render_type_parameters rtparam;
    rtparam.type = colorscreen::render_type_original; // or interpolated? 
    // Just default for now... actually gtkgui sets this based on mode
    rtparam.color = true;
    
    // Antialias heuristic
    if (tile.step > 2.0) rtparam.antialias = true;
    else rtparam.antialias = false;

    // Render
    // Use m_rparams (updated from frameParams)
    bool success = colorscreen::render_tile(*m_scan, m_scrToImg, m_scrDetect, m_rparams, rtparam, tile, progress.get());

    if (success) {
        emit imageReady(reqId, image, xOffset, yOffset, scale);
    } else {
        emit imageReady(reqId, QImage(), xOffset, yOffset, scale);
    }
}
