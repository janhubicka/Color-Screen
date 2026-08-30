#include "ImageViewWindow.h"

#include "ColorScreenApplication.h"
#include "ImageWidget.h"
#include "MainWindow.h"
#include "MultiLineTabWidget.h"
#include "NavigationView.h"
#include "SharpnessPanel.h"
#include "SlantedEdgeDialog.h"
#include "SynchronizedRunnable.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

/** Return the same bundled symbolic icon used by MainWindow's toolbar. */
QIcon viewIcon(const char *resource) {
  return QIcon(QString::fromLatin1(resource));
}

} // namespace

/** Return the one status bar belonging to the current top-level window. */
QStatusBar *ImageViewWindow::statusBar() const {
  return m_workspaceStatusBar ? m_workspaceStatusBar.data()
                              : QMainWindow::statusBar();
}

/** Return this view's private status bar, regardless of attachment. */
QStatusBar *ImageViewWindow::standaloneStatusBar() const {
  return QMainWindow::statusBar();
}

/** Share STATUSBAR with every tab in the enclosing workspace window. */
void ImageViewWindow::setWorkspaceStatusBar(QStatusBar *sharedStatusBar) {
  if (m_workspaceStatusBar.data() == sharedStatusBar)
    return;

  QStatusBar *localStatusBar = QMainWindow::statusBar();
  const QString localMessage = localStatusBar->currentMessage();
  m_workspaceStatusBar = sharedStatusBar;
  if (sharedStatusBar) {
    localStatusBar->hide();
    if (!localMessage.isEmpty())
      sharedStatusBar->showMessage(localMessage);
  }
}

/** Construct one secondary, display-only view of DOCUMENT. */
ImageViewWindow::ImageViewWindow(MainWindow *document, int viewNumber,
                                 QWidget *parent)
    : QMainWindow(parent), m_document(document), m_viewNumber(viewNumber) {
  setObjectName(QStringLiteral("imageViewWindow"));
  m_renderTypeParams = document ? document->viewRenderTypeParameters()
                                : colorscreen::render_type_parameters();
  setupUi();

  if (m_document) {
    connect(m_document, &MainWindow::documentStateChanged, this,
            &ImageViewWindow::refreshFromDocument);
    connect(m_document, &QWidget::windowTitleChanged, this,
            [this](const QString &) { refreshFromDocument(); });
    connect(m_document, &QObject::destroyed, this, [this]() {
      m_document.clear();
      QPointer<ImageViewWindow> view(this);
      QTimer::singleShot(0, qApp, [view]() {
        if (!view)
          return;
        if (auto *application = dynamic_cast<ColorScreenApplication *>(
                QApplication::instance()))
          application->closeView(view);
        else
          view->close();
      });
    });
  }
  refreshFromDocument();
}

/** Construct a Sharpness-only secondary view of an external reference scan. */
ImageViewWindow::ImageViewWindow(MainWindow *document, int viewNumber,
                                 const QString &referenceFile, QWidget *parent)
    : QMainWindow(parent), m_document(document), m_viewNumber(viewNumber),
      m_slantedEdgeReference(true),
      m_referenceFile(QFileInfo(referenceFile).absoluteFilePath()) {
  setObjectName(QStringLiteral("slantedEdgeReferenceView"));
  m_renderTypeParams.type = colorscreen::render_type_original;
  m_renderTypeParams.color = true;
  setupUi();
  setupReferenceInspector();

  if (m_document) {
    connect(m_document, &MainWindow::documentStateChanged, this,
            &ImageViewWindow::refreshFromDocument);
    connect(m_document, &QWidget::windowTitleChanged, this,
            [this](const QString &) { refreshFromDocument(); });
    connect(m_document, &QObject::destroyed, this, [this]() {
      m_document.clear();
      QPointer<ImageViewWindow> view(this);
      QTimer::singleShot(0, qApp, [view]() {
        if (!view)
          return;
        if (auto *application = dynamic_cast<ColorScreenApplication *>(
                QApplication::instance()))
          application->closeView(view);
        else
          view->close();
      });
    });
  }
  refreshFromDocument();
  loadReferenceImage(m_referenceFile);
}


/** Destroy a secondary view without taking the document-owned inspector with it. */
ImageViewWindow::~ImageViewWindow() {
  {
    std::unique_lock<std::mutex> locker(m_referenceLoadMutex);
    m_referenceLoadCondition.wait(
        locker, [this]() { return !m_referenceWorkerActive; });
  }
  releaseDocumentInspector();
}

/** Return the document whose state this secondary view follows. */
MainWindow *ImageViewWindow::sourceDocument() const { return m_document.data(); }

/** Return the inspector appropriate for this secondary view. */
QWidget *ImageViewWindow::workspaceInspectorWidget() const {
  if (m_slantedEdgeReference)
    return m_referenceInspector;
  if (!m_document || !m_imageWidget ||
      m_imageWidget->sharedImageData() != m_document->sharedImageData())
    return nullptr;
  return m_document->workspaceInspectorWidget();
}

