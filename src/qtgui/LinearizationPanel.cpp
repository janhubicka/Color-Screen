#include "LinearizationPanel.h"
#include <QPushButton>
#include <QVBoxLayout>

LinearizationPanel::LinearizationPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent)
{
    setupUi();
}

LinearizationPanel::~LinearizationPanel() = default;

void LinearizationPanel::setupUi()
{
    // Gamma logic
    std::map<double, QString> gammas;
    gammas[-1.0] = "sRGB gamma";
    gammas[0.0] = "Use ICC profile";
    
    std::map<double, QString> quickSelects = gammas;
    quickSelects[1.0] = "1.0";
    quickSelects[1.9] = "1.9";
    quickSelects[2.2] = "2.2";
    
    addDoubleParameter("Image gamma", -1.0, 5.0, 
        [](const ParameterState &s) { return s.rparams.gamma; },
        [](ParameterState &s, double v) { s.rparams.gamma = v; },
        gammas,
        quickSelects,
        nullptr
    );
  /* Only makes effect with backlight correction on; handle it later.  */
#if 0
  addSliderParameter(
      "Scanner/camera black point", 0, 1, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.backlight_correction_black; },
      [](ParameterState &s, double v) { s.rparams.backlight_correction_black = v; }, 3.0,
      nullptr, true);
#endif
    
    QPushButton *cropButton = new QPushButton("Crop", this);
    connect(cropButton, &QPushButton::clicked, this, &LinearizationPanel::cropRequested);
    m_layout->addWidget(cropButton);
}

