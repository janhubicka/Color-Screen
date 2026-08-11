#include "MTFFitDialog.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSize>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStyle>
#include <QVBoxLayout>

namespace {

/** Return the wavelength displayed initially for MEASUREMENT in PARAMETERS.  */
double effectiveMeasurementWavelength(
    const colorscreen::mtf_parameters &parameters,
    const colorscreen::mtf_measurement &measurement) {
  if (measurement.wavelength > 0)
    return measurement.wavelength;
  if (measurement.channel >= 0 && measurement.channel < 4
      && parameters.wavelengths[measurement.channel] > 0)
    return parameters.wavelengths[measurement.channel];
  return parameters.wavelength > 0 ? parameters.wavelength : 0.0;
}

/** Return a translated name for CHANNEL.  */
QString channelName(int channel) {
  switch (channel) {
  case 0:
    return QObject::tr("Red");
  case 1:
    return QObject::tr("Green");
  case 2:
    return QObject::tr("Blue");
  case 3:
    return QObject::tr("Infrared");
  default:
    return QObject::tr("Unknown");
  }
}

} // namespace

/** Construct an explicit MTF fitting dialog from PARAMETERS.  */
MTFFitDialog::MTFFitDialog(
    const colorscreen::mtf_parameters &parameters, QWidget *parent)
    : QDialog(parent), m_initialParameters(parameters) {
  setWindowTitle(tr("Fit measured MTF"));
  setModal(true);
  /* Keep the dialog resizable.  Fixed pixel dimensions are especially awkward
     with accessibility fonts and high-DPI display scaling.  The initial size is
     chosen from font metrics after every control has been constructed.  */
  setSizeGripEnabled(true);

  auto *mainLayout = new QVBoxLayout(this);
  auto *description = new QLabel(
      tr("Choose the physical diffraction model for normal calibrated scans. "
         "An unchecked parameter is fixed exactly at the displayed value; a "
         "checked parameter uses that value as its starting estimate. Zero is "
         "therefore no longer an implicit request to optimize."),
      this);
  description->setWordWrap(true);
  mainLayout->addWidget(description);

  auto *modelForm = new QFormLayout();
  m_modelCombo = new QComboBox(this);
  m_modelCombo->addItem(
      tr("Physical diffraction model"),
      static_cast<int>(colorscreen::mtf_model::physical_diffraction));
  m_modelCombo->addItem(
      tr("Empirical fallback model"),
      static_cast<int>(colorscreen::mtf_model::empirical_fallback));
  m_modelCombo->setToolTip(
      tr("The fallback model is intended only when calibrated scan geometry "
         "or wavelength metadata is unavailable."));
  colorscreen::mtf_model initialModel = parameters.model;
  if (initialModel == colorscreen::mtf_model::automatic_legacy)
    /* The physical model is the normal calibrated-capture workflow.  Show its
       missing metadata explicitly instead of silently steering an old project
       to the empirical backup merely because geometry has not yet been
       entered.  */
    initialModel = colorscreen::mtf_model::physical_diffraction;
  const int initialModelIndex = m_modelCombo->findData(
      static_cast<int>(initialModel));
  if (initialModelIndex >= 0)
    m_modelCombo->setCurrentIndex(initialModelIndex);
  modelForm->addRow(tr("Model:"), m_modelCombo);
  mainLayout->addLayout(modelForm);

  m_physicalMetadataGroup = new QGroupBox(tr("Known capture metadata"), this);
  auto *metadataForm = new QFormLayout(m_physicalMetadataGroup);
  m_scanDpiSpin = new QDoubleSpinBox(m_physicalMetadataGroup);
  m_scanDpiSpin->setRange(0.0, 100000.0);
  m_scanDpiSpin->setDecimals(3);
  m_scanDpiSpin->setSuffix(tr(" PPI"));
  m_scanDpiSpin->setSpecialValueText(tr("missing"));
  m_scanDpiSpin->setValue(parameters.scan_dpi);
  m_scanDpiSpin->setToolTip(
      tr("Object-space scan resolution. It is required metadata and is not "
         "estimated from one radial MTF curve."));
  metadataForm->addRow(tr("Scan resolution:"), m_scanDpiSpin);

  m_pixelPitchSpin = new QDoubleSpinBox(m_physicalMetadataGroup);
  m_pixelPitchSpin->setRange(0.0, 1000.0);
  m_pixelPitchSpin->setDecimals(4);
  m_pixelPitchSpin->setSuffix(tr(" µm"));
  m_pixelPitchSpin->setSpecialValueText(tr("missing"));
  m_pixelPitchSpin->setValue(parameters.pixel_pitch);
  m_pixelPitchSpin->setToolTip(
      tr("Camera sensor pixel pitch. For the Phase One sensor discussed here "
         "use 3.760 µm."));
  metadataForm->addRow(tr("Sensor pixel pitch:"), m_pixelPitchSpin);
  mainLayout->addWidget(m_physicalMetadataGroup);

  auto *parameterGroup = new QGroupBox(tr("Model parameters"), this);
  auto *parameterGrid = new QGridLayout(parameterGroup);
  parameterGrid->addWidget(new QLabel(tr("Parameter"), parameterGroup), 0, 0);
  parameterGrid->addWidget(new QLabel(tr("Value / initial estimate"),
                                      parameterGroup),
                           0, 1);
  parameterGrid->addWidget(new QLabel(tr("Optimize"), parameterGroup), 0, 2);

  m_fStopRow = addParameterRow(
      parameterGrid, 1, tr("Marked f-number"), 0.0, 128.0,
      parameters.f_stop, 3, QString(),
      tr("Known aperture should normally remain fixed. A missing value forces "
         "optimization on."));
  m_sigmaRow = addParameterRow(
      parameterGrid, 2, tr("Residual Gaussian sigma"), 0.0, 20.0,
      parameters.sigma, 5, tr(" px"),
      tr("Residual compact blur after diffraction and defocus. Zero is a valid "
         "fixed value."));
  m_defocusRow = addParameterRow(
      parameterGrid, 3, tr("Image-plane defocus"), 0.0, 20.0,
      parameters.defocus, 6, tr(" mm"),
      tr("One defocus value is fitted for each capture group. Zero is a valid "
         "in-focus value."));
  m_fillFactorRow = addParameterRow(
      parameterGrid, 4, tr("Sensor fill factor"), 0.0, 32.0,
      parameters.sensor_fill_factor, 5, QString(),
      tr("Zero disables the first-cut sensor-aperture MTF and is a valid fixed "
         "value."));
  m_haloFractionRow = addParameterRow(
      parameterGrid, 5, tr("Broad-halo fraction"), 0.0, 0.95,
      parameters.halo_fraction, 6, QString(),
      tr("Optional broad scattering component. It is optimized by default, but "
         "the optimizer may choose an exact zero fraction when no halo is "
         "supported by the measurements."));
  m_haloSigmaRow = addParameterRow(
      parameterGrid, 6, tr("Broad-halo radius"), 0.0, 256.0,
      parameters.halo_sigma, 5, tr(" px"),
      tr("Gaussian radius of the optional broad component."));
  m_blurDiameterRow = addParameterRow(
      parameterGrid, 7, tr("Fallback blur diameter"), 0.0, 64.0,
      parameters.blur_diameter, 5, tr(" px"),
      tr("Circular blur used only by the empirical fallback model. Zero is a "
         "valid fixed value."));

  m_fStopRow.value->setSpecialValueText(tr("missing"));
  m_sigmaRow.optimize->setChecked(true);
  m_defocusRow.optimize->setChecked(true);
  /* A zero halo fraction remains a valid fitted result, so trying the broad
     component by default does not force one into the physical model.  Both
     parameters must be free because the radius is otherwise undefined when an
     existing project stores the disabled value zero.  */
  m_haloFractionRow.optimize->setChecked(true);
  m_haloSigmaRow.optimize->setChecked(true);
  m_blurDiameterRow.optimize->setChecked(true);
  mainLayout->addWidget(parameterGroup);

  auto *measurementGroup = new QGroupBox(tr("Measured curves"), this);
  auto *measurementLayout = new QVBoxLayout(measurementGroup);
  auto *measurementHelp = new QLabel(
      tr("Each curve has its own authoritative wavelength. Channel labels do "
         "not disable wavelength editing. A zero wavelength forces its "
         "Optimize checkbox on. Oversampling, support and windowing are fixed "
         "when the edge is measured; remeasure the edge to change them."),
      measurementGroup);
  measurementHelp->setWordWrap(true);
  measurementLayout->addWidget(measurementHelp);

  m_measurementTable = new QTableWidget(
      static_cast<int>(parameters.measurements.size()), 6, measurementGroup);
  m_measurementTable->setHorizontalHeaderLabels(
      {tr("Use"), tr("Name"), tr("Channel"), tr("Wavelength"),
       tr("Optimize wavelength"), tr("Capture")});
  m_measurementTable->verticalHeader()->setVisible(false);
  m_measurementTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  m_measurementTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Stretch);
  for (int column = 2; column < 6; column++)
    m_measurementTable->horizontalHeader()->setSectionResizeMode(
        column, QHeaderView::ResizeToContents);
  m_measurementTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_measurementTable->setHorizontalScrollMode(
      QAbstractItemView::ScrollPerPixel);
  m_measurementTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_measurementTable->setSizeAdjustPolicy(
      QAbstractScrollArea::AdjustToContentsOnFirstShow);

  int capture = -1;
  m_measurementRows.reserve(parameters.measurements.size());
  for (size_t index = 0; index < parameters.measurements.size(); index++) {
    const colorscreen::mtf_measurement &measurement =
        parameters.measurements[index];
    if (index == 0 || !measurement.same_capture)
      capture++;

    MeasurementRow row;
    row.include = new QCheckBox(m_measurementTable);
    row.include->setChecked(true);
    row.include->setToolTip(tr("Include this curve in the objective."));
    m_measurementTable->setCellWidget(static_cast<int>(index), 0, row.include);

    auto *nameItem = new QTableWidgetItem(
        QString::fromStdString(measurement.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setToolTip(nameItem->text());
    m_measurementTable->setItem(static_cast<int>(index), 1, nameItem);

    auto *channelItem = new QTableWidgetItem(channelName(measurement.channel));
    channelItem->setFlags(channelItem->flags() & ~Qt::ItemIsEditable);
    channelItem->setToolTip(channelItem->text());
    m_measurementTable->setItem(static_cast<int>(index), 2, channelItem);

    row.wavelength = new QDoubleSpinBox(m_measurementTable);
    row.wavelength->setRange(0.0, 2000.0);
    row.wavelength->setDecimals(3);
    row.wavelength->setSuffix(tr(" nm"));
    row.wavelength->setSpecialValueText(tr("unknown"));
    row.wavelength->setValue(
        effectiveMeasurementWavelength(parameters, measurement));
    row.wavelength->setToolTip(
        tr("Wavelength associated with this measured edge."));
    m_measurementTable->setCellWidget(static_cast<int>(index), 3,
                                      row.wavelength);

    row.optimizeWavelength = new QCheckBox(m_measurementTable);
    row.optimizeWavelength->setChecked(row.wavelength->value() <= 0);
    row.optimizeWavelength->setToolTip(
        tr("Estimate this wavelength between 380 and 1000 nm. Keep known "
           "narrow-band values fixed."));
    m_measurementTable->setCellWidget(static_cast<int>(index), 4,
                                      row.optimizeWavelength);

    auto *captureItem = new QTableWidgetItem(QString::number(capture + 1));
    captureItem->setFlags(captureItem->flags() & ~Qt::ItemIsEditable);
    m_measurementTable->setItem(static_cast<int>(index), 5, captureItem);
    m_measurementRows.push_back(row);
  }
  measurementLayout->addWidget(m_measurementTable);
  mainLayout->addWidget(measurementGroup, 1);

  auto *solverGroup = new QGroupBox(tr("Numerical optimization"), this);
  auto *solverLayout = new QGridLayout(solverGroup);
  m_simplexCheck = new QCheckBox(tr("Derivative-free simplex"), solverGroup);
  m_simplexCheck->setChecked(true);
  m_simplexCheck->setToolTip(
      tr("Finds the basin reliably when the derivative of defocus vanishes at "
         "the in-focus starting point."));
  m_multifitCheck = new QCheckBox(tr("Least-squares refinement"), solverGroup);
  m_multifitCheck->setChecked(true);
  solverLayout->addWidget(m_simplexCheck, 0, 0);
  solverLayout->addWidget(m_multifitCheck, 0, 1);
  mainLayout->addWidget(solverGroup);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  mainLayout->addWidget(m_statusLabel);

  m_buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_fitButton = m_buttons->button(QDialogButtonBox::Ok);
  m_fitButton->setText(tr("Fit"));
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttons);

  auto update = [this]() { updateValidation(); };
  connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, update);
  for (QDoubleSpinBox *spin :
       {m_scanDpiSpin, m_pixelPitchSpin, m_fStopRow.value, m_sigmaRow.value,
        m_defocusRow.value, m_fillFactorRow.value, m_haloFractionRow.value,
        m_haloSigmaRow.value, m_blurDiameterRow.value})
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            update);
  for (QCheckBox *check :
       {m_fStopRow.optimize, m_sigmaRow.optimize, m_defocusRow.optimize,
        m_fillFactorRow.optimize, m_haloFractionRow.optimize,
        m_haloSigmaRow.optimize, m_blurDiameterRow.optimize, m_simplexCheck,
        m_multifitCheck})
    connect(check, &QCheckBox::toggled, this, update);
  for (const MeasurementRow &row : m_measurementRows) {
    connect(row.include, &QCheckBox::toggled, this, update);
    connect(row.wavelength,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            update);
    connect(row.optimizeWavelength, &QCheckBox::toggled, this, update);
  }

  updateValidation();
  updateMeasurementTableGeometry();
  resizeForCurrentFont();
}

