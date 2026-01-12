#include "MainWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QComboBox>
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
    
    createToolbar(); // Initialize toolbar
}

void MainWindow::createToolbar()
{
    m_toolbar = addToolBar("Main Toolbar");
    m_toolbar->setMovable(false);
    
    QLabel *modeLabel = new QLabel("Mode: ", m_toolbar);
    m_toolbar->addWidget(modeLabel);
    
    m_modeComboBox = new QComboBox(m_toolbar);
    m_modeComboBox->setMinimumWidth(200);
    connect(m_modeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), 
            this, &MainWindow::onModeChanged);
    m_toolbar->addWidget(m_modeComboBox);
    
    m_toolbar->addSeparator();
    QAction *rotLeftAction = m_toolbar->addAction("Rotate Left");
    connect(rotLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);
    
    QAction *rotRightAction = m_toolbar->addAction("Rotate Right");
    connect(rotRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
    
    updateModeMenu();
}

void MainWindow::rotateLeft()
{
    if (!m_scan) return;
    m_scrToImgParams.final_rotation -= 90.0;
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
}

void MainWindow::rotateRight()
{
    if (!m_scan) return;
    m_scrToImgParams.final_rotation += 90.0;
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
}

void MainWindow::updateModeMenu()
{
    m_modeComboBox->blockSignals(true);
    m_modeComboBox->clear();
    
    // We access the static array in anonymous namespace from render-type-parameters.h
    // Since we included the header, we can try accessing it via the namespace.
    // However, anonymous namespace members have internal linkage. 
    // Usually headers shouldn't define static data in anonymous namespaces unless used carefully.
    // Assuming we can access colorscreen::render_type_properties 
    // NOTE: In C++, anonymous namespace members are accessible in the same TU.
    // If render_type_properties is in a header in anonymous namespace, each TU gets a copy.
    // But we need to refer to it. It's inside namespace colorscreen { namespace { ... } } or just namespace { ... } inside colorscreen?
    // The header has: namespace colorscreen { namespace { static const ... } }
    
    using namespace colorscreen;
    
    for (int i = 0; i < render_type_max; ++i) {
        const render_type_property &prop = render_type_properties[i];
        
        // Filter logic
        bool show = true;
        
        // If given type has render_type_property::NEEDS_SCR_TO_IMG do not show it if scr_to_img type is Random.
        if (prop.flags & render_type_property::NEEDS_SCR_TO_IMG) {
            if (m_scrToImgParams.type == colorscreen::Random) {
                show = false;
            }
        }
        
        // If given type has render_type_property::NEEDS_RGB do not show it if m_scan->rgbdata is NULL.
        if (prop.flags & render_type_property::NEEDS_RGB) {
            // Check m_scan
            if (!m_scan || !m_scan->rgbdata) {
                show = false;
            }
        }
        
        if (show) {
            m_modeComboBox->addItem(prop.name, QVariant(i));
        }
    }
    
    // Select current type if present
    int idx = m_modeComboBox->findData((int)m_renderTypeParams.type);
    if (idx != -1) {
        m_modeComboBox->setCurrentIndex(idx);
    } else if (m_modeComboBox->count() > 0) {
        // Fallback
        m_modeComboBox->setCurrentIndex(0);
        // We might want to update m_renderTypeParams.type? 
        // Let's defer that to user interaction or explicit set.
    }
    
    m_modeComboBox->blockSignals(false);
}

void MainWindow::onModeChanged(int index)
{
    if (index < 0) return;
    
    int newType = m_modeComboBox->itemData(index).toInt();
    if (newType >= 0 && newType < colorscreen::render_type_max) {
        if (m_renderTypeParams.type != (colorscreen::render_type_t)newType) {
            m_renderTypeParams.type = (colorscreen::render_type_t)newType;
            // Trigger render update
            if (m_scan) {
                m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
            }
        }
    }
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
             m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
         }
    } else {
        // Just update params... actually setImage logic uses values.
        // If we want to support run-time tuning without re-creating renderer, we'd need setters in Renderer.
        // But for Open Parameters (file load), re-creating renderer is fine.
        // Even if not changed, we might want to update rparams?
        // Let's just update if we have a scan.
         if (m_scan) {
             m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
         }
    }
    updateModeMenu();
}

void MainWindow::onOpenImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), 
        "Images (*.tif *.tiff *.dng *.png *.jpg *.jpeg);;All Files (*)");
    if (fileName.isEmpty()) return;
    
    // Clear current image and stop rendering
    m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr);
    
    // Check for .par file
    QFileInfo fileInfo(fileName);
    QString parFile = fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
    
    if (QFile::exists(parFile)) {
        if (QMessageBox::question(this, "Load Parameters?", 
                                  "A parameter file was found for this image. Do you want to load it?") 
            == QMessageBox::Yes) {
            
            FILE *f = fopen(parFile.toUtf8().constData(), "r");
            if (f) {
                const char *error = nullptr;
                // We load into members. Note: if successful, m_rparams will be updated.
                // We trust the library to handle potentially partial loads safely or we rely on it failing atomically.
                if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams, &error)) {
                    QMessageBox::warning(this, "Error Loading Parameters", 
                        error ? QString::fromUtf8(error) : "Unknown error loading parameters.");
                } else {
                    // Update our tracking for "changed" state
                    m_prevScrToImgParams = m_scrToImgParams;
                    m_prevDetectParams = m_detectParams;
                }
                fclose(f);
            }
        }
    }

    auto progress = std::make_shared<colorscreen::progress_info>();
    progress->set_task("Loading image", 0);
    addProgress(progress);
    
    // Use QtConcurrent to load in background
    // We capture pointers, but we must ensure safety. 
    // Loading loads into m_scan. m_scan is in MainWindow. 
    // We must ensure UI doesn't access m_scan while loading.
    // Ideally use a temporary image_data and swap it in.
    
    std::shared_ptr<colorscreen::image_data> tempScan = std::make_shared<colorscreen::image_data>();
    
    // Check if rparams was updated by load_csp
    colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;
    
    // Return status and error message
    QFutureWatcher<std::pair<bool, QString>> *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this, watcher, tempScan, progress, fileName]() {
        std::pair<bool, QString> result = watcher->result();
        removeProgress(progress);
        watcher->deleteLater();
        
        if (result.first) {
             // m_scan is now a shared_ptr, we can just assign the pointer
             m_scan = tempScan;
             
             // Initialize default parameters akin to gtkgui IF not loaded from par?
             // GTKGUI logic often resets or sets based on image if not loaded.
             // But if we loaded parameters, we might want to respect them.
             // However, some parameters like gamma might be auto-detected if not specified? 
             // m_scan->gamma is filled by loader.
             // If m_rparams.gamma was loaded, we keep it. If it was default (not loaded), maybe update?
             // load_csp populates rparams.
             
            if ((int)m_scan->gamma != -2 && m_scan->gamma > 0 && m_rparams.gamma == -1) // Update only if unknown
                m_rparams.gamma = m_scan->gamma;
            else if (m_rparams.gamma == -1)
                m_rparams.gamma = -1; // Keep unknown if both unknown

            // Update Widgets (pass all params)
            m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
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
    
    QFuture<std::pair<bool, QString>> future = QtConcurrent::run([tempScan, fileName, progress, demosaic]() {
        const char *error = nullptr;
        bool res = tempScan->load(fileName.toUtf8().constData(), true, &error, progress.get(), demosaic);
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
   // Update UI components that depend on loaded image
   updateModeMenu();
}

