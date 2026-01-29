#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "mtf.h"
#include "fft.h"
#include "nmsimplex.h"
#include "gsl-solver.h"
#include <cmath>
#include <complex>
#include <memory>
namespace colorscreen
{

namespace
{

double
get_j1 (double x)
{
#if defined(__cpp_lib_math_special_functions)                                 \
    || defined(_GLIBCXX_USE_STD_SPEC_FUNCS)
  return std::cyl_bessel_j (1, x);
#elif defined(_WIN32) && !defined(__cpp_lib_math_special_functions)
  return _j1 (x);
#else
  return j1 (x); // Fallback for macOS/libc++
#endif
}

/* Return MTF of given blur circle diameter and given frequency.
   This is model for small defocus that does not seem to work that well.
   See Hopkins model below.  */
double
defocus_mtf (double freq, double blur_circle_diameter)
{
  /* If perfectly in focus or frequency is almost 0, return 1  */
  if (blur_circle_diameter < 1e-9 || freq < 1e-6)
    return 1;

  /* The transfer function of a circular blur is 2*J1(x)/x  */
  double arg = M_PI * freq * blur_circle_diameter;
  return std::abs (2.0 * get_j1 (arg) / arg);
}

/* Return MTF of gaussian blur with a given sigma.  */
double
gaussian_blur_mtf (double freq, double sigma)
{
  return std::exp (-2.0 * M_PI * M_PI * sigma * sigma * freq * freq);
}

/* Normalized sinc function: sin(pi * x) / (pi * x)
   Used to model the sensor's pixel aperture effect. */
double
sinc (double x)
{
  x = std::abs (x);
  if (x < 1e-6)
    return 1.0; /* Avoid division by zero (Taylor expansion limit)  */
  return std::sin (M_PI * x) / (M_PI * x);
}

#if 0
/**
     * Estimates MTF contribution of the Bayer sensor.
     * @param freq_lp_mm Spatial frequency.
     * @param channel "green" (high density) or "red_blue" (low density).
     * @param algorithm_softness 1.0 = perfect reconstruction, 1.5 = typical soft demosaic.
     */
    double calculate_mtf(double freq_lp_mm, std::string channel, double algorithm_softness = 1.2) {
        // 1. Pixel Aperture MTF (The "Box" filter of the physical pixel)
        // Most modern sensors have a high fill factor, so aperture width approx = pitch.
        double arg_aperture = std::numbers::pi * freq_lp_mm * pitch;
        double mtf_aperture = (arg_aperture < 1e-9) ? 1.0 : std::abs(std::sin(arg_aperture) / arg_aperture);

        // 2. Sampling & Demosaic MTF
        // The effective sampling pitch changes based on the Bayer pattern.
        double effective_sampling_pitch = pitch;
        if (channel == "green") {
            // Green pixels are in a quincunx grid; diagonal distance is pitch * sqrt(2)
            // but horizontal/vertical sampling is effectively 'pitch'.
            effective_sampling_pitch = pitch * algorithm_softness;
        } else {
            // Red and Blue are sampled at 2x the pitch distance.
            effective_sampling_pitch = pitch * 2.0 * algorithm_softness;
        }

        double arg_sampling = std::numbers::pi * freq_lp_mm * effective_sampling_pitch;
        double mtf_sampling = (arg_sampling < 1e-9) ? 1.0 : std::abs(std::sin(arg_sampling) / arg_sampling);

        return mtf_aperture * mtf_sampling;
    }
};
#endif

#if 0
double get_polychromatic_mtf(double freq, double defocus_center) {
    // Weights for human vision / Bayer sensitivity
    struct SpectralComponent { double lambda; double weight; double focal_shift; };
    std::vector<SpectralComponent> spectrum = {
        {0.000450, 0.25, 0.002}, // Blue (shifted slightly)
        {0.000550, 0.50, 0.000}, // Green (reference)
        {0.000650, 0.25, -0.002} // Red (shifted slightly)
    };

    double total_mtf = 0;
    for (auto& s : spectrum) {
        total_mtf += s.weight * get_system_mtf(freq, defocus_center + s.focal_shift, s.lambda);
    }
    return total_mtf;
}

/* Pixel pitch of PhaseOne 150MP camera in mm.  */
#define pixel_size (3.76 / 1000)

/* Calculates the MTF of a lens including defocus effects.
   FREQ Spatial frequency in lp/mm.
   F_NUM The f-number of the lens.
   WAVELENGTH_NM Wavelength in nanometers.
   DEFOCUS_MM Displacement from the focal plane in millimeters.
 */
double calculate_defocused_mtf(double freq, double f_num, double wavelength_nm, double defocus_mm) {
    /* Convert wavelength from nm to mm.  */
    double wavelength_mm = wavelength_nm / 1e6;
    /* Calculate the cut-off frequency (fc) */
    double cutoff_freq = 1.0 / (wavelength_mm * f_num);
    /* Calculate the cut-off frequency (fc) */
    double s = freq / cutoff_freq;

    if (s >= 1.0) return 0.0;
    if (s <= 0.0) return 1.0;

    // 1. Diffraction-limited component
    double mtf_diff = (2.0 / M_PI) * (std::acos(s) - s * sqrt(1.0 - s * s));

    // 2. Defocus component (Geometric Approximation)
    // Blur circle diameter D = defocus / f_number
    double blur_diameter = std::abs(defocus_mm) / f_num;
    
    // If perfectly in focus, return diffraction limit
    if (blur_diameter < 1e-9) return mtf_diff;

    // The transfer function of a circular blur is 2*J1(x)/x
    double arg = M_PI * freq * blur_diameter;
    double mtf_defocus = std::abs(2.0 * /*std::*/j1(arg) / arg);

    return mtf_diff * mtf_defocus;
}

/* Computes MTF at frequency f (cycles per unit distance)  */
double
system_mtf (double f, double lens_sigma)
{
  /* 1. Lens Component (Gaussian)
     MTF_lens = exp(-2 * pi^2 * sigma^2 * f^2)  */
  const double pi = 3.141592653589793;
#if 0
  double lens_mtf
      = std::exp (-2.0 * pi * pi * lens_sigma * lens_sigma * f * f);
#endif
  double lens_mtf = calculate_defocused_mtf (f/pixel_size, 8, 720, lens_sigma);
  double gaussian_mtf = std::exp (-2.0 * pi * pi * lens_sigma * lens_sigma * f * f);

  /* 2. Sensor Component (Sinc)
     The aperture width is the pixel pitch d.
     MTF_sensor = |sinc(d * f)|  */
  double sensor_mtf = std::abs (sinc (/*pixel_pitch **/ f, lens_sigma));

  // 3. System MTF is the product
  return lens_mtf * gaussian_mtf * sensor_mtf;
}
#endif

#if 0
/* Calculates the Gaussian LSF value at a given pixel offset x */
double
calculate_lsf (double x, double sigma)
{
  double coefficient = 1.0 / (sigma * std::sqrt (2.0 * M_PI));
  double exponent = -(x * x) / (2.0 * sigma * sigma);
  return coefficient * std::exp (exponent);
}
#endif

void
debug_data (double freq, double v1, double v2)
{
  int len = 60;
  int p1 = v1 * len / 100;
  int p2 = v2 * len / 100;
  printf ("freq %1.2f measured %2.2f obtained %2.2f ", freq, v1, v2);
  for (int j = 0; j < len + 1; j++)
    if (j == p1 && j == p2)
      printf ("E");
    else if (j == p1)
      printf ("m");
    else if (j == p2)
      printf ("e");
    else
      printf (" ");
  printf ("|\n");
}

class mtf_solver
{
  /* We can optimize
     - pixel sigma
     - wavelength
     - distance / blur radius  */
  static constexpr const int maxvals = 3;
  static constexpr const double nanometers_min = 380;
  static constexpr const double nanometers_max = 1000;

public:
  mtf_solver (const mtf_parameters &params, const std::vector <mtf_measurement> &measured, 
              progress_info *progress, bool verbose)
      : m_measurements (measured), m_params (params), m_progress (progress),
        be_verbose (verbose), start ()
  {
    nvalues = 0;
    m_params.measured_mtf_idx = -1;
    m_params.clear_data ();
    if (!params.pixel_pitch || !params.scan_dpi)
      {
        if (!m_params.sigma)
	  {
	    start_vec.push_back (0);
            sigma_index = nvalues++;
	  }
        else
          sigma_index = -1;
	int cur_blur_index = -1;
	for (int m = 0; m < m_measurements.size (); m++)
	  {
	    if (!m_measurements[m].same_capture)
	      cur_blur_index = -1;
	    wavelength_index.push_back (-1);
	    if (!m_params.blur_diameter)
	      {
		if (cur_blur_index == -1)
		  {
		    start_vec.push_back (0);
		    cur_blur_index = nvalues;
		    blur_index.push_back (nvalues++);
		  }
		else
		  blur_index.push_back (cur_blur_index);
	      }
	    else
	      blur_index.push_back(-1);
	  }
        difraction = false;
      }
    else
      {
        difraction = true;
        if (!m_params.f_stop)
          {
	    start_vec.push_back (8);
            f_stop_index = nvalues++;
          }
        else
          f_stop_index = -1;
        if (!m_params.sigma)
	  {
	    start_vec.push_back (0);
            sigma_index = nvalues++;
	  }
        else
          sigma_index = -1;
	chanel_wavelength_index[0] = -1;
	chanel_wavelength_index[1] = -1;
	chanel_wavelength_index[2] = -1;
	chanel_wavelength_index[3] = -1;
	int cur_defocus_index = -1;
	for (int m = 0; m < m_measurements.size (); m++)
	  {
	    if (!m_measurements[m].same_capture)
	      cur_defocus_index = -1;
	    if (!m_params.defocus)
	      {
		if (cur_defocus_index == -1)
		  {
		    start_vec.push_back (0);
		    cur_defocus_index = nvalues;
		    printf ("Defocus %i\n", nvalues);
		    blur_index.push_back (nvalues++);
		  }
		else
		  blur_index.push_back (cur_defocus_index);
	      }
	    else
	      blur_index.push_back (-1);
	    if (m_measurements[m].channel >= 0)
	      {
		int c = m_measurements[m].channel;
		if (m_params.wavelengths[c] > 0)
		  wavelength_index.push_back (-1);
		else if (chanel_wavelength_index[c] >= 0)
		  wavelength_index.push_back (chanel_wavelength_index[c]);
		else
		  {
		    start_vec.push_back (0.5);
		    chanel_wavelength_index [c] = nvalues++;
		    wavelength_index.push_back (chanel_wavelength_index[c]);
		  }
	      }
	    else if (!m_measurements[m].wavelength)
	      {
		start_vec.push_back (0.5);
		wavelength_index.push_back (nvalues++);
	      }
	    else
	      wavelength_index.push_back (-1);
	  }
        if (!m_params.sensor_fill_factor)
	  {
	    start_vec.push_back (1);
            fill_factor_index = nvalues++;
	  }
        else
          fill_factor_index = -1;
      }
    n_observations = 0;
    for (size_t m = 0; m < m_measurements.size (); m++)
      {
	auto &measurement = m_measurements[m];
	for (size_t i = 0; i < measurement.size (); i++)
	  if (measurement.get_freq (i) <= 0.5)
	    n_observations++;
      }
    start = start_vec.data ();
    assert (nvalues == start_vec.size ());
  }
  int
  num_values ()
  {
    return nvalues;
  }
  luminosity_t
  epsilon ()
  {
    return 0.0000001;
  }
  luminosity_t
  derivative_perturbation ()
  {
    return 0.00001;
  }
  bool
  verbose ()
  {
    return be_verbose;
  }
  luminosity_t
  scale ()
  {
    return 1;
  }
  void
  constrain (luminosity_t *vals)
  {
    if (fill_factor_index >= 0 && vals[fill_factor_index] < 0.1)
      vals[fill_factor_index] = 0.1;
    if (fill_factor_index >= 0 && vals[fill_factor_index] > 8)
      vals[fill_factor_index] = 8;
    if (sigma_index >= 0 && vals[sigma_index] < 0)
      vals[sigma_index] = 0;
    for (int e : blur_index)
      if (e >= 0 && vals[e] < 0)
        vals[e] = 0;
    for (int e : wavelength_index)
      if (e >= 0)
	vals[e] = std::clamp (vals[e], (luminosity_t)0, (luminosity_t)1);
    if (f_stop_index >= 0)
      vals[f_stop_index] = std::clamp (vals[f_stop_index],
                                           (luminosity_t)0.5, (luminosity_t)16);
  }
  luminosity_t
  get_fill_factor (const luminosity_t *vals)
  {
    if (fill_factor_index < 0)
      return m_params.sensor_fill_factor;
    return vals[fill_factor_index];
  }
  luminosity_t
  get_wavelength (int measurement, const luminosity_t *vals)
  {
    if (m_measurements[measurement].wavelength > 0)
      return m_measurements[measurement].wavelength;
    if (m_measurements[measurement].channel >= 0
	&& m_params.wavelengths[m_measurements[measurement].channel] > 0)
      return m_params.wavelengths[m_measurements[measurement].channel];
    if (wavelength_index[measurement] < 0)
      return m_params.wavelength;
    return vals[wavelength_index[measurement]] * (nanometers_max - nanometers_min)
           + nanometers_min;
  }
  luminosity_t
  get_f_stop (const luminosity_t *vals)
  {
    if (f_stop_index < 0)
      return m_params.f_stop;
    return vals[f_stop_index];
  }
  luminosity_t
  get_sigma (const luminosity_t *vals)
  {
    if (sigma_index < 0)
      return m_params.sigma;
    return vals[sigma_index];
  }
  luminosity_t
  get_defocus (int measurement, const luminosity_t *vals)
  {
    if (!difraction || blur_index[measurement] < 0)
      return m_params.defocus;
    return vals[blur_index[measurement]];
  }
  luminosity_t
  get_blur_diameter (int measurement, const luminosity_t *vals)
  {
    if (difraction || blur_index[measurement] < 0)
      return m_params.blur_diameter;
    return vals[blur_index[measurement]];
  }
  luminosity_t
  objfunc (const luminosity_t *vals, luminosity_t *f_vec = NULL)
  {
    luminosity_t sum = 0;
    mtf_parameters p = m_params;
    p.sigma = get_sigma (vals);
    p.f_stop = get_f_stop (vals);
    int out_idx = 0;
    for (size_t m = 0; m < m_measurements.size (); m++)
      {
	auto &measurement = m_measurements[m];
	p.wavelength = get_wavelength (m, vals);
	if (difraction)
	  p.defocus = get_defocus (m, vals);
	else
	  p.blur_diameter = get_blur_diameter (m, vals);
	assert (difraction == p.simulate_difraction_p ());
	for (size_t i = 0; i < measurement.size (); i++)
	  {
	    luminosity_t freq = measurement.get_freq (i);
	    /* Do not care about values above Nyquist.  */
	    if (freq > 0.5)
	      continue;
	    luminosity_t contrast = measurement.get_contrast (i);
	    luminosity_t contrast2 = p.system_mtf (freq) * 100;
	    sum += (contrast - contrast2) * (contrast - contrast2);
	    if (f_vec && out_idx < n_observations)
	      f_vec[out_idx++] = contrast - contrast2;
#if 0
	    if (be_verbose)
	      debug_data (freq, contrast, contrast2);
#endif
	  }
      }
    if (be_verbose)
      {
	if (m_progress)
	  m_progress->pause_stdout ();
	printf ("gaussian blur sigma %f, wavelength %f, defocus %f, blur_diameter %f, sqsum %f\n",
	        p.sigma, p.wavelength, p.defocus, p.blur_diameter, sum);
	if (m_progress)
	  m_progress->resume_stdout ();
      }
    return sum;
  }
  int
  num_observations ()
  {
    return n_observations;
  }
  int
  residuals (const luminosity_t *vals, luminosity_t *f_vec)
  {
    objfunc (vals, f_vec);
    return GSL_SUCCESS;
  }
  const std::vector <mtf_measurement> &m_measurements;
  mtf_parameters m_params;
  progress_info *m_progress;
  bool be_verbose;
  std::vector<luminosity_t> start_vec;
  luminosity_t *start;
  bool difraction;
  int nvalues;
  int n_observations;
  int sigma_index;
  int fill_factor_index;
  std::vector<int> wavelength_index;
  std::array<int,4> chanel_wavelength_index;
  std::vector<int> blur_index;
  int f_stop_index;
};

/* Determine PSF kernel radius.  */
static int
get_psf_radius (mtf::psf_t *psf, int size, bool *ok = NULL)
{
  double peak = 0;
  for (int i = 0; i < size; i++)
    {
      // iprintf ("%i %f\n", i, psf[i]);
      if (psf[i] > peak)
        peak = psf[i];
    }
  int this_psf_radius = 1;
  /* Center of the PSF kernel is at 0  */
  for (int i = 1; i < size / 2 - 1; i++)
    if (psf[i] > peak * 0.0001f)
      this_psf_radius = i + 1;

  /* This may be solved by iteratively reducing subsampling.  */
  if (this_psf_radius >= size / 2 - 2)
    {
      if (ok)
        *ok = false;
    }
  else if (ok)
    *ok = true;
  return this_psf_radius;
}

}

