#include "ImageViewWindow.h"

#include "ImageWidget.h"
#include "MainWindow.h"
#include "ColorScreenApplication.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVariant>

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
      close();
    });
  }
  refreshFromDocument();
}

/** Return the document whose state this secondary view follows. */
MainWindow *ImageViewWindow::sourceDocument() const { return m_document.data(); }

/** Build the minimal controls expected for an independent image view. */
void ImageViewWindow::setupUi() {
  m_imageWidget = new ImageWidget(this);
  m_imageWidget->setInteractionMode(ImageWidget::PanMode);
  setCentralWidget(m_imageWidget);

  m_toolbar = addToolBar(tr("View"));
  m_toolbar->setObjectName(QStringLiteral("ImageViewToolbar"));
  m_toolbar->setMovable(false);

  m_toolbar->addWidget(new QLabel(tr("Mode: "), m_toolbar));
  m_modeComboBox = new QComboBox(m_toolbar);
  m_modeComboBox->setMinimumWidth(170);
  m_toolbar->addWidget(m_modeComboBox);
  connect(m_modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ImageViewWindow::onModeChanged);

  m_colorCheckBox = new QCheckBox(tr("Color"), m_toolbar);
  m_toolbar->addWidget(m_colorCheckBox);
  connect(m_colorCheckBox, &QCheckBox::toggled, this,
          &ImageViewWindow::onColorChanged);

  m_toolbar->addSeparator();
  QAction *zoomIn = m_toolbar->addAction(tr("Zoom In"));
  zoomIn->setShortcut(QKeySequence::ZoomIn);
  connect(zoomIn, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomBy(1.25); });
  QAction *zoomOut = m_toolbar->addAction(tr("Zoom Out"));
  zoomOut->setShortcut(QKeySequence::ZoomOut);
  connect(zoomOut, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomBy(0.8); });
  QAction *zoom100 = m_toolbar->addAction(tr("Zoom 1:1"));
  zoom100->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  connect(zoom100, &QAction::triggered, m_imageWidget,
          [this]() { m_imageWidget->smoothZoomTo(1.0); });
  QAction *fit = m_toolbar->addAction(tr("Fit"));
  fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  connect(fit, &QAction::triggered, m_imageWidget, &ImageWidget::smoothFitToView);

  QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
  QAction *closeView = fileMenu->addAction(tr("&Close View"));
  closeView->setShortcut(QKeySequence::Close);
  connect(closeView, &QAction::triggered, this, &QWidget::close);

  QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
  viewMenu->addAction(zoomIn);
  viewMenu->addAction(zoomOut);
  viewMenu->addAction(zoom100);
  viewMenu->addAction(fit);

  QMenu *windowMenu = menuBar()->addMenu(tr("&Window"));
  connect(windowMenu, &QMenu::aboutToShow, this, [this, windowMenu]() {
    if (auto *application =
            dynamic_cast<ColorScreenApplication *>(QApplication::instance()))
      application->populateWindowMenu(windowMenu, m_document.data());
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

  statusBar()->showMessage(tr("Secondary view — document edits are shared"));
  resize(1100, 800);
}

/** Refresh copied shared state without disturbing view-local mode/zoom/pan. */
void ImageViewWindow::refreshFromDocument() {
  if (!m_document)
    return;

  const std::shared_ptr<colorscreen::image_data> scan =
      m_document->sharedImageData();
  const bool imageChanged = scan != m_scan;
  m_scan = scan;

  const ParameterState state = m_document->documentStateSnapshot();
  m_rparams = state.rparams;
  m_detectParams = state.detect;
  m_scrToImgParams = state.scrToImg;
  m_solverParams = state.solver;

  rebuildModeList();
  updateColorControl();
  updateImageParameters(imageChanged);

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
                                  QString::fromUtf8(prop.help),
                                  Qt::ToolTipRole);
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

/** Update whether the view-local IR/RGB switch is meaningful. */
void ImageViewWindow::updateColorControl() {
  using namespace colorscreen;
  const bool hasRgb = m_scan && m_scan->has_rgb();
  const render_type_property &prop =
      render_type_properties[static_cast<int>(m_renderTypeParams.type)];
  const bool supportsSwitch =
      prop.flags & render_type_property::SUPPORTS_IR_RGB_SWITCH;

  m_colorCheckBox->blockSignals(true);
  m_colorCheckBox->setVisible(hasRgb);
  m_colorCheckBox->setEnabled(hasRgb && supportsSwitch);
  m_colorCheckBox->setChecked(hasRgb && m_renderTypeParams.color);
  m_colorCheckBox->blockSignals(false);
}

/** Initialize or update ImageWidget with shared document snapshots. */
void ImageViewWindow::updateImageParameters(bool imageChanged) {
  if (!m_scan)
    return;
  if (imageChanged) {
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                            &m_detectParams, &m_renderTypeParams,
                            &m_solverParams);
    m_imageWidget->fitToView();
  } else {
    m_imageWidget->updateParameters(&m_rparams, &m_scrToImgParams,
                                    &m_detectParams, &m_renderTypeParams,
                                    &m_solverParams);
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
  updateColorControl();
  updateImageParameters(false);
}

/** Change the IR/RGB choice only for this view. */
void ImageViewWindow::onColorChanged(bool checked) {
  m_renderTypeParams.color = checked;
  updateImageParameters(false);
}

/** Programmatically select TYPE in this view. */
bool ImageViewWindow::setRenderType(colorscreen::render_type_t type) {
  const int index = m_modeComboBox->findData(static_cast<int>(type));
  if (index < 0)
    return false;
  m_modeComboBox->setCurrentIndex(index);
  return m_renderTypeParams.type == type;
}
