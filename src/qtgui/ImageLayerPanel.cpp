#include "ImageLayerPanel.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QFormLayout>

ImageLayerPanel::ImageLayerPanel(StateGetter stateGetter, StateSetter stateSetter,
                                 ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ImageLayerPanel::~ImageLayerPanel() = default;

void ImageLayerPanel::setupUi() {
  m_ignoreInfraredCheck = new QCheckBox(tr("Ignore infrared"), this);
  m_form->addRow(m_ignoreInfraredCheck);

  connect(m_ignoreInfraredCheck, &QCheckBox::toggled, this, [this](bool checked) {
    ParameterState s = m_stateGetter();
    s.rparams.ignore_infrared = checked;
    m_stateSetter(s, tr("Ignore infrared %1").arg(checked ? tr("on") : tr("off")));
  });

  addSeparator(tr("Simulated image layer"));

  auto enableSimulated = [this](const ParameterState &s) -> bool {
    auto img = m_imageGetter();
    if (!img) return false;
    return img->has_rgb() && (!img->has_grayscale_or_ir() || s.rparams.ignore_infrared);
  };

  addSliderParameter(
      tr("Mix dark (red)"), -3.0, 1.0, 1, 4, "", "",
      [](const ParameterState &s) { return s.rparams.mix_dark.red; },
      [](ParameterState &s, double v) { s.rparams.mix_dark.red = v; },
      3.0, enableSimulated, false);

  addSliderParameter(
      tr("Mix dark (green)"), -3.0, 1.0, 1, 4, "", "",
      [](const ParameterState &s) { return s.rparams.mix_dark.green; },
      [](ParameterState &s, double v) { s.rparams.mix_dark.green = v; },
      3.0, enableSimulated, false);

  addSliderParameter(
      tr("Mix dark (blue)"), -3.0, 1.0, 1, 4, "", "",
      [](const ParameterState &s) { return s.rparams.mix_dark.blue; },
      [](ParameterState &s, double v) { s.rparams.mix_dark.blue = v; },
      3.0, enableSimulated, false);

  addSliderParameter(
      tr("Mix red"), -10.0, 10.0, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.mix_red; },
      [](ParameterState &s, double v) { s.rparams.mix_red = v; },
      1.0, enableSimulated, false);

  addSliderParameter(
      tr("Mix green"), -10.0, 10.0, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.mix_green; },
      [](ParameterState &s, double v) { s.rparams.mix_green = v; },
      1.0, enableSimulated, false);

  addSliderParameter(
      tr("Mix blue"), -10.0, 10.0, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.mix_blue; },
      [](ParameterState &s, double v) { s.rparams.mix_blue = v; },
      1.0, enableSimulated, false);
}

void ImageLayerPanel::onParametersRefreshed(const ParameterState &state) {
  if (m_ignoreInfraredCheck) {
    m_ignoreInfraredCheck->blockSignals(true);
    m_ignoreInfraredCheck->setChecked(state.rparams.ignore_infrared);
    m_ignoreInfraredCheck->blockSignals(false);

    auto img = m_imageGetter();
    bool enable = img && img->has_rgb() && img->has_grayscale_or_ir();
    m_ignoreInfraredCheck->setEnabled(enable);
  }
}
