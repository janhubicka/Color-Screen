#pragma once

#include "AdaptiveSharpeningParameters.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;
class QString;

/** Configure one adaptive sharpening/focus analysis run.

    Operational settings remain outside ParameterState because they describe
    one analysis request rather than persistent rendering state. */
class AdaptiveSharpeningDialog final : public QDialog {
public:
  AdaptiveSharpeningDialog(const AdaptiveSharpeningParameters &initial,
                           bool physicalFocusAvailable,
                           bool focusInterpolationAvailable,
                           bool varyingStripWidths, bool hasRgb,
                           QWidget *parent = nullptr);

  /** Return the settings currently selected by the user. */
  AdaptiveSharpeningParameters parameters() const;

private:
  /** Add one check box row with VALUE and TOOLTIP. */
  QCheckBox *addCheckBox(QFormLayout *form, const QString &label, bool value,
                         const QString &tooltip);

  /** Add an integer row where zero means that the library derives the value. */
  QSpinBox *addAutomaticSpin(QFormLayout *form, const QString &label,
                             int value, int maximum,
                             const QString &tooltip);

  /** Update controls whose validity depends on the selected fit parameters. */
  void updateAvailability();

  bool m_physicalFocusAvailable = false;
  bool m_focusInterpolationAvailable = false;
  bool m_varyingStripWidths = false;
  bool m_hasRgb = false;
  QComboBox *m_correctionCombo = nullptr;
  QCheckBox *m_positionCheck = nullptr;
  QCheckBox *m_sigmaCheck = nullptr;
  QCheckBox *m_fogCheck = nullptr;
  QCheckBox *m_monochromeCheck = nullptr;
  QCheckBox *m_simulatedInfraredCheck = nullptr;
  QCheckBox *m_normalizeCheck = nullptr;
  QCheckBox *m_dataCollectionCheck = nullptr;
  QCheckBox *m_leastSquaresCheck = nullptr;
  QSpinBox *m_xStepsSpin = nullptr;
  QSpinBox *m_yStepsSpin = nullptr;
  QSpinBox *m_xSubstepsSpin = nullptr;
  QSpinBox *m_ySubstepsSpin = nullptr;
  QSpinBox *m_stripXStepsSpin = nullptr;
  QSpinBox *m_stripYStepsSpin = nullptr;
  QCheckBox *m_optimizeStripWidthsCheck = nullptr;
  QCheckBox *m_reoptimizeStripWidthsCheck = nullptr;
  QDoubleSpinBox *m_skipMinSpin = nullptr;
  QDoubleSpinBox *m_skipMaxSpin = nullptr;
  QDoubleSpinBox *m_minContrastSpin = nullptr;
  QDoubleSpinBox *m_toleranceSpin = nullptr;
  QCheckBox *m_interpolateFocusCheck = nullptr;
  QDoubleSpinBox *m_focusMtfSpin = nullptr;
  QSpinBox *m_focusNodesSpin = nullptr;
  QCheckBox *m_profileCheck = nullptr;
  QLabel *m_statusLabel = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
};
