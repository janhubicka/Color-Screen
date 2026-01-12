#include "MainWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include "ImageWidget.h"
#include "NavigationView.h"
#include "../libcolorscreen/include/render-parameters.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    resize(1200, 800);
    
    // Progress Timer
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(100);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::onProgressTimer);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    createMenus();

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(m_mainSplitter);

    // Left: Image Widget
    m_imageWidget = new ImageWidget(this);
    m_mainSplitter->addWidget(m_imageWidget);
    
    // Connect ImageWidget progress signals
    connect(m_imageWidget, &ImageWidget::progressStarted, this, &MainWindow::addProgress);
    connect(m_imageWidget, &ImageWidget::progressFinished, this, &MainWindow::removeProgress);

    // Right: Column
    m_rightColumn = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    
    QSplitter *rightSplitter = new QSplitter(Qt::Vertical, m_rightColumn);
    rightLayout->addWidget(rightSplitter);

    // Top Right: Navigation View
    m_navigationView = new NavigationView(this);
    m_navigationView->setMinimumHeight(200);
    rightSplitter->addWidget(m_navigationView);

    // Bottom Right: Tabs
    m_configTabs = new QTabWidget(this);
    QWidget *placeholder = new QWidget();
    m_configTabs->addTab(placeholder, "Parameters");
    rightSplitter->addWidget(m_configTabs);

    m_mainSplitter->addWidget(m_rightColumn);
    
    // Set initial sizes (approx 75% for image)
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);
    
    // Status Bar
    QStatusBar *statusBar = new QStatusBar(this);
    setStatusBar(statusBar);
    
    // Progress Container
    m_progressContainer = new QWidget(this);
    QHBoxLayout *progressLayout = new QHBoxLayout(m_progressContainer);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    
    m_progressBar = new QProgressBar(m_progressContainer);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(true);
    
    m_statusLabel = new QLabel(m_progressContainer);
    
    m_cancelButton = new QPushButton("Cancel", m_progressContainer);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    
    progressLayout->addWidget(m_statusLabel);
    progressLayout->addWidget(m_progressBar);
    progressLayout->addWidget(m_cancelButton);
    
    statusBar->addPermanentWidget(m_progressContainer);
    m_progressContainer->setVisible(false); // Hidden by default
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QAction *openAction = fileMenu->addAction("&Open Image...");
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenImage);
    
    QAction *openParamsAction = fileMenu->addAction("Open &Parameters...");
    connect(openParamsAction, &QAction::triggered, this, &MainWindow::onOpenParameters);
    
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::onOpenParameters()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Parameters", QString(), 
        "Parameters (*.par);;All Files (*)");
    if (fileName.isEmpty()) return;

    FILE *f = fopen(fileName.toUtf8().constData(), "r");
    if (!f) {
        QMessageBox::critical(this, "Error", "Could not open file.");
        return;
    }

    const char *error = nullptr;
    
    // Store previous state for comparison
    colorscreen::scr_to_img_parameters oldScrToImg = m_scrToImgParams;
    colorscreen::scr_detect_parameters oldDetect = m_detectParams;
    // render_parameters comparison? 
    // The user said: "Pass scr_to_img_parameters and scr_detect_parameters to renderers. They can be compared by ==."
    
    if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams, &error)) {
        fclose(f);
        QString errStr = error ? QString::fromUtf8(error) : "Unknown error loading parameters.";
        QMessageBox::critical(this, "Error Loading Parameters", errStr);
        return;
    }
    fclose(f);

    // Update Renderer if needed
    // Check if parameters changed
    bool changed = false;
    if (!(oldScrToImg == m_scrToImgParams)) changed = true;
    if (!(oldDetect == m_detectParams)) changed = true;
    // We should also check m_rparams but user emphasized the others.
    // Let's assume ANY successful load might warrant a refresh or update.
    // But specific comparison requested for re-render.
    
    if (changed) {
         if (m_scan) {
             // Re-set image to re-create renderer with new params
             m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
         }
    } else {
        // Just update params... actually setImage logic uses values.
        // If we want to support run-time tuning without re-creating renderer, we'd need setters in Renderer.
        // But for Open Parameters (file load), re-creating renderer is fine.
        // Even if not changed, we might want to update rparams?
        // Let's just update if we have a scan.
         if (m_scan) {
             m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
         }
    }
}

