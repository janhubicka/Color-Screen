#include "MainWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QCloseEvent>
#include <QScreen>
#include <QApplication>
#include <QToolBar>
#include <QComboBox>
#include <QCheckBox>
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
#include <QSettings>
#include <QUndoStack>
#include <QUndoCommand>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include "ImageWidget.h"
#include "../libcolorscreen/include/base.h"
#include "NavigationView.h"
#include "../libcolorscreen/include/render-parameters.h"

// Undo/Redo Implementation

class ChangeParametersCommand : public QUndoCommand
{
public:
    ChangeParametersCommand(MainWindow *window, const ParameterState &oldState, const ParameterState &newState)
        : m_window(window), m_oldState(oldState), m_newState(newState)
    {
        setText("Change Parameters");
    }
    
    int id() const override
    {
        return 1234; // Unique ID for parameter changes
    }
    
    bool mergeWith(const QUndoCommand *other) override
    {
        if (other->id() != id()) return false;
        
        const ChangeParametersCommand *cmd = static_cast<const ChangeParametersCommand*>(other);
        
        // We merged with 'other'. 'other' is the NEWER command. 
        // We are the older command on the stack.
        // Wait, QUndoStack::mergeWith(const QUndoCommand * command)
        // "Attempts to merge this command with command. ... If this function returns true, command is deleted."
        // "this" is the previous command. "command" is the new one.
        // So we update OUR "newState" to be "command"s newState.
        
        m_newState = cmd->m_newState;
        return true;
    }

    void undo() override
    {
        m_window->applyState(m_oldState);
    }

    void redo() override
    {
        m_window->applyState(m_newState);
    }

private:
    MainWindow *m_window;
    ParameterState m_oldState;
    ParameterState m_newState;
};


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_undoStack = new QUndoStack(this);

    setupUi();
    
    // Progress Timer
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(100);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::onProgressTimer);
    
    loadRecentFiles();
    
    // Restore window state (position, size, splitters)
    restoreWindowState();
    
    // Initialize UI state
    updateUIFromState(getCurrentState());
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

    // Connect Navigation Signals
    connect(m_imageWidget, &ImageWidget::viewStateChanged, m_navigationView, &NavigationView::onViewStateChanged);
    connect(m_navigationView, &NavigationView::zoomChanged, m_imageWidget, &ImageWidget::setZoom);
    connect(m_navigationView, &NavigationView::panChanged, m_imageWidget, &ImageWidget::setPan);
    
    // Connect NavigationView progress signals
    connect(m_navigationView, &NavigationView::progressStarted, this, &MainWindow::addProgress);
    connect(m_navigationView, &NavigationView::progressFinished, this, &MainWindow::removeProgress);

    // Bottom Right: Tabs
    m_configTabs = new QTabWidget(this);
    
    m_linearizationPanel = new LinearizationPanel(
        [this](){ return getCurrentState(); },
        [this](const ParameterState &s){ changeParameters(s); },
        [this](){ return m_scan; },
        this
    );
    
    m_sharpnessPanel = new SharpnessPanel(
        [this](){ return getCurrentState(); },
        [this](const ParameterState &s){ changeParameters(s); },
        [this](){ return m_scan; },
        this
    );
    
    m_configTabs->addTab(m_linearizationPanel, "Linearization");
    m_configTabs->addTab(m_sharpnessPanel, "Sharpness");
    rightSplitter->addWidget(m_configTabs);

    m_mainSplitter->addWidget(m_rightColumn);
    
    // Set initial sizes (approx 80% for image, 20% for right panel)
    m_mainSplitter->setStretchFactor(0, 9);
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
    QAction *rotLeftAction = m_toolbar->addAction(QIcon::fromTheme("object-rotate-left"), "Rotate Left");
    connect(rotLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);
    
    QAction *rotRightAction = m_toolbar->addAction(QIcon::fromTheme("object-rotate-right"), "Rotate Right");
    connect(rotRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
    
    m_toolbar->addSeparator();
    m_colorCheckBox = new QCheckBox("Color", m_toolbar);
    m_colorCheckBox->setEnabled(false); // Initially disabled
    connect(m_colorCheckBox, &QCheckBox::toggled, this, &MainWindow::onColorCheckBoxChanged);
    m_toolbar->addWidget(m_colorCheckBox);
    
    updateModeMenu();
}

void MainWindow::rotateLeft()
{
    if (!m_scan) return;
    m_scrToImgParams.final_rotation -= 90.0;
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
}

