#ifndef SPECTRACHARTWIDGET_H
#define SPECTRACHARTWIDGET_H

#include <QWidget>
#include <vector>

class SpectraChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit SpectraChartWidget(QWidget *parent = nullptr);

  // Set data for the 4 curves. Data is expected to be 0..1 (or handled
  // internally) Vectors should match the X-axis range (e.g. 400..720 with some
  // step, or just mapped)
  void setSpectraData(const std::vector<double> &redDye,
                      const std::vector<double> &greenDye,
                      const std::vector<double> &blueDye,
                      const std::vector<double> &backlight);

  void clear();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;

  void setYAxis(double min, double max, const QString &title,
                const QString &suffix = "");

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::vector<double> m_redDye;
  std::vector<double> m_greenDye;
  std::vector<double> m_blueDye;
  std::vector<double> m_backlight;

  bool m_hasData = false;

  double m_yMin = 0.0;
  double m_yMax = 1.0;
  QString m_yTitle = "Transmitance";
  QString m_ySuffix = "%";
};

#endif // SPECTRACHARTWIDGET_H
