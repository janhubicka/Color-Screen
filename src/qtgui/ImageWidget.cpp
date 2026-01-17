#include "ImageWidget.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "Renderer.h"
#include <QDebug>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

// Ensure shared ptr can be passed via signals
Q_DECLARE_METATYPE(std::shared_ptr<colorscreen::progress_info>)
Q_DECLARE_METATYPE(colorscreen::render_parameters)

ImageWidget::ImageWidget(QWidget *parent) : QWidget(parent) {
  qRegisterMetaType<std::shared_ptr<colorscreen::progress_info>>();
  qRegisterMetaType<colorscreen::render_parameters>();

  // Background color
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::black);
  setAutoFillBackground(true);
  setPalette(pal);

  setMouseTracking(false); // Only track when dragging
}

double ImageWidget::getMinScale() const { return m_minScale; }

void ImageWidget::setZoom(double scale) {
  if (qAbs(scale - m_scale) > 0.000001) {
    // Keep center? For now just zoom.
    // Ideally zoom around center of view.
    double centerX = m_viewX + (width() / m_scale) / 2.0;
    double centerY = m_viewY + (height() / m_scale) / 2.0;

    m_scale = scale;

    // New view top-left
    m_viewX = centerX - (width() / m_scale) / 2.0;
    m_viewY = centerY - (height() / m_scale) / 2.0;

    requestRender();
    emit viewStateChanged(
        QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale),
        m_scale);
  }
}

void ImageWidget::setPan(double x, double y) {
  if (qAbs(x - m_viewX) > 0.1 || qAbs(y - m_viewY) > 0.1) {
    m_viewX = x;
    m_viewY = y;
    requestRender();
    emit viewStateChanged(
        QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale),
        m_scale);
  }
}

ImageWidget::~ImageWidget() {
  if (m_renderThread) {
    m_renderThread->requestInterruption();
    m_renderThread->quit();
    m_renderThread->wait();
  }
}

void ImageWidget::setImage(std::shared_ptr<colorscreen::image_data> scan,
                           colorscreen::render_parameters *rparams,
                           colorscreen::scr_to_img_parameters *scrToImg,
                           colorscreen::scr_detect_parameters *scrDetect,
                           colorscreen::render_type_parameters *renderType) {
  // Store new parameters
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;
  m_renderType = renderType;

  if (m_scan != scan) {
    m_pixmap = QImage(); // Clear if new image loaded
  }

  // Cancel current progress if exists
  if (m_currentProgress) {
    m_currentProgress->cancel();
  }

  // Clear old scan to release it from memory
  m_scan = nullptr;

  // Clean up old renderer and thread
  if (m_renderer) {
    disconnect(m_renderer, nullptr, this, nullptr);
    m_renderer->deleteLater();
    m_renderer = nullptr;
  }

  if (m_renderThread) {
    disconnect(m_renderThread, nullptr, nullptr, nullptr);
    if (m_renderThread->isRunning()) {
      m_renderThread->requestInterruption();
      m_renderThread->quit();
      m_renderThread->wait();
    }
    delete m_renderThread;
    m_renderThread = nullptr;
  }

  // Manually emit progressFinished for current progress
  if (m_currentProgress) {
    emit progressFinished(m_currentProgress);
    m_currentProgress.reset();
  }

  m_renderInProgress = false;
  m_hasPendingRender = false;

  m_scan = scan;

  fitToView();

  if (m_scan && m_rparams) {
    m_renderThread = new QThread(this);
    static colorscreen::scr_to_img_parameters defaultScrToImg;
    static colorscreen::scr_detect_parameters defaultScrDetect;
    static colorscreen::render_type_parameters defaultRenderType;

    m_renderer = new Renderer(m_scan, *m_rparams,
                              m_scrToImg ? *m_scrToImg : defaultScrToImg,
                              m_scrDetect ? *m_scrDetect : defaultScrDetect,
                              m_renderType ? *m_renderType : defaultRenderType);
    m_renderer->moveToThread(m_renderThread);

    connect(m_renderThread, &QThread::finished, m_renderer,
            &QObject::deleteLater);
    connect(m_renderer, &Renderer::imageReady, this,
            &ImageWidget::handleImageReady);

    m_renderThread->start();

    requestRender();
  }
  update();
}

void ImageWidget::updateParameters(
    colorscreen::render_parameters *rparams,
    colorscreen::scr_to_img_parameters *scrToImg,
    colorscreen::scr_detect_parameters *scrDetect,
    colorscreen::render_type_parameters *renderType) {
  // Update the parameter pointers
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;
  m_renderType = renderType;

  // Update renderer's cached parameters if it exists
  if (m_renderer) {
    QMetaObject::invokeMethod(
        m_renderer, "updateParameters", Qt::QueuedConnection,
        Q_ARG(colorscreen::render_parameters,
              m_rparams ? *m_rparams : colorscreen::render_parameters()),
        Q_ARG(colorscreen::scr_to_img_parameters,
              m_scrToImg ? *m_scrToImg : colorscreen::scr_to_img_parameters()),
        Q_ARG(colorscreen::scr_detect_parameters,
              m_scrDetect ? *m_scrDetect
                          : colorscreen::scr_detect_parameters()),
        Q_ARG(colorscreen::render_type_parameters,
              m_renderType ? *m_renderType
                           : colorscreen::render_type_parameters()));
  }

  // Request Re-render
  requestRender();
}