/** Build the standard compact toolbar and menus for a secondary image view. */
void ImageViewWindow::setupUi() {
  m_imageWidget = new ImageWidget(this);
  m_imageWidget->setInteractionMode(ImageWidget::PanMode);
  setCentralWidget(m_imageWidget);
  connect(m_imageWidget, &ImageWidget::areaSelected, this,
          &ImageViewWindow::onReferenceAreaSelected);

  m_toolbar = addToolBar(tr("View"));
  m_toolbar->setObjectName(QStringLiteral("ImageViewToolbar"));
  m_toolbar->setMovable(false);
  m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

  m_toolbar->addWidget(new QLabel(tr("Mode: "), m_toolbar));
  m_modeComboBox = new QComboBox(m_toolbar);
  m_modeComboBox->setMinimumWidth(170);
  m_toolbar->addWidget(m_modeComboBox);
  connect(m_modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ImageViewWindow::onModeChanged);

  m_colorCheckBox = new QCheckBox(tr("Color"), m_toolbar);
  m_colorCheckBoxAction = m_toolbar->addWidget(m_colorCheckBox);
  connect(m_colorCheckBox, &QCheckBox::toggled, this,
          &ImageViewWindow::onColorChanged);

  if (!m_slantedEdgeReference) {
    m_toolbar->addWidget(new QLabel(tr("Coordinates: "), m_toolbar));
    m_coordinateComboBox = new QComboBox(m_toolbar);
    m_coordinateComboBox->setObjectName(QStringLiteral("CoordinateSpaceCombo"));
    m_toolbar->addWidget(m_coordinateComboBox);
    connect(m_coordinateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageViewWindow::onCoordinateChanged);

    m_finalRotationLabelAction = m_toolbar->addWidget(
        new QLabel(tr("Final rotation: "), m_toolbar));
    m_finalRotationSpinBox = new QDoubleSpinBox(m_toolbar);
    m_finalRotationSpinBox->setObjectName(QStringLiteral("FinalRotationSpin"));
    m_finalRotationSpinBox->setRange(-180.0, 180.0);
    m_finalRotationSpinBox->setDecimals(2);
    m_finalRotationSpinBox->setSingleStep(0.1);
    m_finalRotationSpinBox->setSuffix(QStringLiteral("°"));
    m_finalRotationSpinBox->setToolTip(
        tr("Continuous rotation of the final-coordinate image; saved in the parameter file"));
    m_finalRotationSpinAction = m_toolbar->addWidget(m_finalRotationSpinBox);
    connect(m_finalRotationSpinBox, &QDoubleSpinBox::valueChanged, this,
            [this](double degrees) {
              if (m_document && m_imageWidget &&
                  m_imageWidget->coordinateSpace() ==
                      colorscreen::render_final_coordinates)
                m_document->setDocumentFinalRotation(degrees);
            });
  }

  m_toolbar->addSeparator();

  QAction *pan = new QAction(viewIcon(":/icons/hand.svg"), tr("Pan"), this);
  if (m_slantedEdgeReference || !m_document)
    m_toolbar->addAction(pan);
  pan->setCheckable(true);
  pan->setChecked(true);
  pan->setShortcut(QKeySequence(QStringLiteral("P")));
  connect(pan, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->setInteractionMode(ImageWidget::PanMode); });
  connect(m_imageWidget, &ImageWidget::interactionModeChanged, this,
          [pan](ImageWidget::InteractionMode mode) {
            const QSignalBlocker blocker(pan);
            pan->setChecked(mode == ImageWidget::PanMode);
          });

  QAction *zoomIn =
      new QAction(viewIcon(":/icons/zoom-in.svg"), tr("Zoom In"), this);
  if (m_slantedEdgeReference || !m_document)
    m_toolbar->addAction(zoomIn);
  zoomIn->setShortcut(QKeySequence::ZoomIn);
  connect(zoomIn, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomBy(1.25); });
  QAction *zoomOut =
      new QAction(viewIcon(":/icons/zoom-out.svg"), tr("Zoom Out"), this);
  if (m_slantedEdgeReference || !m_document)
    m_toolbar->addAction(zoomOut);
  zoomOut->setShortcut(QKeySequence::ZoomOut);
  connect(zoomOut, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomBy(0.8); });
  QAction *zoom100 =
      new QAction(viewIcon(":/icons/zoom-100.svg"), tr("Zoom 1:1"), this);
  if (m_slantedEdgeReference || !m_document)
    m_toolbar->addAction(zoom100);
  zoom100->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  connect(zoom100, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomTo(1.0); });
  QAction *fit =
      new QAction(viewIcon(":/icons/zoom-fit.svg"), tr("Fit"), this);
  if (m_slantedEdgeReference || !m_document)
    m_toolbar->addAction(fit);
  fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  connect(fit, &QAction::triggered, m_imageWidget, &ImageWidget::smoothFitToView);

  if (!m_slantedEdgeReference) {
    m_rotateLeftAction = new QAction(viewIcon(":/icons/rotate-left.svg"),
                                     tr("Rotate Left"), this);
    m_rotateLeftAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(m_rotateLeftAction, &QAction::triggered, this, [this]() {
      if (m_document)
        m_document->rotateDocumentLeft();
    });
    m_rotateRightAction = new QAction(viewIcon(":/icons/rotate-right.svg"),
                                      tr("Rotate Right"), this);
    m_rotateRightAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_rotateRightAction, &QAction::triggered, this, [this]() {
      if (m_document)
        m_document->rotateDocumentRight();
    });
    m_mirrorAction = new QAction(viewIcon(":/icons/mirror.svg"),
                                 tr("Mirror Horizontally"), this);
    m_mirrorAction->setCheckable(true);
    connect(m_mirrorAction, &QAction::toggled, this, [this](bool checked) {
      if (!m_document || !m_imageWidget)
        return;
      if (m_imageWidget->coordinateSpace() ==
          colorscreen::render_final_coordinates)
        m_document->setDocumentFinalMirror(checked);
      else
        m_document->setDocumentMirror(checked);
    });
  }

  if (!m_slantedEdgeReference && m_document)
    m_document->appendOrdinaryViewToolActions(m_toolbar);

  QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
  QAction *closeView = new QAction(tr("&Close View"), this);
  closeView->setShortcut(QKeySequence::Close);
  connect(closeView, &QAction::triggered, this, [this]() {
    if (auto *application =
            dynamic_cast<ColorScreenApplication *>(QApplication::instance()))
      application->closeView(this);
    else
      close();
  });

  QMenu *documentFileMenu = nullptr;
  if (m_document) {
    for (QAction *menuAction : m_document->menuBar()->actions()) {
      if (menuAction && QString(menuAction->text()).remove('&') ==
                            QStringLiteral("File")) {
        documentFileMenu = menuAction->menu();
        break;
      }
    }
  }
  bool addedCloseView = false;
  if (documentFileMenu) {
    for (QAction *action : documentFileMenu->actions()) {
      if (!action)
        continue;
      if (QString(action->text()).remove('&') ==
          QStringLiteral("Close Window")) {
        fileMenu->addAction(closeView);
        addedCloseView = true;
      } else {
        fileMenu->addAction(action);
      }
    }
  }
  if (!addedCloseView)
    fileMenu->addAction(closeView);

  if (!m_slantedEdgeReference && m_document)
    if (QAction *edit = m_document->ordinaryViewEditMenuAction())
      menuBar()->addAction(edit);

  QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
  viewMenu->addAction(pan);
  viewMenu->addSeparator();
  viewMenu->addAction(zoomIn);
  viewMenu->addAction(zoomOut);
  viewMenu->addAction(zoom100);
  viewMenu->addAction(fit);
  if (!m_slantedEdgeReference) {
    viewMenu->addSeparator();
    viewMenu->addAction(m_rotateLeftAction);
    viewMenu->addAction(m_rotateRightAction);
    viewMenu->addAction(m_mirrorAction);
  }

  if (!m_slantedEdgeReference && m_document)
    if (QAction *registration =
            m_document->ordinaryViewRegistrationMenuAction())
      menuBar()->addAction(registration);

  QMenu *windowMenu = menuBar()->addMenu(tr("&Window"));
  connect(windowMenu, &QMenu::aboutToShow, this, [this, windowMenu]() {
    if (auto *application =
            dynamic_cast<ColorScreenApplication *>(QApplication::instance()))
      application->populateWindowMenu(windowMenu, m_document.data(), this);
  });

  QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
  QAction *aboutAction = helpMenu->addAction(tr("&About Color-Screen"));
  connect(aboutAction, &QAction::triggered, this, []() {
    QMessageBox::about(
        QApplication::activeWindow(), QObject::tr("About Color-Screen"),
        QObject::tr("<b>Color-Screen %1</b><br><br>"
                    "Open-source software for digital reconstruction and "
                    "analysis of early color photographic processes.")
            .arg(QApplication::applicationVersion()));
  });
  QAction *aboutQtAction = helpMenu->addAction(tr("About &Qt"));
  connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

  statusBar()->showMessage(
      m_slantedEdgeReference
          ? tr("Slanted-edge reference — sharpness parameters are shared")
          : tr("Secondary view — document panels and edits are shared"));

  if (!m_slantedEdgeReference) {
    m_documentInspectorHost = new QWidget(this);
    m_documentInspectorHost->setObjectName(
        QStringLiteral("SecondaryDocumentInspectorHost"));
    auto *inspectorLayout = new QVBoxLayout(m_documentInspectorHost);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);

    m_documentInspectorDock = new QDockWidget(tr("Document Controls"), this);
    m_documentInspectorDock->setObjectName(
        QStringLiteral("SecondaryDocumentControlsDock"));
    m_documentInspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                             Qt::RightDockWidgetArea);
    m_documentInspectorDock->setMinimumWidth(280);
    m_documentInspectorDock->setWidget(m_documentInspectorHost);
    addDockWidget(Qt::RightDockWidgetArea, m_documentInspectorDock);
    m_documentInspectorDock->hide();
  }
  resize(1100, 800);
}

