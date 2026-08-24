#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <QSize>
#include <memory>

/** Map between scan coordinates and the view's current presentation canvas.

    Scan-coordinate views layer scan crop, quarter-turn rotation and scan
    mirroring over raw pixels.  Final-coordinate views use libcolorscreen's
    final plane directly: continuous final_rotation/final_mirror are already
    part of that mapping, so scan presentation transforms must not be applied
    again.  Public interaction methods always convert back to scan coordinates.

    Final-coordinate map construction is cached internally.  In particular,
    scr_to_img::get_final_range() walks the image boundary and must not be
    recomputed by per-point GUI coordinate conversions.  Cache identity depends
    only on the scan dimensions and scr_to_img_parameters; scan presentation and
    other render parameters do not invalidate final geometry. */
class CoordinateTransformer {
public:
    CoordinateTransformer(
        const colorscreen::image_data* scan,
        const colorscreen::render_parameters& params,
        const colorscreen::scr_to_img_parameters* scrToImg = nullptr,
        colorscreen::render_coordinate_space coordinates =
            colorscreen::render_scan_coordinates);

    colorscreen::int_image_area getCrop() const;

    colorscreen::point_t scanToTransformed(colorscreen::point_t scanPt) const;
    colorscreen::point_t transformedToScan(colorscreen::point_t transformedPt) const;
    colorscreen::point_t scanToTransformedCrop(colorscreen::point_t scanPt) const;
    colorscreen::point_t transformedToScanCrop(colorscreen::point_t transformedPt) const;

    /** Convert transformed-crop coordinates to render_tile coordinates. */
    colorscreen::point_t transformedToRenderCrop(colorscreen::point_t transformedPt) const;
    /** Convert render_tile coordinates to transformed-crop coordinates. */
    colorscreen::point_t renderToTransformedCrop(colorscreen::point_t renderPt) const;

    QSize getTransformedSize() const;
    QSize getTransformedCropSize() const;
    QSize getScanSize() const;

    /** Range in render_tile coordinates occupied by the active crop. */
    colorscreen::int_image_area getRenderCrop() const;
    colorscreen::render_coordinate_space coordinateSpace() const {
        return m_coordinates;
    }
    bool finalCoordinatesAvailable() const { return m_finalAvailable; }

private:
    colorscreen::point_t scanToRender(colorscreen::point_t scanPt) const;
    colorscreen::point_t renderToScan(colorscreen::point_t renderPt) const;
    colorscreen::point_t baseToTransformed(colorscreen::point_t p,
                                            double baseWidth,
                                            double baseHeight) const;
    colorscreen::point_t transformedToBase(colorscreen::point_t p,
                                            double baseWidth,
                                            double baseHeight) const;
    QSize transformedSize(double width, double height) const;

    int m_scanWidth = 0;
    int m_scanHeight = 0;
    bool m_mirror = false;
    int m_rotation = 0;
    colorscreen::int_image_area m_scanCrop;
    colorscreen::render_coordinate_space m_coordinates =
        colorscreen::render_scan_coordinates;
    bool m_finalAvailable = false;
    std::shared_ptr<const colorscreen::scr_to_img> m_map;
    colorscreen::int_image_area m_finalRange;
};