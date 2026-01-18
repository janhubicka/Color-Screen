#include "ImageWidget.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "Renderer.h"
#include <QDebug>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
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

  // Create both animations for when no image is loaded
  m_thamesAnim = new ThamesAnimation(this);
  m_thamesAnim->setGeometry(rect());
  m_thamesAnim->hide();
  
  m_pagetAnim = new PagetAnimation(this);
  m_pagetAnim->setGeometry(rect());
  m_pagetAnim->hide();
  
  // Randomly choose which animation to use
  m_activeAnim = (QRandomGenerator::global()->bounded(2) == 0) ? static_cast<QWidget*>(m_thamesAnim) 
                                     : static_cast<QWidget*>(m_pagetAnim);

  setMouseTracking(false); // Only track when dragging
  m_showRegistrationPoints = false;
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
                           colorscreen::render_type_parameters *renderType,
                           colorscreen::solver_parameters *solver) {
  // Store new parameters
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;
  m_renderType = renderType;
  m_solver = solver;

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
    colorscreen::render_type_parameters *renderType,
    colorscreen::solver_parameters *solver) {
  // Update the parameter pointers
  m_rparams = rparams;
  m_scrToImg = scrToImg;
  m_scrDetect = scrDetect;
  m_renderType = renderType;
  m_solver = solver;

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

    if (m_showRegistrationPoints && m_solver && m_scan && m_scrToImg) {
      colorscreen::scr_to_img map;
      map.set_parameters(*m_scrToImg, *m_scan);

      p.setRenderHint(QPainter::Antialiasing);

      colorscreen::point_t c1 = m_scrToImg->coordinate1;
      double dot_period = sqrt(c1.x * c1.x + c1.y * c1.y);
      if (dot_period < 0.1) dot_period = 10.0;
      double threshold = dot_period / 4.0;

      for (size_t i = 0; i < m_solver->points.size(); ++i) {
        const auto &pt = m_solver->points[i];
        colorscreen::point_t xi = pt.img;
        colorscreen::point_t scr_p = pt.scr;

        if (colorscreen::screen_with_vertical_strips_p(m_scrToImg->type)) {
          colorscreen::point_t scr2 = map.to_scr(xi);
          scr_p.y = scr2.y;
        }
        colorscreen::point_t p_sim = map.to_img(scr_p);

        // Calculate displacement magnitude in image space
        double dx_img = p_sim.x - xi.x;
        double dy_img = p_sim.y - xi.y;
        double dist_img = sqrt(dx_img * dx_img + dy_img * dy_img);

        // Heat map color calculation
        // error_ratio = 1.0 at threshold (dot_period / 4)
        double error_ratio = dist_img / threshold;
        
        // Map 0.0 -> Green (120), 1.0 -> Yellow (60), 2.0+ -> Red (0)
        double hue = 120.0 - std::min(error_ratio, 2.0) * 60.0;
        if (hue < 0) hue = 0;
        QColor color = QColor::fromHslF(hue / 360.0, 1.0, 0.5);

        // Widget coordinates using the new API
        QPointF start = imageToWidget(xi);
        QPointF simulated = imageToWidget(p_sim);

        // Highlight selected points
        bool isSelected = m_selectedPoints.count(SelectedPoint{i, SelectedPoint::RegistrationPoint});
        if (isSelected) {
            p.setPen(QPen(Qt::cyan, 6, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(start, 8, 8);
        }

        // Draw intended location with black outline
        p.setPen(QPen(Qt::black, 4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(start, 4, 4);
        p.setPen(QPen(color, 2));
        p.drawEllipse(start, 4, 4);

        // Displacement arrow amplification
        double amp_scale = 200.0 / (dot_period / 2.0);
        
        // Calculate displacement in image space, then map to widget space for consistent scaling
        colorscreen::point_t xi_displaced = {
            xi.x + (p_sim.x - xi.x) * amp_scale,
            xi.y + (p_sim.y - xi.y) * amp_scale
        };
        QPointF end = imageToWidget(xi_displaced);

        double dx_w = end.x() - start.x();
        double dy_w = end.y() - start.y();

        if (dx_w * dx_w + dy_w * dy_w > 1.0) {
          double angle = std::atan2(dy_w, dx_w);
          double headLen = 8.0;
          QPointF h1(end.x() - headLen * std::cos(angle - M_PI / 6),
                     end.y() - headLen * std::sin(angle - M_PI / 6));
          QPointF h2(end.x() - headLen * std::cos(angle + M_PI / 6),
                     end.y() - headLen * std::sin(angle + M_PI / 6));
          QVector<QPointF> head{h1, end, h2};

          // Draw displacement line with black outline
          p.setPen(QPen(Qt::black, 5));
          p.drawLine(start, end);
          p.drawPolyline(head);

          p.setPen(QPen(color, 3));
          p.drawLine(start, end);
          p.drawPolyline(head);
        }
        
        // Draw simulated location dot with black outline
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(simulated, 3, 3);
        p.setBrush(color);
        p.drawEllipse(simulated, 2, 2);
      }
    }

    // Hide animations when we have an image
    if (m_thamesAnim && !m_thamesAnim->isHidden()) {
      m_thamesAnim->stopAnimation();
      m_thamesAnim->hide();
    }
    if (m_pagetAnim && !m_pagetAnim->isHidden()) {
      m_pagetAnim->stopAnimation();
      m_pagetAnim->hide();
    }
  } else {
    // Show active animation when no image
    if (m_activeAnim && m_activeAnim->isHidden()) {
      m_activeAnim->setGeometry(rect());
      m_activeAnim->show();
      
      // Start the appropriate animation
      if (m_activeAnim == m_thamesAnim) {
        m_thamesAnim->startAnimation();
      } else if (m_activeAnim == m_pagetAnim) {
        m_pagetAnim->startAnimation();
      }
    }
    
    // Update geometry if needed
    if (m_activeAnim) {
      m_activeAnim->setGeometry(rect());
    }
  }
}

void ImageWidget::resizeEvent(QResizeEvent *event) {
  requestRender();
  emit viewStateChanged(
      QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}

void ImageWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_interactionMode == PanMode) {
      m_isDragging = true;
      m_lastMousePos = event->pos();
    } else if (m_interactionMode == SelectMode) {
      bool ctrl = event->modifiers() & Qt::ControlModifier;
      
      // Hit-test points
      int hitIndex = -1;
      if (m_showRegistrationPoints && m_solver) {
        for (size_t i = 0; i < m_solver->points.size(); ++i) {
          QPointF pos = imageToWidget(m_solver->points[i].img);
          if (QLineF(pos, event->position()).length() < 10) {
            hitIndex = i;
            break;
          }
        }
      }

      if (hitIndex != -1) {
        SelectedPoint sp = {(size_t)hitIndex, SelectedPoint::RegistrationPoint};
        if (ctrl) {
          if (m_selectedPoints.count(sp)) m_selectedPoints.erase(sp);
          else m_selectedPoints.insert(sp);
        } else {
          m_selectedPoints.clear();
          m_selectedPoints.insert(sp);
        }
        emit selectionChanged();
        update();
      } else {
        // Start rubber band
        if (!ctrl) clearSelection();
        m_rubberBandOrigin = event->pos();
        if (!m_rubberBand) {
          m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
        m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
        m_rubberBand->show();
      }
    }
  }
}

void ImageWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_interactionMode == PanMode && m_isDragging) {
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    // Move view opposite to drag
    m_viewX -= delta.x() / m_scale;
    m_viewY -= delta.y() / m_scale;

    requestRender();
    emit viewStateChanged(
        QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale),
        m_scale);
  } else if (m_interactionMode == SelectMode && m_rubberBand && m_rubberBand->isVisible()) {
    m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
  }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_interactionMode == PanMode) {
      m_isDragging = false;
    } else if (m_interactionMode == SelectMode && m_rubberBand && m_rubberBand->isVisible()) {
      QRect rect = m_rubberBand->geometry();
      m_rubberBand->hide();
      
      bool ctrl = event->modifiers() & Qt::ControlModifier;
      bool changed = false;

      if (m_showRegistrationPoints && m_solver) {
        for (size_t i = 0; i < m_solver->points.size(); ++i) {
          QPointF pos = imageToWidget(m_solver->points[i].img);
          if (rect.contains(pos.toPoint())) {
            SelectedPoint sp = {i, SelectedPoint::RegistrationPoint};
            if (!m_selectedPoints.count(sp)) {
              m_selectedPoints.insert(sp);
              changed = true;
            }
          }
        }
      }

      if (changed) {
        emit selectionChanged();
        update();
      }
    }
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

