#include "SlantedEdgeDialog.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>

/** Construct the slanted-edge setup dialog from DEFAULTS.
    HAS_PREVIOUS_MEASUREMENT determines whether SAME CAPTURE is meaningful.  */
SlantedEdgeDialog::SlantedEdgeDialog(
    const colorscreen::slanted_edge_parameters &defaults,
    bool hasPreviousMeasurement, bool hasRgb, bool hasInfrared,
    QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Measure slanted-edge MTF"));
  setModal(true);
  /* Let accessibility fonts determine the geometry instead of relying on the
     platform's small default dialog size.  */
  setSizeGripEnabled(true);

  auto *mainLayout = new QVBoxLayout(this);
  auto *description = new QLabel(
      hasRgb
          ? tr("For RGB scans the default is to measure each native scanner "
               "channel from the selected edge. The image layer can still be "
               "measured directly when its RGB mixture is itself the quantity "
               "of interest.")
          : tr("This scan has one monochrome capture channel. The image layer "
               "is measured directly."),
      this);
  description->setWordWrap(true);
  mainLayout->addWidget(description);

  auto *form = new QFormLayout();
  /* Long translated labels may occupy their own line rather than compressing
     the editor column until spin-box text is clipped.  */
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  mainLayout->addLayout(form);

  m_nameEdit = new QLineEdit(
      QString::fromStdString(defaults.name.empty() ? "Slanted edge MTF"
                                                   : defaults.name),
      this);
  m_nameEdit->setToolTip(tr("Name shown in the MTF chart and fit dialog."));
  form->addRow(tr("Measurement name:"), m_nameEdit);

  m_sourceCombo = new QComboBox(this);
  if (hasRgb)
    m_sourceCombo->addItem(
        hasInfrared ? tr("Native RGB + infrared channels")
                    : tr("Native RGB channels"),
        true);
  m_sourceCombo->addItem(tr("Image layer"), false);
  m_sourceCombo->setCurrentIndex(0);
  m_sourceCombo->setToolTip(
      tr("Native channels are analyzed independently from the same ROI. "
         "Image layer measures the current grayscale/infrared or RGB mixture."));
  auto *sourceLabel = new QLabel(tr("Measure:"), this);
  form->addRow(sourceLabel, m_sourceCombo);
  sourceLabel->setVisible(hasRgb);
  m_sourceCombo->setVisible(hasRgb);

  m_wavelengthSpin = new QDoubleSpinBox(this);
  m_wavelengthSpin->setRange(0.0, 2000.0);
  m_wavelengthSpin->setDecimals(3);
  m_wavelengthSpin->setSingleStep(1.0);
  m_wavelengthSpin->setSuffix(tr(" nm"));
  m_wavelengthSpin->setSpecialValueText(tr("unknown"));
  m_wavelengthSpin->setValue(defaults.wavelength);
  m_wavelengthSpin->setToolTip(
      tr("Optional wavelength for an image-layer measurement. Native-channel "
         "measurements use the per-channel wavelengths from capture/MTF "
         "settings; an unknown wavelength can be optimized later."));
  form->addRow(tr("Image-layer wavelength:"), m_wavelengthSpin);

  m_sameCaptureCheck = new QCheckBox(
      tr("Same capture as previous measurement"), this);
  m_sameCaptureCheck->setChecked(hasPreviousMeasurement
                                 && defaults.same_capture);
  m_sameCaptureCheck->setEnabled(hasPreviousMeasurement);
  m_sameCaptureCheck->setToolTip(
      tr("Use this for several channels measured from the same exposure."));
  form->addRow(tr("Capture group:"), m_sameCaptureCheck);

  m_oversamplingSpin = new QSpinBox(this);
  m_oversamplingSpin->setRange(2, 64);
  m_oversamplingSpin->setValue(
      defaults.oversampling >= 2 && defaults.oversampling <= 64
          ? defaults.oversampling
          : 10);
  m_oversamplingSpin->setSuffix(tr("×"));
  m_oversamplingSpin->setToolTip(
      tr("Supersampling of the edge-spread function. 10× is the Color-Screen "
         "default; 4× is useful for traditional ISO-style comparisons."));
  form->addRow(tr("ESF oversampling:"), m_oversamplingSpin);

  m_halfWidthSpin = new QDoubleSpinBox(this);
  m_halfWidthSpin->setRange(0.0, 100000.0);
  m_halfWidthSpin->setDecimals(2);
  m_halfWidthSpin->setSuffix(tr(" px"));
  m_halfWidthSpin->setSpecialValueText(tr("full ROI"));
  m_halfWidthSpin->setValue(defaults.lsf_half_width);
  m_halfWidthSpin->setToolTip(
      tr("Half-width of the retained line-spread function. Zero preserves the "
         "full Color-Screen measurement; about 20 px is useful when comparing "
         "with QuickMTF."));
  form->addRow(tr("LSF half-width:"), m_halfWidthSpin);

  m_windowCombo = new QComboBox(this);
  m_windowCombo->addItem(tr("Hann"),
                         colorscreen::slanted_edge_parameters::window_hann);
  m_windowCombo->addItem(
      tr("Hamming"), colorscreen::slanted_edge_parameters::window_hamming);
  m_windowCombo->addItem(
      tr("Rectangular"),
      colorscreen::slanted_edge_parameters::window_rectangular);
  int windowIndex = m_windowCombo->findData(defaults.window);
  m_windowCombo->setCurrentIndex(windowIndex >= 0 ? windowIndex : 0);
  m_windowCombo->setToolTip(
      tr("Window applied to the retained line-spread function before its "
         "Fourier transform."));
  form->addRow(tr("LSF window:"), m_windowCombo);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  mainLayout->addWidget(m_statusLabel);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_acceptButton = buttons->button(QDialogButtonBox::Ok);
  m_acceptButton->setText(tr("Select edge area"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_wavelengthSpin,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this]() { updateValidation(); });
  connect(m_sourceCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this]() { updateValidation(); });
  mainLayout->addWidget(buttons);
  updateValidation();

  /* Choose a font-relative initial width and cap it at the current screen.  The
     form remains resizable and can wrap labels when the available display is
     narrower than the preferred size.  */
  mainLayout->activate();
  const QFontMetrics metrics(font());
  const int emWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
  QSize requested(std::max(sizeHint().width(), 46 * emWidth),
                  sizeHint().height());
  if (QScreen *currentScreen = screen()) {
    const QSize available = currentScreen->availableGeometry().size();
    const int margin = 2 * std::max(1, metrics.lineSpacing());
    requested.setWidth(
        std::min(requested.width(), std::max(320, available.width() - margin)));
    requested.setHeight(std::min(requested.height(),
                                 std::max(240, available.height() - margin)));
  }
  resize(requested);
}

