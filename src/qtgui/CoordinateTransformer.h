#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QSize>

class CoordinateTransformer {
public:
    CoordinateTransformer(const colorscreen::image_data* scan, const colorscreen::render_parameters& params);

    // Scan (Image) Coordinate -> Transformed (Screen/View normalized) Coordinate
    // Returns coordinates in the range [0, transformedSize]
    colorscreen::point_t scanToTransformed(colorscreen::point_t scanPt) const;

    // Transformed (Screen/View normalized) Coordinate -> Scan (Image) Coordinate
    colorscreen::point_t transformedToScan(colorscreen::point_t transformedPt) const;

    // Get the dimensions of the transformed image
    QSize getTransformedSize() const;

    // Get scan dimensions
    QSize getScanSize() const;

private:
    int m_scanWidth = 0;
    int m_scanHeight = 0;
    bool m_mirror = false;
    int m_rotation = 0; // 0, 1, 2, 3 corresponding to 0, 90, 180, 270 degrees
};