/* Return sensor MTF at given pixel frequency.
   TODO: model Bayer.
   Bayer demosaicing usually reduces MTF by an additional factor of 0.8 to 0.9
   near the Nyquist frequency */
luminosity_t
mtf_parameters::sensor_mtf (double pixel_freq) const
{
  return std::abs (sinc (pixel_freq * my_sqrt (sensor_fill_factor)));
}

/* The effective f-fstop changes in macro photography based on magnification.
 */

double
mtf_parameters::effective_f_stop () const
{
  if (!scan_dpi || !f_stop)
    return f_stop;
  double scan_pixel_pitch_um = 25400.0 / scan_dpi;
  double magnification = pixel_pitch / scan_pixel_pitch_um;
  // printf ("Magnification %f\n", magnification);
  return f_stop * (1 + magnification);
}

/* Normalized frequency (nu)
   nu needs to be in range 0 to 1. 1 is the absolute resoultion of the lens. */
double
mtf_parameters::nu (double pixel_freq) const
{
  double freq = pixel_freq / (pixel_pitch * (1.0 / 1000));
  double wavelength_mm = wavelength / 1e6;
  double cutoff_freq = 1.0 / (wavelength_mm * effective_f_stop ());
  return std::clamp (freq / cutoff_freq, 0.0, 1.0);
}

