#include "SharpnessPanel.h"

SharpnessPanel::SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent)
{
    setupUi();
}

SharpnessPanel::~SharpnessPanel() = default;

void SharpnessPanel::setupUi()
{
    // Sharpen Mode
    std::map<int, QString> sharpenModes;
    for (int i = 0; i < (int)colorscreen::sharpen_parameters::sharpen_mode_max; ++i) {
        sharpenModes[i] = QString::fromUtf8(colorscreen::sharpen_parameters::sharpen_mode_names[i]);
    }
    
    addEnumParameter("Sharpen", sharpenModes,
        [](const ParameterState &s) { return (int)s.rparams.sharpen.mode; },
        [](ParameterState &s, int v) { s.rparams.sharpen.mode = (colorscreen::sharpen_parameters::sharpen_mode)v; }
    );
    
    addSeparator("Scanner/Camera properties");
    
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
    
    addSeparator("Unsharp mask");
}
