#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "Renderer.h"
#include "TaskQueue.h"
#include <QImage>
#include <QThread>
#include <QWidget>
#include <QRubberBand>
#include <QTimer>
#include <memory>
#include <QElapsedTimer>
#include "../libcolorscreen/include/progress-info.h"
#include <list>
#include <set>
#include "ThamesAnimation.h"
#include "PagetAnimation.h"
#include "JolyAnimation.h"
// Forward declarations
namespace colorscreen {
class image_data;
struct render_parameters;
struct progress_info; // Added forward declaration for progress_info
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
  void clearSelection();
  
  // Coordinate System options
  void setLockRelativeCoordinates(bool lock) { m_lockRelativeCoordinates = lock; }
  void selectAll();
  void deleteSelectedPoints();

  enum InteractionMode { PanMode, SelectMode, AddPointMode, SetCenterMode };
  void setInteractionMode(InteractionMode mode);

  struct SelectedPoint {
    size_t index;
    enum Type { RegistrationPoint } type;
    bool operator<(const SelectedPoint &other) const {
      if (type != other.type) return type < other.type;
      return index < other.index;
    }
  };

public slots:
  void setZoom(double scale);
  void setPan(double x, double y);
  void setLastSize(QSize s) { m_lastSize = s; }
  void fitToView(); // New slot
  void rotateLeft();
  void rotateRight();
  void pivotViewport(int oldRotIdx, int newRotIdx);

private:

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);
  void registrationPointsVisibilityChanged(bool visible);
  void viewStateChanged(QRectF visibleRect, double scale);
  void selectionChanged();
  void pointAdded(colorscreen::point_t imgPos, colorscreen::point_t scrPos,
                  colorscreen::point_t color);
  void setCenterRequested(colorscreen::point_t imgPos);
  void areaSelected(QRect area);
  void coordinateSystemChanged();
  void pointManipulationStarted();
  void registrationPointMoved(size_t index, colorscreen::point_t newPos);
  void pointsChanged();
  void exitFullscreenRequested();

public:
  double getMinScale() const; // Returns scale that fits image to view
  double getZoom() const { return m_scale; }
  bool registrationPointsVisible() const { return m_showRegistrationPoints; }
  size_t registrationPointCount() const {
    return m_solver ? m_solver->points.size() : 0;
  }
  const std::set<SelectedPoint>& selectedPoints() const { return m_selectedPoints; }

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
  
  // Coordinate system editing state
  enum class DragTarget { None, Center, Axis1, Axis2 };
  DragTarget m_dragTarget = DragTarget::None;
  QPointF m_dragStartWidget;
  colorscreen::point_t m_dragStartImg;
  colorscreen::scr_to_img_parameters m_pressParams;
  bool m_lockRelativeCoordinates = true;

  bool m_showRegistrationPoints = false;

  Renderer *m_renderer = nullptr;
  QThread *m_renderThread = nullptr;

  QImage m_pixmap; // The currently displayed rendered tile


  double m_scale = 1.0;
  double m_viewX = 0.0; // Top-left of the view in Image Coordinates
  double m_viewY = 0.0;
  double m_minScale = 0.1; // Calculated 'fit' scale
  QSize m_lastSize; // Last known widget size for robust resizing

  // Concurrent Rendering
  TaskQueue m_renderQueue; // Manages IDs and progress notifications
  
  
private slots:
  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress);

private:
  
private:
  // Last successfully rendered state to compare against updates
  double m_lastRenderedScale = 1.0;
  double m_lastRenderedX = 0.0;
  double m_lastRenderedY = 0.0;
  
  // Pending render request (if queue is full)
  bool m_hasPendingRender = false;
  double m_pendingViewX = 0.0;
  double m_pendingViewY = 0.0;
  double m_pendingScale = 1.0;

  // Interaction
  QPoint m_lastMousePos;
  bool m_isDragging = false;
  InteractionMode m_interactionMode = PanMode;
  std::set<SelectedPoint> m_selectedPoints;
  std::vector<colorscreen::point_t> m_simulatedPoints;
  bool m_simulatedPointsDirty = true;
  colorscreen::scr_to_img_parameters m_lastScrToImg;
  QRubberBand *m_rubberBand = nullptr;
  QPoint m_rubberBandOrigin;
  int m_draggedPointIndex = -1;

  void updateSimulatedPoints();
  
  // Animations for when no image loaded
  ThamesAnimation *m_thamesAnim = nullptr;
  PagetAnimation *m_pagetAnim = nullptr;
  JolyAnimation *m_jolyAnim = nullptr;
  QWidget *m_activeAnim = nullptr; // Points to whichever is active
  QTimer *m_refreshTimer = nullptr;
};
