#ifndef COLOR_PANEL_H
#define COLOR_PANEL_H

#include "../libcolorscreen/include/render-parameters.h"
#include "TilePreviewPanel.h"

class ColorPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ColorPanel() override;

protected:
  // TilePreviewPanel overrides
  std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const override;
  bool shouldUpdateTiles(const ParameterState &state) override;
  void onTileUpdateScheduled() override;
  bool isTileRenderingEnabled(const ParameterState &state) const override;

private:
  void setupUi();
  void applyChange(std::function<void(ParameterState &)> modifier) override;
  void onParametersRefreshed(const ParameterState &state) override;

  // Cached parameters for change detection
  colorscreen::render_parameters m_lastRParams;
  int m_lastScrType = -1;
  // We might want to check more than just rparams if other things affect color
  // rendering, but user said "rerendered when rparam changes". Also scan
  // width/height or image source changes are handled by TilePreviewPanel check
  // of scan ptr and pixelSize? TilePreviewPanel checks pixelSize and scan
  // pointer. We just need to check if parameters relevant to render changed.
};

#endif // COLOR_PANEL_H
