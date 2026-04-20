#ifndef TONE_CURVE_WIDGET_H
#define TONE_CURVE_WIDGET_H

#include "InteractiveChartWidget.h"
#include "../libcolorscreen/include/tone-curve.h"

class ToneCurveWidget : public InteractiveChartWidget {
    Q_OBJECT
public:
    enum class CoordinateType { Linear, Gamma22, Log };

    explicit ToneCurveWidget(QWidget *parent = nullptr);
    void setToneCurve(colorscreen::tone_curve::tone_curves type, 
                      const std::vector<colorscreen::point_t> &points);
    void setCoordinateType(CoordinateType type);
    CoordinateType getCoordinateType() const { return m_coordType; }

signals:
    void controlPointsChanged(const std::vector<colorscreen::point_t> &points);

protected:
    void drawGrid(QPainter &painter, const QRectF &rect) override;
    void drawPlot(QPainter &painter, const QRectF &rect) override;
    void drawControlPoints(QPainter &painter, const QRectF &rect) override;
    
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    double logicToPlotX(double x) const;
    double logicToPlotY(double y) const;
    double plotToLogicX(double x) const;
    double plotToLogicY(double y) const;

    QPointF mapToWidget(double x, double y) const;

    colorscreen::tone_curve::tone_curves m_type = colorscreen::tone_curve::tone_curve_linear;
    std::vector<colorscreen::point_t> m_controlPoints;
    CoordinateType m_coordType = CoordinateType::Gamma22;
    int m_dragPointIndex = -1;
};

#endif // TONE_CURVE_WIDGET_H