/** Build the reduced navigation + Sharpness inspector for a reference image. */
void ImageViewWindow::setupReferenceInspector() {
  if (!m_slantedEdgeReference || !m_document)
    return;

  m_referenceInspector = new QWidget(this);
  m_referenceInspector->setObjectName(
      QStringLiteral("SlantedEdgeReferenceInspector"));
  auto *layout = new QVBoxLayout(m_referenceInspector);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *splitter = new QSplitter(Qt::Vertical, m_referenceInspector);
  layout->addWidget(splitter);

  m_navigationView = new NavigationView(splitter);
  m_navigationView->setObjectName(QStringLiteral("SlantedEdgeNavigation"));
  m_navigationView->setMinimumHeight(200);
  splitter->addWidget(m_navigationView);
  connect(m_imageWidget, &ImageWidget::viewStateChanged, m_navigationView,
          &NavigationView::onViewStateChanged);
  connect(m_navigationView, &NavigationView::zoomChanged, m_imageWidget,
          &ImageWidget::setZoom);
  connect(m_navigationView, &NavigationView::panChanged, m_imageWidget,
          &ImageWidget::setPan);

  m_referenceTabs = new MultiLineTabWidget(splitter);
  m_referenceTabs->setObjectName(QStringLiteral("SlantedEdgeReferenceTabs"));
  splitter->addWidget(m_referenceTabs);

  m_sharpnessPanel = new SharpnessPanel(
      [this]() {
        return m_document ? m_document->documentStateSnapshot()
                          : ParameterState();
      },
      [this](const ParameterState &state, const QString &description) {
        if (m_document)
          m_document->applySharedDocumentState(state, description);
      },
      [this]() { return m_scan; }, m_referenceTabs);
  m_referenceTabs->addTab(m_sharpnessPanel, tr("Sharpness"));

  connect(m_sharpnessPanel,
          &SharpnessPanel::openSlantedEdgeReferenceRequested, this, [this]() {
            if (auto *application = dynamic_cast<ColorScreenApplication *>(
                    QApplication::instance()))
              application->openSlantedEdgeReference(m_document.data(), this);
          });
  connect(m_sharpnessPanel, &SharpnessPanel::measureMtfRequested, this,
          &ImageViewWindow::onMeasureMtfRequested);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({220, 520});

  m_referenceInspectorDock = new QDockWidget(tr("Reference Controls"), this);
  m_referenceInspectorDock->setObjectName(
      QStringLiteral("SlantedEdgeSharpnessDock"));
  m_referenceInspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                            Qt::RightDockWidgetArea);
  m_referenceInspectorDock->setMinimumWidth(300);
  m_referenceInspectorDock->setWidget(m_referenceInspector);
  addDockWidget(Qt::RightDockWidgetArea, m_referenceInspectorDock);
  m_referenceInspectorDock->show();
}