/* Return MTF of perfect difraction limited lens.  */

luminosity_t
mtf_parameters::lens_difraction_mtf (double pixel_freq) const
{
  /* IF we have measured MTF data, difractoin is already included.  */
  double s = nu (pixel_freq);
  return (2.0 / M_PI) * (std::acos (s) - s * sqrt (1.0 - s * s));
}

/* Estimate lens blur using Hopkins model.  */
luminosity_t
mtf_parameters::hopkins_defocus_mtf (double pixel_freq) const
{
  double ef_stop = effective_f_stop ();
  double s = nu (pixel_freq);
  double freq = pixel_freq / (pixel_pitch * (1.0 / 1000));
  double wavelength_mm = wavelength / 1e6;
  /* Slight defocus model; not seem to be working in practice.  */
  if (0)
    {
      double blur_diameter = std::abs (defocus) / effective_f_stop ();
      return defocus_mtf (freq, blur_diameter);
    }
  if (s <= 0 || s >= 1)
    return 1;

  /*Calculate Wavefront Defocus (W_{020}). This convertes physical
    distance to wavefront error.

    W_{20} = delta_z / (8 * f_number^2)  */
  double w20 = defocus / (8.0 * ef_stop * ef_stop);

#if 0
  /* Hopkins Defocus Factor
     This is a simplified wave-optical approximation for slight defocus  */
  double arg = 4.0 * M_PI * w20 * s * (1.0 - s) / wavelength_mm;
  return (std::abs(arg) < 1e-9) ? 1.0 : std::sin(arg) / arg;
#endif

  /* Hopkins Defocus Factor
     Z = (2 * pi / wavelength) * w20 * 4 * nu * (1 - nu)  */
  double Z = (2.0 * M_PI / wavelength_mm) * w20 * 4.0 * s * (1.0 - s);

  /* Apply the Defocus Transfer Function using Bessel J1
     Handle the limit where Z -> 0 to avoid division by zero  */
  /* Ringing effect of defocus is modelled by Bessel function.  */
  if (std::abs (Z) > 1e-9)
    return fabs (2.0 * get_j1 (Z) / Z);
  // return fabs (2.0 * j1 (Z) / Z);
  return 1;
}

