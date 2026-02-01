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

  if (m_cache.isNull() || m_cache.size() != size()) {
    generateCache();
  }

  // Draw cached colored shape
  painter.drawImage(0, 0, m_cache);

  // Map locus to widget coordinates for outline
  QPolygonF screenLocus;
  for (const auto &pt : m_locus) {
    screenLocus << mapToWidget(pt.x(), pt.y());
  }

  // Draw Locus Outline
  painter.setPen(QPen(Qt::black, 2));
  painter.setBrush(Qt::NoBrush);
  painter.drawPolygon(screenLocus);

  // Draw D50 and D65
  auto drawMarker = [&](const xyz &white, const QString &label) {
    xy_t xy(white);
    QPointF p = mapToWidget(xy.x, xy.y);
    painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    double r = 4;
    painter.drawEllipse(p, r, r);

    painter.setPen(Qt::gray);
    painter.drawText(p + QPointF(6, 4), label);
  };

  drawMarker(d50_white, "D50");
  drawMarker(d65_white, "D65");

  // Draw selected point
  QPointF pt = mapToWidget(m_selectedX, m_selectedY);
  painter.setPen(QPen(Qt::red, 2));
  double r = 6.0;
  painter.drawLine(pt - QPointF(r, 0), pt + QPointF(r, 0));
  painter.drawLine(pt - QPointF(0, r), pt + QPointF(0, r));

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
  double w = width();
  double h = height();
  double plotW = w * 0.9;
  double plotH = h * 0.9;
  double offsetX = w * 0.05;
  double offsetY = h * 0.05;

  double nx = (x - m_minX) / (m_maxX - m_minX);
  double ny = (y - m_minY) / (m_maxY - m_minY);

  // Y is inverted in screen coords
  return QPointF(offsetX + nx * plotW, h - (offsetY + ny * plotH));
}

std::pair<double, double>
CIEChartWidget::mapFromWidget(const QPointF &p) const {
  double w = width();
  double h = height();
  double plotW = w * 0.9;
  double plotH = h * 0.9;
  double offsetX = w * 0.05;
  double offsetY = h * 0.05;

  double nx = (p.x() - offsetX) / plotW;
  double ny = (h - p.y() - offsetY) / plotH;

  double x = m_minX + nx * (m_maxX - m_minX);
  double y = m_minY + ny * (m_maxY - m_minY);
  return {x, y};
}
