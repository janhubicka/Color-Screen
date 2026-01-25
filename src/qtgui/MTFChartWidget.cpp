#include "MTFChartWidget.h"
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

MTFChartWidget::MTFChartWidget(QWidget *parent) : QWidget(parent) {
  setMinimumHeight(200);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void MTFChartWidget::setMTFData(
    const colorscreen::mtf_parameters::computed_mtf &data,
    bool canSimulateDifraction, double scanDpi, double screenFreq) {
  m_data = data;
  m_hasData = !data.system_mtf.empty();
  m_canSimulateDifraction = canSimulateDifraction;
  m_scanDpi = scanDpi;
  m_screenFreq = screenFreq;
  update();
}

void MTFChartWidget::setMeasuredMTF(const std::vector<double> &freq,
                                    const std::vector<double> &contrast) {
  m_measuredFreq = freq;
  m_measuredContrast = contrast;
  m_hasMeasuredData = !freq.empty() && !contrast.empty();
  update();
}

void MTFChartWidget::clear() {
  m_hasData = false;
  m_hasMeasuredData = false;
  m_measuredFreq.clear();
  m_measuredContrast.clear();
  update();
}

QSize MTFChartWidget::sizeHint() const {
  // Golden ratio: width:height = 1.61:1
  // For 500px width -> height = 500/1.61 + margins = 310 + 195 = 505
  return QSize(500, 505);
}

QSize MTFChartWidget::minimumSizeHint() const {
  // Minimum for golden ratio
  return QSize(300, 380);
}

bool MTFChartWidget::hasHeightForWidth() const { return true; }

int MTFChartWidget::heightForWidth(int width) const {
  // Maintain golden ratio: width:height = 1.61:1
  // Chart area height = width / 1.61
  // Total height = chart height + margins (top 20 + bottom 175)
  int chartHeight = static_cast<int>(width / 1.61);
  return chartHeight + 175; // 20 (top) + 155 (bottom)
}

void MTFChartWidget::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Background
  painter.fillRect(rect(), palette().base());

  if (!m_hasData || m_data.system_mtf.empty()) {
    painter.setPen(palette().text().color());
    painter.drawText(rect(), Qt::AlignCenter, "No MTF data");
    return;
  }

  // Define margins and chart area
  const int marginLeft = 60;
  const int marginRight = 20;
  const int marginTop = 20;
  const int marginBottom = 175; // More space for label + legend + MTF50 + Screen info

  QRect chartRect(marginLeft, marginTop, width() - marginLeft - marginRight,
                  height() - marginTop - marginBottom);

  if (chartRect.width() < 10 || chartRect.height() < 10)
    return;

  // Draw axes
  painter.setPen(QPen(palette().text().color(), 1));
  painter.drawRect(chartRect);

  // Draw grid and labels
  QFont font = painter.font();
  font.setPointSize(9);
  painter.setFont(font);

  // Y-axis labels (0-100%)
  for (int i = 0; i <= 10; i++) {
    int y = chartRect.bottom() - (chartRect.height() * i / 10);
    painter.drawLine(chartRect.left() - 5, y, chartRect.left(), y);

    QString label = QString::number(i * 10);
    QRect textRect(0, y - 10, marginLeft - 10, 20);
    painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);

    // Grid line
    if (i > 0 && i < 10) {
      painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
      painter.drawLine(chartRect.left(), y, chartRect.right(), y);
      painter.setPen(QPen(palette().text().color(), 1));
    }
  }

  // X-axis labels (0.0-1.0 frequency)
  for (int i = 0; i <= 10; i++) {
    int x = chartRect.left() + (chartRect.width() * i / 10);
    painter.drawLine(x, chartRect.bottom(), x, chartRect.bottom() + 5);

    QString label = QString::number(i / 10.0, 'f', 1);
    QRect textRect(x - 20, chartRect.bottom() + 5, 40, 20);
    painter.drawText(textRect, Qt::AlignCenter, label);

    // Grid line
    if (i > 0 && i < 10) {
      painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
      painter.drawLine(x, chartRect.top(), x, chartRect.bottom());
      painter.setPen(QPen(palette().text().color(), 1));
    }
  }

  // Draw Nyquist line at 0.5
  int nyquistX = chartRect.left() + chartRect.width() / 2;
  painter.setPen(QPen(QColor(150, 150, 150), 2, Qt::DashLine));
  painter.drawLine(nyquistX, chartRect.top(), nyquistX, chartRect.bottom());

  // Draw Screen line if available
  int screenX = -1;
  if (m_screenFreq > 0 && m_screenFreq <= 1.0) {
      screenX = chartRect.left() + (int)(m_screenFreq * chartRect.width());
      painter.setPen(QPen(QColor(100, 100, 255), 2, Qt::DashLine));
      painter.drawLine(screenX, chartRect.top(), screenX, chartRect.bottom());
  }

  QFont smallFont = font;
  smallFont.setPointSize(8);
  painter.setFont(smallFont);
  painter.setPen(palette().text().color());

  // Nyquist caption
  painter.drawText(nyquistX - 30, chartRect.top() - 15, 60, 15, Qt::AlignCenter,
                   "Nyquist");

  // Screen caption (avoiding overlap)
  if (screenX != -1) {
      int yOff = 15;
      if (std::abs(screenX - nyquistX) < 40) {
          yOff = 30; // Move it higher up
      }
      painter.setPen(QColor(100, 100, 255));
      painter.drawText(screenX - 30, chartRect.top() - yOff, 60, 15, Qt::AlignCenter,
                       "Screen");
  }
  painter.setFont(font);

  // Axis labels
  painter.setPen(palette().text().color());
  painter.drawText(QRect(0, chartRect.bottom() + 25, width(), 15),
                   Qt::AlignCenter, "Pixel frequency");

  if (m_scanDpi > 0) {
    // Draw cycles per mm axis (lp/mm)
    // 1 pixel frequency = 1 cycle / 2 pixels
    // lp/mm = (pixel_freq) * (DPI / 25.4) / 2
    double lp_mm_max = (m_scanDpi / 25.4) / 2.0;

    for (int i = 0; i <= 10; i++) {
        int x = chartRect.left() + (chartRect.width() * i / 10);
        painter.drawLine(x, chartRect.bottom() + 20, x, chartRect.bottom() + 25);
        
        double lp_mm = (i / 10.0) * lp_mm_max;
        QString label = QString::number(lp_mm, 'f', 1);
        QRect textRect(x - 20, chartRect.bottom() + 45, 40, 20);
        painter.drawText(textRect, Qt::AlignCenter, label);
    }
    painter.drawText(QRect(0, chartRect.bottom() + 65, width(), 15),
                     Qt::AlignCenter, "Cycles per millimeter (lp/mm)");
  }

  painter.save();
  painter.translate(15, height() / 2);
  painter.rotate(-90);
  painter.drawText(-50, 0, 100, 20, Qt::AlignCenter, "MTF (%)");
  painter.restore();

  // Helper function to draw a curve
  auto drawCurve = [&](const std::vector<double> &data, const QColor &color,
                       int lineWidth = 2) {
    if (data.empty())
      return;

    painter.setPen(QPen(color, lineWidth));
    QPointF prevPoint;

    for (size_t i = 0; i < data.size(); ++i) {
      double freq = i / (double)(data.size() - 1);
      double value = data[i] * 100.0; // Scale to percentage

      int x = chartRect.left() + (int)(freq * chartRect.width());
      int y = chartRect.bottom() - (int)((value / 100.0) * chartRect.height());

      QPointF point(x, y);

      if (i > 0)
        painter.drawLine(prevPoint, point);

      prevPoint = point;
    }
  };

  struct LegendItem {
    QString name;
    QColor color;
    int width;
    bool visible;
    const std::vector<double> *data;
  };

  // Define styling and data in one place
  LegendItem items[] = {
      {"Difraction", QColor(255, 100, 100), 2, m_canSimulateDifraction,
       &m_data.lens_difraction_mtf},
      {"Defocus", QColor(255, 165, 0), 2, m_canSimulateDifraction,
       &m_data.stokseth_defocus_mtf},
      {"Hopkins blur", QColor(139, 69, 19), 2, !m_canSimulateDifraction,
       &m_data.hopkins_blur_mtf},
      {"Gaussian blur", QColor(100, 200, 100), 2, true,
       &m_data.gaussian_blur_mtf},
      {"Lens", Qt::blue, 2, true, &m_data.lens_mtf},
      {"Sensor", Qt::gray, 2, true, &m_data.sensor_mtf},
      {"System", Qt::white, 4, true, &m_data.system_mtf},
      {"Measured MTF", Qt::red, 4, m_hasMeasuredData,
       nullptr} // Measured data handled separately
  };

  // Draw all curves
  for (const auto &item : items) {
    if (!item.visible)
      continue;

    // Standard curves
    if (item.data) {
      drawCurve(*item.data, item.color, item.width);
    }
    // Measured MTF special case
    else if (item.name == "Measured MTF" && m_hasMeasuredData &&
             m_measuredFreq.size() == m_measuredContrast.size()) {
      painter.setPen(QPen(item.color, item.width));
      QPointF prevPoint;

      for (size_t i = 0; i < m_measuredFreq.size(); ++i) {
        double freq = m_measuredFreq[i];
        double value = m_measuredContrast[i] * 0.01;

        if (freq < 0.0 || freq > 1.0)
          continue;

        int x = chartRect.left() + (int)(freq * chartRect.width());
        int y = chartRect.bottom() - (int)(value * chartRect.height());

        QPointF point(x, y);

        if (i > 0)
          painter.drawLine(prevPoint, point);

        prevPoint = point;
      }
    }
  }

  // Draw MTF50 information
  double mtf50_freq = -1;
  if (!m_data.system_mtf.empty()) {
      for (size_t i = 0; i < m_data.system_mtf.size() - 1; ++i) {
          if (m_data.system_mtf[i] >= 0.5 && m_data.system_mtf[i+1] < 0.5) {
              // Linear interpolation
              double f0 = i / (double)(m_data.system_mtf.size() - 1);
              double f1 = (i + 1) / (double)(m_data.system_mtf.size() - 1);
              double v0 = m_data.system_mtf[i];
              double v1 = m_data.system_mtf[i+1];
              mtf50_freq = f0 + (0.5 - v0) * (f1 - f0) / (v1 - v0);
              break;
          }
      }
  }

  int infoY = chartRect.bottom() + (m_scanDpi > 0 ? 85 : 45);
  painter.setPen(palette().text().color());
  if (mtf50_freq >= 0) {
      QString mtf50Text = QString("MTF50: %1 px freq").arg(mtf50_freq, 0, 'f', 3);
      if (m_scanDpi > 0) {
          double lp_mm = mtf50_freq * (m_scanDpi / 25.4) / 2.0;
          mtf50Text += QString(" (%1 lp/mm)").arg(lp_mm, 0, 'f', 1);
      }
      painter.drawText(QRect(marginLeft, infoY, chartRect.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, mtf50Text);
      infoY += 20;
  }

  // Draw Screen info
  if (m_screenFreq > 0) {
      QString screenText = QString("Screen: %1 px freq").arg(m_screenFreq, 0, 'f', 3);
      if (m_scanDpi > 0) {
          double lp_mm = m_screenFreq * (m_scanDpi / 25.4) / 2.0;
          screenText += QString(" (%1 lp/mm)").arg(lp_mm, 0, 'f', 1);
      }
      
      // Compute contrast at screen frequency
      double contrast = 0;
      if (!m_data.system_mtf.empty()) {
          double idx_f = m_screenFreq * (m_data.system_mtf.size() - 1);
          int idx = (int)idx_f;
          if (idx >= 0 && idx < (int)m_data.system_mtf.size() - 1) {
              double frac = idx_f - idx;
              contrast = m_data.system_mtf[idx] * (1.0 - frac) + m_data.system_mtf[idx+1] * frac;
          } else if (idx >= (int)m_data.system_mtf.size() - 1) {
              contrast = m_data.system_mtf.back();
          }
      }
      screenText += QString(" contrast: %1%").arg(contrast * 100.0, 0, 'f', 1);
      
      painter.drawText(QRect(marginLeft, infoY, chartRect.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, screenText);
      infoY += 20;
  }

  // Draw legend below "Pixel frequency" label
  int legendY = infoY + 15; // Below the info label
  int legendX = chartRect.left();
  int itemWidth = 120;
  int lineHeight = 20;

  int col = 0;
  for (const auto &item : items) {
    if (!item.visible)
      continue;

    int x = legendX + col * itemWidth;

    // Line sample
    painter.setPen(QPen(item.color, item.width));
    painter.drawLine(x, legendY + lineHeight / 2, x + 20,
                     legendY + lineHeight / 2);

    // Text
    painter.setPen(palette().text().color());
    painter.drawText(x + 25, legendY, 95, lineHeight,
                     Qt::AlignLeft | Qt::AlignVCenter, item.name);

    col++;
    if (col >= 3) { // 3 items per row
      col = 0;
      legendY += lineHeight;
    }
  }
}