luminosity_t
mtf_parameters::stokseth_defocus_mtf (double pixel_freq) const
{
  double ef_stop = effective_f_stop ();
  double freq = pixel_freq / (pixel_pitch * (1.0 / 1000));
  // 1. Calculate Diffraction Cutoff (fc)
  double wavelength_mm = wavelength / 1e6;
  double cutoff_freq = 1.0 / (wavelength_mm * ef_stop);

  if (freq <= 0.0)
    return 1.0;
  if (freq >= cutoff_freq)
    return 0.0;

  double s = freq / cutoff_freq; // Normalized frequency (0 to 1)

  // 2. Calculate Wavefront Aberration W20 (in mm)
  // Formula: delta_z / (8 * N_eff^2)
  double w20 = defocus / (8.0 * std::pow (ef_stop, 2));

  // 3. Stokseth B Parameter
  // Scales the defocus impact by frequency
  double B
      = (4.0 * M_PI * w20 * freq / (wavelength_mm * cutoff_freq)) * (1.0 - s);

  // 5. Defocus MTF using Bessel J1
  // 2*J1(B)/B is the optical transfer of a circular blur
  double j_term = (std::abs (B) < 1e-8) ? 1.0 : 2.0 * get_j1 (B) / B;

  // 6. Full Stokseth Polynomial Correction (1 - 0.6s + 0.4s^2)
  double stokseth_poly = 1.0 - 0.6 * s + 0.4 * s * s;

  // 7. Empirical Correction (1 - 0.6s)
  double phase_error_ratio = w20 / (wavelength_mm * 0.25);
  double weight = std::clamp (phase_error_ratio, 0.0, 1.0);

  // Apply correction: if weight is 0 (perfect focus), factor is 1.0.
  // If weight is 1 (significant defocus), factor is the Stokseth polynomial.
  double final_correction = (1.0 - weight) + (weight * stokseth_poly);
  return std::abs (j_term) * final_correction;
}

/* Simulate lens as a combination of
    - difraction limit (pixel_pitch, wavelength_mm, f_stop)
    - gaussian blur (sigma)
    - defocus (defocus_mm)
 */

luminosity_t
mtf_parameters::lens_mtf (double pixel_freq) const
{
  if (simulate_difraction_p ())
    return stokseth_defocus_mtf (pixel_freq) * lens_difraction_mtf (pixel_freq)
           * gaussian_blur_mtf (pixel_freq, sigma);
  else
    return gaussian_blur_mtf (pixel_freq, sigma)
           * defocus_mtf (pixel_freq, blur_diameter);
}

