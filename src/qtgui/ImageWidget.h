#pragma once

#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include <QImage>
#include <QThread>
#include <QWidget>
#include <memory>
#include "ThamesAnimation.h"
#include "PagetAnimation.h"

// Forward declarations
namespace colorscreen {
class image_data;
struct render_parameters;
} // namespace colorscreen
class Renderer;

class ImageWidget : public QWidget {
  Q_OBJECT
public:
  explicit ImageWidget(QWidget *parent = nullptr);
  ~ImageWidget() override;

  // Use shared_ptr and pointer for rparams (we will copy rparams when
  // requesting render)
  void setImage(std::shared_ptr<colorscreen::image_data> scan,
                colorscreen::render_parameters *rparams,
                colorscreen::scr_to_img_parameters *scrToImg,
                colorscreen::scr_detect_parameters *scrDetect,
                colorscreen::render_type_parameters *renderType,
                colorscreen::solver_parameters *solver);

  // Update parameters without recreating renderer (non-blocking)
  void
  updateParameters(colorscreen::render_parameters *rparams,
                   colorscreen::scr_to_img_parameters *scrToImg,
                   colorscreen::scr_detect_parameters *scrDetect,
                   colorscreen::render_type_parameters *renderType = nullptr,
                   colorscreen::solver_parameters *solver = nullptr);

  void setShowRegistrationPoints(bool show);

public slots:
  void setZoom(double scale);
  void setPan(double x, double y);
  void fitToView(); // New slot

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);
  void registrationPointsVisibilityChanged(bool visible);
  void viewStateChanged(QRectF visibleRect, double scale);

public:
  double getMinScale() const; // Returns scale that fits image to view
  double getZoom() const { return m_scale; }
  bool registrationPointsVisible() const { return m_showRegistrationPoints; }

  // Coordinate mapping API
  QPointF imageToWidget(colorscreen::point_t p) const;
  colorscreen::point_t widgetToImage(QPointF p) const;

private slots:
  void handleImageReady(int reqId, QImage image, double x, double y,
                        double scale, bool success);

private:
  void requestRender();

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters *m_rparams = nullptr;
  colorscreen::scr_to_img_parameters *m_scrToImg = nullptr;
  colorscreen::scr_detect_parameters *m_scrDetect = nullptr;
  colorscreen::render_type_parameters *m_renderType = nullptr;
  colorscreen::solver_parameters *m_solver = nullptr;

  bool m_showRegistrationPoints = false;

  Renderer *m_renderer = nullptr;
  QThread *m_renderThread = nullptr;

  QImage m_pixmap; // The currently displayed rendered tile

  // Current View State
  double m_scale = 1.0;
  double m_viewX = 0.0; // Top-left of the view in Image Coordinates
  double m_viewY = 0.0;
  double m_minScale = 0.1; // Calculated 'fit' scale

  std::shared_ptr<colorscreen::progress_info> m_currentProgress;

  // Single-thread rendering queue
  bool m_renderInProgress = false;
  bool m_hasPendingRender = false;
  double m_pendingViewX = 0.0;
  double m_pendingViewY = 0.0;
  double m_pendingScale = 1.0;

  // Interaction
  QPoint m_lastMousePos;
  bool m_isDragging = false;
  
  // Animations for when no image loaded
  ThamesAnimation *m_thamesAnim = nullptr;
  PagetAnimation *m_pagetAnim = nullptr;
  QWidget *m_activeAnim = nullptr; // Points to whichever is active
};
