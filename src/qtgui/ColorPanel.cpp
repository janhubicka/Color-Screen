#include "ColorPanel.h"
#include "CIEChartWidget.h"
#include "SpectraChartWidget.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QResizeEvent>

using namespace colorscreen;

namespace {
class CorrectedPreviewPanel : public TilePreviewPanel {
public:
  CorrectedPreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                        ImageGetter imageGetter, QWidget *parent = nullptr)
      : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent, false) {
    setDebounceInterval(5);
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
  addSeparator("Adjustments in process color space");

  // dark point
  addSliderParameter(
      "Black", 0, 1, 1, 4, "", "",
      [](const ParameterState &s) { return s.rparams.dark_point; },
      [](ParameterState &s, double v) { s.rparams.dark_point = v; }, 3.0,
      nullptr, false);

  // Presaturation
  addSliderParameter(
      "Presaturation", 0, 100, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.presaturation; },
      [](ParameterState &s, double v) { s.rparams.presaturation = v; }, 1.0);

  // White Balance
  addSliderParameter(
      "White balance red", 0, 10, 100, 2, "", "",
      [](const ParameterState &s) { return s.rparams.white_balance.red; },
      [](ParameterState &s, double v) { s.rparams.white_balance.red = v; }, 1.0);

  addSliderParameter(
      "White balance green", 0, 10, 100, 2, "", "",
      [](const ParameterState &s) { return s.rparams.white_balance.green; },
      [](ParameterState &s, double v) { s.rparams.white_balance.green = v; }, 1.0);

  addSliderParameter(
      "White balance blue", 0, 10, 100, 2, "", "",
      [](const ParameterState &s) { return s.rparams.white_balance.blue; },
      [](ParameterState &s, double v) { s.rparams.white_balance.blue = v; }, 1.0);

  m_currentGroupForm = nullptr; // End Adjustments section

  addSeparator("Backlight");

  // Backlight intensity
  addSliderParameter(
      "Backlight intensity", 0, 65535, 1, 2, "", "",
      [](const ParameterState &s) { return s.rparams.brightness; },
      [](ParameterState &s, double v) { s.rparams.brightness = v; }, 3.0,
      nullptr, true);

  // Backlight temperature
  addSliderParameter(
      "Backlight temperature", 2500, 25000, 1, 0, "K", "",
      [](const ParameterState &s) { return s.rparams.backlight_temperature; },
      [](ParameterState &s, double v) { s.rparams.backlight_temperature = v; },
      3.0);

  m_currentGroupForm = nullptr; // End Backlight section
  addSeparator("Screen dyes");
  setupTiles("Color Preview");

  // Gamut Chart
  m_gamutChart = new CIEChartWidget();
  m_gamutChart->setFixedHeight(200);
  m_gamutChart->setSelectionEnabled(false);

  m_gamutReferenceCombo = new QComboBox();
  m_gamutReferenceCombo->addItems({"None", "sRGB", "AdobeRGB", "SMPTE-C"});
  connect(m_gamutReferenceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int){ updateGamutReference(); });

  QLabel *refLabel = new QLabel("Reference gamut:");
  QHBoxLayout *refLayout = new QHBoxLayout();
  refLayout->addWidget(refLabel);
  refLayout->addWidget(m_gamutReferenceCombo, 1);

  QWidget *gamutWrapper = new QWidget();
  m_gamutContainer = new QVBoxLayout(gamutWrapper);
  m_gamutContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *gamutChartWrapper = new QWidget();
  QVBoxLayout *cwLayout = new QVBoxLayout(gamutChartWrapper);
  cwLayout->setContentsMargins(0, 0, 0, 0);
  cwLayout->addLayout(refLayout);
  cwLayout->addWidget(m_gamutChart, 0, Qt::AlignCenter);

  m_gamutSection = createDetachableSection(
      "Gamut", gamutChartWrapper,
      [this, gamutChartWrapper]() { 
            m_gamutChart->setMinimumSize(0,0);
            m_gamutChart->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_gamutChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            if (gamutChartWrapper->layout()) gamutChartWrapper->layout()->setAlignment(m_gamutChart, Qt::Alignment());
            emit detachGamutChartRequested(gamutChartWrapper); 
      });

  m_gamutContainer->addWidget(m_gamutSection);

  // Add to form layout
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(gamutWrapper);
  else
    m_form->addRow(gamutWrapper);

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



  addCorrelatedRGBParameter(
      "dye age", -10.0, 110.0, 1.0, 0, "%",
      [](const ParameterState &s) { return s.rparams.age * 100.0; },
      [](ParameterState &s, const colorscreen::rgbdata &v) {
        s.rparams.age = v / 100.0;
      },
      [](const ParameterState &s) {
        return render_parameters::color_model_properties[s.rparams.color_model]
                   .flags &
               render_parameters::SUPPORTS_AGING;
      });

  addCorrelatedRGBParameter(
      "dye density", 0.0, 1000.0, 1.0, 0, "%",
      [](const ParameterState &s) { return s.rparams.dye_density * 100.0; },
      [](ParameterState &s, const colorscreen::rgbdata &v) {
        s.rparams.dye_density = v / 100.0;
      });

  m_currentGroupForm = nullptr; // End Screen dyes section
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

    connect(correctedPreview, &TilePreviewPanel::progressStarted, this, &ColorPanel::progressStarted);
    connect(correctedPreview, &TilePreviewPanel::progressFinished, this, &ColorPanel::progressFinished);

    // We need to trigger updates when parent updates
    m_widgetStateUpdaters.push_back(
        [correctedPreview]() { correctedPreview->updateUI(); });

    if (m_currentGroupForm)
      m_currentGroupForm->addRow(correctedPreview);
    else
      m_form->addRow(correctedPreview);
  }

  // Corrected Gamut Chart
  m_correctedGamutChart = new CIEChartWidget();
  m_correctedGamutChart->setFixedHeight(200);
  m_correctedGamutChart->setSelectionEnabled(false);

  m_correctedGamutReferenceCombo = new QComboBox();
  m_correctedGamutReferenceCombo->addItems({"None", "sRGB", "AdobeRGB", "SMPTE-C"});
  connect(m_correctedGamutReferenceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int){ updateCorrectedGamutReference(); });

  QLabel *corrRefLabel = new QLabel("Reference gamut:");
  QHBoxLayout *corrRefLayout = new QHBoxLayout();
  corrRefLayout->addWidget(corrRefLabel);
  corrRefLayout->addWidget(m_correctedGamutReferenceCombo, 1);

  QWidget *corrGamutWrapper = new QWidget();
  m_correctedGamutContainer = new QVBoxLayout(corrGamutWrapper);
  m_correctedGamutContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *corrGamutChartWrapper = new QWidget();
  QVBoxLayout *cwCorrLayout = new QVBoxLayout(corrGamutChartWrapper);
  cwCorrLayout->setContentsMargins(0, 0, 0, 0);
  cwCorrLayout->addLayout(corrRefLayout);
  cwCorrLayout->addWidget(m_correctedGamutChart, 0, Qt::AlignCenter);

  m_correctedGamutSection = createDetachableSection(
      "Corrected Gamut", corrGamutChartWrapper,
      [this, corrGamutChartWrapper]() { 
            m_correctedGamutChart->setMinimumSize(0,0);
            m_correctedGamutChart->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_correctedGamutChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            if (corrGamutChartWrapper->layout()) corrGamutChartWrapper->layout()->setAlignment(m_correctedGamutChart, Qt::Alignment());
            emit detachCorrectedGamutChartRequested(corrGamutChartWrapper); 
      });

  m_correctedGamutContainer->addWidget(m_correctedGamutSection);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(corrGamutWrapper);
  else
    m_form->addRow(corrGamutWrapper);

  // Dye Balancing Selector
  addEnumParameter(
      "Dye balancing", render_parameters::dye_balance_names,
      (int)render_parameters::dye_balance_t::dye_balance_max,
      [](const ParameterState &s) { return (int)s.rparams.dye_balance; },
      [](ParameterState &s, int v) {
        s.rparams.dye_balance = (render_parameters::dye_balance_t)v;
      });

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
  size_t size = (size_t)((data.max_freq - data.min_freq) / 5 + 1);
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
  updateGamutChart();
  updateCorrectedGamutChart();
}

