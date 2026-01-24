#include "NavigationView.h"
#include "Renderer.h"
#include "CoordinateTransformer.h"
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

  connect(&m_renderQueue, &TaskQueue::triggerRender, this, &NavigationView::onTriggerRender);
  connect(&m_renderQueue, &TaskQueue::progressStarted, this, &NavigationView::progressStarted);
  connect(&m_renderQueue, &TaskQueue::progressFinished, this, &NavigationView::progressFinished);
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

  // Cancel current progress if exists - handled by queue cancelAll?
  // Actually, RenderQueue::cancelAll() will handle active renders.
  m_renderQueue.cancelAll();

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

  // Reset render queue state for new renderer
  
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

    m_renderQueue.requestRender();
  }
  update();
}


void NavigationView::onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress) {
//    qDebug() << "NavigationView::onTriggerRender reqId:" << reqId << " renderer:" << m_renderer << " scan:" << m_scan.get();
    
    if (!m_renderer || !m_scan) {
//        qDebug() << "NavigationView::onTriggerRender - missing renderer or scan";
        m_renderQueue.reportFinished(reqId, false);
        return;
    }

    // 1. Calculate available size
    QRect viewRect = rect();
    if (m_zoomSlider && m_zoomSlider->isVisible()) {
        viewRect.setBottom(m_zoomSlider->geometry().top());
    }

    int w = viewRect.width();
    int h = viewRect.height();
    
    // Use CoordinateTransformer to get effective dimensions
    CoordinateTransformer transformer(m_scan.get(), *m_rparams);
    QSize transformedSize = transformer.getTransformedSize();
    int imgW = transformedSize.width();
    int imgH = transformedSize.height();
    
    double scale = 0.1;

    if (imgW > 0 && imgH > 0 && w > 0 && h > 0) {
        double scaleX = (double)w / imgW;
        double scaleY = (double)h / imgH;
        scale = qMin(scaleX, scaleY);
    }
    
    if (scale <= 0) scale = 0.1;
    m_previewScale = scale; // Update member for mouse interaction

    // Correct target size calculation based on ROTATED dimensions
    int targetW = (int)(imgW * scale);
    int targetH = (int)(imgH * scale);
    if (targetW <= 0) targetW = 1;
    if (targetH <= 0) targetH = 1;

    // 2. set current progress so onImageReady can emit finished signal
    m_currentProgress = progress;

    // 3. Invoke renderer
    bool result = QMetaObject::invokeMethod(
      m_renderer, "render", Qt::QueuedConnection,
      Q_ARG(int, reqId),      
      Q_ARG(double, 0.0), // xOffset
      Q_ARG(double, 0.0), // yOffset
      Q_ARG(double, scale), Q_ARG(int, targetW), Q_ARG(int, targetH),
      Q_ARG(colorscreen::render_parameters, *m_rparams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
      Q_ARG(const char*, "Rendering navigation"));
      
    if (!result) {
        qWarning() << "NavigationView::onTriggerRender - FAILED to invoke render method!";
        m_renderQueue.reportFinished(reqId, false);
    }
}

void NavigationView::updateParameters(
    colorscreen::render_parameters *rparams,
    colorscreen::scr_to_img_parameters *scrToImg,
    colorscreen::scr_detect_parameters *scrDetect) {
  // Update parameter pointers
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;

  // Recalculate render type based on new screen type
  if (m_scrToImg && m_scrToImg->type != colorscreen::Random) {
    m_renderType.type = colorscreen::render_type_fast;
  } else {
    m_renderType.type = colorscreen::render_type_original;
    m_renderType.color = true;
  }

  // Update renderer's cached parameters if it exists
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
  m_renderQueue.requestRender();
}

void NavigationView::resizeEvent(QResizeEvent *event) { m_renderQueue.requestRender(); }



void NavigationView::onImageReady(int reqId, QImage image, double x, double y,
                                  double scale, bool success) {
  
  // qDebug() << "NavigationView::onImageReady reqId:" << reqId << " success:" << success << " size:" << image.size();
  
  // This is always the current render completing (only one active at a time)
  if (m_currentProgress) {
    emit progressFinished(m_currentProgress);
    m_currentProgress.reset();
  }

  // Only update preview image if render was successful
  if (success) {
    m_previewImage = image;
    update();
  }

  m_renderQueue.reportFinished(reqId, success);
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
 
  // Determine dimensions to draw
  int imgW = 0;
  int imgH = 0;
  
  if (!m_previewImage.isNull()) {
    imgW = m_previewImage.width();
    imgH = m_previewImage.height();
  } else if (m_scan && m_scan->width > 0 && m_scan->height > 0) {
     // Fallback: calculate theoretical size
     QRect viewRect = rect();
     if (m_zoomSlider->isVisible())
        viewRect.setBottom(m_zoomSlider->geometry().top());
     
     int w = viewRect.width();
     int h = viewRect.height();
     
     CoordinateTransformer transformer(m_scan.get(), *m_rparams);
     QSize transformedSize = transformer.getTransformedSize();
     int scanW = transformedSize.width();
     int scanH = transformedSize.height();
     
     double scaleX = (double)w / scanW;
     double scaleY = (double)h / scanH;
     double scale = qMin(scaleX, scaleY);
     if (scale <= 0) scale = 0.1;

     if (m_previewScale <= 0.0001) {
         const_cast<NavigationView*>(this)->m_previewScale = scale;
     }

     imgW = (int)(scanW * scale);
     imgH = (int)(scanH * scale);
  } else {
     // No image data at all
     return;
  }

  // Draw centered
  QRect viewRect = rect();
  if (m_zoomSlider->isVisible())
    viewRect.setBottom(m_zoomSlider->geometry().top());

  int x = viewRect.left() + (viewRect.width() - imgW) / 2;
  int y = viewRect.top() + (viewRect.height() - imgH) / 2;

  m_imageRect = QRect(x, y, imgW, imgH);
  
  if (!m_previewImage.isNull()) {
     p.drawImage(x, y, m_previewImage);
  } else {
     p.setPen(QPen(Qt::darkGray));
     p.drawRect(m_imageRect);
     p.drawText(m_imageRect, Qt::AlignCenter, "Rendering...");
  }

  // Draw Viewport Rect
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
    
    if (m_visibleRect.contains(imageX, imageY)) {
        // Grab logic
        double centerX = m_visibleRect.center().x();
        double centerY = m_visibleRect.center().y();
        m_dragOffset = QPointF(imageX - centerX, imageY - centerY);
    } else {
        // Jump logic
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

void NavigationView::wheelEvent(QWheelEvent *event) {
  if (m_previewScale <= 0) return;

  // Calculate zoom factor matching ImageWidget
  double numDegrees = event->angleDelta().y() / 8.0;
  double numSteps = numDegrees / 15.0;
  double factor = qPow(1.1, numSteps);

  // Mouse position in Image Coordinates
  double mouseX = event->position().x() - m_imageRect.left();
  double mouseY = event->position().y() - m_imageRect.top();
  
  double imageX = mouseX / m_previewScale;
  double imageY = mouseY / m_previewScale;

  // Current main view state
  double oldScale = m_mainScale;
  double newScale = oldScale * factor;

  // Calculate new center to keep imageX fixed relative to viewport
  // Math: NewCenter = Mouse + (OldCenter - Mouse) / factor
  double oldCx = m_visibleRect.center().x();
  double oldCy = m_visibleRect.center().y();
  
  double newCx = imageX + (oldCx - imageX) / factor;
  double newCy = imageY + (oldCy - imageY) / factor;
  
  // New dimensions
  double newW = m_visibleRect.width() / factor;
  double newH = m_visibleRect.height() / factor;
  
  double newX = newCx - newW / 2.0;
  double newY = newCy - newH / 2.0;

  // Apply changes
  // Note: We emit zoomChanged first, which will reset the view to be centered on the *current* center.
  // Then we emit panChanged to move it to our desired target.
  emit zoomChanged(newScale);
  emit panChanged(newX, newY);
}
