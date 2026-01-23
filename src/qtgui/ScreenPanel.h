#ifndef SCREEN_PANEL_H
#define SCREEN_PANEL_H

#include "ParameterPanel.h"

class TilePreviewPanel;
namespace colorscreen { struct progress_info; } // namespace colorscreen

class ScreenPanel : public ParameterPanel {
  Q_OBJECT
public:
  ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
              ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ScreenPanel() override;

  void reattachPreview(QWidget *widget);

signals:
  void detachPreviewRequested(QWidget *widget);
  void progressStarted(std::shared_ptr<colorscreen::progress_info> progress);
  void progressFinished(std::shared_ptr<colorscreen::progress_info> progress);
  void autodetectRequested();

private:
  void setupUi();
  TilePreviewPanel *m_previewPanel = nullptr;
};

#endif // SCREEN_PANEL_H
