/* Modulation transfer function parameters.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef MTF_PARAMETERS_H
#define MTF_PARAMETERS_H
#include <array>
#include <string>
#include <vector>
#include "base.h"
#include "color.h"
#include "progress-info.h"

namespace colorscreen
{

/* One measured spatial-frequency-response curve and its capture metadata.  */
struct mtf_measurement
{
  /* Construct an empty unlabelled measurement.  */
  mtf_measurement ()
      : channel (-1), wavelength (0), same_capture (false),
        name ("Measured MTF")
  {
  }
  /* Channel: -1 unknown, 0 red, 1 green, 2 blue, 3 IR  */
  int channel;
  /* Wavelength in nanometers.  A positive value is authoritative for this
     measurement irrespective of CHANNEL.  Zero means unknown and permits a
     channel or capture-level wavelength to be used as a fallback.  */
  double wavelength;
  /* True if this measurement comes from the same capture as the preceding
     measurement.  */
  bool same_capture;
  /* Name.  */
  std::string name;

  /* Append one sample at FREQ cycles per pixel with CONTRAST in percent.
     UNCERTAINTY is the estimated one-standard-deviation uncertainty of
     CONTRAST in percentage points.  Zero means that no uncertainty estimate
     is available and preserves the historical uniformly weighted fit.  */
  void
  add_value (double freq, double contrast, double uncertainty = 0)
  {
    m_data.push_back ({freq, contrast, uncertainty});
  }
  /* Return number of stored samples.  */
  size_t
  size () const
  {
    return m_data.size ();
  }
  /* Return frequency of sample I in cycles per pixel.  */
  double
  get_freq (int i) const
  {
    return m_data[i].freq;
  }
  /* Return contrast of sample I in percent.  */
  double
  get_contrast (int i) const
  {
    return m_data[i].contrast;
  }
  /* Return one-standard-deviation uncertainty of sample I in percentage
     points.  Zero means that no uncertainty estimate is available.  */
  double
  get_uncertainty (int i) const
  {
    return m_data[i].uncertainty;
  }
  /* Return true when this measurement equals O exactly.  */
  bool
  operator== (const mtf_measurement &o) const
  {
    return channel == o.channel && wavelength == o.wavelength
           && same_capture == o.same_capture && name == o.name
           && m_data == o.m_data;
  }
private:
  /* One measured frequency/contrast pair and its optional uncertainty.  */
  struct entry
  {
    double freq;
    double contrast;
    double uncertainty;

    /* Return true when this entry equals O exactly.  */
    bool operator== (const entry &o) const
    {
      return freq == o.freq && contrast == o.contrast
             && uncertainty == o.uncertainty;
    }
  };
  std::vector <entry> m_data;
};

/* MTF model selection.  AUTOMATIC_LEGACY preserves the historical behavior
   for old project files, PHYSICAL_DIFFRACTION prefers the metadata-driven
   optical model, and EMPIRICAL_FALLBACK explicitly selects the backup model.  */
enum class mtf_model
{
  /* Reproduce the historical choice: use the physical model when scan
     geometry is present and otherwise use the empirical fallback.  */
  automatic_legacy,
  /* Fit the diffraction-based physical model.  */
  physical_diffraction,
  /* Fit the Gaussian plus circular-blur empirical fallback.  */
  empirical_fallback
};

/* Explicit description of which MTF values are free during one fit.

   Numeric parameter values are always values or starting estimates.  They no
   longer have the second, ambiguous meaning that zero requests optimization.
   INCLUDE_MEASUREMENTS and OPTIMIZE_MEASUREMENT_WAVELENGTHS are indexed like
   MTF_PARAMETERS::MEASUREMENTS.  An empty INCLUDE_MEASUREMENTS vector includes
   every curve, while missing wavelength entries mean false.  */
struct mtf_estimation_options
{
  mtf_model model = mtf_model::automatic_legacy;
  bool optimize_sigma = false;
  bool optimize_defocus = false;
  bool optimize_blur_diameter = false;
  bool optimize_f_stop = false;
  bool optimize_sensor_fill_factor = false;
  bool optimize_halo_fraction = false;
  bool optimize_halo_sigma = false;
  std::vector<bool> include_measurements;
  std::vector<bool> optimize_measurement_wavelengths;

  /* Return true when measurement I participates in the fit.  */
  bool
  include_measurement_p (size_t i) const
  {
    return include_measurements.empty ()
           || (i < include_measurements.size () && include_measurements[i]);
  }

