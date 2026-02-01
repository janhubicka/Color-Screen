#ifndef CIE_CHART_WIDGET_H
#define CIE_CHART_WIDGET_H

#include <QPolygonF>
#include <QWidget>
#include <vector>

namespace colorscreen {
struct xy_t;
}

class CIEChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit CIEChartWidget(QWidget *parent = nullptr);
  ~CIEChartWidget() override;
  QSize sizeHint() const override;

  void setWhitepoint(double x, double y);
  std::pair<double, double> getWhitepoint() const;

  struct GamutData {
      bool valid = false;
      double rx, ry; 
      double gx, gy;
      double bx, by;
      double wx, wy;
  };
  void setGamut(const GamutData& gamut);
  void setReferenceGamut(const GamutData& gamut);
  void setSelectionEnabled(bool enabled);

signals:
  void whitepointChanged(double x, double y);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void updateLocus();
  QPointF mapToWidget(double x, double y) const;
  std::pair<double, double> mapFromWidget(const QPointF &p) const;
  QRectF getChartRect() const;
  void generateCache(); // Added

  QPolygonF m_locus;
  QImage m_cache; // Added
  double m_selectedX = 0.33;
  double m_selectedY = 0.33;
  
  GamutData m_gamut;
  GamutData m_referenceGamut;
  bool m_selectionEnabled = true;

  // Chart logic
  double m_minX = 0.0;
  double m_maxX = 0.8;
  double m_minY = 0.0;
  double m_maxY = 0.9;
};

#endif // CIE_CHART_WIDGET_H
