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