/** Create floating diagnostic docks owned by this specialized reference view. */
/** Return any floating reference diagnostics to the Sharpness panel. */
/** Load the reference scan asynchronously without touching document filenames. */
void ImageViewWindow::loadReferenceImage(const QString &fileName) {
  if (!m_slantedEdgeReference || fileName.isEmpty() || m_referenceLoadPending)
    return;

  m_referenceLoadPending = true;
  m_referenceFile = QFileInfo(fileName).absoluteFilePath();
  statusBar()->showMessage(tr("Opening slanted-edge reference…"));

  const colorscreen::image_data::demosaicing_t demosaic =
      m_document ? m_document->documentStateSnapshot().rparams.demosaic
                 : colorscreen::image_data::demosaic_none;
  auto scan = std::make_shared<colorscreen::image_data>();
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Opening slanted edge reference", 0);
  const QString path = m_referenceFile;

  {
    std::lock_guard<std::mutex> locker(m_referenceLoadMutex);
    m_referenceWorkerActive = true;
    m_referenceLoadOk = false;
    m_referenceLoadError.clear();
    m_pendingReferenceScan.reset();
  }

  runSynchronized([this, scan, path, progress, demosaic]() {
    const char *error = nullptr;
    colorscreen::sub_task task(progress.get());
    const bool ok = scan->load(path.toUtf8().constData(), true, &error,
                               progress.get(), demosaic);
    const QString message =
        !ok && error ? QString::fromUtf8(error) : QString();

    {
      std::lock_guard<std::mutex> locker(m_referenceLoadMutex);
      m_referenceLoadOk = ok;
      m_referenceLoadError = message;
      m_pendingReferenceScan = scan;
    }

    // The queued Qt call carries no non-trivial payload.  The result itself is
    // published through m_referenceLoadMutex, which TSan can observe.
    QMetaObject::invokeMethod(this, "finishReferenceLoad",
                              Qt::QueuedConnection);

    {
      std::lock_guard<std::mutex> locker(m_referenceLoadMutex);
      m_referenceWorkerActive = false;
    }
    m_referenceLoadCondition.notify_all();
  });
}

void ImageViewWindow::finishReferenceLoad() {
  std::shared_ptr<colorscreen::image_data> scan;
  bool ok = false;
  QString error;
  {
    std::lock_guard<std::mutex> locker(m_referenceLoadMutex);
    ok = m_referenceLoadOk;
    error = m_referenceLoadError;
    scan = std::move(m_pendingReferenceScan);
  }

  m_referenceLoadPending = false;
  if (!ok) {
    QMessageBox::critical(this, tr("Error Loading Slanted Edge Reference"),
                          error.isEmpty() ? tr("Failed to load image.")
                                          : error);
    close();
    return;
  }

  m_scan = std::move(scan);
  rebuildModeList();
  updateViewControls();
  updateImageParameters(true);
  if (m_sharpnessPanel)
    m_sharpnessPanel->updateUI();
  setWindowTitle(tr("%1 — Slanted edge reference")
                     .arg(QFileInfo(m_referenceFile).fileName()));
  statusBar()->showMessage(
      tr("Slanted-edge reference — sharpness parameters are shared"));
}

