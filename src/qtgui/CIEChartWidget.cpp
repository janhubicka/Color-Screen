#include "CIEChartWidget.h"
#include "../libcolorscreen/include/spectrum-to-xyz.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace colorscreen;

CIEChartWidget::CIEChartWidget(QWidget *parent) : QWidget(parent) {
  setMinimumSize(200, 200);
  updateLocus();
}

CIEChartWidget::~CIEChartWidget() = default;

QSize CIEChartWidget::sizeHint() const {
  return QSize(400, 400);
}

void CIEChartWidget::setWhitepoint(double x, double y) {
  if (m_selectedX == x && m_selectedY == y)
    return;
  m_selectedX = x;
  m_selectedY = y;
  update();
}

std::pair<double, double> CIEChartWidget::getWhitepoint() const {
  return {m_selectedX, m_selectedY};
}

void CIEChartWidget::updateLocus() {
  m_locus.clear();
  // Build spectral locus from CMF data
  // Assuming cie_cmf_* arrays are available and linked
  for (int i = 0; i < SPECTRUM_SIZE; ++i) {
    double x = cie_cmf_x[i];
    double y = cie_cmf_y[i];
    double z = cie_cmf_z[i];
    double sum = x + y + z;
    if (sum > 0) {
      m_locus << QPointF(x / sum, y / sum);
    }
  }
  // Close polygon
  if (!m_locus.isEmpty())
    m_locus << m_locus.first();
}

void CIEChartWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Use theme colors
  QColor textColor = palette().color(QPalette::Text);
  QColor axisColor = palette().color(QPalette::Mid); // Or WindowText slightly dimmed
  if (axisColor.lightness() < 50) axisColor = Qt::lightGray; // Ensure visibility on dark theme fallback
  
  // Background: Transparent (handled by widget background or parent)
  // Remove direct fillRect(..., Qt::white)
  
  QRectF r = getChartRect();
  
  // Draw Axes
  painter.setPen(QPen(axisColor, 1));
  // Grid lines
  for (double x = 0.0; x <= 0.81; x += 0.1) {
      QPointF p = mapToWidget(x, 0.0);
      QPointF top = mapToWidget(x, 0.9);
      painter.drawLine(QPointF(p.x(), r.bottom()), QPointF(p.x(), r.top()));
  }
  for (double y = 0.0; y <= 0.91; y += 0.1) {
      QPointF p = mapToWidget(0.0, y);
      painter.drawLine(QPointF(r.left(), p.y()), QPointF(r.right(), p.y()));
  }
  
  if (m_cache.isNull() || m_cache.size() != size()) {
    generateCache();
  }
  painter.drawImage(0, 0, m_cache);

  // Map locus to widget coordinates for outline
  QPolygonF screenLocus;
  for (const auto &pt : m_locus) {
    screenLocus << mapToWidget(pt.x(), pt.y());
  }

  // Draw Locus Outline
  painter.setPen(QPen(textColor, 2));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(screenLocus);

  // Draw Axes Labels
  painter.setPen(textColor);
  QFont font = painter.font();
  font.setPointSize(8);
  painter.setFont(font);
  
  // X Axis
  for (double x = 0.0; x <= 0.81; x += 0.1) {
      QPointF p = mapToWidget(x, 0.0);
      painter.drawText(QRectF(p.x() - 15, r.bottom() + 2, 30, 15), Qt::AlignCenter, QString::number(x, 'f', 1));
  }
  painter.drawText(QRectF(r.right() + 5, r.bottom() - 15, 30, 15), Qt::AlignLeft, "x");

  // Y Axis
  for (double y = 0.0; y <= 0.91; y += 0.1) {
      QPointF p = mapToWidget(0.0, y);
      painter.drawText(QRectF(r.left() - 35, p.y() - 7, 30, 15), Qt::AlignRight, QString::number(y, 'f', 1));
  }
  painter.drawText(QRectF(r.left() - 15, r.top() - 20, 30, 20), Qt::AlignCenter, "y");

  // Format Wavelengths
  painter.setPen(textColor);
  for (int wl = 380; wl <= 700; wl += 20) { // Limit to visible 700ish for neatness or go full
      int i = (wl - SPECTRUM_START) / SPECTRUM_STEP;
      if (i < 0 || i >= SPECTRUM_SIZE) continue;
      
      double x = cie_cmf_x[i];
      double y = cie_cmf_y[i];
      double z = cie_cmf_z[i];
      double sum = x + y + z;
      if (sum <= 0) continue;
      
      QPointF p = mapToWidget(x/sum, y/sum);
      
      // Draw tick
      // Calculate normal direction or just point outwards?
      // Simple heuristic: pointing away from center (0.33, 0.33)
      QPointF center = mapToWidget(0.333, 0.333);
      QPointF dir = p - center;
      double len = std::sqrt(dir.x()*dir.x() + dir.y()*dir.y());
      if (len > 0) dir /= len;
      
      QPointF tickEnd = p + dir * 5.0;
      painter.drawLine(p, tickEnd);
      
      // Draw Label for 380, 450, 500, etc. (less crowded)
      bool major = (wl % 40 == 0) || wl == 380 || wl == 700;
      if (major) {
          QPointF textPos = p + dir * 15.0;
          painter.drawText(QRectF(textPos.x()-15, textPos.y()-7, 30, 15), Qt::AlignCenter, QString::number(wl));
      }
  }

  // Draw D50 and D65
  auto drawMarker = [&](const xyz &white, const QString &label) {
    xy_t xy(white);
    QPointF p = mapToWidget(xy.x, xy.y);
    painter.setPen(QPen(textColor, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    double r = 4;
    painter.drawEllipse(p, r, r);

    painter.setPen(textColor);
    painter.drawText(p + QPointF(6, 4), label);
  };

  drawMarker(d50_white, "D50");
  drawMarker(d65_white, "D65");

  // Draw Reference Gamut
  if (m_referenceGamut.valid) {
      QPointF rPt = mapToWidget(m_referenceGamut.rx, m_referenceGamut.ry);
      QPointF gPt = mapToWidget(m_referenceGamut.gx, m_referenceGamut.gy);
      QPointF bPt = mapToWidget(m_referenceGamut.bx, m_referenceGamut.by);
      QPointF wPt = mapToWidget(m_referenceGamut.wx, m_referenceGamut.wy);
      
      QPolygonF triangle;
      triangle << rPt << gPt << bPt;
      
      painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
      painter.setBrush(Qt::NoBrush);
      painter.drawPolygon(triangle);
  }

  // Draw selected point
  if (m_selectionEnabled) {
      QPointF pt = mapToWidget(m_selectedX, m_selectedY);
      painter.setPen(QPen(Qt::red, 2));
      double rPtSize = 6.0;
      painter.drawLine(pt - QPointF(rPtSize, 0), pt + QPointF(rPtSize, 0));
      painter.drawLine(pt - QPointF(0, rPtSize), pt + QPointF(0, rPtSize));
  }

  if (m_gamut.valid) {
      QPointF rPt = mapToWidget(m_gamut.rx, m_gamut.ry);
      QPointF gPt = mapToWidget(m_gamut.gx, m_gamut.gy);
      QPointF bPt = mapToWidget(m_gamut.bx, m_gamut.by);
      QPointF wPt = mapToWidget(m_gamut.wx, m_gamut.wy);
      
      QPolygonF triangle;
      triangle << rPt << gPt << bPt;
      
      painter.setPen(QPen(Qt::white, 2));
      painter.setBrush(Qt::NoBrush);
      painter.drawPolygon(triangle);
      
      auto drawDot = [&](QPointF p, QColor c) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(c);
          painter.drawEllipse(p, 4, 4);
      };

      drawDot(rPt, Qt::red);
      drawDot(gPt, Qt::green);
      drawDot(bPt, Qt::blue);
      drawDot(wPt, Qt::white);
      
      painter.setPen(Qt::white);
      painter.drawText(wPt + QPointF(6, 4), "WP");
  }

}

void CIEChartWidget::setGamut(const GamutData& gamut) {
    m_gamut = gamut;
    update();
}

void CIEChartWidget::setReferenceGamut(const GamutData& gamut) {
    m_referenceGamut = gamut;
    update();
}

void CIEChartWidget::setSelectionEnabled(bool enabled) {
    m_selectionEnabled = enabled;
    update();
}

