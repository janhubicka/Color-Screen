#include "ToneCurveWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
#include <algorithm>
#include "../libcolorscreen/include/color.h"

ToneCurveWidget::ToneCurveWidget(QWidget *parent) : InteractiveChartWidget(parent) {
    m_defaultMinX = m_minX = 0.0;
    m_defaultMaxX = m_maxX = 1.0;
    m_defaultMinY = m_minY = 0.0;
    m_defaultMaxY = m_maxY = 1.0;
    
    m_controlPoints = colorscreen::tone_curve::default_control_points();
}

void ToneCurveWidget::setToneCurve(colorscreen::tone_curve::tone_curves type, 
                                   const std::vector<colorscreen::point_t> &points) {
    m_type = type;
    m_controlPoints = points;
    update();
}

void ToneCurveWidget::setCoordinateType(CoordinateType type) {
    if (m_coordType == type) return;
    m_coordType = type;
    
    if (m_coordType == CoordinateType::Log) {
        m_defaultMinX = m_minX = -3.0; // log10(0.001)
        m_defaultMaxX = m_maxX = 0.0;  // log10(1.0)
        m_defaultMinY = m_minY = -3.0;
        m_defaultMaxY = m_maxY = 0.0;
    } else {
        m_defaultMinX = m_minX = 0.0;
        m_defaultMaxX = m_maxX = 1.0;
        m_defaultMinY = m_minY = 0.0;
        m_defaultMaxY = m_maxY = 1.0;
    }
    update();
}

double ToneCurveWidget::logicToPlotX(double x) const {
    switch (m_coordType) {
        case CoordinateType::Linear: return x;
        case CoordinateType::Gamma22: return std::pow(std::max(0.0, x), 1.0/2.2);
        case CoordinateType::Log: return std::log10(std::max(1e-4, x));
    }
    return x;
}

double ToneCurveWidget::logicToPlotY(double y) const {
    switch (m_coordType) {
        case CoordinateType::Linear: return y;
        case CoordinateType::Gamma22: return std::pow(std::max(0.0, y), 1.0/2.2);
        case CoordinateType::Log: return std::log10(std::max(1e-4, y));
    }
    return y;
}

double ToneCurveWidget::plotToLogicX(double x) const {
    switch (m_coordType) {
        case CoordinateType::Linear: return x;
        case CoordinateType::Gamma22: return std::pow(std::max(0.0, x), 2.2);
        case CoordinateType::Log: return std::pow(10.0, x);
    }
    return x;
}

double ToneCurveWidget::plotToLogicY(double y) const {
    switch (m_coordType) {
        case CoordinateType::Linear: return y;
        case CoordinateType::Gamma22: return std::pow(std::max(0.0, y), 2.2);
        case CoordinateType::Log: return std::pow(10.0, y);
    }
    return y;
}

QPointF ToneCurveWidget::mapToWidget(double x, double y) const {
    return plotToWidget(logicToPlotX(x), logicToPlotY(y));
}

void ToneCurveWidget::drawGrid(QPainter &painter, const QRectF &rect) {
    // Draw grid lines
    painter.setPen(QPen(QColor(100, 100, 100, 100), 1, Qt::DashLine));
    
    double step = (m_coordType == CoordinateType::Log) ? 1.0 : 0.2;
    for (double x = std::ceil(m_minX / step) * step; x <= m_maxX; x += step) {
        double px = plotToWidget(x, 0).x();
        if (px < rect.left() || px > rect.right()) continue;
        painter.drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
        
        painter.setPen(Qt::white);
        QString label;
        if (m_coordType == CoordinateType::Log) label = QString::number(x, 'f', 0);
        else label = QString::number(x, 'f', 1);
        // Shift label down to avoid overlapping with the grayscale bar
        painter.drawText(QRectF(px - 20, rect.bottom() + 18, 40, 15), Qt::AlignCenter, label);
        painter.setPen(QPen(QColor(100, 100, 100, 100), 1, Qt::DashLine));
    }
    
    for (double y = std::ceil(m_minY / step) * step; y <= m_maxY; y += step) {
        double py = plotToWidget(0, y).y();
        if (py < rect.top() || py > rect.bottom()) continue;
        painter.drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
        
        painter.setPen(Qt::white);
        QString label;
        if (m_coordType == CoordinateType::Log) label = QString::number(y, 'f', 0);
        else label = QString::number(y, 'f', 1);
        // Shift label left to avoid overlapping with the grayscale bar
        painter.drawText(QRectF(1, py - 7, rect.left() - 20, 15), Qt::AlignRight | Qt::AlignVCenter, label);
        painter.setPen(QPen(QColor(100, 100, 100, 100), 1, Qt::DashLine));
    }
    
    // Draw grayscale guides
    drawGrayscaleBar(painter, rect, Qt::Horizontal, [this](double plotX) {
        double logicX = plotToLogicX(plotX);
        int gray = std::clamp((int)(colorscreen::linear_to_srgb(logicX) * 255.0f), 0, 255);
        return QColor(gray, gray, gray);
    });
    
    drawGrayscaleBar(painter, rect, Qt::Vertical, [this](double plotY) {
        double logicY = plotToLogicY(plotY);
        int gray = std::clamp((int)(colorscreen::linear_to_srgb(logicY) * 255.0f), 0, 255);
        return QColor(gray, gray, gray);
    });
    
    // Identity line
    painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::SolidLine));
    double diagStart = std::max(m_minX, m_minY);
    double diagEnd = std::min(m_maxX, m_maxY);
    if (diagStart < diagEnd) {
        painter.drawLine(plotToWidget(diagStart, diagStart), plotToWidget(diagEnd, diagEnd));
    }
}

