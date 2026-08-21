#pragma once

#include "MultiLineTabWidget.h"

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/render-type-parameters.h" // Added
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class QSplitter;
class QTabWidget;
class QDockWidget; // Added
class QToolBar;    // Added
class QComboBox;   // Added
class QCheckBox;   // Added
class QVBoxLayout; // Added for Linearization tab
#include "ImageWidget.h"

class NavigationView;
class QProgressBar;
class QLabel;
class QPushButton;
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

struct ProgressEntry {
  std::shared_ptr<colorscreen::progress_info> info;
  QElapsedTimer startTime;
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

  /** Restore this document from its per-window recovery directory. */
  bool restoreRecoveryState();

  /** Rebuild this window's Window menu from the application document list. */
  void refreshWindowMenu();

  /** Return the document-specific toolbar while this document is attached to
      the shared workspace. */
  QToolBar *workspaceToolBar() const { return m_toolbar; }

  /** Return the navigation/parameter panel column hosted by the workspace. */
  QWidget *workspaceInspectorWidget() const { return m_rightColumn; }

  /** Return the per-document progress controls shown in the workspace status
      bar while this document is active. */
  QWidget *workspaceStatusWidget() const { return m_progressContainer; }

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

private:
  // Helper to check for unsaved changes and prompt to save
  bool maybeSave();

  /** Save parameters to FILEName and update the document's save metadata. */
  bool saveParametersToFile(const QString &fileName);

  /** Prompt for a parameter filename and save synchronously. */
  bool saveParametersAs();

  /** Return whether undo state or recovered state differs from the last save. */
  bool isDocumentModified() const;

  void setupUi();
  void createMenus();
  QRect getImageArea(QRect area);
  void pivotViewport(int oldRot, int newRot);
  void createToolbar();  // New helper
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

  void updateWindowTitle(); // Helper to update window title


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
  bool m_workspaceEmbedded = false;
  QList<int> m_splitterSizesBeforeFullscreen; // Save splitter state before fullscreen

  // Left side
  ImageWidget *m_imageWidget;

  // Right side
  QWidget *m_rightColumn;
  NavigationView *m_navigationView;
  MultiLineTabWidget *m_configTabs;

  QToolBar *m_toolbar;        // New toolbar
  QComboBox *m_modeComboBox;  // Mode selector
  QCheckBox *m_colorCheckBox; // Color checkbox (IR/RGB switch)

  // Core Data
  // We keep shared copies or references.
  // Using std::shared_ptr or just direct members.
  // Given the library usage in gtkgui, direct members are fine.
  QString m_lastOpenDir;
  QString m_lastSaveDir;

  std::function<void(QRect)> m_areaSelectionCallback = nullptr;
  ImageWidget::InteractionMode m_previousInteractionMode = ImageWidget::PanMode;


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
  void addProgress(std::shared_ptr<colorscreen::progress_info> info);
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
  QWidget *m_progressContainer; // Container for the above

  // Progress switcher UI (for multiple progresses)
  QLabel *m_progressCountLabel;
  QPushButton *m_prevProgressButton;
  QPushButton *m_nextProgressButton;

  QDockWidget *m_sharpnessFinetuneImagesDock = nullptr;
  AdaptiveSharpeningChart *m_adaptiveSharpeningChart = nullptr;
  QDockWidget *m_adaptiveSharpeningDock = nullptr;

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

  // Helper to find the longest running task
  ProgressEntry *getLongestRunningTask();

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
  QDockWidget *m_mtfDock;
  QDockWidget *m_dotSpreadDock;
  QDockWidget *m_spectraDock;
  QDockWidget *m_tilesDock;
  QDockWidget *m_colorTilesDock;
  QDockWidget *m_correctedColorTilesDock;
  QDockWidget *m_screenPreviewDock;
  QDockWidget *m_deformationDock;
  QDockWidget *m_lensDock;
  QDockWidget *m_perspectiveDock;
  QDockWidget *m_nonlinearDock;
  QDockWidget *m_backlightDock;
  BacklightChartWidget *m_backlightChart;
  QDockWidget *m_finetuneImagesDock; // Finetune diagnostic images dock (Geometry)
  QDockWidget *m_gamutDock; // Gamut visualization dock
  QDockWidget *m_hdCurveDock; // Added
  QDockWidget *m_toneCurveDock; // Added
  QDockWidget *m_correctedGamutDock; // Corrected gamut visualization dock

  // Current parameters file path
  QString m_currentImageFile;
  QString m_currentParamsFile;
  bool m_currentParamsFileIsWeak = false; // true if filename is suggested, not loaded
  bool m_imageLoadPending = false;
  bool m_recoveryDirty = false;
  bool m_closing = false;
  bool m_focusAnalysisPending = false;
  uint64_t m_focusAnalysisFlags = 0;

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
