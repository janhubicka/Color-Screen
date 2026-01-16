#include "SharpnessPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "MTFChartWidget.h"
#include <QDebug>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QtConcurrent>

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;

// Result of rendering all 3 tiles
struct TileRenderResult {
  QImage originalTile;
  QImage bluredTile;
  QImage sharpenedTile;
  int generation = 0;
  bool success = false;
};

Q_DECLARE_METATYPE(TileRenderResult)

namespace {
// Render all 3 tiles in background thread
TileRenderResult
renderTiles(ParameterState state, int scanWidth, int scanHeight, int generation,
            int tileSize,
            std::shared_ptr<colorscreen::progress_info> progress) {
  TileRenderResult result;
  result.generation = generation;
  result.success = false;

  // Create tile parameters
  tile_parameters tile;
  tile.width = tileSize;
  tile.height = tileSize;
  tile.pixelbytes = 3;
  tile.rowstride = tileSize * 3; // 3 bytes per pixel (RGB)

  // Compute pixel size
  scr_to_img scrToImgObj;
  scrToImgObj.set_parameters(state.scrToImg, scanWidth, scanHeight);
  coord_t pixel_size = scrToImgObj.pixel_size(scanWidth, scanHeight);

  // Render original
  std::vector<uint8_t> originalPixels(tileSize * tileSize * 3);
  tile.pixels = originalPixels.data();
  if (render_screen_tile(tile, state.scrToImg.type, state.rparams, pixel_size,
                         original_screen, progress.get())) {
    result.originalTile = QImage(originalPixels.data(), tileSize, tileSize,
                                 tile.rowstride, QImage::Format_RGB888)
                              .copy();
  }

  // Render blurred
  std::vector<uint8_t> bluredPixels(tileSize * tileSize * 3);
  tile.pixels = bluredPixels.data();
  if (render_screen_tile(tile, state.scrToImg.type, state.rparams, pixel_size,
                         blured_screen, progress.get())) {
    result.bluredTile = QImage(bluredPixels.data(), tileSize, tileSize,
                               tile.rowstride, QImage::Format_RGB888)
                            .copy();
  }

  // Render sharpened
  std::vector<uint8_t> sharpenedPixels(tileSize * tileSize * 3);
  tile.pixels = sharpenedPixels.data();
  if (render_screen_tile(tile, state.scrToImg.type, state.rparams, pixel_size,
                         sharpened_screen, progress.get())) {
    result.sharpenedTile = QImage(sharpenedPixels.data(), tileSize, tileSize,
                                  tile.rowstride, QImage::Format_RGB888)
                               .copy();
  }

  result.success = !result.originalTile.isNull() &&
                   !result.bluredTile.isNull() &&
                   !result.sharpenedTile.isNull();
  return result;
}
} // namespace

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                               ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  // Initialize debounce timer for tile updates
  m_updateTimer = new QTimer(this);
  m_updateTimer->setSingleShot(true);
  m_updateTimer->setInterval(30); // 30ms debounce
  connect(m_updateTimer, &QTimer::timeout, this,
          &SharpnessPanel::performTileRender);

  // Initialize tile watcher for async rendering
  m_tileWatcher = new QFutureWatcher<TileRenderResult>(this);
  connect(
      m_tileWatcher, &QFutureWatcher<TileRenderResult>::finished, this,
      [this]() {
        TileRenderResult result = m_tileWatcher->result();

        // Only update if this is still the current generation
        if (result.generation == m_tileGenerationCounter && result.success) {
          m_originalTileLabel->setPixmap(
              QPixmap::fromImage(result.originalTile));
          m_bluredTileLabel->setPixmap(QPixmap::fromImage(result.bluredTile));
          m_sharpenedTileLabel->setPixmap(
              QPixmap::fromImage(result.sharpenedTile));
        } else {
        }

        // Clear progress when done
        if (m_tileProgress) {
          // Progress completed;
          m_tileProgress.reset();
        }
      });

  setupUi();
}

