#include "CoordinateTransformer.h"
#include <algorithm>

CoordinateTransformer::CoordinateTransformer(const colorscreen::image_data* scan, const colorscreen::render_parameters& params) {
    if (scan) {
        m_scanWidth = scan->width;
        m_scanHeight = scan->height;
    }
    m_mirror = params.scan_mirror;
    m_rotation = (int)params.scan_rotation % 4;
    if (m_rotation < 0) m_rotation += 4;
}

QSize CoordinateTransformer::getScanSize() const {
    return QSize(m_scanWidth, m_scanHeight);
}

QSize CoordinateTransformer::getTransformedSize() const {
    if (m_rotation == 1 || m_rotation == 3) {
        return QSize(m_scanHeight, m_scanWidth);
    }
    return QSize(m_scanWidth, m_scanHeight);
}

colorscreen::point_t CoordinateTransformer::scanToTransformed(colorscreen::point_t scanPt) const {
    if (m_scanWidth == 0 || m_scanHeight == 0) return scanPt;

    // 1. Normalize
    double px = scanPt.x / m_scanWidth;
    double py = scanPt.y / m_scanHeight;
    double u = 0, v = 0;

    // 2. Mirror (Forward)
    if (m_mirror) px = 1.0 - px;

    // 3. Rotate (Forward)
    if (m_rotation == 0) { u = px; v = py; }
    else if (m_rotation == 1) { u = 1.0 - py; v = px; } // 90 CW
    else if (m_rotation == 2) { u = 1.0 - px; v = 1.0 - py; } // 180
    else if (m_rotation == 3) { u = py; v = 1.0 - px; } // 270 CW

    // 4. Denormalize to Transformed Size
    QSize ts = getTransformedSize();
    return { u * ts.width(), v * ts.height() };
}

colorscreen::point_t CoordinateTransformer::transformedToScan(colorscreen::point_t transformedPt) const {
    QSize ts = getTransformedSize();
    if (ts.width() == 0 || ts.height() == 0) return transformedPt;

    // 1. Normalize Transformed Coordinate
    double u = transformedPt.x / ts.width();
    double v = transformedPt.y / ts.height();

    // 2. Un-Rotate
    double px = 0, py = 0;
    if (m_rotation == 0) { px = u; py = v; }
    else if (m_rotation == 1) { px = v; py = 1.0 - u; }
    else if (m_rotation == 2) { px = 1.0 - u; py = 1.0 - v; }
    else if (m_rotation == 3) { px = 1.0 - v; py = u; }

    // 3. Un-Mirror
    if (m_mirror) px = 1.0 - px;

    // 4. Denormalize to Scan Size
    return { px * m_scanWidth, py * m_scanHeight };
}
