#include "NavigationView.h"
#include "Renderer.h"
#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QThread>
#include <QVBoxLayout>
#include <QtMath> // qMin, qMax

// We need access to render_type definitions? already in Renderer.h
// Logic:
// render_type_fast if scr_to_img type != Random
// render_type_original (color=true) otherwise

NavigationView::NavigationView(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Top filler (image area is painted on widget background/area)
  layout->addStretch(1);

  m_zoomSlider = new QSlider(Qt::Horizontal, this);
  m_zoomSlider->setRange(0, 100);
  connect(m_zoomSlider, &QSlider::valueChanged, this,
          &NavigationView::onSliderValueChanged);
  layout->addWidget(m_zoomSlider);

  setMinimumHeight(200);
}

NavigationView::~NavigationView() {
  if (m_renderThread) {
    m_renderThread->requestInterruption();
    m_renderThread->quit();
    m_renderThread->wait();
  }
}

void NavigationView::setImage(std::shared_ptr<colorscreen::image_data> scan,
                              colorscreen::render_parameters *rparams,
                              colorscreen::scr_to_img_parameters *scrToImg,
                              colorscreen::scr_detect_parameters *scrDetect) {
  // Store new parameters
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;
  // Don't set m_scan yet - will set it after cleanup
  if (m_scan != scan) {
    m_previewImage = QImage();
  }

  // Determine render type
  if (m_scrToImg && m_scrToImg->type != colorscreen::Random) {
    // Find fast
    m_renderType.type = colorscreen::render_type_fast;
  } else {
    m_renderType.type = colorscreen::render_type_original;
    m_renderType.color = true;
  }
  // Antialias?
  m_renderType.antialias = true; // Always look nice

  // Cancel current progress if exists
  if (m_currentProgress) {
    m_currentProgress->cancel();
  }

  // Clear old scan to release it from memory
  m_scan = nullptr;

  // Clean up old renderer and thread
  if (m_renderer) {
    // Disconnect all signals to prevent callbacks during shutdown
    disconnect(m_renderer, nullptr, this, nullptr);
    m_renderer->deleteLater();
    m_renderer = nullptr;
  }

  if (m_renderThread) {
    // Disconnect thread signals to prevent deadlock
    disconnect(m_renderThread, nullptr, nullptr, nullptr);

    // Ensure thread stops
    if (m_renderThread->isRunning()) {
      m_renderThread->requestInterruption();
      m_renderThread->quit();
      m_renderThread->wait(); // Wait indefinitely for clean shutdown
    }
    // Safe to delete immediately
    delete m_renderThread;
    m_renderThread = nullptr;
  }

  // Manually emit progressFinished for current progress
  if (m_currentProgress) {
    emit progressFinished(m_currentProgress);
    m_currentProgress.reset();
  }

  // Reset render queue state for new renderer
  m_renderInProgress = false;
  m_hasPendingRender = false;

  // Now set the new scan pointer (old one is released)
  m_scan = scan;

  if (m_scan && m_rparams) {
    m_renderThread = new QThread(this);
    static colorscreen::scr_to_img_parameters defaultScrToImg;
    static colorscreen::scr_detect_parameters defaultScrDetect;

    m_renderer = new Renderer(
        m_scan, *m_rparams, m_scrToImg ? *m_scrToImg : defaultScrToImg,
        m_scrDetect ? *m_scrDetect : defaultScrDetect, m_renderType);
    m_renderer->moveToThread(m_renderThread);

    connect(m_renderThread, &QThread::finished, m_renderer,
            &QObject::deleteLater);
    connect(m_renderer, &Renderer::imageReady, this,
            &NavigationView::onImageReady);
    m_renderThread->start();

    requestRender();
  }
  update();
}

void NavigationView::updateParameters(
    colorscreen::render_parameters *rparams,
    colorscreen::scr_to_img_parameters *scrToImg,
    colorscreen::scr_detect_parameters *scrDetect) {
  // Update parameter pointers
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;

  // Update renderer's cached parameters if it exists
  // Note: NavigationView always uses its own m_renderType with antialias=true
  if (m_renderer) {
    QMetaObject::invokeMethod(
        m_renderer, "updateParameters", Qt::QueuedConnection,
        Q_ARG(colorscreen::render_parameters, *m_rparams),
        Q_ARG(colorscreen::scr_to_img_parameters,
              m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters()),
        Q_ARG(colorscreen::scr_detect_parameters,
              m_scrDetect ? *m_scrDetect
                          : colorscreen::scr_detect_parameters()),
        Q_ARG(colorscreen::render_type_parameters, m_renderType));
  }

  // Request re-render
  requestRender();
}

