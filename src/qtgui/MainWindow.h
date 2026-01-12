#pragma once

#include <QMainWindow>
#include <memory>
#include <vector>
#include <QElapsedTimer>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/progress-info.h"

class QSplitter;
class QTabWidget;
class ImageWidget;
class NavigationView;
class QProgressBar;
class QLabel;
class QPushButton;
class QTimer;

struct ProgressEntry {
    std::shared_ptr<colorscreen::progress_info> info;
    QElapsedTimer startTime;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOpenImage();
    void onImageLoaded(); // Called when image is ready
    void onOpenParameters();

private:
    void setupUi();
    void createMenus();

    QSplitter *m_mainSplitter;
    
    // Left side
    ImageWidget *m_imageWidget;

    // Right side
    QWidget *m_rightColumn;
    NavigationView *m_navigationView;
    QTabWidget *m_configTabs;

    // Core Data
    // We keep shared copies or references. 
    // Using std::shared_ptr or just direct members.
    // Given the library usage in gtkgui, direct members are fine.
    std::shared_ptr<colorscreen::image_data> m_scan;
    colorscreen::render_parameters m_rparams;
    colorscreen::scr_detect_parameters m_detectParams;
    colorscreen::scr_to_img_parameters m_scrToImgParams;
    colorscreen::solver_parameters m_solverParams;
    
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
};
