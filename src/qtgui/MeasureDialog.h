#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QLabel;

class MeasureDialog : public QDialog {
  Q_OBJECT
public:
  MeasureDialog(double pixelDistance, double currentDpi, QWidget *parent = nullptr);

  double getResultDpi() const;

private slots:
  void updateResult();

private:
  double m_pixelDistance;
  QDoubleSpinBox *m_distanceSpin;
  QComboBox *m_unitCombo;
  QLabel *m_resultLabel;
  double m_resultDpi;
};
