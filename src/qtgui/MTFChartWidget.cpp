#include "MTFChartWidget.h"
#include "spectrum-to-xyz.h"
#include "color.h"
#include <QFontMetrics>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

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

void MTFChartWidget::setMeasuredMTF(const std::vector<colorscreen::mtf_measurement> &measurements, const std::array<double, 4> &channelWavelengths) {
  m_measurements = measurements;
  m_channelWavelengths = channelWavelengths;
  m_hasMeasuredData = !measurements.empty();
  update();
}

void MTFChartWidget::clear() {
  m_hasData = false;
  m_hasMeasuredData = false;
  m_measurements.clear();
  update();
}

QSize MTFChartWidget::sizeHint() const {
  LayoutInfo layout = calculateLayout(500, 310 + 225); // Estimated for 500 width
  return QSize(500, 310 + layout.marginTop + layout.marginBottom);
}

QSize MTFChartWidget::minimumSizeHint() const {
  LayoutInfo layout = calculateLayout(300, 186 + 225);
  return QSize(300, 186 + layout.marginTop + layout.marginBottom);
}

bool MTFChartWidget::hasHeightForWidth() const { return true; }

int MTFChartWidget::heightForWidth(int width) const {
  // Maintain golden ratio: width:height = 1.61:1
  // Chart area height = width / 1.61
  int chartHeight = static_cast<int>(width / 1.61);
  LayoutInfo layout = calculateLayout(width, chartHeight + 250);
  return chartHeight + layout.marginTop + layout.marginBottom;
}

// Wavelength to RGB conversion using CIE CMFs
static QColor wavelengthToRGB(double wavelength) {
  if (wavelength < 380 || wavelength > 780)
    return Qt::white; // User requested white for not well visible/outside range

  // Interpolate CMFs
  double pos = (wavelength - SPECTRUM_START) / (double)SPECTRUM_STEP;
  int idx = (int)pos;
  double t = pos - idx;

  if (idx < 0 || idx >= SPECTRUM_SIZE - 1)
    return Qt::white;

  colorscreen::xyz c = {(colorscreen::luminosity_t)(colorscreen::cie_cmf_x[idx] * (1 - t) +
                            colorscreen::cie_cmf_x[idx + 1] * t),
                        (colorscreen::luminosity_t)(colorscreen::cie_cmf_y[idx] * (1 - t) +
                            colorscreen::cie_cmf_y[idx + 1] * t),
                        (colorscreen::luminosity_t)(colorscreen::cie_cmf_z[idx] * (1 - t) +
                            colorscreen::cie_cmf_z[idx + 1] * t)};
  colorscreen::luminosity_t r, g, b;
  (c).to_normalized_srgb(&r, &g, &b);

  QColor col(std::clamp((int)(r * 255), 0, 255),
             std::clamp((int)(g * 255), 0, 255),
             std::clamp((int)(b * 255), 0, 255));
             
  // If it's too dark or too close to background, make it white
  if (col.red() + col.green() + col.blue() < 60)
      return Qt::white;
      
  return col;
}

std::vector<MTFChartWidget::LegendItem> MTFChartWidget::getLegendItems() const {
    std::vector<LegendItem> items;
    items.push_back({"Difraction", QColor(255, 100, 100), 2, m_canSimulateDifraction, &m_data.lens_difraction_mtf});
    items.push_back({"Defocus", QColor(255, 165, 0), 2, m_canSimulateDifraction, &m_data.stokseth_defocus_mtf});
    items.push_back({"Hopkins blur", QColor(139, 69, 19), 2, !m_canSimulateDifraction, &m_data.hopkins_blur_mtf});
    items.push_back({"Gaussian blur", QColor(100, 200, 100), 2, true, &m_data.gaussian_blur_mtf});
    items.push_back({"Lens", Qt::blue, 2, true, &m_data.lens_mtf});
    items.push_back({"Sensor", Qt::gray, 2, true, &m_data.sensor_mtf});
    items.push_back({"System", Qt::white, 4, true, &m_data.system_mtf});

    if (m_hasMeasuredData) {
        for (const auto &m : m_measurements) {
            double wl = m.wavelength;
            if (m.channel >= 0 && m.channel < 4) {
                wl = m_channelWavelengths[m.channel];
            }
            QColor col = (wl > 0) ? wavelengthToRGB(wl) : Qt::white;
            items.push_back({QString::fromStdString(m.name), col, 2, true, nullptr, &m});
        }
    }
    return items;
}

