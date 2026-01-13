#pragma once

#include <QMainWindow>
#include <memory>
#include <memory>
#include <vector>
#include <functional>
#include <map>
#include <set>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h" // Added
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

class QSplitter;
class QTabWidget;
class QToolBar; // Added
class QComboBox; // Added
class QCheckBox; // Added
class QVBoxLayout; // Added for Linearization tab
class ImageWidget;
class NavigationView;
class QProgressBar;
class QLabel;
class QPushButton;
class QTimer;
#include <QElapsedTimer>
#include "ParameterState.h"
#include "LinearizationPanel.h"
#include "SharpnessPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/solver-parameters.h"

struct ProgressEntry {
    std::shared_ptr<colorscreen::progress_info> info;
    QElapsedTimer startTime;
};

class QUndoStack; // Forward decl

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    
    // Internal use by Undo Command
    void applyState(const ParameterState &state);

private slots:
    void onOpenImage();
    void onImageLoaded(); // Called when image is ready
    void onOpenParameters();
    void onModeChanged(int index); // Slot for mode change
    void rotateLeft();
    void rotateRight();
    void onColorCheckBoxChanged(bool checked); // Slot for color checkbox
    
    // Recent Files
    void openRecentFile();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void createMenus();
    void createToolbar(); // New helper
    void updateModeMenu(); // Updates combo box items
    
    // Window state management
    void saveWindowState();
    void restoreWindowState();
    
    // Recent Files
    void updateRecentFileActions();
    void addToRecentFiles(const QString &filePath);
    void loadRecentFiles();
    void saveRecentFiles();
    void loadFile(const QString &fileName);
    
    QMenu *m_recentFilesMenu;
    enum { MaxRecentFiles = 10 };
    QList<QAction*> m_recentFileActions;
    QStringList m_recentFiles;

    QSplitter *m_mainSplitter;
    
    // Left side
    ImageWidget *m_imageWidget;

    // Right side
    QWidget *m_rightColumn;
    NavigationView *m_navigationView;
    QTabWidget *m_configTabs;

    QToolBar *m_toolbar; // New toolbar
    QComboBox *m_modeComboBox; // Mode selector
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

private:
    // Status Bar Widgets
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPushButton *m_cancelButton;
    QWidget *m_progressContainer; // Container for the above

    QTimer *m_progressTimer;
    std::vector<ProgressEntry> m_activeProgresses;
    
    // Helper to find the longest running task
    ProgressEntry* getLongestRunningTask();
    
    // Undo/Redo
    QUndoStack *m_undoStack;
    void changeParameters(const ParameterState &newState);
    ParameterState getCurrentState() const;
    void updateUIFromState(const ParameterState &state);
    
    // Linearization Panel
    LinearizationPanel *m_linearizationPanel;
    SharpnessPanel *m_sharpnessPanel;
};