  /* Return true when wavelength of measurement I is a fit variable.  */
  bool
  optimize_measurement_wavelength_p (size_t i) const
  {
    return i < optimize_measurement_wavelengths.size ()
           && optimize_measurement_wavelengths[i];
  }
};

/* MTF can be either based on real measured data (then size() != 0)
   or computed by diffraction limit or just a blur disk simulation.
   In each case one can adjust sigma.  */
struct mtf_parameters
{
  /* Model used for evaluation and sharpening.  New GUI fits select a model
     explicitly; old projects retain AUTOMATIC_LEGACY.  */
  mtf_model model = mtf_model::automatic_legacy;

  /* Sigma (in pixels) used to estimate gaussian blur.  */
  double sigma = 0;

  /* Fraction of optical PSF energy assigned to a broad scattering halo.
     Zero disables the halo.  The remaining 1-HALO_FRACTION stays in the
     signed diffraction/defocus/compact-blur core.  */
  double halo_fraction = 0;

  /* Standard deviation of the broad Gaussian halo PSF component in output
     pixels.  The physical OTF is the energy-weighted sum of the signed core
     OTF and this positive halo OTF.  */
  double halo_sigma = 0;

  /* Size of blur diameter (in pixels) used to estimate defocus.
     This parameter is used only if pixel_pitch/f_stop/wavelength
     is not defined.  */
  double blur_diameter = 0;

  /* Defocus (in millimeters) */
  double defocus = 0;
  /* F-stop.  */
  double f_stop = 0;
  /* Wavelength of light in nm.  */
  double wavelength = 0;
  /* Per-channel Wavelength of light in nm.  */
  std::array<double, 4> wavelengths = {0, 0, 0, 0};
  /* Sensor pixel pitch (size of a pixel) in micrometers.  */
  double pixel_pitch = 0;
  /* The ratio of the active area to the total pixel area of the sensor.
     Usually in range 0..1 but for scanner and linear sensor it may be greater
     than that.  For Nikon Coolscan it seems to be 4.

     0 disables accounting sensor MTF.  */
  double sensor_fill_factor = 1;

  /* DPI of the scan; necessary to calculate magnification.  */
  double scan_dpi = 0;

  /* Measurement to use.  */
  int measured_mtf_idx = -1;

  std::vector <mtf_measurement> measurements;


