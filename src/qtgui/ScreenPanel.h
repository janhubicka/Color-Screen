#ifndef SCREEN_PANEL_H
#define SCREEN_PANEL_H

#include "ParameterPanel.h"

class TilePreviewPanel;

class ScreenPanel : public ParameterPanel {
  Q_OBJECT
public:
  ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
              ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ScreenPanel() override;

  void reattachPreview(QWidget *widget);

signals:
  void detachPreviewRequested(QWidget *widget);

private:
  void setupUi();
  TilePreviewPanel *m_previewPanel = nullptr;
};

#endif // SCREEN_PANEL_H
