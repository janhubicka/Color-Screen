#include "AdaptiveSharpeningDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVariant>
#include <QVBoxLayout>

AdaptiveSharpeningDialog::AdaptiveSharpeningDialog(const AdaptiveSharpeningParameters &initial,
                         bool physicalFocusAvailable,
                         bool focusInterpolationAvailable,
                         bool varyingStripWidths, bool hasRgb,
                         QWidget *parent)
    : QDialog(parent), m_physicalFocusAvailable(physicalFocusAvailable),
      m_focusInterpolationAvailable(focusInterpolationAvailable),
      m_varyingStripWidths(varyingStripWidths), m_hasRgb(hasRgb) {
  setWindowTitle(tr("Adaptive sharpening analysis"));
  setModal(true);
  setSizeGripEnabled(true);

  auto *mainLayout = new QVBoxLayout(this);
  auto *description = new QLabel(
      tr("Choose the parameters fitted at every sample and the sampling "
         "grids. Zero in an automatic dimension lets the library derive it "
         "from the image aspect ratio or the paired value."), this);
  description->setWordWrap(true);
  mainLayout->addWidget(description);

  auto *tabs = new QTabWidget(this);
  mainLayout->addWidget(tabs, 1);

  auto *fitGroup = new QGroupBox(tr("Parameters to optimize"), this);
  auto *fitForm = new QFormLayout(fitGroup);
  m_correctionCombo = new QComboBox(fitGroup);
  m_correctionCombo->addItem(
      tr("Scanner/camera defocus (or fallback blur diameter)"),
      QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_scanner_mtf_defocus)));
  m_correctionCombo->addItem(
      tr("Scanner/camera defocus per channel"),
      QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_scanner_mtf_channel_defocus)));
  m_correctionCombo->addItem(
      tr("Legacy screen blur"),
      QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_screen_blur)));
  m_correctionCombo->addItem(
      tr("Legacy screen blur per channel"),
      QVariant::fromValue(static_cast<qulonglong>(colorscreen::finetune_screen_channel_blurs)));
  const uint64_t correctionMask = colorscreen::finetune_screen_blur
      | colorscreen::finetune_screen_channel_blurs
      | colorscreen::finetune_scanner_mtf_defocus
      | colorscreen::finetune_scanner_mtf_channel_defocus;
  const uint64_t initialCorrection = initial.flags & correctionMask;
  for (int i = 0; i < m_correctionCombo->count(); ++i)
    if (m_correctionCombo->itemData(i).toULongLong() == initialCorrection) {
      m_correctionCombo->setCurrentIndex(i);
      break;
    }
  m_correctionCombo->setToolTip(
      tr("The correction table stores one scalar blur/focus family. "
         "Per-channel fits are currently reduced to their mean when stored."));
  fitForm->addRow(tr("Correction:"), m_correctionCombo);

  m_positionCheck = addCheckBox(
      fitForm, tr("Refine screen position"),
      initial.flags & colorscreen::finetune_position,
      tr("Refine the exact local screen phase/translation for every fit."));
  m_sigmaCheck = addCheckBox(
      fitForm, tr("Optimize residual MTF sigma"),
      initial.flags & colorscreen::finetune_scanner_mtf_sigma,
      tr("Fit the residual Gaussian component together with scanner/camera focus."));
  m_fogCheck = addCheckBox(
      fitForm, tr("Optimize fog / dark point"),
      initial.flags & colorscreen::finetune_fog,
      tr("Fit the local fog/dark offset as an auxiliary parameter."));
  m_monochromeCheck = addCheckBox(
      fitForm, tr("Use monochrome / IR channel"),
      initial.flags & colorscreen::finetune_bw,
      tr("Use the measured monochrome/infrared channel when present; otherwise "
         "finetune derives grayscale from RGB."));
  if (!m_hasRgb) {
    m_monochromeCheck->setChecked(true);
    m_monochromeCheck->setEnabled(false);
    m_monochromeCheck->setToolTip(
        tr("The loaded scan has no RGB channels, so monochrome fitting is "
           "mandatory."));
  }
  m_simulatedInfraredCheck = addCheckBox(
      fitForm, tr("Simulate infrared layer"),
      initial.flags & colorscreen::finetune_simulate_infrared,
      tr("Experimental RGB objective that estimates a neutral simulated-IR layer."));
  m_normalizeCheck = addCheckBox(
      fitForm, tr("Normalize RGB colors"),
      !(initial.flags & colorscreen::finetune_no_normalize),
      tr("Remove the approximately neutral image layer before registration."));
  m_dataCollectionCheck = addCheckBox(
      fitForm, tr("Use data collection for colors"),
      !(initial.flags & colorscreen::finetune_no_data_collection),
      tr("Use the fast patch-color estimate when it is applicable."));
  m_leastSquaresCheck = addCheckBox(
      fitForm, tr("Use least-squares color fit"),
      !(initial.flags & colorscreen::finetune_no_least_squares),
      tr("Use variable projection for linear screen colors when applicable."));
  m_optimizeStripWidthsCheck = addCheckBox(
      fitForm, tr("Optimize strip widths in coarse prepass"),
      initial.optimizeStripWidthsInPrepass,
      tr("For Dufay and other variable-strip screens, fit strip widths in "
         "the exact coarse prepass. Disable this to keep the strip widths "
         "already present in the rendering parameters (or process defaults)."));
  m_reoptimizeStripWidthsCheck = addCheckBox(
      fitForm, tr("Reoptimize strip widths in dense pass"),
      initial.reoptimizeStripWidths,
      tr("For Dufay and other variable-strip screens, fit strip widths again "
         "inside every dense sample instead of fixing the robust prepass values."));
  if (!m_varyingStripWidths) {
    m_optimizeStripWidthsCheck->setChecked(false);
    m_optimizeStripWidthsCheck->setEnabled(false);
    m_optimizeStripWidthsCheck->setToolTip(
        tr("The selected screen process has no variable strip widths."));
    m_reoptimizeStripWidthsCheck->setChecked(false);
    m_reoptimizeStripWidthsCheck->setEnabled(false);
    m_reoptimizeStripWidthsCheck->setToolTip(
        tr("The selected screen process has no variable strip widths."));
  }
  auto *fitTab = new QWidget(tabs);
  auto *fitTabLayout = new QVBoxLayout(fitTab);
  fitTabLayout->addWidget(fitGroup);
  fitTabLayout->addStretch();
  tabs->addTab(fitTab, tr("Fitting"));

  auto *gridGroup = new QGroupBox(tr("Sampling grids"), this);
  auto *gridForm = new QFormLayout(gridGroup);
  m_xStepsSpin = addAutomaticSpin(gridForm, tr("Correction columns:"),
                                  initial.xSteps, 1000,
                                  tr("Width of the final correction table."));
  m_yStepsSpin = addAutomaticSpin(gridForm, tr("Correction rows:"),
                                  initial.ySteps, 1000,
                                  tr("Height of the final correction table; automatic preserves image aspect ratio."));
  m_xSubstepsSpin = addAutomaticSpin(gridForm, tr("Samples per cell, X:"),
                                     initial.xSubsteps, 100,
                                     tr("Independent horizontal finetune samples reduced into each correction cell; automatic defaults to 5."));
  m_ySubstepsSpin = addAutomaticSpin(gridForm, tr("Samples per cell, Y:"),
                                     initial.ySubsteps, 100,
                                     tr("Independent vertical finetune samples reduced into each correction cell; automatic follows X or defaults to 5."));
  m_stripXStepsSpin = addAutomaticSpin(gridForm, tr("Coarse prepass columns:"),
                                       initial.stripXSteps, 1000,
                                       tr("Horizontal samples used by the exact coarse blur/focus and strip-width prepass."));
  m_stripYStepsSpin = addAutomaticSpin(gridForm, tr("Coarse prepass rows:"),
                                       initial.stripYSteps, 1000,
                                       tr("Vertical samples used by the coarse prepass; automatic preserves image aspect ratio."));
  auto *reductionGroup = new QGroupBox(tr("Robust reduction"), this);
  auto *reductionForm = new QFormLayout(reductionGroup);
  m_skipMinSpin = new QDoubleSpinBox(reductionGroup);
  m_skipMinSpin->setRange(0.0, 50.0);
  m_skipMinSpin->setDecimals(2);
  m_skipMinSpin->setSuffix(tr(" %"));
  m_skipMinSpin->setValue(initial.skipMin);
  m_skipMinSpin->setToolTip(tr("Discard this fraction from the low end before robust averaging."));
  reductionForm->addRow(tr("Skip low:"), m_skipMinSpin);
  m_skipMaxSpin = new QDoubleSpinBox(reductionGroup);
  m_skipMaxSpin->setRange(0.0, 50.0);
  m_skipMaxSpin->setDecimals(2);
  m_skipMaxSpin->setSuffix(tr(" %"));
  m_skipMaxSpin->setValue(initial.skipMax);
  m_skipMaxSpin->setToolTip(tr("Discard this fraction from the high end / worst-fit tail."));
  reductionForm->addRow(tr("Skip high:"), m_skipMaxSpin);
  m_minContrastSpin = new QDoubleSpinBox(reductionGroup);
  m_minContrastSpin->setRange(0.0, 100.0);
  m_minContrastSpin->setDecimals(8);
  m_minContrastSpin->setSingleStep(0.01);
  m_minContrastSpin->setSuffix(tr(" %"));
  m_minContrastSpin->setSpecialValueText(tr("disabled"));
  m_minContrastSpin->setValue(initial.minimumContrast * 100.0);
  m_minContrastSpin->setToolTip(
      tr("Reject a local fit when the fitted additive-screen patch modulation "
         "is below this threshold. Such a finite solution does not contain "
         "enough information to determine focus reliably. Zero disables "
         "only the contrast floor; numerical validity checks remain active."));
  reductionForm->addRow(tr("Minimum fitted contrast:"), m_minContrastSpin);
  m_toleranceSpin = new QDoubleSpinBox(reductionGroup);
  m_toleranceSpin->setRange(-1.0, 10.0);
  m_toleranceSpin->setDecimals(4);
  m_toleranceSpin->setSpecialValueText(tr("disabled"));
  m_toleranceSpin->setValue(initial.tolerance);
  m_toleranceSpin->setToolTip(tr("Maximum accepted robust correction range within a cell. -1 disables the check."));
  reductionForm->addRow(tr("Correction tolerance:"), m_toleranceSpin);
  auto *samplingTab = new QWidget(tabs);
  auto *samplingTabLayout = new QVBoxLayout(samplingTab);
  samplingTabLayout->addWidget(gridGroup);
  samplingTabLayout->addWidget(reductionGroup);
  samplingTabLayout->addStretch();
  tabs->addTab(samplingTab, tr("Sampling"));

  auto *focusGroup = new QGroupBox(tr("Blur/focus acceleration"), this);
  auto *focusForm = new QFormLayout(focusGroup);
  m_interpolateFocusCheck = addCheckBox(
      focusForm, tr("Interpolate cached focus nodes"), initial.interpolateFocus,
      tr("Use nonlinear exact focus nodes and interpolate intermediate "
         "dense-pass screens for physical defocus. The empirical fallback "
         "uses its fast exact analytical transfer because its objective is "
         "too multimodal for reliable interpolation. The coarse prepass and "
         "final selected solution remain exact."));
  m_focusMtfSpin = new QDoubleSpinBox(focusGroup);
  m_focusMtfSpin->setRange(0.001, 99.999);
  m_focusMtfSpin->setDecimals(3);
  m_focusMtfSpin->setSuffix(tr(" %"));
  m_focusMtfSpin->setValue(initial.focusMtfThreshold * 100.0);
  m_focusMtfSpin->setToolTip(tr("Stop the useful blur/focus range at the first point where the screen-frequency system MTF reaches this magnitude."));
  focusForm->addRow(tr("Minimum screen-frequency MTF:"), m_focusMtfSpin);
  m_focusNodesSpin = new QSpinBox(focusGroup);
  m_focusNodesSpin->setRange(2, 64);
  m_focusNodesSpin->setValue(initial.focusInterpolationNodes);
  m_focusNodesSpin->setToolTip(tr("Number of quadratically spaced exact blur/focus nodes, including both endpoints."));
  focusForm->addRow(tr("Exact focus nodes:"), m_focusNodesSpin);
  m_profileCheck = addCheckBox(
      focusForm, tr("Collect finetune profile"), initial.reportProfile,
      tr("Collect and print cache, FFT, objective and timing counters for this analysis."));
  auto *focusTab = new QWidget(tabs);
  auto *focusTabLayout = new QVBoxLayout(focusTab);
  focusTabLayout->addWidget(focusGroup);
  focusTabLayout->addStretch();
  tabs->addTab(focusTab, tr("Focus cache"));

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  mainLayout->addWidget(m_statusLabel);

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                   this);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttons);

  connect(m_correctionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateAvailability(); });
  connect(m_sigmaCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_optimizeStripWidthsCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_reoptimizeStripWidthsCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_monochromeCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_simulatedInfraredCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_interpolateFocusCheck, &QCheckBox::toggled, this,
          [this](bool) { updateAvailability(); });
  connect(m_skipMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double) { updateAvailability(); });
  connect(m_skipMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double) { updateAvailability(); });

  updateAvailability();
  resize(620, 620);
}

