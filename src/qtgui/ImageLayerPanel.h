#ifndef IMAGE_LAYER_PANEL_H
#define IMAGE_LAYER_PANEL_H

#include "ParameterPanel.h"
#include <QCheckBox>
#include <QPushButton>

class ImageLayerPanel : public ParameterPanel {
  Q_OBJECT
public:
  explicit ImageLayerPanel(StateGetter stateGetter, StateSetter stateSetter,
                           ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ImageLayerPanel() override;

  void setNeutralAreaChecked(bool checked);
  void setNeutralAreaEnabled(bool enabled);

protected:
  void onParametersRefreshed(const ParameterState &state) override;

signals:
  void neutralAreaRequested();

private:
  void setupUi();

  QCheckBox *m_ignoreInfraredCheck = nullptr;
  QPushButton *m_setNeutralAreaBtn = nullptr;
};

#endif // IMAGE_LAYER_PANEL_H