void MainWindow::rotateRight()
{
    if (!m_scan) return;
    m_scrToImgParams.final_rotation += 90.0;
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
}

void MainWindow::onColorCheckBoxChanged(bool checked)
{
    // Update the color field in render_type_parameters
    m_renderTypeParams.color = checked;
    
    // Trigger re-render when color changes
    if (m_scan) {
        m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
    }
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
    
    // Update color checkbox state based on current render type
    const render_type_property &currentProp = render_type_properties[(int)m_renderTypeParams.type];
    bool supportsColorSwitch = currentProp.flags & render_type_property::SUPPORTS_IR_RGB_SWITCH;
    m_colorCheckBox->setEnabled(supportsColorSwitch);
    m_colorCheckBox->blockSignals(true);
    m_colorCheckBox->setChecked(m_renderTypeParams.color);
    m_colorCheckBox->blockSignals(false);
    
    m_modeComboBox->blockSignals(false);
}

void MainWindow::onModeChanged(int index)
{
    if (index < 0) return;
    
    int newType = m_modeComboBox->itemData(index).toInt();
    if (newType >= 0 && newType < colorscreen::render_type_max) {
        if (m_renderTypeParams.type != (colorscreen::render_type_t)newType) {
            m_renderTypeParams.type = (colorscreen::render_type_t)newType;
            
            // Update color checkbox based on new render type
            using namespace colorscreen;
            const render_type_property &prop = render_type_properties[newType];
            bool supportsColorSwitch = prop.flags & render_type_property::SUPPORTS_IR_RGB_SWITCH;
            m_colorCheckBox->setEnabled(supportsColorSwitch);
            m_colorCheckBox->blockSignals(true);
            m_colorCheckBox->setChecked(m_renderTypeParams.color);
            m_colorCheckBox->blockSignals(false);
            
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
    
    m_recentFilesMenu = fileMenu->addMenu("Open &Recent");
    updateRecentFileActions();
    
    QAction *openParamsAction = fileMenu->addAction("Open &Parameters...");
    connect(openParamsAction, &QAction::triggered, this, &MainWindow::onOpenParameters);
    
    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu("&Edit");
    QAction *undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    
    QAction *redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
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
             m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams); // Update nav too
         }
    }
    m_undoStack->clear();
    updateModeMenu();
    updateUIFromState(getCurrentState());
}

void MainWindow::onOpenImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), 
        "Images (*.tif *.tiff *.dng *.png *.jpg *.jpeg);;All Files (*)");
    if (fileName.isEmpty()) return;
    
    loadFile(fileName);
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
      if (m_scan) {
        m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams);
        m_navigationView->setMinScale(m_imageWidget->getMinScale());
    }
    
    // Refresh param values too
    applyState(getCurrentState());
}

// Recent Files Implementation

void MainWindow::addToRecentFiles(const QString &filePath)
{
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    
    while (m_recentFiles.size() > MaxRecentFiles)
        m_recentFiles.removeLast();
        
    updateRecentFileActions();
    saveRecentFiles();
}

void MainWindow::updateRecentFileActions()
{
    m_recentFilesMenu->clear();
    m_recentFileActions.clear();
    
    for (int i = 0; i < m_recentFiles.size(); ++i) {
        QString text = tr("&%1 %2").arg(i + 1).arg(QFileInfo(m_recentFiles[i]).fileName());
        QAction *action = m_recentFilesMenu->addAction(text, this, &MainWindow::openRecentFile);
        action->setData(m_recentFiles[i]);
        action->setToolTip(m_recentFiles[i]);
        m_recentFileActions.append(action);
    }
    
    if (m_recentFiles.isEmpty()) {
        m_recentFilesMenu->addAction("No Recent Files")->setEnabled(false);
    } else {
        m_recentFilesMenu->addSeparator();
        QAction *clearAction = m_recentFilesMenu->addAction("Clear Recent Files");
        connect(clearAction, &QAction::triggered, this, [this](){
            m_recentFiles.clear();
            updateRecentFileActions();
            saveRecentFiles();
        });
    }
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action) {
        QString fileName = action->data().toString();
        loadFile(fileName);
    }
}

