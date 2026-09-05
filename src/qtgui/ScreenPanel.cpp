#include "ScreenPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "TilePreviewPanel.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStandardItemModel>
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

enum class ScreenDetectionAvailability {
  Ready,
  NoImage,
  CaptureUnknown,
  NoHistoricalScreen,
  StochasticScreen,
  SelectRegularScreen,
  NeedsRgb
};

/** Return whether regular-screen detection can run for STATE and IMAGE. */
ScreenDetectionAvailability
screenDetectionAvailability(const ParameterState &state,
                            const image_data *image) {
  if (!image)
    return ScreenDetectionAvailability::NoImage;

  const auto capture = state.rparams.get_capture_type(image);
  if (capture == render_parameters::capture_unknown)
    return ScreenDetectionAvailability::CaptureUnknown;
  if (!render_parameters::capture_has_screen_p(capture))
    return ScreenDetectionAvailability::NoHistoricalScreen;

  if (render_parameters::capture_requires_regular_screen_p(capture)) {
    return screen_has_regular_geometry_p(state.scrToImg.type)
               ? ScreenDetectionAvailability::Ready
               : ScreenDetectionAvailability::SelectRegularScreen;
  }

  if (screen_has_regular_geometry_p(state.scrToImg.type))
    return ScreenDetectionAvailability::Ready;
  if (stochastic_screen_p(state.scrToImg.type))
    return ScreenDetectionAvailability::StochasticScreen;
  if (state.scrToImg.type == NoScreen)
    return image->has_rgb() ? ScreenDetectionAvailability::Ready
                            : ScreenDetectionAvailability::NeedsRgb;
  return ScreenDetectionAvailability::SelectRegularScreen;
}

/** Explain why regular-screen detection is unavailable, or return empty text. */
QString screenDetectionUnavailableHint(ScreenDetectionAvailability availability) {
  switch (availability) {
  case ScreenDetectionAvailability::Ready:
    return QString();
  case ScreenDetectionAvailability::NoImage:
    return QStringLiteral("Load an image to use screen detection.");
  case ScreenDetectionAvailability::CaptureUnknown:
    return QStringLiteral(
        "Choose the capture type in Digital capture before detecting a screen.");
  case ScreenDetectionAvailability::NoHistoricalScreen:
    return QStringLiteral(
        "Screen detection is not applicable to this capture type.");
  case ScreenDetectionAvailability::StochasticScreen:
    return QStringLiteral(
        "Regular-lattice detection is not used for stochastic screens.");
  case ScreenDetectionAvailability::SelectRegularScreen:
    return QStringLiteral("Choose a regular screen type before detection.");
  case ScreenDetectionAvailability::NeedsRgb:
    return QStringLiteral(
        "Automatic screen identification needs RGB screen-colour data; choose "
        "the regular screen type first.");
  }
  return QString();
}

