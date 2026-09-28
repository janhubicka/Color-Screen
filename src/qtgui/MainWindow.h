#pragma once

#include "MultiLineTabWidget.h"

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/focus-analysis.h"
#include "../libcolorscreen/include/render-type-parameters.h" // Added
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include <QByteArray>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QAction;
class QSplitter;
class QTabWidget;
class QDockWidget; // Added
class QToolBar;    // Added
class QStatusBar;
class QComboBox;   // Added
class QCheckBox;   // Added
class QDoubleSpinBox;
class QVBoxLayout; // Added for Linearization tab
class QLabel;
class QProgressBar;
class QDialog;
class QMessageBox;
class QPushButton;
class QWidget;
#include "ImageWidget.h"

class NavigationView;
class QTimer;
class QThread;
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "FlatFieldWorker.h"
#include "CapturePanel.h"
#include "ColorPanel.h"
#include "ProfilePanel.h"
#include "TilesPanel.h"
#include "ImageLayerPanel.h"
#include "ContactCopyPanel.h"
#include "ParameterState.h"
#include "SharpnessPanel.h"
#include "TaskQueue.h"
#include "BackgroundThreadRegistry.h"
#include "OneShotOperationController.h"
#include "DocumentProgressController.h"
#include "FileRenderController.h"
#include "BacklightChartWidget.h"

class ScreenPanel;
class GeometryPanel;
class GeometrySolverWorker;
class ColorOptimizerWorker;
class AdaptiveSharpeningWorker;
class AdaptiveSharpeningChart; // Added
class QUndoStack; // Forward decl
class ColorScreenApplication;
struct DetectScreenAnalysisResult;

/** Explain an automatic screen-detection failure without inventing a cause. */
QString screenDetectionFailureMessage(colorscreen::scr_type attemptedType);
/** Explain failure to establish initial coordinates for a known regular screen. */
QString coordinateDetectionFailureMessage(colorscreen::scr_type attemptedType);
/** Explain a progressive registration-discovery failure and retained state. */
QString registrationDiscoveryFailureMessage(bool screenAutodetection);
/** Explain geometry-fit failure, preserved document state, and next actions. */
QString geometryFitFailureMessage();
/** Explain profile-optimization failure without inventing a numerical cause. */
QString profileOptimizationFailureMessage();
/** Explain measured-MTF fit failure, retained calibration, and next actions. */
QString mtfModelFitFailureMessage(const QString &detail = QString());
/** Explain slanted-edge MTF measurement failure and retained calibration. */
QString mtfMeasurementFailureMessage(const QString &detail = QString());
/** Explain focus-area discovery failure and retained document state. */
QString focusAreaSearchFailureMessage(const QString &detail = QString());
/** Explain focus-area model-analysis failure and retained document state. */
QString focusAreaAnalysisFailureMessage(const QString &detail = QString());
/** Explain flat-field reference-analysis failure and retained calibration. */
QString flatFieldFailureMessage(const QString &detail = QString());
/** Explain adaptive-sharpening analysis failure and retained correction. */
QString adaptiveSharpeningFailureMessage(const QString &detail = QString());
/** Explain coordinate-refinement failure and retained registration state. */
QString coordinateOptimizationFailureMessage(const QString &detail = QString());
/** Explain failed render/export, snapshot isolation, and retry actions. */
QString renderToFileFailureMessage(const QString &outputPath,
                                   const QString &detail = QString());
/** Explain one-area focus-analysis failure and retained sharpening state. */
QString pointFocusAnalysisFailureMessage(const QString &detail = QString());
/** Explain a failed selected-area auto adjustment without guessing its cause. */
QString areaComputationFailureMessage(const QString &operation);

/** Start the completion-driven workspace ownership/lifecycle smoke test. */
void startWorkspaceChurnSmoke(ColorScreenApplication &app,
                              std::function<void()> completed);

