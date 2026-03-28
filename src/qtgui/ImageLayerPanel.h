#ifndef IMAGE_LAYER_PANEL_H
#define IMAGE_LAYER_PANEL_H

#include "ParameterPanel.h"
#include <QCheckBox>

class ImageLayerPanel : public ParameterPanel {
  Q_OBJECT
public:
  explicit ImageLayerPanel(StateGetter stateGetter, StateSetter stateSetter,
                           ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ImageLayerPanel() override;

protected:
  void onParametersRefreshed(const ParameterState &state) override;

private:
  void setupUi();

  QCheckBox *m_ignoreInfraredCheck = nullptr;
};

#endif // IMAGE_LAYER_PANEL_H