/* Adjustment factor to measured mtf basd on
    - gaussian blur (sigma)
    - defocus (defocus_mm)
 */

luminosity_t
mtf_parameters::measured_mtf_correction (double pixel_freq) const
{
  if (simulate_difraction_p ())
    return stokseth_defocus_mtf (pixel_freq)
           * gaussian_blur_mtf (pixel_freq, sigma);
  else
    return gaussian_blur_mtf (pixel_freq, sigma)
           * defocus_mtf (pixel_freq, blur_diameter);
}

/* Simulate system as a combination of sensor MTF and lens MTF.  */

luminosity_t
mtf_parameters::system_mtf (double pixel_freq) const
{
  return sensor_mtf (pixel_freq) * lens_mtf (pixel_freq);
}

/* Compute right half of LSF.  */

void
mtf::compute_lsf (std::vector<psf_t, fft_allocator<psf_t>> &lsf,
                  luminosity_t subsample) const
{
  int size = lsf.size ();
  if (size & 1)
    {
      lsf[size - 1] = 0;
      size--;
    }
  std::vector<psf_t, fft_allocator<psf_t>> mtf_half (size);
  auto plan = fft_plan_r2r_1d<psf_t> (size, FFTW_REDFT00, mtf_half.data (), lsf.data ());
  psf_t scale = 1 / (size * subsample * 2);

  /* Mirror mtf.  */
  for (int i = 0; i < size; i++)
    mtf_half[i] = get_mtf (i * scale);

  plan.execute_r2r (mtf_half.data (), lsf.data ());

  psf_t sum = 0;
  for (int i = 0; i < size; i++)
    sum += lsf[i];
  psf_t fin_scale = 1.0 / sum;
  for (int i = 0; i < size; i++)
    lsf[i] *= fin_scale;
}

std::vector<mtf::psf_t, fft_allocator<mtf::psf_t>>
mtf::compute_2d_psf (int psf_size, luminosity_t subscale,
                     progress_info *progress)
{
  int fft_size = psf_size / 2 + 1;
  const psf_t psf_step = 1 / (psf_size * subscale);
  // Use unique_ptr with FFTW allocator for fftw_complex array
  auto mtf_kernel = fft_alloc_complex<psf_t> (psf_size * fft_size);
  std::vector<psf_t, fft_allocator<psf_t>> psf_data (psf_size * psf_size);
  auto plan = fft_plan_c2r_2d<psf_t> (psf_size, psf_size, mtf_kernel.get (), psf_data.data ());
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(fft_size, psf_step, mtf_kernel, psf_size)
  for (int y = 0; y < fft_size; y++)
    for (int x = 0; x < fft_size; x++)
      {
        std::complex ker (std::clamp (get_mtf (x, y, psf_step),
                                      (luminosity_t)0, (luminosity_t)1),
                          (luminosity_t)0);
        mtf_kernel.get ()[y * fft_size + x][0] = real (ker);
        mtf_kernel.get ()[y * fft_size + x][1] = imag (ker);
        if (y)
          {
            mtf_kernel.get ()[(psf_size - y) * fft_size + x][0] = real (ker);
            mtf_kernel.get ()[(psf_size - y) * fft_size + x][1] = imag (ker);
          }
      }
  plan.execute_c2r (mtf_kernel.get (), psf_data.data ());

  return psf_data;
}

/* Determine raidus of PSF to be sure that the minimal value is at most MAX *
   MIN_THRESHOLD. If SUM_THRESHOLD is non-zero reduce it then so the sum of the
   kernel up to radius is 1-sum_threshold of the overall kernel.  */
luminosity_t
mtf::estimate_psf_size (luminosity_t min_threshold,
                        luminosity_t sum_threshold) const
{
  /* Make a guess that PSF does not spread over 4 pixels.  */
  luminosity_t subscale = 1 / 32.0;
  int lsf_size = 256;
  while (true)
    {
      std::vector<psf_t, fft_allocator<psf_t>> lsf (lsf_size);
      compute_lsf (lsf, subscale);
      psf_t max = 0;
      for (auto v : lsf)
        max = std::max (max, v);
      /* Not good enough.  */
      if (lsf.back () > max * min_threshold)
        {
          if (lsf_size < 4096)
            lsf_size *= 2;
          else
            subscale *= 2;
          continue;
        }
      int radius = 1;
      for (int v = 2; v < lsf_size; v++)
        if (lsf[v] > max * min_threshold)
          radius = v + 1;
#if 0
      if (sum_threshold)
	{
	  double sum = 0;
	  for (int radius = 0; radius < lsf_size && sum < 1 - sum_threshold; radius++)
	    sum += lsf[radius];
	  if (radius == lsf_size)
	    {
	      printf ("Upscaling 2 %i %f %f\n", lsf_size, subscale, sum);
	      if (lsf_size < 4096)
		lsf_size *= 2;
	      else
		subscale *= 2;
	      continue;
	    }
	}
#endif
      assert (radius);
      return radius * subscale;
    }
}

/* Compute PSF as 2D FFT of circular MTF.
   MAX_RADIUS is an estimate of radius.  SUBSCALE is a size of
   pixel we compute at (smaller pixel means more precise PSF)  */