namespace colorscreen {
class screen_map;
}

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  /** Construct one independent image-document window.

      RECOVERYDIRECTORY is unique to this document and is managed by
      ColorScreenApplication.  */
  explicit MainWindow(const QString &recoveryDirectory,
                      QWidget *parent = nullptr);
  ~MainWindow() override;

  // Internal use by Undo Command
  void applyState(const ParameterState &state);

  /** Load FILENAME into this document window.

      New user-initiated opens normally go through ColorScreenApplication so
      an occupied window is never overwritten.  SUPPRESSPARAMPROMPT is reserved
      for crash recovery.  */
  void loadFile(const QString &fileName, bool suppressParamPrompt = false);

  /** Return true when this untouched empty window may host a newly opened
      image instead of allocating another document window.  */
  bool canReuseForOpen() const;

  /** Return the filename shown in the Window menu, including a modification
      marker when this document has unsaved parameters.  */
  QString documentDisplayName() const;

  /** Return the absolute path of the image assigned to this document. */
  QString currentImageFile() const { return m_currentImageFile; }

  /** Save the current document parameters to FILENAME without opening a dialog.
      On success the file becomes the document's current parameter file and the
      undo stack is marked clean. */
  bool saveParametersToFile(const QString &fileName);

  /** Load FILENAME as this document's parameter file without opening a dialog.
      The load is transactional, adopts the file on success, refreshes the UI,
      and resets undo/dirty state to the newly loaded contents. */
  bool loadParameterFile(const QString &fileName);

  /** Resolve this document's save/render close questions without destroying it.

      File -> Exit uses this as a preflight so a later document can veto the
      application close without secondary views or earlier documents already
      having disappeared.  The approval is consumed by the next real close. */
  bool prepareForApplicationClose();

  /** Cancel an unused application-close preflight approval. */
  void cancelPreparedApplicationClose();

  /** Share the loaded scan with additional views of this document. */
  std::shared_ptr<colorscreen::image_data> sharedImageData() const {
    return m_scan;
  }

  /** Return a copy of the shared document parameters for a secondary view. */
  ParameterState documentStateSnapshot() const;

  /** Apply an undoable shared parameter update originating in another view. */
  void applySharedDocumentState(const ParameterState &state,
                                const QString &description,
                                const QString &parameterKey = QString());

  /** Return this view's render-mode settings as the initial state for a new
      secondary view. */
  colorscreen::render_type_parameters viewRenderTypeParameters() const {
    return m_renderTypeParams;
  }

  /** Add the document-owned canvas actions to an ordinary secondary toolbar.
      These actions route through inspectorImageWidget(), so only the active
      ordinary presentation executes them. */
  void appendOrdinaryViewToolActions(QToolBar *toolbar);

  /** Synchronize document-owned detected-patch diagnostics to one ordinary view. */
  void syncDetectedScreenDiagnostics(ImageWidget *image) const;

  /** Synchronize document-owned focus-analysis rectangles to one ordinary view. */
  void syncFocusAreaOverlays(ImageWidget *image) const;

  /** Synchronize the selected stored-MTF spatial overlay to one ordinary view. */
  void syncMtfMeasurementOverlay(ImageWidget *image) const;

  /** Synchronize document-owned profile spots/results to one ordinary view. */
  void syncProfileSpotOverlay(ImageWidget *image) const;

  /** Return the shared Edit menu action for ordinary secondary views. */
  QAction *ordinaryViewEditMenuAction() const;

  /** Return the shared Registration menu action for ordinary secondary views. */
  QAction *ordinaryViewRegistrationMenuAction() const;

  /** Rotate the shared document left from any attached view. */
  void rotateDocumentLeft();

  /** Rotate the shared document right from any attached view. */
  void rotateDocumentRight();

  /** Set horizontal scan mirroring for the shared document from any view. */
  void setDocumentMirror(bool checked);

  /** Set continuous final-plane rotation from any ordinary view. */
  void setDocumentFinalRotation(double degrees);

  /** Set horizontal final-plane mirroring from any ordinary view. */
  void setDocumentFinalMirror(bool checked);

  /** Restore this document from its per-window recovery directory. */
  bool restoreRecoveryState();

  /** Return this document's private crash-recovery directory.

      ColorScreenApplication uses this only for auxiliary recovery metadata
      owned by views associated with this document. */
  QString recoveryDirectory() const { return m_recoveryDir; }

  /** Rebuild this window's Window menu from the application document list. */
  void refreshWindowMenu();

  /** Return the document-specific toolbar while this document is attached to
      the shared workspace. */
  QToolBar *workspaceToolBar() const { return m_toolbar; }

  /** Return the status bar for this presentation's current top-level window.

      All attached document tabs return the enclosing WorkspaceWindow status
      bar.  A detached document returns its private QMainWindow status bar. */
  QStatusBar *statusBar() const;

  /** Return the private status bar used only by a detached document window. */
  QStatusBar *standaloneStatusBar() const;

  /** Route status operations to STATUSBAR while this document is attached.
      Passing nullptr restores routing to the private detached-window bar. */
  void setWorkspaceStatusBar(QStatusBar *statusBar);

  /** Return the document-owned navigation/parameter panel column. */
  QWidget *workspaceInspectorWidget() const { return m_rightColumn; }

  /** Detach and return the document-owned inspector from its current host. */
  QWidget *takeWorkspaceInspector();

  /** Restore the document-owned inspector beside the primary image view. */
  void restoreWorkspaceInspector();

  /** Route inspector navigation and interactive panel tools to IMAGEWIDGET. */
  void setInspectorImageWidget(ImageWidget *imageWidget);

  /** Return the image view currently controlled by the document inspector. */
  ImageWidget *inspectorImageWidget() const {
    return m_inspectorImageRouting.image
               ? m_inspectorImageRouting.image.data()
               : m_imageWidget;
  }

  /** Return the document's primary image view. */
  ImageWidget *primaryImageWidget() const { return m_imageWidget; }

  /** Return this document's transient progress presentation. Attached
      workspaces host it globally regardless of the selected tab. */
  QWidget *workspaceStatusWidget() const {
    return m_progressController.transientWidget();
  }

  /** Remove/restore transient progress from this document's private bar. */
  QWidget *takeWorkspaceStatusWidget();
  void restoreWorkspaceStatusWidget();

  /** Return whether transient progress has passed the display delay. */
  bool hasVisibleTransientProgress() const {
    return m_progressController.hasVisibleTransientProgress();
  }

  /** Return this document's persistent user-visible progress rows.

      Attached documents keep this widget in the workspace global status area
      even while another image is active. */
  QWidget *workspaceUserVisibleStatusWidget() const {
    return m_progressController.userVisibleWidget();
  }

  /** Return the local frameless task-progress dock used by detached views. */
  QDockWidget *userVisibleProgressDock() const {
    return m_progressController.userVisibleDock();
  }

  /** Remove the persistent progress widget from this document's local layout
      so the workspace can host it globally. */
  QWidget *takeUserVisibleStatusWidget();

  /** Return user-visible progress rows to this document's own status widget
      before detaching it from the shared workspace. */
  void restoreUserVisibleStatusWidget();

  /** Remove shared chrome from this QMainWindow before it is embedded in the
      application-level MDI area.  The document state itself is unchanged. */
  void prepareForWorkspaceEmbedding();

  /** Restore the ordinary standalone-window layout after leaving the MDI
      workspace. */
  void restoreFromWorkspaceEmbedding();

  /** Return whether this document is currently presented by the workspace. */
  bool isWorkspaceEmbedded() const { return m_workspaceEmbedded; }

  struct SolverRequestData {
    std::shared_ptr<colorscreen::image_data> scan;
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::solver_parameters solver;
    bool computeMesh;
  };

  struct ColorOptimizerRequestData {
    std::shared_ptr<colorscreen::image_data> scan;
    colorscreen::scr_to_img_parameters scrParams;
    colorscreen::render_parameters     rparams;
    std::vector<colorscreen::point_t>  spots;
  };

  /** Shared document-level measured-MTF fit provenance used by every
      Sharpness panel, including external slanted-edge reference views. */
  QString mtfCalibrationSummary() const;
  bool mtfModelFitRunning() const { return m_mtfFit.running; }
  /** Start one measured-MTF model fit from an immutable dialog/document
      snapshot. The fit is a document final-result operation shared by every
      Sharpness panel. */
  bool requestMtfModelFit(
      const ParameterState &baseline,
      const colorscreen::mtf_parameters &input,
      const colorscreen::mtf_estimation_options &options, int flags,
      QWidget *resultParent = nullptr);

  /** Lifecycle descriptor for one final-result background operation.

      Generic replacement/progress/publication mechanics live in
      OneShotOperationController; document-specific snapshot and UI callbacks
      remain owned by MainWindow. */
  using OneShotOperation = OneShotOperationController::Operation;

  /** Run WORKER under this document's one-shot progress/cancellation lifecycle.
      Secondary/reference views use the same queue, not independent watchers. */
  void runOneShotOperation(
      OneShotOperation operation,
      std::function<void(colorscreen::progress_info *)> worker);

