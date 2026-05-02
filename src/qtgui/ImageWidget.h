#pragma once

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/colorscreen.h"
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
#include "HurleyAnimation.h"

// Forward declarations
namespace colorscreen {
class image_data;
struct render_parameters;
struct progress_info;
} // namespace colorscreen

class Renderer;

class ImageWidget : public QWidget {
  Q_OBJECT
public:
  struct RenderRequestData {
    double xOffset;
    double yOffset;
    double scale;
    int w;
    int h;
    colorscreen::render_parameters params;
  };

  explicit ImageWidget(QWidget *parent = nullptr);
  ~ImageWidget() override;

  // Use shared_ptr and pointer for rparams (we will copy rparams when
  // requesting render)
  /**
   * @brief Sets the image and rendering parameters.
   * Initializing this will create the background Renderer thread.
   * 
   * @param scan Shared pointer to the image data.
   * @param rparams Core rendering parameters (crop, rotation, etc).
   * @param scrToImg Screen-to-image mapping parameters.
   * @param scrDetect Screen detection parameters.
   * @param renderType Parameters for specific renderers (e.g. Paget, Thames).
   * @param solver Parameters for the registration solver.
   */
  void setImage(std::shared_ptr<colorscreen::image_data> scan,
                colorscreen::render_parameters *rparams,
                colorscreen::scr_to_img_parameters *scrToImg,
                colorscreen::scr_detect_parameters *scrDetect,
                colorscreen::render_type_parameters *renderType,
                colorscreen::solver_parameters *solver);

  /**
   * @brief Updates rendering parameters without recreating the renderer.
   * @param rparams Core rendering parameters.
   * @param scrToImg Screen-to-image mapping parameters.
   * @param scrDetect Screen detection parameters.
   * @param renderType Renderer-specific parameters.
   * @param solver Solver parameters.
   */
  void
  updateParameters(colorscreen::render_parameters *rparams,
                   colorscreen::scr_to_img_parameters *scrToImg,
                   colorscreen::scr_detect_parameters *scrDetect,
                   colorscreen::render_type_parameters *renderType = nullptr,
                   colorscreen::solver_parameters *solver = nullptr);

  /**
   * @brief Sets the profile spots to be displayed.
   * @param spots Vector of spot coordinates.
   * @param results Vector of color matching results for these spots.
   */
  void setProfileSpots(const std::vector<colorscreen::point_t> *spots,
                       const std::vector<colorscreen::color_match> *results);

  /**
   * @brief Toggles the visibility of registration points.
   * @param show True to show.
   */
  void setShowRegistrationPoints(bool show);

  /**
   * @brief Toggles the visibility of profile spots.
   * @param show True to show.
   */
  void setShowProfileSpots(bool show);

  /**
   * @brief Clears the current selection.
   */
  void clearSelection();
  
  // Coordinate System options
  /**
   * @brief Sets whether relative coordinates should be locked during manipulation.
   * @param lock True to lock.
   */
  void setLockRelativeCoordinates(bool lock) { m_lockRelativeCoordinates = lock; }

  /**
   * @brief Selects all registration points.
   */
  void selectAll();

  /**
   * @brief Deletes all currently selected points.
   */
  void deleteSelectedPoints();

  enum InteractionMode {
    PanMode,         ///< Panning and zooming
    SelectMode,      ///< Selecting and moving registration points
    AddPointMode,    ///< Adding new registration points or selecting areas
    SetCenterMode,   ///< Adjusting the screen coordinate system center and axes
    CropMode,        ///< Selecting crop area
    GenericAreaMode, ///< Generic area selection
    ExploreMode,     ///< Precision navigation mode with auto-centering
    MeasureMode      ///< Distance measurement tool
  };

  /**
   * @brief Sets the current interaction mode.
   * @param mode The new interaction mode.
   */
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
  /**
   * @brief Sets the zoom level (scale factor).
   * @param scale The new scale.
   */
  void setZoom(double scale);

  /**
   * @brief Animates a relative zoom change.
   * @param factor Zoom factor (e.g. 1.1 for 10% zoom in).
   */
  void smoothZoomBy(double factor);

  /**
   * @brief Animates zoom to a specific target scale.
   * @param targetScale The target scale.
   * @param fast If true, use a faster animation.
   */
  void smoothZoomTo(double targetScale, bool fast = false);

  /**
   * @brief Sets the pan position (top-left of the view in image coordinates).
   * @param x X coordinate.
   * @param y Y coordinate.
   */
  void setPan(double x, double y);

  /**
   * @brief Centers the view on a specific image coordinate.
   * @param imgPos The point to center on.
   */
  void centerOn(colorscreen::point_t imgPos);

