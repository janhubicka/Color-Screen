#include "ColorPanel.h"

using namespace colorscreen;

ColorPanel::ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ColorPanel::~ColorPanel() = default;

void ColorPanel::setupUi() {
  setupTiles("Color Preview");

  // Dyes dropdown
  std::map<int, QString> dyes;
  for (int i = 0; i < render_parameters::color_model_max; ++i) {
    dyes[i] = QString::fromUtf8(render_parameters::color_model_names[i]);
  }

  addEnumParameter(
      "Dyes", dyes,
      [](const ParameterState &s) { return (int)s.rparams.color_model; },
      [](ParameterState &s, int v) {
        s.rparams.color_model = (render_parameters::color_model_t)v;
      });

  // Future: Add color parameters here
}

std::vector<std::pair<render_screen_tile_type, QString>>
ColorPanel::getTileTypes() const {
  return {{backlight_screen, "Backlight"},
          {detail_screen, "Detail"},
          {full_screen, "Screen"}};
}

bool ColorPanel::shouldUpdateTiles(const ParameterState &state) {
  if (!(m_lastRParams == state.rparams)) {
    return true;
  }

  // Also check scrtotype if it affects us?
  // Based on user request "rerendered when rparam changes"
  // But scan parameters might change too.
  // m_lastScrType was used before.
  if ((int)state.scrToImg.type != m_lastScrType) {
    return true;
  }

  return false;
}

void ColorPanel::onTileUpdateScheduled() {
  ParameterState state = m_stateGetter();
  m_lastScrType = (int)state.scrToImg.type;
  m_lastRParams = state.rparams;
}

bool ColorPanel::isTileRenderingEnabled(const ParameterState &state) const {
  // User requested "Make the new ColorPanel appear even when scr_to_img type is
  // Random"
  return true;
}

void ColorPanel::applyChange(std::function<void(ParameterState &)> modifier) {
  ParameterPanel::applyChange(modifier);
  scheduleTileUpdate();
}

void ColorPanel::onParametersRefreshed(const ParameterState &state) {
  scheduleTileUpdate();
}
