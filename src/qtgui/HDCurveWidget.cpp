#include "HDCurveWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

HDCurveWidget::HDCurveWidget(QWidget *parent) : InteractiveChartWidget(parent) {
    m_defaultMinX = m_minX = -5.0;
    m_defaultMaxX = m_maxX = 5.0;
    m_defaultMinY = m_minY = -5.0;
    m_defaultMaxY = m_maxY = 5.0;
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
        m_defaultMinX = m_minX = -5.0; m_defaultMaxX = m_maxX = 5.0;
        m_defaultMinY = m_minY = -5.0; m_defaultMaxY = m_maxY = 5.0;
    } else {
        m_defaultMinX = m_minX = 0.0; m_defaultMaxX = m_maxX = 2.0;
        m_defaultMinY = m_minY = 0.0; m_defaultMaxY = m_maxY = 1.1;
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

QPointF HDCurveWidget::mapToWidget(double x, double y) const {
    double plotX = colorscreen::hd_log_exposure_to_axis_x(x, m_displayMode);
    double plotY = colorscreen::hd_density_to_axis_y(y, m_densityBoost, m_displayMode);
    return plotToWidget(plotX, plotY);
}

std::pair<double, double> HDCurveWidget::mapFromWidget(const QPointF &p) const {
    auto [plotX, plotY] = widgetToPlot(p);
    return {colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode),
            colorscreen::hd_axis_y_to_density(plotY, m_densityBoost, m_displayMode)};
}

void HDCurveWidget::drawGrid(QPainter &painter, const QRectF &rect) {
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
        painter.drawText(QRectF(px - 20, rect.bottom() + 18, 40, 15), Qt::AlignCenter, 
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

    // Draw HD Colors result bands
    if (!m_hdColors.empty() && m_hdColorsMaxY > m_hdColorsMinY) {
        // Vertical guide (Density/Output)
        drawGrayscaleBar(painter, rect, Qt::Vertical, [this](double plotY) {
            double pos = (plotY - m_hdColorsMinY) / (m_hdColorsMaxY - m_hdColorsMinY);
            int idx = std::clamp((int)(pos * (m_hdColors.size() - 1)), 0, (int)m_hdColors.size() - 1);
            auto c = m_hdColors[idx];
            return QColor(std::clamp((int)c.red, 0, 255), 
                          std::clamp((int)c.green, 0, 255), 
                          std::clamp((int)c.blue, 0, 255));
        });

        // Horizontal guide (Exposure -> Resulting color)
        drawGrayscaleBar(painter, rect, Qt::Horizontal, [this](double plotX) {
            // Find logicX for this plotX
            double logicX = colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode);
            // Apply Richards curve logic
            colorscreen::richards_hd_curve synthLine(100, m_params);
            double logicY = synthLine.apply(logicX);
            
            // Map logicY to color
            double pos = (logicY - m_hdColorsMinY) / (m_hdColorsMaxY - m_hdColorsMinY);
            int idx = std::clamp((int)(pos * (m_hdColors.size() - 1)), 0, (int)m_hdColors.size() - 1);
            auto c = m_hdColors[idx];
            return QColor(std::clamp((int)c.red, 0, 255), 
                          std::clamp((int)c.green, 0, 255), 
                          std::clamp((int)c.blue, 0, 255));
        });

        auto drawBand = [&](int channel, QColor bandColor) {
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
}

void HDCurveWidget::drawPlot(QPainter &painter, const QRectF &rect) {
    Q_UNUSED(rect);
    painter.setPen(QPen(Qt::green, 2));
    if (!m_curvePath.isEmpty()) {
        painter.drawPolyline(m_curvePath);
    }
}

void HDCurveWidget::drawControlPoints(QPainter &painter, const QRectF &rect) {
    Q_UNUSED(rect);
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
            return;
        }
    }
    InteractiveChartWidget::mousePressEvent(event);
}

void HDCurveWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragPointIndex != -1) {
        QRectF rect = getChartRect();
        double nx = qBound(0.0, (double)(event->position().x() - rect.left()) / rect.width(), 1.0);
        double ny = qBound(0.0, (double)(rect.bottom() - event->position().y()) / rect.height(), 1.0);
        
        double plotX = m_minX + nx * (m_maxX - m_minX);
        double plotY = m_minY + ny * (m_maxY - m_minY);
        
        double logicX = colorscreen::hd_axis_x_to_log_exposure(plotX, m_displayMode);
        double logicY = colorscreen::hd_axis_y_to_density(plotY, m_densityBoost, m_displayMode);
        
        switch (m_dragPointIndex) {
            case 0: logicX = qMin(logicX, m_params.linear1x); m_params.minx = logicX; m_params.miny = logicY; break;
            case 1: logicX = qBound(m_params.minx, logicX, m_params.linear2x); m_params.linear1x = logicX; m_params.linear1y = logicY; break;
            case 2: logicX = qBound(m_params.linear1x, logicX, m_params.maxx); m_params.linear2x = logicX; m_params.linear2y = logicY; break;
            case 3: logicX = qMax(logicX, m_params.linear2x); m_params.maxx = logicX; m_params.maxy = logicY; break;
        }
        
        updateCurve();
        update();
        emit parametersChanged(m_params);
    } else {
        InteractiveChartWidget::mouseMoveEvent(event);
        if (m_isPanning) {
            updateCurve();
        }
    }
}

void HDCurveWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_dragPointIndex != -1) {
        m_dragPointIndex = -1;
        update();
    }
    InteractiveChartWidget::mouseReleaseEvent(event);
}

void HDCurveWidget::resizeEvent(QResizeEvent *event) {
    InteractiveChartWidget::resizeEvent(event);
    updateCurve();
}