/** Return the settings currently selected by the user. */
AdaptiveSharpeningParameters AdaptiveSharpeningDialog::parameters() const {
  AdaptiveSharpeningParameters result;
  result.stripXSteps = m_stripXStepsSpin->value();
  result.stripYSteps = m_stripYStepsSpin->value();
  result.xSteps = m_xStepsSpin->value();
  result.ySteps = m_yStepsSpin->value();
  result.xSubsteps = m_xSubstepsSpin->value();
  result.ySubsteps = m_ySubstepsSpin->value();
  result.flags = m_correctionCombo->currentData().toULongLong();
  if (m_positionCheck->isChecked())
    result.flags |= colorscreen::finetune_position;
  if (m_sigmaCheck->isEnabled() && m_sigmaCheck->isChecked())
    result.flags |= colorscreen::finetune_scanner_mtf_sigma;
  if (m_fogCheck->isEnabled() && m_fogCheck->isChecked())
    result.flags |= colorscreen::finetune_fog;
  if (m_monochromeCheck->isChecked())
    result.flags |= colorscreen::finetune_bw;
  if (m_simulatedInfraredCheck->isChecked())
    result.flags |= colorscreen::finetune_simulate_infrared;
  if (!m_normalizeCheck->isChecked())
    result.flags |= colorscreen::finetune_no_normalize;
  if (!m_dataCollectionCheck->isChecked())
    result.flags |= colorscreen::finetune_no_data_collection;
  if (!m_leastSquaresCheck->isChecked())
    result.flags |= colorscreen::finetune_no_least_squares;
  result.optimizeStripWidthsInPrepass
      = m_optimizeStripWidthsCheck->isEnabled()
        && m_optimizeStripWidthsCheck->isChecked();
  result.reoptimizeStripWidths = m_reoptimizeStripWidthsCheck->isChecked();
  result.skipMin = m_skipMinSpin->value();
  result.skipMax = m_skipMaxSpin->value();
  result.tolerance = m_toleranceSpin->value();
  result.minimumContrast = m_minContrastSpin->value() / 100.0;
  result.reportProfile = m_profileCheck->isChecked();
  result.interpolateFocus = m_interpolateFocusCheck->isEnabled()
                            && m_interpolateFocusCheck->isChecked();
  result.focusMtfThreshold = m_focusMtfSpin->value() / 100.0;
  result.focusInterpolationNodes = m_focusNodesSpin->value();
  return result;
}

