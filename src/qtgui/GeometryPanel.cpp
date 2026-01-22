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

  addSeparator("Registration points");

  QCheckBox *showBox = new QCheckBox("Show registration points");
  m_form->addRow(showBox);
  
  QHBoxLayout *optLayout = new QHBoxLayout();
  QPushButton *optButton = new QPushButton("Optimize geometry");
  optButton->setObjectName("optimizeButton");
  QCheckBox *autoBtn = new QCheckBox("Auto");
  autoBtn->setObjectName("autoSolverBox");
  optLayout->addWidget(optButton);
  optLayout->addWidget(autoBtn);
  m_form->addRow(optLayout);

  connect(optButton, &QPushButton::clicked, this, [this, autoBtn]() {
      emit optimizeRequested(autoBtn->isChecked());
  });

  m_nonlinearBox = new QCheckBox("Nonlinear corrections");
  m_nonlinearBox->setObjectName("nonlinearBox");
  m_form->addRow(m_nonlinearBox);

  connect(m_nonlinearBox, &QCheckBox::toggled, this, &GeometryPanel::nonlinearToggled);
  
  // To make it easy for MainWindow to sync, let's give it an object name
  showBox->setObjectName("showRegistrationPointsBox");

  // Heatmap tolerance (using addSliderParameter via state)
  // Heatmap tolerance (manual slider, not in ParameterState)
  addSeparator("Heatmap Settings");
  m_heatmapToleranceSlider = new QSlider(Qt::Horizontal);
  m_heatmapToleranceSlider->setRange(0, 1000); // 0.0 to 1.0 mapped to 0-1000
  m_heatmapToleranceSlider->setValue(500); // Default 0.5
  m_heatmapToleranceSlider->setToolTip("Adjust heatmap color sensitivity (0.0 to 1.0)");
  connect(m_heatmapToleranceSlider, &QSlider::valueChanged, this, [this](int value) {
      if (m_deformationChart) {
          m_deformationChart->setHeatmapTolerance(value / 1000.0);
      }
  });

  QHBoxLayout *hmLayout = new QHBoxLayout();
  hmLayout->addWidget(new QLabel("Heatmap tolerance:"));
  hmLayout->addWidget(m_heatmapToleranceSlider);
  m_form->addRow(hmLayout);

  // Deformation Chart
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
  
  m_form->addRow(container);

  updateUI();
}

bool GeometryPanel::isAutoEnabled() const {
  QCheckBox *cb = findChild<QCheckBox *>("autoSolverBox");
  return cb && cb->isChecked();
}

bool GeometryPanel::isNonlinearEnabled() const {
  return m_nonlinearBox && m_nonlinearBox->isChecked();
}

void GeometryPanel::updateDeformationChart() {
  if (!m_deformationChart)
    return;

  // Get current state
  ParameterState state = m_stateGetter();
  
  // Sync tolerance from slider
  if (m_heatmapToleranceSlider) {
      m_deformationChart->setHeatmapTolerance(m_heatmapToleranceSlider->value() / 1000.0);
  }
  
  // Get scan image
  auto scan = m_imageGetter();
  if (!scan || scan->width <= 0 || scan->height <= 0) {
    m_deformationChart->clear();
    return;
  }

  // Create undeformed parameters by copying center and coordinates from current params
  colorscreen::scr_to_img_parameters undeformed;
  undeformed.center = state.scrToImg.center;
  undeformed.coordinate1 = state.scrToImg.coordinate1;
  undeformed.coordinate2 = state.scrToImg.coordinate2;
  
  // Set the deformation data
  m_deformationChart->setDeformationData(
      state.scrToImg,  // deformed (current parameters)
      undeformed,      // undeformed (linear only)
      scan->width,
      scan->height
  );
}

void GeometryPanel::reattachDeformationChart(QWidget *widget) {
    if (!widget) return;
    
    QWidget *detachable = createDetachableSection(
        "Deformation Visualization", widget,
        [this, widget](){ emit detachDeformationChartRequested(widget); });
    
    m_chartContainer->addWidget(detachable);
}