/** Make every measurement row accommodate the current font and embedded
    widgets.  Qt's style default can otherwise remain at a small pixel height
    even after the application font is enlarged, clipping spin-box text and
    checkbox indicators.  */
void MTFFitDialog::updateMeasurementTableGeometry() {
  if (!m_measurementTable)
    return;

  const int frame = m_measurementTable->style()->pixelMetric(
      QStyle::PM_DefaultFrameWidth, nullptr, m_measurementTable);
  int minimumRowHeight = m_measurementTable->fontMetrics().lineSpacing()
                         + std::max(6, 2 * frame + 4);
  for (const MeasurementRow &row : m_measurementRows) {
    minimumRowHeight = std::max(minimumRowHeight,
                               row.include->sizeHint().height() + 2 * frame + 4);
    minimumRowHeight = std::max(
        minimumRowHeight, row.wavelength->sizeHint().height() + 2 * frame + 4);
    minimumRowHeight = std::max(
        minimumRowHeight,
        row.optimizeWavelength->sizeHint().height() + 2 * frame + 4);
  }

  QHeaderView *verticalHeader = m_measurementTable->verticalHeader();
  verticalHeader->setMinimumSectionSize(minimumRowHeight);
  verticalHeader->setDefaultSectionSize(minimumRowHeight);
  verticalHeader->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_measurementTable->resizeRowsToContents();
  for (int row = 0; row < m_measurementTable->rowCount(); row++)
    if (m_measurementTable->rowHeight(row) < minimumRowHeight)
      m_measurementTable->setRowHeight(row, minimumRowHeight);

  QHeaderView *horizontalHeader = m_measurementTable->horizontalHeader();
  horizontalHeader->setMinimumHeight(
      std::max(horizontalHeader->minimumHeight(),
               horizontalHeader->sizeHint().height()));
}