/** Update source-dependent controls.  Wavelength is intentionally optional:
    native channels can obtain it from capture metadata and a genuinely mixed
    image layer may not have one physically meaningful wavelength.  */
void SlantedEdgeDialog::updateValidation() {
  const bool nativeChannels = measureNativeChannels();
  m_wavelengthSpin->setEnabled(!nativeChannels);
  m_acceptButton->setEnabled(true);
  m_statusLabel->setText(
      nativeChannels
          ? tr("One curve will be produced for each native channel. All curves "
               "will be marked as belonging to the same capture.")
          : tr("The current image layer will be measured directly."));
}

/** Return the measurement metadata and numerical controls entered by the
    user.  */
colorscreen::slanted_edge_parameters SlantedEdgeDialog::parameters() const {
  colorscreen::slanted_edge_parameters result;
  result.name = m_nameEdit->text().trimmed().toStdString();
  if (result.name.empty())
    result.name = "Slanted edge MTF";
  result.channel = -1;
  result.wavelength = m_wavelengthSpin->value();
  result.same_capture = m_sameCaptureCheck->isEnabled()
                        && m_sameCaptureCheck->isChecked();
  result.oversampling = m_oversamplingSpin->value();
  result.lsf_half_width = m_halfWidthSpin->value();
  result.window = static_cast<colorscreen::slanted_edge_parameters::window_type>(
      m_windowCombo->currentData().toInt());
  return result;
}

/** Return true if the dialog requests the default per-channel measurement.  */
bool SlantedEdgeDialog::measureNativeChannels() const {
  return m_sourceCombo->currentData().toBool();
}
