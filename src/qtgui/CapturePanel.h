#ifndef CAPTURE_PANEL_H
#define CAPTURE_PANEL_H

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

class CapturePanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit CapturePanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~CapturePanel() override;

signals:
    void cropRequested();

private:
    void setupUi();
};

#endif // CAPTURE_PANEL_H
