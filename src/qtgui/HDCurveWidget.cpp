#include "HDCurveWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

HDCurveWidget::HDCurveWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(250, 250);
    setMouseTracking(true);
    updateCurve();
}

HDCurveWidget::~HDCurveWidget() = default;

QSize HDCurveWidget::sizeHint() const {
    return QSize(400, 400);
}

colorscreen::hd_curve_parameters HDCurveWidget::getParameters() const {
    return m_params;
}

void HDCurveWidget::setParameters(const colorscreen::hd_curve_parameters &params) {
    if (m_params == params) return;
    m_params = params;
    updateCurve();
    update();
}

void HDCurveWidget::setDisplayMode(colorscreen::hd_axis_type mode) {
    if (m_displayMode == mode) return;
    m_displayMode = mode;
    
    // Reset zoom based on mode
    if (m_displayMode == colorscreen::hd_axis_hd) {
        m_minX = -5.0; m_maxX = 5.0;
        m_minY = -5.0; m_maxY = 5.0;
    } else {
        // Linear exposure normally from 0 to something above 1
        m_minX = 0.0; m_maxX = 2.0;
        // Transmittance from 0 (black) to 1 (transparent background)
        m_minY = 0.0; m_maxY = 1.1;
    }
    updateCurve();
    update();
}

void HDCurveWidget::setDensityBoost(double boost) {
    if (m_densityBoost == boost) return;
    m_densityBoost = boost;
    if (m_displayMode != colorscreen::hd_axis_hd) {
        updateCurve();
        update();
    }
}

void HDCurveWidget::setHistogram(const std::vector<uint64_t> &hist, double minX, double maxX) {
    m_histogram = hist;
    m_histMinX = minX;
    m_histMaxX = maxX;
    update();
}

void HDCurveWidget::setHDColors(const std::vector<colorscreen::rgbdata> &colors, double minY, double maxY) {
    m_hdColors = colors;
    m_hdColorsMinY = minY;
    m_hdColorsMaxY = maxY;
    update();
}

void HDCurveWidget::updateCurve() {
    m_curvePath.clear();
    colorscreen::richards_hd_curve synthLine(100, m_params);
    
    QRectF rect = getChartRect();
    if (rect.width() <= 0) return;
    
    int startPx = std::floor(rect.left());
    int endPx = std::ceil(rect.right());
    
    for (int px = startPx; px <= endPx; ++px) {
        // Find logical x for this pixel column
        double nx = (px - rect.left()) / rect.width();
        double plotX = m_minX + nx * (m_maxX - m_minX);
        double logicX = colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode);

        // Use apply() to get logical y
        double logicY = synthLine.apply(logicX);
        
        m_curvePath.append(mapToWidget(logicX, logicY));
    }
}

QRectF HDCurveWidget::getChartRect() const {
    const int margin = 20;
    const int leftMargin = 60;
    return QRectF(leftMargin, margin, width() - leftMargin - margin, height() - 2 * margin);
}

QPointF HDCurveWidget::plotToWidget(double plotX, double plotY) const {
    QRectF rect = getChartRect();
    double nx = (plotX - m_minX) / (m_maxX - m_minX);
    double ny = (plotY - m_minY) / (m_maxY - m_minY);
    return QPointF(rect.left() + nx * rect.width(), rect.bottom() - ny * rect.height());
}

QPointF HDCurveWidget::mapToWidget(double x, double y) const {
    double plotX = colorscreen::hd_log_exposure_to_axis_x(x, m_displayMode);
    double plotY = colorscreen::hd_density_to_axis_y(y, m_densityBoost, m_displayMode);
    return plotToWidget(plotX, plotY);
}

