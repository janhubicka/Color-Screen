#include "ScreenPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "TilePreviewPanel.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <map>
#include <vector>

using namespace colorscreen;

namespace {
class ScreenComboBox : public QComboBox {
public:
  using QComboBox::QComboBox;

protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentIcon = QIcon();
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
  }
};

class ScreenPreviewPanel : public TilePreviewPanel {
public:
  ScreenPreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                     ImageGetter imageGetter, QWidget *parent = nullptr)
      : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent, false) {
    setDebounceInterval(5);
  }

  void init(const QString &title) { setupTiles(title); }

protected:
  std::vector<std::pair<render_screen_tile_type, QString>>
  getTileTypes() const override {
    return {{original_screen, "Screen"}};
  }

  bool shouldUpdateTiles(const ParameterState &state) override {
    if ((int)state.scrToImg.type != m_lastScrType)
      return true;
    if (state.rparams.red_strip_width != m_lastRedStripWidth)
      return true;
    if (state.rparams.green_strip_width != m_lastGreenStripWidth)
      return true;
    return false;
  }

  void onTileUpdateScheduled() override {
    ParameterState state = m_stateGetter();
    m_lastScrType = (int)state.scrToImg.type;
    m_lastRedStripWidth = state.rparams.red_strip_width;
    m_lastGreenStripWidth = state.rparams.green_strip_width;
  }

  bool requiresScan() const override { return false; }

private:
  int m_lastScrType = -1;
  double m_lastRedStripWidth = -1.0;
  double m_lastGreenStripWidth = -1.0;
};
} // namespace

ScreenPanel::ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
                         ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ScreenPanel::~ScreenPanel() = default;

void ScreenPanel::reattachPreview(QWidget *widget) {
  if (m_previewPanel)
    m_previewPanel->reattachTiles(widget);
}