void MainWindow::loadFile(const QString &fileName)
{
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
                if (!colorscreen::load_csp(f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams, &error)) {
                    QMessageBox::warning(this, "Error Loading Parameters", 
                        error ? QString::fromUtf8(error) : "Unknown error loading parameters.");
                } else {
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
    
    std::shared_ptr<colorscreen::image_data> tempScan = std::make_shared<colorscreen::image_data>();
    // Access m_rparams carefully. It's a member.
    colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;
    
    QFutureWatcher<std::pair<bool, QString>> *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this, watcher, tempScan, progress, fileName]() {
        std::pair<bool, QString> result = watcher->result();
        removeProgress(progress);
        watcher->deleteLater();
        
        if (result.first) {
             m_scan = tempScan;
             
            if ((int)m_scan->gamma != -2 && m_scan->gamma > 0 && m_rparams.gamma == -1) // Update only if unknown
                m_rparams.gamma = m_scan->gamma;
            else if (m_rparams.gamma == -1)
                m_rparams.gamma = -1; 

            m_undoStack->clear();

            m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams, &m_detectParams, &m_renderTypeParams);
            onImageLoaded();
            
            // Add to recent files
            addToRecentFiles(fileName);
            
        } else {
             if (progress->cancelled()) {
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

void MainWindow::loadRecentFiles()
{
    QSettings settings;
    m_recentFiles = settings.value("recentFiles").toStringList();
    updateRecentFileActions();
}

void MainWindow::saveRecentFiles()
{
    QSettings settings;
    settings.setValue("recentFiles", m_recentFiles);
}

// Undo/Redo Implementation

void MainWindow::applyState(const ParameterState &state)
{
    // User requested rotation is not part of parameters.
    // Preserve current rotation when applying state.
    double currentRot = m_scrToImgParams.final_rotation;
    
    m_rparams = state.rparams;
    m_scrToImgParams = state.scrToImg;
    m_detectParams = state.detect;
    m_solverParams = state.solver; // Manually copy logic if needed? Struct copy should work if fields are copyable.
    // solver_parameters has vector, copy constructor should be fine (std::vector).
    
    m_scrToImgParams.final_rotation = currentRot;
    
    // Update widgets - use updateParameters to avoid blocking
    if (m_scan) {
        m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams);
        m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams, &m_detectParams);
    }
    
    updateUIFromState(state);
}

void MainWindow::updateUIFromState(const ParameterState &state)
{
    m_linearizationPanel->updateUI();
    m_sharpnessPanel->updateUI();
}

ParameterState MainWindow::getCurrentState() const
{
    ParameterState state;
    state.rparams = m_rparams;
    state.scrToImg = m_scrToImgParams;
    state.detect = m_detectParams;
    state.solver = m_solverParams;
    return state;
}

void MainWindow::changeParameters(const ParameterState &newState)
{
    ParameterState currentState = getCurrentState();
    if (currentState == newState) return;
    
    m_undoStack->push(new ChangeParametersCommand(this, currentState, newState));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    event->accept();
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    
    // Save window geometry and state
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("windowState", saveState());
    
    // Save desktop size for validation on restore
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        settings.setValue("desktopSize", screen->availableGeometry().size());
    }
    
    // Save splitter positions
    if (m_mainSplitter) {
        settings.setValue("mainSplitterState", m_mainSplitter->saveState());
    }
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    
    // Check if desktop size has changed
    bool desktopSizeValid = true;
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        QSize savedDesktopSize = settings.value("desktopSize").toSize();
        QSize currentDesktopSize = screen->availableGeometry().size();
        
        // Allow some tolerance (e.g., taskbar changes)
        if (savedDesktopSize.isValid()) {
            int widthDiff = qAbs(savedDesktopSize.width() - currentDesktopSize.width());
            int heightDiff = qAbs(savedDesktopSize.height() - currentDesktopSize.height());
            
            // If desktop size changed significantly (more than 100 pixels), don't restore
            if (widthDiff > 100 || heightDiff > 100) {
                desktopSizeValid = false;
            }
        }
    }
    
    // Restore window geometry if desktop size is compatible
    if (desktopSizeValid && settings.contains("windowGeometry")) {
        restoreGeometry(settings.value("windowGeometry").toByteArray());
        restoreState(settings.value("windowState").toByteArray());
    } else {
        // Default size and position
        resize(1200, 800);
        // Center on screen
        if (screen) {
            QRect screenGeometry = screen->availableGeometry();
            move(screenGeometry.center() - rect().center());
        }
    }
    
    // Restore splitter state (always try this, it's safe)
    if (m_mainSplitter && settings.contains("mainSplitterState")) {
        m_mainSplitter->restoreState(settings.value("mainSplitterState").toByteArray());
    }
}

