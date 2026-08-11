#pragma once

#include "../libcolorscreen/include/mtf-parameters.h"
#include <QDialog>
#include <QString>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

/** Collect explicit physical-MTF fitting values and free-variable choices.
    Values and optimization checkboxes are deliberately independent, so zero
    can be fixed for sigma, defocus, halo fraction, or fallback blur.  */
class MTFFitDialog : public QDialog {
public:
  /** Construct a fit dialog initialized from PARAMETERS.  */
  explicit MTFFitDialog(const colorscreen::mtf_parameters &parameters,
                        QWidget *parent = nullptr);

  /** Return model values and edited per-measurement wavelengths.  */
  colorscreen::mtf_parameters parameters() const;

  /** Return the explicit selection of fitted and fixed values.  */
  colorscreen::mtf_estimation_options options() const;

  /** Return numerical-solver flags selected in the dialog.  */
  int estimationFlags() const;

private:
  /** Widgets representing one scalar model value and its Optimize checkbox.  */
  struct ParameterRow {
    QLabel *label = nullptr;
    QDoubleSpinBox *value = nullptr;
    QCheckBox *optimize = nullptr;
  };

  /** Widgets representing controls associated with one measured curve.  */
  struct MeasurementRow {
    QCheckBox *include = nullptr;
    QDoubleSpinBox *wavelength = nullptr;
    QCheckBox *optimizeWavelength = nullptr;
  };

  /** Create one row in the parameter grid.  */
  ParameterRow addParameterRow(class QGridLayout *layout, int row,
                               const QString &name, double minimum,
                               double maximum, double value, int decimals,
                               const QString &suffix,
                               const QString &tooltip);

  /** Show controls relevant to the selected model and hide the others.  */
  void updateModelVisibility();

  /** Enforce mandatory optimization choices and update the Fit button.  */
  void updateValidation();

  /** Make table rows tall enough for the active application font and style.  */
  void updateMeasurementTableGeometry();

  /** Choose a resizable initial dialog size derived from the active font.  */
  void resizeForCurrentFont();

  colorscreen::mtf_parameters m_initialParameters;
  QComboBox *m_modelCombo = nullptr;
  QGroupBox *m_physicalMetadataGroup = nullptr;
  QDoubleSpinBox *m_scanDpiSpin = nullptr;
  QDoubleSpinBox *m_pixelPitchSpin = nullptr;
  ParameterRow m_fStopRow;
  ParameterRow m_sigmaRow;
  ParameterRow m_defocusRow;
  ParameterRow m_fillFactorRow;
  ParameterRow m_haloFractionRow;
  ParameterRow m_haloSigmaRow;
  ParameterRow m_blurDiameterRow;
  QTableWidget *m_measurementTable = nullptr;
  std::vector<MeasurementRow> m_measurementRows;
  QCheckBox *m_simplexCheck = nullptr;
  QCheckBox *m_multifitCheck = nullptr;
  QLabel *m_statusLabel = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QPushButton *m_fitButton = nullptr;
};