bool ColorPanel::isTileRenderingEnabled(const ParameterState &state) const {
  // User requested "Make the new ColorPanel appear even when scr_to_img type is
  // Random"
  return true;
}

void ColorPanel::applyChange(std::function<void(ParameterState &)> modifier, const QString &description) {
  ParameterPanel::applyChange(modifier, description);
  scheduleTileUpdate();
}

QWidget *ColorPanel::getGamutChartWidget() const { return m_gamutChart; }

void ColorPanel::reattachGamutChart(QWidget *widget) {
  if (!widget)
    return;
    
  // Restore docked constraints
  m_gamutChart->setFixedHeight(200);
  // Width handled by resizeEvent
  m_gamutChart->updateGeometry();
  
  // Restore alignment
  if (widget->layout()) widget->layout()->setAlignment(m_gamutChart, Qt::AlignCenter);

  // Re-wrap in detachable section
  QWidget *detachable = createDetachableSection(
      "Gamut", widget,
      [this, widget]() { 
            m_gamutChart->setMinimumSize(0,0);
            m_gamutChart->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_gamutChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            if (widget->layout()) widget->layout()->setAlignment(m_gamutChart, Qt::Alignment());
            emit detachGamutChartRequested(widget); 
      });

  m_gamutContainer->addWidget(detachable);
  if (width() > 0) m_gamutChart->setFixedWidth(width() / 2);
}

void ColorPanel::resizeEvent(QResizeEvent *event) {
    TilePreviewPanel::resizeEvent(event);
    if (m_gamutSection && m_gamutSection->isVisible() && m_gamutSection->isAncestorOf(m_gamutChart)) {
         m_gamutChart->setFixedWidth(event->size().width() / 2);
    }
    if (m_correctedGamutSection && m_correctedGamutSection->isVisible() && m_correctedGamutSection->isAncestorOf(m_correctedGamutChart)) {
         m_correctedGamutChart->setFixedWidth(event->size().width() / 2);
    }
}