void NavigationView::resizeEvent(QResizeEvent *event) { requestRender(); }

void NavigationView::requestRender() {
  if (!m_renderer || !m_scan)
    return;

  // If render is in progress, cancel it and mark this as pending
  if (m_renderInProgress) {
    if (m_currentProgress) {
      m_currentProgress->cancel();
    }
    m_hasPendingRender = true;
    return; // Don't start render yet, wait for current to finish
  }

  // No render in progress, start new one
  m_renderInProgress = true;
  m_hasPendingRender = false;

  // Render to fit widget size
  // Calculate rotation to know dimensions
  double rot = m_scrToImg ? m_scrToImg->final_rotation : 0.0;
  int angle = (int)rot % 360;
  if (angle < 0)
    angle += 360;

  double imgW = m_scan->width;
  double imgH = m_scan->height;
  if (angle == 90 || angle == 270)
    std::swap(imgW, imgH);

  // Available size for image (above slider)
  int sliderHeight = m_zoomSlider->sizeHint().height();
  int availH = height() - sliderHeight;
  if (availH <= 0)
    availH = 100;

  int availW = width();
  if (availW <= 0)
    availW = 100;

  // Scale to fit
  double scaleX = availW / imgW;
  double scaleY = availH / imgH;
  double scale = qMin(scaleX, scaleY);
  if (scale <= 0)
    scale = 0.1;

  m_previewScale = scale;

  // Render size
  int targetW = (int)(imgW * scale);
  int targetH = (int)(imgH * scale);
  if (targetW <= 0)
    targetW = 1;
  if (targetH <= 0)
    targetH = 1;

  m_currentProgress = std::make_shared<colorscreen::progress_info>();
  m_currentProgress->set_task("Rendering preview", 0);
  emit progressStarted(m_currentProgress);

  // Request render
  // NOTE: Renderer expects xOffset/yOffset in Rotated Coordinate System (Widget
  // Pixels) if we follow ImageWidget logic? In ImageWidget, xOffset passed to
  // render was `viewX * scale`. Here we want full image. `viewX` equivalent is
  // 0.

  // Renderer logic:
  // `tile.width` = targetW.
  // `tile.step` = 1.0 / scale.
  // `tile.pos` = xOffset, yOffset.

  QMetaObject::invokeMethod(
      m_renderer, "render", Qt::QueuedConnection,
      Q_ARG(int, 0),      // reqId not needed anymore
      Q_ARG(double, 0.0), // xOffset
      Q_ARG(double, 0.0), // yOffset
      Q_ARG(double, scale), Q_ARG(int, targetW), Q_ARG(int, targetH),
      Q_ARG(colorscreen::render_parameters, *m_rparams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, m_currentProgress));
}

void NavigationView::onImageReady(int reqId, QImage image, double x, double y,
                                  double scale, bool success) {
  // This is always the current render completing (only one active at a time)
  if (m_currentProgress) {
    emit progressFinished(m_currentProgress);
    m_currentProgress.reset();
  }

  // Mark render as no longer in progress
  m_renderInProgress = false;

  // Only update preview image if render was successful
  if (success) {
    m_previewImage = image;
    update();
  }
  // If render failed or was cancelled, do nothing (keep old image)

  // Start pending render if one is queued
  if (m_hasPendingRender) {
    m_hasPendingRender = false;
    requestRender();
  }
}

