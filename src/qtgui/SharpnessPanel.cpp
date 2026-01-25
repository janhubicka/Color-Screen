#include "SharpnessPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img.h"
#include "MTFChartWidget.h"
#include <QDebug>
#include <QComboBox>
#include <QString>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon> // Added
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout> // Added
#include <QtConcurrent>

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter,
                               ImageGetter imageGetter, QWidget *parent)
    : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent) {
  setDebounceInterval(5);
  setupUi();
}

SharpnessPanel::~SharpnessPanel() = default;
void SharpnessPanel::setupUi() {
  // Screen tile previews
  setupTiles("Sharpness Preview");

  // Sharpen mode dropdown
  addEnumParameter<sharpen_mode, sharpen_parameters::sharpen_mode_names,
                   (int)sharpen_mode::sharpen_mode_max>(
      "Sharpen",
      [](const ParameterState &s) { return (int)s.rparams.sharpen.mode; },
      [](ParameterState &s, int v) {
        s.rparams.sharpen.mode = (sharpen_mode)v;
      });

  QToolButton *separatorToggle = addSeparator("Scanner/Camera properties");

  // MTF Chart
  m_mtfChart = new MTFChartWidget();
  m_mtfChart->setMinimumHeight(250);

  // Create container for MTF
  QWidget *mtfWrapper = new QWidget();
  m_mtfContainer = new QVBoxLayout(mtfWrapper);
  m_mtfContainer->setContentsMargins(0, 0, 0, 0);

  QWidget *detachableMTF =
      createDetachableSection("MTF Chart", m_mtfChart, [this]() {
        emit detachMTFChartRequested(m_mtfChart);
      });
  m_mtfContainer->addWidget(detachableMTF);

  if (m_currentGroupForm)
    m_currentGroupForm->addRow(mtfWrapper);
  else
    m_form->addRow(mtfWrapper);
  updateMTFChart();

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
      }, 3);

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
      }, 3);

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

  // Add "Autodetect regular screen" button
  addButtonParameter("", "Autodetect regular screen", 
      [this]() { emit autodetectRequested(); },
      [this](const ParameterState &) {
          auto img = m_imageGetter();
          return img && img->has_rgb();
      });

  // Add "Match measured data" button (visible only if measured data exists)
  addButtonParameter("", "Match measured data", 
    [this]() {
      applyChange([](ParameterState &s) {
        const char *error = nullptr;
        s.rparams.sharpen.scanner_mtf.estimate_parameters(
            s.rparams.sharpen.scanner_mtf, nullptr, nullptr, &error, false);
      });
    },
    [this, separatorToggle](const ParameterState &s) {
       bool visible = s.rparams.sharpen.scanner_mtf.size() > 0;
       if (separatorToggle && !separatorToggle->isChecked())
         visible = false;
       return visible;
    });

  // MTF Scale
  // Range 0.0 - 2.0 (0.0 = no MTF)
  addSliderParameter(
      "MTF scale", 0.0, 2.0, 100.0, 2, "", "no MTF",
      [](const ParameterState &s) {
        return s.rparams.sharpen.scanner_mtf_scale;
      },
      [](ParameterState &s, double v) {
        s.rparams.sharpen.scanner_mtf_scale = v;
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
      }, 3);

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
    std::function<void(ParameterState &)> modifier, const QString &description) {
  ParameterPanel::applyChange(modifier, description);
  updateMTFChart();
  updateScreenTiles();
}

void SharpnessPanel::onParametersRefreshed(const ParameterState &state) {
  updateMTFChart();
  updateScreenTiles();
}

// Methods removed as they are now in TilePreviewPanel or handled by it
// scheduleTileUpdate, startNextRender, performTileRender, resizeEvent

std::vector<std::pair<render_screen_tile_type, QString>>
SharpnessPanel::getTileTypes() const {
  return {{original_screen, "Original"},
          {blured_screen, "Blured"},
          {sharpened_screen, "Sharpened"}};
}

bool SharpnessPanel::shouldUpdateTiles(const ParameterState &state) {
  if (m_lastTileSize == 0 || // First run
                             // tileSize check is done in base
      (int)state.scrToImg.type != m_lastScrType ||
      !state.rparams.sharpen.equal_p(m_lastSharpen) ||
      state.rparams.red_strip_width != m_lastRedStripWidth ||
      state.rparams.green_strip_width != m_lastGreenStripWidth) 
    return true;
  return false;
}

void SharpnessPanel::onTileUpdateScheduled() {
  ParameterState state = m_stateGetter();
  // Cache current parameters
  m_lastScrType = (int)state.scrToImg.type;
  m_lastSharpen = state.rparams.sharpen;
  m_lastRedStripWidth = state.rparams.red_strip_width;
  m_lastGreenStripWidth = state.rparams.green_strip_width;
}

QWidget *SharpnessPanel::getMTFChartWidget() const { return m_mtfChart; }

// getTilesWidget removed here (in base)

// createDetachableSection removed (moved to ParameterPanel)

void SharpnessPanel::reattachMTFChart(QWidget *widget) {
  if (widget != m_mtfChart)
    return;

  if (m_mtfContainer && m_mtfContainer->count() > 0) {
    QWidget *section = m_mtfContainer->itemAt(0)->widget();
    if (section && section->layout()) {
      // Remove placeholder (last item)
      QLayoutItem *item =
          section->layout()->takeAt(section->layout()->count() - 1);
      if (item) {
        if (item->widget())
          delete item->widget();
        delete item;
      }

      // Add widget back
      section->layout()->addWidget(widget);
      widget->show();

      // Show header again
      if (section->layout()->count() > 0) {
        QLayoutItem *headerItem = section->layout()->itemAt(0);
        if (headerItem && headerItem->widget()) {
          headerItem->widget()->show();
        }
      }
    }
  }
}

// reattachTiles removed (in base)
