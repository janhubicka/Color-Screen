#ifndef TILES_PANEL_H
#define TILES_PANEL_H

#include "ParameterPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QWidget>
#include <vector>

namespace colorscreen {
class image_data;
}

class TilesPanel : public ParameterPanel {
  Q_OBJECT
public:
  explicit TilesPanel(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter, QWidget *parent = nullptr);
  ~TilesPanel() override;

  // Called when the image changes so the panel can rebuild the tile grid
  void updateForNewImage();

protected:
  void onParametersRefreshed(const ParameterState &state) override;

private:
  void setupUi();
  void rebuildTileGrid();
  void refreshTileToggles(const ParameterState &state);
  void refreshTileSliders();
  int  currentTileX() const;
  int  currentTileY() const;

  // stitch grid dimensions at last rebuild
  int m_gridW = 0;
  int m_gridH = 0;

  // Grid of "enabled" checkboxes  [y][x]
  std::vector<std::vector<QCheckBox *>> m_tileChecks;

  // Combo that selects which tile's exposure/dark_point sliders apply to
  QComboBox *m_tileSelector = nullptr;

  // Widgets for exposure / dark_point sliders (kept to hide/show)
  QWidget *m_sliderSection = nullptr;

  // The form row's widgets are replaced via applyChange; we only need to store
  // the current index to route the lambda getters/setters.
  int m_currentTileIndex = 0; // flat index = y * w + x

  // Per-tile slider updaters registered via addSliderParameter. We keep and
  // re-invoke them on tile selection change.
  QWidget *m_exposureRow   = nullptr;
  QWidget *m_darkPointRow  = nullptr;
};

#endif // TILES_PANEL_H
