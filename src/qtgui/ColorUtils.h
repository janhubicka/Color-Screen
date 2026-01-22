#ifndef COLORUTILS_H
#define COLORUTILS_H

#include <QColor>
#include <algorithm>
#include <cmath>

// Shared utility for heatmap color calculation
// Maps displacement error to a color gradient (green -> yellow -> red)
// displacement: the displacement magnitude in pixels/units
// tolerance: the threshold at which color should be fully red
inline QColor getHeatMapColor(double displacement, double tolerance) {
    if (tolerance <= 0.0) tolerance = 1.0; // Avoid division by zero
    
    // Calculate error ratio
    double error_ratio = displacement / tolerance;
    
    // Hue: 120 (green) -> 60 (yellow) -> 0 (red)
    // Clamp at 2.0x tolerance for full red
    double hue = 120.0 - std::min(error_ratio, 2.0) * 60.0;
    if (hue < 0) hue = 0;
    
    return QColor::fromHslF(hue / 360.0, 1.0, 0.5);
}

#endif // COLORUTILS_H
