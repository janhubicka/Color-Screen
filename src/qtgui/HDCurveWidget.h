#ifndef HD_CURVE_WIDGET_H
#define HD_CURVE_WIDGET_H

#include "InteractiveChartWidget.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/sensitivity.h"
#include "../libcolorscreen/include/colorscreen.h"

class HDCurveWidget : public InteractiveChartWidget {
    Q_OBJECT
public:
    explicit HDCurveWidget(QWidget *parent = nullptr);
    ~HDCurveWidget() override;
    QSize sizeHint() const override;

    colorscreen::hd_curve_parameters getParameters() const;
    void setParameters(const colorscreen::hd_curve_parameters &params);
    void setHDColors(const std::vector<colorscreen::rgbdata> &colors, double minY, double maxY);
    void setDisplayMode(colorscreen::hd_axis_type mode);
    colorscreen::hd_axis_type getDisplayMode() const { return m_displayMode; }
    void setDensityBoost(double boost);
    void setHistogram(const std::vector<uint64_t> &hist, double minX, double maxX);

signals:
    void parametersChanged(const colorscreen::hd_curve_parameters &params);

protected:
    void drawGrid(QPainter &painter, const QRectF &rect) override;
    void drawPlot(QPainter &painter, const QRectF &rect) override;
    void drawControlPoints(QPainter &painter, const QRectF &rect) override;
    
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateCurve();
    QPointF mapToWidget(double x, double y) const;
    std::pair<double, double> mapFromWidget(const QPointF &p) const;
    
    colorscreen::hd_curve_parameters m_params;
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
