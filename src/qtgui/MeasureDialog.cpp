#include "MeasureDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

MeasureDialog::MeasureDialog(double pixelDistance, double currentDpi, QWidget *parent)
    : QDialog(parent), m_pixelDistance(pixelDistance), m_resultDpi(currentDpi) {
  setWindowTitle(tr("Measure Distance"));

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  QFormLayout *formLayout = new QFormLayout();

  m_distanceSpin = new QDoubleSpinBox();
  m_distanceSpin->setRange(0.001, 1000000.0);
  m_distanceSpin->setDecimals(3);

  m_unitCombo = new QComboBox();
  m_unitCombo->addItem(tr("mm"), 25.4);
  m_unitCombo->addItem(tr("cm"), 2.54);
  m_unitCombo->addItem(tr("inches"), 1.0);
  m_unitCombo->addItem(tr("m"), 0.0254);

  // Start in millimetres when the existing resolution determines a distance.
  if (currentDpi > 0) {
    const double distanceInInches = m_pixelDistance / currentDpi;
    m_distanceSpin->setValue(distanceInInches * 25.4);
    m_unitCombo->setCurrentIndex(0);
  } else {
    m_distanceSpin->setValue(10.0);
    m_unitCombo->setCurrentIndex(0);
  }

  QHBoxLayout *distLayout = new QHBoxLayout();
  distLayout->addWidget(m_distanceSpin);
  distLayout->addWidget(m_unitCombo);
  formLayout->addRow(tr("Distance:"), distLayout);

  m_resultLabel = new QLabel();
  formLayout->addRow(tr("Resulting DPI:"), m_resultLabel);

  mainLayout->addLayout(formLayout);

  QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);

  connect(m_distanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MeasureDialog::updateResult);
  connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasureDialog::updateResult);

  updateResult();
}

void MeasureDialog::updateResult() {
  const double distance = m_distanceSpin->value();
  const double unitsPerInch = m_unitCombo->currentData().toDouble();

  if (distance > 0 && unitsPerInch > 0) {
    const double distanceInInches = distance / unitsPerInch;
    m_resultDpi = m_pixelDistance / distanceInInches;
    m_resultLabel->setText(tr("%1 PPI").arg(m_resultDpi, 0, 'f', 2));
  } else {
    m_resultLabel->setText(tr("Invalid"));
  }
}

double MeasureDialog::getResultDpi() const {
  return m_resultDpi;
}