SharpnessPanel::~SharpnessPanel() = default;
void SharpnessPanel::setupUi() {
  // Screen tile previews (Original, Blurred, Sharpened)
  m_tilesContainer = new QWidget();
  QHBoxLayout *tilesLayout = new QHBoxLayout(m_tilesContainer);
  tilesLayout->setContentsMargins(0, 0, 0, 0);
  tilesLayout->setSpacing(5);

  m_originalTileLabel = new QLabel();
  m_originalTileLabel->setScaledContents(false);
  m_originalTileLabel->setAlignment(Qt::AlignCenter);
  m_originalTileLabel->setMinimumSize(100, 100);
  m_originalTileLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  m_bluredTileLabel = new QLabel();
  m_bluredTileLabel->setScaledContents(false);
  m_bluredTileLabel->setAlignment(Qt::AlignCenter);
  m_bluredTileLabel->setMinimumSize(100, 100);
  m_bluredTileLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  m_sharpenedTileLabel = new QLabel();
  m_sharpenedTileLabel->setScaledContents(false);
  m_sharpenedTileLabel->setAlignment(Qt::AlignCenter);
  m_sharpenedTileLabel->setMinimumSize(100, 100);
  m_sharpenedTileLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  tilesLayout->addWidget(m_originalTileLabel, 1);
  tilesLayout->addWidget(m_bluredTileLabel, 1);
  tilesLayout->addWidget(m_sharpenedTileLabel, 1);

  m_form->addRow(m_tilesContainer);

  // Update tile visibility (only visible if scrToImg != Random)
  m_widgetStateUpdaters.push_back([this]() {
    ParameterState s = m_stateGetter();
    bool visible = s.scrToImg.type != scr_type::Random;
    m_tilesContainer->setVisible(visible);
  });

  // Sharpen mode dropdown
  std::map<int, QString> sharpenModes;
  for (int i = 0; i < (int)sharpen_mode::sharpen_mode_max; ++i) {
    sharpenModes[i] =
        QString::fromUtf8(sharpen_parameters::sharpen_mode_names[i]);
  }

  addEnumParameter(
      "Sharpen", sharpenModes,
      [](const ParameterState &s) { return (int)s.rparams.sharpen.mode; },
      [](ParameterState &s, int v) {
        s.rparams.sharpen.mode = (sharpen_mode)v;
      });

  QToolButton *separatorToggle = addSeparator("Scanner/Camera properties");

  // Add "Use measured MTF" checkbox (visible only if measured data exists and
  // separator is open)
  addCheckboxParameter(
      "Use measured MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.use_measured_mtf;
      },
      [](ParameterState &s, bool v) {
        s.rparams.sharpen.scanner_mtf.use_measured_mtf = v;
      },
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.size() > 2;
      });

  // Add "Match measured data" button (visible only if measured data exists)
  QPushButton *matchButton = new QPushButton("Match measured data");
  m_form->addRow(matchButton);

  // Wire up the button
  connect(matchButton, &QPushButton::clicked, this, [this]() {
    applyChange([](ParameterState &s) {
      const char *error = nullptr;
      double result = s.rparams.sharpen.scanner_mtf.estimate_parameters(
          s.rparams.sharpen.scanner_mtf, nullptr, nullptr, &error, false);
      if (error) {
        // Handle error if needed
      }
    });
  });

  // Update button visibility (only visible if measured data exists)
  m_widgetStateUpdaters.push_back([matchButton, this]() {
    ParameterState s = m_stateGetter();
    bool visible = s.rparams.sharpen.scanner_mtf.size() > 0;
    matchButton->setVisible(visible);
  });

  // MTF Chart
  m_mtfChart = new MTFChartWidget();
  m_mtfChart->setMinimumHeight(250);
  m_form->addRow(m_mtfChart);
  updateMTFChart();

  // Connect separator toggle to chart visibility
  if (separatorToggle) {
    connect(separatorToggle, &QToolButton::toggled, m_mtfChart,
            &QWidget::setVisible);
  }

  // Gaussian blur (Sigma)
  // Range 0.0 - 20.0, Pixels.
  // Slider step? 3 decimal precision for entry, but slider might be coarser or
  // same? If we use scale 1000, slider has 20000 steps. That works efficiently
  // on modern computers.

  addSliderParameter(
      "Gaussian blur sigma", 0.0, 20.0, 1000.0, 3, "pixels", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.sigma = v;
      });

  // Nominal f-stop
  // Range 0.0 - 20.0 (0.0 = unknown)
  addSliderParameter(
      "nominal f-stop", 0.0, 20.0, 1000.0, 3, "", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.f_stop;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.f_stop = v;
      });

  // Sensor pixel pitch
  // Range 0.0 - 20.0 (0.0 = unknown)
  addSliderParameter(
      "Sensor pixel pitch", 0.0, 20.0, 1000.0, 3, "μm", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.pixel_pitch;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.pixel_pitch = v;
      });

  // Wavelength
  // Range 0.0 - 1200.0 (0.0 = unknown)
  addSliderParameter(
      "Wavelength", 0.0, 1200.0, 10.0, 1, "nm", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.wavelength;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.wavelength = v;
      });

  // Sensor fill factor
  // Range 0.0 - 8.0 (0.0 = unknown)
  addSliderParameter(
      "Sensor fill factor", 0.0, 8.0, 1000.0, 3, "", "unknown",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.sensor_fill_factor;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.sensor_fill_factor = v;
      });

  // Defocus
  // Range 0.0 - 10.0 mm
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if simulate_difraction_p()
  addSliderParameter(
      "Defocus", 0.0, 10.0, 1000.0, 3, "mm", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.defocus;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.defocus = v;
      },
      2.0, // Gamma
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.simulate_difraction_p();
      });

  // Blur diameter
  // Range 0.0 - 20.0 pixels
  // Non-linear Gamma 2.0 (slow start)
  // Enabled only if !simulate_difraction_p()
  addSliderParameter(
      "Blur diameter", 0.0, 20.0, 1000.0, 2, "pixels", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf.blur_diameter;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf.blur_diameter = v;
      },
      2.0, // Gamma
      [](const ParameterState &s) {
        return !s.rparams.sharpen.scanner_mtf.simulate_difraction_p();
      });

  addSeparator("Deconvolution");

  // Supersample
  // Range 1 - 16, integer
  addSliderParameter(
      "Supersample", 1.0, 16.0, 1.0, 0, "", "",
      [](const ParameterState &s) {
        return (double)s.rparams.sharpen.supersample;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.supersample = (int)v;
      });

  addSeparator("Wiener filter");

  // Signal to noise ratio
  // Range 0 - 65535, slow at start (gamma 2.0)
  addSliderParameter(
      "Signal to noise ratio", 0.0, 65535.0, 1.0, 0, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.scanner_snr; },
      [](ParameterState &s, double v) { s.rparams.sharpen.scanner_snr = v; },
      2.0 // Gamma (slow start)
  );

  addSeparator("Richardson–Lucy deconvolution");

  // Richardson-Lucy iterations
  // Range 0 - 50000, integer, slow at beginning (gamma 2.0)
  addSliderParameter(
      "Iterations", 0.0, 50000.0, 1.0, 0, "", "",
      [](const ParameterState &s) {
        return (double)s.rparams.sharpen.richardson_lucy_iterations;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_iterations = (int)v;
      },
      2.0 // Gamma (slow start)
  );

  // Richardson-Lucy sigma
  // Range 0.0 - 2.0, floating point
  addSliderParameter(
      "Sigma", 0.0, 2.0, 1000.0, 3, "", "",
      [](const ParameterState &s) {
        return s.rparams.sharpen.richardson_lucy_sigma;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.richardson_lucy_sigma = v;
      });

  addSeparator("Unsharp mask");

  // Unsharp mask radius
  // Range 0.0 - 20.0, Pixels
  // Same properties as Gaussian blur sigma
  // Enabled only when mode is unsharp_mask
  addSliderParameter(
      "Radius", 0.0, 20.0, 1000.0, 3, "pixels", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_radius; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_radius = v; },
      1.0, // No gamma
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      });

  // Unsharp mask amount
  // Range 0.0 - 100.0
  // Gamma 2.0 for slow start
  // Enabled only when mode is unsharp_mask
  addSliderParameter(
      "Amount", 0.0, 100.0, 100.0, 1, "", "",
      [](const ParameterState &s) { return s.rparams.sharpen.usm_amount; },
      [](ParameterState &s, double v) { s.rparams.sharpen.usm_amount = v; },
      2.0, // Gamma (slow start)
      [](const ParameterState &s) {
        return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask;
      });
}