  /**
   * @brief Internal helper to track the widget size.
   * @param s New size.
   */
  void setLastSize(QSize s) { m_lastSize = s; }

  /**
   * @brief Instantly fits the image to the current widget size.
   */
  void fitToView();

  /**
   * @brief Smoothly animates the image to fit the current widget size.
   */
  void smoothFitToView();
  /**
   * @brief Rotates the image viewport 90 degrees counter-clockwise.
   */
  void rotateLeft();

  /**
   * @brief Rotates the image viewport 90 degrees clockwise.
   */
  void rotateRight();

  /**
   * @brief Internal helper to pivot the viewport around its center during rotation.
   * @param oldRotIdx Previous rotation index.
   * @param newRotIdx New rotation index.
   */
  void pivotViewport(int oldRotIdx, int newRotIdx);

  /**
   * @brief Toggles explore mode (precision navigation).
   * @param enable True to enable.
   */
  void setExploreMode(bool enable);

  /**
   * @brief Sets the tolerance for the registration error heatmap.
   * @param tol Tolerance value (typically in pixels/units).
   */
  void setHeatmapTolerance(double tol);

  /**
   * @brief Sets the exaggeration factor for error arrows.
   * @param ex Exaggeration factor.
   */
  void setExaggerate(double ex);

  /**
   * @brief Sets the maximum length for error arrows in pixels.
   * @param len Maximum length.
   */
  void setMaxArrowLength(double len);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;

signals:
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);
  void registrationPointsVisibilityChanged(bool visible);
  void interactionModeChanged(ImageWidget::InteractionMode mode);
  void viewStateChanged(QRectF visibleRect, double scale);
  void selectionChanged();
  void pointAdded(colorscreen::point_t imgPos, colorscreen::point_t scrPos,
                  colorscreen::point_t color);
  void setCenterRequested(colorscreen::point_t imgPos);
  void areaSelected(QRect area);
  void distanceMeasured(colorscreen::point_t p1, colorscreen::point_t p2);
  void coordinateSystemChanged();
  void coordinateSystemManipulationStarted();
  void coordinateSystemManipulationFinished();
  void pointManipulationStarted();
  void registrationPointMoved(size_t index, colorscreen::point_t newPos);
  void pointsChanged();
  void exitFullscreenRequested();
  void profileSpotRemoveRequested(int index);

private:
  // Drawing helpers to keep paintEvent clean
  void drawPointsOverlay(QPainter &p);
  void drawProfileSpots(QPainter &p);
  void drawScreenCoordinateSystem(QPainter &p);
  void drawMeasurement(QPainter &p);

  // Interaction handlers
  void handleSetCenterPress(QMouseEvent *event);
  void handleSelectPress(QMouseEvent *event);
  void handleAreaPress(QMouseEvent *event);
  void handleMeasurePress(QMouseEvent *event);

  void handleSetCenterMove(QMouseEvent *event);
  void handleSelectMove(QMouseEvent *event);
  void handleExploreMove(QMouseEvent *event);
  void handleMeasureMove(QMouseEvent *event);

  void handleSetCenterRelease(QMouseEvent *event);
  void handleSelectRelease(QMouseEvent *event);
  void handleAreaRelease(QMouseEvent *event);
  void handleMeasureRelease(QMouseEvent *event);

public:
  /**
   * @brief Returns the minimum scale that fits the image to the view.
   * @return Minimum scale factor.
   */
  double getMinScale() const;

  /**
   * @brief Returns the current zoom level.
   * @return Scale factor.
   */
  double getZoom() const { return m_scale; }

  /**
   * @brief Returns the current interaction mode.
   * @return The interaction mode.
   */
  InteractionMode interactionMode() const { return m_interactionMode; }
  /**
   * @brief Checks if registration points are currently visible.
   * @return true if visible.
   */
  bool registrationPointsVisible() const { return m_showRegistrationPoints; }

  /**
   * @brief Gets the number of registration points in the current solver.
   * @return The point count.
   */
  size_t registrationPointCount() const {
    return m_solver ? m_solver->points.read().size() : 0;
  }

  /**
   * @brief Returns the set of currently selected points.
   * @return Reference to the selection set.
   */
  const std::set<SelectedPoint>& selectedPoints() const { return m_selectedPoints; }

  // Coordinate mapping API
  /**
   * @brief Converts image coordinates to widget coordinates.
   * Accounts for current zoom, pan, and image transformations (rotation/mirror/crop).
   * @param p Point in image coordinates.
   * @return Point in widget (pixel) coordinates.
   */
  QPointF imageToWidget(colorscreen::point_t p) const;

  /**
   * @brief Converts widget coordinates to image coordinates.
   * Inverse of imageToWidget.
   * @param p Point in widget (pixel) coordinates.
   * @return Point in image coordinates.
   */
  colorscreen::point_t widgetToImage(QPointF p) const;