static bool
postDemosaicDenoiseAvailable (const ParameterState &state)
{
  const bool materialized
      = paget_like_screen_p (state.scrToImg.type)
        || dufay_like_screen_p (state.scrToImg.type);
  const auto alg = state.rparams.screen_demosaic;
  return materialized
         && (alg == render_parameters::default_demosaic
             || (int)alg >= (int)render_parameters::hamilton_adams_demosaic);
}

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
  screenCombo->setObjectName(QStringLiteral("ScreenTypeCombo"));
  screenCombo->view()->setIconSize(QSize(64, 64));

  for (int i = 0; i < max_scr_type; ++i) {
    if (!scr_names[i].name)
      continue;

    scr_type type = (scr_type)i;
    QString name = QString::fromUtf8(scr_names[i].pretty_name);

    if (!screen_has_regular_geometry_p(type)) {
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
  screenCombo->setToolTip(
      "Select the physical color screen process. None means no historical "
      "screen; Random, Autochrome and Agfa Farbenplatte are stochastic "
      "screens without a regular geometric lattice. Monochrome captures made "
      "through a color screen require a regular screen because the color "
      "identity of stochastic screen elements is not recoverable.");

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
  m_paramUpdaters.push_back([this, screenCombo](const ParameterState &state) {
    const auto img = m_imageGetter();
    const auto capture =
        img ? state.rparams.get_capture_type(img.get())
            : state.rparams.capture_type;
    const bool regularOnly =
        render_parameters::capture_requires_regular_screen_p(capture);
    if (auto *model =
            qobject_cast<QStandardItemModel *>(screenCombo->model())) {
      for (int i = 0; i < screenCombo->count(); ++i) {
        const auto type = (scr_type)screenCombo->itemData(i).toInt();
        if (QStandardItem *item = model->item(i))
          item->setEnabled(!regularOnly || screen_has_regular_geometry_p(type));
      }
    }

    int val = (int)state.scrToImg.type;
    int idx = screenCombo->findData(val);
    if (idx != -1) {
      screenCombo->blockSignals(true);
      screenCombo->setCurrentIndex(idx);
      screenCombo->blockSignals(false);
    }
  });

  // Screen detection is a local operation. Cross-stage progress and next-step
  // guidance remain in the persistent Workflow summary above the tabs.
  QPushButton *detectButton = addButtonParameter(
      "", "Detect screen", [this]() { emit autodetectRequested(); },
      [this](const ParameterState &state) {
        const auto image = m_imageGetter();
        return screenDetectionAvailability(state, image.get()) ==
               ScreenDetectionAvailability::Ready;
      },
      "Detect or refine a regular screen lattice from the current capture. "
      "RGB captures can identify a regular screen automatically when Screen "
      "type is None; monochrome screened captures require the physical regular "
      "screen type to be selected first.");
  detectButton->setObjectName(QStringLiteral("ScreenDetectButton"));

  auto *detectionHint = new QLabel(this);
  detectionHint->setObjectName(QStringLiteral("ScreenDetectionHint"));
  detectionHint->setWordWrap(true);
  detectionHint->setEnabled(false);
  detectionHint->hide();
  if (m_currentGroupForm)
    m_currentGroupForm->addRow(detectionHint);
  else
    m_form->addRow(detectionHint);
  m_paramUpdaters.push_back([this, detectionHint](const ParameterState &state) {
    const auto image = m_imageGetter();
    const QString hint = screenDetectionUnavailableHint(
        screenDetectionAvailability(state, image.get()));
    detectionHint->setText(hint);
    detectionHint->setVisible(!hint.isEmpty());
  });

  QToolButton *screenPatternToggle = addSeparator("Screen pattern");
  screenPatternToggle->setObjectName(QStringLiteral("ScreenPatternToggle"));

  ScreenPreviewPanel *preview =
      new ScreenPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
  m_previewPanel = preview;
  preview->init("Screen Preview");

  connect(preview, &TilePreviewPanel::detachTilesRequested, this,
          &ScreenPanel::detachPreviewRequested);

  connect(preview, &TilePreviewPanel::progressStarted, this,
          &ScreenPanel::progressStarted);
  connect(preview, &TilePreviewPanel::progressFinished, this,
          &ScreenPanel::progressFinished);

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(preview);
  } else {
    m_form->addRow(preview);
  }
  setParameterApplicability(preview, [](const ParameterState &state) {
    return screen_has_regular_geometry_p(state.scrToImg.type);
  });

  // Strip-width parameters are meaningful only for processes whose line-screen
  // proportions are adjustable. Hide the complete rows otherwise; merely
  // disabling them leaves unexplained strip terminology on unrelated screens.
  QWidget *redStripWidth = addSliderParameter(
      "Red Strip Width", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.red_strip_width; },
      [](ParameterState &s, double v) { s.rparams.red_strip_width = v; }, 1.0,
      nullptr, false,
      "Relative width of the red filter strips for line-screen processes like "
      "Joly or Dufaycolor.");
  redStripWidth->setObjectName(QStringLiteral("ScreenRedStripWidth"));
  setParameterApplicability(redStripWidth, [](const ParameterState &state) {
    return screen_with_varying_strips_p(state.scrToImg.type);
  });

  QWidget *greenStripWidth = addSliderParameter(
      "Green Strip Width", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.green_strip_width; },
      [](ParameterState &s, double v) { s.rparams.green_strip_width = v; },
      1.0, nullptr, false,
      "Relative width of the green filter strips for line-screen processes "
      "like Joly or Dufaycolor.");
  greenStripWidth->setObjectName(QStringLiteral("ScreenGreenStripWidth"));
  setParameterApplicability(greenStripWidth, [](const ParameterState &state) {
    return screen_with_varying_strips_p(state.scrToImg.type);
  });

  m_widgetStateUpdaters.push_back([preview]() { preview->updateUI(); });

  addSeparator("Reconstruction");

  // Element density collection threshold
  addSliderParameter(
      "Collection threshold", 0.0, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.collection_threshold; },
      [](ParameterState &s, double v) { s.rparams.collection_threshold = v; },
      1.0, nullptr, false, "Threshold for identifying screen elements based on their color density. Smaller values require stronger color enhancement and may result in edge artefacts. Too large values may result in no data being collected at all.");

  // Collection Quality
  addEnumParameter("Collection quality", 
      render_parameters::collection_quality_names, 
      render_parameters::max_collection_quality,
      [](const ParameterState &s) { return (int)s.rparams.collection_quality; },
      [](ParameterState &s, int v) { s.rparams.collection_quality = (render_parameters::collection_quality_t)v; },
      nullptr, "Algorithmic precision for collecting and averaging color information from the screen elements."
  );

  // Screen Demosaic
  addEnumParameter("Screen demosaicing",
      render_parameters::screen_demosaic_names,
      render_parameters::max_screen_demosaic,
      [](const ParameterState &s) { return (int)s.rparams.screen_demosaic; },
      [](ParameterState &s, int v) { s.rparams.screen_demosaic = (render_parameters::screen_demosaic_t)v; },
      nullptr, "Interpolation algorithm used to fill in missing color information between the screen elements."
  );

  // Demosaiced Image Scaling Algorithm
  addEnumParameter("Demosaiced image scaling",
      render_parameters::demosaiced_scaling_names,
      render_parameters::max_demosaiced_scaling,
      [](const ParameterState &s) { return (int)s.rparams.demosaiced_scaling; },
      [](ParameterState &s, int v) { s.rparams.demosaiced_scaling = (render_parameters::demosaiced_scaling_t)v; },
      nullptr, "Method used to rescale the internally reconstructed image to the final output resolution."
  );


  addSeparator("Pre-demosaic denoising");

  // Screen Denoise Mode
  addEnumParameter("Denoise Mode",
      denoise_parameters::denoise_mode_names,
      (int)denoise_parameters::denoise_mode_max,
      [](const ParameterState &s) { return (int)s.rparams.screen_denoise.mode; },
      [](ParameterState &s, int v) { s.rparams.screen_denoise.mode = (denoise_parameters::denoise_mode)v; },
      nullptr, "Denoise collected screen-element colors before demosaicing. Collection support is used so weak/sub-pixel measurements have less authority."
  );

  // Strength (h)
  addSliderParameter(
      "Strength", 0.0, 1.0, 10000.0, 4, "", "",
      [](const ParameterState &s) { return s.rparams.screen_denoise.strength; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.strength = v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Patch-distance scale for Non-local means denoising. Larger values accept less-similar patches and therefore smooth more strongly.");

  // Patch Radius
  addSliderParameter(
      "Patch Radius", 1, 10, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.screen_denoise.patch_radius; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.patch_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Radius of the patch used for similarity comparison, measured in common physical screen coordinates before demosaicing.");

  // Search Radius
  addSliderParameter(
      "Search Radius", 1, 30, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.screen_denoise.search_radius; },
      [](ParameterState &s, double v) { s.rparams.screen_denoise.search_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return s.rparams.screen_denoise.mode == denoise_parameters::nl_means ||
               s.rparams.screen_denoise.mode == denoise_parameters::nl_fast;
      }, false, "Radius of the search window in common physical screen coordinates before demosaicing. Larger values are slower.");

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

  addSeparator("Post-demosaic denoising");

  addEnumParameter("Denoise Mode",
      denoise_parameters::denoise_mode_names,
      (int)denoise_parameters::denoise_mode_max,
      [](const ParameterState &s) { return (int)s.rparams.demosaiced_denoise.mode; },
      [](ParameterState &s, int v) { s.rparams.demosaiced_denoise.mode = (denoise_parameters::denoise_mode)v; },
      postDemosaicDenoiseAvailable, "Denoise the complete demosaiced color field before it is resampled and combined with the high-resolution B&W detail. RGB similarity weights are shared by all three channels. This stage currently requires the materialized advanced Paget/Dufay demosaicing path."
  );

  addSliderParameter(
      "Strength", 0.0, 1.0, 10000.0, 4, "", "",
      [](const ParameterState &s) { return s.rparams.demosaiced_denoise.strength; },
      [](ParameterState &s, double v) { s.rparams.demosaiced_denoise.strength = v; },
      1.0,
      [](const ParameterState &s) {
        return postDemosaicDenoiseAvailable (s)
               && (s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_means ||
                   s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_fast);
      }, false, "RMS RGB patch-distance scale for Non-local means. One similarity weight is applied to the whole RGB vector.");

  addSliderParameter(
      "Patch Radius", 1, 10, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.demosaiced_denoise.patch_radius; },
      [](ParameterState &s, double v) { s.rparams.demosaiced_denoise.patch_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return postDemosaicDenoiseAvailable (s)
               && (s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_means ||
                   s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_fast);
      }, false, "Radius of RGB patches used for post-demosaic similarity comparison.");

  addSliderParameter(
      "Search Radius", 1, 30, 1, 0, "", "",
      [](const ParameterState &s) { return (double)s.rparams.demosaiced_denoise.search_radius; },
      [](ParameterState &s, double v) { s.rparams.demosaiced_denoise.search_radius = (int)v; },
      1.0,
      [](const ParameterState &s) {
        return postDemosaicDenoiseAvailable (s)
               && (s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_means ||
                   s.rparams.demosaiced_denoise.mode == denoise_parameters::nl_fast);
      }, false, "Radius of the search window in the demosaiced color field.");

  addSliderParameter(
      "Bilateral Spatial Sigma", 0.1, 10.0, 10.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.demosaiced_denoise.bilateral_sigma_s; },
      [](ParameterState &s, double v) { s.rparams.demosaiced_denoise.bilateral_sigma_s = v; },
      1.0,
      [](const ParameterState &s) { return postDemosaicDenoiseAvailable (s)
                                      && s.rparams.demosaiced_denoise.mode == denoise_parameters::bilateral; },
      false, "Spatial standard deviation for post-demosaic vector bilateral filtering.");

  addSliderParameter(
      "Bilateral Range Sigma", 0.01, 1.0, 100.0, 2, "", "",
      [](const ParameterState &s) { return s.rparams.demosaiced_denoise.bilateral_sigma_r; },
      [](ParameterState &s, double v) { s.rparams.demosaiced_denoise.bilateral_sigma_r = v; },
      1.0,
      [](const ParameterState &s) { return postDemosaicDenoiseAvailable (s)
                                      && s.rparams.demosaiced_denoise.mode == denoise_parameters::bilateral; },
      false, "RMS RGB range standard deviation for post-demosaic vector bilateral filtering.");

  updateUI();
}