void SharpnessPanel::updateMTFChart() {
  if (!m_mtfChart)
    return;

  ParameterState state = m_stateGetter();

  // Compute MTF curves with 100 steps
  mtf_parameters::computed_mtf curves =
      state.rparams.sharpen.scanner_mtf.compute_curves(100);

  // Pass simulation flag to chart
  bool canSimulateDifraction =
      state.rparams.sharpen.scanner_mtf.simulate_difraction_p();
  m_mtfChart->setMTFData(curves, canSimulateDifraction);

  // Extract measured MTF data if available
  const auto &scanner_mtf = state.rparams.sharpen.scanner_mtf;
  if (scanner_mtf.size() > 0) {
    std::vector<double> freq, contrast;
    freq.reserve(scanner_mtf.size());
    contrast.reserve(scanner_mtf.size());

    for (size_t i = 0; i < scanner_mtf.size(); ++i) {
      freq.push_back(scanner_mtf.get_freq(i));
      contrast.push_back(scanner_mtf.get_contrast(i));
    }

    m_mtfChart->setMeasuredMTF(freq, contrast);
  } else {
    // No measured data, clear it
    m_mtfChart->setMeasuredMTF({}, {});
  }
}

void SharpnessPanel::updateScreenTiles() {
  // Schedule debounced update
  scheduleTileUpdate();
}