/** Resize the completed dialog using font-relative dimensions, while keeping
    it within the available screen.  This preserves the familiar normal-font
    size and gives large-font users enough width for table headers and form
    labels without imposing an unresizable fixed geometry.  */
void MTFFitDialog::resizeForCurrentFont() {
  if (layout())
    layout()->activate();

  const QFontMetrics metrics(font());
  const int emWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
  const int lineHeight = std::max(1, metrics.lineSpacing());
  QSize requested(std::max(sizeHint().width(), 76 * emWidth),
                  std::max(minimumSizeHint().height(), 38 * lineHeight));

  if (QScreen *currentScreen = screen()) {
    const QSize available = currentScreen->availableGeometry().size();
    const int margin = 2 * lineHeight;
    requested.setWidth(
        std::min(requested.width(), std::max(320, available.width() - margin)));
    requested.setHeight(std::min(requested.height(),
                                 std::max(240, available.height() - margin)));
  }
  resize(requested);
}

/** Create one scalar parameter row in LAYOUT at ROW.  */
MTFFitDialog::ParameterRow MTFFitDialog::addParameterRow(
    QGridLayout *layout, int row, const QString &name, double minimum,
    double maximum, double value, int decimals, const QString &suffix,
    const QString &tooltip) {
  ParameterRow result;
  result.label = new QLabel(name, this);
  result.value = new QDoubleSpinBox(this);
  result.value->setRange(minimum, maximum);
  result.value->setDecimals(decimals);
  result.value->setValue(value);
  result.value->setSuffix(suffix);
  result.value->setToolTip(tooltip);
  result.optimize = new QCheckBox(this);
  result.optimize->setToolTip(
      tr("Checked: optimize from the displayed starting value. Unchecked: "
         "keep the displayed value fixed exactly."));
  layout->addWidget(result.label, row, 0);
  layout->addWidget(result.value, row, 1);
  layout->addWidget(result.optimize, row, 2, Qt::AlignHCenter);
  return result;
}

