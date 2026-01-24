#include "ImageWidget.h"
#include "ColorUtils.h"
#include "CoordinateTransformer.h"
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
#include <QTimer>
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
  
  m_jolyAnim = new JolyAnimation(this);
  m_jolyAnim->setGeometry(rect());
  m_jolyAnim->hide();
  
  // Randomly choose which animation to use
  int animChoice = QRandomGenerator::global()->bounded(3);
  if (animChoice == 0) m_activeAnim = m_thamesAnim;
  else if (animChoice == 1) m_activeAnim = m_pagetAnim;
  else m_activeAnim = m_jolyAnim;

  setMouseTracking(false); // Only track when dragging
  setContextMenuPolicy(Qt::NoContextMenu); // Allow right-click for our custom handling
  setFocusPolicy(Qt::StrongFocus); // Ensure widget can receive all mouse events
  m_showRegistrationPoints = false;

  connect(&m_renderQueue, &TaskQueue::triggerRender, this, &ImageWidget::onTriggerRender);
  connect(&m_renderQueue, &TaskQueue::progressStarted, this, &ImageWidget::progressStarted);
  connect(&m_renderQueue, &TaskQueue::progressFinished, this, &ImageWidget::progressFinished);
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
  m_renderQueue.cancelAll();

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

  // m_renderQueue.cancelAll() handled above.




  m_scan = scan;
  m_simulatedPointsDirty = true;

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

  // Selective invalidation: only dirty if scrToImg parameters changed
  if (scrToImg && *scrToImg != m_lastScrToImg) {
    m_simulatedPointsDirty = true;
    m_lastScrToImg = *scrToImg;
  }

  // Request Re-render
  requestRender();
}





void ImageWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  if (!m_pixmap.isNull()) {
    // Calculate offset and scale difference between the rendered image and current view
    double scaleFactor = m_scale / m_lastRenderedScale;
    
    double xPos = (m_lastRenderedX - m_viewX) * m_scale;
    double yPos = (m_lastRenderedY - m_viewY) * m_scale;
    
    // If scales differ significantly (during zoom), we stretch the image to fit 
    // the current view until a new high-res render arrives.
    // This provides smooth "continuous" zooming.
    
    double targetWidth = m_pixmap.width() * scaleFactor;
    double targetHeight = m_pixmap.height() * scaleFactor;
    
    QRectF targetRect(xPos, yPos, targetWidth, targetHeight);
    
    p.drawImage(targetRect, m_pixmap, QRectF(0, 0, m_pixmap.width(), m_pixmap.height()));

    if (m_showRegistrationPoints && m_solver && m_scan && m_scrToImg) {
      p.setRenderHint(QPainter::Antialiasing);

      if (m_simulatedPointsDirty || m_simulatedPoints.size() != m_solver->points.size()) {
        updateSimulatedPoints();
      }

      colorscreen::point_t c1 = m_scrToImg->coordinate1;
      double dot_period = sqrt(c1.x * c1.x + c1.y * c1.y);
      if (dot_period < 0.1) dot_period = 10.0;
      double threshold = dot_period / 4.0;
      double amp_scale = 200.0 / (dot_period / 2.0);

      // Viewport culling: expand rect to include displacement arrows
      // Displacement arrow amplification is 200.0 / (dot_period / 2.0).
      // Max pixel length is 100.
      double margin = 100.0 / m_scale + 20.0;
      QRectF visibleRect(m_viewX - margin, m_viewY - margin, 
                         width() / m_scale + 2 * margin, height() / m_scale + 2 * margin);


      for (size_t i = 0; i < m_solver->points.size(); ++i) {
        const auto &xi = m_solver->points[i].img;

        QPointF p_widget = imageToWidget(xi);
        
        double marginPixels = 100.0 + 20.0 * m_scale;
        
        if (p_widget.x() < -marginPixels || p_widget.x() > width() + marginPixels ||
            p_widget.y() < -marginPixels || p_widget.y() > height() + marginPixels) {
            continue;
        }

        if (p_widget.x() < -marginPixels || p_widget.x() > width() + marginPixels ||
            p_widget.y() < -marginPixels || p_widget.y() > height() + marginPixels) {
            continue;
        }

        const auto &p_sim = m_simulatedPoints[i];

        // Calculate displacement magnitude in image space
        double dx_img = p_sim.x - xi.x;
        double dy_img = p_sim.y - xi.y;
        double dist_img = sqrt(dx_img * dx_img + dy_img * dy_img);

        // Heat map color calculation
        QColor color = getHeatMapColor(dist_img, threshold);

        // Widget coordinates (start/simulated)
        QPointF start = imageToWidget(xi);
        QPointF simulated = imageToWidget(p_sim);

        // Highlight selected points
        bool isSelected = m_selectedPoints.count(SelectedPoint{i, SelectedPoint::RegistrationPoint});
        if (isSelected) {
            p.setPen(QPen(Qt::cyan, 6, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(start, 8, 8);
        }

        // Draw intended location
        p.setPen(QPen(Qt::black, 4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(start, 4, 4);
        p.setPen(QPen(color, 2));
        p.drawEllipse(start, 4, 4);

        // Displacement arrow math
        colorscreen::point_t xi_displaced = {
            xi.x + dx_img * amp_scale,
            xi.y + dy_img * amp_scale
        };
        QPointF end = imageToWidget(xi_displaced);

        double dx_w = end.x() - start.x();
        double dy_w = end.y() - start.y();
        double dist_w2 = dx_w * dx_w + dy_w * dy_w;

        if (dist_w2 > 1.0) {
          // Cap arrow length to 100 pixels
          if (dist_w2 > 10000.0) {
            double dist_w = sqrt(dist_w2);
            dx_w *= 100.0 / dist_w;
            dy_w *= 100.0 / dist_w;
            end = QPointF(start.x() + dx_w, start.y() + dy_w);
          }

          double arrowAngle = std::atan2(dy_w, dx_w);
          double headLen = 8.0;
          double cosA = std::cos(arrowAngle - M_PI / 6);
          double sinA = std::sin(arrowAngle - M_PI / 6);
          double cosB = std::cos(arrowAngle + M_PI / 6);
          double sinB = std::sin(arrowAngle + M_PI / 6);
          
          QPointF h1(end.x() - headLen * cosA, end.y() - headLen * sinA);
          QPointF h2(end.x() - headLen * cosB, end.y() - headLen * sinB);
          QPointF head[] = {h1, end, h2};

          // Draw displacement line
          p.setPen(QPen(Qt::black, 5));
          p.drawLine(start, end);
          p.drawPolyline(head, 3);

          p.setPen(QPen(color, 3));
          p.drawLine(start, end);
          p.drawPolyline(head, 3);
        }
        
        // Draw simulated location dot
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(simulated, 3, 3);
        p.setBrush(color);
        p.drawEllipse(simulated, 2, 2);
      }
    }

    // Draw screen coordinate system when SetCenterMode is active
    if (m_interactionMode == SetCenterMode && m_scrToImg) {
      // Get color from first point location
      int npoints = 0;
      colorscreen::solver_parameters::point_location *points = 
        colorscreen::solver_parameters::get_point_locations(m_scrToImg->type, &npoints);
      
      QColor dotColor = Qt::blue; // Default
      if (points && npoints > 0) {
        switch (points[0].color) {
          case colorscreen::solver_parameters::red:
            dotColor = Qt::red;
            break;
          case colorscreen::solver_parameters::green:
            dotColor = Qt::green;
            break;
          case colorscreen::solver_parameters::blue:
            dotColor = Qt::blue;
            break;
          default:
            dotColor = Qt::blue;
            break;
        }
      }
      
      QPointF centerWidget = imageToWidget(m_scrToImg->center);
      
      // Calculate viewport bounds in image coordinates
      colorscreen::point_t topLeft = widgetToImage(QPointF(0, 0));
      colorscreen::point_t bottomRight = widgetToImage(QPointF(width(), height()));
      double minX = qMin(topLeft.x, bottomRight.x);
      double maxX = qMax(topLeft.x, bottomRight.x);
      double minY = qMin(topLeft.y, bottomRight.y);
      double maxY = qMax(topLeft.y, bottomRight.y);
      
      // Draw X axis (coordinate1) - extend to viewport edges
      colorscreen::point_t xDir = {m_scrToImg->coordinate1.x, m_scrToImg->coordinate1.y};
      double xLen = sqrt(xDir.x * xDir.x + xDir.y * xDir.y);
      if (xLen > 0) {
        xDir.x /= xLen;
        xDir.y /= xLen;
        
        // Find intersection with viewport
        double tMax = qMax(qMax((maxX - m_scrToImg->center.x) / xDir.x,
                                 (minX - m_scrToImg->center.x) / xDir.x),
                           qMax((maxY - m_scrToImg->center.y) / xDir.y,
                                 (minY - m_scrToImg->center.y) / xDir.y));
        double tMin = qMin(qMin((maxX - m_scrToImg->center.x) / xDir.x,
                                 (minX - m_scrToImg->center.x) / xDir.x),
                           qMin((maxY - m_scrToImg->center.y) / xDir.y,
                                 (minY - m_scrToImg->center.y) / xDir.y));
        
        colorscreen::point_t xStart = {m_scrToImg->center.x + xDir.x * tMin,
                                        m_scrToImg->center.y + xDir.y * tMin};
        colorscreen::point_t xEnd = {m_scrToImg->center.x + xDir.x * tMax,
                                      m_scrToImg->center.y + xDir.y * tMax};
        
        QPointF xStartWidget = imageToWidget(xStart);
        QPointF xEndWidget = imageToWidget(xEnd);
        
        // Calculate dash length based on coordinate1 vector length (not whole axis)
        colorscreen::point_t coord1End = {
          m_scrToImg->center.x + m_scrToImg->coordinate1.x,
          m_scrToImg->center.y + m_scrToImg->coordinate1.y
        };
        QPointF centerWidget = imageToWidget(m_scrToImg->center);
        QPointF coord1Widget = imageToWidget(coord1End);
        double coord1LengthWidget = QLineF(centerWidget, coord1Widget).length();
        double dashLengthPixels = qMax(9.0, coord1LengthWidget / 3.0);
        double penWidth = 3.0;
        double dashLength = dashLengthPixels / penWidth;  // Dash pattern is in pen-width units
        
        // Calculate offset using coordinate1 (X-axis vector)
        // Correct phase math: anchor pattern to center point
        // Pattern at distance 'd' should be 0 (start of pattern).
        // Qt uses (x/pen + offset). So d/pen + offset = 0 (mod period).
        // offset = period - (d/pen % period).
        double distToCenter = QLineF(xStartWidget, centerWidget).length();
        double distInPenWidths = distToCenter / penWidth;
        double period = dashLength * 2.0;
        double centerOffset = period - fmod(distInPenWidths, period);
        
        // Draw axis with alternating black/white dashed pattern
        QPen dashedPen(Qt::white, penWidth);
        dashedPen.setStyle(Qt::CustomDashLine);
        dashedPen.setDashPattern({dashLength, dashLength});
        dashedPen.setDashOffset(centerOffset);
        p.setPen(dashedPen);
        p.drawLine(xStartWidget, xEndWidget);
        
        dashedPen.setColor(Qt::black);
        dashedPen.setDashOffset(centerOffset - dashLength);  // Offset for alternating
        p.setPen(dashedPen);
        p.drawLine(xStartWidget, xEndWidget);
      }
      
      // Draw Y axis (coordinate2) - extend to viewport edges
      colorscreen::point_t yDir = {m_scrToImg->coordinate2.x, m_scrToImg->coordinate2.y};
      double yLen = sqrt(yDir.x * yDir.x + yDir.y * yDir.y);
      if (yLen > 0) {
        yDir.x /= yLen;
        yDir.y /= yLen;
        
        // Find intersection with viewport
        double tMax = qMax(qMax((maxX - m_scrToImg->center.x) / yDir.x,
                                 (minX - m_scrToImg->center.x) / yDir.x),
                           qMax((maxY - m_scrToImg->center.y) / yDir.y,
                                 (minY - m_scrToImg->center.y) / yDir.y));
        double tMin = qMin(qMin((maxX - m_scrToImg->center.x) / yDir.x,
                                 (minX - m_scrToImg->center.x) / yDir.x),
                           qMin((maxY - m_scrToImg->center.y) / yDir.y,
                                 (minY - m_scrToImg->center.y) / yDir.y));
        
        colorscreen::point_t yStart = {m_scrToImg->center.x + yDir.x * tMin,
                                        m_scrToImg->center.y + yDir.y * tMin};
        colorscreen::point_t yEnd = {m_scrToImg->center.x + yDir.x * tMax,
                                      m_scrToImg->center.y + yDir.y * tMax};
        
        QPointF yStartWidget = imageToWidget(yStart);
        QPointF yEndWidget = imageToWidget(yEnd);
        
        // Calculate dash length based on coordinate2 vector length (not whole axis)
        colorscreen::point_t coord2End = {
          m_scrToImg->center.x + m_scrToImg->coordinate2.x,
          m_scrToImg->center.y + m_scrToImg->coordinate2.y
        };
        QPointF centerWidget = imageToWidget(m_scrToImg->center);
        QPointF coord2Widget = imageToWidget(coord2End);
        double coord2LengthWidget = QLineF(centerWidget, coord2Widget).length();
        double dashLengthPixels = qMax(9.0, coord2LengthWidget / 3.0);
        double penWidth = 3.0;
        double dashLength = dashLengthPixels / penWidth;  // Dash pattern is in pen-width units
        
        // Calculate offset using coordinate2 (Y-axis vector)
        // Correct phase math: anchor pattern to center point
        centerWidget = imageToWidget(m_scrToImg->center);
        double distToCenter = QLineF(yStartWidget, centerWidget).length();
        double distInPenWidths = distToCenter / penWidth;
        double period = dashLength * 2.0;
        double centerOffset = period - fmod(distInPenWidths, period);
        
        // Draw axis with alternating black/white dashed pattern
        QPen dashedPen(Qt::white, penWidth);
        dashedPen.setStyle(Qt::CustomDashLine);
        dashedPen.setDashPattern({dashLength, dashLength});
        dashedPen.setDashOffset(centerOffset);
        p.setPen(dashedPen);
        p.drawLine(yStartWidget, yEndWidget);
        
        dashedPen.setColor(Qt::black);
        dashedPen.setDashOffset(centerOffset - dashLength);  // Offset for alternating
        p.setPen(dashedPen);
        p.drawLine(yStartWidget, yEndWidget);
      }
      
      // Draw dots at center and axis endpoints (all same color)
      colorscreen::point_t xAxisEnd = {
        m_scrToImg->center.x + m_scrToImg->coordinate1.x,
        m_scrToImg->center.y + m_scrToImg->coordinate1.y
      };
      colorscreen::point_t yAxisEnd = {
        m_scrToImg->center.x + m_scrToImg->coordinate2.x,
        m_scrToImg->center.y + m_scrToImg->coordinate2.y
      };
      
      QPointF xWidget = imageToWidget(xAxisEnd);
      QPointF yWidget = imageToWidget(yAxisEnd);
      
      // Draw center point
      p.setPen(QPen(Qt::black, 2));
      p.setBrush(dotColor);
      p.drawEllipse(centerWidget, 6, 6);
      
      // Draw X axis endpoint
      p.drawEllipse(xWidget, 6, 6);
      
      // Draw Y axis endpoint  
      p.drawEllipse(yWidget, 6, 6);

      // Draw labels with outline and orthogonal placement
      auto drawLabel = [&](QPointF endPos, QPointF center, QString text) {
          QPointF dir = endPos - center;
          double len = sqrt(dir.x()*dir.x() + dir.y()*dir.y());
          if (len > 0) {
              // Rotate 90 degrees (orthogonal)
              // (x, y) -> (-y, x)
              QPointF perp(-dir.y() / len, dir.x() / len);
              
              // Offset by 25 pixels orthogonal, plus a bit forward along axis to clear dot
              QPointF labelPos = endPos + perp * 25.0 + (dir / len) * 10.0;
              
              QRectF r(labelPos.x() - 15, labelPos.y() - 15, 30, 30);
              
              // Draw black outline
              p.setPen(Qt::black);
              for (int dx = -1; dx <= 1; ++dx) {
                  for (int dy = -1; dy <= 1; ++dy) {
                      if (dx != 0 || dy != 0)
                          p.drawText(r.translated(dx, dy), Qt::AlignCenter, text);
                  }
              }
              // Draw yellow fill
              p.setPen(Qt::yellow);
              p.drawText(r, Qt::AlignCenter, text);
          }
      };

      QFont f = p.font();
      f.setBold(true);
      f.setPointSize(14);
      p.setFont(f);

      drawLabel(xWidget, centerWidget, "X");
      drawLabel(yWidget, centerWidget, "Y");
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
    if (m_jolyAnim && !m_jolyAnim->isHidden()) {
      m_jolyAnim->stopAnimation();
      m_jolyAnim->hide();
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
      } else if (m_activeAnim == m_jolyAnim) {
        m_jolyAnim->startAnimation();
      }
    }
  }
    
  // Update geometry if needed
  if (m_activeAnim) {
    m_activeAnim->setGeometry(rect());
  }
}

void ImageWidget::resizeEvent(QResizeEvent *event) {
  QSize oldSize = event->oldSize();
  QSize newSize = event->size();
  
  // Only adjust if we have a valid old size with non-zero dimensions
  if (oldSize.isValid() && oldSize.width() > 0 && oldSize.height() > 0 && 
      oldSize != newSize && m_scale > 0) {
    // Calculate the center point of the current view in image coordinates
    double centerX = m_viewX + (oldSize.width() / m_scale) / 2.0;
    double centerY = m_viewY + (oldSize.height() / m_scale) / 2.0;
    
    // Recalculate view position to keep the center point stable (without changing zoom)
    m_viewX = centerX - (newSize.width() / m_scale) / 2.0;
    m_viewY = centerY - (newSize.height() / m_scale) / 2.0;
  }
  
  requestRender();
  emit viewStateChanged(
      QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale), m_scale);
}

void ImageWidget::mousePressEvent(QMouseEvent *event) {
  if (m_interactionMode == SetCenterMode) {
    bool ctrl = event->modifiers() & Qt::ControlModifier;
    bool alt = event->modifiers() & Qt::AltModifier; 

    // Determine target based on button/modifiers
    if (event->button() == Qt::LeftButton && !ctrl && !alt) {
      // Start dragging center
      m_dragTarget = DragTarget::Center;
      m_dragStartWidget = event->position();
      m_dragStartImg = widgetToImage(event->position());
      if (m_scrToImg) {
        m_pressParams = *m_scrToImg;
      }
    } else if (event->button() == Qt::RightButton || (event->button() == Qt::LeftButton && ctrl)) {
      // Start dragging Axis 1 (X)
      m_dragTarget = DragTarget::Axis1;
      m_dragStartWidget = event->position();
      m_dragStartImg = widgetToImage(event->position());
      if (m_scrToImg) {
        m_pressParams = *m_scrToImg;
      }
      grabMouse();
      event->accept();
    } else if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && alt)) {
      // Start dragging Axis 2 (Y)
      m_dragTarget = DragTarget::Axis2;
      m_dragStartWidget = event->position();
      m_dragStartImg = widgetToImage(event->position());
      if (m_scrToImg) {
        m_pressParams = *m_scrToImg;
      }
      grabMouse();
      event->accept();
    }
    return;
  }

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
        emit pointManipulationStarted();
        m_draggedPointIndex = hitIndex;
        SelectedPoint sp = {(size_t)hitIndex, SelectedPoint::RegistrationPoint};
        if (ctrl) {
          if (m_selectedPoints.count(sp)) m_selectedPoints.erase(sp);
          else m_selectedPoints.insert(sp);
        } else {
          // If already selected, don't clear (to allow dragging multiple if implemented later, 
          // but for now we just drag one).
          if (m_selectedPoints.count(sp) == 0) {
            m_selectedPoints.clear();
            m_selectedPoints.insert(sp);
          }
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
    } else if (m_interactionMode == AddPointMode) {
      // Start rubber band for area selection
      m_rubberBandOrigin = event->pos();
      if (!m_rubberBand) {
        m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
      }
      m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
      m_rubberBand->show();
    }
  }
}

void ImageWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_interactionMode == PanMode && m_isDragging) {
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    // Move view opposite to drag (m_viewX/Y are in transformed space already)
    m_viewX -= delta.x() / m_scale;
    m_viewY -= delta.y() / m_scale;

    requestRender();
    emit viewStateChanged(
        QRectF(m_viewX, m_viewY, width() / m_scale, height() / m_scale),
        m_scale);
  } else if (m_interactionMode == SelectMode) {
    if (m_draggedPointIndex != -1) {
      colorscreen::point_t imgPos = widgetToImage(event->position());
      if (m_solver && (size_t)m_draggedPointIndex < m_solver->points.size()) {
        m_solver->points[m_draggedPointIndex].img = imgPos;
        m_simulatedPointsDirty = true;
        update();
      }
    } else if (m_rubberBand && m_rubberBand->isVisible()) {
      m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
    }
  } else if (m_interactionMode == AddPointMode) {
    if (m_rubberBand && m_rubberBand->isVisible()) {
      m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
    }
  } else if (m_interactionMode == SetCenterMode) {
    if (m_dragTarget == DragTarget::Center && m_scrToImg) {
      // Drag center: translate by offset
      colorscreen::point_t currentImg = widgetToImage(event->position());
      double xOffset = currentImg.x - m_dragStartImg.x;
      double yOffset = currentImg.y - m_dragStartImg.y;
      m_scrToImg->center.x = m_pressParams.center.x + xOffset;
      m_scrToImg->center.y = m_pressParams.center.y + yOffset;
      emit coordinateSystemChanged();
      update();
    } else if ((m_dragTarget == DragTarget::Axis1 || m_dragTarget == DragTarget::Axis2) && m_scrToImg) {
      // Drag axes: rotate and scale
      colorscreen::point_t currentImg = widgetToImage(event->position());
      double x1 = m_dragStartImg.x - m_pressParams.center.x;
      double y1 = m_dragStartImg.y - m_pressParams.center.y;
      double x2 = currentImg.x - m_pressParams.center.x;  // Use press params center, not current
      double y2 = currentImg.y - m_pressParams.center.y;
      
      double startDist = sqrt((x1 * x1) + (y1 * y1));
      double currentDist = sqrt((x2 * x2) + (y2 * y2));
      double scale = (startDist > 1e-9) ? currentDist / startDist : 1.0;
      
      // Enforce minimum length of 2.0
      double len1 = sqrt(m_pressParams.coordinate1.x*m_pressParams.coordinate1.x + 
                         m_pressParams.coordinate1.y*m_pressParams.coordinate1.y);
      double len2 = sqrt(m_pressParams.coordinate2.x*m_pressParams.coordinate2.x + 
                         m_pressParams.coordinate2.y*m_pressParams.coordinate2.y);
      
      if (m_lockRelativeCoordinates) {
          // Both axes scale together, use stricter limit
          double minScale1 = (len1 > 1e-9) ? 2.0 / len1 : 0.0;
          double minScale2 = (len2 > 1e-9) ? 2.0 / len2 : 0.0;
          double minScale = std::max(minScale1, minScale2);
          if (scale < minScale) scale = minScale;
      } else {
          // Independent scaling
          if (m_dragTarget == DragTarget::Axis1) {
              double minScale1 = (len1 > 1e-9) ? 2.0 / len1 : 0.0;
              if (scale < minScale1) scale = minScale1;
          } else {
              double minScale2 = (len2 > 1e-9) ? 2.0 / len2 : 0.0;
              if (scale < minScale2) scale = minScale2;
          }
      }

      double angle = atan2(y2, x2) - atan2(y1, x1);
      
      if (angle != 0.0 || scale != 1.0) {
        double cosAngle = cos(angle);
        double sinAngle = sin(angle);
        
        if (m_lockRelativeCoordinates) {
            // Standard behavior: Rotate/Scale BOTH axes
            m_scrToImg->coordinate1.x = (m_pressParams.coordinate1.x * cosAngle 
                                         - m_pressParams.coordinate1.y * sinAngle) * scale;
            m_scrToImg->coordinate1.y = (m_pressParams.coordinate1.x * sinAngle 
                                         + m_pressParams.coordinate1.y * cosAngle) * scale;
            
            m_scrToImg->coordinate2.x = (m_pressParams.coordinate2.x * cosAngle 
                                         - m_pressParams.coordinate2.y * sinAngle) * scale;
            m_scrToImg->coordinate2.y = (m_pressParams.coordinate2.x * sinAngle 
                                         + m_pressParams.coordinate2.y * cosAngle) * scale;
        } else {
            // Independent behavior: Only affect the dragged axis
            // While we calculate 'angle' and 'scale' based on the drag, we only apply the transformation
            // to the dragged axis. The other axis should strictly remain at its CURRENT state (or initial state?).
            // If we use m_pressParams, we are essentially 'resetting' the non-dragged axis to its initial state 
            // if we were to assign it. But if we DON'T touch it, it remains as is. 
            // Since we might have moved it in a previous step?
            // Wait, m_scrToImg is the source of truth. If DragTarget::Axis1, we modify coordinate1.
            // We should NOT touch coordinate2.
            
            if (m_dragTarget == DragTarget::Axis1) {
                m_scrToImg->coordinate1.x = (m_pressParams.coordinate1.x * cosAngle 
                                             - m_pressParams.coordinate1.y * sinAngle) * scale;
                m_scrToImg->coordinate1.y = (m_pressParams.coordinate1.x * sinAngle 
                                             + m_pressParams.coordinate1.y * cosAngle) * scale;
            } else {
                m_scrToImg->coordinate2.x = (m_pressParams.coordinate2.x * cosAngle 
                                             - m_pressParams.coordinate2.y * sinAngle) * scale;
                m_scrToImg->coordinate2.y = (m_pressParams.coordinate2.x * sinAngle 
                                             + m_pressParams.coordinate2.y * cosAngle) * scale;
            }
        }
        
        emit coordinateSystemChanged();
        update();
      }
    }
  }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_interactionMode == PanMode) {
      m_isDragging = false;
    } else if (m_interactionMode == SelectMode) {
      if (m_draggedPointIndex != -1) {
        m_draggedPointIndex = -1;
        emit pointsChanged();
      } else if (m_rubberBand && m_rubberBand->isVisible()) {
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
    } else if (m_interactionMode == AddPointMode) {
      if (m_rubberBand && m_rubberBand->isVisible()) {
        QRect rect = m_rubberBand->geometry();
        m_rubberBand->hide();
        
        // Check if this was a small movement (single click) or area selection
        QPoint dragDistance = event->pos() - m_rubberBandOrigin;
        bool isClick = dragDistance.manhattanLength() < 5;
        
        if (isClick) {
          // Single point click - emit pointAdded signal
          colorscreen::point_t imgPos = widgetToImage(event->position());
          emit pointAdded(imgPos, colorscreen::point_t{0, 0}, colorscreen::point_t{0, 0});
        } else {
          // Area selection - emit areaSelected signal
          emit areaSelected(rect);
        }
      }
    }
  }

  // Handle SetCenterMode
  if (m_interactionMode == SetCenterMode) {
    if (m_dragTarget == DragTarget::Axis1 || m_dragTarget == DragTarget::Axis2) {
      releaseMouse(); // Release the grab
    }

    // Handle click logic (only if it was a small movement)
    QPointF dragDistance = event->position() - m_dragStartWidget;
    bool isClick = dragDistance.manhattanLength() < 5;

    if (m_dragTarget == DragTarget::Center && isClick && m_scrToImg) {
       // Set center to this specific point.
       m_scrToImg->center = widgetToImage(event->position());
       emit coordinateSystemChanged();
       update();
    } else if (m_dragTarget == DragTarget::Axis1 && isClick && m_scrToImg) {
       // Set Coordinate1
       colorscreen::point_t clickImg = widgetToImage(event->position());
       colorscreen::point_t center = m_scrToImg->center;

       double newC1x = clickImg.x - center.x;
       double newC1y = clickImg.y - center.y;

       double newLenSq = newC1x*newC1x + newC1y*newC1y;

       if (newLenSq > 4.0) {
           if (m_lockRelativeCoordinates) {
               // Update Coordinate1 and coordinate 2 to preserve relative
               double oldC1x = m_pressParams.coordinate1.x;
               double oldC1y = m_pressParams.coordinate1.y;
               double oldLen = sqrt(oldC1x*oldC1x + oldC1y*oldC1y);
               double newLen = sqrt(newLenSq);
               
               double angleC1Old = atan2(oldC1y, oldC1x);
               double angleC2Old = atan2(m_pressParams.coordinate2.y, m_pressParams.coordinate2.x);
               double angleC1New = atan2(newC1y, newC1x);

               double relAngle = angleC2Old - angleC1Old;
               double angleC2New = angleC1New + relAngle;

               double oldLenC2 = sqrt(m_pressParams.coordinate2.x*m_pressParams.coordinate2.x + 
                                      m_pressParams.coordinate2.y*m_pressParams.coordinate2.y);
               double scaleRatio = (oldLen > 1e-9) ? oldLenC2 / oldLen : 1.0;
               double newLenC2 = newLen * scaleRatio;

               if (newLenC2 >= 2.0) {
                   m_scrToImg->coordinate1.x = newC1x;
                   m_scrToImg->coordinate1.y = newC1y;
                   m_scrToImg->coordinate2.x = newLenC2 * cos(angleC2New);
                   m_scrToImg->coordinate2.y = newLenC2 * sin(angleC2New);
                   emit coordinateSystemChanged();
                   update();
               }
           } else {
               // Independent update
               m_scrToImg->coordinate1.x = newC1x;
               m_scrToImg->coordinate1.y = newC1y;
               emit coordinateSystemChanged();
               update();
           }
       }
    } else if (m_dragTarget == DragTarget::Axis2 && isClick && m_scrToImg) {
       // Set Coordinate2
       colorscreen::point_t clickImg = widgetToImage(event->position());
       colorscreen::point_t center = m_scrToImg->center;

       double newC2x = clickImg.x - center.x;
       double newC2y = clickImg.y - center.y;

       double newLenC2Sq = newC2x*newC2x + newC2y*newC2y;

       if (newLenC2Sq > 4.0) {
           if (m_lockRelativeCoordinates) {
               double oldC2x = m_pressParams.coordinate2.x;
               double oldC2y = m_pressParams.coordinate2.y;
               double oldLenC2 = sqrt(oldC2x*oldC2x + oldC2y*oldC2y);
               double newLenC2 = sqrt(newLenC2Sq);
               
               double angleC1Old = atan2(m_pressParams.coordinate1.y, m_pressParams.coordinate1.x);
               double angleC2Old = atan2(oldC2y, oldC2x);
               double angleC2New = atan2(newC2y, newC2x);
               
               double relAngle = angleC2Old - angleC1Old;
               double angleC1New = angleC2New - relAngle;
               
               double oldLenC1 = sqrt(m_pressParams.coordinate1.x*m_pressParams.coordinate1.x + 
                                      m_pressParams.coordinate1.y*m_pressParams.coordinate1.y);
               double scaleRatioNum = (oldLenC1 > 1e-9) ? oldLenC2 / oldLenC1 : 1.0;
               double newLenC1 = (scaleRatioNum > 1e-9) ? newLenC2 / scaleRatioNum : newLenC2;
               
               if (newLenC1 >= 2.0) {
                   m_scrToImg->coordinate2.x = newC2x;
                   m_scrToImg->coordinate2.y = newC2y;
                   m_scrToImg->coordinate1.x = newLenC1 * cos(angleC1New);
                   m_scrToImg->coordinate1.y = newLenC1 * sin(angleC1New);
                   emit coordinateSystemChanged();
                   update();
               }
           } else {
               // Independent update
               m_scrToImg->coordinate2.x = newC2x;
               m_scrToImg->coordinate2.y = newC2y;
               emit coordinateSystemChanged();
               update();
           }
       }
    }

    // Reset drag target
    m_dragTarget = DragTarget::None;
    event->accept();
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

void ImageWidget::keyPressEvent(QKeyEvent *event) {
  // Handle fullscreen exit keys
  if (isFullScreen() && (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F11)) {
    emit exitFullscreenRequested();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

colorscreen::point_t ImageWidget::widgetToImage(QPointF p) const {
  if (!m_scan || !m_scrToImg)
    return {p.x(), p.y()};

  // 1. Undo Offset & Scale (View -> Transformed)
  double xr = p.x() / m_scale + m_viewX;
  double yr = p.y() / m_scale + m_viewY;

  // 2. Use Transformer (Transformed -> Scan)
  CoordinateTransformer transformer(m_scan.get(), *m_rparams);
  return transformer.transformedToScan({xr, yr});
}

QPointF ImageWidget::imageToWidget(colorscreen::point_t p) const {
  if (!m_scan || !m_scrToImg)
    return QPointF(p.x, p.y);

  // 1. Transformer (Scan -> Transformed)
  CoordinateTransformer transformer(m_scan.get(), *m_rparams);
  colorscreen::point_t tr = transformer.scanToTransformed(p);

  // 2. Apply View Offset & Scale (Transformed -> View)
  return QPointF((tr.x - m_viewX) * m_scale, (tr.y - m_viewY) * m_scale);
}

void ImageWidget::rotateLeft() {
    if (!m_rparams) return;
    int oldRot = (int)m_rparams->scan_rotation;
    int newRot = (oldRot - 1 + 4) % 4;
    pivotViewport(oldRot, newRot);
    m_rparams->scan_rotation = newRot;
    requestRender();
}

void ImageWidget::rotateRight() {
    if (!m_rparams) return;
    int oldRot = (int)m_rparams->scan_rotation;
    int newRot = (oldRot + 1) % 4;
    pivotViewport(oldRot, newRot);
    m_rparams->scan_rotation = newRot;
    requestRender();
}

void ImageWidget::pivotViewport(int oldRotIdx, int newRotIdx) {
    if (!m_scan) return;
    
    // Viewport center in current transformed coordinates (relative to image)
    double centerX_tr = m_viewX + (width() / m_scale) / 2.0;
    double centerY_tr = m_viewY + (height() / m_scale) / 2.0;

    // We use CoordinateTransformer twice: once with OLD rotation, once with NEW
    colorscreen::render_parameters oldParams = *m_rparams;
    oldParams.scan_rotation = oldRotIdx;
    CoordinateTransformer oldTrans(m_scan.get(), oldParams);
    
    // Map to Scan
    colorscreen::point_t scanPt = oldTrans.transformedToScan({centerX_tr, centerY_tr});
    
    // Create params for NEW
    colorscreen::render_parameters newParams = *m_rparams;
    newParams.scan_rotation = newRotIdx;
    CoordinateTransformer newTrans(m_scan.get(), newParams);
    
    // Map to Transformed (New)
    colorscreen::point_t newCenter_tr = newTrans.scanToTransformed(scanPt);
    
    // Set new viewX/Y to maintain center
    m_viewX = newCenter_tr.x - (width() / m_scale) / 2.0;
    m_viewY = newCenter_tr.y - (height() / m_scale) / 2.0;
}

void ImageWidget::fitToView() {
  if (!m_scan || m_scan->width <= 0)
    return;

  double w = width();
  double h = height();

  CoordinateTransformer transformer(m_scan.get(), *m_rparams);
  QSize transformedSize = transformer.getTransformedSize();
  double imgW = transformedSize.width();
  double imgH = transformedSize.height();

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

void ImageWidget::selectAll() {
  if (!m_solver || m_solver->points.empty()) return;
  
  bool changed = false;
  for (size_t i = 0; i < m_solver->points.size(); ++i) {
    auto result = m_selectedPoints.insert({i, SelectedPoint::RegistrationPoint});
    if (result.second) changed = true;
  }
  
  if (changed) {
    emit selectionChanged();
    update();
  }
}

void ImageWidget::deleteSelectedPoints() {
  if (!m_solver || m_selectedPoints.empty()) return;
  
  emit pointManipulationStarted();
  
  std::vector<size_t> toDelete;
  for (const auto& sp : m_selectedPoints) {
    if (sp.type == SelectedPoint::RegistrationPoint) {
      toDelete.push_back(sp.index);
    }
  }
  
  if (toDelete.empty()) return;
  
  // Sort descending to avoid index shifting during deletion
  std::sort(toDelete.rbegin(), toDelete.rend());
  
  for (size_t idx : toDelete) {
    if (idx < m_solver->points.size()) {
      m_solver->points.erase(m_solver->points.begin() + idx);
    }
  }
  
  m_selectedPoints.clear();
  m_simulatedPointsDirty = true;
  emit selectionChanged();
  emit pointsChanged();
  update();
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

void ImageWidget::updateSimulatedPoints() {
  if (!m_solver || !m_scan || !m_rparams || !m_scrToImg) return;
  
  m_simulatedPoints.resize(m_solver->points.size());
  
  colorscreen::scr_to_img map;
  map.set_parameters(*m_scrToImg, *m_scan);
  
  bool isVerticalStrips = colorscreen::screen_with_vertical_strips_p(m_scrToImg->type);
  
  for (size_t i = 0; i < m_solver->points.size(); ++i) {
    const auto &pt = m_solver->points[i];
    colorscreen::point_t scr_p = pt.scr;

    if (isVerticalStrips) {
      colorscreen::point_t scr2 = map.to_scr(pt.img);
      scr_p.y = scr2.y;
    }
    m_simulatedPoints[i] = map.to_img(scr_p);
  }
  
  m_simulatedPointsDirty = false;
}

// Request a new render job (non-blocking)
void ImageWidget::requestRender()
{
    // Capture pending view parameters
    m_pendingViewX = m_viewX;
    m_pendingViewY = m_viewY;
    m_pendingScale = m_scale;
    m_hasPendingRender = true;
    
    m_renderQueue.requestRender();
}

void ImageWidget::onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress)
{
    if (!m_renderer) {
         m_renderQueue.reportFinished(reqId, false);
         return; 
    }

    // Prepare Render
    colorscreen::render_parameters params = m_rparams ? *m_rparams : colorscreen::render_parameters();
    colorscreen::render_parameters frameParams = m_rparams ? *m_rparams : colorscreen::render_parameters();

    double xOffset = m_pendingViewX;
    double yOffset = m_pendingViewY;
    double scale = m_pendingScale;
    int w = width();
    int h = height();
    
    // Trigger generic render
    bool result = QMetaObject::invokeMethod(m_renderer, "render", Qt::QueuedConnection,
                              Q_ARG(int, reqId),
                              Q_ARG(double, xOffset),
                              Q_ARG(double, yOffset),
                              Q_ARG(double, scale),
                              Q_ARG(int, w),
                              Q_ARG(int, h),
                              Q_ARG(colorscreen::render_parameters, frameParams),
                              Q_ARG(std::shared_ptr<colorscreen::progress_info>, progress),
                              Q_ARG(const char*, "Rendering image"));
    
    if (!result) {
        m_renderQueue.reportFinished(reqId, false);
    }
}
void ImageWidget::handleImageReady(int reqId, QImage image, double xOffset, double yOffset, double scale, bool success)
{
    bool valid = success; // You might want to check tracking explicitly if needed, but Queue handles ID tracking.
    
    // We update m_pixmap only if this is a valid success.
    // NOTE: RenderQueue::reportFinished manages the cancellation of older jobs.
    // However, RenderQueue doesn't know about "is this image newer than what I have on screen?".
    // Actually, RenderQueue just reports "Finished". It doesn't decide what to display.
    // WE need to decide whether to show it.
    
    // But wait, RenderQueue::reportFinished cancels OLDER jobs.
    // So if this job finishes successfully, we should tell queue.
    
    m_renderQueue.reportFinished(reqId, success);

    if (success) {
        // We simply trust that if it finished successfully and wasn't cancelled, it's good.
        // Actually, we should check timestamps if we want to be super safe, but the queue logic 
        // cancels stale ones. If a stale one arrives here, it means it wasn't cancelled *yet* or race.
        // But since we are main thread, and cancellation sends signal, maybe we are OK?
        
        // Let's implement a simple check:
        // We need a local tracker? No, the queue does it.
        // But the queue logic "if reqId > m_lastCompletedReqId" is inside Queue now.
        // We can't access it.
        
        // Actually, we should probably update display ALWAYS if success, 
        // because the queue wouldn't have cancelled it if it was super outdated?
        // Wait, the queue cancels older active jobs when a newer one finishes.
        // But if an older one finishes *after* a newer one (race?), the queue handles it?
        
        // Let's assume we update.
        m_pixmap = image;
        m_lastRenderedScale = scale;
        m_lastRenderedX = xOffset;
        m_lastRenderedY = yOffset;
        update(); 
    }
}
