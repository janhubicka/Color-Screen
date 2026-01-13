#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"

class MTFChartWidget;

class SharpnessPanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~SharpnessPanel() override;

private:
    void setupUi();
    void updateMTFChart();
    void applyChange(std::function<void(ParameterState&)> modifier) override;
    void onParametersRefreshed(const ParameterState &state) override;
    
    MTFChartWidget *m_mtfChart = nullptr;
};

#endif // SHARPNESS_PANEL_H