/** Add one check box row with VALUE and TOOLTIP. */
QCheckBox *AdaptiveSharpeningDialog::addCheckBox(QFormLayout *form, const QString &label, bool value,
                       const QString &tooltip) {
  auto *check = new QCheckBox(label, form->parentWidget());
  check->setChecked(value);
  check->setToolTip(tooltip);
  form->addRow(QString(), check);
  return check;
}

/** Add an integer row where zero means that the library derives the value. */
QSpinBox *AdaptiveSharpeningDialog::addAutomaticSpin(QFormLayout *form, const QString &label,
                           int value, int maximum,
                           const QString &tooltip) {
  auto *spin = new QSpinBox(form->parentWidget());
  spin->setRange(0, maximum);
  spin->setSpecialValueText(tr("automatic"));
  spin->setValue(value);
  spin->setToolTip(tooltip);
  form->addRow(label, spin);
  return spin;
}

/** Update controls whose validity depends on the selected fit parameters. */
void AdaptiveSharpeningDialog::updateAvailability() {
  const uint64_t correction = m_correctionCombo->currentData().toULongLong();
  const bool mtfCorrection = correction == colorscreen::finetune_scanner_mtf_defocus
      || correction == colorscreen::finetune_scanner_mtf_channel_defocus;
  if (!mtfCorrection && m_sigmaCheck->isChecked())
    m_sigmaCheck->setChecked(false);
  m_sigmaCheck->setEnabled(mtfCorrection);

  /* Simulated IR is an RGB objective and is distinct from measured IR/BW.
     It also disables normalization and data collection internally.  Reflect
     those implications explicitly so the dialog does not advertise ignored
     settings.  */
  const bool canSimulateInfrared = m_hasRgb && !m_monochromeCheck->isChecked();
  if (!canSimulateInfrared && m_simulatedInfraredCheck->isChecked())
    m_simulatedInfraredCheck->setChecked(false);
  m_simulatedInfraredCheck->setEnabled(canSimulateInfrared);
  if (m_simulatedInfraredCheck->isChecked()) {
    m_normalizeCheck->setChecked(false);
    m_dataCollectionCheck->setChecked(false);
  }

  /* The BW/IR path has no RGB color vector, so fog and RGB normalization
     are ignored by finetune_solver.  Keep their checked state while they
     are disabled so switching back to RGB restores the user's choices.  */
  const bool monochrome = m_monochromeCheck->isChecked();
  m_fogCheck->setEnabled(!monochrome);
  m_normalizeCheck->setEnabled(!monochrome
                               && !m_simulatedInfraredCheck->isChecked());
  m_dataCollectionCheck->setEnabled(!m_simulatedInfraredCheck->isChecked());

  const bool scalarInterpolatableFocus
      = correction == colorscreen::finetune_scanner_mtf_defocus
        && m_focusInterpolationAvailable;
  const bool interpolationCompatible
      = scalarInterpolatableFocus && !m_sigmaCheck->isChecked()
        && !m_reoptimizeStripWidthsCheck->isChecked();
  if (!interpolationCompatible && m_interpolateFocusCheck->isChecked())
    m_interpolateFocusCheck->setChecked(false);
  m_interpolateFocusCheck->setEnabled(interpolationCompatible);
  const bool interpolationActive = interpolationCompatible
                                   && m_interpolateFocusCheck->isChecked();
  m_focusMtfSpin->setEnabled(interpolationActive);
  m_focusNodesSpin->setEnabled(interpolationActive);

  QString status;
  if (m_skipMinSpin->value() + m_skipMaxSpin->value() >= 100.0)
    status = tr("Skip-low and skip-high must add to less than 100%.");
  else if (!m_physicalFocusAvailable
           && correction == colorscreen::finetune_scanner_mtf_defocus)
    status = tr("Physical capture metadata are incomplete: this correction "
                "mode uses the compact fallback blur diameter. The fallback "
                "is evaluated exactly using its fast analytical transfer; "
                "focus interpolation is disabled.");
  else if (!m_focusInterpolationAvailable
           && correction == colorscreen::finetune_scanner_mtf_defocus)
    status = tr("A measured MTF curve is active. Scalar residual blur can "
                "still be fitted, but cached blur/focus interpolation is "
                "disabled for measured transfer data.");
  else if (!m_varyingStripWidths)
    status = tr("The coarse prepass still estimates the global blur/focus; "
                "the selected process has no variable strip widths.");
  else if (m_optimizeStripWidthsCheck->isChecked())
    status = tr("The coarse prepass determines robust strip widths and the "
                "global blur/focus before the dense correction pass.");
  else if (m_reoptimizeStripWidthsCheck->isChecked())
    status = tr("The coarse prepass keeps the current strip widths; strip "
                "widths are fitted independently in every dense sample.");
  else
    status = tr("Strip widths are kept fixed at the current rendering values "
                "through both the coarse and dense passes.");
  m_statusLabel->setText(status);
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(
      m_skipMinSpin->value() + m_skipMaxSpin->value() < 100.0);
}