private slots:
  void handleImageReady(int reqId, QImage image, double x, double y,
                        double scale, bool success);
  void onTriggerRender(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);
  void exploreTick();

private:
  void requestRender();

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters *m_rparams = nullptr;
  colorscreen::scr_to_img_parameters *m_scrToImg = nullptr;
  colorscreen::scr_detect_parameters *m_scrDetect = nullptr;
  colorscreen::render_type_parameters *m_renderType = nullptr;
  colorscreen::solver_parameters *m_solver = nullptr;
  const std::vector<colorscreen::point_t> *m_profileSpots = nullptr;
  const std::vector<colorscreen::color_match> *m_profileSpotResults = nullptr;
  
  // Coordinate system editing state
  enum class DragTarget { None, Center, Axis1, Axis2 };
  DragTarget m_dragTarget = DragTarget::None;
  QPointF m_dragStartWidget;
  colorscreen::point_t m_dragStartImg;
  colorscreen::scr_to_img_parameters m_pressParams;
  bool m_lockRelativeCoordinates = true;

  bool m_showRegistrationPoints = false;
  bool m_showProfileSpots = true;

  Renderer *m_renderer = nullptr;
  QThread *m_renderThread = nullptr;

  QImage m_pixmap; // The currently displayed rendered tile

  double m_scale = 1.0;
  double m_viewX = 0.0; // Top-left of the view in Image Coordinates
  double m_viewY = 0.0;
  double m_minScale = 0.1; // Calculated 'fit' scale
  QSize m_lastSize; // Last known widget size for robust resizing

  double m_heatmapTolerance = 0.1;
  double m_exaggerate = 200.0;
  double m_maxArrowLength = 100.0;

  // Concurrent Rendering — image and points overlay run in parallel.
  TaskQueue m_renderQueue;  // image tile rendering
  TaskQueue m_pointsQueue;  // points overlay pre-rendering
  bool m_pointsRenderPending = false;

  
  // Last successfully rendered state to compare against updates
  double m_lastRenderedScale = 1.0;
  double m_lastRenderedX = 0.0;
  double m_lastRenderedY = 0.0;
  int m_lastCompletedReqId = 0;
  
  // Interaction
  QPoint m_lastMousePos;
  bool m_isDragging = false;
  InteractionMode m_interactionMode = PanMode;
  std::set<SelectedPoint> m_selectedPoints;
  /* Pre-rendered points overlay image and the view state it was rendered at.
     Composited on top of m_pixmap in paintEvent — same stretch logic.  */
  QImage  m_pointsOverlay;
  double  m_lastPointsScale = 1.0;
  double  m_lastPointsX = 0.0;
  double  m_lastPointsY = 0.0;
  bool    m_pointsOverlayDirty = true;
  colorscreen::scr_to_img_parameters m_lastScrToImg;

  colorscreen::int_optional_image_area m_lastScanCrop;
  int m_lastRotation = 0;
  bool m_lastMirror = false;
  QRubberBand *m_rubberBand = nullptr;
  QPoint m_rubberBandOrigin;
  int m_draggedPointIndex = -1;
  colorscreen::point_t m_measureStart;
  colorscreen::point_t m_measureEnd;
  bool m_isMeasuring = false;

  QTimer *m_exploreTimer = nullptr;
  double m_exploreTargetX = 0.0;
  double m_exploreTargetY = 0.0;
  double m_exploreTargetScale = 1.0;
  double m_exploreZoomSpeed = 0.15;
  bool m_zoomFocusCenter = false;
  bool m_panAnimationActive = false;
  bool m_ignoreNextMouseMove = false;

  bool m_plusHeld = false;
  bool m_minusHeld = false;
  double m_keyboardZoomVelocity = 0.0;

  void updateSimulatedPoints();
  // Schedule a background render of the points overlay image.
  void schedulePointsOverlayRender();

  
  // Animations for when no image loaded
  ThamesAnimation *m_thamesAnim = nullptr;
  PagetAnimation *m_pagetAnim = nullptr;
  JolyAnimation *m_jolyAnim = nullptr;
  HurleyAnimation *m_hurleyAnim = nullptr;
  QWidget *m_activeAnim = nullptr; // Points to whichever is active
  QTimer *m_refreshTimer = nullptr;
};
