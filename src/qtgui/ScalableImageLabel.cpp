#include "ScalableImageLabel.h"
#include <QPainter>
#include <QPaintEvent>

ScalableImageLabel::ScalableImageLabel(QWidget *parent)
    : QWidget(parent) {
  // Use Expanding horizontal so it adapts to width.
  // Use Preferred vertical so it can be scaled down by layout if space is limited.
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ScalableImageLabel::setPixmap(const QPixmap &pixmap) {
  m_pixmap = pixmap;
  update();
  updateGeometry(); // Trigger layout update to pick up new sizeHint
}

QSize ScalableImageLabel::sizeHint() const {
  if (m_pixmap.isNull())
      return QSize(100, 100); 
  // Cap the size hint to a reasonable thumbnail size.
  // This prevents the layout from being gargantuan by default.
  // The widget is still allowed to grow/shrink based on sizePolicy.
  return m_pixmap.size().boundedTo(QSize(256, 256));
}

QSize ScalableImageLabel::minimumSizeHint() const {
  return QSize(10, 10);
}

int ScalableImageLabel::heightForWidth(int width) const {
  if (m_pixmap.isNull() || m_pixmap.width() <= 0)
    return width;
  return width * m_pixmap.height() / m_pixmap.width();
}

void ScalableImageLabel::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  
  // Fill background with window color to prevent artifacts
  // (e.g. trails when resizing or black bars)
  painter.fillRect(rect(), palette().window());
  
  if (m_pixmap.isNull())
    return;

  painter.setRenderHint(QPainter::Antialiasing, false);

  QSize s = size();
  
  // Calculate target size
  QSize targetSize = m_pixmap.size().scaled(s, Qt::KeepAspectRatio);
  
  // Position the image based on alignment
  int x = 0;
  if (m_alignment & Qt::AlignHCenter) {
    x = (s.width() - targetSize.width()) / 2;
  } else if (m_alignment & Qt::AlignRight) {
    x = s.width() - targetSize.width();
  }
  
  int y = 0;
  if (m_alignment & Qt::AlignVCenter) {
    y = (s.height() - targetSize.height()) / 2;
  } else if (m_alignment & Qt::AlignBottom) {
    y = s.height() - targetSize.height();
  }

  QRect targetRect(x, y, targetSize.width(), targetSize.height());

  // Determine scaling mode
  // If we are scaling UP significantly (>2x), use FastTransformation (Nearest Neighbor)
  // to keep pixels sharp for diagnostic images.
  // Otherwise use SmoothTransformation.
  Qt::TransformationMode mode = Qt::SmoothTransformation;
  if (targetRect.width() > m_pixmap.width() * 2) {
    mode = Qt::FastTransformation;
  }

  painter.setRenderHint(QPainter::SmoothPixmapTransform, mode == Qt::SmoothTransformation);
  painter.drawPixmap(targetRect, m_pixmap);
}
