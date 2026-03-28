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
  
  void setInfraredAreaChecked(bool checked);
  void setInfraredAreaEnabled(bool enabled);
  
  void setDarkAreaChecked(bool checked);
  void setDarkAreaEnabled(bool enabled);

protected:
  void onParametersRefreshed(const ParameterState &state) override;

signals:
  void neutralAreaRequested();
  void infraredAreaRequested();
  void darkAreaRequested();

private:
  void setupUi();

  QCheckBox *m_ignoreInfraredCheck = nullptr;
  QPushButton *m_setNeutralAreaBtn = nullptr;
  QPushButton *m_setInfraredAreaBtn = nullptr;
  QPushButton *m_setDarkAreaBtn = nullptr;
};

#endif // IMAGE_LAYER_PANEL_H
