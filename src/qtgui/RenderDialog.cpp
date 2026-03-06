#include "RenderDialog.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace colorscreen;

RenderDialog::RenderDialog(
    const render_type_parameters &rtparams,
    const render_parameters &rparams,
    const scr_to_img_parameters &scrParams,
    const image_data *scan,
    bool isDng,
    QWidget *parent)
  : QDialog(parent), m_rtparams(rtparams)
{
  setWindowTitle(tr("Render Settings"));
  setMinimumWidth(380);

  auto *mainLayout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  mainLayout->addLayout(form);

  // ── Mode ──────────────────────────────────────────────────────────────────
  m_modeCombo = new QComboBox(this);
  for (int i = 0; i < render_type_max; ++i) {
    const render_type_property &prop = render_type_properties[i];
    bool show = true;
    if ((prop.flags & render_type_property::NEEDS_SCR_TO_IMG) &&
        scrParams.type == colorscreen::Random)
      show = false;
    if ((prop.flags & render_type_property::NEEDS_RGB) &&
        (!scan || !scan->rgbdata))
      show = false;
    if (show)
      m_modeCombo->addItem(prop.name, QVariant(i));
  }
  // Select current mode
  int idx = m_modeCombo->findData((int)rtparams.type);
  if (idx >= 0)
    m_modeCombo->setCurrentIndex(idx);
  form->addRow(tr("Mode:"), m_modeCombo);

  // ── Non-DNG options ───────────────────────────────────────────────────────
  m_nonDngWidget = new QWidget(this);
  auto *nonDngForm = new QFormLayout(m_nonDngWidget);
  nonDngForm->setContentsMargins(0, 0, 0, 0);

  // Output profile
  m_profileCombo = new QComboBox(m_nonDngWidget);
  for (int i = 0; i < (int)render_parameters::output_profile_max; ++i)
    m_profileCombo->addItem(render_parameters::output_profile_names[i], i);
  m_profileCombo->setCurrentIndex((int)rparams.output_profile);
  nonDngForm->addRow(tr("Output profile:"), m_profileCombo);

  // HDR checkbox
  m_hdrCheck = new QCheckBox(tr("HDR output"), m_nonDngWidget);
  nonDngForm->addRow(QString(), m_hdrCheck);

  // Bit depth
  m_depthCombo = new QComboBox(m_nonDngWidget);
  m_depthCombo->addItem("8-bit",  QVariant(8));
  m_depthCombo->addItem("16-bit", QVariant(16));
  m_depthCombo->addItem("32-bit", QVariant(32));
  m_depthCombo->setCurrentIndex(1); // default 16-bit
  nonDngForm->addRow(tr("Bit depth:"), m_depthCombo);

  // Geometry (only when scr type is not Random)
  if (scrParams.type != colorscreen::Random) {
    m_geometryCombo = new QComboBox(m_nonDngWidget);
    for (int i = 0; i < (int)render_to_file_params::max_geometry; ++i)
      m_geometryCombo->addItem(render_to_file_params::geometry_names[i].name, i);
    m_geometryCombo->setCurrentIndex((int)render_to_file_params::default_geometry);
    nonDngForm->addRow(tr("Geometry:"), m_geometryCombo);
  }

  m_nonDngWidget->setVisible(!isDng);
  mainLayout->addWidget(m_nonDngWidget);

  // ── Antialias ─────────────────────────────────────────────────────────────
  m_antialiasSpin = new QSpinBox(this);
  m_antialiasSpin->setRange(0, 32);
  m_antialiasSpin->setValue(0);
  m_antialiasSpin->setSpecialValueText(tr("default"));
  form->addRow(tr("Antialias (NxN):"), m_antialiasSpin);

  // ── Buttons ───────────────────────────────────────────────────────────────
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);
}

render_type_parameters RenderDialog::renderTypeParams() const {
  render_type_parameters p = m_rtparams;
  int typeVal = m_modeCombo->currentData().toInt();
  p.type = (render_type_t)typeVal;
  return p;
}

render_parameters::output_profile_t RenderDialog::outputProfile() const {
  if (!m_profileCombo)
    return render_parameters::output_profile_sRGB;
  return (render_parameters::output_profile_t)m_profileCombo->currentData().toInt();
}

bool RenderDialog::hdr() const {
  return m_hdrCheck ? m_hdrCheck->isChecked() : false;
}

int RenderDialog::depth() const {
  return m_depthCombo ? m_depthCombo->currentData().toInt() : 16;
}

render_to_file_params::output_geometry RenderDialog::geometry() const {
  if (!m_geometryCombo)
    return render_to_file_params::default_geometry;
  return (render_to_file_params::output_geometry)m_geometryCombo->currentData().toInt();
}

int RenderDialog::antialias() const {
  return m_antialiasSpin ? m_antialiasSpin->value() : 0;
}
