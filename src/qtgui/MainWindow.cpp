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
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::onOpenImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), 
        "Images (*.tif *.tiff *.dng *.png *.jpg *.jpeg);;All Files (*)");
    if (fileName.isEmpty()) return;

    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task("Loading image", 0);
    addProgress(progress);
    
    // Use QtConcurrent to load in background
    // We capture pointers, but we must ensure safety. 
    // Loading loads into m_scan. m_scan is in MainWindow. 
    // We must ensure UI doesn't access m_scan while loading.
    // Ideally use a temporary image_data and swap it in.
    
    std::shared_ptr<colorscreen::image_data> tempScan = std::make_shared<colorscreen::image_data>();
    
    QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, tempScan, progress, fileName]() {
        bool result = watcher->result();
        removeProgress(progress);
        watcher->deleteLater();
        
        if (result) {
             // m_scan is now a shared_ptr, we can just assign the pointer
             m_scan = tempScan;
             
             // Initialize default parameters akin to gtkgui
            if ((int)m_scan->gamma != -2 && m_scan->gamma > 0)
                m_rparams.gamma = m_scan->gamma;
            else
                m_rparams.gamma = -1; 

            // Update Widgets
            m_imageWidget->setImage(m_scan, &m_rparams);
            onImageLoaded();
        } else {
             // Error handling
             // We can't easily get error string from inside thread unless we pass it out.
             QMessageBox::critical(this, "Error Loading Image", "Failed to load image.");
        }
    });
    
    QFuture<bool> future = QtConcurrent::run([tempScan, fileName, progress]() {
        const char *error = nullptr;
        // set progress info to be used by loader?
        // image_data::load takes progress_info*.
        return tempScan->load(fileName.toUtf8().constData(), true, &error, progress.get());
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

