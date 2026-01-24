#include "GeometryPanel.h"
#include "DeformationChartWidget.h"
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>

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
  
  // Heatmap tolerance (manual slider, not in ParameterState)
  // Removed separator "Heatmap Settings" to keep it under Registration Points
  m_heatmapToleranceSlider = new QSlider(Qt::Horizontal);
  m_heatmapToleranceSlider->setRange(0, 1000); // 0.0 to 1.0 mapped to 0-1000
  m_heatmapToleranceSlider->setValue(500); // Default 0.5
  m_heatmapToleranceSlider->setToolTip("Adjust heatmap color sensitivity (0.0 to 1.0)");
  connect(m_heatmapToleranceSlider, &QSlider::valueChanged, this, [this](int value) {
      double tol = value / 1000.0;
      if (m_deformationChart) m_deformationChart->setHeatmapTolerance(tol);
      if (m_lensChart) m_lensChart->setHeatmapTolerance(tol);
      if (m_perspectiveChart) m_perspectiveChart->setHeatmapTolerance(tol);
      if (m_nonlinearChart) m_nonlinearChart->setHeatmapTolerance(tol);
  });

  QHBoxLayout *hmLayout = new QHBoxLayout();
  hmLayout->addWidget(new QLabel("Heatmap tolerance:"));
  hmLayout->addWidget(m_heatmapToleranceSlider);
  addToPanel(hmLayout);

  addSeparator("Optimization");

  QHBoxLayout *optLayout = new QHBoxLayout();
  QPushButton *optButton = new QPushButton("Optimize geometry");
  optButton->setObjectName("optimizeButton");
  QCheckBox *autoBtn = new QCheckBox("Auto");
  autoBtn->setObjectName("autoSolverBox");
  optLayout->addWidget(optButton);
  optLayout->addWidget(autoBtn);
  addToPanel(optLayout);

  connect(autoBtn, &QCheckBox::toggled, this, [this](bool checked){
      if (checked) emit optimizeRequested(true);
  });

  auto triggerIfAuto = [this, autoBtn]() {
      if (autoBtn->isChecked()) emit optimizeRequested(true);
  };

  connect(optButton, &QPushButton::clicked, this, [this, autoBtn]() {
      emit optimizeRequested(autoBtn->isChecked());
  });

  QCheckBox *lensCb = addCheckboxParameter("Optimize lens correction",
      [](const ParameterState &s){ return s.solver.optimize_lens; },
      [](ParameterState &s, bool v){ s.solver.optimize_lens = v; });
  connect(lensCb, &QCheckBox::toggled, this, triggerIfAuto);

  QCheckBox *tiltCb = addCheckboxParameter("Optimize tilt",
      [](const ParameterState &s){ return s.solver.optimize_tilt; },
      [](ParameterState &s, bool v){ s.solver.optimize_tilt = v; });
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

  QToolButton* visBtn = addSeparator("Visualization");
  connect(visBtn, &QToolButton::toggled, this, [this](bool checked){
      if (checked) updateDeformationChart();
  });
  
  auto setupChart = [this, addToPanel](DeformationChartWidget*& chart, QVBoxLayout*& containerLayout, 
                           const QString& title, auto detachSignal) {
      chart = new DeformationChartWidget();
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
  int w = scan->width;
  int h = scan->height;

  // Create undeformed parameters by copying center and coordinates from current params
  colorscreen::scr_to_img_parameters p0;
  p0.center = state.scrToImg.center;
  p0.coordinate1 = state.scrToImg.coordinate1;

  p0.coordinate2 = state.scrToImg.coordinate2;

  
  bool mirror = state.rparams.scan_mirror;
  int rotation = state.rparams.scan_rotation;

  // 1. Lens Chart: Compare Undef -> Undef+Lens
  colorscreen::scr_to_img_parameters p1 = p0;
  p1.lens_correction = state.scrToImg.lens_correction;
  if (m_lensChart) m_lensChart->setDeformationData(p1, p0, w, h, mirror, rotation);

  // 2. Perspective Chart: Compare p1 -> p1+Perspective
  colorscreen::scr_to_img_parameters p2 = p1;
  p2.tilt_x = state.scrToImg.tilt_x;
  p2.tilt_y = state.scrToImg.tilt_y;
  p2.projection_distance = state.scrToImg.projection_distance;
  if (m_perspectiveChart) m_perspectiveChart->setDeformationData(p2, p1, w, h, mirror, rotation);

  // 3. Nonlinear Chart: Compare p2 -> p2+Mesh
  colorscreen::scr_to_img_parameters p3 = p2;
  p3.mesh_trans = state.scrToImg.mesh_trans;
  if (m_nonlinearChart) m_nonlinearChart->setDeformationData(p3, p2, w, h, mirror, rotation);
  
  // 4. Final: Compare p0 -> Final (all)
  if (m_deformationChart)
    m_deformationChart->setDeformationData(state.scrToImg, p0, w, h, mirror, rotation);
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