signals:
  /** Emitted after the loaded image or shared document parameters change.
      Secondary views refresh from this signal while keeping render mode, zoom,
      and pan view-local. */
  void documentStateChanged();
  /** Emitted when session-local MTF fit provenance changes without parameters. */
  void mtfCalibrationStateChanged();
  /** Emitted when detected-patch map availability or visibility changes. */
  void detectedScreenDiagnosticsChanged();
  /** Emitted when document-owned focus-analysis rectangles change. */
  void focusAreaOverlaysChanged();
  /** Emitted when the selected stored-MTF spatial overlay changes. */
  void mtfMeasurementOverlayChanged();
  /** Emitted when session-local profile spot/result overlay data changes. */
  void profileSpotOverlayChanged();
  /** Emitted when this document gains or loses dedicated progress rows. */
  void userVisibleProgressVisibilityChanged(bool visible);
  /** Emitted when delayed transient progress appears or disappears. */
  void transientProgressVisibilityChanged(bool visible);

private slots:
  void onZoomIn();
  void onZoomOut();
  void onZoom100();
  void onZoomFit();
  void onOpenImage();
  void onImageLoaded(); // Called when image is ready
  void onOpenParameters();
  void onSaveParameters();
  void onSaveParametersAs();
  void onRender();
  void onColorOptimizeRequested(bool autoMode);
  void onAddSpotModeRequested(bool active);
  void onModeChanged(int index); // Slot for mode change
  void rotateLeft();
  void rotateRight();
  void toggleFullscreen();
  void onGamutWarningToggled(bool checked);
  void onColorCheckBoxChanged(bool checked);  // Slot for color checkbox
  void onRegistrationPointsToggled(bool checked); // Slot for Registration Points toggle
  void onOptimizeGeometry(bool autoChecked);
  void onNonlinearToggled(bool checked);
      // Slot for Geometry Optimization
  void onSolverFinished(int reqId, colorscreen::scr_to_img_parameters result,
                        bool success, bool cancelled);
  void onTriggerColorOptimize(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);
  void onColorOptimizerFinished(int reqId, colorscreen::render_parameters updatedRparams,
                                std::vector<colorscreen::color_match> results,
                                bool success, bool cancelled);
  void onSelectAll();
  void onDeselectAll();
  void onDeleteSelected();
  void onPruneMisplaced();
  void onCropRequested();
  void onPointAdded(colorscreen::point_t imgPos, colorscreen::point_t scrPos,
                    colorscreen::point_t color);
  void onAreaSelected(QRect area);
  void startAreaSelection(const QString &message, std::function<void(QRect)> callback);
  /** Cancel one pending Generic Area operation and synchronize all UI owners. */
  void cancelAreaSelectionPresentation();
  void onSetCenter(colorscreen::point_t imgPos);
  void onPointManipulationStarted();
  void onCoordinateSystemManipulationStarted();
  void onCoordinateSystemManipulationFinished();
  void updateRegistrationActions();
  void maybeTriggerAutoSolver();
  void onFocusAnalysisRequested(bool checked, uint64_t flags);
  void onFindFocusAreasRequested();
  void onAnalyzeFocusAreasRequested(uint64_t flags);
  void onAdaptiveSharpeningRequested(const AdaptiveSharpeningParameters &parameters);
  void onAdaptiveSharpeningFinished(
      bool success,
      std::shared_ptr<colorscreen::scanner_blur_correction_parameters> result,
      const QString &error);
  void onAutomaticallyAddPointsRequested(const colorscreen::finetune_area_parameters &params);
  void onAutomaticallyAddPointsInAreaRequested(const colorscreen::finetune_area_parameters &params);
  void onAutodetectCoordinatesRequested();
  void onAlternateColorsRequested();
  void onOptimizeCoordinatesRequested();
  void onMeasureRequested();
  void onMeasureMtfRequested(bool checked);
  void onDistanceMeasured(colorscreen::point_t p1, colorscreen::point_t p2);

  // Recent Files
  // Recent Files
  void openRecentFile();
  // Recent Parameters
  void openRecentParams();

