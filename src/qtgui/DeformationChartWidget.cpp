#include "DeformationChartWidget.h"
#include "ColorUtils.h"
#include "CoordinateTransformer.h"
#include "../libcolorscreen/include/color.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <cmath>

DeformationChartWidget::DeformationChartWidget(QWidget *parent)
    : QWidget(parent)
{
    // Create main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(5);
    
    // Chart area will be drawn in paintEvent
    // Add stretch to push slider to bottom
    mainLayout->addStretch();
    
    // Create slider layout
    QHBoxLayout *sliderLayout = new QHBoxLayout();
    m_sliderLabel = new QLabel("Exaggerate: 1.0x");
    m_exaggerateSlider = new QSlider(Qt::Horizontal);
    m_exaggerateSlider->setRange(0, 10000); // 0-10000 for fine control with large range
    // Connect slider to update label and trigger repaint
    connect(m_exaggerateSlider, &QSlider::valueChanged, this, [this](int value) {
        float factor = getExaggerationFactor();
        m_sliderLabel->setText(QString("Exaggerate: %1x").arg(factor, 0, 'f', 1));
        update();
    });

    m_exaggerateSlider->setValue(5000); // 5000 = 100x (sqrt of range)
    m_exaggerateSlider->setEnabled(true);
    
    sliderLayout->addWidget(m_sliderLabel);
    sliderLayout->addWidget(m_exaggerateSlider);
    
    mainLayout->addLayout(sliderLayout);
    
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void DeformationChartWidget::setDeformationData(
    const colorscreen::scr_to_img_parameters &deformed,
    const colorscreen::scr_to_img_parameters &undeformed,
    int viewWidth, int viewHeight, bool mirror, int rotation,
    int offsetX, int offsetY, int fullWidth, int fullHeight)
{
    m_deformedParams = deformed;
    m_undeformedParams = undeformed;
    m_scanWidth = viewWidth;
    m_scanHeight = viewHeight;
    m_mirror = mirror;
    m_rotation = rotation;
    m_offsetX = offsetX;
    m_offsetY = offsetY;
    m_fullScanWidth = (fullWidth > 0) ? fullWidth : viewWidth;
    m_fullScanHeight = (fullHeight > 0) ? fullHeight : viewHeight;
    m_hasData = (viewWidth > 0 && viewHeight > 0);
    updateGeometry(); // Trigger layout update
    update();
}

void DeformationChartWidget::setHeatmapTolerance(double tolerance)
{
    m_heatmapTolerance = tolerance;
    update();
}

void DeformationChartWidget::clear()
{
    m_hasData = false;
    m_scanWidth = 0;
    m_scanHeight = 0;
    m_offsetX = 0;
    m_offsetY = 0;
    updateGeometry();
    update();
}

QSize DeformationChartWidget::sizeHint() const
{
    if (m_hasData && m_scanWidth > 0 && m_scanHeight > 0) {
        return QSize(400, heightForWidth(400));
    }
    return QSize(400, 350);
}

QSize DeformationChartWidget::minimumSizeHint() const
{
    return QSize(200, 200);
}

bool DeformationChartWidget::hasHeightForWidth() const
{
    return true;
}

int DeformationChartWidget::heightForWidth(int width) const
{
    if (!m_hasData || m_scanWidth <= 0 || m_scanHeight <= 0) 
        return 350;
        
    // Calculate desired height based on aspect ratio
    // w_chart / h_chart = w_scan / h_scan
    
    // Account for margins and slider height
    const int sliderHeight = 50;
    const int verticalMargins = 20;
    const int horizontalMargins = 20;
    
    int availableWidth = width - horizontalMargins;
    if (availableWidth <= 0) availableWidth = 10;
    
    double aspectRatio = getAspectRatio();
    int desiredContentHeight = static_cast<int>(availableWidth / aspectRatio);

    // Limit height for portrait images to prevent excessive vertical space usage.
    // However, user requested "space needed", so we shouldn't compress it too much unless necessary.
    // Removing artificial clamp to satisfy "determine size from width" request.
    /*
    int maxContentHeight = availableWidth; 
    if (desiredContentHeight > maxContentHeight) desiredContentHeight = maxContentHeight;
    */
    
    // Ensure at least some visibility
    if (desiredContentHeight < 50) desiredContentHeight = 50;
    
    return desiredContentHeight + sliderHeight + verticalMargins;
}



double DeformationChartWidget::getAspectRatio() const
{
    if (m_scanHeight > 0) {
        double w = m_scanWidth;
        double h = m_scanHeight;
        
        // Use scan_rotation (0, 1, 2, 3) where 1=90deg
        int angle = (m_rotation * 90) % 360;
        if (angle < 0) angle += 360;
        
        if (angle == 90 || angle == 270) {
           std::swap(w, h);
        }
        
        return w / h;
    }
    return 1.0;
}

float DeformationChartWidget::getExaggerationFactor() const
{
    if (!m_exaggerateSlider)
        return 1.0f;
    
    int sliderValue = m_exaggerateSlider->value();
    
    // Logarithmic scale: 0-10000 slider maps to 1.0-10000.0 exaggeration
    // Formula: factor = 10000.0 ^ (sliderValue / 10000.0)
    
    float normalizedValue = sliderValue / 10000.0f; // 0.0 to 1.0
    float factor = std::pow(10000.0f, normalizedValue); // 1.0 to 10000.0
    
    return factor;
}

QRect DeformationChartWidget::getChartRect() const
{
    if (!m_hasData || m_scanWidth <= 0 || m_scanHeight <= 0)
        return QRect();
        
    // Calculate chart area (leave space for slider at bottom)
    const int sliderHeight = 50;
    const int marginLeft = 10;
    const int marginRight = 10;
    const int marginTop = 10;
    const int marginBottom = sliderHeight + 10;
    
    QRect availableRect(marginLeft, marginTop,
                   width() - marginLeft - marginRight,
                   height() - marginTop - marginBottom);
    
    if (availableRect.width() < 10 || availableRect.height() < 10)
        return QRect();

    // Use CoordinateTransformer for dimensions
    colorscreen::image_data fakeScan;
    fakeScan.width = m_scanWidth;
    fakeScan.height = m_scanHeight;
    colorscreen::render_parameters fakeParams;
    fakeParams.scan_mirror = m_mirror;
    fakeParams.scan_rotation = m_rotation;
    
    CoordinateTransformer transformer(&fakeScan, fakeParams);
    QSize transformedSize = transformer.getTransformedSize();
    
    double scanAspect = (double)transformedSize.width() / (double)transformedSize.height();
        
    double widgetAspect = (double)availableRect.width() / (double)availableRect.height();
    
    QRect chartRect = availableRect;
    
    if (widgetAspect > scanAspect) {
        // Widget is too wide, limit width based on height
        int newWidth = static_cast<int>(availableRect.height() * scanAspect);
        int offset = (availableRect.width() - newWidth) / 2;
        chartRect.setLeft(availableRect.left() + offset);
        chartRect.setWidth(newWidth);
    } else {
        // Widget is too tall, limit height based on width
        int newHeight = static_cast<int>(availableRect.width() / scanAspect);
        int offset = (availableRect.height() - newHeight) / 2;
        chartRect.setTop(availableRect.top() + offset);
        chartRect.setHeight(newHeight);
    }
    return chartRect;
}

void DeformationChartWidget::mousePressEvent(QMouseEvent *event)
{
    QRect chartRect = getChartRect();
    if (chartRect.isNull() || !chartRect.contains(event->pos()))
        return;

    // Use CoordinateTransformer for dimensions
    colorscreen::image_data fakeScan;
    fakeScan.width = m_scanWidth;
    fakeScan.height = m_scanHeight;
    colorscreen::render_parameters fakeParams;
    fakeParams.scan_mirror = m_mirror;
    fakeParams.scan_rotation = m_rotation;
    
    CoordinateTransformer transformer(&fakeScan, fakeParams);
    QSize transformedSize = transformer.getTransformedSize();

    // Normalized Transformed Coordinate
    double u = (event->pos().x() - chartRect.left()) / (double)chartRect.width();
    double v = (event->pos().y() - chartRect.top()) / (double)chartRect.height();
    
    double tx = u * transformedSize.width();
    double ty = v * transformedSize.height();
    
    colorscreen::point_t local = transformer.transformedToScan({tx, ty});
    colorscreen::point_t p = { local.x + m_offsetX, local.y + m_offsetY };
    
    emit clicked(p);
}

void DeformationChartWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), palette().base());
    
    QRect chartRect = getChartRect();
    if (chartRect.isNull()) {
        painter.setPen(palette().text().color());
        painter.drawText(rect(), Qt::AlignCenter, "No deformation data");
        return;
    }
    
    // Use CoordinateTransformer for dimensions
    colorscreen::image_data fakeScan;
    fakeScan.width = m_scanWidth;
    fakeScan.height = m_scanHeight;
    colorscreen::render_parameters fakeParams;
    fakeParams.scan_mirror = m_mirror;
    fakeParams.scan_rotation = m_rotation;
    
    CoordinateTransformer transformer(&fakeScan, fakeParams);
    QSize transformedSize = transformer.getTransformedSize();
        
    // Draw border
    painter.setPen(QPen(palette().text().color(), 1));
    painter.drawRect(chartRect);
    
    // Initialize scr_to_img instances using FULL proportions
    colorscreen::scr_to_img deformed_map;
    colorscreen::scr_to_img undeformed_map;
    
    deformed_map.set_parameters(m_deformedParams, m_fullScanWidth, m_fullScanHeight);
    undeformed_map.set_parameters(m_undeformedParams, m_fullScanWidth, m_fullScanHeight);
    
    // Calculate grid size (approximately 10 pixels)
    const int gridPixelSize = 10;
    // Use the drawn chart rect for grid calculation
    int gridCols = chartRect.width() / gridPixelSize;
    int gridRows = chartRect.height() / gridPixelSize;
    
    if (gridCols < 2) gridCols = 2;
    if (gridRows < 2) gridRows = 2;
    
    float exaggeration = getExaggerationFactor();

    // Helper: Map Chart Point (pixel in chartRect) -> Scan Point (via Transformer)
    auto chartToScan = [&](QPointF cPt) -> colorscreen::point_t {
         // Normalized Transformed Coordinate
         double u = (cPt.x() - chartRect.left()) / chartRect.width();
         double v = (cPt.y() - chartRect.top()) / chartRect.height();
         
         double tx = u * transformedSize.width();
         double ty = v * transformedSize.height();
         
         colorscreen::point_t local = transformer.transformedToScan({tx, ty});
         return { local.x + m_offsetX, local.y + m_offsetY };
    };

    // Helper: Map Scan Point -> Chart Point
    auto scanToChart = [&](colorscreen::point_t sPt) -> QPointF {
         colorscreen::point_t local = { sPt.x - m_offsetX, sPt.y - m_offsetY };
         colorscreen::point_t tr = transformer.scanToTransformed(local);
         
         // Normalize relative to transformed size
         double u = tr.x / transformedSize.width();
         double v = tr.y / transformedSize.height();
         
         return QPointF(chartRect.left() + u * chartRect.width(),
                        chartRect.top() + v * chartRect.height());
    };

    // Draw chessboard
    // We iterate over the CHART grid (visual grid) and map back to scan coordinates to find deformation
    for (int row = 0; row < gridRows; row++) {
        for (int col = 0; col < gridCols; col++) {
            // Get the four corners of this tile in chart coordinates
            double x0 = chartRect.left() + col * chartRect.width() / (double)gridCols;
            double y0 = chartRect.top() + row * chartRect.height() / (double)gridRows;
            double x1 = chartRect.left() + (col + 1) * chartRect.width() / (double)gridCols;
            double y1 = chartRect.top() + (row + 1) * chartRect.height() / (double)gridRows;
            
            // Get the four corners in scan coordinates
            colorscreen::point_t p00 = chartToScan(QPointF(x0, y0));
            colorscreen::point_t p10 = chartToScan(QPointF(x1, y0));
            colorscreen::point_t p01 = chartToScan(QPointF(x0, y1));
            colorscreen::point_t p11 = chartToScan(QPointF(x1, y1));
            
            // Function to compute true deformation (un-exaggerated)
            auto computeTrueDeformation = [&](colorscreen::point_t undeformed_point) -> colorscreen::point_t {
                colorscreen::point_t scr = undeformed_map.to_scr(undeformed_point);
                return deformed_map.to_img(scr);
            };

            // Calculate true deformed positions for color (heatmap from original displacement)
            colorscreen::point_t true_d00 = computeTrueDeformation(p00);
            colorscreen::point_t true_d10 = computeTrueDeformation(p10);
            colorscreen::point_t true_d01 = computeTrueDeformation(p01);
            colorscreen::point_t true_d11 = computeTrueDeformation(p11);

            // Function to exaggerate deformation
            auto exaggeratePoint = [&](colorscreen::point_t orig, colorscreen::point_t deformed) -> colorscreen::point_t {
                 return orig + (deformed - orig) * exaggeration;
            };

            // Calculate exaggerated positions for geometry
            colorscreen::point_t d00 = exaggeratePoint(p00, true_d00);
            colorscreen::point_t d10 = exaggeratePoint(p10, true_d10);
            colorscreen::point_t d01 = exaggeratePoint(p01, true_d01);
            colorscreen::point_t d11 = exaggeratePoint(p11, true_d11);
            
            QPointF q00 = scanToChart(d00);
            QPointF q10 = scanToChart(d10);
            QPointF q01 = scanToChart(d01);
            QPointF q11 = scanToChart(d11);
            
            // Determine chessboard color (alternating pattern)
            bool isBlack = ((row + col) % 2) == 0;
            
            if (isBlack) {
                // Dark tiles remain dark gray to preserve chessboard pattern
                QPolygonF poly;
                poly << q00 << q10 << q11 << q01;
                painter.setBrush(QColor(50, 50, 50));
                painter.setPen(Qt::NoPen);
                painter.drawPolygon(poly);
            } else {
                // Bright tiles get the heatmap gradient
                
                // Determine colors for corners based on displacement in screen coordinates
                auto getDisplacementColor = [&](colorscreen::point_t original, colorscreen::point_t deformed) -> QColor {
                    // Displacement in screen coordinates
                    double dist = (undeformed_map.to_scr(original) - undeformed_map.to_scr(deformed)).length ();
                    return getHeatMapColor(dist, m_heatmapTolerance);
                };
                
                // Use true_dXY (un-exaggerated) for color calculation
                QColor c00 = getDisplacementColor(p00, true_d00);
                QColor c10 = getDisplacementColor(p10, true_d10);
                QColor c11 = getDisplacementColor(p11, true_d11);
                QColor c01 = getDisplacementColor(p01, true_d01);
                
                // Draw the deformed quadrilateral with gradients
                // To approximate general 4-corner gradients with QPainter, 
                // we split into 4 triangles meeting at the center.
                QPointF center = (q00 + q10 + q11 + q01) / 4.0;
                
                // Average color at center
                int r = (c00.red() + c10.red() + c11.red() + c01.red()) / 4;
                int g = (c00.green() + c10.green() + c11.green() + c01.green()) / 4;
                int b = (c00.blue() + c10.blue() + c11.blue() + c01.blue()) / 4;
                QColor cc(r, g, b);
                
                auto drawTriangle = [&](QPointF p1, QPointF p2, QPointF p3, QColor col1, QColor col2, QColor col3) {
                     // Approximation: linear gradient from edge p1-p2 (avg color) to p3 (center)
                     QPointF mid12 = (p1 + p2) / 2.0;
                     QLinearGradient grad(p3, mid12);
                     grad.setColorAt(0, col3);
                     QColor edgeColor((col1.red()+col2.red())/2, (col1.green()+col2.green())/2, (col1.blue()+col2.blue())/2);
                     grad.setColorAt(1, edgeColor);
                     
                     const QPointF points[] = {p1, p2, p3};
                     painter.setPen(Qt::NoPen);
                     painter.setBrush(grad);
                     painter.drawPolygon(points, 3);
                };
                
                drawTriangle(q00, q10, center, c00, c10, cc);
                drawTriangle(q10, q11, center, c10, c11, cc);
                drawTriangle(q11, q01, center, c11, c01, cc);
                drawTriangle(q01, q00, center, c01, c00, cc);
            }
        }
    }
    
    // Draw grid lines for better visibility
    painter.setPen(QPen(palette().mid().color(), 1));
    for (int row = 0; row <= gridRows; row++) {
        for (int col = 0; col <= gridCols; col++) {
            double x = chartRect.left() + col * chartRect.width() / (double)gridCols;
            double y = chartRect.top() + row * chartRect.height() / (double)gridRows;
            
            // Map chart point to scan point to get "undeformed" scan coord
            colorscreen::point_t p = chartToScan(QPointF(x, y));
            
            // Map undeformed scan -> screen -> deformed image
            colorscreen::point_t scr = undeformed_map.to_scr(p);
            colorscreen::point_t deformed = deformed_map.to_img(scr);
            
            // Map deformed scan -> chart
            QPointF chartPoint = scanToChart(deformed);
            
            // Draw small point
            painter.drawEllipse(chartPoint, 1, 1);
        }
    }
}