bool
mtf::compute_psf (luminosity_t max_radius, luminosity_t subscale, const char *filename,
                  const char **error)
{
  bool verbose = false;
  /* Cap size of FFT to solve.  */
  while (ceil (max_radius / subscale) * 2 + 1 > 1024)
    subscale *= 2;
  int psf_size = ceil (max_radius / subscale) * 2 + 1;
  int iterations = 0;

  while (true)
    {
      /* Determine PSF radius.  */

      bool ok;
      auto psf_data = mtf::compute_2d_psf (psf_size, subscale, NULL);
      if (!psf_data.size ())
        return false;
      int radius = get_psf_radius (psf_data.data (), psf_size, &ok);
      /* If FFT size was to small for the PSF, increase it and restart.  */
      if (!ok && iterations < 10)
        {
          if (psf_size < 1024)
            psf_size *= 2;
          else
            subscale /= 2;
	  printf ("Iterating PSF computation %i %i %f\n", iterations, psf_size, subscale);
          iterations++;
          continue;
        }
      m_psf_radius = radius * subscale;

      /* Compute LSF. Circular LSF is PSF.  */
      /* Make sure PSF also trails by 0.  */
      luminosity_t d1 = psf_data[radius];
      luminosity_t d2 = psf_data[radius + 1];
      psf_data[radius] = 0;
      psf_data[radius + 1] = 0;
      m_psf.set_range (0, (radius + 2) * subscale);
      m_psf.init_by_y_values (psf_data.data (), radius + 2);
      psf_data[radius] = d1;
      psf_data[radius + 1] = d2;
      if (filename)
        {
          tiff_writer_params pp;
          int width = 2 * radius;
          int height = 2 * radius;
          pp.width = width;
          const int lsf_size = 100;
          pp.height = height + lsf_size;
          pp.depth = 16;
          const char *error;
          pp.filename = filename;
          tiff_writer renderedu (pp, &error);
          if (error)
            return false;
          luminosity_t err = 0, m = 0;
          for (int y = 0; y < psf_size / 2; y++)
            for (int x = 0; x < psf_size / 2; x++)
              {
                luminosity_t val = get_psf (x, y, 1 / subscale);
                luminosity_t diff = fabs (val - psf_data[y * psf_size + x]);
                if (val > m)
                  m = val;
                if (diff > err)
                  err = diff;
                // psf_data[y * psf_size + x] = val;
              }
          for (int y = 0; y < height; y++)
            {
              for (int x = 0; x < width; x++)
                {
                  int xp = nearest_int (x * subscale);
                  int yp = nearest_int (y * subscale);
                  int xx = ((x + psf_size / 2 - radius) + psf_size / 2)
                           % psf_size;
                  int yy = ((y + psf_size / 2 - radius) + psf_size / 2)
                           % psf_size;
                  int v = std::clamp (
                      (int)(invert_gamma (psf_data[yy * psf_size + xx] / m, -1)
                                * (65535)
                            + 0.5),
                      0, 65535);
                  int vv
                      = std::clamp (v + 100 * 256 * ((xp + yp) % 2), 0, 65535);
                  renderedu.put_pixel (x, v, v, vv);
                }
              if (!renderedu.write_row ())
                return false;
            }
          std::vector<psf_t, fft_allocator<psf_t>> lsf (radius);
          compute_lsf (lsf, subscale);
          psf_t lsf_max = lsf[0], collected_lsf_max = 0;
          std::vector<psf_t, fft_allocator<psf_t>> collected_lsf (width);
          for (int x = 0; x < width; x++)
            {
              int xx = (x < radius ? radius - x - 1 : x - radius);
              for (int y = 0; y < psf_size; y++)
                collected_lsf[x] += psf_data[xx * psf_size + y];
              if (collected_lsf[x] > collected_lsf_max)
                collected_lsf_max = collected_lsf[x];
            }
          for (int y = 0; y < lsf_size; y++)
            {
              for (int x = 0; x < width; x++)
                {
                  int idx = lsf_size
                            - nearest_int (
                                lsf[(x < radius ? radius - x - 1 : x - radius)]
                                * lsf_size / lsf_max);
                  int idx2
                      = lsf_size
                        - nearest_int (psf_data[((x + psf_size / 2 - radius)
                                                 + psf_size / 2)
                                                % psf_size]
                                       * lsf_size / m);
                  int idx3 = lsf_size
                             - nearest_int (collected_lsf[x] * lsf_size
                                            / collected_lsf_max);
                  int r = (idx == y) * 65535;
                  int g = (idx2 == y) * 65535;
                  int b = (idx3 == y) * 65535;
                  renderedu.put_pixel (x, r, g, b);
                }
              if (!renderedu.write_row ())
                return false;
            }
          if (verbose)
            printf ("Max %f, err %f normalized %f\n", m, err, err / m);
        }
      return true;
    }
}

