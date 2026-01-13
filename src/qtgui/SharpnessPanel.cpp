#include "SharpnessPanel.h"
#include "MTFChartWidget.h"
#include "../libcolorscreen/include/render-parameters.h"
#include <QFormLayout>

using namespace colorscreen;
using sharpen_mode = colorscreen::sharpen_parameters::sharpen_mode;

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent)
{
    setupUi();
}

SharpnessPanel::~SharpnessPanel() = default;

void SharpnessPanel::setupUi()
{
    // Sharpen mode dropdown
    std::map<int, QString> sharpenModes;
    for (int i = 0; i < (int)sharpen_mode::sharpen_mode_max; ++i) {
        sharpenModes[i] = QString::fromUtf8(sharpen_parameters::sharpen_mode_names[i]);
    }
    
    addEnumParameter(
        "Sharpen",
        sharpenModes,
        [](const ParameterState &s) { return (int)s.rparams.sharpen.mode; },
        [](ParameterState &s, int v) { s.rparams.sharpen.mode = (sharpen_mode)v; }
    );
    
    QToolButton *separatorToggle = addSeparator("Scanner/Camera properties");
    
    // Add "Use measured MTF" checkbox (visible only if measured data exists and separator is open)
    addCheckboxParameter("Use measured MTF",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.use_measured_mtf; },
        [](ParameterState &s, bool v) { s.rparams.sharpen.scanner_mtf.use_measured_mtf = v; },
        [](const ParameterState &s) { 
            return s.rparams.sharpen.scanner_mtf.size() > 2; 
        }
    );
    
    // MTF Chart
    m_mtfChart = new MTFChartWidget();
    m_mtfChart->setMinimumHeight(250);
    m_form->addRow(m_mtfChart);
    updateMTFChart();
    
    // Connect separator toggle to chart visibility
    if (separatorToggle) {
        connect(separatorToggle, &QToolButton::toggled, m_mtfChart, &QWidget::setVisible);
    }
    
    // Gaussian blur (Sigma)
    // Range 0.0 - 20.0, Pixels.
    // Slider step? 3 decimal precision for entry, but slider might be coarser or same?
    // If we use scale 1000, slider has 20000 steps. That works efficiently on modern computers.
    
    addSliderParameter("Gaussian blur sigma", 0.0, 20.0, 1000.0, 3, "pixels", "",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.sigma; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.sigma = v; }
    );
    
    // Nominal f-stop
    // Range 0.0 - 20.0 (0.0 = unknown)
    addSliderParameter("nominal f-stop", 0.0, 20.0, 1000.0, 3, "", "unknown",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.f_stop; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.f_stop = v; }
    );
    
    // Sensor pixel pitch
    // Range 0.0 - 20.0 (0.0 = unknown)
    addSliderParameter("Sensor pixel pitch", 0.0, 20.0, 1000.0, 3, "μm", "unknown",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.pixel_pitch; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.pixel_pitch = v; }
    );
    
    // Wavelength
    // Range 0.0 - 1200.0 (0.0 = unknown)
    addSliderParameter("Wavelength", 0.0, 1200.0, 10.0, 1, "nm", "unknown",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.wavelength; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.wavelength = v; }
    );
    
    // Sensor fill factor
    // Range 0.0 - 8.0 (0.0 = unknown)
    addSliderParameter("Sensor fill factor", 0.0, 8.0, 1000.0, 3, "", "unknown",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.sensor_fill_factor; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.sensor_fill_factor = v; }
    );
    
    // Defocus
    // Range 0.0 - 10.0 mm
    // Non-linear Gamma 2.0 (slow start)
    // Enabled only if simulate_difraction_p()
    addSliderParameter("Defocus", 0.0, 10.0, 1000.0, 3, "mm", "",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.defocus; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.defocus = v; },
        2.0, // Gamma
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.simulate_difraction_p(); }
    );
    
    // Blur diameter
    // Range 0.0 - 20.0 pixels
    // Non-linear Gamma 2.0 (slow start)
    // Enabled only if !simulate_difraction_p()
    addSliderParameter("Blur diameter", 0.0, 20.0, 1000.0, 2, "pixels", "",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_mtf.blur_diameter; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_mtf.blur_diameter = v; },
        2.0, // Gamma
        [](const ParameterState &s) { return !s.rparams.sharpen.scanner_mtf.simulate_difraction_p(); }
    );
    
    addSeparator("Deconvolution");
    
    // Supersample
    // Range 1 - 16, integer
    addSliderParameter("Supersample", 1.0, 16.0, 1.0, 0, "", "",
        [](const ParameterState &s) { return (double)s.rparams.sharpen.supersample; },
        [](ParameterState &s, double v) { s.rparams.sharpen.supersample = (int)v; }
    );
    
    addSeparator("Wiener filter");
    
    // Signal to noise ratio
    // Range 0 - 65535, slow at start (gamma 2.0)
    addSliderParameter("Signal to noise ratio", 0.0, 65535.0, 1.0, 0, "", "",
        [](const ParameterState &s) { return s.rparams.sharpen.scanner_snr; },
        [](ParameterState &s, double v) { s.rparams.sharpen.scanner_snr = v; },
        2.0 // Gamma (slow start)
    );
    
    addSeparator("Richardson–Lucy deconvolution");
    
    // Richardson-Lucy iterations
    // Range 0 - 50000, integer, slow at beginning (gamma 2.0)
    addSliderParameter("Iterations", 0.0, 50000.0, 1.0, 0, "", "",
        [](const ParameterState &s) { return (double)s.rparams.sharpen.richardson_lucy_iterations; },
        [](ParameterState &s, double v) { s.rparams.sharpen.richardson_lucy_iterations = (int)v; },
        2.0 // Gamma (slow start)
    );
    
    // Richardson-Lucy sigma
    // Range 0.0 - 2.0, floating point
    addSliderParameter("Sigma", 0.0, 2.0, 1000.0, 3, "", "",
        [](const ParameterState &s) { return s.rparams.sharpen.richardson_lucy_sigma; },
        [](ParameterState &s, double v) { s.rparams.sharpen.richardson_lucy_sigma = v; }
    );
    
    addSeparator("Unsharp mask");
    
    // Unsharp mask radius
    // Range 0.0 - 20.0, Pixels
    // Same properties as Gaussian blur sigma
    // Enabled only when mode is unsharp_mask
    addSliderParameter("Radius", 0.0, 20.0, 1000.0, 3, "pixels", "",
        [](const ParameterState &s) { return s.rparams.sharpen.usm_radius; },
        [](ParameterState &s, double v) { s.rparams.sharpen.usm_radius = v; },
        1.0, // No gamma
        [](const ParameterState &s) { return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask; }
    );
    
    // Unsharp mask amount
    // Range 0.0 - 100.0
    // Gamma 2.0 for slow start
    // Enabled only when mode is unsharp_mask
    addSliderParameter("Amount", 0.0, 100.0, 100.0, 1, "", "",
        [](const ParameterState &s) { return s.rparams.sharpen.usm_amount; },
        [](ParameterState &s, double v) { s.rparams.sharpen.usm_amount = v; },
        2.0, // Gamma (slow start)
        [](const ParameterState &s) { return s.rparams.sharpen.mode == sharpen_mode::unsharp_mask; }
    );
}