void ToneCurveWidget::drawPlot(QPainter &painter, const QRectF &rect) {
    colorscreen::tone_curve curve(m_type, m_controlPoints);
    
    QPainterPath path;
    bool first = true;
    
    int startPx = rect.left();
    int endPx = rect.right();
    
    for (int px = startPx; px <= endPx; px++) {
        double nx = (double)(px - rect.left()) / rect.width();
        double plotX = m_minX + nx * (m_maxX - m_minX);
        double logicX = plotToLogicX(plotX);
        
        if (logicX < 0 || logicX > 1.0) continue;
        
        double logicY = curve.apply(logicX);
        double plotY = logicToPlotY(logicY);
        
        QPointF p = plotToWidget(plotX, plotY);
        if (first) {
            path.moveTo(p);
            first = false;
        } else {
            path.lineTo(p);
        }
    }
    
    painter.setPen(QPen(Qt::green, 2));
    painter.drawPath(path);
}

void ToneCurveWidget::drawControlPoints(QPainter &painter, const QRectF &rect) {
    if (m_type != colorscreen::tone_curve::tone_curve_custom) return;
    for (size_t i = 0; i < m_controlPoints.size(); i++) {
        QPointF p = mapToWidget(m_controlPoints[i].x, m_controlPoints[i].y);
        QPointF p_orig = mapToWidget(m_controlPoints[i].x, m_controlPoints[i].x);

        double dx = p.x() - p_orig.x();
        double dy = p.y() - p_orig.y();
        double dist2 = dx*dx + dy*dy;
        if (dist2 > 25.0) {
            QColor arrowColor(0, 255, 255, 180);
            painter.setPen(QPen(Qt::black, 3));
            painter.drawLine(p_orig, p);
            painter.setPen(QPen(arrowColor, 1));
            painter.drawLine(p_orig, p);

            double angle = std::atan2(dy, dx);
            double headLen = 6.0;
            QPointF h1(p.x() - headLen * std::cos(angle - M_PI/6), p.y() - headLen * std::sin(angle - M_PI/6));
            QPointF h2(p.x() - headLen * std::cos(angle + M_PI/6), p.y() - headLen * std::sin(angle + M_PI/6));
            painter.setPen(QPen(Qt::black, 3));
            painter.drawLine(p, h1); painter.drawLine(p, h2);
            painter.setPen(QPen(arrowColor, 1));
            painter.drawLine(p, h1); painter.drawLine(p, h2);
        }

        if ((int)i == m_dragPointIndex) painter.setBrush(Qt::red);
        else painter.setBrush(Qt::white);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(p, 4, 4);
    }
}

void ToneCurveWidget::mousePressEvent(QMouseEvent *event) {
    if (m_type == colorscreen::tone_curve::tone_curve_custom && event->button() == Qt::LeftButton) {
        m_dragPointIndex = -1;
        for (size_t i = 0; i < m_controlPoints.size(); i++) {
            QPointF p = mapToWidget(m_controlPoints[i].x, m_controlPoints[i].y);
            if (QLineF(event->position(), p).length() < 10) {
                m_dragPointIndex = (int)i;
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

void ToneCurveWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragPointIndex == -1) {
        bool found = false;
        for (size_t i = 0; i < m_controlPoints.size(); i++) {
            QPointF p = mapToWidget(m_controlPoints[i].x, m_controlPoints[i].y);
            if (QLineF(event->position(), p).length() < 10) {
                setCursor(Qt::PointingHandCursor);
                found = true;
                break;
            }
        }
        if (!found) setCursor(Qt::ArrowCursor);
    }
    if (m_dragPointIndex != -1) {
        auto [plotX, plotY] = widgetToPlot(event->position());
        double logicX = plotToLogicX(plotX);
        double logicY = plotToLogicY(plotY);
        
        logicX = std::clamp(logicX, 0.0, 1.0);
        logicY = std::clamp(logicY, 0.0, 1.0);
        
        // Boundaries (0,0) and (1,1) should ideally be fixed? 
        // Image tools usually let you move them only along Y if they are at 0 or 1.
        if (m_dragPointIndex == 0) logicX = 0;
        else if (m_dragPointIndex == (int)m_controlPoints.size() - 1) logicX = 1;
        else {
            // Keep order
            logicX = std::clamp(logicX, m_controlPoints[m_dragPointIndex - 1].x + 0.001, 
                                        m_controlPoints[m_dragPointIndex + 1].x - 0.001);
        }
        
        m_controlPoints[m_dragPointIndex].x = logicX;
        m_controlPoints[m_dragPointIndex].y = logicY;
        
        update();
        emit controlPointsChanged(m_controlPoints);
    } else {
        InteractiveChartWidget::mouseMoveEvent(event);
    }
}

void ToneCurveWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragPointIndex != -1) {
        m_dragPointIndex = -1;
        update();
    }
    InteractiveChartWidget::mouseReleaseEvent(event);
}
