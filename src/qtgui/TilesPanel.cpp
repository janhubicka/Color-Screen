#include "TilesPanel.h"
#include "../libcolorscreen/include/stitch.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

TilesPanel::TilesPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

TilesPanel::~TilesPanel() = default;

void TilesPanel::setupUi() {
  // Tile selector
  {
    auto *row = new QWidget(this);
    auto *hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(0, 0, 0, 0);
    m_tileSelector = new QComboBox(row);
    m_tileSelector->setObjectName("tileSelectorCombo");
    hbox->addWidget(m_tileSelector);
    m_form->addRow(tr("Current tile"), row);

    connect(m_tileSelector,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
              m_currentTileIndex = idx;
              refreshTileSliders();
            });
  }

  m_currentGroupForm = nullptr;

  // Exposure slider for selected tile
  addSeparator(tr("Tile adjustments"));

  // We use addSliderParameter with a getter/setter that routes through
  // m_currentTileIndex. The updaters registered by addSliderParameter will be
  // called by ParameterPanel::updateUI, so they must work correctly in all
  // cases (including when no stitch is loaded – in that case we just return 1).
  addSliderParameter(
      tr("Exposure"), 0.01, 10.0, 100, 3, "", "",
      [this](const ParameterState &s) -> double {
        auto img = m_imageGetter();
        if (!img || !img->stitch) return 1.0;
        int w = img->stitch->params.width;
        if (w <= 0) return 1.0;
        int x = m_currentTileIndex % w;
        int y = m_currentTileIndex / w;
        return s.rparams.get_tile_adjustment(img->stitch, x, y).exposure;
      },
      [this](ParameterState &s, double v) {
        auto img = m_imageGetter();
        if (!img || !img->stitch) return;
        int w = img->stitch->params.width;
        if (w <= 0) return;
        int x = m_currentTileIndex % w;
        int y = m_currentTileIndex / w;
        s.rparams.get_tile_adjustment(x, y).exposure = (colorscreen::luminosity_t)v;
      },
      2.0);

  addSliderParameter(
      tr("Dark point"), -0.1, 0.5, 1000, 4, "", "",
      [this](const ParameterState &s) -> double {
        auto img = m_imageGetter();
        if (!img || !img->stitch) return 0.0;
        int w = img->stitch->params.width;
        if (w <= 0) return 0.0;
        int x = m_currentTileIndex % w;
        int y = m_currentTileIndex / w;
        return s.rparams.get_tile_adjustment(img->stitch, x, y).dark_point;
      },
      [this](ParameterState &s, double v) {
        auto img = m_imageGetter();
        if (!img || !img->stitch) return;
        int w = img->stitch->params.width;
        if (w <= 0) return;
        int x = m_currentTileIndex % w;
        int y = m_currentTileIndex / w;
        s.rparams.get_tile_adjustment(x, y).dark_point = (colorscreen::luminosity_t)v;
      },
      3.0);

  // Tile toggle grid will be built dynamically via rebuildTileGrid().
  m_currentGroupForm = nullptr;
  addSeparator(tr("Tile enable / disable"));
}

// ---------------------------------------------------------------------------
// Called by MainWindow after a new image is assigned to m_scan
// ---------------------------------------------------------------------------
void TilesPanel::updateForNewImage() {
  rebuildTileGrid();
}

// ---------------------------------------------------------------------------
// Rebuild the checkbox grid whenever the stitch dimensions change
// ---------------------------------------------------------------------------
void TilesPanel::rebuildTileGrid() {
  // Remove old checkboxes
  for (auto &row : m_tileChecks)
    for (auto *cb : row)
      delete cb;
  m_tileChecks.clear();

  // Clear old combo items
  m_tileSelector->blockSignals(true);
  m_tileSelector->clear();

  auto img = m_imageGetter();
  if (!img || !img->stitch) {
    m_gridW = 0;
    m_gridH = 0;
    m_tileSelector->blockSignals(false);
    return;
  }

  m_gridW = img->stitch->params.width;
  m_gridH = img->stitch->params.height;
  m_tileChecks.resize(m_gridH, std::vector<QCheckBox *>(m_gridW, nullptr));

  ParameterState state = m_stateGetter();

  // Add one row per tile-row to a grid container that we stick into the form
  auto *gridWidget = new QWidget(this);
  auto *gridLayout = new QVBoxLayout(gridWidget);
  gridLayout->setContentsMargins(0, 0, 0, 0);
  gridLayout->setSpacing(2);

  for (int gy = 0; gy < m_gridH; gy++) {
    auto *rowWidget = new QWidget(gridWidget);
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    for (int gx = 0; gx < m_gridW; gx++) {
      auto *cb = new QCheckBox(tr("%1,%2").arg(gx).arg(gy), rowWidget);
      cb->setChecked(state.rparams.get_tile_adjustment(gx, gy).enabled);
      m_tileChecks[gy][gx] = cb;
      rowLayout->addWidget(cb);

      connect(cb, &QCheckBox::toggled, this, [this, gx, gy](bool checked) {
        ParameterState s = m_stateGetter();
        s.rparams.get_tile_adjustment(gx, gy).enabled = checked;
        m_stateSetter(s, tr("Toggle tile %1,%2").arg(gx).arg(gy));
      });

      // Add tile to combobox
      m_tileSelector->addItem(tr("Tile %1,%2").arg(gx).arg(gy));
    }
    gridLayout->addWidget(rowWidget);
  }

  m_form->addRow(gridWidget);
  m_tileSelector->blockSignals(false);

  if (m_currentTileIndex >= m_gridW * m_gridH)
    m_currentTileIndex = 0;
  m_tileSelector->setCurrentIndex(m_currentTileIndex);
  refreshTileSliders();
}

// ---------------------------------------------------------------------------
// Keep checkbox states in sync with parameter state (undo/redo etc.)
// ---------------------------------------------------------------------------
void TilesPanel::onParametersRefreshed(const ParameterState &state) {
  refreshTileToggles(state);
}

void TilesPanel::refreshTileToggles(const ParameterState &state) {
  auto img = m_imageGetter();
  if (!img || !img->stitch) return;

  // Rebuild grid if dimensions changed
  int w = img->stitch->params.width;
  int h = img->stitch->params.height;
  if (w != m_gridW || h != m_gridH) {
    rebuildTileGrid();
    return;
  }

  for (int gy = 0; gy < m_gridH; gy++) {
    for (int gx = 0; gx < m_gridW; gx++) {
      if (m_tileChecks[gy][gx]) {
        m_tileChecks[gy][gx]->blockSignals(true);
        m_tileChecks[gy][gx]->setChecked(
            state.rparams.get_tile_adjustment(img->stitch, gx, gy).enabled);
        m_tileChecks[gy][gx]->blockSignals(false);
      }
    }
  }
}

void TilesPanel::refreshTileSliders() {
  // Trigger a full UI update so the slider's registered updater re-reads from
  // the correct tile index.
  updateUI();
}

int TilesPanel::currentTileX() const {
  if (m_gridW <= 0) return 0;
  return m_currentTileIndex % m_gridW;
}

int TilesPanel::currentTileY() const {
  if (m_gridW <= 0) return 0;
  return m_currentTileIndex / m_gridW;
}
