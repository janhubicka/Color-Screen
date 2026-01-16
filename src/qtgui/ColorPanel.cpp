#include "ColorPanel.h"
#include "SpectraChartWidget.h"
#include <QFormLayout>
#include <QVBoxLayout>

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

  // Spectral Transmitance Chart
  m_spectraChart = new SpectraChartWidget();

  // Wrap layout in a widget to add to FormLayout
  QWidget *spectraWrapper = new QWidget();
  m_spectraContainer = new QVBoxLayout(spectraWrapper);
  m_spectraContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *chartWrapper = new QWidget();
  QVBoxLayout *wrapperLayout = new QVBoxLayout(chartWrapper);
  wrapperLayout->setContentsMargins(0, 0, 0, 0);
  wrapperLayout->addWidget(m_spectraChart);

  // Detachable section
  QWidget *detachableChart = createDetachableSection(
      "Spectral Transmitance", chartWrapper, [this, chartWrapper]() {
        emit detachSpectraChartRequested(chartWrapper);
      });

  m_spectraContainer->addWidget(detachableChart);

  // Add to form layout (after Dyes)
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(spectraWrapper);
  else
    m_form->addRow(spectraWrapper);

  // Future: Add color parameters here
}

void ColorPanel::updateSpectraChart() {
  ParameterState state = m_stateGetter();
  render_parameters::transmission_data data;
  data.min_freq = 400;
  data.max_freq = 720;
  // 1nm resolution
  size_t size = (size_t)(data.max_freq - data.min_freq + 1);
  data.red.resize(size);
  data.green.resize(size);
  data.blue.resize(size);
  data.backlight.resize(size);

  if (state.rparams.get_transmission_data(data)) {
    m_spectraChart->setSpectraData(data.red, data.green, data.blue,
                                   data.backlight);
  } else {
    m_spectraChart->clear();
  }
}

QWidget *ColorPanel::getSpectraChartWidget() const { return m_spectraChart; }

void ColorPanel::reattachSpectraChart(QWidget *widget) {
  if (!widget)
    return;

  // Re-wrap in detachable section
  QWidget *detachable = createDetachableSection(
      "Spectral Transmitance", widget,
      [this, widget]() { emit detachSpectraChartRequested(widget); });

  m_spectraContainer->addWidget(detachable);
}

std::vector<std::pair<render_screen_tile_type, QString>>
ColorPanel::getTileTypes() const {
  return {{backlight_screen, "Backlight"},
          {detail_screen, "Detail"},
          {full_screen, "Screen"}};
}

bool ColorPanel::shouldUpdateTiles(const ParameterState &state) {
  if (!(m_lastRParams == state.rparams)
      || (int)state.scrToImg.type != m_lastScrType)
    return true;

  return false;
}

void ColorPanel::onTileUpdateScheduled() {
  ParameterState state = m_stateGetter();
  m_lastScrType = (int)state.scrToImg.type;
  m_lastRParams = state.rparams;

  updateSpectraChart();
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