  /* Return true when at least one non-empty measurement is available.  */
  bool
  has_measurements () const
  {
    return measurements.size () && measurements[0].size ();
  }
  /* Return true when MEASURED_MTF_IDX selects a usable measured curve.  */
  bool
  use_measured_mtf () const
  {
    return measured_mtf_idx >= 0 && measurements.size () > (size_t)measured_mtf_idx && measurements[measured_mtf_idx].size () > 2;
  }
  /* Return true when the evaluated capture transfer already contains sensor
     pixel-aperture attenuation.  A measured curve is obtained from sampled
     image data and therefore contains this response.  An analytical model
     contains it only when SENSOR_FILL_FACTOR enables SENSOR_MTF.  */
  pure_attr bool
  includes_sensor_aperture_p () const
  {
    return use_measured_mtf ()
           || (my_isfinite (sensor_fill_factor) && sensor_fill_factor > 0);
  }
  /* Return true when all metadata required by the physical model is valid.  */
  bool
  can_simulate_diffraction_p () const
  {
    return my_isfinite (pixel_pitch) && pixel_pitch > 0
           && my_isfinite (f_stop) && f_stop > 0
           && my_isfinite (scan_dpi) && scan_dpi > 0;
  }
  /* Return true when the physical model, rather than measured data, is used.  */
  bool
  simulate_diffraction_p () const
  {
    return !use_measured_mtf ()
           && model != mtf_model::empirical_fallback
           && can_simulate_diffraction_p ();
  }
  /* Return true when O produces the same cached transfer function.  */
  bool
  operator== (const mtf_parameters &o) const
  {
    if (use_measured_mtf ())
      {
	if (!o.use_measured_mtf ())
	  return false;
	return measurements[measured_mtf_idx]
               == o.measurements[o.measured_mtf_idx]
           && sigma == o.sigma
           && blur_diameter == o.blur_diameter;
      }
    else if (o.use_measured_mtf ())
      return false;
    if (sigma != o.sigma || sensor_fill_factor != o.sensor_fill_factor)
      return false;
    if (simulate_diffraction_p ())
      return o.simulate_diffraction_p ()
             && halo_fraction == o.halo_fraction
             && halo_sigma == o.halo_sigma
	     && defocus == o.defocus
	     && blur_diameter == o.blur_diameter
	     && f_stop == o.f_stop
	     && wavelength == o.wavelength
	     && pixel_pitch == o.pixel_pitch
	     && scan_dpi == o.scan_dpi;
    else if (o.simulate_diffraction_p ())
      return false;
    if (blur_diameter != o.blur_diameter)
      return false;
    return true;
  }
  /* Return true when every stored parameter equals the corresponding one in
     O.  Unlike OPERATOR== this also compares inactive and diagnostic data.  */
  bool
  equal_p (const mtf_parameters &o) const
  {
    return sigma == o.sigma
	   && model == o.model
	   && halo_fraction == o.halo_fraction
	   && halo_sigma == o.halo_sigma
	   && blur_diameter == o.blur_diameter
	   && defocus == o.defocus
	   && f_stop == o.f_stop
	   && wavelength == o.wavelength
	   && wavelengths == o.wavelengths
	   && pixel_pitch == o.pixel_pitch
	   && scan_dpi == o.scan_dpi
	   && measured_mtf_idx == o.measured_mtf_idx
	   && measurements == o.measurements
	   && sensor_fill_factor == o.sensor_fill_factor;
  }
  /* Release all measured curves.  */
  void
  clear_data ()
  {
    std::vector <mtf_measurement>().swap (measurements);
  }
  /* Return capture magnification inferred from PIXEL_PITCH and SCAN_DPI.  */
  pure_attr double magnification () const;
  /* Return image-side working f-number.  */
  pure_attr double effective_f_stop () const;
  /* Return PIXEL_FREQ normalized by the incoherent diffraction cutoff.  */
  pure_attr double nu (double pixel_freq) const;
  /* Return diffraction-limited circular-pupil OTF at PIXEL_FREQ.  For an
     unaberrated circular pupil this is nonnegative, but the OTF naming keeps
     the distinction from magnitude-only measured data explicit.  */
  pure_attr double lens_diffraction_otf (double pixel_freq) const;
  /* Return diffraction-limited circular-pupil MTF magnitude at PIXEL_FREQ.  */
  pure_attr double lens_diffraction_mtf (double pixel_freq) const;
  /* Return the legacy approximate defocus factor at PIXEL_FREQ.  */
  pure_attr double hopkins_defocus_mtf (double pixel_freq) const;
  /* Return exact signed circular-pupil defocus OTF factor at PIXEL_FREQ.  */
  pure_attr double lens_defocus_otf (double pixel_freq) const;
  /* Return magnitude of the exact circular-pupil defocus factor at
     PIXEL_FREQ.  */
  pure_attr double lens_defocus_mtf (double pixel_freq) const;
  /* Return historical Stokseth/Bessel factor at PIXEL_FREQ.  */
  pure_attr double stokseth_defocus_mtf (double pixel_freq) const;
  /* Return the normalized broad-scatter halo MTF at PIXEL_FREQ.  This is the
     transfer of the broad Gaussian halo component itself, before it is mixed
     with the signed compact optical core.  */
  pure_attr double halo_mtf (double pixel_freq) const;
  /* Return signed complete lens OTF at PIXEL_FREQ.  This is meaningful only
     for the known analytical physical model; measured curves do not carry
     phase/sign information.  */
  pure_attr double lens_otf (double pixel_freq) const;
  /* Return complete lens MTF magnitude at PIXEL_FREQ.  */
  pure_attr double lens_mtf (double pixel_freq) const;
  /* Return signed lens times sensor OTF at PIXEL_FREQ.  */
  pure_attr double system_otf (double pixel_freq) const;
  /* Return lens times sensor MTF magnitude at PIXEL_FREQ.  */
  pure_attr double system_mtf (double pixel_freq) const;
  /* Return signed radial first-cut sensor-aperture OTF at PIXEL_FREQ.  */
  pure_attr double sensor_otf (double pixel_freq) const;
  /* Return radial first-cut sensor-aperture MTF magnitude at PIXEL_FREQ.  */
  pure_attr double sensor_mtf (double pixel_freq) const;
  /* Return optional residual correction at PIXEL_FREQ for measured data.  */
  pure_attr double measured_mtf_correction (double pixel_freq) const;
 
