#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"

class SharpnessPanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~SharpnessPanel() override;

private:
    void setupUi();
};

#endif // SHARPNESS_PANEL_H