/** Return model values and edited per-measurement wavelengths.  */
colorscreen::mtf_parameters MTFFitDialog::parameters() const {
  colorscreen::mtf_parameters result = m_initialParameters;
  result.model = static_cast<colorscreen::mtf_model>(
      m_modelCombo->currentData().toInt());
  result.scan_dpi = m_scanDpiSpin->value();
  result.pixel_pitch = m_pixelPitchSpin->value();
  result.f_stop = m_fStopRow.value->value();
  result.sigma = m_sigmaRow.value->value();
  result.defocus = m_defocusRow.value->value();
  result.sensor_fill_factor = m_fillFactorRow.value->value();
  result.halo_fraction = m_haloFractionRow.value->value();
  result.halo_sigma = m_haloSigmaRow.value->value();
  result.blur_diameter = m_blurDiameterRow.value->value();
  for (size_t index = 0; index < result.measurements.size(); index++)
    result.measurements[index].wavelength =
        m_measurementRows[index].wavelength->value();
  return result;
}

/** Return the explicit free-variable selection represented by the dialog.  */
colorscreen::mtf_estimation_options MTFFitDialog::options() const {
  colorscreen::mtf_estimation_options result;
  result.model = static_cast<colorscreen::mtf_model>(
      m_modelCombo->currentData().toInt());
  const bool physical =
      result.model == colorscreen::mtf_model::physical_diffraction;
  result.optimize_sigma = m_sigmaRow.optimize->isChecked();
  result.optimize_f_stop = physical && m_fStopRow.optimize->isChecked();
  result.optimize_defocus = physical && m_defocusRow.optimize->isChecked();
  result.optimize_sensor_fill_factor =
      physical && m_fillFactorRow.optimize->isChecked();
  result.optimize_halo_fraction =
      physical && m_haloFractionRow.optimize->isChecked();
  result.optimize_halo_sigma =
      physical && m_haloSigmaRow.optimize->isChecked();
  result.optimize_blur_diameter =
      !physical && m_blurDiameterRow.optimize->isChecked();
  result.include_measurements.reserve(m_measurementRows.size());
  result.optimize_measurement_wavelengths.reserve(m_measurementRows.size());
  for (const MeasurementRow &row : m_measurementRows) {
    result.include_measurements.push_back(row.include->isChecked());
    result.optimize_measurement_wavelengths.push_back(
        physical && row.optimizeWavelength->isChecked());
  }
  return result;
}