void SharpnessPanel::applyChange(
    std::function<void(ParameterState &)> modifier) {
  ParameterPanel::applyChange(modifier);
  updateMTFChart();
  updateScreenTiles();
}

void SharpnessPanel::onParametersRefreshed(const ParameterState &state) {
  updateMTFChart();
  updateScreenTiles();
}

void SharpnessPanel::scheduleTileUpdate() {
  // Restart debounce timer
  m_updateTimer->start();
}

void SharpnessPanel::performTileRender() {
  if (!m_originalTileLabel || !m_bluredTileLabel || !m_sharpenedTileLabel)
    return;

  ParameterState state = m_stateGetter();
  std::shared_ptr<colorscreen::image_data> scan = m_imageGetter();

  // Only update if we have an image and scrToImg is not Random
  if (!scan || state.scrToImg.type == scr_type::Random) {
    m_originalTileLabel->clear();
    m_bluredTileLabel->clear();
    m_sharpenedTileLabel->clear();
    return;
  }

  // Calculate dynamic tile size (1/3 of available width)
  int availableWidth = width();
  QScrollArea *sa = findChild<QScrollArea *>();
  if (sa && sa->viewport()) {
    availableWidth = sa->viewport()->width();
  }

  // Account for margins and spacing
  int margins = 0;
  if (m_form) {
    int l, t, r, b;
    m_form->getContentsMargins(&l, &t, &r, &b);
    margins = l + r;
  }
  int spacing = 10; // 2 * 5px spacing between 3 items

  int tileSize = qMax(64, (availableWidth - margins - spacing) / 3);

  // Set fixed square size for all labels to maintain aspect ratio
  m_originalTileLabel->setFixedSize(tileSize, tileSize);
  m_bluredTileLabel->setFixedSize(tileSize, tileSize);
  m_sharpenedTileLabel->setFixedSize(tileSize, tileSize);

  // Compute pixel size
  scr_to_img scrToImgObj;
  scrToImgObj.set_parameters(state.scrToImg, scan->width, scan->height);
  coord_t pixel_size = scrToImgObj.pixel_size(scan->width, scan->height);

  // Check if any relevant parameters changed
  bool needsUpdate = false;
  if (m_lastTileSize == 0 || // First run
                             // abs(tileSize - m_lastTileSize) > 10 ||  // Size
                             // changed significantly
      tileSize != m_lastTileSize ||
      fabs(1 - pixel_size / m_lastPixelSize) > 0.1 ||
      (int)state.scrToImg.type != m_lastScrType ||
      !state.rparams.sharpen.equal_p(m_lastSharpen) ||
      state.rparams.red_strip_width != m_lastRedStripWidth ||
      state.rparams.green_strip_width != m_lastGreenStripWidth) {
    needsUpdate = true;
  }

  if (!needsUpdate) {
    return;
  }

  // Cache current parameters
  m_lastTileSize = tileSize;
  m_lastPixelSize = pixel_size;
  m_lastScrType = (int)state.scrToImg.type;
  m_lastSharpen = state.rparams.sharpen;
  m_lastRedStripWidth = state.rparams.red_strip_width;
  m_lastGreenStripWidth = state.rparams.green_strip_width;

  // Create progress info
  m_tileProgress = std::make_shared<colorscreen::progress_info>();

  // Increment generation and launch async render
  m_tileGenerationCounter++;
  int currentGen = m_tileGenerationCounter;

  // Launch async tile rendering
  QFuture<TileRenderResult> future =
      QtConcurrent::run(renderTiles, state, scan->width, scan->height,
                        currentGen, tileSize, m_tileProgress);
  m_tileWatcher->setFuture(future);
}

void SharpnessPanel::resizeEvent(QResizeEvent *event) {
  ParameterPanel::resizeEvent(event);

  // Schedule tile update when panel is resized
  if (m_updateTimer && event->size() != event->oldSize()) {
    scheduleTileUpdate();
  }
}
