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
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <memory>
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
class QPushButton;
class QWidget;
#include "ImageWidget.h"

class NavigationView;
class QTimer;
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
#include "BacklightChartWidget.h"
#include <QElapsedTimer>

/** Action offered for a user-visible long-running progress task. */
enum class ProgressAction { Cancel, Stop };

/** One registered background operation and its optional dedicated status row. */
struct ProgressEntry {
  std::shared_ptr<colorscreen::progress_info> info;
  QElapsedTimer startTime;
  bool userVisible = false;
  ProgressAction action = ProgressAction::Cancel;
  QString title;
  QWidget *row = nullptr;
  QLabel *rowLabel = nullptr;
  QProgressBar *rowProgressBar = nullptr;
  QPushButton *rowActionButton = nullptr;
};

class ScreenPanel;
class GeometryPanel;
class GeometrySolverWorker;
class ColorOptimizerWorker;
class AdaptiveSharpeningWorker;
class CoordinateOptimizationWorker;
class AdaptiveSharpeningChart; // Added
class QUndoStack; // Forward decl

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

  /** Share the loaded scan with additional views of this document. */
  std::shared_ptr<colorscreen::image_data> sharedImageData() const {
    return m_scan;
  }

  /** Return a copy of the shared document parameters for a secondary view. */
  ParameterState documentStateSnapshot() const;

  /** Apply an undoable shared parameter update originating in another view. */
  void applySharedDocumentState(const ParameterState &state,
                                const QString &description);

  /** Return this view's render-mode settings as the initial state for a new
      secondary view. */
  colorscreen::render_type_parameters viewRenderTypeParameters() const {
    return m_renderTypeParams;
  }

  /** Add the document-owned canvas actions to an ordinary secondary toolbar.
      These actions route through inspectorImageWidget(), so only the active
      ordinary presentation executes them. */
  void appendOrdinaryViewToolActions(QToolBar *toolbar);

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
    return m_inspectorImageWidget ? m_inspectorImageWidget.data() : m_imageWidget;
  }

  /** Return the document's primary image view. */
  ImageWidget *primaryImageWidget() const { return m_imageWidget; }

  /** Return this document's transient progress presentation. Attached
      workspaces host it globally regardless of the selected tab. */
  QWidget *workspaceStatusWidget() const { return m_progressContainer; }

  /** Remove/restore transient progress from this document's private bar. */
  QWidget *takeWorkspaceStatusWidget();
  void restoreWorkspaceStatusWidget();

  /** Return whether transient progress has passed the display delay. */
  bool hasVisibleTransientProgress() const {
    return m_transientProgressVisible;
  }

  /** Return this document's persistent user-visible progress rows.

      Attached documents keep this widget in the workspace global status area
      even while another image is active. */
  QWidget *workspaceUserVisibleStatusWidget() const {
    return m_userVisibleProgressContainer;
  }

  /** Return the local frameless task-progress dock used by detached views. */
  QDockWidget *userVisibleProgressDock() const { return m_userVisibleProgressDock; }

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
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::solver_parameters solver;
    bool computeMesh;
  };

  struct ColorOptimizerRequestData {
    colorscreen::scr_to_img_parameters scrParams;
    colorscreen::render_parameters     rparams;
    std::vector<colorscreen::point_t>  spots;
  };

