#ifndef INTERACTIVE_CHART_WIDGET_H
#define INTERACTIVE_CHART_WIDGET_H

#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <functional>

class InteractiveChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit InteractiveChartWidget(QWidget *parent = nullptr);
    virtual ~InteractiveChartWidget() override;

    void setViewRange(double minX, double maxX, double minY, double maxY);
    void resetZoom();

    double minX() const { return m_minX; }
    double maxX() const { return m_maxX; }
    double minY() const { return m_minY; }
    double maxY() const { return m_maxY; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    virtual void drawBackground(QPainter &painter, const QRectF &rect);
    virtual void drawGrid(QPainter &painter, const QRectF &rect) = 0;
    virtual void drawPlot(QPainter &painter, const QRectF &rect) = 0;
    virtual void drawControlPoints(QPainter &painter, const QRectF &rect) { Q_UNUSED(painter); Q_UNUSED(rect); }

    void drawGrayscaleBar(QPainter &painter, const QRectF &rect, Qt::Orientation orientation,
                          std::function<QColor(double)> colorMap);

    virtual void onViewChanged() {}

    virtual QRectF getChartRect() const;
    QPointF plotToWidget(double plotX, double plotY) const;
    std::pair<double, double> widgetToPlot(const QPointF &p) const;

    double m_minX = -1.0, m_maxX = 1.0, m_minY = -1.0, m_maxY = 1.0;
    double m_defaultMinX = -1.0, m_defaultMaxX = 1.0, m_defaultMinY = -1.0, m_defaultMaxY = 1.0;
    
    QPointF m_lastMousePos;
    bool m_isPanning = false;

    // Margin settings.
    int m_marginTop = 20;
    int m_marginBottom = 40;
    int m_marginLeft = 80;
    int m_marginRight = 20;
};

#endif // INTERACTIVE_CHART_WIDGET_H