bool
mtf::precompute (progress_info *progress)
{
  m_lock.lock ();
  if (m_precomputed)
    {
      m_lock.unlock ();
      return true;
    }

  /* Use actual MTF data.  */
  if (m_params.use_measured_mtf ())
    {
      bool monotone = true;
      mtf_measurement &measurement = m_params.measurements[m_params.measured_mtf_idx];
      for (size_t i = 1; i < measurement.size () && monotone; i++)
        {
          if (!(measurement.get_freq (i - 1) < measurement.get_freq (i)))
            {
              printf ("Sorry, unimplemented: MTF freqs are not in increasing "
                      "order.\n");
              monotone = false;
              abort ();
            }
        }

      /* See if the MTF table has regular steps. In this case we can initialize
         precomputed function exactly.  */
      bool regular_steps = true;
      luminosity_t step
          = (measurement.get_freq (measurement.size () - 1) - measurement.get_freq (0)) / (measurement.size () - 1);
      for (size_t i = 1; i < measurement.size () - 2 && regular_steps; i++)
        if (fabs (measurement.get_freq (i) - measurement.get_freq (0) - i * step) > 0.0006)
          regular_steps = false;

      if (!regular_steps)
        {
          std::vector<luminosity_t, fft_allocator<psf_t>> contrasts (measurement.size ()
                                                                       + 2);
          std::vector<luminosity_t, fft_allocator<psf_t>> freqs (measurement.size ()
                                                                   + 2);
          for (size_t i = 0; i < measurement.size (); i++)
            {
              contrasts[i] = measurement.get_contrast (i) * 0.01
                             * m_params.measured_mtf_correction (measurement.get_freq (i));
              freqs[i] = measurement.get_freq (i);
            }
          contrasts[measurement.size ()] = 0;
          contrasts[measurement.size () + 1] = 0;
          freqs[measurement.size ()] = freqs[measurement.size () - 1] + step;
          freqs[measurement.size () + 1] = freqs[measurement.size () - 1] + 2 * step;
          m_mtf.set_range (measurement.get_freq (0), measurement.get_freq (measurement.size () - 1) + 2 * step);
          m_mtf.init_by_x_y_values (freqs.data (), contrasts.data (),
                                    measurement.size () + 2, 1024);
        }
      else
        {
          std::vector<luminosity_t, fft_allocator<psf_t>> contrasts (measurement.size ()
                                                                       + 2);
          for (size_t i = 0; i < measurement.size (); i++)
            contrasts[i] = measurement.get_contrast (i) * 0.01
                           * m_params.measured_mtf_correction (measurement.get_freq (i));
          /* Be sure that MTF trails in 0.  */
          contrasts[measurement.size ()] = 0;
          contrasts[measurement.size () + 1] = 0;
          m_mtf.set_range (measurement.get_freq (0), measurement.get_freq (measurement.size () - 1) + 2 * step);
          m_mtf.init_by_y_values (contrasts.data (), measurement.size () + 2);
        }
      if (colorscreen_checking)
        for (size_t i = 0; i < measurement.size (); i++)
          {
            luminosity_t freq = measurement.get_freq (i);
            if (fabs (measurement.get_contrast (i) * 0.01
                          * m_params.measured_mtf_correction (freq)
                      - get_mtf (freq))
                > 0.01)
              {
                printf (
                    "Mismatch (measured) %i freq %f table %f precomputed %f\n",
                    (int)i, freq,
                    measurement.get_contrast (i) * 0.01
                        * m_params.measured_mtf_correction (freq),
                    m_mtf.apply (freq));
                abort ();
              }
          }

      if (progress)
        progress->set_task ("computing point spread function", 1);
    }
  /* Use lens model.  */
  else
    {
      const int entries = 512;
      std::vector<luminosity_t, fft_allocator<psf_t>> contrasts (entries);
      luminosity_t step = 1.0 / (entries - 2);
      for (int i = 0; i < entries - 2; i++)
        contrasts[i] = m_params.system_mtf (i * step);
      contrasts[entries - 2] = contrasts[entries - 1] = 0;
      m_mtf.set_range (0, 1 + step);
      m_mtf.init_by_y_values (contrasts.data (), entries);
      int radius;

      if (colorscreen_checking)
        for (int i = 0; i < 1000; i++)
          if (fabs (m_params.system_mtf (i / 1000.0)
                    - m_mtf.apply (i / 1000.0))
              > 0.001)
            {
              printf ("Mismatch (model) %f %f\n",
                      m_params.system_mtf (i / 1000.),
                      m_mtf.apply (i / 1000.0));
              abort ();
            }

      if (progress)
        progress->set_task ("computing point spread function", 1);
    }
  m_psf_radius = estimate_psf_size ();
  // print_lsf (stdout);

  // m_mtf.plot (0, 1);
  // m_psf.plot (0, 5);
  m_precomputed = true;
  m_lock.unlock ();
  return true;
}
bool
mtf::precompute_psf (progress_info *progress, const char *filename, const char **error)
{
  if (!precompute (progress))
    return false;
  m_lock.lock ();
  if (m_precomputed_psf)
    {
      m_lock.unlock ();
      return true;
    }
  if (!compute_psf (psf_size (1), 1 / 32.0, filename, error))
    {
      m_lock.unlock ();
      return false;
    }
  m_precomputed_psf = true;
  m_lock.unlock ();
  return true;
}

void
mtf::print_psf (FILE *f)
{
  luminosity_t scale = get_psf (0);
  printf ("psf radius %f\n", m_psf_radius);
  for (int i = 0; i < m_psf_radius * 10; i++)
    fprintf (f, "%1.3f %1.3f\n", i * 0.1, get_psf (i * 0.1) / scale);
}

mtf *
mtf::get_new_mtf (struct mtf_parameters &p, progress_info *)
{
  return new mtf (p);
}

static mtf::mtf_cache_t
    mtf_cache ("Modulation transfer functions");

mtf::mtf_cache_t::cached_ptr
mtf::get_mtf (const mtf_parameters &mtfp, progress_info *p)
{
  return mtf_cache.get_cached (const_cast<mtf_parameters &> (mtfp), p);
}

bool
mtf_parameters::save_psf (progress_info *progress, const char *write_table,
                          const char **error) const
{
  mtf mtf (*this);
  return mtf.precompute_psf (progress, write_table, error);
}

bool
mtf_parameters::print_csv_header (FILE *f) const
{
  return fprintf (
             f,
             "difraction f-stop 0/%.0f (effective 0/%.0f) wavelength %f.0nm "
             "magnification %f pixel pitch %f	defocus %.5fmm	"
             "sigma=%.2fpx	lens	sensor fill factor %.1f	estimated\n",
             f_stop, effective_f_stop (), wavelength,
             pixel_pitch / (25400.0 / scan_dpi), pixel_pitch, defocus, sigma,
             sensor_fill_factor)
         >= 0;
}

bool
mtf_parameters::write_table (const char *write_table, const char **error) const
{
  if (write_table)
    {
      FILE *f = fopen (write_table, "wt");
      if (!f)
        {
          *error = "failed to open output file";
          return false;
        }
      if (fprintf (f, "frequency	") < 0 || !print_csv_header (f))
        {
          *error = "write error";
          return false;
        }
      for (size_t i = 0; i < 400; i++)
        {
          luminosity_t freq = i / 400.0;
          if (fprintf (f,
                       "%1.3f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f	"
                       "%2.2f\n",
                       freq, lens_difraction_mtf (freq) * 100,
                       stokseth_defocus_mtf (freq) * 100,
                       gaussian_blur_mtf (freq, sigma) * 100,
                       lens_mtf (freq) * 100, sensor_mtf (freq) * 100,
                       system_mtf (freq) * 100)
              < 0)
            {
              *error = "write error";
              return false;
            }
        }
      if (fclose (f))
        {
          *error = "error closing output file";
          return false;
        }
    }
  return true;
}