QPointF ImageWidget::imageToWidget(colorscreen::point_t p) const {
  if (!m_scan || !m_scrToImg)
    return QPointF(p.x, p.y);

  double rot = m_scrToImg->final_rotation;
  int angle = (int)rot % 360;
  if (angle < 0) angle += 360;

  double xr = p.x;
  double yr = p.y;
  double w = m_scan->width;
  double h = m_scan->height;

  if (angle == 90) {
    xr = p.y;
    yr = w - p.x;
  } else if (angle == 180) {
    xr = w - p.x;
    yr = h - p.y;
  } else if (angle == 270) {
    xr = h - p.y;
    yr = p.x;
  }

  return QPointF((xr - m_viewX) * m_scale, (yr - m_viewY) * m_scale);
}

colorscreen::point_t ImageWidget::widgetToImage(QPointF p) const {
  if (!m_scan || !m_scrToImg)
    return {p.x(), p.y()};

  double xr = p.x() / m_scale + m_viewX;
  double yr = p.y() / m_scale + m_viewY;

  double rot = m_scrToImg->final_rotation;
  int angle = (int)rot % 360;
  if (angle < 0) angle += 360;

  double w = m_scan->width;
  double h = m_scan->height;

  if (angle == 90) {
    return {w - yr, xr};
  } else if (angle == 180) {
    return {w - xr, h - yr};
  } else if (angle == 270) {
    return {yr, h - xr};
  }

  return {xr, yr};
}

void ImageWidget::fitToView() {
  if (!m_scan || m_scan->width <= 0)
    return;

  double w = width();
  double h = height();

  // Handle rotation for scale calculation
  double rot = m_scrToImg ? m_scrToImg->final_rotation : 0.0;
  int angle = (int)rot % 360;
  if (angle < 0) angle += 360;

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

void ImageWidget::setShowRegistrationPoints(bool show) {
  m_showRegistrationPoints = show;
  if (!show) {
    clearSelection();
  }
  update();
  emit registrationPointsVisibilityChanged(show);
}

void ImageWidget::setInteractionMode(InteractionMode mode) {
  if (m_interactionMode == mode) return;
  m_interactionMode = mode;
  if (m_rubberBand) m_rubberBand->hide();
  update();
}

void ImageWidget::clearSelection() {
  if (!m_selectedPoints.empty()) {
    m_selectedPoints.clear();
    emit selectionChanged();
    update();
  }
}