/** Return numerical solver flags selected in the dialog.  */
int MTFFitDialog::estimationFlags() const {
  int result = 0;
  if (m_simplexCheck->isChecked())
    result |= colorscreen::mtf_parameters::estimate_use_nmsimplex;
  if (m_multifitCheck->isChecked())
    result |= colorscreen::mtf_parameters::estimate_use_multifit;
  return result;
}

/** Show controls relevant to the currently selected model.  */
void MTFFitDialog::updateModelVisibility() {
  const bool physical =
      static_cast<colorscreen::mtf_model>(
          m_modelCombo->currentData().toInt())
      == colorscreen::mtf_model::physical_diffraction;
  m_physicalMetadataGroup->setVisible(physical);
  for (ParameterRow *row :
       {&m_fStopRow, &m_defocusRow, &m_fillFactorRow, &m_haloFractionRow,
        &m_haloSigmaRow}) {
    row->label->setVisible(physical);
    row->value->setVisible(physical);
    row->optimize->setVisible(physical);
  }
  m_blurDiameterRow.label->setVisible(!physical);
  m_blurDiameterRow.value->setVisible(!physical);
  m_blurDiameterRow.optimize->setVisible(!physical);
  m_measurementTable->setColumnHidden(3, !physical);
  m_measurementTable->setColumnHidden(4, !physical);
}