/** Reload the external reference with the document's current demosaic mode. */
void ImageViewWindow::reloadReferenceImage() {
  if (!m_slantedEdgeReference || m_referenceFile.isEmpty())
    return;
  loadReferenceImage(m_referenceFile);
}

/** Refresh shared state without disturbing view-local render mode/zoom/pan. */
void ImageViewWindow::refreshFromDocument() {
  if (!m_document)
    return;

  std::shared_ptr<colorscreen::image_data> scan = m_scan;
  if (!m_slantedEdgeReference)
    scan = m_document->sharedImageData();
  const bool imageChanged = !m_slantedEdgeReference && scan != m_scan;
  const int oldRotation = static_cast<int>(m_rparams.scan_rotation);
  if (!m_slantedEdgeReference)
    m_scan = scan;

  const ParameterState state = m_document->documentStateSnapshot();
  m_rparams = state.rparams;
  m_detectParams = state.detect;
  m_scrToImgParams = state.scrToImg;
  m_solverParams = state.solver;

  const int newRotation = static_cast<int>(m_rparams.scan_rotation);
  if (!imageChanged && m_scan && oldRotation != newRotation)
    m_imageWidget->pivotViewport(oldRotation, newRotation);

  rebuildModeList();
  updateViewControls();
  updateImageParameters(imageChanged);
  if (m_sharpnessPanel)
    m_sharpnessPanel->updateUI();

  if (m_slantedEdgeReference && !m_referenceFile.isEmpty())
    setWindowTitle(tr("%1 — Slanted edge reference")
                       .arg(QFileInfo(m_referenceFile).fileName()));
  else
    setWindowTitle(tr("%1 — View %2")
                       .arg(m_document->documentDisplayName())
                       .arg(m_viewNumber));
}

/** Populate the mode combo using the same availability rules as MainWindow. */
void ImageViewWindow::rebuildModeList() {
  using namespace colorscreen;

  const int previousType = static_cast<int>(m_renderTypeParams.type);
  m_modeComboBox->blockSignals(true);
  m_modeComboBox->clear();

  for (int i = 0; i < render_type_max; ++i) {
    if (m_slantedEdgeReference && i != render_type_original &&
        i != render_type_image_layer)
      continue;
    const render_type_property &prop = render_type_properties[i];
    bool show = !(prop.flags & render_type_property::HIDE_IN_GUI);
    if (show && (prop.flags & render_type_property::NEEDS_SCR_TO_IMG) &&
        m_scrToImgParams.type == colorscreen::Random)
      show = false;
    if (show && (prop.flags & render_type_property::NEEDS_RGB) &&
        (!m_scan || !m_scan->has_rgb()))
      show = false;
    if (show &&
        (prop.flags & render_type_property::NEEDS_CORRECTION_PROFILE) &&
        !m_rparams.has_correction_profile())
      show = false;
    if (!show)
      continue;

    m_modeComboBox->addItem(QString::fromUtf8(prop.pretty_name), QVariant(i));
    if (prop.help)
      m_modeComboBox->setItemData(m_modeComboBox->count() - 1,
                                  QString::fromUtf8(prop.help), Qt::ToolTipRole);
  }

  int index = m_modeComboBox->findData(previousType);
  if (index < 0 && m_modeComboBox->count() > 0) {
    index = 0;
    m_renderTypeParams.type = static_cast<render_type_t>(
        m_modeComboBox->itemData(index).toInt());
  }
  if (index >= 0)
    m_modeComboBox->setCurrentIndex(index);
  m_modeComboBox->blockSignals(false);
}

