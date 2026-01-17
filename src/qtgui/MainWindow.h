#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
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
#include "ColorPanel.h"
#include "LinearizationPanel.h"
#include "ParameterState.h"
#include "SharpnessPanel.h"
#include <QElapsedTimer>

struct ProgressEntry {
  std::shared_ptr<colorscreen::progress_info> info;
  QElapsedTimer startTime;
};

class ScreenPanel;
class QUndoStack; // Forward decl

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  // Internal use by Undo Command
  void applyState(const ParameterState &state);

  // Public method for loading files (CLI support)
  void loadFile(const QString &fileName);

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
  void onModeChanged(int index); // Slot for mode change
  void rotateLeft();
  void rotateRight();
  void onGamutWarningToggled(bool checked);
  void onColorCheckBoxChanged(bool checked); // Slot for color checkbox

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
  void createToolbar();  // New helper
  void updateModeMenu(); // Updates combo box items

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
  QMenu *m_helpMenu;

  QAction *m_openAction;
  QAction *m_saveAction;
  QAction *m_saveAsAction;
  QAction *m_zoomInAction;       // Added
  QAction *m_zoomOutAction;      // Added
  QAction *m_zoom100Action;      // Added
  QAction *m_zoomFitAction;      // Added
  QAction *m_rotateLeftAction;   // Added (to be explicitly exposed)
  QAction *m_rotateRightAction;  // Added (to be explicitly exposed)
  QAction *m_gamutWarningAction; // Added Gamut Warning toggle
  QAction
      *m_colorCheckBoxAction; // Added to control visibility of color checkbox
  QMenu *m_recentFilesMenu;
  enum { MaxRecentFiles = 10 };
  QList<QAction *> m_recentFileActions;
  QStringList m_recentFiles;

  QMenu *m_recentParamsMenu;
  QList<QAction *> m_recentParamsActions;
  QStringList m_recentParams;

  QSplitter *m_mainSplitter;

  // Left side
  ImageWidget *m_imageWidget;

  // Right side
  QWidget *m_rightColumn;
  NavigationView *m_navigationView;
  QTabWidget *m_configTabs;

  QToolBar *m_toolbar;        // New toolbar
  QComboBox *m_modeComboBox;  // Mode selector
  QCheckBox *m_colorCheckBox; // Color checkbox (IR/RGB switch)

  // Core Data
  // We keep shared copies or references.
  // Using std::shared_ptr or just direct members.
  // Given the library usage in gtkgui, direct members are fine.
  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
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

  // Helper to update color checkbox state and visibility
  void updateColorCheckBoxState();

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

  QTimer *m_progressTimer;
  QTimer *m_recoveryTimer;  // Auto-save timer for crash recovery
  std::vector<ProgressEntry> m_activeProgresses;
  std::shared_ptr<colorscreen::progress_info>
      m_currentlyDisplayedProgress;    // Track displayed progress for cancel
                                       // button
  int m_manuallySelectedProgressIndex; // -1 = auto-select, >= 0 = manual
                                       // selection

  // Helper to find the longest running task
  ProgressEntry *getLongestRunningTask();

  // Undo/Redo
  QUndoStack *m_undoStack;
  void changeParameters(const ParameterState &newState);
  ParameterState getCurrentState() const;
  void updateUIFromState(const ParameterState &state);

  // Linearization Panel
  // Linearization Panel
  // Linearization Panel
  LinearizationPanel *m_linearizationPanel;
  SharpnessPanel *m_sharpnessPanel;
  ScreenPanel *m_screenPanel;
  ColorPanel *m_colorPanel;

  // List of all panels for automated updates
  std::vector<ParameterPanel *> m_panels;

  // Docks
  QDockWidget *m_mtfDock;
  QDockWidget *m_spectraDock;
  QDockWidget *m_tilesDock;
  QDockWidget *m_colorTilesDock;
  QDockWidget *m_correctedColorTilesDock;
  QDockWidget *m_screenPreviewDock;

  // Current parameters file path
  QString m_currentParamsFile;

  // Crash recovery
  QString m_recoveryDir;
  void saveRecoveryState();
  void loadRecoveryState();
  void clearRecoveryFiles();
  bool hasRecoveryFiles();
};