protected:
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  // Helper to check for unsaved changes and prompt to save
  bool maybeSave();

  /** Ask every user-visible question that can veto final document closure. */
  bool confirmClose();

  /** Prompt for a parameter filename and save synchronously. */
  bool saveParametersAs();

  /** Return whether undo state or recovered state differs from the last save. */
  bool isDocumentModified() const;

  /** Reload the current image using the selected demosaic mode without
      prompting for parameter data. Existing unsaved parameter state remains
      marked dirty across the asynchronous reload. If AUTODETECTSCREEN is true,
      launch Screen detection only after the replacement image has loaded. */
  void reloadCurrentImageWithDemosaic(bool autodetectScreen = false);

  /** Offer conservative post-load setup guidance when ANALYSIS says the
      normally demosaiced RAW is likely an achromatic Bayer capture. */
  void maybeOfferInitialSetupGuide(
      const colorscreen::monochrome_bayer_analysis &analysis,
      bool suggestDetectedMetadata);

  void setupUi();

  /** Select the saved application-wide inspector stage when it is currently
      available. Hidden-stage fallback never changes the saved preference. */
  void restorePreferredInspectorPanel();

  void createMenus();
  QRect getImageArea(QRect area, ImageWidget *imageWidget = nullptr);
  void pivotViewport(int oldRot, int newRot);
  void createToolbar();  // New helper
  void updateCoordinateSpaceControls();
  void createModeShortcuts(); // Create 1-0 hotkeys for modes
  void updateModeMenu(); // Updates combo box items
  QIcon renderScreenIcon(colorscreen::scr_type type);

  /** Detect an initial basis; optionally continue with automatic point finding.
      The continuation belongs to this request, never to mutable window state. */
  void startCoordinateAutodetection(bool addPointsAfterDetection);

  /** Apply a successful coordinate refinement as one undoable document edit. */
  void applyOptimizedCoordinates(const colorscreen::finetune_result &result);

  /** Submit one geometry solve while tracking the current nonlinear UI mode.

      COMPUTEMESH controls whether this worker recomputes the nonlinear mesh;
      the provenance gate independently records the panel's current mode. */
  void requestGeometryOptimization(bool computeMesh);

  /** Present RESULT and defer accepted publication through a window-modal prompt. */
  void presentDetectedScreenResult(
      const DetectScreenAnalysisResult &result,
      std::shared_ptr<colorscreen::image_data> scan,
      const ParameterState &baseline);

  /** Offer screen-specific dye and resolution recommendations. GEOMETRY is
      already known (or newly detected). APPLY is called only while BASELINE
      and SCAN remain current. ALWAYS_SHOW preserves the RGB screen-detection
      confirmation even when no optional recommendation differs. */
  void presentScreenDetectionSuggestions(
      std::shared_ptr<colorscreen::image_data> scan,
      const ParameterState &baseline,
      const colorscreen::scr_to_img_parameters &geometry,
      bool alwaysShow,
      std::function<void(bool usePreferredColorModel, bool useScreenDpi,
                         double screenDpi)> apply);

  /** Publish one accepted automatic-detection patch map with provenance. */
  void publishDetectedScreenDiagnostics(
      std::shared_ptr<const colorscreen::screen_map> map,
      std::shared_ptr<colorscreen::image_data> scan,
      const ParameterState &detectorInputs);

  /** Clear an obsolete automatic-detection patch map from every presentation. */
  void clearDetectedScreenDiagnostics();

  /** Dismiss obsolete one-shot confirmations without publishing their results. */
  void dismissOneShotPrompts();

  /** Reset focus-area busy state after success, failure, cancellation or staleness. */
  void finishFocusAreaOperation(const QString &summary);

  /** Present snapshot-bound focus diagnostics and return the visible summary. */
  QString presentFocusAreaAnalysisResult(
      const colorscreen::finetune_focus_analysis_result &analysis,
      std::shared_ptr<colorscreen::image_data> scan,
      const ParameterState &baseline, uint64_t flags);

  /** Launch an area-based parameter computation.
      Shows MESSAGE, captures the current image/ParameterState snapshot, then
      runs WORKER through runOneShotOperation(). The whole-state result is
      published only if the same image and exact input state are still current
      and WORKER reports success. A non-cancelled false result leaves the
      document unchanged and presents actionable retry guidance. */
  void runAreaComputation(
      const QString &message,
      const QString &description,
      std::function<void()> onStart,
      std::function<void()> onDone,
      std::function<bool(ParameterState &, colorscreen::image_data &,
                         const colorscreen::int_image_area &,
                         colorscreen::progress_info *)> worker);

  /**
   * @brief Saves the current interaction mode (if not a temporary mode like GenericAreaMode).
   * This is used before switching to a temporary mode (like crop or area selection)
   * so that the user's previous tool (e.g., Select, Pan) can be restored later.
   */
  void saveInteractionMode();

  /**
   * @brief Restores the interaction mode saved by saveInteractionMode().
   * This also ensures that the toolbar buttons are synchronized with the restored mode.
   */
  void restoreInteractionMode();

  /** Clear the temporary Profile/Focus point-click owner without changing the
      canvas mode. Panel toggles are synchronized with signals blocked. */
  void clearPointClickToolPresentation();

  /** Synchronize shared toolbar/menu tool state with the active ordinary view. */
  void syncInspectorInteractionActions(ImageWidget::InteractionMode mode);

  /** Synchronize coordinate-dependent shared actions with the active view. */
  void syncInspectorViewActions();

  /** Return true when IMAGEWIDGET presents this document's current scan. */
  bool acceptsInspectorImageWidget(ImageWidget *imageWidget) const;

  void updateWindowTitle(); // Helper to update window title

  /** Refresh the compact persistent processing-stage summary in the inspector. */
  void updateWorkflowSummary();

  /** Publish current focus-analysis rectangles to every ordinary view. */
  void updateFocusAreaOverlays();
  /** Return the selected stored-MTF spatial record when it belongs to this scan. */
  const colorscreen::mtf_measurement *currentMtfMeasurementOverlay() const;
  /** Refresh/optionally locate the selected measured-MTF ROI in ordinary views. */
  void updateMtfMeasurementOverlay(bool locate = false);
  void refreshMtfCalibrationPresentation();
  /** Return document-owned color-profile fit provenance. */
  QString profileCalibrationSummary() const;
  /** Clear transient automatic focus-area state for this document. */
  void clearFocusAreaAnalysis(const QString &summary = QString());

  /** Return true only while GENERATION/PROGRESS still own the adaptive
      sharpening request and its immutable scan/parameter snapshot is current. */
  bool adaptiveSharpeningRequestCurrent(
      uint64_t generation,
      const std::shared_ptr<colorscreen::progress_info> &progress) const;

  /** Cancel adaptive sharpening when CURRENTSTATE no longer matches the
      request snapshot, and restore the chart to accepted document data. */
  void cancelStaleAdaptiveSharpening(const ParameterState &currentState);

  /** Replace live adaptive-analysis pixels with the accepted correction map. */
  void restoreAdaptiveSharpeningChart();


  // Window state management
  void saveWindowState();
  void restoreWindowState();

  // Recent Files
  void updateRecentFileActions();
  void addToRecentFiles(const QString &filePath);
  void loadRecentFiles();
  void saveRecentFiles();

  // Recent Parameters
  void updateRecentParamsActions();
  void addToRecentParams(const QString &filePath);
  void loadRecentParams();
  void saveRecentParams();

  QMenu *m_fileMenu;
  QMenu *m_editMenu;
  QMenu *m_viewMenu; // Added
  QMenu *m_modeMenu;
  QMenu *m_windowMenu;
  QMenu *m_registrationMenu;
  QMenu *m_helpMenu;

  QAction *m_openAction;
  QAction *m_saveAction;
  QAction *m_renderAction;
  QAction *m_saveAsAction;
  QAction *m_zoomInAction;       // Added
  QAction *m_zoomOutAction;      // Added
  QAction *m_zoom100Action;      // Added
  QAction *m_zoomFitAction;      // Added

  QAction *m_gamutWarningAction; // Added Gamut Warning toggle
  QAction *m_fullscreenAction;   // Fullscreen toggle
  QAction *m_lockRelativeCoordinatesAction; // Lock relative coords toggle
  QAction *m_optimizeCoordinatesAction; // Optimize coordinates button
  QAction *m_registrationPointsAction; // Registration points toggle
  QAction *m_detectedPatchCentersAction; // Auto-detected patch centers toggle
  QAction *m_panAction;
  QAction *m_selectAction;
  QAction *m_addPointAction;
  QAction *m_setCenterAction;
  QAction *m_selectAllAction;
  QAction *m_deselectAllAction;
  QAction *m_deleteSelectedAction;
  QAction *m_pruneMisplacedAction;
  QAction *m_optimizeGeometryAction;
  QAction *m_autoOptimizeAction;
  QAction *m_optimizeAction;
  QAction *m_nonLinearAction;
  QAction *m_rotateLeftAction;
  QAction *m_rotateRightAction;
  QAction *m_mirrorAction; // Added
  QAction
      *m_colorCheckBoxAction; // Added to control visibility of color checkbox
  QList<QAction*> m_registrationActions; // Track registration group actions for visibility
  QMenu *m_recentFilesMenu;
  enum { MaxRecentFiles = 10 };
  QList<QAction *> m_recentFileActions;
  QList<QAction *> m_modeActions; // 1-0 hotkeys for modes
  QStringList m_recentFiles;

  QMenu *m_recentParamsMenu;
  QList<QAction *> m_recentParamsActions;
  QStringList m_recentParams;

  QSplitter *m_mainSplitter;
  QByteArray m_workspaceSplitterState;
  QPointer<QStatusBar> m_workspaceStatusBar;
  bool m_workspaceEmbedded = false;
  QList<int> m_splitterSizesBeforeFullscreen; // Save splitter state before fullscreen

  // Left side
  ImageWidget *m_imageWidget;

  /** Dynamic routing for the one shared document inspector.
      IMAGE is weak because secondary views are independently closable;
      CONNECTIONS belong exactly to that image; SWITCHING suppresses temporary
      tool cancellation while ownership moves between compatible ordinary views. */
  struct InspectorImageRoutingState {
    QPointer<ImageWidget> image;
    std::vector<QMetaObject::Connection> connections;
    bool switching = false;
  };
  InspectorImageRoutingState m_inspectorImageRouting;

  // Right side
  QWidget *m_rightColumn;
  NavigationView *m_navigationView;
  MultiLineTabWidget *m_configTabs;
  QLabel *m_workflowProcessLabel = nullptr;
  QLabel *m_workflowImageLayerLabel = nullptr;
  QLabel *m_workflowRegistrationLabel = nullptr;
  QLabel *m_workflowCalibrationLabel = nullptr;
  QLabel *m_workflowProfileLabel = nullptr;
  QLabel *m_workflowNextStepLabel = nullptr;
  QPushButton *m_workflowNextStepButton = nullptr;

  QToolBar *m_toolbar;        // New toolbar
  QComboBox *m_modeComboBox;  // Mode selector
  QComboBox *m_coordinateComboBox = nullptr; // Scan/final canvas selector
  QDoubleSpinBox *m_finalRotationSpinBox = nullptr;
  QAction *m_finalRotationLabelAction = nullptr;
  QAction *m_finalRotationSpinAction = nullptr;
  QCheckBox *m_colorCheckBox; // Color checkbox (IR/RGB switch)

  // Core Data
  // We keep shared copies or references.
  // These parameter objects are document-local, so direct members are appropriate.
  QString m_lastOpenDir;
  QString m_lastSaveDir;

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
  /** Session-only provenance for the patch-center map returned by automatic
      screen detection. The map stays meaningful across geometry refinement,
      but not across changes to the scan or detector/color-classification
      inputs that produced it. */
  struct DetectedScreenDiagnosticsState {
    /** Exact subset consumed while building the retained screen_map. */
    struct Inputs {
      colorscreen::scr_type type;
      colorscreen::scanner_type scannerType;
      colorscreen::scr_detect_parameters detect;
      colorscreen::luminosity_t gamma;
      colorscreen::sharpen_parameters sharpen;

      Inputs(colorscreen::scr_type detectedType,
             const ParameterState &state)
          : type(detectedType),
            scannerType(state.scrToImg.scanner_type),
            detect(state.detect),
            gamma(state.rparams.gamma),
            sharpen(state.rparams.sharpen) {}

      bool matches(const ParameterState &state) const {
        return type == state.scrToImg.type &&
               scannerType == state.scrToImg.scanner_type &&
               detect == state.detect &&
               gamma == state.rparams.gamma &&
               sharpen == state.rparams.sharpen;
      }
    };

    std::shared_ptr<const colorscreen::screen_map> map;
    std::optional<Inputs> inputs;
    std::weak_ptr<colorscreen::image_data> scan;
    // View preference survives stale-evidence clearing and is reused when a
    // later current detection publishes another map.
    bool showCenters = false;

    void clearEvidence() {
      map.reset();
      inputs.reset();
      scan.reset();
    }
  };
  DetectedScreenDiagnosticsState m_detectedScreenDiagnostics;

  /** Session-only ownership for the combined Detect screen workflow.
      The confirmation prompt and Wait/Cancel -> Wait/Stop progress handoff
      belong to one logical operation. The weak progress identity never owns
      the task and cannot outlive its worker. */
  struct ScreenAutodetectionState {
    QPointer<QDialog> prompt;
    std::weak_ptr<colorscreen::progress_info> progress;
    bool usesStop = false;

    void clearProgress() {
      progress.reset();
      usesStop = false;
    }
  };
  ScreenAutodetectionState m_screenAutodetection;
  /** Last slanted-edge setup used in this session.  Each accepted measurement
      stores an independent copy of its metadata, while the numerical controls
      determine the generated curve.  */
  colorscreen::slanted_edge_parameters m_slantedEdgeParameters;
  std::vector<colorscreen::point_t> m_profileSpots;

  /** Undo baselines for in-place canvas gestures.
      Each completion consumes exactly one matching start snapshot. */
  struct CanvasGestureUndoState {
    std::optional<ParameterState> pointEdit;
    std::optional<ParameterState> coordinateEdit;
  };
  CanvasGestureUndoState m_canvasGestureUndo;

  colorscreen::render_type_parameters m_renderTypeParams; // New member

  void resetParameters();

  // Progress Reporting
