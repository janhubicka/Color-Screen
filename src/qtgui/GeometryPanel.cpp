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
  QCheckBox *autoBtn = new QCheckBox("Auto");
  optLayout->addWidget(optButton);
  optLayout->addWidget(autoBtn);
  m_form->addRow(optLayout);

  connect(optButton, &QPushButton::clicked, this, [this, autoBtn]() {
      emit optimizeRequested(autoBtn->isChecked());
  });

  QCheckBox *nonLinearBox = new QCheckBox("Nonlinear corrections");
  nonLinearBox->setObjectName("nonLinearBox");
  m_form->addRow(nonLinearBox);

  connect(nonLinearBox, &QCheckBox::toggled, this, &GeometryPanel::nonlinearToggled);
  
  // To make it easy for MainWindow to sync, let's give it an object name
  showBox->setObjectName("showRegistrationPointsBox");

  updateUI();
}
