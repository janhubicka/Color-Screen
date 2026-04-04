#include "RenderDialog.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

using namespace colorscreen;

// ── Scale <-> slider mapping (log scale 0.01 .. 20) ──────────────────────────
static int scaleToSlider(double s) {
  double t = (std::log(s) - std::log(0.01)) / (std::log(20.0) - std::log(0.01));
  return (int)std::round(std::clamp(t, 0.0, 1.0) * 1000);
}
static double sliderToScale(int v) {
  double t = v / 1000.0;
  return std::exp(std::log(0.01) + t * (std::log(20.0) - std::log(0.01)));
}

// ── Helper: create a (spinbox + slider) row ───────────────────────────────────
static void makeScaleRow(QFormLayout *form, const QString &label,
                         QDoubleSpinBox **spinOut, QSlider **sliderOut,
                         QWidget *parent)
{
  auto *container = new QWidget(parent);
  auto *hlay = new QHBoxLayout(container);
  hlay->setContentsMargins(0, 0, 0, 0);
  hlay->setSpacing(6);

  auto *spin = new QDoubleSpinBox(container);
  spin->setRange(0.01, 20.0);
  spin->setDecimals(3);
  spin->setSingleStep(0.05);
  spin->setValue(1.0);
  spin->setFixedWidth(90);
  hlay->addWidget(spin);

  auto *slider = new QSlider(Qt::Horizontal, container);
  slider->setRange(0, 1000);
  slider->setValue(scaleToSlider(1.0));
  hlay->addWidget(slider, 1);

  form->addRow(label, container);
  *spinOut   = spin;
  *sliderOut = slider;
}