void SharpnessPanel::updateMTFChart()
{
    if (!m_mtfChart)
        return;
    
    ParameterState state = m_stateGetter();
    
    // Compute MTF curves with 100 steps
    mtf_parameters::computed_mtf curves = state.rparams.sharpen.scanner_mtf.compute_curves(100);
    
    // Pass simulation flag to chart
    bool canSimulateDifraction = state.rparams.sharpen.scanner_mtf.simulate_difraction_p();
    m_mtfChart->setMTFData(curves, canSimulateDifraction);
    
    // Extract measured MTF data if available
    const auto &scanner_mtf = state.rparams.sharpen.scanner_mtf;
    if (scanner_mtf.size() > 0)
    {
        std::vector<double> freq, contrast;
        freq.reserve(scanner_mtf.size());
        contrast.reserve(scanner_mtf.size());
        
        for (size_t i = 0; i < scanner_mtf.size(); ++i)
        {
            freq.push_back(scanner_mtf.get_freq(i));
            contrast.push_back(scanner_mtf.get_contrast(i));
        }
        
        m_mtfChart->setMeasuredMTF(freq, contrast);
    }
    else
    {
        // No measured data, clear it
        m_mtfChart->setMeasuredMTF({}, {});
    }
}

void SharpnessPanel::applyChange(std::function<void(ParameterState&)> modifier)
{
    ParameterPanel::applyChange(modifier);
    updateMTFChart();
}

void SharpnessPanel::onParametersRefreshed(const ParameterState &state)
{
    updateMTFChart();
}
