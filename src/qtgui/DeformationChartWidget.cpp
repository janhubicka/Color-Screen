#include "DeformationChartWidget.h"
#include "ColorUtils.h"
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
    m_exaggerateSlider->setRange(0, 1000); // 0-1000 for fine control
    m_exaggerateSlider->setValue(0); // 0 = 1.0x (no exaggeration)
    m_exaggerateSlider->setEnabled(true);
    
    // Connect slider to update label and trigger repaint
    connect(m_exaggerateSlider, &QSlider::valueChanged, this, [this](int value) {
        float factor = getExaggerationFactor();
        m_sliderLabel->setText(QString("Exaggerate: %1x").arg(factor, 0, 'f', 1));
        update();
    });
    
    sliderLayout->addWidget(m_sliderLabel);
    sliderLayout->addWidget(m_exaggerateSlider);
    
    mainLayout->addLayout(sliderLayout);
    
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void DeformationChartWidget::setDeformationData(
    const colorscreen::scr_to_img_parameters &deformed,
    const colorscreen::scr_to_img_parameters &undeformed,
    int scanWidth, int scanHeight)
{
    m_deformedParams = deformed;
    m_undeformedParams = undeformed;
    m_scanWidth = scanWidth;
    m_scanHeight = scanHeight;
    m_hasData = (scanWidth > 0 && scanHeight > 0);
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
    update();
}

QSize DeformationChartWidget::sizeHint() const
{
    if (m_hasData && m_scanWidth > 0 && m_scanHeight > 0) {
        // Use scan aspect ratio
        double aspectRatio = getAspectRatio();
        int width = 500;
        int chartHeight = static_cast<int>(width / aspectRatio);
        return QSize(width, chartHeight + 50); // +50 for slider
    }
    return QSize(500, 350);
}

QSize DeformationChartWidget::minimumSizeHint() const
{
    return QSize(300, 250);
}

bool DeformationChartWidget::hasHeightForWidth() const
{
    return true;
}

int DeformationChartWidget::heightForWidth(int width) const
{
    if (m_hasData && m_scanWidth > 0 && m_scanHeight > 0) {
        double aspectRatio = getAspectRatio();
        int chartHeight = static_cast<int>(width / aspectRatio);
        return chartHeight + 50; // +50 for slider
    }
    return width; // Default to square
}

double DeformationChartWidget::getAspectRatio() const
{
    if (m_scanHeight > 0) {
        return static_cast<double>(m_scanWidth) / static_cast<double>(m_scanHeight);
    }
    return 1.0;
}

float DeformationChartWidget::getExaggerationFactor() const
{
    if (!m_exaggerateSlider)
        return 1.0f;
    
    int sliderValue = m_exaggerateSlider->value();
    
    // Logarithmic scale: 0-1000 slider maps to 1.0-100.0 exaggeration
    // This makes it move slowly at first (fine control near 1.0)
    // Formula: factor = 1.0 * (100.0 / 1.0) ^ (sliderValue / 1000.0)
    //        = 100.0 ^ (sliderValue / 1000.0)
    
    float normalizedValue = sliderValue / 1000.0f; // 0.0 to 1.0
    float factor = std::pow(100.0f, normalizedValue); // 1.0 to 100.0
    
    return factor;
}

void DeformationChartWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), palette().base());
    
    if (!m_hasData || m_scanWidth <= 0 || m_scanHeight <= 0) {
        painter.setPen(palette().text().color());
        painter.drawText(rect(), Qt::AlignCenter, "No deformation data");
        return;
    }
    
    // Calculate chart area (leave space for slider at bottom)
    const int sliderHeight = 50;
    const int marginLeft = 10;
    const int marginRight = 10;
    const int marginTop = 10;
    const int marginBottom = sliderHeight + 10;
    
    QRect chartRect(marginLeft, marginTop,
                   width() - marginLeft - marginRight,
                   height() - marginTop - marginBottom);
    
    if (chartRect.width() < 10 || chartRect.height() < 10)
        return;
    
    // Draw border
    painter.setPen(QPen(palette().text().color(), 1));
    painter.drawRect(chartRect);
    
    // Initialize scr_to_img instances
    colorscreen::scr_to_img deformed_map;
    colorscreen::scr_to_img undeformed_map;
    
    deformed_map.set_parameters(m_deformedParams, m_scanWidth, m_scanHeight);
    undeformed_map.set_parameters(m_undeformedParams, m_scanWidth, m_scanHeight);
    
    // Calculate grid size (approximately 10 pixels)
    const int gridPixelSize = 10;
    int gridCols = chartRect.width() / gridPixelSize;
    int gridRows = chartRect.height() / gridPixelSize;
    
    if (gridCols < 2) gridCols = 2;
    if (gridRows < 2) gridRows = 2;
    
    // Draw chessboard
    for (int row = 0; row < gridRows; row++) {
        for (int col = 0; col < gridCols; col++) {
            // Get the four corners of this tile in chart coordinates
            double x0 = col * chartRect.width() / (double)gridCols;
            double y0 = row * chartRect.height() / (double)gridRows;
            double x1 = (col + 1) * chartRect.width() / (double)gridCols;
            double y1 = (row + 1) * chartRect.height() / (double)gridRows;
            
            // Convert chart coordinates to scan coordinates
            auto chartToScan = [&](double cx, double cy) -> colorscreen::point_t {
                colorscreen::point_t p;
                p.x = (cx / chartRect.width()) * m_scanWidth;
                p.y = (cy / chartRect.height()) * m_scanHeight;
                return p;
            };
            
            // Get the four corners in scan coordinates
            colorscreen::point_t p00 = chartToScan(x0, y0);
            colorscreen::point_t p10 = chartToScan(x1, y0);
            colorscreen::point_t p01 = chartToScan(x0, y1);
            colorscreen::point_t p11 = chartToScan(x1, y1);
            
            // Apply deformation: undeformed -> scr -> deformed
            // Then exaggerate the deformation if slider is set
            float exaggeration = getExaggerationFactor();
            
            auto applyDeformation = [&](colorscreen::point_t undeformed_point) -> colorscreen::point_t {
                colorscreen::point_t scr = undeformed_map.to_scr(undeformed_point);
                colorscreen::point_t deformed_point = deformed_map.to_img(scr);
                
                // Exaggerate the deformation
                if (exaggeration > 1.0f) {
                    // Compute displacement vector
                    double dx = deformed_point.x - undeformed_point.x;
                    double dy = deformed_point.y - undeformed_point.y;
                    
                    // Scale displacement by exaggeration factor
                    deformed_point.x = undeformed_point.x + dx * exaggeration;
                    deformed_point.y = undeformed_point.y + dy * exaggeration;
                }
                
                return deformed_point;
            };
            
            colorscreen::point_t d00 = applyDeformation(p00);
            colorscreen::point_t d10 = applyDeformation(p10);
            colorscreen::point_t d01 = applyDeformation(p01);
            colorscreen::point_t d11 = applyDeformation(p11);
            
            // Convert deformed points back to chart coordinates
            auto scanToChart = [&](colorscreen::point_t p) -> QPointF {
                double cx = (p.x / m_scanWidth) * chartRect.width();
                double cy = (p.y / m_scanHeight) * chartRect.height();
                return QPointF(chartRect.left() + cx, chartRect.top() + cy);
            };
            
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
                    colorscreen::point_t scr_orig = undeformed_map.to_scr(original);
                    colorscreen::point_t scr_deformed = undeformed_map.to_scr(deformed);
                    
                    // Displacement in screen coordinates
                    double dx = scr_deformed.x - scr_orig.x;
                    double dy = scr_deformed.y - scr_orig.y;
                    double dist = std::sqrt(dx*dx + dy*dy);
                    
                    return getHeatMapColor(dist, m_heatmapTolerance);
                };
                
                QColor c00 = getDisplacementColor(p00, d00);
                QColor c10 = getDisplacementColor(p10, d10);
                QColor c11 = getDisplacementColor(p11, d11);
                QColor c01 = getDisplacementColor(p01, d01);
                
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
            double x = col * chartRect.width() / (double)gridCols;
            double y = row * chartRect.height() / (double)gridRows;
            
            colorscreen::point_t p = {
                (x / chartRect.width()) * m_scanWidth,
                (y / chartRect.height()) * m_scanHeight
            };
            
            colorscreen::point_t scr = undeformed_map.to_scr(p);
            colorscreen::point_t deformed = deformed_map.to_img(scr);
            
            double cx = (deformed.x / m_scanWidth) * chartRect.width();
            double cy = (deformed.y / m_scanHeight) * chartRect.height();
            
            QPointF chartPoint(chartRect.left() + cx, chartRect.top() + cy);
            
            // Draw small point
            painter.drawEllipse(chartPoint, 1, 1);
        }
    }
}
