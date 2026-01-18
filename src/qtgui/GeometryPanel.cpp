#include "GeometryPanel.h"
#include <QCheckBox>
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

  // Synchronization with external state is handled by the caller (MainWindow)
  // via signals/slots to keep it simple, or we could add a field to ParameterState.
  // Since the user asked for a checkbox "working same way as one in view menu",
  // and we already have boolean state in ImageWidget, we'll link them in MainWindow.
  
  // Actually, we can just export the checkbox or use signals.
  // Let's use a signal on this panel to notify when the checkbox is toggled.
  connect(showBox, &QCheckBox::toggled, this, [this](bool checked) {
      // We don't have a specific field in ParameterState for visibility yet,
      // so we use a custom signal or just let MainWindow handle it.
  });
  
  // To make it easy for MainWindow to sync, let's give it an object name
  showBox->setObjectName("showRegistrationPointsBox");

  updateUI();
}
