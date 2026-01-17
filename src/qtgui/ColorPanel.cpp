#include "ColorPanel.h"
#include "SpectraChartWidget.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QVBoxLayout>

using namespace colorscreen;

ColorPanel::ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent) {
  setDebounceInterval(5); // Fast update for smoother sliders
  setupUi();
}

ColorPanel::~ColorPanel() = default;

void ColorPanel::setupUi() {
  setupTiles("Color Preview");

  // Dyes dropdown
  using color_model = render_parameters::color_model_t;
  std::map<int, QString> dyes;
  for (int i = 0; i < (int)color_model::color_model_max; ++i) {
    dyes[i] = QString::fromUtf8(render_parameters::color_model_names[i]);
  }

  addEnumParameter(
      "Dyes", dyes,
      [](const ParameterState &s) { return (int)s.rparams.color_model; },
      [](ParameterState &s, int v) { s.rparams.color_model = (color_model)v; });

  // Manual Slider Implementation for Synchronized Dyes
  auto addManualSlider =
      [this](const QString &label,
             int channel) -> QPair<QSlider *, QDoubleSpinBox *> {
    QWidget *container = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(0, 0, 0, 0);

    QSlider *slider = new QSlider(Qt::Horizontal);
    int min = -100;
    int max = 100;
    slider->setRange(min, max);

    QDoubleSpinBox *spin = new QDoubleSpinBox();
    spin->setRange(min, max);
    spin->setDecimals(0);
    spin->setSingleStep(1.0);
    spin->setSuffix(" %");

    hLayout->addWidget(slider, 1);
    hLayout->addWidget(spin, 0);

    if (m_currentGroupForm)
      m_currentGroupForm->addRow(label, container);
    else
      m_form->addRow(label, container);

    // Connect internal sync
    connect(slider, &QSlider::valueChanged, this, [spin](int val) {
      spin->blockSignals(true);
      spin->setValue(val);
      spin->blockSignals(false);
    });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider](double val) {
              slider->blockSignals(true);
              slider->setValue((int)val);
              slider->blockSignals(false);
            });

    // Updater from State
    m_paramUpdaters.push_back([slider, spin, channel](const ParameterState &s) {
      double v = 0;
      if (channel == 0)
        v = s.rparams.age.red;
      if (channel == 1)
        v = s.rparams.age.green;
      if (channel == 2)
        v = s.rparams.age.blue;

      spin->blockSignals(true);
      spin->setValue(v);
      spin->blockSignals(false);

      slider->blockSignals(true);
      slider->setValue((int)v);
      slider->blockSignals(false);
    });

    return {slider, spin};
  };

  auto red = addManualSlider("Red dye age", 0);
  auto green = addManualSlider("Green dye age", 1);
  auto blue = addManualSlider("Blue dye age", 2);

  // Common change handler
  auto handleValueChange = [this, red, green, blue](int channel,
                                                    double percentVal) {
    // 1. Get current authoritative state to calculate delta
    ParameterState s = m_stateGetter();
    double newVal = percentVal / 100.0;
    double oldVal = 0;
    if (channel == 0)
      oldVal = s.rparams.age.red;
    else if (channel == 1)
      oldVal = s.rparams.age.green;
    else if (channel == 2)
      oldVal = s.rparams.age.blue;

    double delta = newVal - oldVal;

    // 2. Calculate next values for all channels
    double nextRed = s.rparams.age.red;
    double nextGreen = s.rparams.age.green;
    double nextBlue = s.rparams.age.blue;

    if (channel == 0)
      nextRed = newVal;
    else if (channel == 1)
      nextGreen = newVal;
    else if (channel == 2)
      nextBlue = newVal;

    if (m_linkDyeAges->isChecked()) {
      // Apply delta to others (preserving offset)
      if (channel != 0)
        nextRed += delta;
      if (channel != 1)
        nextGreen += delta;
      if (channel != 2)
        nextBlue += delta;
    }

    // 3. Apply to State
    applyChange([nextRed, nextGreen, nextBlue](ParameterState &state) {
      state.rparams.age.red = nextRed;
      state.rparams.age.green = nextGreen;
      state.rparams.age.blue = nextBlue;
    });

    // 4. Force Visual Update of linked sliders (Optimistic UI)
    if (m_linkDyeAges->isChecked()) {
      auto updateWidget = [](QPair<QSlider *, QDoubleSpinBox *> pair,
                             double val) {
        double p = val * 100.0;
        pair.first->blockSignals(true);
        pair.first->setValue((int)p);
        pair.first->blockSignals(false);

        pair.second->blockSignals(true);
        pair.second->setValue(p);
        pair.second->blockSignals(false);
      };

      if (channel != 0)
        updateWidget(red, nextRed);
      if (channel != 1)
        updateWidget(green, nextGreen);
      if (channel != 2)
        updateWidget(blue, nextBlue);
    }
  };

  // Connect user interaction
  // Red
  connect(red.second, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [handleValueChange](double v) { handleValueChange(0, v); });
  connect(red.first, &QSlider::valueChanged, this,
          [handleValueChange](int v) { handleValueChange(0, (double)v); });

  // Green
  connect(green.second, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [handleValueChange](double v) { handleValueChange(1, v); });
  connect(green.first, &QSlider::valueChanged, this,
          [handleValueChange](int v) { handleValueChange(1, (double)v); });

  // Blue
  connect(blue.second, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [handleValueChange](double v) { handleValueChange(2, v); });
  connect(blue.first, &QSlider::valueChanged, this,
          [handleValueChange](int v) { handleValueChange(2, (double)v); });

  // Link Ages Checkbox (Moved to bottom)
  m_linkDyeAges = new QCheckBox("Link ages");
  m_linkDyeAges->setChecked(true);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow("", m_linkDyeAges);
  else
    m_form->addRow("", m_linkDyeAges);

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
  m_spectraSection = createDetachableSection(
      "Spectral Transmitance", chartWrapper, [this, chartWrapper]() {
        emit detachSpectraChartRequested(chartWrapper);
      });

  m_spectraContainer->addWidget(m_spectraSection);

  // Add to form layout (after Dyes)
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(spectraWrapper);
  else
    m_form->addRow(spectraWrapper);

  // Spacer to push everything up
  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(spacer);
  else
    m_form->addRow(spacer);

  // Future: Add color parameters here
  updateUI();
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
    if (m_spectraSection)
      m_spectraSection->show();
  } else {
    m_spectraChart->clear();
    if (m_spectraSection)
      m_spectraSection->hide();
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
  if (!(m_lastRParams == state.rparams) ||
      (int)state.scrToImg.type != m_lastScrType)
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