// ── Constructor ───────────────────────────────────────────────────────────────
RenderDialog::RenderDialog(
    const render_type_parameters &rtparams,
    const render_parameters &rparams,
    const scr_to_img_parameters &scrParams,
    const image_data *scan,
    bool isDng,
    QWidget *parent)
  : QDialog(parent), m_rtparams(rtparams), m_scrParams(scrParams), m_scan(scan)
{
  setWindowTitle(tr("Render Settings"));
  setMinimumWidth(460);

  auto *mainLayout = new QVBoxLayout(this);

  // ── Mode ──────────────────────────────────────────────────────────────────
  auto *topForm = new QFormLayout;
  mainLayout->addLayout(topForm);

  m_modeCombo = new QComboBox(this);
  for (int i = 0; i < render_type_max; ++i) {
    const render_type_property &prop = render_type_properties[i];
    bool show = true;
    if (prop.flags & render_type_property::HIDE_IN_GUI)
      show = false;
    if ((prop.flags & render_type_property::NEEDS_SCR_TO_IMG) &&
        scrParams.type == colorscreen::Random)
      show = false;
    if ((prop.flags & render_type_property::NEEDS_RGB) &&
        (!scan || !scan->rgbdata))
      show = false;
    if ((prop.flags & render_type_property::NEEDS_CORRECTION_PROFILE)
	&& !rparams.has_correction_profile ())
      show = false;
    if (show)
      {
        m_modeCombo->addItem(prop.pretty_name, QVariant(i));
	if (prop.help)
	  m_modeCombo->setItemData (i, QString::fromUtf8 (prop.help), Qt::ToolTipRole);
      }
  }
  int idx = m_modeCombo->findData((int)rtparams.type);
  if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
  topForm->addRow(tr("Mode:"), m_modeCombo);

  // ── Non-DNG options ───────────────────────────────────────────────────────
  m_nonDngWidget = new QWidget(this);
  auto *nonDngForm = new QFormLayout(m_nonDngWidget);
  nonDngForm->setContentsMargins(0, 0, 0, 0);

  m_profileCombo = new QComboBox(m_nonDngWidget);
  for (int i = 0; i < (int)render_parameters::output_profile_max; ++i)
    m_profileCombo->addItem(render_parameters::output_profile_names[i], i);
  m_profileCombo->setCurrentIndex((int)rparams.output_profile);
  nonDngForm->addRow(tr("Output profile:"), m_profileCombo);

  m_hdrCheck = new QCheckBox(tr("HDR output"), m_nonDngWidget);
  nonDngForm->addRow(QString(), m_hdrCheck);

  m_depthCombo = new QComboBox(m_nonDngWidget);
  m_depthCombo->addItem("8-bit",  QVariant(8));
  m_depthCombo->addItem("16-bit", QVariant(16));
  m_depthCombo->addItem("32-bit", QVariant(32));
  m_depthCombo->setCurrentIndex(1); // default 16-bit
  nonDngForm->addRow(tr("Bit depth:"), m_depthCombo);
  connect(m_depthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &RenderDialog::updateSizePreview);

  if (scrParams.type != colorscreen::Random) {
    m_geometryCombo = new QComboBox(m_nonDngWidget);
    for (int i = 0; i < (int)render_to_file_params::max_geometry; ++i)
      m_geometryCombo->addItem(render_to_file_params::geometry_names[i].name, i);
    m_geometryCombo->setCurrentIndex((int)render_to_file_params::default_geometry);
    nonDngForm->addRow(tr("Geometry:"), m_geometryCombo);
    connect(m_geometryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderDialog::onGeometryChanged);
  }

  m_nonDngWidget->setVisible(!isDng);
  mainLayout->addWidget(m_nonDngWidget);

  // ── Antialias ─────────────────────────────────────────────────────────────
  auto *commonForm = new QFormLayout;
  mainLayout->addLayout(commonForm);

  m_antialiasSpin = new QSpinBox(this);
  m_antialiasSpin->setRange(0, 32);
  m_antialiasSpin->setValue(0);
  m_antialiasSpin->setSpecialValueText(tr("default"));
  commonForm->addRow(tr("Antialias (NxN):"), m_antialiasSpin);

  // ── Output Size group ─────────────────────────────────────────────────────
  auto *sizeGroup = new QGroupBox(tr("Output Size"), this);
  auto *sizeForm  = new QFormLayout(sizeGroup);
  mainLayout->addWidget(sizeGroup);

  // Scale (default 1.0)
  makeScaleRow(sizeForm, tr("Scale:"), &m_scaleSpin, &m_scaleSlider, sizeGroup);

  // Screen scale (shown only for screen geometry)
  {
    auto *ssContainer = new QWidget(sizeGroup);
    auto *hlay = new QHBoxLayout(ssContainer);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_screenScaleSpin = new QDoubleSpinBox(ssContainer);
    m_screenScaleSpin->setRange(0.0, 200.0);
    m_screenScaleSpin->setDecimals(2);
    m_screenScaleSpin->setSingleStep(0.5);
    m_screenScaleSpin->setSpecialValueText(tr("disabled"));
    m_screenScaleSpin->setValue(0.0);
    m_screenScaleSpin->setFixedWidth(90);
    hlay->addWidget(m_screenScaleSpin);

    m_screenScaleSlider = new QSlider(Qt::Horizontal, ssContainer);
    m_screenScaleSlider->setRange(0, 1000);
    m_screenScaleSlider->setValue(0);
    hlay->addWidget(m_screenScaleSlider, 1);

    m_screenScaleRow = ssContainer;
    sizeForm->addRow(tr("Screen scale:"), ssContainer);
  }

  // Width / Height
  {
    auto *whContainer = new QWidget(sizeGroup);
    auto *hlay = new QHBoxLayout(whContainer);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_widthSpin = new QSpinBox(whContainer);
    m_widthSpin->setRange(0, 100000);
    m_widthSpin->setValue(0);
    m_widthSpin->setSpecialValueText(tr("auto"));
    m_widthSpin->setSuffix(" px");
    auto *resetWidth = new QPushButton(QStringLiteral("↺"), whContainer);
    resetWidth->setFixedWidth(28);
    resetWidth->setToolTip(tr("Reset to auto"));
    connect(resetWidth, &QPushButton::clicked, this, [this]() { m_widthSpin->setValue(0); });
    hlay->addWidget(new QLabel(tr("W:"), whContainer));
    hlay->addWidget(m_widthSpin, 1);
    hlay->addWidget(resetWidth);

    m_heightSpin = new QSpinBox(whContainer);
    m_heightSpin->setRange(0, 100000);
    m_heightSpin->setValue(0);
    m_heightSpin->setSpecialValueText(tr("auto"));
    m_heightSpin->setSuffix(" px");
    auto *resetHeight = new QPushButton(QStringLiteral("↺"), whContainer);
    resetHeight->setFixedWidth(28);
    resetHeight->setToolTip(tr("Reset to auto"));
    connect(resetHeight, &QPushButton::clicked, this, [this]() { m_heightSpin->setValue(0); });
    hlay->addWidget(new QLabel(tr("H:"), whContainer));
    hlay->addWidget(m_heightSpin, 1);
    hlay->addWidget(resetHeight);

    sizeForm->addRow(tr("Width / Height:"), whContainer);
  }

  // Output size label (exact, not estimated)
  m_sizePreviewLabel = new QLabel(tr("—"), sizeGroup);
  m_sizePreviewLabel->setAlignment(Qt::AlignRight);
  sizeForm->addRow(tr("Output size:"), m_sizePreviewLabel);

  // ── Signal connections ────────────────────────────────────────────────────
  connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &RenderDialog::onModeChanged);

  // Scale spinbox <-> slider (sync only, then update preview + states)
  connect(m_scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double v) {
            if (m_updatingSliders) return;
            m_updatingSliders = true;
            m_scaleSlider->setValue(scaleToSlider(v));
            m_updatingSliders = false;
            updateSizePreview();
            updateControlStates();
          });
  connect(m_scaleSlider, &QSlider::valueChanged, this, [this](int v) {
    if (m_updatingSliders) return;
    m_updatingSliders = true;
    m_scaleSpin->setValue(sliderToScale(v));
    m_updatingSliders = false;
    updateSizePreview();
    updateControlStates();
  });

  // Screen scale spinbox <-> slider (linear 0..20)
  connect(m_screenScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double v) {
            if (m_updatingSliders) return;
            m_updatingSliders = true;
            m_screenScaleSlider->setValue(v > 0 ? (int)(v / 20.0 * 1000) : 0);
            m_updatingSliders = false;
            updateSizePreview();
            updateControlStates();
          });
  connect(m_screenScaleSlider, &QSlider::valueChanged, this, [this](int v) {
    if (m_updatingSliders) return;
    m_updatingSliders = true;
    m_screenScaleSpin->setValue(v > 0 ? v / 1000.0 * 20.0 : 0.0);
    m_updatingSliders = false;
    updateSizePreview();
    updateControlStates();
  });

  connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
    updateSizePreview();
    updateControlStates();
  });
  connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
    updateSizePreview();
    updateControlStates();
  });
  connect(m_antialiasSpin, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &RenderDialog::updateSizePreview);

  // ── Buttons ───────────────────────────────────────────────────────────────
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);

  // Initial state
  onGeometryChanged(-1);
  updateControlStates();
  updateSizePreview();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool RenderDialog::screenGeometryActive() const
{
  if (!m_geometryCombo) return false;
  auto g = (render_to_file_params::output_geometry)m_geometryCombo->currentData().toInt();
  if (g == render_to_file_params::screen_geometry) return true;
  if (g == render_to_file_params::default_geometry) {
    int typeVal = m_modeCombo->currentData().toInt();
    const render_type_property &prop = render_type_properties[typeVal];
    return (prop.flags & render_type_property::NEEDS_SCR_TO_IMG) != 0;
  }
  return false;
}