signals:
  /** Emitted after the loaded image or shared document parameters change.
      Secondary views refresh from this signal while keeping render mode, zoom,
      and pan view-local. */
  void documentStateChanged();
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
  void onFinetuneFinished(bool success, std::vector<colorscreen::solver_parameters::solver_point_t> points,
                          QThread *thread, std::shared_ptr<colorscreen::progress_info> progress);
  void onSetCenter(colorscreen::point_t imgPos);
  void onPointManipulationStarted();
  void onCoordinateSystemManipulationStarted();
  void onCoordinateSystemManipulationFinished();
  void updateRegistrationActions();
  void maybeTriggerAutoSolver();
  void onFocusAnalysisRequested(bool checked, uint64_t flags);
  void onFocusAnalysisFinished(bool success, colorscreen::finetune_result result);
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
  void onAutodetectCoordinatesFinished(int reqId, colorscreen::scr_to_img_parameters result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);
  void onOptimizeCoordinatesFinished(int reqId, colorscreen::finetune_result result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);
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

  /** Save parameters to FILEName and update the document's save metadata. */
  bool saveParametersToFile(const QString &fileName);

  /** Prompt for a parameter filename and save synchronously. */
  bool saveParametersAs();

  /** Return whether undo state or recovered state differs from the last save. */
  bool isDocumentModified() const;

  /** Reload the current image using the selected demosaic mode without
      prompting for parameter data.  Existing unsaved parameter state remains
      marked dirty across the asynchronous reload. */
  void reloadCurrentImageWithDemosaic();

  /** Offer conservative post-load setup guidance when ANALYSIS says the
      normally demosaiced RAW is likely an achromatic Bayer capture. */
  void maybeOfferInitialSetupGuide(
      const colorscreen::monochrome_bayer_analysis &analysis);

  void setupUi();
  void createMenus();
  QRect getImageArea(QRect area, ImageWidget *imageWidget = nullptr);
  void pivotViewport(int oldRot, int newRot);
  void createToolbar();  // New helper
  void updateCoordinateSpaceControls();
  void createModeShortcuts(); // Create 1-0 hotkeys for modes
  void updateModeMenu(); // Updates combo box items
  QIcon renderScreenIcon(colorscreen::scr_type type);

  /** Launch an area-based parameter computation.
      Shows MESSAGE in the status bar, enters area selection mode, and when
      the user draws a rectangle, runs WORKER in a background thread.
      WORKER modifies a ParameterState in-place; the result is pushed as an
      undoable change with DESCRIPTION.
      ON_START is called before launching (to disable UI), ON_DONE after
      completion (to uncheck toggle buttons).  */
  void runAreaComputation(
      const QString &message,
      const QString &description,
      std::function<void()> onStart,
      std::function<void()> onDone,
      std::function<void(ParameterState &, colorscreen::image_data &,
                         const colorscreen::int_image_area &,
                         colorscreen::progress_info *)> worker);

  /** Load parameters from a .par file and update all UI.
      Resets params to defaults before loading (load_csp merges).
      Returns true on success.  */
  bool loadParameterFile(const QString &fileName);

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

  /** Synchronize shared toolbar/menu tool state with the active ordinary view. */
  void syncInspectorInteractionActions(ImageWidget::InteractionMode mode);

  /** Synchronize coordinate-dependent shared actions with the active view. */
  void syncInspectorViewActions();

  /** Return true when IMAGEWIDGET presents this document's current scan. */
  bool acceptsInspectorImageWidget(ImageWidget *imageWidget) const;

  void updateWindowTitle(); // Helper to update window title
  /** Refresh focus-analysis rectangles in the ordinary view currently
      presenting this document's inspector. */
  void updateFocusAreaOverlays();
  /** Clear transient automatic focus-area state for this document. */
  void clearFocusAreaAnalysis();


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
  QPointer<ImageWidget> m_inspectorImageWidget;
  std::vector<QMetaObject::Connection> m_inspectorImageConnections;

  // Right side
  QWidget *m_rightColumn;
  NavigationView *m_navigationView;
  MultiLineTabWidget *m_configTabs;

  QToolBar *m_toolbar;        // New toolbar
  QComboBox *m_modeComboBox;  // Mode selector
  QComboBox *m_coordinateComboBox = nullptr; // Scan/final canvas selector
  QDoubleSpinBox *m_finalRotationSpinBox = nullptr;
  QAction *m_finalRotationLabelAction = nullptr;
  QAction *m_finalRotationSpinAction = nullptr;
  QCheckBox *m_colorCheckBox; // Color checkbox (IR/RGB switch)

  // Core Data
  // We keep shared copies or references.
  // Using std::shared_ptr or just direct members.
  // Given the library usage in gtkgui, direct members are fine.
  QString m_lastOpenDir;
  QString m_lastSaveDir;

  std::function<void(QRect)> m_areaSelectionCallback = nullptr;
  ImageWidget::InteractionMode m_previousInteractionMode = ImageWidget::PanMode;
  bool m_autoAddPointsAfterCoordinates = false;
  bool m_switchingInspectorImage = false;


  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
  /** Last slanted-edge setup used in this session.  Each accepted measurement
      stores an independent copy of its metadata, while the numerical controls
      determine the generated curve.  */
  colorscreen::slanted_edge_parameters m_slantedEdgeParameters;
  std::vector<colorscreen::point_t> m_profileSpots;
  ParameterState m_undoSnapshot; // Added
  ParameterState m_gridManipulationOldState;
  colorscreen::render_type_parameters m_renderTypeParams; // New member

  // Copies for change detection
  colorscreen::scr_detect_parameters m_prevDetectParams;
  colorscreen::scr_to_img_parameters m_prevScrToImgParams;

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
  void onProgressTimer();
  void onCancelClicked();
  void onPrevProgress();
  void onNextProgress();
  void onOptimizeCoordinates();
  void onCoordinateSystemChanged();
  void onAutodetectScreen();
  void onFlatFieldRequested();
  void onDetectScreenFinished(bool success, colorscreen::detected_screen result, colorscreen::solver_parameters solverParams);
  void onFlatFieldFinished(bool success, std::shared_ptr<colorscreen::backlight_correction_parameters> result);
  void onMirrorHorizontally(bool checked);

  // Helper to update color checkbox state and visibility
  void updateColorCheckBoxState();
  
  // Helper to update registration group visibility
  void updateRegistrationGroupVisibility();

