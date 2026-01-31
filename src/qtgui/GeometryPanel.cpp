#include "GeometryPanel.h"
#include "DeformationChartWidget.h"
#include "FinetuneImagesPanel.h"
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"

GeometryPanel::GeometryPanel(StateGetter stateGetter, StateSetter stateSetter,
                             ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

GeometryPanel::~GeometryPanel() = default;

void GeometryPanel::setupUi() {
  m_layout->setContentsMargins(0, 0, 0, 0);

  auto addToPanel = [this](auto* item) {
      if (m_currentGroupForm) m_currentGroupForm->addRow(item);
      else m_form->addRow(item);
  };

  addSeparator("Registration points");

  std::map<int, QString> pretty_scanner_type_names = {
      {colorscreen::fixed_lens, "Fixed Lens"},
      {colorscreen::fixed_lens_sensor_move_horisontally, "Fixed Lens (Sensor moves horizontally)"},
      {colorscreen::fixed_lens_sensor_move_vertically, "Fixed Lens (Sensor moves vertically)"},
      {colorscreen::lens_move_horisontally, "Lens moves horizontally"},
      {colorscreen::lens_move_vertically, "Lens moves vertically"}
  };

  QCheckBox *showBox = new QCheckBox("Show registration points");
  addToPanel(showBox);
  
  // To make it easy for MainWindow to sync, let's give it an object name
  showBox->setObjectName("showRegistrationPointsBox");
  
  m_exaggerateSliderContainer = addSlider("Exaggerate", 1.0, 10000.0, 100.0, 1, "x", "", 200.0,
            [this](double v) {
                emit exaggerateChanged(v);
            }, 1.0, true);

  m_maxArrowLengthSliderContainer = addSlider("Max arrow length", 1.0, 1000.0, 1.0, 0, "px", "", 100.0,
            [this](double v) {
                emit maxArrowLengthChanged(v);
            });

  connect(showBox, &QCheckBox::toggled, this, [this](bool checked){
      if (m_exaggerateSliderContainer) m_exaggerateSliderContainer->setEnabled(checked);
      if (m_maxArrowLengthSliderContainer) m_maxArrowLengthSliderContainer->setEnabled(checked);
  });

  // Initial state sync
  m_exaggerateSliderContainer->setEnabled(showBox->isChecked());
  m_maxArrowLengthSliderContainer->setEnabled(showBox->isChecked());

  addSlider("Heatmap tolerance", 0.0, 1.0, 1000.0, 3, "", "", 0.1,
            [this](double v) {
                if (m_deformationChart) m_deformationChart->setHeatmapTolerance(v);
                if (m_lensChart) m_lensChart->setHeatmapTolerance(v);
                if (m_perspectiveChart) m_perspectiveChart->setHeatmapTolerance(v);
                if (m_nonlinearChart) m_nonlinearChart->setHeatmapTolerance(v);
                emit heatmapToleranceChanged(v);
            });

  addSeparator("Optimization");

  QCheckBox *autoBtn = addCheckboxParameter("Auto optimize",
      [this](const ParameterState &){ return isAutoEnabled(); },
      [](ParameterState &, bool){ /* Handled by checkbox toggle signal */ });
  autoBtn->setObjectName("autoSolverBox");

  addButtonParameter("Optimization", "Optimize geometry", [this, autoBtn]() {
      emit optimizeRequested(autoBtn->isChecked());
  });

  connect(autoBtn, &QCheckBox::toggled, this, [this](bool checked){
      if (checked) emit optimizeRequested(true);
  });

  auto triggerIfAuto = [this, autoBtn]() {
      if (autoBtn->isChecked()) emit optimizeRequested(true);
  };

  QCheckBox *lensCb = addCheckboxWithReset("Optimize lens correction",
      [](const ParameterState &s){ return s.solver.optimize_lens; },
      [](ParameterState &s, bool v){ s.solver.optimize_lens = v; },
      [](ParameterState &s){ s.scrToImg.lens_correction = colorscreen::lens_warp_correction_parameters(); });
  connect(lensCb, &QCheckBox::toggled, this, triggerIfAuto);

  QCheckBox *tiltCb = addCheckboxWithReset("Optimize tilt",
      [](const ParameterState &s){ return s.solver.optimize_tilt; },
      [](ParameterState &s, bool v){ s.solver.optimize_tilt = v; },
      [](ParameterState &s){ s.scrToImg.tilt_x = 0; s.scrToImg.tilt_y = 0; });
  connect(tiltCb, &QCheckBox::toggled, this, triggerIfAuto);

  QCheckBox *nlCb = addCheckboxParameter("Nonlinear corrections",
      [this](const ParameterState &s){
          return m_nonlinearEnabled || (s.scrToImg.mesh_trans != nullptr);
      },
      [this](ParameterState &, bool v){
          m_nonlinearEnabled = v;
      });
  nlCb->setObjectName("nonlinearBox");
  connect(nlCb, &QCheckBox::toggled, this, &GeometryPanel::nonlinearToggled);

  addEnumParameter("Scanner/camera geometry", pretty_scanner_type_names,
      [](const ParameterState &s){ return (int)s.scrToImg.scanner_type; },
      [](ParameterState &s, int v){ s.scrToImg.scanner_type = (colorscreen::scanner_type)v; });

  // Ensure Finetune widget is separate from Optimization group
  endGroup();

  // Finetune Diagnostic Images
  m_finetuneImagesPanel = new FinetuneImagesPanel();
  
  QWidget *finetuneWrapper = new QWidget();
  QVBoxLayout *finetuneWrapperLayout = new QVBoxLayout(finetuneWrapper);
  finetuneWrapperLayout->setContentsMargins(0, 0, 0, 0);
  finetuneWrapperLayout->addWidget(m_finetuneImagesPanel);
  
  QWidget *finetuneDetachable = createDetachableSection(
      "Finetune Diagnostic Images", finetuneWrapper,
      [this, finetuneWrapper](){
        emit detachFinetuneImagesRequested(finetuneWrapper);
      });
  
  QWidget *finetuneContainer = new QWidget();
  m_finetuneImagesContainer = new QVBoxLayout(finetuneContainer);
  m_finetuneImagesContainer->setContentsMargins(0, 0, 0, 0);
  m_finetuneImagesContainer->addWidget(finetuneDetachable);
  
  // Initially hide until first finetune
  finetuneContainer->hide();
  
  addToPanel(finetuneContainer);

  QToolButton* visBtn = addSeparator("Visualization");
  connect(visBtn, &QToolButton::toggled, this, [this](bool checked){
      if (checked) updateDeformationChart();
  });
  
  auto setupChart = [this, addToPanel](DeformationChartWidget*& chart, QVBoxLayout*& containerLayout, 
                           const QString& title, auto detachSignal) {
      chart = new DeformationChartWidget();
      connect(chart, &DeformationChartWidget::clicked, this, &GeometryPanel::centerOnRequested);
      QWidget *wrapper = new QWidget();
      QVBoxLayout *wl = new QVBoxLayout(wrapper);
      wl->setContentsMargins(0, 0, 0, 0);
      wl->addWidget(chart);
      
      QWidget *detachable = createDetachableSection(title, wrapper, [this, wrapper, detachSignal](){
          emit (this->*detachSignal)(wrapper);
      });
      
      QWidget *container = new QWidget();
      containerLayout = new QVBoxLayout(container);
      containerLayout->setContentsMargins(0,0,0,0);
      containerLayout->addWidget(detachable);
      addToPanel(container);
  };

  setupChart(m_lensChart, m_lensChartContainer, "Lens Correction", &GeometryPanel::detachLensChartRequested);
  setupChart(m_perspectiveChart, m_perspectiveChartContainer, "Perspective", &GeometryPanel::detachPerspectiveChartRequested);
  setupChart(m_nonlinearChart, m_nonlinearChartContainer, "Nonlinear transformation", &GeometryPanel::detachNonlinearChartRequested);

  // Existing Deformation Chart
  m_deformationChart = new DeformationChartWidget();
  connect(m_deformationChart, &DeformationChartWidget::clicked, this, &GeometryPanel::centerOnRequested);

  // Wrapper for chart
  QWidget *chartWrapper = new QWidget();
  QVBoxLayout *wrapperLayout = new QVBoxLayout(chartWrapper);
  wrapperLayout->setContentsMargins(0, 0, 0, 0);
  wrapperLayout->addWidget(m_deformationChart);
  
  // Detachable section
  QWidget *detachable = createDetachableSection(
      "Deformation Visualization", chartWrapper, 
      [this, chartWrapper](){
         emit detachDeformationChartRequested(chartWrapper);
      });

  // Container to allow reattaching
  QWidget *container = new QWidget();
  m_chartContainer = new QVBoxLayout(container);
  m_chartContainer->setContentsMargins(0, 0, 0, 0);
  m_chartContainer->addWidget(detachable);
  
  addToPanel(container);

  updateUI();
}

bool GeometryPanel::isAutoEnabled() const {
  QCheckBox *cb = findChild<QCheckBox *>("autoSolverBox");
  return cb && cb->isChecked();
}

bool GeometryPanel::isNonlinearEnabled() const {
  if (m_nonlinearEnabled) return true;
  ParameterState s = m_stateGetter();
  return s.scrToImg.mesh_trans != nullptr;
}

void GeometryPanel::updateDeformationChart() {
  if (!m_deformationChart)
    return;

  // Get current state
  ParameterState state = m_stateGetter();
  
  // Sync tolerance from slider
  double tol = 0.5;
  if (m_heatmapToleranceSlider) {
      tol = m_heatmapToleranceSlider->value() / 1000.0;
  }
  if (m_deformationChart) m_deformationChart->setHeatmapTolerance(tol);
  if (m_lensChart) m_lensChart->setHeatmapTolerance(tol);
  if (m_perspectiveChart) m_perspectiveChart->setHeatmapTolerance(tol);
  if (m_nonlinearChart) m_nonlinearChart->setHeatmapTolerance(tol);
  
  // Get scan image
  auto scan = m_imageGetter();
  bool hasScan = (scan && scan->width > 0 && scan->height > 0);

  // Update visibility based on content
  bool showLens = hasScan && !state.scrToImg.lens_correction.is_noop();
  bool showPerspective = hasScan && (std::abs(state.scrToImg.tilt_x) > 1e-6 || std::abs(state.scrToImg.tilt_y) > 1e-6);
  bool showNonlinear = hasScan && (state.scrToImg.mesh_trans != nullptr);
  bool showFinal = hasScan && (state.scrToImg.type != colorscreen::Random);

  // Using parentWidget() of the layout to get the container widget added to the form
  if (m_lensChartContainer && m_lensChartContainer->parentWidget())
      m_lensChartContainer->parentWidget()->setVisible(showLens);
  
  if (m_perspectiveChartContainer && m_perspectiveChartContainer->parentWidget())
      m_perspectiveChartContainer->parentWidget()->setVisible(showPerspective);

  if (m_nonlinearChartContainer && m_nonlinearChartContainer->parentWidget())
      m_nonlinearChartContainer->parentWidget()->setVisible(showNonlinear);
      
  if (m_chartContainer && m_chartContainer->parentWidget())
      m_chartContainer->parentWidget()->setVisible(showFinal);
  
  if (!hasScan) {
    if(m_deformationChart) m_deformationChart->clear();
    if(m_lensChart) m_lensChart->clear();
    if(m_perspectiveChart) m_perspectiveChart->clear();
    if(m_nonlinearChart) m_nonlinearChart->clear();
    return;
  }
  colorscreen::int_image_area crop = state.rparams.get_scan_crop(scan->width, scan->height);
  int w = crop.width;
  int h = crop.height;
  int ox = crop.x;
  int oy = crop.y;
  int fw = scan->width;
  int fh = scan->height;

  // Create undeformed parameters by copying center and coordinates from current params
  colorscreen::scr_to_img_parameters p0;
  p0.center = state.scrToImg.center;
  p0.coordinate1 = state.scrToImg.coordinate1;
  p0.coordinate2 = state.scrToImg.coordinate2;
  p0.scanner_type = state.scrToImg.scanner_type;

  bool mirror = state.rparams.scan_mirror;
  int rotation = state.rparams.scan_rotation;

  // 1. Lens Chart: Undeformed vs Lens
  colorscreen::scr_to_img_parameters p1 = p0;
  p1.lens_correction = state.scrToImg.lens_correction;
  if (m_lensChart) m_lensChart->setDeformationData(p1, p0, w, h, mirror, rotation, ox, oy, fw, fh);

  // 2. Perspective Chart: Lens vs Perspective
  colorscreen::scr_to_img_parameters p2 = p1;
  p2.tilt_x = state.scrToImg.tilt_x;
  p2.tilt_y = state.scrToImg.tilt_y;
  p2.projection_distance = state.scrToImg.projection_distance;
  if (m_perspectiveChart) m_perspectiveChart->setDeformationData(p2, p1, w, h, mirror, rotation, ox, oy, fw, fh);

  // 3. Nonlinear Chart: Perspective vs Nonlinear
  colorscreen::scr_to_img_parameters p3 = p2;
  p3.mesh_trans = state.scrToImg.mesh_trans;
  if (m_nonlinearChart) m_nonlinearChart->setDeformationData(p3, p2, w, h, mirror, rotation, ox, oy, fw, fh);
  
  // 4. Final: Undeformed vs Current
  if (m_deformationChart)
    m_deformationChart->setDeformationData(state.scrToImg, p0, w, h, mirror, rotation, ox, oy, fw, fh);
}

void GeometryPanel::reattachDeformationChart(QWidget *widget) {
    if (!widget) return;
    
    QWidget *detachable = createDetachableSection(
        "Deformation Visualization", widget,
        [this, widget](){ emit detachDeformationChartRequested(widget); });
    
    m_chartContainer->addWidget(detachable);
}

void GeometryPanel::reattachLensChart(QWidget *widget) {
    if (!widget) return;
    QWidget *detachable = createDetachableSection("Lens Correction", widget, [this, widget](){ emit detachLensChartRequested(widget); });
    m_lensChartContainer->addWidget(detachable);
}

void GeometryPanel::reattachPerspectiveChart(QWidget *widget) {
    if (!widget) return;
    QWidget *detachable = createDetachableSection("Perspective", widget, [this, widget](){ emit detachPerspectiveChartRequested(widget); });
    m_perspectiveChartContainer->addWidget(detachable);
}

void GeometryPanel::reattachNonlinearChart(QWidget *widget) {
    if (!widget) return;
    QWidget *detachable = createDetachableSection("Nonlinear transformation", widget, [this, widget](){ emit detachNonlinearChartRequested(widget); });
    m_nonlinearChartContainer->addWidget(detachable);
}

void GeometryPanel::updateFinetuneImages(const colorscreen::finetune_result& result) {
    if (!m_finetuneImagesPanel) return;
    
    m_finetuneImagesPanel->setFinetuneResult(result);
    
    // Show the container if it was hidden
    if (m_finetuneImagesContainer && m_finetuneImagesContainer->parentWidget()) {
        m_finetuneImagesContainer->parentWidget()->show();
    }
}

void GeometryPanel::reattachFinetuneImages(QWidget *widget) {
    if (!widget) return;
    QWidget *detachable = createDetachableSection("Finetune Diagnostic Images", widget, [this, widget](){ emit detachFinetuneImagesRequested(widget); });
    m_finetuneImagesContainer->addWidget(detachable);
}
