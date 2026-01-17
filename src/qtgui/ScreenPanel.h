#ifndef SCREEN_PANEL_H
#define SCREEN_PANEL_H

#include "ParameterPanel.h"

class ScreenPanel : public ParameterPanel {
  Q_OBJECT
public:
  ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
              ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ScreenPanel() override;

private:
  void setupUi();
};

#endif // SCREEN_PANEL_H
