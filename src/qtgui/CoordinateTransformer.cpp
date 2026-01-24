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
    m_scanCrop = params.scan_crop;
}

colorscreen::int_image_area CoordinateTransformer::getCrop() const {
    colorscreen::int_image_area fullRect(0, 0, m_scanWidth, m_scanHeight);
    if (!m_scanCrop.set) return fullRect;
    
    // We need to const_cast or trust rparams if we had them.
    // Actually, get_scan_crop is in render_parameters. 
    // Let's implement intersection logic here to avoid needing to store full rparams.
    colorscreen::int_image_area crop(m_scanCrop.x, m_scanCrop.y, m_scanCrop.width, m_scanCrop.height);
    
    // Intersect manually to avoid dependency loop if needed, but it's already in base.h/render-parameters.h
    // Let's use the intersect logic from render_parameters::get_scan_crop
    
    colorscreen::int_image_area ret = crop;
    if (ret.x < fullRect.x) { ret.width -= fullRect.x - ret.x; ret.x = fullRect.x; }
    if (ret.y < fullRect.y) { ret.height -= fullRect.y - ret.y; ret.y = fullRect.y; }
    if (ret.x + ret.width > fullRect.x + fullRect.width) ret.width = fullRect.x + fullRect.width - ret.x;
    if (ret.y + ret.height > fullRect.y + fullRect.height) ret.height = fullRect.y + fullRect.height - ret.y;
    ret.width = std::max(ret.width, 0);
    ret.height = std::max(ret.height, 0);
    
    if (ret.width <= 0 || ret.height <= 0) return fullRect;
    return ret;
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

QSize CoordinateTransformer::getTransformedCropSize() const {
    colorscreen::int_image_area crop = getCrop();
    if (m_rotation == 1 || m_rotation == 3) {
        return QSize(crop.height, crop.width);
    }
    return QSize(crop.width, crop.height);
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

colorscreen::point_t CoordinateTransformer::scanToTransformedCrop(colorscreen::point_t scanPt) const {
    colorscreen::int_image_area crop = getCrop();
    QSize ts = getTransformedCropSize();
    if (ts.width() <= 0 || ts.height() <= 0) return {0,0};

    // 1. Normalize relative to CROP
    double px = (scanPt.x - crop.x) / crop.width;
    double py = (scanPt.y - crop.y) / crop.height;
    double u = 0, v = 0;

    // 2. Mirror (Forward)
    if (m_mirror) px = 1.0 - px;

    // 3. Rotate (Forward)
    if (m_rotation == 0) { u = px; v = py; }
    else if (m_rotation == 1) { u = 1.0 - py; v = px; }
    else if (m_rotation == 2) { u = 1.0 - px; v = 1.0 - py; }
    else if (m_rotation == 3) { u = py; v = 1.0 - px; }

    // 4. Denormalize
    return { u * ts.width(), v * ts.height() };
}

colorscreen::point_t CoordinateTransformer::transformedToScanCrop(colorscreen::point_t trPt) const {
    QSize ts = getTransformedCropSize();
    colorscreen::int_image_area crop = getCrop();
    if (ts.width() <= 0 || ts.height() <= 0) return { (double)crop.x, (double)crop.y };

    // 1. Normalize
    double u = trPt.x / ts.width();
    double v = trPt.y / ts.height();

    // 2. Un-Rotate
    double px = 0, py = 0;
    if (m_rotation == 0) { px = u; py = v; }
    else if (m_rotation == 1) { px = v; py = 1.0 - u; }
    else if (m_rotation == 2) { px = 1.0 - u; py = 1.0 - v; }
    else if (m_rotation == 3) { px = 1.0 - v; py = u; }

    // 3. Un-Mirror
    if (m_mirror) px = 1.0 - px;

    // 4. Denormalize to CROP rect
    return { crop.x + px * crop.width, crop.y + py * crop.height };
}