private:
  // Status Bar Widgets
  QProgressBar *m_progressBar;
  QLabel *m_statusLabel;
  QPushButton *m_cancelButton;
  QWidget *m_progressContainer; // Outer progress area for a detached document
  QVBoxLayout *m_progressLayout = nullptr;
  QWidget *m_userVisibleProgressContainer = nullptr;
  QVBoxLayout *m_userVisibleProgressLayout = nullptr;
  QDockWidget *m_userVisibleProgressDock = nullptr;
  QWidget *m_transientProgressRow = nullptr;
  bool m_transientProgressVisible = false;

  // Progress switcher UI (for multiple transient progresses)
  QLabel *m_progressCountLabel;
  QPushButton *m_prevProgressButton;
  QPushButton *m_nextProgressButton;


  QTimer *m_progressTimer;
  QTimer *m_recoveryTimer;  // Auto-save timer for crash recovery
  std::vector<ProgressEntry> m_activeProgresses;
  std::shared_ptr<colorscreen::progress_info>
      m_currentlyDisplayedProgress;    // Track displayed progress for cancel
                                       // button
  int m_manuallySelectedProgressIndex; // -1 = auto-select, >= 0 = manual
                                       // selection
  // Tracks the active render progress so we can confirm before cancelling
  std::weak_ptr<colorscreen::progress_info> m_renderProgress;

  /** Register INFO with either transient or dedicated-row presentation. */
  void registerProgress(std::shared_ptr<colorscreen::progress_info> info,
                        bool userVisible, const QString &title,
                        ProgressAction action);

  /** Return currently registered transient progress entries. */
  std::vector<ProgressEntry *> transientProgresses();

  /** Find the longest running transient task. */
  ProgressEntry *getLongestRunningTask();

  /** Update LABEL and BAR from ENTRY's nested progress state. */
  void updateProgressWidgets(const ProgressEntry &entry, QLabel *label,
                             QProgressBar *bar, const QString &title);

  /** Request cooperative cancellation/stopping of INFO. */
  void requestProgressTermination(
      const std::shared_ptr<colorscreen::progress_info> &info,
      ProgressAction action);

  /** Return focus from a disappearing long-task row to an image canvas. */
  void releaseUserVisibleProgressFocus(QWidget *row);

  /** Set delayed transient visibility and notify the workspace. */
  void setTransientProgressVisible(bool visible);

  /** Synchronize visibility of the outer progress container. */
  void updateProgressContainerVisibility();

  // Undo/Redo
  QUndoStack *m_undoStack;
  void changeParameters(const ParameterState &newState, const QString &description = QString());
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

  // Color optimizer results (kept outside ParameterState — not undo-able)
  std::vector<colorscreen::color_match> m_profileSpotResults;
  bool m_addingProfileSpot = false;

  // List of all panels for automated updates
  std::vector<ParameterPanel *> m_panels;

  // Docks
  BacklightChartWidget *m_backlightChart;

  // Current parameters file path
  QString m_currentImageFile;
  QString m_currentParamsFile;
  bool m_currentParamsFileIsWeak = false; // true if filename is suggested, not loaded
  bool m_imageLoadPending = false;
  bool m_recoveryDirty = false;
  bool m_closing = false;
  bool m_focusAnalysisPending = false;
  uint64_t m_focusAnalysisFlags = 0;
  std::vector<colorscreen::finetune_focus_area_candidate> m_focusAreaCandidates;
  colorscreen::finetune_focus_analysis_result m_focusAreaAnalysisResult;
  bool m_focusAreaAnalysisRunning = false;

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
  // std::shared_ptr<colorscreen::progress_info> m_solverProgress; // Removed, now handled by queue request
  
  // Detect Screen Worker
  QThread *m_detectScreenThread = nullptr;
  QThread *m_flatFieldThread = nullptr;
  std::shared_ptr<colorscreen::mesh> m_detectedMesh; // Store mesh from autodetection
  
  // Solver Queue
  TaskQueue m_solverQueue;
  
  // Coordinate Optimization Worker
  CoordinateOptimizationWorker *m_coordOptimizationWorker = nullptr;
  QThread *m_coordOptimizationThread = nullptr;

  // Finetune threads (allow multiple concurrent)
  std::vector<QThread*> m_finetuneThreads;
  
private slots:
  void onTriggerSolve(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);
};
