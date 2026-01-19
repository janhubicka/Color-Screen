#include "GeometryPanel.h"
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFormLayout>

GeometryPanel::GeometryPanel(StateGetter stateGetter, StateSetter stateSetter,
                             ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

GeometryPanel::~GeometryPanel() = default;

void GeometryPanel::setupUi() {
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

  updateUI();
}

bool GeometryPanel::isAutoEnabled() const {
  QCheckBox *cb = findChild<QCheckBox *>("autoSolverBox");
  return cb && cb->isChecked();
}

bool GeometryPanel::isNonlinearEnabled() const {
  return m_nonlinearBox && m_nonlinearBox->isChecked();
}