/** Synchronize controls owned either by this view or by the shared document. */
void ImageViewWindow::updateViewControls() {
  using namespace colorscreen;
  const bool hasRgb = m_scan && m_scan->has_rgb();
  const render_type_property &prop =
      render_type_properties[static_cast<int>(m_renderTypeParams.type)];
  const bool supportsSwitch =
      prop.flags & render_type_property::SUPPORTS_IR_RGB_SWITCH;

  m_colorCheckBox->blockSignals(true);
  if (m_colorCheckBoxAction)
    m_colorCheckBoxAction->setVisible(hasRgb);
  m_colorCheckBox->setVisible(hasRgb);
  m_colorCheckBox->setEnabled(hasRgb && supportsSwitch);
  m_colorCheckBox->setChecked(hasRgb && m_renderTypeParams.color);
  m_colorCheckBox->blockSignals(false);

  if (m_coordinateComboBox) {
    const bool stitched = m_scan && m_scan->stitch;
    const bool hasFinal = stitched ||
        (m_scan && m_scrToImgParams.type != colorscreen::Random);
    const QSignalBlocker blocker(m_coordinateComboBox);
    m_coordinateComboBox->clear();
    if (!stitched)
      m_coordinateComboBox->addItem(tr("Scan coordinates"),
          (int)colorscreen::render_scan_coordinates);
    if (hasFinal)
      m_coordinateComboBox->addItem(tr("Screen coordinates"),
          (int)colorscreen::render_final_coordinates);
    if (stitched)
      m_imageWidget->setCoordinateSpace(colorscreen::render_final_coordinates);
    else if (!hasFinal && m_imageWidget->coordinateSpace() ==
                            colorscreen::render_final_coordinates)
      m_imageWidget->setCoordinateSpace(colorscreen::render_scan_coordinates);
    int coordinateIndex = m_coordinateComboBox->findData(
        (int)m_imageWidget->coordinateSpace());
    if (coordinateIndex < 0 && m_coordinateComboBox->count())
      coordinateIndex = 0;
    if (coordinateIndex >= 0)
      m_coordinateComboBox->setCurrentIndex(coordinateIndex);
    m_coordinateComboBox->setEnabled(m_coordinateComboBox->count() > 1);
  }

  const bool finalCoordinates = m_imageWidget &&
      m_imageWidget->coordinateSpace() == colorscreen::render_final_coordinates;
  if (m_rotateLeftAction)
    m_rotateLeftAction->setVisible(!finalCoordinates);
  if (m_rotateRightAction)
    m_rotateRightAction->setVisible(!finalCoordinates);
  if (m_finalRotationLabelAction)
    m_finalRotationLabelAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinAction)
    m_finalRotationSpinAction->setVisible(finalCoordinates);
  if (m_finalRotationSpinBox) {
    const QSignalBlocker blocker(m_finalRotationSpinBox);
    m_finalRotationSpinBox->setValue(m_scrToImgParams.final_rotation);
  }
  if (m_mirrorAction) {
    const QSignalBlocker blocker(m_mirrorAction);
    m_mirrorAction->setChecked(finalCoordinates ? m_scrToImgParams.final_mirror
                                                : m_rparams.scan_mirror);
    m_mirrorAction->setText(finalCoordinates ? tr("Mirror Final Image")
                                             : tr("Mirror Horizontally"));
    m_mirrorAction->setToolTip(finalCoordinates
        ? tr("Mirror the final-coordinate image; saved in the parameter file")
        : tr("Mirror the digital scan horizontally"));
  }
}

/** Initialize or update ImageWidget with shared document snapshots. */
void ImageViewWindow::updateImageParameters(bool imageChanged) {
  if (!m_scan)
    return;
  if (imageChanged) {
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                            &m_detectParams, &m_renderTypeParams,
                            &m_solverParams);
    if (m_navigationView)
      m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                 &m_detectParams);
    m_imageWidget->fitToView();
  } else {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
    if (m_navigationView)
      m_navigationView->updateParameters(&m_rparams, &m_scrToImgParams,
                                         &m_detectParams);
  }
}

/** Change the render mode only for this view. */
void ImageViewWindow::onModeChanged(int index) {
  if (index < 0)
    return;
  const int type = m_modeComboBox->itemData(index).toInt();
  if (type < 0 || type >= colorscreen::render_type_max)
    return;
  m_renderTypeParams.type = static_cast<colorscreen::render_type_t>(type);
  updateViewControls();
  updateImageParameters(false);
}

/** Change the IR/RGB choice only for this view. */
void ImageViewWindow::onColorChanged(bool checked) {
  m_renderTypeParams.color = checked;
  updateImageParameters(false);
}

/** Change the coordinate canvas only for this view. */
void ImageViewWindow::onCoordinateChanged(int index) {
  if (!m_coordinateComboBox || index < 0)
    return;
  const auto coordinates = static_cast<colorscreen::render_coordinate_space>(
      m_coordinateComboBox->itemData(index).toInt());
  m_imageWidget->setCoordinateSpace(coordinates);
  updateViewControls();
  if (!m_slantedEdgeReference && m_document &&
      m_document->inspectorImageWidget() == m_imageWidget)
    m_document->setInspectorImageWidget(m_imageWidget);
}

colorscreen::render_coordinate_space ImageViewWindow::coordinateSpace() const {
  return m_imageWidget ? m_imageWidget->coordinateSpace()
                       : colorscreen::render_scan_coordinates;
}

/** Programmatically select this view's coordinate canvas. */
bool ImageViewWindow::setCoordinateSpace(
    colorscreen::render_coordinate_space coordinates) {
  if (m_slantedEdgeReference || !m_imageWidget)
    return false;
  const bool ok = m_imageWidget->setCoordinateSpace(coordinates);
  updateViewControls();
  if (m_document && m_document->inspectorImageWidget() == m_imageWidget)
    m_document->setInspectorImageWidget(m_imageWidget);
  return ok && m_imageWidget->coordinateSpace() == coordinates;
}

/** Programmatically select TYPE in this view. */
bool ImageViewWindow::setRenderType(colorscreen::render_type_t type) {
  const int index = m_modeComboBox->findData(static_cast<int>(type));
  if (index < 0)
    return false;
  m_modeComboBox->setCurrentIndex(index);
  return m_renderTypeParams.type == type;
}

