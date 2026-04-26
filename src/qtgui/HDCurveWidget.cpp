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

void HDCurveWidget::onViewChanged() {
    updateCurve();
}

void HDCurveWidget::drawPlot(QPainter &painter, const QRectF &rect) {
    Q_UNUSED(rect);
    painter.setPen(QPen(Qt::green, 2));
    if (!m_curvePath.isEmpty()) {
        painter.drawPolyline(m_curvePath);
    }
}

void HDCurveWidget::drawControlPoints(QPainter &painter, const QRectF &rect) {
    // Draw limit lines first (so they are under the points)
    QPointF p_min = mapToWidget(m_params.minx, m_params.miny);
    QPointF p_max = mapToWidget(m_params.maxx, m_params.maxy);
    
    // Green color for limit lines
    QColor greenLine(0, 150, 0, 180);

    // Dmin, Dmax (horizontal)
    if (m_dragPointIndex == 5) painter.setPen(QPen(Qt::yellow, 1, Qt::SolidLine));
    else painter.setPen(QPen(greenLine, 1, Qt::DashLine));
    painter.drawLine(QPointF(rect.left(), p_min.y()), QPointF(rect.right(), p_min.y()));
    
    if (m_dragPointIndex == 6) painter.setPen(QPen(Qt::yellow, 1, Qt::SolidLine));
    else painter.setPen(QPen(greenLine, 1, Qt::DashLine));
    painter.drawLine(QPointF(rect.left(), p_max.y()), QPointF(rect.right(), p_max.y()));
    
    // Xmin, Xmax (vertical)
    if (m_dragPointIndex == 7) painter.setPen(QPen(Qt::yellow, 1, Qt::SolidLine));
    else painter.setPen(QPen(greenLine, 1, Qt::DashLine));
    painter.drawLine(QPointF(p_min.x(), rect.top()), QPointF(p_min.x(), rect.bottom()));
    
    if (m_dragPointIndex == 8) painter.setPen(QPen(Qt::yellow, 1, Qt::SolidLine));
    else painter.setPen(QPen(greenLine, 1, Qt::DashLine));
    painter.drawLine(QPointF(p_max.x(), rect.top()), QPointF(p_max.x(), rect.bottom()));

    // Draw points
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::black, 1));
    
    QPointF pts[4] = {
        p_min,
        mapToWidget(m_params.linear1x, m_params.linear1y),
        mapToWidget(m_params.linear2x, m_params.linear2y),
        p_max
    };
    
    // Middle point
    double midX = (m_params.linear1x + m_params.linear2x) / 2.0;
    double midY = (m_params.linear1y + m_params.linear2y) / 2.0;
    QPointF p_mid = mapToWidget(midX, midY);
    
    for (int i = 0; i < 4; ++i) {
        if (m_dragPointIndex == i) {
            painter.setBrush(Qt::red);
        } else {
            painter.setBrush(Qt::white);
        }
        painter.drawEllipse(pts[i], 4, 4);
    }

    // Middle point (square, black inside, white border)
    if (m_dragPointIndex == 4) painter.setBrush(Qt::red);
    else painter.setBrush(Qt::black);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawRect(QRectF(p_mid.x() - 4, p_mid.y() - 4, 8, 8));
}


void HDCurveWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // 1. Check points (highest priority)
        QPointF p_min = mapToWidget(m_params.minx, m_params.miny);
        QPointF p_l1 = mapToWidget(m_params.linear1x, m_params.linear1y);
        QPointF p_l2 = mapToWidget(m_params.linear2x, m_params.linear2y);
        QPointF p_max = mapToWidget(m_params.maxx, m_params.maxy);
        
        double midX = (m_params.linear1x + m_params.linear2x) / 2.0;
        double midY = (m_params.linear1y + m_params.linear2y) / 2.0;
        QPointF p_mid = mapToWidget(midX, midY);
        
        QPointF pts[5] = { p_min, p_l1, p_l2, p_max, p_mid };
        
        m_dragPointIndex = -1;
        for (int i = 0; i < 5; ++i) {
            if (QLineF(event->position(), pts[i]).length() < 10) {
                m_dragPointIndex = i;
                break;
            }
        }
        
        // 2. Check lines if no point hit
        if (m_dragPointIndex == -1) {
            QPointF pos = event->position();
            // dmin line (y = p_min.y())
            if (std::abs(pos.y() - p_min.y()) < 5) m_dragPointIndex = 5;
            // dmax line (y = p_max.y())
            else if (std::abs(pos.y() - p_max.y()) < 5) m_dragPointIndex = 6;
            // xmin line (x = p_min.x())
            else if (std::abs(pos.x() - p_min.x()) < 5) m_dragPointIndex = 7;
            // xmax line (x = p_max.x())
            else if (std::abs(pos.x() - p_max.x()) < 5) m_dragPointIndex = 8;
        }
        
        if (m_dragPointIndex != -1) {
            auto [lx, ly] = mapFromWidget(event->position());
            m_lastLogicX = lx;
            m_lastLogicY = ly;
            update();
            return;
        }
    }
    InteractiveChartWidget::mousePressEvent(event);
}