void ColorPanel::updateGamutChart() {
    if (!m_gamutChart) return;
    ParameterState state = m_stateGetter();
    
    auto gamut = state.rparams.get_gamut(false, state.scrToImg.type);
    
    CIEChartWidget::GamutData data;
    data.valid = true;
    data.rx = gamut.red.x; data.ry = gamut.red.y;
    data.gx = gamut.green.x; data.gy = gamut.green.y;
    data.bx = gamut.blue.x; data.by = gamut.blue.y;
    data.wx = gamut.whitepoint.x; data.wy = gamut.whitepoint.y;
    
    m_gamutChart->setGamut(data);
}

void ColorPanel::updateGamutReference() {
    if (!m_gamutChart || !m_gamutReferenceCombo) return;
    QString txt = m_gamutReferenceCombo->currentText();
    CIEChartWidget::GamutData data;
    data.valid = false;
    
    if (txt == "sRGB") {
        data.valid = true;
        data.rx = 0.64; data.ry = 0.33;
        data.gx = 0.30; data.gy = 0.60;
        data.bx = 0.15; data.by = 0.06;
        data.wx = 0.3127; data.wy = 0.3290;
    } else if (txt == "AdobeRGB") {
        data.valid = true;
        data.rx = 0.64; data.ry = 0.33;
        data.gx = 0.21; data.gy = 0.71;
        data.bx = 0.15; data.by = 0.06;
        data.wx = 0.3127; data.wy = 0.3290;
    } else if (txt == "SMPTE-C") {
        data.valid = true;
        data.rx = 0.630; data.ry = 0.340;
        data.gx = 0.310; data.gy = 0.595;
        data.bx = 0.155; data.by = 0.070;
        data.wx = 0.3127; data.wy = 0.3290;
    }
    
    m_gamutChart->setReferenceGamut(data);
}

QWidget *ColorPanel::getCorrectedGamutChartWidget() const { return m_correctedGamutChart; }

void ColorPanel::reattachCorrectedGamutChart(QWidget *widget) {
  if (!widget)
    return;
    
  // Restore docked constraints
  m_correctedGamutChart->setFixedHeight(200);
  m_correctedGamutChart->updateGeometry();
  
  // Restore alignment
  if (widget->layout()) widget->layout()->setAlignment(m_correctedGamutChart, Qt::AlignCenter);

  // Re-wrap in detachable section
  QWidget *detachable = createDetachableSection(
      "Corrected Gamut", widget,
      [this, widget]() { 
            m_correctedGamutChart->setMinimumSize(0,0);
            m_correctedGamutChart->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_correctedGamutChart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            if (widget->layout()) widget->layout()->setAlignment(m_correctedGamutChart, Qt::Alignment());
            emit detachCorrectedGamutChartRequested(widget); 
      });

  m_correctedGamutContainer->addWidget(detachable);
  if (width() > 0) m_correctedGamutChart->setFixedWidth(width() / 2);
}

void ColorPanel::updateCorrectedGamutChart() {
    if (!m_correctedGamutChart) return;
    ParameterState state = m_stateGetter();
    
    auto gamut = state.rparams.get_gamut(true, state.scrToImg.type);
    
    CIEChartWidget::GamutData data;
    data.valid = true;
    data.rx = gamut.red.x; data.ry = gamut.red.y;
    data.gx = gamut.green.x; data.gy = gamut.green.y;
    data.bx = gamut.blue.x; data.by = gamut.blue.y;
    data.wx = gamut.whitepoint.x; data.wy = gamut.whitepoint.y;
    
    m_correctedGamutChart->setGamut(data);
}

void ColorPanel::updateCorrectedGamutReference() {
    if (!m_correctedGamutChart || !m_correctedGamutReferenceCombo) return;
    QString txt = m_correctedGamutReferenceCombo->currentText();
    CIEChartWidget::GamutData data;
    data.valid = false;
    
    if (txt == "sRGB") {
        data.valid = true;
        data.rx = 0.64; data.ry = 0.33;
        data.gx = 0.30; data.gy = 0.60;
        data.bx = 0.15; data.by = 0.06;
        data.wx = 0.3127; data.wy = 0.3290;
    } else if (txt == "AdobeRGB") {
        data.valid = true;
        data.rx = 0.64; data.ry = 0.33;
        data.gx = 0.21; data.gy = 0.71;
        data.bx = 0.15; data.by = 0.06;
        data.wx = 0.3127; data.wy = 0.3290;
    } else if (txt == "SMPTE-C") {
        data.valid = true;
        data.rx = 0.630; data.ry = 0.340;
        data.gx = 0.310; data.gy = 0.595;
        data.bx = 0.155; data.by = 0.070;
        data.wx = 0.3127; data.wy = 0.3290;
    }
    
    m_correctedGamutChart->setReferenceGamut(data);
}
