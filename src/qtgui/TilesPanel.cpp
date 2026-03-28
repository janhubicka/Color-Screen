#include "TilesPanel.h"
#include "../libcolorscreen/include/stitch.h"
#include <QButtonGroup>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TilesPanel::TilesPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

TilesPanel::~TilesPanel() = default;

void TilesPanel::setupUi() {
  // Tile selector (Grid of buttons instead of combo)
  m_selectorGridWidget = new QWidget(this);
  m_form->addRow(tr("Current tile"), m_selectorGridWidget);

  m_currentGroupForm = nullptr;

  // Exposure slider for selected tile
  addSeparator(tr("Tile adjustments"));

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

  // Tile toggle grid
  m_currentGroupForm = nullptr;
  addSeparator(tr("Tile enable / disable"));
  m_enableGridWidget = new QWidget(this);
  m_form->addRow(m_enableGridWidget);
}

void TilesPanel::updateForNewImage() {
  rebuildTileGrid();
}

void TilesPanel::rebuildTileGrid() {
  // Clear existing buttons and layouts
  if (m_selectorGroup) {
    m_selectorGroup->deleteLater();
    m_selectorGroup = nullptr;
  }
  qDeleteAll(m_selectorGridWidget->findChildren<QWidget*>());
  if (m_selectorGridWidget->layout()) {
    delete m_selectorGridWidget->layout();
  }
  qDeleteAll(m_enableGridWidget->findChildren<QWidget*>());
  if (m_enableGridWidget->layout()) {
    delete m_enableGridWidget->layout();
  }

  m_tileChecks.clear();
  m_tileSelectors.clear();

  auto img = m_imageGetter();
  if (!img || !img->stitch) {
    m_gridW = 0;
    m_gridH = 0;
    return;
  }

  m_gridW = img->stitch->params.width;
  m_gridH = img->stitch->params.height;

  m_tileChecks.resize(m_gridH, std::vector<QPushButton *>(m_gridW, nullptr));
  m_tileSelectors.resize(m_gridH, std::vector<QPushButton *>(m_gridW, nullptr));

  m_selectorGroup = new QButtonGroup(this);
  m_selectorGroup->setExclusive(true);

  auto *selectorLayout = new QGridLayout(m_selectorGridWidget);
  selectorLayout->setContentsMargins(0, 0, 0, 0);
  selectorLayout->setSpacing(2);

  auto *enableLayout = new QGridLayout(m_enableGridWidget);
  enableLayout->setContentsMargins(0, 0, 0, 0);
  enableLayout->setSpacing(2);

  ParameterState state = m_stateGetter();

  for (int gy = 0; gy < m_gridH; gy++) {
    for (int gx = 0; gx < m_gridW; gx++) {
      int flatIndex = gy * m_gridW + gx;

      // 1. Selector button (Radio button behavior)
      auto *selBtn = new QPushButton(m_selectorGridWidget);
      selBtn->setCheckable(true);
      selBtn->setFixedSize(32, 32);
      selBtn->setToolTip(tr("Select tile %1, %2").arg(gx).arg(gy));
      m_selectorGroup->addButton(selBtn, flatIndex);
      selectorLayout->addWidget(selBtn, gy, gx);
      m_tileSelectors[gy][gx] = selBtn;

      // 2. Enable button (Checkbox behavior)
      auto *enBtn = new QPushButton(m_enableGridWidget);
      enBtn->setCheckable(true);
      enBtn->setFixedSize(32, 32);
      enBtn->setToolTip(tr("Enable/Disable tile %1, %2").arg(gx).arg(gy));
      enBtn->setChecked(state.rparams.get_tile_adjustment(img->stitch, gx, gy).enabled);
      enableLayout->addWidget(enBtn, gy, gx);
      m_tileChecks[gy][gx] = enBtn;

      connect(enBtn, &QPushButton::toggled, this, [this, gx, gy](bool checked) {
        ParameterState s = m_stateGetter();
        s.rparams.get_tile_adjustment(gx, gy).enabled = checked;
        m_stateSetter(s, tr("Toggle tile %1,%2").arg(gx).arg(gy));
      });
    }
  }

  // Ensure current tile index is valid
  if (m_currentTileIndex >= m_gridW * m_gridH || m_currentTileIndex < 0)
    m_currentTileIndex = 0;

  // Check the currently selected tile in the radio group
  if (m_selectorGroup->button(m_currentTileIndex)) {
    m_selectorGroup->button(m_currentTileIndex)->setChecked(true);
  }

  connect(m_selectorGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
    if (checked) {
      m_currentTileIndex = id;
      refreshTileSliders();
    }
  });

  refreshTileSliders();
}

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
