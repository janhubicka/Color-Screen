#ifndef LINEARIZATION_PANEL_H
#define LINEARIZATION_PANEL_H

#include <QWidget>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include "ParameterState.h"
#include "../libcolorscreen/include/base.h" // For image_data definition if needed, or forward decl

namespace colorscreen {
    class image_data;
}

#include "ParameterPanel.h"

class LinearizationPanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit LinearizationPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~LinearizationPanel() override;

private:
    void setupUi();
};

#endif // LINEARIZATION_PANEL_H
