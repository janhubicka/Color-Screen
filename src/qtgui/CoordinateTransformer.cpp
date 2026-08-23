#include "CoordinateTransformer.h"
#include <algorithm>
#include <cmath>

CoordinateTransformer::CoordinateTransformer(
    const colorscreen::image_data* scan,
    const colorscreen::render_parameters& params,
    const colorscreen::scr_to_img_parameters* scrToImg,
    colorscreen::render_coordinate_space coordinates) {
    if (scan) {
        m_scanWidth = scan->width;
        m_scanHeight = scan->height;
    }
    m_scanCrop = params.get_scan_crop(m_scanWidth, m_scanHeight);
    m_coordinates = coordinates;

    /* scan_rotation, scan_mirror and scan_crop are presentation properties of
       the digitized scan.  A final-coordinate canvas already has its own
       continuous orientation in scr_to_img_parameters.  */
    if (coordinates == colorscreen::render_scan_coordinates) {
        m_mirror = params.scan_mirror;
        m_rotation = (int)params.scan_rotation % 4;
        if (m_rotation < 0) m_rotation += 4;
    }
    if (coordinates == colorscreen::render_final_coordinates && scan &&
        scrToImg && scrToImg->type != colorscreen::Random && !scan->stitch) {
        m_map = std::make_shared<colorscreen::scr_to_img>();
        if (m_map->set_parameters(*scrToImg, *scan)) {
            m_finalRange = colorscreen::int_image_area(
                m_map->get_final_range(m_scanWidth, m_scanHeight));
            m_finalAvailable = !m_finalRange.empty_p();
        }
        if (!m_finalAvailable) {
            m_map.reset();
            m_coordinates = colorscreen::render_scan_coordinates;
        }
    } else if (coordinates == colorscreen::render_final_coordinates && scan &&
               scan->stitch) {
        /* Stitched image_data already exposes the common final viewport as its
           only meaningful canvas. */
        m_finalAvailable = true;
    }
}

colorscreen::int_image_area CoordinateTransformer::getCrop() const {
    return m_scanCrop;
}

QSize CoordinateTransformer::getScanSize() const {
    return QSize(m_scanWidth, m_scanHeight);
}

QSize CoordinateTransformer::transformedSize(double width, double height) const {
    int w = std::max(0, (int)std::ceil(width));
    int h = std::max(0, (int)std::ceil(height));
    if (m_rotation == 1 || m_rotation == 3)
        return QSize(h, w);
    return QSize(w, h);
}

QSize CoordinateTransformer::getTransformedSize() const {
    if (m_coordinates == colorscreen::render_final_coordinates) {
        if (m_map)
            return transformedSize(m_finalRange.width, m_finalRange.height);
        return transformedSize(m_scanWidth, m_scanHeight);
    }
    return transformedSize(m_scanWidth, m_scanHeight);
}

colorscreen::int_image_area CoordinateTransformer::getRenderCrop() const {
    if (m_coordinates == colorscreen::render_final_coordinates) {
        if (m_map)
            return colorscreen::int_image_area(0, 0, m_finalRange.width,
                                               m_finalRange.height);
        /* Stitched images already expose their final viewport as image_data. */
        return colorscreen::int_image_area(0, 0, m_scanWidth, m_scanHeight);
    }
    return m_scanCrop;
}

QSize CoordinateTransformer::getTransformedCropSize() const {
    colorscreen::int_image_area crop = getRenderCrop();
    return transformedSize(crop.width, crop.height);
}

colorscreen::point_t CoordinateTransformer::baseToTransformed(
    colorscreen::point_t p, double baseWidth, double baseHeight) const {
    if (baseWidth <= 0 || baseHeight <= 0) return p;
    double px = p.x / baseWidth;
    double py = p.y / baseHeight;
    if (m_mirror) px = 1.0 - px;
    double u = 0, v = 0;
    if (m_rotation == 0) { u = px; v = py; }
    else if (m_rotation == 1) { u = 1.0 - py; v = px; }
    else if (m_rotation == 2) { u = 1.0 - px; v = 1.0 - py; }
    else { u = py; v = 1.0 - px; }
    QSize ts = transformedSize(baseWidth, baseHeight);
    return {u * ts.width(), v * ts.height()};
}