mtf_parameters::computed_mtf
mtf_parameters::compute_curves (int steps) const
{
  computed_mtf result;
  result.system_mtf.reserve (steps);
  result.sensor_mtf.reserve (steps);
  result.gaussian_blur_mtf.reserve (steps);
  result.stokseth_defocus_mtf.reserve (steps);
  result.lens_difraction_mtf.reserve (steps);
  result.lens_mtf.reserve (steps);
  result.hopkins_blur_mtf.reserve (steps);

  for (int i = 0; i < steps; i++)
    {
      double freq = i / (double)(steps - 1);
      result.lens_difraction_mtf.push_back (lens_difraction_mtf (freq));
      result.stokseth_defocus_mtf.push_back (stokseth_defocus_mtf (freq));
      result.gaussian_blur_mtf.push_back (gaussian_blur_mtf (freq, sigma));
      result.lens_mtf.push_back (lens_mtf (freq));
      result.sensor_mtf.push_back (sensor_mtf (freq));
      result.system_mtf.push_back (system_mtf (freq));
      result.hopkins_blur_mtf.push_back (defocus_mtf (freq, blur_diameter));
    }

  return result;
}

double
mtf_parameters::estimate_parameters (mtf_parameters &par,
                                     const char *write_table,
                                     progress_info *progress,
                                     const char **error, int flags)
{
  *this = par;

  mtf_solver s (*this, par.measurements, progress, flags | estimate_verbose);
  if (flags & estimate_use_nmsimplex)
    gsl_simplex<luminosity_t, mtf_solver> (s, "optimizing lens parameters (simplex)",
				       progress);
  if (flags & estimate_use_multifit)
    gsl_multifit<luminosity_t, mtf_solver> (s, "optimizing lens parameters (multifit)",
				       progress);
  wavelength = s.get_wavelength (0, s.start);
  f_stop = s.get_f_stop (s.start);
  sigma = s.get_sigma (s.start);
  defocus = s.get_defocus (0, s.start);
  blur_diameter = s.get_blur_diameter (0, s.start);
  sensor_fill_factor = s.get_fill_factor (s.start);

  if (write_table)
    {
      FILE *f = fopen (write_table, "wt");
      if (!f)
        {
          *error = "failed to open CSV file for writting";
          return -1;
        }
      if (fprintf (f, "frequency	measured MTF	") < 0
          || !print_csv_header (f))
        {
          *error = "write error in CSV file";
          return -1;
        }
      for (size_t m = 0; m < par.measurements.size (); m++)
      {
	auto &measurement = par.measurements[m];
	for (size_t i = 0; i < measurement.size (); i++)
	  {
	    luminosity_t freq = measurement.get_freq (i);
	    luminosity_t c = measurement.get_contrast (i);
	    if (fprintf (f,
			 "%1.3f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f	"
			 "%2.2f	%2.2f\n",
			 freq, c, lens_difraction_mtf (freq) * 100,
			 stokseth_defocus_mtf (freq) * 100,
			 gaussian_blur_mtf (freq, sigma) * 100,
			 lens_mtf (freq) * 100, sensor_mtf (freq) * 100,
			 system_mtf (freq) * 100)
		< 0)
	      {
		*error = "write error in CSV file";
		return -1;
	      }
	  }
	fprintf (f,"\n");
      }
      if (fclose (f))
        {
          *error = "error closing CSV file";
          return -1;
        }
    }

#if 0
  if (verbose)
    {
      for (size_t i = 0; i < par.size (); i++)
        {
          luminosity_t freq = par.get_freq (i);
          luminosity_t v1 = par.get_contrast (i);
          luminosity_t v2 = system_mtf (freq) * 100;
          debug_data (freq, v1, v2);
        }
    }
#endif
  return s.objfunc (s.start);
}

int
mtf_parameters::load_csv (FILE *in, std::string name, const char **error)
{
  struct row {float freq; float contrast[4];};
  bool rgb = false;
  std::vector<row> data;
  char line[1024];
  float v1, v2, v3, v4, v5;
  char extra;
  while (fgets(line, sizeof(line), in)) {
    int itemsFound = sscanf(line, "%f\t%f\t%f\t%f\t%f %c", &v1, &v2, &v3, &v4, &v5, &extra);
    /* Data are saved in order freq, red, green, blue, combined  */
    if (itemsFound == 5) 
      {
	if (v2 != v3 || v2 != v3 || v2 != v4 || v2 != v5)
	  rgb = true;
        data.push_back ({v1,{v2,v3,v4,v5}});
      }
    else
      {
	*error = "Quickmtf output file should contain 4 tab separated values on every line:\n"
		"pixel_frequency	blue_contrast	green_constrast	red_contrast	combined_contrast\n"
		"contrasts are in percents.\n";
	return -1;
      }
  }
  for (int c = rgb ? 0 : 3; c < (rgb ? 3 : 4); c++)
    {
      mtf_measurement m;
      const char *color[3]={"red","green","blue"};
      if (rgb)
	{
	  m.name = name + " " + color[c];
	  /* It seems that it is blue/green/red  */
	  m.channel = 3 - c;
	  if (c)
	    m.same_capture = true;
	}
      else
	{
	  m.name = name;
	  m.channel = -1;
	}
      for (int i = 0; i < data.size (); i++)
	m.add_value (data[i].freq,data[i].contrast[c]);
      printf ("size %i %i\n", m.size (), data.size ());
      measurements.push_back (m);
    }
  printf ("File loaded %i\n",(int)rgb);
  return rgb ? 3 : 1;
}

}
