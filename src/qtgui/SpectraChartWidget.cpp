#include "SpectraChartWidget.h"
#include "color.h"
#include "spectrum-to-xyz.h"
#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <cmath>

SpectraChartWidget::SpectraChartWidget(QWidget *parent) : QWidget(parent) {
  setMinimumHeight(200);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void SpectraChartWidget::setSpectraData(const std::vector<double> &redDye,
                                        const std::vector<double> &greenDye,
                                        const std::vector<double> &blueDye,
                                        const std::vector<double> &backlight) {
  m_redDye = redDye;
  m_greenDye = greenDye;
  m_blueDye = blueDye;
  m_backlight = backlight;
  m_hasData = true;
  update();
}

void SpectraChartWidget::clear() {
  m_redDye.clear();
  m_greenDye.clear();
  m_blueDye.clear();
  m_backlight.clear();
  m_hasData = false;
  update();
}

QSize SpectraChartWidget::sizeHint() const {
  return QSize(600, 325); // ~ 600/3 + 125
}

QSize SpectraChartWidget::minimumSizeHint() const { return QSize(300, 310); }

bool SpectraChartWidget::hasHeightForWidth() const { return true; }

int SpectraChartWidget::heightForWidth(int width) const {
  int chartHeight = static_cast<int>(width / 3.0);
  return chartHeight + 125;
}

// Precise Wavelength to RGB conversion using CIE CMFs
static QColor wavelengthToRGB(double wavelength) {
  if (wavelength < 380 || wavelength > 780)
    return QColor(0, 0, 0);

  // Interpolate CMFs
  double pos = (wavelength - SPECTRUM_START) / (double)SPECTRUM_STEP;
  int idx = (int)pos;
  double t = pos - idx;

  if (idx < 0 || idx >= SPECTRUM_SIZE - 1)
    return QColor(0, 0, 0);

  colorscreen::xyz c = {colorscreen::cie_cmf_x[idx] * (1 - t) +
                            colorscreen::cie_cmf_x[idx + 1] * t,
                        colorscreen::cie_cmf_y[idx] * (1 - t) +
                            colorscreen::cie_cmf_y[idx + 1] * t,
                        colorscreen::cie_cmf_z[idx] * (1 - t) +
                            colorscreen::cie_cmf_z[idx + 1] * t};
  colorscreen::luminosity_t r, g, b;
  (c * 0.15).to_srgb(&r, &g, &b);

  return QColor(std::clamp((int)(r * 255), 0, 255),
                std::clamp((int)(g * 255), 0, 255),
                std::clamp((int)(b * 255), 0, 255));
}

void SpectraChartWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Fill background normally first
  painter.fillRect(rect(), palette().base());

  const int marginLeft = 80;
  const int marginRight = 20;
  const int marginTop = 20;
  const int marginBottom = 105;

  QRect chartRect(marginLeft, marginTop, width() - marginLeft - marginRight,
                  height() - marginTop - marginBottom);

  if (chartRect.width() < 10 || chartRect.height() < 10)
    return;

  // Draw Spectral Background
  // Iterate pixels in chart width
  QImage bgImage(chartRect.width(), 1, QImage::Format_RGB32);
  for (int x = 0; x < chartRect.width(); ++x) {
    double t = (double)x / (chartRect.width() - 1);
    double wl = 400.0 + t * (720.0 - 400.0);
    QColor col = wavelengthToRGB(wl);

    int r = col.red();
    int g = col.green();
    int b = col.blue();

    bgImage.setPixel(x, 0, qRgb(r, g, b));
  }
  // Stretch to fill chart height
  painter.drawImage(chartRect, bgImage);

  // Draw Axes Box
  painter.setPen(QPen(palette().text().color(), 1));
  painter.drawRect(chartRect);

  // Draw Y-axis grid and labels
  QFont font = painter.font();
  font.setPointSize(9);
  painter.setFont(font);

  for (int i = 0; i <= 10; i++) {
    int y = chartRect.bottom() - (chartRect.height() * i / 10);
    painter.drawLine(chartRect.left() - 5, y, chartRect.left(), y);

    QString label = QString::number(i * 10) + "%";
    QRect textRect(0, y - 10, marginLeft - 10, 20);
    painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);

    if (i > 0 && i < 10) {
      painter.setPen(
          QPen(QColor(200, 200, 200, 50), 1, Qt::DotLine)); // Faint grid
      painter.drawLine(chartRect.left(), y, chartRect.right(), y);
      painter.setPen(QPen(palette().text().color(), 1));
    }
  }

  // Draw X-axis labels (400 to 720)
  // 400, 440, 480, ... 720 (interval 40? or maybe just 50s: 400, 450, 500...)
  // Range is 320nm.
  for (int wl = 400; wl <= 720; wl += 40) {
    double t = (wl - 400.0) / (720.0 - 400.0);
    int x = chartRect.left() + (int)(t * chartRect.width());

    painter.drawLine(x, chartRect.bottom(), x, chartRect.bottom() + 5);

    QString label = QString::number(wl);
    QRect textRect(x - 20, chartRect.bottom() + 5, 40, 20);
    painter.drawText(textRect, Qt::AlignCenter, label);

    if (wl > 400 && wl < 720) {
      painter.setPen(QPen(QColor(200, 200, 200, 50), 1, Qt::DotLine));
      painter.drawLine(x, chartRect.top(), x, chartRect.bottom());
      painter.setPen(QPen(palette().text().color(), 1));
    }
  }

  // Axis Labels
  painter.setPen(palette().text().color());
  painter.drawText(QRect(0, chartRect.bottom() + 25, width(), 15),
                   Qt::AlignCenter, "Wavelength (nm)");

  painter.save();
  painter.translate(12, height() / 2);
  painter.rotate(-90);
  painter.drawText(-50, 0, 100, 20, Qt::AlignCenter, "Transmitance");
  painter.restore();

  // Draw Curves
  if (!m_hasData) {
    painter.setPen(palette().text().color());
    painter.drawText(chartRect, Qt::AlignCenter, "No spectra data");
    return;
  }

  auto drawCurve = [&](const std::vector<double> &data, const QColor &color,
                       int width = 2) {
    if (data.empty())
      return;

    painter.setPen(QPen(color, width));
    QPointF prevPoint;

    for (size_t i = 0; i < data.size(); ++i) {
      double t = i / (double)(data.size() - 1);
      double val = std::clamp(data[i], 0.0, 1.0);

      int x = chartRect.left() + (int)(t * chartRect.width());
      int y = chartRect.bottom() - (int)(val * chartRect.height());
      QPointF point(x, y);

      if (i > 0)
        painter.drawLine(prevPoint, point);
      prevPoint = point;
    }
  };

  drawCurve(m_redDye, Qt::red, 2);
  drawCurve(m_greenDye, Qt::green, 2);
  drawCurve(m_blueDye, Qt::blue, 2);
  drawCurve(m_backlight, Qt::white, 2);

  // Legend
  struct LegendItem {
    QString name;
    QColor color;
  };
  LegendItem items[] = {{"Red Dye", Qt::red},
                        {"Green Dye", Qt::green},
                        {"Blue Dye", Qt::blue},
                        {"Backlight", Qt::white}};

  int legendY = chartRect.bottom() + 45;
  int legendX = chartRect.left();
  int itemWidth = 100;

  for (int i = 0; i < 4; ++i) {
    int x = legendX + i * itemWidth;
    // Line
    painter.setPen(QPen(items[i].color, 2));
    painter.drawLine(x, legendY + 10, x + 20, legendY + 10);
    // Text
    painter.setPen(palette().text().color());
    painter.drawText(x + 25, legendY, 70, 20, Qt::AlignLeft | Qt::AlignVCenter,
                     items[i].name);
  }
}
