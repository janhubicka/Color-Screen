#include "InteractiveChartWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

InteractiveChartWidget::InteractiveChartWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(250, 250);
    setMouseTracking(true);
}

InteractiveChartWidget::~InteractiveChartWidget() = default;

void InteractiveChartWidget::setViewRange(double minX, double maxX, double minY, double maxY) {
    m_minX = minX;
    m_maxX = maxX;
    m_minY = minY;
    m_maxY = maxY;
    update();
}

void InteractiveChartWidget::resetZoom() {
    m_minX = m_defaultMinX;
    m_maxX = m_defaultMaxX;
    m_minY = m_defaultMinY;
    m_maxY = m_defaultMaxY;
    onViewChanged();
    update();
}

QRectF InteractiveChartWidget::getChartRect() const {
    return QRectF(m_marginLeft, m_marginTop, 
                  width() - m_marginLeft - m_marginRight, 
                  height() - m_marginTop - m_marginBottom);
}

QPointF InteractiveChartWidget::plotToWidget(double plotX, double plotY) const {
    QRectF rect = getChartRect();
    if (rect.width() <= 0 || rect.height() <= 0) return QPointF();
    
    double nx = (plotX - m_minX) / (m_maxX - m_minX);
    double ny = (plotY - m_minY) / (m_maxY - m_minY);
    return QPointF(rect.left() + nx * rect.width(), rect.bottom() - ny * rect.height());
}

std::pair<double, double> InteractiveChartWidget::widgetToPlot(const QPointF &p) const {
    QRectF rect = getChartRect();
    if (rect.width() <= 0 || rect.height() <= 0) return {0, 0};
    
    double nx = (p.x() - rect.left()) / rect.width();
    double ny = (rect.bottom() - p.y()) / rect.height();
    
    double plotX = m_minX + nx * (m_maxX - m_minX);
    double plotY = m_minY + ny * (m_maxY - m_minY);
    return {plotX, plotY};
}

void InteractiveChartWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRectF rect = getChartRect();
    drawBackground(painter, rect);
    drawGrid(painter, rect);
    drawPlot(painter, rect);
    drawControlPoints(painter, rect);
}

void InteractiveChartWidget::drawBackground(QPainter &painter, const QRectF &rect) {
    painter.fillRect(rect, QColor(40, 40, 40));
}

void InteractiveChartWidget::mousePressEvent(QMouseEvent *event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::RightButton) {
        m_isPanning = true;
    }
}

void InteractiveChartWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_isPanning) {
        QPointF delta = event->position() - m_lastMousePos;
        QRectF rect = getChartRect();
        
        double dx = (delta.x() / rect.width()) * (m_maxX - m_minX);
        double dy = (delta.y() / rect.height()) * (m_maxY - m_minY);
        
        m_minX -= dx;
        m_maxX -= dx;
        m_minY += dy;
        m_maxY += dy;
        
        onViewChanged();
        update();
    }
    m_lastMousePos = event->position();
}

void InteractiveChartWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        m_isPanning = false;
    }
}

void InteractiveChartWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    resetZoom();
}

void InteractiveChartWidget::wheelEvent(QWheelEvent *event) {
    double angle = event->angleDelta().y();
    double factor = std::pow(0.9, angle / 120.0);
    
    QRectF rect = getChartRect();
    double nx = (event->position().x() - rect.left()) / rect.width();
    double ny = (rect.bottom() - event->position().y()) / rect.height();
    
    double zoomX = m_minX + nx * (m_maxX - m_minX);
    double zoomY = m_minY + ny * (m_maxY - m_minY);
    
    m_minX = zoomX - (zoomX - m_minX) * factor;
    m_maxX = zoomX + (m_maxX - zoomX) * factor;
    m_minY = zoomY - (zoomY - m_minY) * factor;
    m_maxY = zoomY + (m_maxY - zoomY) * factor;
    
    // Clamp zoom levels
    if (m_maxX - m_minX < 0.001) {
        double mid = (m_minX + m_maxX) / 2.0;
        m_minX = mid - 0.0005;
        m_maxX = mid + 0.0005;
    }
    if (m_maxY - m_minY < 0.001) {
        double mid = (m_minY + m_maxY) / 2.0;
        m_minY = mid - 0.0005;
        m_maxY = mid + 0.0005;
    }
    
    onViewChanged();
    update();
}

void InteractiveChartWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    onViewChanged();
}

void InteractiveChartWidget::drawGrayscaleBar(QPainter &painter, const QRectF &rect, Qt::Orientation orientation,
                                            std::function<QColor(double)> colorMap) {
    if (orientation == Qt::Horizontal) {
        int y = rect.bottom() + 5;
        int h = 10;
        QLinearGradient grad(rect.left(), 0, rect.right(), 0);
        
        // Sample for smooth gradient
        for (int i = 0; i <= 20; ++i) {
            double pos = (double)i / 20.0;
            double plotX = m_minX + pos * (m_maxX - m_minX);
            grad.setColorAt(pos, colorMap(plotX));
        }
        painter.fillRect(QRectF(rect.left(), y, rect.width(), h), grad);
        painter.setPen(QPen(QColor(100, 100, 100), 1));
        painter.drawRect(QRectF(rect.left(), y, rect.width(), h));
    } else {
        int x = rect.left() - 15;
        int w = 10;
        QLinearGradient grad(0, rect.bottom(), 0, rect.top());
        
        for (int i = 0; i <= 20; ++i) {
            double pos = (double)i / 20.0;
            double plotY = m_minY + pos * (m_maxY - m_minY);
            grad.setColorAt(pos, colorMap(plotY));
        }
        painter.fillRect(QRectF(x, rect.top(), w, rect.height()), grad);
        painter.setPen(QPen(QColor(100, 100, 100), 1));
        painter.drawRect(QRectF(x, rect.top(), w, rect.height()));
    }
}