void ImageWidget::requestRender() {
  if (!m_renderer || !m_scan)
    return;

  // If render is in progress, cancel it and queue this request as pending
  if (m_renderInProgress) {
    if (m_currentProgress) {
      m_currentProgress->cancel();
    }
    // Store pending render parameters (only latest)
    m_hasPendingRender = true;
    m_pendingViewX = m_viewX;
    m_pendingViewY = m_viewY;
    m_pendingScale = m_scale;
    return; // Don't start render yet, wait for current to finish
  }

  // No render in progress, start new one
  m_renderInProgress = true;
  m_hasPendingRender = false;

  double xOff = m_viewX * m_scale;
  double yOff = m_viewY * m_scale;

  m_currentProgress = std::make_shared<colorscreen::progress_info>();
  m_currentProgress->set_task("Rendering", 0);
  emit progressStarted(m_currentProgress);

  // Invoke render on worker thread
  QMetaObject::invokeMethod(
      m_renderer, "render", Qt::QueuedConnection,
      Q_ARG(int, 0), // reqId not needed anymore
      Q_ARG(double, xOff), Q_ARG(double, yOff), Q_ARG(double, m_scale),
      Q_ARG(int, width()), Q_ARG(int, height()),
      Q_ARG(colorscreen::render_parameters, *m_rparams),
      Q_ARG(std::shared_ptr<colorscreen::progress_info>, m_currentProgress));
}

void ImageWidget::handleImageReady(int reqId, QImage image, double x, double y,
                                   double scale, bool success) {
  // This is always the current render completing (only one active at a time)
  bool wasCancelled = false;
  if (m_currentProgress) {
    wasCancelled = m_currentProgress->cancelled();
    emit progressFinished(m_currentProgress);
    m_currentProgress.reset();
  }

  // Mark render as no longer in progress
  m_renderInProgress = false;

  if (wasCancelled) {
    // Do nothing with image if cancelled
  } else if (!success) {
    // Show error message to user
    QMessageBox::warning(
        this, "Rendering Error",
        "Failed to render image. The rendering process encountered an error.");
  } else {
    // Success - update the displayed image
    m_pixmap = image;
    update();
  }

  // Start pending render if one is queued
  if (m_hasPendingRender) {
    m_hasPendingRender = false;
    requestRender();
  }
}

void ImageWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  if (!m_pixmap.isNull()) {
    p.drawImage(0, 0, m_pixmap);
  } else {
    p.drawText(rect(), Qt::AlignCenter, "No Image");
  }
}

void ImageWidget::resizeEvent(QResizeEvent *event) {
  requestRender();
  emit viewStateChanged(
      QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}

void ImageWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_lastMousePos = event->pos();
  }
}

void ImageWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_isDragging) {
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    // Move view opposite to drag
    m_viewX -= delta.x() / m_scale;
    m_viewY -= delta.y() / m_scale;

    requestRender();
    emit viewStateChanged(
        QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale),
        m_scale);
  }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
  }
}

void ImageWidget::wheelEvent(QWheelEvent *event) {
  double numDegrees = event->angleDelta().y() / 8.0;
  double numSteps = numDegrees / 15.0;
  double factor = qPow(1.1, numSteps);

  double mouseX = event->position().x();
  double mouseY = event->position().y();

  double mouseImageX = m_viewX + mouseX / m_scale;
  double mouseImageY = m_viewY + mouseY / m_scale;

  m_scale *= factor;

  // Clamp scale? (Optional)

  // Adjust view so mouseImageX remains under mouseX
  // new_viewX + mouseX / new_scale = mouseImageX
  m_viewX = mouseImageX - mouseX / m_scale;
  m_viewY = mouseImageY - mouseY / m_scale;

  requestRender();
  emit viewStateChanged(
      QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}

void ImageWidget::fitToView() {
  if (!m_scan || m_scan->width <= 0)
    return;

  double w = width();
  double h = height();

  // Handle rotation for scale calculation
  double rot = m_scrToImg ? m_scrToImg->final_rotation : 0.0;
  int angle = (int)rot;
  angle = angle % 360;
  if (angle < 0)
    angle += 360;

  double imgW = m_scan->width;
  double imgH = m_scan->height;

  if (angle == 90 || angle == 270) {
    std::swap(imgW, imgH);
  }

  if (w > 0 && h > 0 && imgW > 0 && imgH > 0) {
    double scaleX = w / imgW;
    double scaleY = h / imgH;
    m_scale = qMin(scaleX, scaleY);
    if (m_scale == 0)
      m_scale = 1.0;
    m_minScale = m_scale;
  } else {
    m_scale = 0.1; // Fallback
    m_minScale = 0.1;
  }

  // Center view
  m_viewX = (imgW - w / m_scale) / 2.0;
  m_viewY = (imgH - h / m_scale) / 2.0;

  requestRender();
  emit viewStateChanged(
      QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}