/** Enforce mandatory optimization choices and display library validation.  */
void MTFFitDialog::updateValidation() {
  updateModelVisibility();
  const bool physical =
      static_cast<colorscreen::mtf_model>(
          m_modelCombo->currentData().toInt())
      == colorscreen::mtf_model::physical_diffraction;

  const bool missingFStop = physical && m_fStopRow.value->value() <= 0;
  {
    QSignalBlocker blocker(m_fStopRow.optimize);
    if (missingFStop)
      m_fStopRow.optimize->setChecked(true);
    m_fStopRow.optimize->setEnabled(!missingFStop);
  }
  m_fStopRow.optimize->setToolTip(
      missingFStop
          ? tr("A missing f-number must be optimized and cannot be fixed at "
               "zero.")
          : tr("Uncheck to keep the known marked f-number fixed."));

  const bool haloCanBePresent =
      physical
      && (m_haloFractionRow.optimize->isChecked()
          || m_haloFractionRow.value->value() > 0);
  {
    QSignalBlocker blocker(m_haloSigmaRow.optimize);
    if (!haloCanBePresent)
      m_haloSigmaRow.optimize->setChecked(false);
    else if (m_haloSigmaRow.value->value() <= 0)
      m_haloSigmaRow.optimize->setChecked(true);
    m_haloSigmaRow.optimize->setEnabled(
        haloCanBePresent && m_haloSigmaRow.value->value() > 0);
  }
  if (!haloCanBePresent)
    m_haloSigmaRow.optimize->setToolTip(
        tr("The halo radius is inactive while the halo fraction is fixed at "
           "zero."));
  else if (m_haloSigmaRow.value->value() <= 0)
    m_haloSigmaRow.optimize->setToolTip(
        tr("A missing halo radius must be optimized and cannot be fixed at "
           "zero."));
  else
    m_haloSigmaRow.optimize->setToolTip(
        tr("Uncheck to keep the displayed halo radius fixed."));

  for (const MeasurementRow &row : m_measurementRows) {
    const bool included = row.include->isChecked();
    const bool missingWavelength =
        physical && included && row.wavelength->value() <= 0;
    QSignalBlocker blocker(row.optimizeWavelength);
    if (missingWavelength)
      row.optimizeWavelength->setChecked(true);
    row.optimizeWavelength->setEnabled(physical && included
                                       && !missingWavelength);
    row.wavelength->setEnabled(physical && included);
    row.optimizeWavelength->setToolTip(
        missingWavelength
            ? tr("A missing wavelength must be optimized and cannot be fixed "
                 "at zero.")
            : tr("Uncheck to keep this measured wavelength fixed."));
  }

  const char *error = nullptr;
  colorscreen::mtf_parameters fitParameters = parameters();
  colorscreen::mtf_estimation_options fitOptions = options();
  bool valid = colorscreen::mtf_parameters::validate_estimation_options(
      fitParameters, fitOptions, &error);
  bool hasFreeParameter = fitOptions.optimize_sigma
                          || fitOptions.optimize_f_stop
                          || fitOptions.optimize_defocus
                          || fitOptions.optimize_sensor_fill_factor
                          || fitOptions.optimize_halo_fraction
                          || fitOptions.optimize_halo_sigma
                          || fitOptions.optimize_blur_diameter;
  for (size_t index = 0;
       index < fitOptions.optimize_measurement_wavelengths.size(); index++)
    hasFreeParameter = hasFreeParameter
                       || (fitOptions.include_measurement_p(index)
                           && fitOptions
                                  .optimize_measurement_wavelength_p(index));
  if (valid && !hasFreeParameter) {
    valid = false;
    error = "select at least one parameter to optimize";
  } else if (valid && !m_simplexCheck->isChecked()
             && !m_multifitCheck->isChecked()) {
    valid = false;
    error = "select at least one numerical optimizer";
  }

  m_fitButton->setEnabled(valid);
  if (valid) {
    m_statusLabel->setText(
        tr("Ready. Known values will remain fixed exactly; checked values will "
           "be optimized."));
    m_statusLabel->setStyleSheet(QString());
  } else {
    m_statusLabel->setText(
        tr("Cannot fit: %1").arg(QString::fromUtf8(error ? error
                                                        : "invalid settings")));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #b00020;"));
  }
}