std::pair<double, double> HDCurveWidget::mapFromWidget(const QPointF &p) const {
    QRectF rect = getChartRect();
    double nx = (p.x() - rect.left()) / rect.width();
    double ny = (rect.bottom() - p.y()) / rect.height();
    
    double plotX = m_minX + nx * (m_maxX - m_minX);
    double plotY = m_minY + ny * (m_maxY - m_minY);
    
    return {colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode),
            colorscreen::hd_axis_y_to_density(plotY, m_densityBoost, m_displayMode)};
}

void HDCurveWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRectF rect = getChartRect();
    
    // Fill background
    painter.fillRect(rect, QColor(40, 40, 40));
    
    // Draw Histogram
    if (!m_histogram.empty()) {
        uintmax_t maxCount = 0;
        for (auto v : m_histogram) if (v > maxCount) maxCount = v;
        
        if (maxCount > 0) {
            QPainterPath histPath;
            bool first = true;
            for (size_t i = 0; i < m_histogram.size(); ++i) {
                double plotX = m_histMinX + (double)i / (m_histogram.size() - 1) * (m_histMaxX - m_histMinX);
                double h = (double)m_histogram[i] / maxCount;
                
                double px = plotToWidget(plotX, 0).x();
                double py = rect.bottom() - h * rect.height();
                
                if (first) {
                    histPath.moveTo(px, rect.bottom());
                    first = false;
                }
                histPath.lineTo(px, py);
            }
            histPath.lineTo(plotToWidget(m_histMaxX, 0).x(), rect.bottom());
            histPath.closeSubpath();
            
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(150, 150, 150, 60));
            painter.drawPath(histPath);
        }
    }
    
    // Draw Grid
    double xStep = (m_displayMode == colorscreen::hd_axis_hd) ? 1.0 : 0.2;
    for (double i = std::ceil(m_minX / xStep) * xStep; i <= m_maxX; i += xStep) {
        double px = plotToWidget(i, 0).x();
        if (px < rect.left() || px > rect.right()) continue;
        
        painter.setPen(QPen(QColor(100, 100, 100, 100), 1, Qt::DashLine));
        painter.drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
        painter.setPen(QPen(Qt::white, 1));
        painter.drawText(QRectF(px - 20, rect.bottom() + 2, 40, 15), Qt::AlignCenter, 
                         (m_displayMode == colorscreen::hd_axis_hd) ? QString::number(i, 'f', 0) : QString::number(i, 'f', 1));
    }
    
    double yStep = (m_displayMode == colorscreen::hd_axis_hd) ? 1.0 : 0.2;
    for (double i = std::ceil(m_minY / yStep) * yStep; i <= m_maxY; i += yStep) {
        double py = plotToWidget(0, i).y();
        if (py < rect.top() || py > rect.bottom()) continue;

        painter.setPen(QPen(QColor(100, 100, 100, 100), 1, Qt::DashLine));
        painter.drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
        painter.setPen(QPen(Qt::white, 1));
        painter.drawText(QRectF(1, py - 7, rect.left() - 20, 15), Qt::AlignRight | Qt::AlignVCenter, 
                         (m_displayMode == colorscreen::hd_axis_hd) ? QString::number(i, 'f', 0) : QString::number(i, 'f', 1));
    }

    // Draw main axis lines
    painter.setPen(QPen(QColor(150, 150, 150), 1));
    QPointF zeroOrigin = mapToWidget(0, 0);
    painter.drawLine(QPointF(rect.left(), zeroOrigin.y()), QPointF(rect.right(), zeroOrigin.y()));
    painter.drawLine(QPointF(zeroOrigin.x(), rect.top()), QPointF(zeroOrigin.x(), rect.bottom()));

    // Draw HD Colors and Transparency Bands
    if (!m_hdColors.empty() && m_hdColorsMaxY > m_hdColorsMinY) {
        int leftStripX = rect.left() - 15;
        int stripW = 10;
        
        // Draw vertical strip left of chart using a gradient to avoid banding
        QLinearGradient gradient(0, rect.bottom(), 0, rect.top());
        for (size_t i = 0; i < m_hdColors.size(); ++i) {
            double pos = (double)i / (m_hdColors.size() - 1);
            auto c = m_hdColors[i];
            gradient.setColorAt(pos, QColor(std::clamp((int)c.red, 0, 255), 
                                            std::clamp((int)c.green, 0, 255), 
                                            std::clamp((int)c.blue, 0, 255)));
        }
        painter.fillRect(QRectF(leftStripX, rect.top(), stripW, rect.height()), gradient);

        // Draw transparent bands for 1..254 across chart
        auto drawBand = [&](int channel, QColor bandColor) {
            // Find y ranges where channel is in [1, 254]
            // Since the curve is monotonic, we expect a single range
            int startIdx = -1;
            int endIdx = -1;
            for (int i = 0; i < (int)m_hdColors.size(); ++i) {
                int val = 0;
                if (channel == 0) val = m_hdColors[i].red;
                else if (channel == 1) val = m_hdColors[i].green;
                else val = m_hdColors[i].blue;

                if (val >= 1 && val <= 254) {
                    if (startIdx == -1) startIdx = i;
                    endIdx = i;
                }
            }

            if (startIdx != -1) {
                double y1 = m_hdColorsMinY + (double)startIdx / (m_hdColors.size() - 1) * (m_hdColorsMaxY - m_hdColorsMinY);
                double y2 = m_hdColorsMinY + (double)endIdx / (m_hdColors.size() - 1) * (m_hdColorsMaxY - m_hdColorsMinY);
                
                QPointF p1 = plotToWidget(0, y1);
                QPointF p2 = plotToWidget(0, y2);
                
                QRectF bandRect(rect.left(), std::min(p1.y(), p2.y()), rect.width(), std::abs(p1.y() - p2.y()));
                painter.fillRect(bandRect, bandColor);
            }
        };

        drawBand(0, QColor(255, 0, 0, 30));
        drawBand(1, QColor(0, 255, 0, 30));
        drawBand(2, QColor(0, 0, 255, 30));
    }

    // Draw Curve
    painter.setPen(QPen(Qt::green, 2));
    if (!m_curvePath.isEmpty()) {
        painter.drawPolyline(m_curvePath);
    }
    
    // Draw Control Points
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::black, 1));
    
    QPointF pts[4] = {
        mapToWidget(m_params.minx, m_params.miny),
        mapToWidget(m_params.linear1x, m_params.linear1y),
        mapToWidget(m_params.linear2x, m_params.linear2y),
        mapToWidget(m_params.maxx, m_params.maxy)
    };
    
    for (int i = 0; i < 4; ++i) {
        if (m_dragPointIndex == i) {
            painter.setBrush(Qt::red);
        } else {
            painter.setBrush(Qt::white);
        }
        painter.drawEllipse(pts[i], 4, 4);
    }
}