void ScreenPanel::setupUi() {
  // Screen Type Selector
  QComboBox *screenCombo = new ScreenComboBox();
  screenCombo->view()->setIconSize(QSize(64, 64));

  for (int i = 0; i < max_scr_type; ++i) {
    if (!scr_names[i].name)
      continue;

    scr_type type = (scr_type)i;
    QString name = QString::fromUtf8(scr_names[i].pretty_name);

    if (type == Random) {
      screenCombo->addItem(name, i);
    } else {
      // Render Preview
      int w = 64;
      int h = 64;
      std::vector<uint8_t> buffer(w * h * 3);

      tile_parameters tile;
      tile.pixels = buffer.data();
      tile.rowstride = w * 3;
      tile.pixelbytes = 3;
      tile.width = w;
      tile.height = h;
      tile.pos = {0.0, 0.0};
      tile.step = 1.0;

      render_parameters rparams;

      bool ok = render_screen_tile(tile, type, rparams, 1.0, original_screen,
                                   nullptr);

      if (ok) {
        QImage img(buffer.data(), w, h, w * 3, QImage::Format_RGB888);
        QIcon icon(QPixmap::fromImage(img.copy()));
        screenCombo->addItem(icon, name, i);
      } else {
        screenCombo->addItem(name, i);
      }

    }
  }

  addEnumTooltips(screenCombo, scr_names, max_scr_type);
  screenCombo->setToolTip("Select the physical color screen process (e.g., Autochrome, Joly, Dufaycolor). \"Random\" is used for modern digital images without a screen.");

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow("Screen type", screenCombo);
  } else {
    m_form->addRow("Screen type", screenCombo);
  }

  // Connect: UI -> State
  connect(screenCombo, QOverload<int>::of(&QComboBox::activated), this,
          [this, screenCombo](int index) {
            int val = screenCombo->itemData(index).toInt();
            applyChange(
                [val](ParameterState &s) { s.scrToImg.type = (scr_type)val; });
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back([screenCombo](const ParameterState &state) {
    int val = (int)state.scrToImg.type;
    int idx = screenCombo->findData(val);
    if (idx != -1) {
      screenCombo->blockSignals(true);
      screenCombo->setCurrentIndex(idx);
      screenCombo->blockSignals(false);
    }
  });

  // Autodetect Regular Screen Button
  addButtonParameter("", "Autodetect regular screen", 
      [this]() { emit autodetectRequested(); },
      [this](const ParameterState &) {
          auto img = m_imageGetter();
          return img && img->has_rgb();
      }, "Attempt to automatically identify the screen type and its orientation from the image content.");

  addSeparator("Regular screen");
  
  ScreenPreviewPanel *preview =
      new ScreenPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
  m_previewPanel = preview;
  preview->init("Screen Preview");

  connect(preview, &TilePreviewPanel::detachTilesRequested, this,
          &ScreenPanel::detachPreviewRequested);
  
  connect(preview, &TilePreviewPanel::progressStarted, this, &ScreenPanel::progressStarted);
  connect(preview, &TilePreviewPanel::progressFinished, this, &ScreenPanel::progressFinished);

  m_widgetStateUpdaters.push_back([this, preview]() {
    preview->updateUI();
    bool visible = m_stateGetter().scrToImg.type != Random;
    preview->setVisible(visible);
  });

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(preview);
  } else {
    m_form->addRow(preview);
  }

  // Red Strip Width
  addSliderParameter(
      "Red Strip Width", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.red_strip_width; },
      [](ParameterState &s, double v) { s.rparams.red_strip_width = v; },
      1.0,
      [](const ParameterState &s) {
        return screen_with_varying_strips_p(s.scrToImg.type);
      }, false, "Relative width of the red filter strips for line-screen processes like Joly or Dufaycolor.");

  // Green Strip Width
  addSliderParameter(
      "Green Strip Width", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.green_strip_width; },
      [](ParameterState &s, double v) { s.rparams.green_strip_width = v; },
      1.0,
      [](const ParameterState &s) {
        return screen_with_varying_strips_p(s.scrToImg.type);
      }, false, "Relative width of the green filter strips for line-screen processes like Joly or Dufaycolor.");

  // Element density collection threshold
  addSliderParameter(
      "Element density collection threshold", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.collection_threshold; },
      [](ParameterState &s, double v) { s.rparams.collection_threshold = v; },
      1.0, nullptr, false, "Threshold for identifying screen elements based on their color density. Smaller values require stronger color enhancement and may result in edge artefacts. Too large values may result in no data being collected at all.");

  // Collection Quality
  addEnumParameter("Collection Quality", 
      render_parameters::collection_quality_names, 
      render_parameters::max_collection_quality,
      [](const ParameterState &s) { return (int)s.rparams.collection_quality; },
      [](ParameterState &s, int v) { s.rparams.collection_quality = (render_parameters::collection_quality_t)v; },
      nullptr, "Algorithmic precision for collecting and averaging color information from the screen elements."
  );

  // Screen Demosaic
  addEnumParameter("Screen Demosaic",
      render_parameters::screen_demosaic_names,
      render_parameters::max_screen_demosaic,
      [](const ParameterState &s) { return (int)s.rparams.screen_demosaic; },
      [](ParameterState &s, int v) { s.rparams.screen_demosaic = (render_parameters::screen_demosaic_t)v; },
      nullptr, "Interpolation algorithm used to fill in missing color information between the screen elements."
  );

  // Demosaiced Image Scaling Algorithm
  addEnumParameter("Demosaiced image scaling algorithm",
      render_parameters::demosaiced_scaling_names,
      render_parameters::max_demosaiced_scaling,
      [](const ParameterState &s) { return (int)s.rparams.demosaiced_scaling; },
      [](ParameterState &s, int v) { s.rparams.demosaiced_scaling = (render_parameters::demosaiced_scaling_t)v; },
      nullptr, "Method used to rescale the internally reconstructed image to the final output resolution."
  );


  addSeparator("Denoising");

  // Screen Denoise Mode
  addEnumParameter("Denoise Mode",
      denoise_parameters::denoise_mode_names,
      (int)denoise_parameters::denoise_mode_max,
      [](const ParameterState &s) { return (int)s.rparams.screen_denoise.mode; },
      [](ParameterState &s, int v) { s.rparams.screen_denoise.mode = (denoise_parameters::denoise_mode)v; },
      nullptr, "Select denoising algorithm to reduce noise in the reconstructed image."
  );

  // Strength (h)
  addSliderParameter(
      "Strength", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.screen_denoise.strength; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.strength = v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Filtering strength for Non-local means denoising. Larger values remove more noise but may blur details.");

  // Patch Radius
  addSliderParameter(
      "Patch Radius", 1, 10, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.screen_denoise.patch_radius; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.patch_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Radius of the patch used for similarity comparison in Non-local means.");

  // Search Radius
  addSliderParameter(
      "Search Radius", 1, 30, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.screen_denoise.search_radius; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.search_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Radius of the search window for Non-local means. Larger values are slower but may produce better results.");

  // Bilateral Sigma S
  addSliderParameter(
      "Bilateral Spatial Sigma", 0.1, 10.0, 10.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.screen_denoise.bilateral_sigma_s; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.bilateral_sigma_s = v; },
      1.0,
      [](const ParameterState &s) { return s.rparams.screen_denoise.mode == denoise_parameters::bilateral; },
      false, "Spatial standard deviation for Bilateral filter. Controls the size of the smoothing neighborhood.");

  // Bilateral Sigma R
  addSliderParameter(
      "Bilateral Range Sigma", 0.01, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.screen_denoise.bilateral_sigma_r; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.bilateral_sigma_r = v; },
      1.0,
      [](const ParameterState &s) { return s.rparams.screen_denoise.mode == denoise_parameters::bilateral; },
      false, "Range standard deviation for Bilateral filter. Controls how much intensity difference is allowed while smoothing.");

  updateUI();
}