void RenderDialog::onGeometryChanged(int)
{
  if (m_screenScaleRow)
    m_screenScaleRow->setVisible(screenGeometryActive());
  updateControlStates();
  updateSizePreview();
}

void RenderDialog::onModeChanged(int)
{
  onGeometryChanged(-1); // re-evaluate screen geometry visibility
}

void RenderDialog::updateControlStates()
{
  bool widthOrHeight = (m_widthSpin && m_widthSpin->value() > 0) ||
                       (m_heightSpin && m_heightSpin->value() > 0);
  bool screenScaleSet = m_screenScaleSpin && m_screenScaleSpin->value() > 0;

  // Width or height overrides everything → gray out scale and screen_scale
  if (m_scaleSpin)        m_scaleSpin->setEnabled(!widthOrHeight);
  if (m_scaleSlider)      m_scaleSlider->setEnabled(!widthOrHeight);
  if (m_screenScaleSpin)  m_screenScaleSpin->setEnabled(!widthOrHeight);
  if (m_screenScaleSlider) m_screenScaleSlider->setEnabled(!widthOrHeight);

  // screen_scale set → gray out scale (but only if w/h not already disabling)
  if (!widthOrHeight) {
    if (m_scaleSpin)   m_scaleSpin->setEnabled(!screenScaleSet);
    if (m_scaleSlider) m_scaleSlider->setEnabled(!screenScaleSet);
  }
}