void CIEChartWidget::generateCache() {
  m_cache = QImage(size(), QImage::Format_ARGB32_Premultiplied);
  m_cache.fill(Qt::transparent); // Match widget background by being transparent

  QPolygonF screenLocus;
  for (const auto &pt : m_locus) {
    screenLocus << mapToWidget(pt.x(), pt.y());
  }

  // Bounding rect for optimization
  QRect bounds = screenLocus.boundingRect().toRect().intersected(rect());

  // Pixel generation
  for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
    QRgb *scanLine = (QRgb *)m_cache.scanLine(y);
    for (int x = bounds.left(); x <= bounds.right(); ++x) {
      QPointF p(x, y);
      if (screenLocus.containsPoint(p, Qt::OddEvenFill)) {
        auto val = mapFromWidget(p);
        // Convert xy to RGB
        // Assume Y = 1.0 (maximum brightness)
        double X = val.first;
        double Y = 1.0;
        double Z = (1.0 - val.first - val.second);

        if (val.second <= 0.001)
          continue; // Avoid div by zero
        X = X / val.second;
        Z = Z / val.second;

        luminosity_t r, g, b;
        xyz_to_srgb(X, Y, Z, &r, &g, &b);

        // Simple gamut mapping/clipping
        // Find max component to normalize brightness if it exceeds 1
        double maxComp = std::max({r, g, b});
        if (maxComp > 1.0) {
          r /= maxComp;
          g /= maxComp;
          b /= maxComp;
        }

        // Clamp to 0-1
        r = std::clamp((double)r, 0.0, 1.0) * 0.3;
        g = std::clamp((double)g, 0.0, 1.0) * 0.3;
        b = std::clamp((double)b, 0.0, 1.0) * 0.3;

        scanLine[x] = qRgb(r * 255, g * 255, b * 255);
      }
    }
  }
}

void CIEChartWidget::resizeEvent(QResizeEvent *event) {
  m_cache = QImage(); // Invalidate
  QWidget::resizeEvent(event);
}

void CIEChartWidget::mousePressEvent(QMouseEvent *event) {
  if (!m_selectionEnabled) return;
  if (event->button() == Qt::LeftButton) {
    auto val = mapFromWidget(event->pos());
    // Basic bounds check (0-1)
    if (val.first >= 0 && val.first <= 1 && val.second >= 0 &&
        val.second <= 1) {
      // Optional: Check if inside locus or close to it?
      // For now, allow selection logic to handle or just emit.
      // Usually whitepoints are well inside.
      // We'll update and emit.
      setWhitepoint(val.first, val.second);
      emit whitepointChanged(val.first, val.second);
    }
  }
}

void CIEChartWidget::mouseMoveEvent(QMouseEvent *event) {
  if (!m_selectionEnabled) return;
  if (event->buttons() & Qt::LeftButton) {
    auto val = mapFromWidget(event->pos());
    if (val.first >= 0 && val.first <= 1 && val.second >= 0 &&
        val.second <= 1) {
      setWhitepoint(val.first, val.second);
      emit whitepointChanged(val.first, val.second);
    }
  }
}

QPointF CIEChartWidget::mapToWidget(double x, double y) const {
  QRectF r = getChartRect();
  
  double nx = (x - m_minX) / (m_maxX - m_minX);
  double ny = (y - m_minY) / (m_maxY - m_minY);

  // Y is inverted in screen coords
  return QPointF(r.left() + nx * r.width(), r.bottom() - ny * r.height());
}

std::pair<double, double>
CIEChartWidget::mapFromWidget(const QPointF &p) const {
  QRectF r = getChartRect();

  double nx = (p.x() - r.left()) / r.width();
  double ny = (r.bottom() - p.y()) / r.height();

  double x = m_minX + nx * (m_maxX - m_minX);
  double y = m_minY + ny * (m_maxY - m_minY);
  return {x, y};
}

QRectF CIEChartWidget::getChartRect() const {
    double w = width();
    double h = height();
    // Keep 1:1 aspect ratio for the plot area (0.8 width vs 0.9 height in data)
    double rangeX = m_maxX - m_minX;
    double rangeY = m_maxY - m_minY;
    
    // Add margins for axes
    double marginL = 40;
    double marginR = 20;
    double marginT = 20;
    double marginB = 40;
    
    double availW = w - marginL - marginR;
    double availH = h - marginT - marginB;
    
    if (availW < 10) availW = 10;
    if (availH < 10) availH = 10;
    
    double scale = std::min(availW / rangeX, availH / rangeY);
    
    double plotW = rangeX * scale;
    double plotH = rangeY * scale;
    
    // Center it
    double x = marginL + (availW - plotW) / 2.0;
    double y = marginT + (availH - plotH) / 2.0;
    
    return QRectF(x, y, plotW, plotH);
}