void NavigationView::onViewStateChanged(QRectF visibleRect, double scale) {
  m_visibleRect = visibleRect;
  m_mainScale = scale;

  // Update slider based on scale
  // Range: minScale -> 10.0
  // Slider 0 -> 100
  if (m_minScale > 0) {
    // Logarithmic scale
    // scale = minScale * (10/minScale)^t where t in [0,1]
    // scale/minScale = (10/minScale)^t
    // log(scale/minScale) = t * log(10/minScale)
    // t = log(scale/minScale) / log(10/minScale)

    double ratio = scale / m_minScale;
    double maxRatio = 10.0 / m_minScale;

    if (ratio < 1.0)
      ratio = 1.0;
    if (maxRatio < 1.0)
      maxRatio = 1.0; // Should not happen if minScale < 10

    double t = 0;
    if (maxRatio > 1.0) {
      t = std::log(ratio) / std::log(maxRatio);
    }

    if (t < 0)
      t = 0;
    if (t > 1)
      t = 1;
    m_zoomSlider->blockSignals(true);
    m_zoomSlider->setValue((int)(t * 100));
    m_zoomSlider->blockSignals(false);
  }

  update();
}

void NavigationView::setMinScale(double scale) { m_minScale = scale; }

void NavigationView::onSliderValueChanged(int value) {
  double t = value / 100.0;
  // Logarithmic scale
  // scale = minScale * pow(10/minScale, t)

  double targetScale = m_minScale;
  if (m_minScale > 0) {
    double maxRatio = 10.0 / m_minScale;
    if (maxRatio >= 1.0) {
      targetScale = m_minScale * std::pow(maxRatio, t);
    } else {
      targetScale = m_minScale;
    }
  }

  emit zoomChanged(targetScale);
}

void NavigationView::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), Qt::black);

  if (m_previewImage.isNull())
    return;

  // Draw centered
  // We calculated scale based on available size, but widget might have resized.
  // Ideally we re-render or stick to corner.
  // Using current m_previewImage width/height.

  QRect viewRect = rect();
  if (m_zoomSlider->isVisible())
    viewRect.setBottom(m_zoomSlider->geometry().top());

  int imgW = m_previewImage.width();
  int imgH = m_previewImage.height();

  int x = viewRect.left() + (viewRect.width() - imgW) / 2;
  int y = viewRect.top() + (viewRect.height() - imgH) / 2;

  m_imageRect = QRect(x, y, imgW, imgH);
  p.drawImage(x, y, m_previewImage);

  // Draw Viewport Rect
  // m_visibleRect is in Rotated Image Pixels.
  // m_previewScale is PreviewPixels / RotatedImagePixels.
  // viewport in preview:
  double vx = m_visibleRect.x() * m_previewScale;
  double vy = m_visibleRect.y() * m_previewScale;
  double vw = m_visibleRect.width() * m_previewScale;
  double vh = m_visibleRect.height() * m_previewScale;

  QRectF rect(x + vx, y + vy, vw, vh);
  p.setPen(QPen(Qt::red, 2));
  p.setBrush(Qt::NoBrush);
  p.drawRect(rect);
}

void NavigationView::mousePressEvent(QMouseEvent *event) {
  if (m_imageRect.contains(event->pos())) {
    m_isDragging = true;

    double clickX = event->pos().x() - m_imageRect.left();
    double clickY = event->pos().y() - m_imageRect.top();
    
    double imageX = clickX / m_previewScale;
    double imageY = clickY / m_previewScale;
    
    // Check if click is inside current visible rect
    if (m_visibleRect.contains(imageX, imageY)) {
        // Grab logic: calculate offset from center
        double centerX = m_visibleRect.center().x();
        double centerY = m_visibleRect.center().y();
        
        m_dragOffset = QPointF(imageX - centerX, imageY - centerY);
    } else {
        // Jump logic: center on click, reset offset
        m_dragOffset = QPointF(0, 0);
        
        double hw = m_visibleRect.width() / 2.0;
        double hh = m_visibleRect.height() / 2.0;
        emit panChanged(imageX - hw, imageY - hh);
    }
  }
}

void NavigationView::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging) {
    double clickX = event->pos().x() - m_imageRect.left();
    double clickY = event->pos().y() - m_imageRect.top();

    double imageX = clickX / m_previewScale;
    double imageY = clickY / m_previewScale;

    // Apply offset
    double targetCenterX = imageX - m_dragOffset.x();
    double targetCenterY = imageY - m_dragOffset.y();

    double hw = m_visibleRect.width() / 2.0;
    double hh = m_visibleRect.height() / 2.0;

    emit panChanged(targetCenterX - hw, targetCenterY - hh);
  }
}

void NavigationView::mouseReleaseEvent(QMouseEvent *event) {
  m_isDragging = false;
}
