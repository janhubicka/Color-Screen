#ifndef MTF_PARAMETERS_H
#define MTF_PARAMETERS_H
#include <string>
#include <array>
#include "base.h"
#include "color.h"
#include "progress-info.h"

namespace colorscreen
{
struct mtf_measurement
{
  mtf_measurement ()
  : channel (-1), wavelength (0), same_capture (false), name("Measured MTF")
  {
  }
  /* Channel: -1 unknown, 0 red, 1 green, 2 blue, 3 IR  */
  int channel;
  /* Wavelength. Only used if channel is -1. 0=unknown  */
  double wavelength;
  /* True if measurement come from same capture as earlier one.  */
  bool same_capture;
  /* Name.  */
  std::string name;

  void
  add_value (double freq, luminosity_t contrast)
  {
    m_data.push_back ({freq, contrast});
  }
  size_t
  size () const
  {
    return m_data.size ();
  }
  double
  get_freq (int i) const
  {
    return m_data[i].freq;
  }
  luminosity_t
  get_contrast (int i) const
  {
    return m_data[i].contrast;
  }
  bool
  operator== (const mtf_measurement &o) const
  {
    return channel == o.channel && wavelength == o.wavelength && same_capture == o.same_capture && name == o.name && m_data == o.m_data;
  }
private:
  struct entry {
    double freq;
    luminosity_t contrast;
    bool operator== (const entry &o) const
    {
      return freq == o.freq && contrast == o.contrast;
    }
  };
  std::vector <entry> m_data;
};
/* MTF can be either based on real measured data (then size() != 0)
   or computed by difraction limit or just a blur disk simulation.
   In each case one can adjust sigma.  */
struct mtf_parameters
{
  /* Sigma (in pixels) used to estimate gaussian blur.  */
  double sigma;

  /* Size of blur diameter (in pixels) used to estimate defocus.
     This parameter is used only if pixel_pitch/f_num/wavelength_nm
     is not defined.  */
  double blur_diameter;

  /* Defocus (in milimeters) */
  double defocus;
  /* F-stop.  */
  double f_stop;
  /* Wavelength of light in nm.  */
  double wavelength;
  /* Per-channel Wavelength of light in nm.  */
  std::array<double, 4> wavelengths;
  /* Sensor pixel pitch (size of a pixel) in micrometers.  */
  double pixel_pitch;
  /* The ratio of the active area to the total pixel area of the sensor.
     Usually in range 0..1 but for scanner and linear sensor it may be grater
     than that.  For Nikon Coolscan it seems to be 4.

     0 disables accounting sensor MTF.  */
  double sensor_fill_factor;

  /* DPI of the scan; necessary to calculate magnification.  */
  double scan_dpi;

  /* Measurement to use.  */
  int measured_mtf_idx;

  std::vector <mtf_measurement> measurements;


  bool
  has_measurements () const
  {
    return measurements.size () && measurements[0].size ();
  }
  int
  use_measured_mtf () const
  {
    return measured_mtf_idx >= 0 && measurements.size () > measured_mtf_idx && measurements[measured_mtf_idx].size () >2;
  }
  bool
  can_simulate_difraction_p () const
  {
    return pixel_pitch && f_stop && wavelength && scan_dpi;
  }
  bool
  simulate_difraction_p () const
  {
    return !use_measured_mtf () && can_simulate_difraction_p ();
  }
  bool
  operator== (const mtf_parameters &o) const
  {
    if (use_measured_mtf ())
      {
	if (!o.use_measured_mtf ())
	  return false;
	return measurements[measured_mtf_idx] == o.measurements[o.measured_mtf_idx];
      }
    else if (o.use_measured_mtf ())
      return false;
    if (sigma != o.sigma || sensor_fill_factor != o.sensor_fill_factor)
      return false;
    if (simulate_difraction_p ())
      return o.simulate_difraction_p ()
	     && defocus == o.defocus
	     && blur_diameter == o.blur_diameter
	     && f_stop == o.f_stop
	     && wavelength == o.wavelength
	     && pixel_pitch == o.pixel_pitch
	     && scan_dpi == o.scan_dpi;
    else if (o.simulate_difraction_p ())
      return false;
    if (blur_diameter != o.blur_diameter)
      return false;
    return true;
  }
  bool equal_p (const mtf_parameters &o) const
  {
    return sigma == o.sigma
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
  void
  clear_data ()
  {
    std::vector <mtf_measurement>().swap (measurements);
  }
  pure_attr double effective_f_stop () const;
  pure_attr double nu (double pixel_freq) const;
  pure_attr luminosity_t lens_difraction_mtf (double pixe_freq) const;
  pure_attr luminosity_t hopkins_defocus_mtf (double pixe_freq) const;
  pure_attr luminosity_t stokseth_defocus_mtf (double pixe_freq) const;
  pure_attr luminosity_t lens_mtf (double pixel_freq) const;
  pure_attr luminosity_t system_mtf (double pixel_freq) const;
  pure_attr luminosity_t sensor_mtf (double pixel_freq) const;
  pure_attr luminosity_t measured_mtf_correction (double pixel_freq) const;
 
  struct computed_mtf {
      std::vector<double> system_mtf;
      std::vector<double> sensor_mtf;
      std::vector<double> gaussian_blur_mtf;
      std::vector<double> stokseth_defocus_mtf;
      std::vector<double> lens_difraction_mtf;
      std::vector<double> lens_mtf;
      std::vector<double> hopkins_blur_mtf;
  };

  enum estimation_flags
  {
    estimate_verbose = 1,
    estimate_use_nmsimplex = 2,
    estimate_use_multifit = 4,
    estimate_verbose_solving = 8
  };
  
  DLL_PUBLIC double estimate_parameters (mtf_parameters &par, const char *write_table = NULL, progress_info *progress = NULL, const char **error = NULL, int flags = estimate_use_nmsimplex | estimate_use_multifit);
  mtf_parameters ()
  : sigma (0), blur_diameter (0), defocus (0), f_stop (0), wavelength (0), wavelengths {0, 0, 0, 0}, pixel_pitch (0), sensor_fill_factor (1), scan_dpi (0), measured_mtf_idx (-1), measurements ()
  { }
  DLL_PUBLIC bool save_psf (progress_info *progress, const char *write_table, const char **error) const;
  DLL_PUBLIC bool write_table (const char *write_table, const char **error) const;
  DLL_PUBLIC computed_mtf compute_curves (int steps) const;
  DLL_PUBLIC int load_csv (FILE *in, std::string name, const char **error);
  luminosity_t get_channel_wavelength (int c) const
  {
    /* Approximate peaks of spectral sensitivity curves of Dikon D700.  */
    static const constexpr luminosity_t default_wavelengths[] = {600, 530, 450, 850};
    if (wavelengths[c] > 0)
      return wavelengths[c];
    else
      return default_wavelengths[c];
  }
private:
  bool print_csv_header (FILE *f) const;
};
}
#endif
