#include "ColorPanel.h"
#include "CIEChartWidget.h"
#include "SpectraChartWidget.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

using namespace colorscreen;

namespace {
class CorrectedPreviewPanel : public TilePreviewPanel {
public:
  CorrectedPreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                        ImageGetter imageGetter, QWidget *parent = nullptr)
      : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent, false) {
  }

  void init(const QString &title) { setupTiles(title); }

protected:
  std::vector<std::pair<render_screen_tile_type, QString>>
  getTileTypes() const override {
    return {{corrected_backlight_screen, "Backlight"},
            {corrected_detail_screen, "Detail"},
            {corrected_full_screen, "Screen"}};
  }

  bool shouldUpdateTiles(const ParameterState &state) override {
    if (!(m_lastRParams == state.rparams) ||
        (int)state.scrToImg.type != m_lastScrType)
      return true;
    return false;
  }

  void onTileUpdateScheduled() override {
    ParameterState state = m_stateGetter();
    m_lastScrType = (int)state.scrToImg.type;
    m_lastRParams = state.rparams;
  }

  bool isTileRenderingEnabled(const ParameterState &state) const override {
    return true;
  }

  bool requiresScan() const override { return false; }

private:
  render_parameters m_lastRParams;
  int m_lastScrType = -1;
};
} // namespace

ColorPanel::ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                       ImageGetter imageGetter, QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent) {
  setDebounceInterval(5); // Fast update for smoother sliders
  setupUi();
}

ColorPanel::~ColorPanel() = default;

void ColorPanel::reattachCorrectedTiles(QWidget *widget) {
  if (m_correctedPreview)
    m_correctedPreview->reattachTiles(widget);
}