void RenderDialog::updateSizePreview()
{
  if (!m_scan || !m_sizePreviewLabel) return;

  render_to_file_params rfp;
  rfp.scale        = m_scaleSpin ? m_scaleSpin->value() : 1.0;
  rfp.screen_scale = (screenGeometryActive() && m_screenScaleSpin)
                     ? m_screenScaleSpin->value() : 0.0;
  rfp.width        = m_widthSpin  ? m_widthSpin->value()  : 0;
  rfp.height       = m_heightSpin ? m_heightSpin->value() : 0;
  rfp.antialias    = m_antialiasSpin ? m_antialiasSpin->value() : 0;
  rfp.geometry     = geometry();

  render_type_parameters rt = renderTypeParams();
  scr_to_img_parameters  sc = m_scrParams;

  if (!complete_rendered_file_parameters(rt, sc, const_cast<image_data &>(*m_scan), &rfp)) {
    m_sizePreviewLabel->setText(tr("(cannot compute)"));
    return;
  }

  int depth = m_depthCombo ? m_depthCombo->currentData().toInt() : 16;
  double mpx = rfp.width * (double)rfp.height / 1e6;
  double bytes = rfp.width * (double)rfp.height * 3.0 * (depth / 8);
  double mb = bytes / (1024.0 * 1024.0);
  QString sizeStr;
  if (mb >= 1024.0)
    sizeStr = tr("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
  else
    sizeStr = tr("%1 MB").arg(mb, 0, 'f', 1);

  m_sizePreviewLabel->setText(
      tr("%1 × %2 px  (%3 Mpx, %4)")
          .arg(rfp.width)
          .arg(rfp.height)
          .arg(mpx, 0, 'f', 2)
          .arg(sizeStr));
}

// ── Accessors ─────────────────────────────────────────────────────────────────

render_type_parameters RenderDialog::renderTypeParams() const {
  render_type_parameters p = m_rtparams;
  p.type = (render_type_t)m_modeCombo->currentData().toInt();
  return p;
}
render_parameters::output_profile_t RenderDialog::outputProfile() const {
  return m_profileCombo
    ? (render_parameters::output_profile_t)m_profileCombo->currentData().toInt()
    : render_parameters::output_profile_sRGB;
}
bool RenderDialog::hdr() const { return m_hdrCheck && m_hdrCheck->isChecked(); }
int  RenderDialog::depth() const { return m_depthCombo ? m_depthCombo->currentData().toInt() : 16; }
render_to_file_params::output_geometry RenderDialog::geometry() const {
  return m_geometryCombo
    ? (render_to_file_params::output_geometry)m_geometryCombo->currentData().toInt()
    : render_to_file_params::default_geometry;
}
int RenderDialog::antialias() const { return m_antialiasSpin ? m_antialiasSpin->value() : 0; }

double RenderDialog::scale() const {
  return m_scaleSpin ? m_scaleSpin->value() : 1.0;
}
double RenderDialog::screenScale() const {
  return (m_screenScaleSpin && screenGeometryActive()) ? m_screenScaleSpin->value() : 0.0;
}
int RenderDialog::outputWidth()  const { return m_widthSpin  ? m_widthSpin->value()  : 0; }
int RenderDialog::outputHeight() const { return m_heightSpin ? m_heightSpin->value() : 0; }