public:
  /** Register ordinary transient background progress. */
  void addProgress(std::shared_ptr<colorscreen::progress_info> info);

  /** Register a long-running task that gets its own status-bar row.

      TITLE is the stable user-facing task name. ACTION controls whether the
      row offers Cancel or Stop; both request cooperative termination through
      the progress_info object, but Stop is used for incremental work whose
      already-produced results remain useful. */
  void addUserVisibleProgress(
      std::shared_ptr<colorscreen::progress_info> info, const QString &title,
      ProgressAction action = ProgressAction::Cancel);

  void removeProgress(std::shared_ptr<colorscreen::progress_info> info);

private slots:
  void onOptimizeCoordinates();
  void onCoordinateSystemChanged();
  void onAutodetectScreen();
  void onFlatFieldRequested();
  void onMirrorHorizontally(bool checked);

  // Helper to update color checkbox state and visibility
  void updateColorCheckBoxState();
  
  // Helpers for registration/tool availability.
  void updateRegistrationGroupVisibility();
  bool screenCoordinateToolAvailable() const;
  void updateScreenCoordinateToolPresentation();

  /** Start full-crop point discovery. SCREENAUTODETECTION pins Workflow
      guidance to the active Detect screen operation instead of provisional
      point/fit state. */
  void startAutomaticPointDiscovery(
      const colorscreen::finetune_area_parameters &params,
      bool screenAutodetection);

  /** Launch one progressive registration-discovery worker over AREA.
      SELECTEDAREA chooses user-facing text; ALLOWREGISTRATIONBOOTSTRAP is used
      only by full-image discovery. */
  void startRegistrationDiscovery(
      const colorscreen::int_image_area &area,
      const colorscreen::finetune_area_parameters &params,
      bool screenAutodetection, bool allowRegistrationBootstrap,
      bool selectedArea);

  /** Return true while GENERATION/PROGRESS own the evolving registration
      request and the live document still equals its expected accepted state. */
  bool registrationDiscoveryRequestCurrent(
      uint64_t generation,
      const std::shared_ptr<colorscreen::progress_info> &progress) const;

  /** Cancel progressive registration discovery after an unrelated document
      edit while preserving batches already accepted into Undo. */
  void cancelStaleRegistrationDiscovery(const ParameterState &currentState);

  /** Publish/clear the progress request currently owning Detect-screen
      guidance. Identity checks make coordinate-to-point handoff race-safe. */
  void setScreenAutodetectionProgress(
      const std::shared_ptr<colorscreen::progress_info> &progress,
      bool usesStop);
  void clearScreenAutodetectionProgress(
      const std::shared_ptr<colorscreen::progress_info> &progress);