colorscreen::point_t CoordinateTransformer::transformedToBase(
    colorscreen::point_t p, double baseWidth, double baseHeight) const {
    QSize ts = transformedSize(baseWidth, baseHeight);
    if (ts.width() <= 0 || ts.height() <= 0) return p;
    double u = p.x / ts.width();
    double v = p.y / ts.height();
    double px = 0, py = 0;
    if (m_rotation == 0) { px = u; py = v; }
    else if (m_rotation == 1) { px = v; py = 1.0 - u; }
    else if (m_rotation == 2) { px = 1.0 - u; py = 1.0 - v; }
    else { px = 1.0 - v; py = u; }
    if (m_mirror) px = 1.0 - px;
    return {px * baseWidth, py * baseHeight};
}

colorscreen::point_t CoordinateTransformer::scanToRender(
    colorscreen::point_t scanPt) const {
    if (m_coordinates != colorscreen::render_final_coordinates || !m_map)
        return scanPt;
    colorscreen::point_t p = m_map->img_to_final(scanPt);
    p.x -= m_finalRange.x;
    p.y -= m_finalRange.y;
    return p;
}

colorscreen::point_t CoordinateTransformer::renderToScan(
    colorscreen::point_t renderPt) const {
    if (m_coordinates != colorscreen::render_final_coordinates || !m_map)
        return renderPt;
    renderPt.x += m_finalRange.x;
    renderPt.y += m_finalRange.y;
    return m_map->final_to_img(renderPt);
}

colorscreen::point_t CoordinateTransformer::scanToTransformed(
    colorscreen::point_t scanPt) const {
    colorscreen::point_t render = scanToRender(scanPt);
    QSize base = (m_coordinates == colorscreen::render_final_coordinates && m_map)
        ? QSize(m_finalRange.width, m_finalRange.height)
        : QSize(m_scanWidth, m_scanHeight);
    return baseToTransformed(render, base.width(), base.height());
}

colorscreen::point_t CoordinateTransformer::transformedToScan(
    colorscreen::point_t transformedPt) const {
    QSize base = (m_coordinates == colorscreen::render_final_coordinates && m_map)
        ? QSize(m_finalRange.width, m_finalRange.height)
        : QSize(m_scanWidth, m_scanHeight);
    return renderToScan(transformedToBase(transformedPt,
                                          base.width(), base.height()));
}

colorscreen::point_t CoordinateTransformer::scanToTransformedCrop(
    colorscreen::point_t scanPt) const {
    colorscreen::int_image_area crop = getRenderCrop();
    colorscreen::point_t render = scanToRender(scanPt);
    render.x -= crop.x;
    render.y -= crop.y;
    return baseToTransformed(render, crop.width, crop.height);
}

colorscreen::point_t CoordinateTransformer::transformedToScanCrop(
    colorscreen::point_t transformedPt) const {
    return renderToScan(transformedToRenderCrop(transformedPt));
}

colorscreen::point_t CoordinateTransformer::transformedToRenderCrop(
    colorscreen::point_t transformedPt) const {
    colorscreen::int_image_area crop = getRenderCrop();
    colorscreen::point_t p = transformedToBase(transformedPt,
                                                crop.width, crop.height);
    p.x += crop.x;
    p.y += crop.y;
    return p;
}

colorscreen::point_t CoordinateTransformer::renderToTransformedCrop(
    colorscreen::point_t renderPt) const {
    colorscreen::int_image_area crop = getRenderCrop();
    renderPt.x -= crop.x;
    renderPt.y -= crop.y;
    return baseToTransformed(renderPt, crop.width, crop.height);
}