void MainWindow::onOpenImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), 
        "Images (*.tif *.tiff *.dng *.png *.jpg *.jpeg);;All Files (*)");
    if (fileName.isEmpty()) return;
    
    // Clear current image and stop rendering
    m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr);

    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task("Loading image", 0);
    addProgress(progress);
    
    // Use QtConcurrent to load in background
    // We capture pointers, but we must ensure safety. 
    // Loading loads into m_scan. m_scan is in MainWindow. 
    // We must ensure UI doesn't access m_scan while loading.
    // Ideally use a temporary image_data and swap it in.
    
    std::shared_ptr<colorscreen::image_data> tempScan = std::make_shared<colorscreen::image_data>();
    
    // Return status and error message
    QFutureWatcher<std::pair<bool, QString>> *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this, watcher, tempScan, progress, fileName]() {
        std::pair<bool, QString> result = watcher->result();
        removeProgress(progress);
        watcher->deleteLater();
        
        if (result.first) {
             // m_scan is now a shared_ptr, we can just assign the pointer
             m_scan = tempScan;
             
             // Initialize default parameters akin to gtkgui
            if ((int)m_scan->gamma != -2 && m_scan->gamma > 0)
                m_rparams.gamma = m_scan->gamma;
            else
                m_rparams.gamma = -1; 

            // Update Widgets
            m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
            onImageLoaded();
        } else {
             // Error handling
             // Check if cancelled
             if (progress->cancelled()) {
                 // User cancelled, do nothing (maybe show status?)
             } else {
                 QMessageBox::critical(this, "Error Loading Image", 
                    result.second.isEmpty() ? "Failed to load image." : result.second);
             }
        }
    });
    
    QFuture<std::pair<bool, QString>> future = QtConcurrent::run([tempScan, fileName, progress]() {
        const char *error = nullptr;
        bool res = tempScan->load(fileName.toUtf8().constData(), true, &error, progress.get());
        QString errStr;
        if (!res && error) {
            errStr = QString::fromUtf8(error);
        }
        return std::make_pair(res, errStr);
    });
    
    watcher->setFuture(future);
}

void MainWindow::addProgress(std::shared_ptr<colorscreen::progress_info> info)
{
    ProgressEntry entry;
    entry.info = info;
    entry.startTime.start();
    m_activeProgresses.push_back(entry);
    
    if (!m_progressTimer->isActive()) {
        m_progressTimer->start();
    }
}

void MainWindow::removeProgress(std::shared_ptr<colorscreen::progress_info> info)
{
    for (auto it = m_activeProgresses.begin(); it != m_activeProgresses.end(); ++it) {
        if (it->info == info) {
            m_activeProgresses.erase(it);
            break;
        }
    }
    
    if (m_activeProgresses.empty()) {
        m_progressTimer->stop();
        m_progressContainer->setVisible(false);
    }
}

ProgressEntry* MainWindow::getLongestRunningTask()
{
   ProgressEntry* maxEntry = nullptr;
   qint64 maxTime = -1;
   
   for (auto &entry : m_activeProgresses) {
       if (entry.startTime.elapsed() > maxTime) {
           maxTime = entry.startTime.elapsed();
           maxEntry = &entry;
       }
   }
   return maxEntry;
}

void MainWindow::onProgressTimer()
{
    ProgressEntry* task = getLongestRunningTask();
    if (!task) return;
    
    if (task->startTime.elapsed() > 300) {
        if (!m_progressContainer->isVisible()) {
            m_progressContainer->setVisible(true);
        }
        
        const char *tName = "";
        float percent = 0;
        task->info->get_status(&tName, &percent);
        
        m_statusLabel->setText(QString::fromUtf8(tName));
        if (percent >= 0) {
            m_progressBar->setValue((int)percent);
        } else {
            m_progressBar->setValue(0);
        }
    }
}

void MainWindow::onCancelClicked()
{
    ProgressEntry* task = getLongestRunningTask();
    if (task && task->info) {
        task->info->cancel();
    }
}


void MainWindow::onImageLoaded()
{
   // TODO: Update UI components that depend on loaded image
}