private:
  DocumentProgressController m_progressController;
  FileRenderController m_fileRenderController;
  QTimer *m_recoveryTimer;  // Auto-save timer for crash recovery

  /** Return focus from a disappearing long-task row to an image canvas. */
  void releaseUserVisibleProgressFocus(QWidget *row);

  // Undo/Redo
  QUndoStack *m_undoStack;
  void changeParameters(const ParameterState &newState,
                        const QString &description = QString(),
                        const QString &parameterKey = QString());
  ParameterState getCurrentState() const;
  void updateUIFromState(const ParameterState &state);

  // Digital Capture Panel
  CapturePanel *m_capturePanel;
  SharpnessPanel *m_sharpnessPanel;
  ScreenPanel *m_screenPanel;
  GeometryPanel *m_geometryPanel;
  ContactCopyPanel *m_contactCopyPanel;
  ColorPanel *m_colorPanel;
  ProfilePanel *m_profilePanel;
  TilesPanel   *m_tilesPanel = nullptr;
  ImageLayerPanel *m_imageLayerPanel = nullptr;

  // Profile optimizer results are session-only and live in
  // ProfileCalibrationState below; they never enter ParameterState.

  // List of all panels for automated updates
  std::vector<ParameterPanel *> m_panels;

  // Docks
  BacklightChartWidget *m_backlightChart;

  // Current image file path.
  QString m_currentImageFile;

  /** Parameter-file target for this document.
      A suggested path is only a Save-As default and must never be overwritten
      without confirmation as though it had already been loaded/saved. */
  struct ParameterFileState {
    QString path;
    bool suggested = false;

    void setLoaded(const QString &fileName) {
      path = fileName;
      suggested = false;
    }
    void setSuggested(const QString &fileName) {
      path = fileName;
      suggested = true;
    }
  };
  ParameterFileState m_parameterFile;

  /** Session-only ownership for asynchronous image replacement.
      Generation gates stale completions; the optional autodetect handoff names
      the exact reload generation allowed to continue into Detect screen. */
  struct ImageLoadState {
    bool pending = false;
    uint64_t generation = 0;
    std::optional<uint64_t> screenAutodetectAfterGeneration;
  };
  ImageLoadState m_imageLoad;

  bool m_recoveryDirty = false;

  /** Transactional close lifecycle for one logical document.

      File -> Exit may preflight several documents before destroying any of
      them. Keep that one-shot approval mutually exclusive with active teardown
      rather than representing the phases with independent booleans. */
  struct DocumentCloseLifecycleState {
    enum class Phase { Open, ApplicationPreflightApproved, Closing };
    Phase phase = Phase::Open;

    bool closing() const { return phase == Phase::Closing; }
    bool preflightApproved() const {
      return phase == Phase::ApplicationPreflightApproved;
    }
    void approvePreflight() {
      if (!closing())
        phase = Phase::ApplicationPreflightApproved;
    }
    void cancelPreflight() {
      if (preflightApproved())
        phase = Phase::Open;
    }
    bool consumePreflight() {
      if (!preflightApproved())
        return false;
      phase = Phase::Open;
      return true;
    }
    void beginClosing() { phase = Phase::Closing; }
  };
  DocumentCloseLifecycleState m_closeLifecycle;

  /** Exclusive session-only owner of ImageWidget::AddPointMode when that mode
      is temporarily borrowed by Profile calibration or one-area Focus analysis.
      Ordinary registration Add Point is represented by Intent::None. */
  struct PointClickToolState {
    enum class Intent { None, ProfileSpot, FocusAnalysis };

    Intent intent = Intent::None;
    uint64_t focusFlags = 0;

    bool active() const { return intent != Intent::None; }
    bool profileSpot() const { return intent == Intent::ProfileSpot; }
    bool focusAnalysis() const { return intent == Intent::FocusAnalysis; }

    void armProfileSpot() {
      intent = Intent::ProfileSpot;
      focusFlags = 0;
    }
    void armFocusAnalysis(uint64_t requestedFlags) {
      intent = Intent::FocusAnalysis;
      focusFlags = requestedFlags;
    }
    void clear() {
      intent = Intent::None;
      focusFlags = 0;
    }
  };

  /** Session-only ownership for temporary canvas operations.

      All temporary tools restore one persistent canvas mode. Generic Area owns
      at most one callback, while Profile/Focus may borrow AddPointMode through
      POINTCLICK. Keeping these together prevents one temporary operation from
      overwriting or outliving another operation's restoration state. */
  struct TemporaryCanvasToolState {
    ImageWidget::InteractionMode restoreMode = ImageWidget::PanMode;
    std::function<void(QRect)> areaSelectionCallback;
    PointClickToolState pointClick;
  };
  TemporaryCanvasToolState m_temporaryCanvas;

  /** Session-local automatic multi-area focus-analysis state.
      Candidate rectangles, accepted diagnostics, approval prompt and running
      presentation share one lifecycle but never enter ParameterState. */
  struct FocusAreaAnalysisState {
    std::vector<colorscreen::finetune_focus_area_candidate> candidates;
    colorscreen::finetune_focus_analysis_result result;
    QPointer<QMessageBox> prompt;
    // Candidate rectangles and diagnostics are valid only for the render +
    // screen-to-image inputs under which they were produced.
    std::optional<ParameterState> baseline;
    std::weak_ptr<colorscreen::image_data> scan;
    QString statusSummary;
    bool running = false;
  };
  FocusAreaAnalysisState m_focusAreaAnalysis;
  int m_selectedMtfMeasurement = -1;

  /** Session-only provenance for an accepted flat-field calibration.
      The correction itself is saved in ParameterState; freshness depends only
      on the capture gamma/demosaic inputs used to decode the reference files. */
  struct FlatFieldCalibrationState {
    std::optional<colorscreen::luminosity_t> gamma;
    std::optional<colorscreen::image_data::demosaicing_t> demosaic;
    std::weak_ptr<colorscreen::backlight_correction_parameters> correction;
    QString whiteReference;
    QString blackReference;
    std::weak_ptr<colorscreen::progress_info> progress;

    void clearRequest() { progress.reset(); }
    void clearAccepted() {
      gamma.reset();
      demosaic.reset();
      correction.reset();
      whiteReference.clear();
      blackReference.clear();
    }
    void clear() {
      clearRequest();
      clearAccepted();
    }
  };
  FlatFieldCalibrationState m_flatFieldCalibration;

  /** Session-local ownership for progressive adaptive sharpening.
      Unlike a one-shot result this worker intentionally publishes live chart
      cells, so generation, progress identity and immutable inputs must remain
      coupled until completion/cancellation. */
  struct AdaptiveSharpeningState {
    uint64_t generation = 0;
    std::optional<ParameterState> baseline;
    std::shared_ptr<colorscreen::image_data> scan;
    std::weak_ptr<colorscreen::progress_info> progress;
    // Accepted correction provenance is session-only.  Keep a weak scan
    // identity so freshness never pins a replaced source image in memory.
    std::optional<ParameterState> acceptedBaseline;
    std::weak_ptr<colorscreen::image_data> acceptedScan;

    void clearRequest() {
      baseline.reset();
      scan.reset();
      progress.reset();
    }
    void clearAccepted() {
      acceptedBaseline.reset();
      acceptedScan.reset();
    }
    void clear() {
      clearRequest();
      clearAccepted();
    }
  };
  AdaptiveSharpeningState m_adaptiveSharpening;

  /** Session-local ownership for incremental registration discovery.
      EXPECTEDSTATE evolves after each accepted worker-owned point/geometry
      batch. Any other document edit makes the live state diverge and cancels
      the worker before another batch can publish. */
  struct RegistrationDiscoveryState {
    uint64_t generation = 0;
    std::optional<ParameterState> expectedState;
    std::shared_ptr<colorscreen::image_data> scan;
    std::weak_ptr<colorscreen::progress_info> progress;

    void clearRequest() {
      expectedState.reset();
      scan.reset();
      progress.reset();
    }
  };
  RegistrationDiscoveryState m_registrationDiscovery;

  // Crash recovery
  QString m_recoveryDir;
  void saveRecoveryState();
  void clearRecoveryFiles();
  
  // Solver Worker
  GeometrySolverWorker *m_solverWorker;
  QThread *m_solverThread;
  
  // Color Optimizer Worker
  ColorOptimizerWorker *m_colorOptimizerWorker = nullptr;
  QThread *m_colorOptimizerThread = nullptr;
  TaskQueue m_colorOptimizerQueue;
  // Session-local profile-fit provenance. Keep the request snapshots and the
  // accepted-fit diagnostic together so their lifecycle cannot drift apart.
  // The persisted matrix remains usable, but is only called current when an
  // accepted optimizer result matches geometry, color inputs and calibration
  // spots.
  struct ProfileCalibrationState {
    std::optional<ColorOptimizerRequestData> baseline;
    std::weak_ptr<colorscreen::image_data> acceptedScan;
    std::optional<ColorOptimizerRequestData> pendingInputs;
    std::optional<int> pendingRequestId;
    std::optional<ColorOptimizerRequestData> failureInputs;
    std::weak_ptr<colorscreen::image_data> failureScan;
    std::vector<colorscreen::color_match> spotResults;
    double averageDeltaE = -1;

    void clearDiagnostics() {
      spotResults.clear();
      averageDeltaE = -1;
    }

    void clear() {
      baseline.reset();
      acceptedScan.reset();
      pendingInputs.reset();
      pendingRequestId.reset();
      failureInputs.reset();
      failureScan.reset();
      clearDiagnostics();
    }
  };
  ProfileCalibrationState m_profileCalibration;
  // std::shared_ptr<colorscreen::progress_info> m_solverProgress; // Removed, now handled by queue request
  
  // Solver Queue
  TaskQueue m_solverQueue;

  // Final-result one-shot operations share newest-request publication rules.
  OneShotOperationController m_oneShotOperations;

  // Session-local provenance for geometry fitting. The baseline is captured
  // only after an accepted solver result, so loaded/manual geometry is never
  // incorrectly advertised as a current automatic fit. Pending input state is
  // also used as a publication gate when parameters change during a solve.
  struct GeometryFitState {
    std::optional<ParameterState> baseline;
    std::weak_ptr<colorscreen::image_data> acceptedScan;
    std::optional<ParameterState> pendingInputs;
    std::shared_ptr<colorscreen::image_data> pendingScan;
    std::optional<bool> pendingNonlinearEnabled;
    // TaskQueue publishability and provenance cleanup are separate. Track the
    // dispatched request that owns pendingInputs so a cancelled completion can
    // clear its own state without an older completion clearing a newer fit.
    std::optional<int> pendingRequestId;
    std::optional<ParameterState> failureInputs;
    std::weak_ptr<colorscreen::image_data> failureScan;

    void clearRequest() {
      pendingInputs.reset();
      pendingScan.reset();
      pendingNonlinearEnabled.reset();
      pendingRequestId.reset();
    }

    void clearAccepted() {
      baseline.reset();
      acceptedScan.reset();
      failureInputs.reset();
      failureScan.reset();
    }

    void clear() {
      clearRequest();
      clearAccepted();
    }
  };
  GeometryFitState m_geometryFit;

  // Session-local MTF model-fit provenance shared by all Sharpness views.
  // Keep request snapshots, accepted-fit diagnostics and publication identity
  // together so their lifecycle cannot drift apart.
  struct MtfFitState {
    std::optional<colorscreen::mtf_parameters> baseline;
    std::optional<colorscreen::mtf_parameters> pendingInputs;
    std::optional<colorscreen::mtf_parameters> failureInputs;
    double rms = -1;
    bool running = false;
    // Request identity prevents a cancelled old fit from clearing provenance
    // for a newer fit after parameter/image replacement.
    std::weak_ptr<colorscreen::progress_info> progress;

    void clear() {
      baseline.reset();
      pendingInputs.reset();
      failureInputs.reset();
      rms = -1;
      running = false;
      progress.reset();
    }
  };
  MtfFitState m_mtfFit;
  
  // Ad-hoc workers that intentionally publish intermediate results.
  BackgroundThreadRegistry m_backgroundThreads;

  // The workspace smoke probe exercises private one-shot publication rules.
  friend void startWorkspaceChurnSmoke(ColorScreenApplication &app,
                                       std::function<void()> completed);
  // The document lifecycle smoke verifies pooled one-shot cancellation on close.
  friend void startDocumentLifecycleSmoke(ColorScreenApplication &app,
                                          std::function<void()> completed);
  
private slots:
  void onTriggerSolve(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);
};