/** Start or cancel slanted-edge selection in a specialized reference view. */
void ImageViewWindow::onMeasureMtfRequested(bool checked) {
  if (!m_slantedEdgeReference || !m_sharpnessPanel)
    return;

  if (!checked) {
    m_pendingMtfParameters.clear();
    if (m_imageWidget->interactionMode() == ImageWidget::GenericAreaMode)
      m_imageWidget->setInteractionMode(ImageWidget::PanMode);
    statusBar()->showMessage(
        tr("Slanted-edge reference — sharpness parameters are shared"));
    return;
  }
  if (!m_document || !m_scan) {
    m_sharpnessPanel->setMeasureMtfChecked(false);
    return;
  }

  const ParameterState currentState = m_document->documentStateSnapshot();
  const colorscreen::mtf_parameters &currentMtf =
      currentState.rparams.sharpen.scanner_mtf;
  colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
  const bool hasRgb = m_scan->has_rgb();
  const bool hasInfrared = m_scan->has_grayscale_or_ir();
  if (!hasRgb) {
    defaults.wavelength
        = currentState.rparams.get_image_layer_wavelength(m_scan.get());
  } else if (defaults.wavelength <= 0) {
    if (!currentMtf.measurements.empty() &&
        currentMtf.measurements.back().wavelength > 0)
      defaults.wavelength = currentMtf.measurements.back().wavelength;
    else if (currentMtf.wavelength > 0)
      defaults.wavelength = currentMtf.wavelength;
  }

  SlantedEdgeDialog dialog(defaults, !currentMtf.measurements.empty(), hasRgb,
                           hasInfrared, this);
  if (dialog.exec() != QDialog::Accepted) {
    m_sharpnessPanel->setMeasureMtfChecked(false);
    return;
  }

  const colorscreen::slanted_edge_parameters baseParameters =
      dialog.parameters();
  m_slantedEdgeParameters = baseParameters;
  m_pendingMtfParameters.clear();
  if (dialog.measureNativeChannels()) {
    static const char *const channelNames[4] = {"Red", "Green", "Blue",
                                                "Infrared"};
    const int channelCount = hasInfrared ? 4 : 3;
    for (int channel = 0; channel < channelCount; ++channel) {
      colorscreen::slanted_edge_parameters p = baseParameters;
      p.channel = channel;
      p.name = baseParameters.name + " " + channelNames[channel];
      p.same_capture = channel == 0 ? baseParameters.same_capture : true;
      double wavelength = currentMtf.wavelengths[channel];
      if (!(colorscreen::my_isfinite(wavelength) && wavelength > 0))
        wavelength = m_scan->wavelengths[channel];
      p.wavelength = colorscreen::my_isfinite(wavelength) && wavelength > 0
                         ? wavelength
                         : 0;
      m_pendingMtfParameters.push_back(std::move(p));
    }
  } else {
    colorscreen::slanted_edge_parameters p = baseParameters;
    p.channel = -1;
    m_pendingMtfParameters.push_back(std::move(p));
  }

  m_imageWidget->setInteractionMode(ImageWidget::GenericAreaMode);
  statusBar()->showMessage(
      tr("Select an area containing a slanted edge to compute its MTF"));
}

/** Convert a widget-space selection to a bounded rectangle in reference scan. */
QRect ImageViewWindow::referenceImageArea(QRect area) const {
  if (!m_scan || area.width() <= 0 || area.height() <= 0)
    return {};

  const colorscreen::point_t p1 = m_imageWidget->widgetToImage(area.topLeft());
  const colorscreen::point_t p2 = m_imageWidget->widgetToImage(area.topRight());
  const colorscreen::point_t p3 =
      m_imageWidget->widgetToImage(area.bottomLeft());
  const colorscreen::point_t p4 =
      m_imageWidget->widgetToImage(area.bottomRight());
  int xmin = std::min({p1.x, p2.x, p3.x, p4.x});
  int xmax = std::max({p1.x, p2.x, p3.x, p4.x});
  int ymin = std::min({p1.y, p2.y, p3.y, p4.y});
  int ymax = std::max({p1.y, p2.y, p3.y, p4.y});
  xmin = std::max(0, xmin);
  ymin = std::max(0, ymin);
  xmax = std::min(static_cast<int>(m_scan->width) - 1, xmax);
  ymax = std::min(static_cast<int>(m_scan->height) - 1, ymax);
  return xmax >= xmin && ymax >= ymin
             ? QRect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1)
             : QRect();
}