void HDCurveWidget::mousePressEvent(QMouseEvent *event) {
    m_lastMousePos = event->position();
    
    if (event->button() == Qt::LeftButton) {
        QPointF pts[4] = {
            mapToWidget(m_params.minx, m_params.miny),
            mapToWidget(m_params.linear1x, m_params.linear1y),
            mapToWidget(m_params.linear2x, m_params.linear2y),
            mapToWidget(m_params.maxx, m_params.maxy)
        };
        
        m_dragPointIndex = -1;
        for (int i = 0; i < 4; ++i) {
            if (QLineF(event->position(), pts[i]).length() < 10) {
                m_dragPointIndex = i;
                break;
            }
        }
        
        if (m_dragPointIndex != -1) {
            update();
        }
    }
}

void HDCurveWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragPointIndex != -1) {
        QRectF rect = getChartRect();
        double nx = qBound(0.0, (event->position().x() - rect.left()) / rect.width(), 1.0);
        double ny = qBound(0.0, (rect.bottom() - event->position().y()) / rect.height(), 1.0);
        
        double plotX = m_minX + nx * (m_maxX - m_minX);
        double plotY = m_minY + ny * (m_maxY - m_minY);
        
        double logicX = colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode);
        double logicY = colorscreen::hd_axis_y_to_density(plotY, m_densityBoost, m_displayMode);
        
        // Constrain points to maintain left-to-right order to keep function valid
        switch (m_dragPointIndex) {
            case 0: // min
                logicX = qMin(logicX, m_params.linear1x);
                m_params.minx = logicX;
                m_params.miny = logicY;
                break;
            case 1: // linear1
                logicX = qBound(m_params.minx, logicX, m_params.linear2x);
                m_params.linear1x = logicX;
                m_params.linear1y = logicY;
                break;
            case 2: // linear2
                logicX = qBound(m_params.linear1x, logicX, m_params.maxx);
                m_params.linear2x = logicX;
                m_params.linear2y = logicY;
                break;
            case 3: // max
                logicX = qMax(logicX, m_params.linear2x);
                m_params.maxx = logicX;
                m_params.maxy = logicY;
                break;
        }
        
        updateCurve();
        update();
        emit parametersChanged(m_params);
    } else if (event->buttons() & Qt::RightButton) {
        QPointF delta = event->position() - m_lastMousePos;
        
        QRectF rect = getChartRect();
        double dx = (delta.x() / rect.width()) * (m_maxX - m_minX);
        double dy = (delta.y() / rect.height()) * (m_maxY - m_minY);
        
        m_minX -= dx;
        m_maxX -= dx;
        m_minY += dy;
        m_maxY += dy;
        
        updateCurve();
        update();
        emit parametersChanged(m_params); // To refresh color strip if it uses these bounds
    }
    m_lastMousePos = event->position();
}

void HDCurveWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_dragPointIndex != -1) {
        m_dragPointIndex = -1;
        update();
    }
}

void HDCurveWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    resetZoom();
}

void HDCurveWidget::resetZoom() {
    m_minX = -5.0;
    m_maxX = 5.0;
    m_minY = -5.0;
    m_maxY = 5.0;
    updateCurve();
    update();
    emit parametersChanged(m_params);
}

void HDCurveWidget::wheelEvent(QWheelEvent *event) {
    // Zoom factor: 0.9 per 120 units (step)
    double angle = event->angleDelta().y();
    double factor = std::pow(0.9, angle / 120.0);
    
    // Zoom around the mouse cursor
    // Zoom around the mouse cursor
    QRectF rect = getChartRect();
    double nx = (event->position().x() - rect.left()) / rect.width();
    double ny = (rect.bottom() - event->position().y()) / rect.height();
    
    double zoomX = m_minX + nx * (m_maxX - m_minX);
    double zoomY = m_minY + ny * (m_maxY - m_minY);
    
    m_minX = zoomX - (zoomX - m_minX) * factor;
    m_maxX = zoomX + (m_maxX - zoomX) * factor;
    m_minY = zoomY - (zoomY - m_minY) * factor;
    m_maxY = zoomY + (m_maxY - zoomY) * factor;
    
    // Clamp zoom levels to prevent numeric issues
    // Min range: 0.01, Max range: 100
    if (m_maxX - m_minX < 0.01) {
        double mid = (m_minX + m_maxX) / 2.0;
        m_minX = mid - 0.005;
        m_maxX = mid + 0.005;
    }
    if (m_maxY - m_minY < 0.01) {
        double mid = (m_minY + m_maxY) / 2.0;
        m_minY = mid - 0.005;
        m_maxY = mid + 0.005;
    }
    
    updateCurve();
    update();
    
    // Notify ContactCopyPanel to potentially re-compute high-res color strips
    // actually, let's just use the parametersChanged signal but we haven't changed parameters.
    // Let's create a new signal: zoomChanged
    emit parametersChanged(m_params); // Force update to refresh the color strip if needed
}

void HDCurveWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateCurve();
}
