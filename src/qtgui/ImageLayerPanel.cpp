#include "ImageLayerPanel.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QSignalBlocker>
#include <QFormLayout>

ImageLayerPanel::ImageLayerPanel(StateGetter stateGetter, StateSetter stateSetter,
                                 ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ImageLayerPanel::~ImageLayerPanel() = default;

void ImageLayerPanel::setupUi() {
  m_ignoreInfraredCheck = new QCheckBox(tr("Use simulated RGB image layer"), this);
  m_ignoreInfraredCheck->setObjectName(
      QStringLiteral("ImageLayerUseSimulatedRgbCheck"));
  m_ignoreInfraredCheck->setToolTip(
      tr("When the capture contains both RGB and a native grayscale/infrared "
         "channel, use a weighted RGB simulation as the image layer instead. "
         "Leave this unchecked to use the native grayscale/infrared channel."));
  m_form->addRow(m_ignoreInfraredCheck);
  setParameterApplicability(
      m_ignoreInfraredCheck, [this](const ParameterState &) {
        const auto img = m_imageGetter();
        return img && img->has_rgb() && img->has_grayscale_or_ir();
      });

  connect(m_ignoreInfraredCheck, &QCheckBox::toggled, this, [this](bool checked) {
    ParameterState s = m_stateGetter();
    s.rparams.ignore_infrared = checked;
    m_stateSetter(s, tr("Use simulated RGB image layer %1")
                        .arg(checked ? tr("on") : tr("off")),
                  QString());
  });

  QToolButton *simulatedSectionToggle =
      addSeparator(tr("Simulated image layer from RGB"));
  simulatedSectionToggle->setObjectName(
      QStringLiteral("SimulatedImageLayerToggle"));
  QWidget *simulatedSection = simulatedSectionToggle->parentWidget();
  if (simulatedSection)
    simulatedSection = simulatedSection->parentWidget();
  if (simulatedSection)
    simulatedSection->setObjectName(
        QStringLiteral("SimulatedImageLayerSection"));

  auto enableSimulated = [this](const ParameterState &s) -> bool {
    auto img = m_imageGetter();
    if (!img)
      return false;
    return img->has_rgb() &&
           (!img->has_grayscale_or_ir() || s.rparams.ignore_infrared);
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

  // Dark Area Button
  m_setDarkAreaBtn = addToggleButtonParameter(
      "", tr("Set by dark area"),
      [this](bool) { emit darkAreaRequested(); },
      nullptr,
      enableSimulated);

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

  // Neutral Area Button
  m_setNeutralAreaBtn = addToggleButtonParameter(
      "", tr("Set by neutral area"),
      [this](bool) { emit neutralAreaRequested(); },
      nullptr,
      enableSimulated);

  // Infrared Area Button
  m_setInfraredAreaBtn = addToggleButtonParameter(
      "", tr("Set by infrared channel"),
      [this](bool) { emit infraredAreaRequested(); },
      nullptr,
      [this](const ParameterState &s) {
        auto img = m_imageGetter();
        return img && img->has_rgb() && img->has_grayscale_or_ir() &&
               s.rparams.ignore_infrared;
      });
  m_setInfraredAreaBtn->setObjectName(
      QStringLiteral("ImageLayerSetByInfraredButton"));
  setParameterApplicability(
      m_setInfraredAreaBtn, [this](const ParameterState &s) {
        auto img = m_imageGetter();
        return img && img->has_rgb() && img->has_grayscale_or_ir() &&
               s.rparams.ignore_infrared;
      });

  // The checkbox exists only when there is a real source choice. Likewise, do
  // not show the RGB mixer when the active image layer comes from a native
  // grayscale/IR channel, or when no RGB data exists to synthesize one.
  m_widgetStateUpdaters.push_back(
      [this, simulatedSection, enableSimulated]() {
        const auto img = m_imageGetter();
        const ParameterState state = m_stateGetter();
        const bool simulatedActive = enableSimulated(state);
        if (simulatedSection)
          simulatedSection->setVisible(simulatedActive);
      });
}

void ImageLayerPanel::onParametersRefreshed(const ParameterState &state) {
  if (m_ignoreInfraredCheck) {
    QSignalBlocker signalBlocker1(m_ignoreInfraredCheck);
    m_ignoreInfraredCheck->setChecked(state.rparams.ignore_infrared);
    signalBlocker1.unblock();

    auto img = m_imageGetter();
    bool enable = img && img->has_rgb() && img->has_grayscale_or_ir();
    m_ignoreInfraredCheck->setEnabled(enable);
  }
  
  if (m_setNeutralAreaBtn) {
    auto img = m_imageGetter();
    bool enableSim = img && img->has_rgb() && (!img->has_grayscale_or_ir() || state.rparams.ignore_infrared);
    m_setNeutralAreaBtn->setEnabled(enableSim);
  }
  
  if (m_setInfraredAreaBtn) {
    auto img = m_imageGetter();
    bool enableIr = img && img->has_rgb() && img->has_grayscale_or_ir() && state.rparams.ignore_infrared;
    m_setInfraredAreaBtn->setEnabled(enableIr);
  }
  
  if (m_setDarkAreaBtn) {
    auto img = m_imageGetter();
    bool enableSim = img && img->has_rgb() && (!img->has_grayscale_or_ir() || state.rparams.ignore_infrared);
    m_setDarkAreaBtn->setEnabled(enableSim);
  }
}

void ImageLayerPanel::setNeutralAreaChecked(bool checked) {
  if (m_setNeutralAreaBtn) {
    QSignalBlocker signalBlocker2(m_setNeutralAreaBtn);
    m_setNeutralAreaBtn->setChecked(checked);
    signalBlocker2.unblock();
  }
}

void ImageLayerPanel::setNeutralAreaEnabled(bool enabled) {
  if (m_setNeutralAreaBtn) {
    m_setNeutralAreaBtn->setEnabled(enabled);
  }
}

void ImageLayerPanel::setInfraredAreaChecked(bool checked) {
  if (m_setInfraredAreaBtn) {
    QSignalBlocker signalBlocker3(m_setInfraredAreaBtn);
    m_setInfraredAreaBtn->setChecked(checked);
    signalBlocker3.unblock();
  }
}

void ImageLayerPanel::setInfraredAreaEnabled(bool enabled) {
  if (m_setInfraredAreaBtn) {
    m_setInfraredAreaBtn->setEnabled(enabled);
  }
}

void ImageLayerPanel::setDarkAreaChecked(bool checked) {
  if (m_setDarkAreaBtn) {
    QSignalBlocker signalBlocker4(m_setDarkAreaBtn);
    m_setDarkAreaBtn->setChecked(checked);
    signalBlocker4.unblock();
  }
}

void ImageLayerPanel::setDarkAreaEnabled(bool enabled) {
  if (m_setDarkAreaBtn) {
    m_setDarkAreaBtn->setEnabled(enabled);
  }
}
