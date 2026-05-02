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
class ImageWidget;
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
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  // Internal use by Undo Command
  void applyState(const ParameterState &state);

  // Public method for loading files (CLI support)
  void loadFile(const QString &fileName, bool suppressParamPrompt = false);

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
  void onAdaptiveSharpeningRequested(int xsteps);
  void onAdaptiveSharpeningFinished(bool success, std::shared_ptr<colorscreen::scanner_blur_correction_parameters> result);
  void onAutomaticallyAddPointsRequested(const colorscreen::finetune_area_parameters &params);
  void onAutomaticallyAddPointsInAreaRequested(const colorscreen::finetune_area_parameters &params);
  void onAutodetectCoordinatesRequested();
  void onAlternateColorsRequested();
  void onOptimizeCoordinatesRequested();
  void onAutodetectCoordinatesFinished(int reqId, colorscreen::scr_to_img_parameters result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);
  void onOptimizeCoordinatesFinished(int reqId, colorscreen::finetune_result result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);

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

  void setupUi();
  void createMenus();
  QRect getImageArea(QRect area);
  void pivotViewport(int oldRot, int newRot);
  void createToolbar();  // New helper
  void createModeShortcuts(); // Create 1-0 hotkeys for modes
  void updateModeMenu(); // Updates combo box items
  QIcon renderScreenIcon(colorscreen::scr_type type);
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

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
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
  bool m_focusAnalysisPending = false;
  uint64_t m_focusAnalysisFlags = 0;

  // Crash recovery
  QString m_recoveryDir;
  void saveRecoveryState();
  void loadRecoveryState();
  void clearRecoveryFiles();
  bool hasRecoveryFiles();
  
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
