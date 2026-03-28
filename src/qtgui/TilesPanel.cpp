#include "TilesPanel.h"
#include "../libcolorscreen/include/stitch.h"
#include <QButtonGroup>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QFrame>

TilesPanel::TilesPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

TilesPanel::~TilesPanel() = default;

void TilesPanel::setupUi() {
  m_gridWidget = new QWidget(this);
  m_form->addRow(m_gridWidget);

  m_currentGroupForm = nullptr;

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
}

void TilesPanel::updateForNewImage() {
  rebuildTileGrid();
}

void TilesPanel::rebuildTileGrid() {
  if (m_selectorGroup) {
    m_selectorGroup->deleteLater();
    m_selectorGroup = nullptr;
  }
  qDeleteAll(m_gridWidget->findChildren<QWidget*>());
  if (m_gridWidget->layout()) {
    delete m_gridWidget->layout();
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

  m_tileChecks.resize(m_gridH, std::vector<QCheckBox *>(m_gridW, nullptr));
  m_tileSelectors.resize(m_gridH, std::vector<QPushButton *>(m_gridW, nullptr));

  m_selectorGroup = new QButtonGroup(this);
  m_selectorGroup->setExclusive(true);

  auto *gridLayout = new QGridLayout(m_gridWidget);
  gridLayout->setContentsMargins(0, 0, 0, 0);
  gridLayout->setSpacing(4);

  ParameterState state = m_stateGetter();

  for (int gy = 0; gy < m_gridH; gy++) {
    for (int gx = 0; gx < m_gridW; gx++) {
      int flatIndex = gy * m_gridW + gx;

      auto *tileWidget = new QFrame(m_gridWidget);
      tileWidget->setFrameStyle(QFrame::NoFrame);
      
      auto *tileLayout = new QGridLayout(tileWidget);
      tileLayout->setContentsMargins(0, 0, 0, 0);
      tileLayout->setSpacing(0);

      // 1. Selector button (Radio button behavior)
      auto *selBtn = new QPushButton(tileWidget);
      selBtn->setCheckable(true);
      // Golden ratio: e.g. ~ 72x44
      selBtn->setMinimumSize(72, 44);
      selBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      selBtn->setToolTip(tr("Select tile %1, %2").arg(gx).arg(gy));
      selBtn->setStyleSheet(
          "QPushButton { background-color: #e0e0e0; border: 1px solid #888; border-radius: 4px; }"
          "QPushButton:hover { background-color: #d0d0d0; }"
          "QPushButton:checked { background-color: #a5d6a7; border: 2px solid #2e7d32; font-weight: bold; }"
      );

      m_selectorGroup->addButton(selBtn, flatIndex);
      // Add selBtn to the grid spanning 2x2 cells so it fills the entire frame
      tileLayout->addWidget(selBtn, 0, 0, 2, 2);
      m_tileSelectors[gy][gx] = selBtn;

      // 2. Enable checkbox (overlaid in bottom right)
      auto *enBtn = new QCheckBox(tileWidget);
      enBtn->setToolTip(tr("Enable/Disable tile %1, %2").arg(gx).arg(gy));
      // Give it no text
      enBtn->setText("");
      // Using layout margins instead of CSS so we don't break native styling
      enBtn->setContentsMargins(0, 0, 4, 4);
      enBtn->setChecked(state.rparams.get_tile_adjustment(img->stitch, gx, gy).enabled);

      // Add enBtn to bottom right corner (row 1, col 1, aligned bottom-right)
      tileLayout->addWidget(enBtn, 1, 1, Qt::AlignBottom | Qt::AlignRight);
      m_tileChecks[gy][gx] = enBtn;

      // enBtn is a QCheckBox, so its toggled signal passes a boolean
      connect(enBtn, &QCheckBox::toggled, this, [this, gx, gy](bool checked) {
        ParameterState s = m_stateGetter();
        s.rparams.get_tile_adjustment(gx, gy).enabled = checked;
        m_stateSetter(s, tr("Toggle tile %1,%2").arg(gx).arg(gy));
      });

      gridLayout->addWidget(tileWidget, gy, gx);
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