/** Measure the selected reference edge and append results to shared params. */
void ImageViewWindow::onReferenceAreaSelected(QRect widgetArea) {
  if (!m_slantedEdgeReference || m_pendingMtfParameters.empty() ||
      !m_document || !m_scan)
    return;

  const QRect area = referenceImageArea(widgetArea);
  if (area.isEmpty())
    return;

  m_imageWidget->setInteractionMode(ImageWidget::PanMode);
  m_sharpnessPanel->setMeasureMtfEnabled(false);
  statusBar()->showMessage(tr("Measuring slanted-edge MTF…"));

  const auto scan = m_scan;
  const ParameterState state = m_document->documentStateSnapshot();
  const auto parameters = m_pendingMtfParameters;
  m_pendingMtfParameters.clear();
  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Measure slanted edge reference", parameters.size());
  auto error = std::make_shared<std::string>();
  auto *watcher = new QFutureWatcher<ParameterState>(this);
  connect(watcher, &QFutureWatcher<ParameterState>::finished, this,
          [this, watcher, error]() {
            const ParameterState updated = watcher->result();
            watcher->deleteLater();
            if (!m_document)
              return;
            if (!error->empty()) {
              QMessageBox::warning(
                  this, tr("MTF Measurement Failed"),
                  tr("%1\n\nSelect one straight, isolated edge with clear "
                     "plateaus on both sides. Avoid dust, texture, multiple "
                     "edges, and edges parallel to the pixel grid.")
                      .arg(QString::fromStdString(*error)));
            } else {
              m_document->applySharedDocumentState(
                  updated, tr("Measure MTF from slanted-edge reference"));
            }
            m_sharpnessPanel->setMeasureMtfChecked(false);
            m_sharpnessPanel->setMeasureMtfEnabled(true);
            m_sharpnessPanel->updateUI();
            statusBar()->showMessage(
                tr("Slanted-edge reference — sharpness parameters are shared"));
          });

  QFuture<ParameterState> future = QtConcurrent::run(
      [scan, state, area, parameters, progress, error]() mutable {
        ParameterState updated = state;
        std::vector<colorscreen::slanted_edge_results> results;
        results.reserve(parameters.size());
        const colorscreen::int_image_area imageArea = {
            area.x(), area.y(), area.width(), area.height()};
        for (const auto &p : parameters) {
          colorscreen::slanted_edge_results result = colorscreen::slanted_edge_mtf(
              updated.rparams, *scan, imageArea, p, progress.get());
          if (!result.success) {
            *error = p.name + ": " +
                     (result.error.empty()
                          ? std::string("no usable single slanted edge was found")
                          : result.error);
            return updated;
          }
          results.push_back(std::move(result));
          progress->inc_progress();
        }
        for (auto &result : results)
          updated.rparams.sharpen.scanner_mtf.measurements.push_back(
              std::move(result.measurement));
        return updated;
      });
  watcher->setFuture(future);
}


/** Present the source document's full inspector in this detached ordinary view. */
void ImageViewWindow::claimDocumentInspector() {
  if (m_slantedEdgeReference || m_workspaceEmbedded || !m_document ||
      !m_documentInspectorHost || !m_documentInspectorDock ||
      !m_imageWidget || m_imageWidget->sharedImageData() !=
                            m_document->sharedImageData())
    return;

  QWidget *inspector = m_document->takeWorkspaceInspector();
  if (!inspector)
    return;

  QLayout *layout = m_documentInspectorHost->layout();
  if (layout && inspector->parentWidget() != m_documentInspectorHost) {
    inspector->setParent(m_documentInspectorHost);
    layout->addWidget(inspector);
  }
  m_document->setInspectorImageWidget(m_imageWidget);
  inspector->show();
  m_documentInspectorDock->show();
}

/** Release the borrowed document inspector without changing document state. */
void ImageViewWindow::releaseDocumentInspector() {
  if (m_slantedEdgeReference || !m_document || !m_documentInspectorHost)
    return;

  QWidget *inspector = m_document->workspaceInspectorWidget();
  if (inspector && m_documentInspectorHost->isAncestorOf(inspector))
    m_document->takeWorkspaceInspector();
  if (m_documentInspectorDock)
    m_documentInspectorDock->hide();
}

/** Close one secondary presentation and finalize its document only when last. */
void ImageViewWindow::closeEvent(QCloseEvent *event) {
  if (auto *application = dynamic_cast<ColorScreenApplication *>(
          QApplication::instance())) {
    if (!application->requestViewClose(this)) {
      event->ignore();
      return;
    }
  }
  QMainWindow::closeEvent(event);
}

/** Reclaim panels when this detached ordinary view becomes active. */
void ImageViewWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (event && event->type() == QEvent::WindowActivate && !m_workspaceEmbedded)
    claimDocumentInspector();
}

/** Remove standalone chrome while the view is hosted by WorkspaceWindow. */
void ImageViewWindow::prepareForWorkspaceEmbedding() {
  if (m_workspaceEmbedded)
    return;
  releaseDocumentInspector();
  if (m_documentInspectorDock)
    m_documentInspectorDock->hide();
  if (m_referenceInspectorDock && m_referenceInspector) {
    m_referenceInspector->setParent(nullptr);
    m_referenceInspectorDock->setWidget(nullptr);
    m_referenceInspectorDock->hide();
  }
  if (m_toolbar)
    m_toolbar->hide();
  if (menuBar())
    menuBar()->hide();
  if (statusBar())
    statusBar()->hide();
  m_workspaceEmbedded = true;
}

/** Restore standalone chrome after leaving WorkspaceWindow. */
void ImageViewWindow::restoreFromWorkspaceEmbedding() {
  if (!m_workspaceEmbedded)
    return;
  if (m_referenceInspectorDock && m_referenceInspector) {
    m_referenceInspector->setParent(nullptr);
    m_referenceInspectorDock->setWidget(m_referenceInspector);
    m_referenceInspectorDock->show();
  }
  if (m_toolbar)
    m_toolbar->show();
  if (menuBar())
    menuBar()->show();
  if (statusBar())
    statusBar()->show();
  m_workspaceEmbedded = false;

  // Detaching is an explicit ownership transition. Do not rely on the window
  // system to synthesize WindowActivate before the view receives its panels;
  // headless/offscreen platforms in particular may never send that event.
  claimDocumentInspector();
}