MTFChartWidget::LayoutInfo MTFChartWidget::calculateLayout(int w, int h) const {
  LayoutInfo layout;
  
  // Calculate dynamic font sizes
  layout.baseFontSize = 9;
  layout.smallFontSize = 8;
  if (w > 300) {
      int delta = (w - 300) / 100;
      layout.baseFontSize = std::min(9 + delta, 14);
      layout.smallFontSize = std::min(8 + delta, 12);
  }
  layout.lineHeight = layout.baseFontSize + 10;

  // Margin Left: Title + Space + Labels + Tick
  layout.marginLeft = 45 + (layout.baseFontSize * 4); // Dynamic based on font
  layout.marginLeft = std::max(layout.marginLeft, 90);

  // Margin Top: Captions
  layout.marginTop = 45; 
  if (layout.baseFontSize > 11) layout.marginTop += (layout.baseFontSize - 11) * 8;

  // Margin Right: Padding
  layout.marginRight = 20;

  // Margin Bottom components calculation
  // Legend items count and height
  auto allItems = getLegendItems();
  int numVisibleItems = 0;
  for (const auto &item : allItems) if (item.visible) numVisibleItems++;
  
  int itemWidth = std::max(120, (int)(120 * (layout.baseFontSize / 9.0)));
  int col = 0;
  int legendRows = numVisibleItems > 0 ? 1 : 0;
  for (int i = 0; i < numVisibleItems; ++i) {
      int x = layout.marginLeft + col * itemWidth;
      col++;
      if (col >= 3 || (x + itemWidth * 2 > w)) {
          col = 0;
          if (i < numVisibleItems - 1) legendRows++;
      }
  }
  layout.legendHeight = legendRows * layout.lineHeight + 20;
  
  layout.infoSectionHeight = 0;
  if (m_hasData) {
      // MTF50 check (pre-calculation)
      bool has_mtf50 = false;
      if (!m_data.system_mtf.empty() && isVisible("System")) {
          for (size_t i = 0; i < m_data.system_mtf.size() - 1; ++i) {
              if (m_data.system_mtf[i] >= 0.5 && m_data.system_mtf[i+1] < 0.5) {
                  has_mtf50 = true;
                  break;
              }
          }
      }
      if (has_mtf50) layout.infoSectionHeight += layout.lineHeight;
  }
  if (m_screenFreq > 0) layout.infoSectionHeight += layout.lineHeight;
  
  // Axis labels height
  double labelsHeightRatio = m_scanDpi > 0 ? 4.3 : 2.8;
  int labelsHeight = (int)(labelsHeightRatio * layout.lineHeight);
  if (m_scanDpi > 0) labelsHeight += 4;
  
  layout.marginBottom = labelsHeight + layout.infoSectionHeight + layout.legendHeight;
  
  layout.chartRect = QRect(layout.marginLeft, layout.marginTop, 
                           std::max(10, w - layout.marginLeft - layout.marginRight),
                           std::max(10, h - layout.marginTop - layout.marginBottom));
                           
  // Finalize starts
  layout.infoStartY = layout.chartRect.bottom() + (int)(layout.lineHeight * (m_scanDpi > 0 ? 4.3 : 2.8));
  if (m_scanDpi > 0) layout.infoStartY += 4;
  
  layout.legendStartY = layout.infoStartY + layout.infoSectionHeight + (int)(layout.lineHeight * 0.5);
  
  return layout;
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

  LayoutInfo layout = calculateLayout(width(), height());
  QRect chartRect = layout.chartRect;

  if (chartRect.width() < 10 || chartRect.height() < 10)
    return;

  QFont baseFont = painter.font();
  baseFont.setPointSize(layout.baseFontSize);
  
  QFont smallFont = baseFont;
  smallFont.setPointSize(layout.smallFontSize);

  // Draw axes
  painter.setPen(QPen(palette().text().color(), 1));
  painter.drawRect(chartRect);

  // Y-axis labels (0-100%)
  painter.setFont(baseFont);
  for (int i = 0; i <= 10; i++) {
    int y = chartRect.bottom() - (chartRect.height() * i / 10);
    painter.drawLine(chartRect.left() - 5, y, chartRect.left(), y);

    QString label = QString::number(i * 10);
    QRect textRect(0, y - 10, layout.marginLeft - 10, 20);
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

  painter.setFont(smallFont);
  painter.setPen(palette().text().color());

  // Nyquist caption
  painter.drawText(nyquistX - 40, chartRect.top() - (int)(layout.lineHeight * 1.0), 80, layout.lineHeight, Qt::AlignCenter,
                   "Nyquist");

  // Screen caption (avoiding overlap)
  if (screenX != -1) {
      int yOff = (int)(layout.lineHeight * 1.0);
      if (std::abs(screenX - nyquistX) < 50) {
          yOff = (int)(layout.lineHeight * 1.8); // Move it higher up
      }
      painter.setPen(QColor(100, 100, 255));
      painter.drawText(screenX - 40, chartRect.top() - yOff, 80, layout.lineHeight, Qt::AlignCenter,
                       "Screen");
  }
  
  painter.setFont(baseFont);

  // Axis labels
  painter.setPen(palette().text().color());
  int axisLabelY = chartRect.bottom() + (int)(layout.lineHeight * 0.8);
  painter.drawText(QRect(layout.marginLeft, axisLabelY, chartRect.width(), layout.lineHeight),
                   Qt::AlignCenter, "Pixel frequency");
                   
  if (m_scanDpi > 0) {
    int lpMmY = axisLabelY + layout.lineHeight + 4;
    // Draw cycles per mm axis (lp/mm)
    double lp_mm_max = (m_scanDpi / 25.4);

    for (int i = 0; i <= 10; i++) {
        int x = chartRect.left() + (chartRect.width() * i / 10);
        painter.drawLine(x, lpMmY - 5, x, lpMmY);
        
        double lp_mm = (i / 10.0) * lp_mm_max;
        QString label = QString::number(lp_mm, 'f', 1);
        QRect textRect(x - 30, lpMmY, 60, layout.lineHeight);
        painter.drawText(textRect, Qt::AlignCenter, label);
    }
    int titleY = lpMmY + layout.lineHeight;
    painter.drawText(QRect(layout.marginLeft, titleY, chartRect.width(), layout.lineHeight),
                     Qt::AlignCenter, "Cycles per millimeter (lp/mm)");
  }

  painter.save();
  // Move MTF title further left for larger fonts
  painter.translate(std::max(10, layout.marginLeft - 75), (chartRect.top() + chartRect.bottom()) / 2);
  painter.rotate(-90);
  painter.drawText(-100, 0, 200, 30, Qt::AlignCenter, "MTF (%)");
  painter.restore();

  // Helper function to draw a curve
  auto drawCurve = [&](const std::vector<double> &data, const QColor &color,
                       int lineWidth = 2, Qt::PenStyle style = Qt::SolidLine) {
    if (data.empty())
      return;

    painter.setPen(QPen(color, lineWidth, style));
    QPainterPath path;

    for (size_t i = 0; i < data.size(); ++i) {
      double freq = i / (double)(data.size() - 1);
      double value = data[i] * 100.0; // Scale to percentage

      int x = chartRect.left() + (int)(freq * chartRect.width());
      int y = chartRect.bottom() - (int)((value / 100.0) * chartRect.height());

      if (i == 0)
        path.moveTo(x, y);
      else
        path.lineTo(x, y);
    }
    painter.drawPath(path);
  };

  auto items = getLegendItems();

  // Draw all curves
  for (const auto &item : items) {
    if (!item.visible || !isVisible(item.name))
      continue;

    // Standard curves
    if (item.data) {
      drawCurve(*item.data, item.color, item.width);
    } else if (item.measurement) {
        // Measured MTFs
        painter.setPen(QPen(item.color, 2, Qt::DotLine));
        QPainterPath path;
        for (size_t i = 0; i < item.measurement->size(); ++i) {
            double freq = item.measurement->get_freq(i);
            double value = item.measurement->get_contrast(i) * 0.01;
            if (freq < 0.0 || freq > 1.0) continue;
            int x = chartRect.left() + (int)(freq * chartRect.width());
            int y = chartRect.bottom() - (int)(value * chartRect.height());
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        painter.drawPath(path);
    }
  }

  // Draw MTF50 information
  double mtf50_freq = -1;
  if (!m_data.system_mtf.empty() && isVisible("System")) {
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

  int infoY = layout.infoStartY;
  painter.setFont(baseFont);
  painter.setPen(palette().text().color());
  if (mtf50_freq >= 0) {
      QString mtf50Text = QString("MTF50: %1 px freq").arg(mtf50_freq, 0, 'f', 3);
      if (m_scanDpi > 0) {
          double lp_mm = mtf50_freq * (m_scanDpi / 25.4);
          mtf50Text += QString(" (%1 lp/mm)").arg(lp_mm, 0, 'f', 1);
      }
      painter.drawText(QRect(layout.marginLeft, infoY, chartRect.width(), layout.lineHeight), Qt::AlignLeft | Qt::AlignVCenter, mtf50Text);
      infoY += layout.lineHeight;
  }

  // Draw Screen info
  if (m_screenFreq > 0) {
      QString screenText = QString("Screen: %1 px freq").arg(m_screenFreq, 0, 'f', 3);
      if (m_scanDpi > 0) {
          double lp_mm = m_screenFreq * (m_scanDpi / 25.4);
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
      
      painter.drawText(QRect(layout.marginLeft, infoY, chartRect.width(), layout.lineHeight), Qt::AlignLeft | Qt::AlignVCenter, screenText);
      infoY += layout.lineHeight;
  }

  // Draw legend
  int legendY = layout.legendStartY;
  int legendX = chartRect.left();
  int itemWidth = std::max(120, (int)(120 * (layout.baseFontSize / 9.0)));

  int col = 0;
  for (const auto &item : items) {
    if (!item.visible)
      continue;

    int x = legendX + col * itemWidth;
    bool visible = isVisible(item.name);

    // Line sample
    QColor itemColor = item.color;
    if (!visible) itemColor = palette().mid().color();
    
    painter.setPen(QPen(itemColor, item.width, item.measurement ? Qt::DotLine : Qt::SolidLine));
    painter.drawLine(x, legendY + layout.lineHeight / 2, x + 20,
                     legendY + layout.lineHeight / 2);

    // Text
    painter.setPen(visible ? palette().text().color() : palette().mid().color());
    painter.drawText(x + 25, legendY, itemWidth - 25, layout.lineHeight,
                     Qt::AlignLeft | Qt::AlignVCenter, item.name);

    col++;
    if (col >= 3 || (x + itemWidth * 2 > width())) { // wrap if no space
      col = 0;
      legendY += layout.lineHeight;
    }
  }
}

void MTFChartWidget::mousePressEvent(QMouseEvent *event) {
    LayoutInfo layout = calculateLayout(width(), height());
    
    int legendY = layout.legendStartY;
    int legendX = layout.chartRect.left();
    int itemWidth = std::max(120, (int)(120 * (layout.baseFontSize / 9.0)));

    auto items = getLegendItems();
    int col = 0;
    for (const auto &item : items) {
        if (!item.visible) continue;

        int x = legendX + col * itemWidth;
        QRect itemRect(x, legendY, itemWidth, layout.lineHeight);
        
        if (itemRect.contains(event->pos())) {
            if (m_hiddenItems.count(item.name)) {
                m_hiddenItems.erase(item.name);
            } else {
                m_hiddenItems.insert(item.name);
            }
            update();
            return;
        }

        col++;
        if (col >= 3 || (x + itemWidth * 2 > width())) {
            col = 0;
            legendY += layout.lineHeight;
        }
    }
    
    QWidget::mousePressEvent(event);
}