void ColorPanel::setupUi() {
  setupTiles("Color Preview");

  // Dyes dropdown (Moved before Spectral Chart)
  using color_model = render_parameters::color_model_t;
  std::map<int, QString> dyes;
  for (int i = 0; i < (int)color_model::color_model_max; ++i) {
    dyes[i] = QString::fromUtf8(render_parameters::color_model_properties[i].pretty_name);
  }

  QComboBox *dyesCombo = addEnumParameter(
      "Dyes", dyes,
      [](const ParameterState &s) { return (int)s.rparams.color_model; },
      [](ParameterState &s, int v) { s.rparams.color_model = (color_model)v; });

  // Add Tooltips
  for (int i = 0; i < dyesCombo->count(); ++i) {
    int val = dyesCombo->itemData(i).toInt();
    if (val >= 0 && val < (int)color_model::color_model_max) {
      dyesCombo->setItemData(
          i, QString::fromUtf8(render_parameters::color_model_properties[val].description),
          Qt::ToolTipRole);
    }
  }

  // Spectral Transmitance Chart
  m_spectraChart = new SpectraChartWidget();

  // Mode Selector
  m_spectraMode = new QComboBox();
  m_spectraMode->addItem("Transmittance", 0);
  m_spectraMode->addItem("Absorbance (0-4)", 1);
  connect(m_spectraMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateSpectraChart(); });

  // Wrap layout in a widget to add to FormLayout
  QWidget *spectraWrapper = new QWidget();
  m_spectraContainer = new QVBoxLayout(spectraWrapper);
  m_spectraContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *chartWrapper = new QWidget();
  QVBoxLayout *wrapperLayout = new QVBoxLayout(chartWrapper);
  wrapperLayout->setContentsMargins(0, 0, 0, 0);

  // Header with mode
  QHBoxLayout *headerLayout = new QHBoxLayout();
  headerLayout->addStretch();
  headerLayout->addWidget(new QLabel("Mode:"));
  headerLayout->addWidget(m_spectraMode);
  wrapperLayout->addLayout(headerLayout);

  wrapperLayout->addWidget(m_spectraChart);

  // Detachable section
  m_spectraSection = createDetachableSection(
      "Spectral Chart", chartWrapper, [this, chartWrapper]() {
        emit detachSpectraChartRequested(chartWrapper);
      });

  m_spectraContainer->addWidget(m_spectraSection);

  // Add to form layout (at the top)
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(spectraWrapper);
  else
    m_form->addRow(spectraWrapper);



  // Manual Slider Implementation for Synchronized Dyes
  auto addManualSlider =
      [this](const QString &label,
             int channel) -> QPair<QSlider *, QDoubleSpinBox *> {
    QWidget *container = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(0, 0, 0, 0);

    QSlider *slider = new QSlider(Qt::Horizontal);
    int min = -100;
    int max = 200;
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

      // Convert internal 0-1 range (approx) to percent 0-100
      v *= 100.0;

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

  // Updater for slider enablement
  m_paramUpdaters.push_back([red, green, blue, this](const ParameterState &s) {
    bool supportsAging =
        render_parameters::color_model_properties[s.rparams.color_model].flags &
        render_parameters::SUPPORTS_AGING;
    
    red.first->setEnabled(supportsAging);
    red.second->setEnabled(supportsAging);
    green.first->setEnabled(supportsAging);
    green.second->setEnabled(supportsAging);
    blue.first->setEnabled(supportsAging);
    blue.second->setEnabled(supportsAging);
    m_linkDyeAges->setEnabled(supportsAging);
  });

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

  // Backlight Temperature
  {
    QWidget *container = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(container);
    hLayout->setContentsMargins(0, 0, 0, 0);

    QSlider *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 1000); // Internal resolution

    QDoubleSpinBox *spin = new QDoubleSpinBox();
    spin->setRange(2500, 25000);
    spin->setSingleStep(100);
    spin->setSuffix(" K");

    hLayout->addWidget(slider, 1);
    hLayout->addWidget(spin, 0);

    if (m_currentGroupForm)
      m_currentGroupForm->addRow("Backlight temperature", container);
    else
      m_form->addRow("Backlight temperature", container);

    // cubic mapping: K = min + (max - min) * (t^3)
    auto toK = [](int sliderVal) -> double {
      double t = sliderVal / 1000.0;
      return 2500.0 + (25000.0 - 2500.0) * (t * t * t);
    };

    auto toSlider = [](double k) -> int {
      double t3 = (k - 2500.0) / (25000.0 - 2500.0);
      double t = std::cbrt(t3); // cubic root
      return std::clamp((int)(t * 1000.0), 0, 1000);
    };

    // Connect synchronization
    connect(slider, &QSlider::valueChanged, this, [spin, toK](int val) {
      spin->blockSignals(true);
      spin->setValue(toK(val));
      spin->blockSignals(false);
    });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider, toSlider](double val) {
              slider->blockSignals(true);
              slider->setValue(toSlider(val));
              slider->blockSignals(false);
            });

    // Updater
    m_paramUpdaters.push_back(
        [slider, spin, toSlider](const ParameterState &s) {
          double v = s.rparams.backlight_temperature;
          spin->blockSignals(true);
          spin->setValue(v);
          spin->blockSignals(false);

          slider->blockSignals(true);
          slider->setValue(toSlider(v));
          slider->blockSignals(false);
        });

    // Change handler
    auto handler = [this, toK](int sliderVal) {
      applyChange([toK, sliderVal](ParameterState &s) {
        s.rparams.backlight_temperature = toK(sliderVal);
      });
    };
    // Note: Spinbox handler needs to be separate or adapted
    auto spinHandler = [this](double val) {
      applyChange(
          [val](ParameterState &s) { s.rparams.backlight_temperature = val; });
    };

    connect(slider, &QSlider::valueChanged, this, handler);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            spinHandler);
  }

  // Separator
  addSeparator("Viewing conditions correction");

  // Corrected Color Preview
  {
    CorrectedPreviewPanel *correctedPreview =
        new CorrectedPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
    m_correctedPreview = correctedPreview;
    correctedPreview->init("Corrected Color Preview");

    connect(correctedPreview, &TilePreviewPanel::detachTilesRequested, this,
            &ColorPanel::detachCorrectedTilesRequested);

    // We need to trigger updates when parent updates
    m_widgetStateUpdaters.push_back(
        [correctedPreview]() { correctedPreview->updateUI(); });

    if (m_currentGroupForm)
      m_currentGroupForm->addRow(correctedPreview);
    else
      m_form->addRow(correctedPreview);
  }

  // Dye Balancing Selector
  {
    using dye_balance = render_parameters::dye_balance_t;
    std::map<int, QString> modes;
    for (int i = 0; i < (int)dye_balance::dye_balance_max; ++i) {
      modes[i] = QString::fromUtf8(render_parameters::dye_balance_names[i].pretty_name);
    }

    addEnumParameter(
        "Dye balancing", modes,
        [](const ParameterState &s) { return (int)s.rparams.dye_balance; },
        [](ParameterState &s, int v) {
          s.rparams.dye_balance = (dye_balance)v;
        });
  }

  // Observer Whitepoint
  {
    QWidget *wrapper = new QWidget();
    QHBoxLayout *hLayout = new QHBoxLayout(wrapper);
    hLayout->setContentsMargins(0, 5, 0, 5); // Add some vertical breathing room

    QLabel *label = new QLabel("Observer whitepoint");
    // Ensure vertical centering relative to the tall chart
    hLayout->addWidget(label, 0, Qt::AlignVCenter);

    // "Move selector more to right"
    hLayout->addSpacing(40);

    CIEChartWidget *cieChart = new CIEChartWidget();
    cieChart->setFixedHeight(200); // Reasonable height
    cieChart->setFixedWidth(
        200); // Also fix width to keep aspect sensible or let it expand?
              // User said "move to right", implying it might be small?
              // Let's allow expanding but maybe add a stretch before it?
              // Actually, standard behavior is fine, just added spacing.
    hLayout->addWidget(cieChart);

    // If we want it to push to the right edge:
    // hLayout->addStretch();
    // But usually charts should be left-aligned after the label/spacing.
    // I'll stick to addSpacing(40).

    if (m_currentGroupForm)
      m_currentGroupForm->addRow(wrapper);
    else
      m_form->addRow(wrapper);

    // Connect updates
    connect(cieChart, &CIEChartWidget::whitepointChanged, this,
            [this](double x, double y) {
              applyChange([x, y](ParameterState &s) {
                s.rparams.observer_whitepoint = colorscreen::xy_t(x, y);
              });
            });

    // Update from state
    m_paramUpdaters.push_back([cieChart](const ParameterState &s) {
      cieChart->setWhitepoint(s.rparams.observer_whitepoint.x,
                              s.rparams.observer_whitepoint.y);
    });
  }

  // Spacer to push everything up
  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(spacer);
  else
    m_form->addRow(spacer);

  // Future: Add color parameters here
  
    // Updater for Spectra Chart visibility in main layout
  m_paramUpdaters.push_back([this](const ParameterState &s) {
    bool spectraBased =
        render_parameters::color_model_properties[s.rparams.color_model].flags &
        render_parameters::SPECTRA_BASED;
        
     if (spectraBased) {
         if (m_spectraSection) m_spectraSection->show();
     } else {
         if (m_spectraSection) m_spectraSection->hide();
     }
  });
  
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
    bool absorbance = (m_spectraMode->currentIndex() == 1);

    if (absorbance) {
      // Convert to Absorbance
      for (auto &v : data.red)
        v = colorscreen::transmitance_to_absorbance(v);
      for (auto &v : data.green)
        v = colorscreen::transmitance_to_absorbance(v);
      for (auto &v : data.blue)
        v = colorscreen::transmitance_to_absorbance(v);
      for (auto &v : data.backlight)
        v = colorscreen::transmitance_to_absorbance(v);

      m_spectraChart->setYAxis(0.0, 4.0, "Absorbance", "");
    } else {
      m_spectraChart->setYAxis(0.0, 1.0, "Transmittance", "%");
    }

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