void HDCurveWidget::mouseMoveEvent(QMouseEvent *event) {
    auto [logicX, logicY] = mapFromWidget(event->position());
    QPointF pos = event->position();

    if (m_dragPointIndex != -1) {
        double dx = logicX - m_lastLogicX;
        double dy = logicY - m_lastLogicY;
        
        switch (m_dragPointIndex) {
            case 0: // min point
                logicX = qMin(logicX, m_params.linear1x);
                m_params.minx = logicX; m_params.miny = logicY;
                setCursor(Qt::ClosedHandCursor);
                break;
            case 1: // linear1 point
                logicX = qBound(m_params.minx, logicX, m_params.linear2x);
                m_params.linear1x = logicX; m_params.linear1y = logicY;
                setCursor(Qt::ClosedHandCursor);
                break;
            case 2: // linear2 point
                logicX = qBound(m_params.linear1x, logicX, m_params.maxx);
                m_params.linear2x = logicX; m_params.linear2y = logicY;
                setCursor(Qt::ClosedHandCursor);
                break;
            case 3: // max point
                logicX = qMax(logicX, m_params.linear2x);
                m_params.maxx = logicX; m_params.maxy = logicY;
                setCursor(Qt::ClosedHandCursor);
                break;
            case 4: // middle point (move all)
                m_params.minx += dx; m_params.miny += dy;
                m_params.linear1x += dx; m_params.linear1y += dy;
                m_params.linear2x += dx; m_params.linear2y += dy;
                m_params.maxx += dx; m_params.maxy += dy;
                setCursor(Qt::SizeAllCursor);
                break;
            case 5: { // dmin line scale
                double oldRange = m_params.maxy - m_params.miny;
                double newRange = m_params.maxy - logicY;
                if (std::abs(oldRange) > 1e-9) {
                    double scale = newRange / oldRange;
                    m_params.linear1y = m_params.maxy - (m_params.maxy - m_params.linear1y) * scale;
                    m_params.linear2y = m_params.maxy - (m_params.maxy - m_params.linear2y) * scale;
                    m_params.miny = logicY;
                }
                setCursor(Qt::SizeVerCursor);
                break;
            }
            case 6: { // dmax line scale
                double oldRange = m_params.maxy - m_params.miny;
                double newRange = logicY - m_params.miny;
                if (std::abs(oldRange) > 1e-9) {
                    double scale = newRange / oldRange;
                    m_params.linear1y = m_params.miny + (m_params.linear1y - m_params.miny) * scale;
                    m_params.linear2y = m_params.miny + (m_params.linear2y - m_params.miny) * scale;
                    m_params.maxy = logicY;
                }
                setCursor(Qt::SizeVerCursor);
                break;
            }
            case 7: { // xmin line scale
                double oldRange = m_params.maxx - m_params.minx;
                double newRange = m_params.maxx - logicX;
                if (std::abs(oldRange) > 1e-9) {
                    double scale = newRange / oldRange;
                    m_params.linear1x = m_params.maxx - (m_params.maxx - m_params.linear1x) * scale;
                    m_params.linear2x = m_params.maxx - (m_params.maxx - m_params.linear2x) * scale;
                    m_params.minx = logicX;
                }
                setCursor(Qt::SizeHorCursor);
                break;
            }
            case 8: { // xmax line scale
                double oldRange = m_params.maxx - m_params.minx;
                double newRange = logicX - m_params.minx;
                if (std::abs(oldRange) > 1e-9) {
                    double scale = newRange / oldRange;
                    m_params.linear1x = m_params.minx + (m_params.linear1x - m_params.minx) * scale;
                    m_params.linear2x = m_params.minx + (m_params.linear2x - m_params.minx) * scale;
                    m_params.maxx = logicX;
                }
                setCursor(Qt::SizeHorCursor);
                break;
            }
        }
        
        m_lastLogicX = logicX;
        m_lastLogicY = logicY;
        
        updateCurve();
        update();
        emit parametersChanged(m_params);
    } else {
        // Hover logic
        QPointF p_min = mapToWidget(m_params.minx, m_params.miny);
        QPointF p_l1 = mapToWidget(m_params.linear1x, m_params.linear1y);
        QPointF p_l2 = mapToWidget(m_params.linear2x, m_params.linear2y);
        QPointF p_max = mapToWidget(m_params.maxx, m_params.maxy);
        double midX = (m_params.linear1x + m_params.linear2x) / 2.0;
        double midY = (m_params.linear1y + m_params.linear2y) / 2.0;
        QPointF p_mid = mapToWidget(midX, midY);
        
        QPointF pts[5] = { p_min, p_l1, p_l2, p_max, p_mid };
        bool hitPoint = false;
        for (int i = 0; i < 5; ++i) {
            if (QLineF(pos, pts[i]).length() < 10) {
                if (i == 4) setCursor(Qt::SizeAllCursor);
                else setCursor(Qt::PointingHandCursor);
                hitPoint = true;
                break;
            }
        }
        
        if (!hitPoint) {
            if (std::abs(pos.y() - p_min.y()) < 5 || std::abs(pos.y() - p_max.y()) < 5) {
                setCursor(Qt::SizeVerCursor);
            } else if (std::abs(pos.x() - p_min.x()) < 5 || std::abs(pos.x() - p_max.x()) < 5) {
                setCursor(Qt::SizeHorCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
        InteractiveChartWidget::mouseMoveEvent(event);
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
}
