#ifndef HD_CURVE_WIDGET_H
#define HD_CURVE_WIDGET_H

#include <QWidget>
#include <QPolygonF>
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/sensitivity.h"
#include "../libcolorscreen/include/colorscreen.h"

class HDCurveWidget : public QWidget {
    Q_OBJECT
public:
    explicit HDCurveWidget(QWidget *parent = nullptr);
    ~HDCurveWidget() override;
    QSize sizeHint() const override;

    colorscreen::hd_curve_parameters getParameters() const;
    double getMinX() const { return m_minX; }
    double getMaxX() const { return m_maxX; }
    double getMinY() const { return m_minY; }
    double getMaxY() const { return m_maxY; }
    void setParameters(const colorscreen::hd_curve_parameters &params);
    void setHDColors(const std::vector<colorscreen::rgbdata> &colors, double minY, double maxY);
    void setDisplayMode(colorscreen::hd_axis_type mode);
    colorscreen::hd_axis_type getDisplayMode() const { return m_displayMode; }
    void setDensityBoost(double boost);
    void setHistogram(const std::vector<uint64_t> &hist, double minX, double maxX);
    QPointF plotToWidget(double plotX, double plotY) const;

signals:
    void parametersChanged(const colorscreen::hd_curve_parameters &params);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateCurve();
    void resetZoom();
    QPointF mapToWidget(double x, double y) const;
    std::pair<double, double> mapFromWidget(const QPointF &p) const;
    QRectF getChartRect() const;
    
    // Bounds for display.
    double m_minX = -5.0;
    double m_maxX = 5.0;
    double m_minY = -5.0;
    double m_maxY = 5.0;

    colorscreen::hd_curve_parameters m_params;
    QPointF m_lastMousePos;
    QPolygonF m_curvePath;
    
    std::vector<colorscreen::rgbdata> m_hdColors;
    double m_hdColorsMinY = 0.0;
    double m_hdColorsMaxY = 0.0;
    
    colorscreen::hd_axis_type m_displayMode = colorscreen::hd_axis_hd;
    double m_densityBoost = 1.0;
    
    std::vector<uint64_t> m_histogram;
    double m_histMinX = 0, m_histMaxX = 0;
    
    int m_dragPointIndex = -1; // 0: min, 1: linear1, 2: linear2, 3: max
};

#endif // HD_CURVE_WIDGET_H