  /* Component curves used by the chart and diagnostic CSV output.  */
  struct computed_mtf
  {
    /* Signed analytical system OTF used by forward blur and deconvolution.
       Measured slanted-edge data is never inserted here because it carries
       magnitude only.  */
    std::vector<double> system_otf;
    std::vector<double> system_mtf;
    std::vector<double> sensor_mtf;
    std::vector<double> gaussian_blur_mtf;
    std::vector<double> halo_mtf;
    std::vector<double> lens_defocus_mtf;
    /* Legacy approximation retained for model-validation comparisons.  */
    std::vector<double> stokseth_defocus_mtf;
    std::vector<double> lens_diffraction_mtf;
    std::vector<double> lens_mtf;
    /* Historical field name; values are the fallback circular-blur MTF.  */
    std::vector<double> hopkins_blur_mtf;
  };

  /* Flags controlling physical-model parameter estimation.  */
  enum estimation_flags
  {
    estimate_verbose = 1,
    estimate_use_nmsimplex = 2,
    estimate_use_multifit = 4,
    estimate_verbose_solving = 8,
    /* Estimate HALO_FRACTION and HALO_SIGMA when they are not supplied.
       This is opt-in because a broad halo can also describe target-edge
       structure or illumination flare rather than the capture optics.  */
    estimate_halo = 16
  };
  
  /* Fit this object to measurements in PAR.  WRITE_TABLE optionally receives
     component curves, PROGRESS reports work, ERROR receives a diagnostic, and
     FLAGS selects optimizers and optional parameters.  Return squared error.  */
  DLL_PUBLIC double estimate_parameters (
      mtf_parameters &par, const char *write_table = nullptr,
      progress_info *progress = nullptr, const char **error = nullptr,
      int flags = estimate_use_nmsimplex | estimate_use_multifit);

  /* Validate an explicit fitting request OPTIONS for parameter values PAR.
     ERROR receives a static diagnostic on failure.  */
  DLL_PUBLIC static bool validate_estimation_options (
      const mtf_parameters &par, const mtf_estimation_options &options,
      const char **error = nullptr);

  /* Fit this object to measurements in PAR using explicit free-variable
     OPTIONS.  WRITE_TABLE optionally receives component curves, PROGRESS
     reports work, ERROR receives a diagnostic, and FLAGS selects numerical
     solvers.  Return squared percentage-point error or -1 on failure.  */
  DLL_PUBLIC double estimate_parameters (
      mtf_parameters &par, const mtf_estimation_options &options,
      const char *write_table = nullptr, progress_info *progress = nullptr,
      const char **error = nullptr,
      int flags = estimate_use_nmsimplex | estimate_use_multifit);

  /* Construct default parameters.  */
  mtf_parameters () = default;

  /* Save the model PSF to WRITE_TABLE.  PROGRESS reports work and ERROR
     receives a diagnostic.  */
  DLL_PUBLIC bool save_psf (progress_info *progress, const char *write_table,
                            const char **error) const;

  /* Write component curves to WRITE_TABLE; report failures through ERROR.  */
  DLL_PUBLIC bool write_table (const char *write_table,
                               const char **error) const;

  /* Evaluate all diagnostic component curves at STEPS equidistant samples.  */
  DLL_PUBLIC computed_mtf compute_curves (int steps) const;

  /* Load a QuickMTF-style curve from IN, label it NAME, and report failures
     through ERROR.  Return the newly appended measurement index or -1.  */
  DLL_PUBLIC int load_csv (FILE *in, std::string name, const char **error);

  /* Return configured wavelength for channel C, falling back to the global
     narrow-band wavelength and then to a generic capture default.  HAS_RGB
     distinguishes a fourth infrared channel from a standalone monochrome
     capture: the former defaults to near-IR while the latter defaults to
     visible green light.  Scanner/camera metadata and explicit parameters
     always override these defaults.  */
  double
  get_channel_wavelength (int c, bool has_rgb = true) const
  {
    static constexpr double default_wavelengths[] = {600, 530, 450, 750};
    if (c < 0 || c >= 4)
      return wavelength > 0 ? wavelength : 550;
    if (wavelengths[c] > 0)
      return wavelengths[c];
    if (wavelength > 0)
      return wavelength;
    if (c == 3 && !has_rgb)
      return 550;
    return default_wavelengths[c];
  }
private:
  /* Shared implementation of legacy and explicit fitting APIs.  OPTIONS is
     null only for the compatibility interface which retains zero sentinels.  */
  double estimate_parameters_internal (
      mtf_parameters &par, const mtf_estimation_options *options,
      const char *write_table, progress_info *progress, const char **error,
      int flags);
  /* Write diagnostic-table header to F and return true on success.  */
  bool print_csv_header (FILE *f) const;
};
}
#endif
