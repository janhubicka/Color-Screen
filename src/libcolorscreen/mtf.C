#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "mtf.h"
#include "nmsimplex.h"
#include <cmath>
#include <complex>
namespace colorscreen
{

namespace
{

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
  return std::abs (2.0 * j1 (arg) / arg);
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

/* Calculates LSF accounting for both Lens (sigma) and Sensor (pixel aperture)
 */
double
calculate_system_lsf (double x, double sigma)
{
  if (sigma < 0.01)
    {
      /* If sigma is near zero, LSF is just the pixel box (Rect function)  */
      return (std::abs (x) <= 0.5) ? 1.0 : 0.0;
    }

  const double sqrt2 = 1.41421356237;

  // Integration of Gaussian from (x - 0.5) to (x + 0.5)
  double term1 = std::erf ((x + 0.5) / (sigma * sqrt2));
  double term2 = std::erf ((x - 0.5) / (sigma * sqrt2));

  return 0.5 * (term1 - term2);
}

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
  static constexpr const bool be_verbose = false;
  mtf_solver (const mtf_parameters &measured, const mtf_parameters &params,
              progress_info *progress)
      : m_measured_params (params), m_params (params), m_progress (progress),
        start{ 0.5, 0.5, 0.5 }
  {
    nvalues = 0;
    m_params.clear_data ();
    if (!params.pixel_pitch || !params.f_stop)
      {
        wavelength_index = -1;
        if (!m_params.sigma)
          sigma_index = nvalues++;
        else
          sigma_index = -1;
        if (!m_params.blur_diameter)
          blur_index = nvalues++;
        else
          blur_index = -1;
        difraction = false;
      }
    else
      {
        difraction = true;
        if (!m_params.wavelength)
          {
            wavelength_index = nvalues++;
            start[wavelength_index] = 0.5;
          }
        else
          wavelength_index = -1;
        if (!m_params.sigma)
          sigma_index = nvalues++;
        else
          sigma_index = -1;
        if (!m_params.defocus)
          blur_index = nvalues++;
        else
          blur_index = -1;
      }
  }
  int
  num_values ()
  {
    return nvalues;
  }
  luminosity_t
  epsilon ()
  {
    return 0.00000001;
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
    if (sigma_index >= 0 && vals[sigma_index] < 0)
      vals[sigma_index] = 0;
    if (blur_index >= 0 && vals[blur_index] < 0)
      vals[blur_index] = 0;
    if (wavelength_index >= 0)
      vals[wavelength_index] = std::clamp (vals[wavelength_index],
                                           (luminosity_t)0, (luminosity_t)1);
  }
  luminosity_t
  get_wavelength (luminosity_t *vals)
  {
    if (wavelength_index < 0)
      return m_params.wavelength;
    return vals[wavelength_index] * (nanometers_max - nanometers_min)
           + nanometers_min;
  }
  luminosity_t
  get_sigma (luminosity_t *vals)
  {
    if (sigma_index < 0)
      return m_params.sigma;
    return vals[sigma_index];
  }
  luminosity_t
  get_defocus (luminosity_t *vals)
  {
    if (!difraction || blur_index < 0)
      return m_params.defocus;
    return vals[blur_index] * m_params.pixel_pitch * (1.0 / 1000);
  }
  luminosity_t
  get_blur_diameter (luminosity_t *vals)
  {
    if (difraction || blur_index < 0)
      return m_params.blur_diameter;
    return vals[blur_index];
  }
  luminosity_t
  objfunc (luminosity_t *vals)
  {
    luminosity_t sum = 0;
    mtf_parameters p = m_params;
    if (sigma_index >= 0)
      p.sigma = vals[sigma_index];
    if (wavelength_index >= 0)
      p.wavelength = get_wavelength (vals);
    if (blur_index >= 0)
      {
        if (difraction)
          p.defocus = vals[blur_index] * p.pixel_pitch * (1.0 / 1000);
        else
          p.blur_diameter = vals[blur_index];
      }
    for (size_t i = 0; i < m_measured_params.size (); i++)
      {
        luminosity_t freq = m_measured_params.get_freq (i);
        /* Do not care about values above Nyquist.  */
        if (freq > 0.5)
          continue;
        luminosity_t contrast = m_measured_params.get_contrast (i);
        luminosity_t contrast2 = p.system_mtf (freq) * 100;
        sum += (contrast - contrast2) * (contrast - contrast2);
        if (be_verbose)
          debug_data (freq, contrast, contrast2);
      }
    return sum;
  }
  const mtf_parameters &m_measured_params;
  mtf_parameters m_params;
  progress_info *m_progress;
  coord_t start[maxvals];
  bool difraction;
  int nvalues;
  int sigma_index;
  int wavelength_index;
  int blur_index;
};

/* Determine PSF kernel radius.  */
static int
get_psf_radius (double *psf, int size, bool *ok = NULL)
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
double
mtf_parameters::sensor_mtf(double pixel_freq) const
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
    return fabs (2.0 * std::cyl_bessel_j (1, Z) / Z);
  // return fabs (2.0 * j1 (Z) / Z);
  return 1;
}

double
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
  double j_term
      = (std::abs (B) < 1e-8) ? 1.0 : 2.0 * std::cyl_bessel_j (1, B) / B;

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
    return stokseth_defocus_mtf (pixel_freq) * gaussian_blur_mtf (pixel_freq, sigma);
  else
    return gaussian_blur_mtf (pixel_freq, sigma) * defocus_mtf (pixel_freq, blur_diameter);
}

/* Simulate system as a combination of sensor MTF and lens MTF.  */

luminosity_t
mtf_parameters::system_mtf (double pixel_freq) const
{
  return sensor_mtf (pixel_freq) * lens_mtf (pixel_freq);
}

/* Compute right half of LSF.  */

void
mtf::compute_lsf (std::vector <double,fftw_allocator<double>> &lsf, luminosity_t subsample) const
{
  int size = lsf.size ();
  if (size & 1)
  {
    lsf[size - 1] = 0;
    size --;
  }
  std::vector<double> mtf_half(size);
  double scale = 1/(size * subsample * 2);

  /* Mirror mtf.  */
  for (int i = 0; i < size; i++)
    mtf_half[i] = get_mtf (i * scale);

  fftw_plan plan = fftw_plan_r2r_1d(size, mtf_half.data(), lsf.data(),
                                    FFTW_REDFT00, FFTW_ESTIMATE);
  fftw_execute(plan);
  double sum = 0;
  for (int i = 0; i < size; i++)
    sum += lsf[i];
  printf ("sum: %f %i\n", sum, size);
  double fin_scale = 1.0 / sum;
  for (int i = 0; i < size; i++)
    lsf[i] *= fin_scale;
  fftw_destroy_plan (plan);
}

/* Compute PSF as 2D FFT of circular MTF.
   MAX_RADIUS is an estimate of radius.  SUBSCALE is a size of
   pixel we compute at (smaller pixel means more precise PSF)  */
bool
mtf::compute_psf (int max_radius, luminosity_t subscale, const char *filename,
                  const char **error)
{
  bool verbose = false;
  int psf_size = ceil (max_radius / subscale) * 2 + 1;
  int iterations = 0;

  while (true)
    {
      int fft_size = psf_size / 2 + 1;
      const double psf_step = 1 / (psf_size * subscale);
      std::vector<fftw_complex> mtf_kernel (psf_size * fft_size);
      for (int y = 0; y < fft_size; y++)
        for (int x = 0; x < fft_size; x++)
          {
            std::complex ker (std::clamp (get_mtf (x, y, psf_step),
                                          (luminosity_t)0, (luminosity_t)1),
                              (luminosity_t)0);
            mtf_kernel[y * fft_size + x][0] = real (ker);
            mtf_kernel[y * fft_size + x][1] = imag (ker);
            if (y)
              {
                mtf_kernel[(psf_size - y) * fft_size + x][0] = real (ker);
                mtf_kernel[(psf_size - y) * fft_size + x][1] = imag (ker);
              }
          }
      std::vector<double,fftw_allocator<double>> psf_data (psf_size * psf_size);
      fftw_lock.lock ();
      fftw_plan plan
          = fftw_plan_dft_c2r_2d (psf_size, psf_size, mtf_kernel.data (),
                                  psf_data.data (), FFTW_ESTIMATE);
      fftw_lock.unlock ();
      fftw_execute (plan);
      fftw_lock.lock ();
      fftw_destroy_plan (plan);
      fftw_lock.unlock ();

      /* Determine PSF radius.  */

      bool ok;
      int radius = get_psf_radius (psf_data.data (), psf_size, &ok);
      /* If FFT size was to small for the PSF, increase it and restart.  */
      if (!ok && iterations < 10)
        {
          if (psf_size < 4096)
            psf_size *= 2;
          else
            subscale /= 2;
          iterations++;
          continue;
        }
      m_psf_radius = radius * subscale;
      // printf ("psf radius %i %f\n", radius, m_psf_radius);

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
                  int xx = ((x + psf_size / 2 - radius) + psf_size / 2) % psf_size;
                  int yy = ((y + psf_size / 2 - radius)+ psf_size / 2) % psf_size;
                  int v = std::clamp (
                      (int)(invert_gamma (psf_data[yy * psf_size + xx] / m, -1)
                                * (65535)
                            + 0.5),
                      0, 65535);
                  int vv = std::clamp (v + 100 * 256 * ((xp + yp) % 2), 0, 65535);
                  renderedu.put_pixel (x, v, v, vv);
                }
              if (!renderedu.write_row ())
                return false;
            }
	  std::vector<double,fftw_allocator<double>> lsf(radius);
	  mtf::compute_lsf (lsf, subscale);
	  double lsf_max = lsf[0], collected_lsf_max = 0;
	  std::vector<double,fftw_allocator<double>> collected_lsf(width);
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
	          int idx = lsf_size - nearest_int (lsf[(x < radius ? radius - x - 1 : x - radius)] * lsf_size / lsf_max);
	          int idx2 = lsf_size - nearest_int (psf_data[((x + psf_size / 2 - radius) + psf_size / 2) % psf_size] * lsf_size / m);
	          int idx3 = lsf_size - nearest_int (collected_lsf[x] * lsf_size / collected_lsf_max);
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
mtf::precompute (progress_info *progress, const char *filename,
                 const char **error)
{
  m_lock.lock ();
  if (m_precomputed)
    {
      m_lock.unlock ();
      return true;
    }

  /* Determine sigma of data.  Used only for mtf measurements with
     too few data points.  */
  if (size () < 10)
    m_params.estimate_parameters (m_params, NULL, progress);

  /* If there seeems enough data point, use actual MTF data.  */
  if (size () >= 10)
    {
      bool monotone = true;
      for (size_t i = 1; i < size () && monotone; i++)
        {
          if (!(get_freq (i - 1) < get_freq (i)))
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
          = (get_freq (size () - 1) - get_freq (0)) / (size () - 1);
      for (size_t i = 1; i < size () - 2 && regular_steps; i++)
        if (fabs (get_freq (i) - get_freq (0) - i * step) > 0.0006)
          regular_steps = false;

      /* TODO: Implement.  */
      if (!regular_steps)
        {
          std::vector<luminosity_t,fftw_allocator<double>> contrasts (size () + 2);
          std::vector<luminosity_t,fftw_allocator<double>> freqs (size () + 2);
          for (size_t i = 0; i < size (); i++)
	  {
            contrasts[i]
                = get_contrast (i) * 0.01 * m_params.measured_mtf_correction (get_freq (i));
	    freqs[i] = get_freq (i);
	  }
          contrasts[size ()] = 0;
          contrasts[size () + 1] = 0;
	  freqs[size ()] = freqs[size()-1] + step;
	  freqs[size () + 1] = freqs[size()-1] + 2 *step;
          m_mtf.set_range (get_freq (0), get_freq (size () - 1) + 2 * step);
          m_mtf.init_by_x_y_values (freqs.data (), contrasts.data (), size () + 2, 1024);
        }
      else
        {
          std::vector<luminosity_t,fftw_allocator<double>> contrasts (size () + 2);
          for (size_t i = 0; i < size (); i++)
            contrasts[i]
                = get_contrast (i) * 0.01 * m_params.measured_mtf_correction (get_freq (i));
          /* Be sure that MTF trails in 0.  */
          contrasts[size ()] = 0;
          contrasts[size () + 1] = 0;
          m_mtf.set_range (get_freq (0), get_freq (size () - 1) + 2 * step);
          m_mtf.init_by_y_values (contrasts.data (), size () + 2);
        }
      if (colorscreen_checking)
        for (size_t i = 0; i < size (); i++)
          {
            luminosity_t freq = get_freq (i);
            if (fabs (get_contrast (i) * 0.01
                          * m_params.measured_mtf_correction (freq)
                      - get_mtf (freq))
                > 0.01)
            {
              printf ("Mismatch (measured) %i freq %f table %f precomputed %f\n",
		      (int)i, freq,
		      get_contrast (i) * 0.01 * m_params.measured_mtf_correction (freq),
                      m_mtf.apply (freq));
              abort ();
            }
          }

      if (progress)
	progress->set_task ("computing point spread function", 1);
      compute_psf (128, 1 / 32.0, filename, error);
    }
  /* Use lens model.  */
  else
    {
      const int entries = 512;
      std::vector<luminosity_t,fftw_allocator<double>> contrasts (entries);
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
              printf ("Mismatch (model) %f %f\n", m_params.system_mtf (i / 1000.),
                      m_mtf.apply (i / 1000.0));
              abort ();
            }

      /* FIXME: For some reason this still gives too small kernels.  */
      for (radius = 0;
           calculate_system_lsf (radius, m_params.sigma) > /*0.0001*/ 0.000001;
           radius++)
        ;
      radius++;
      if (radius < 3)
        radius = 3;
#if 0
      printf ("Estimated radius for sigma %f: %i\n", m_sigma, radius);
#endif
      if (progress)
	progress->set_task ("computing point spread function", 1);
      if (!compute_psf (radius, 1 / 32.0, filename, error))
        return false;
#if 0
      printf ("Final radius: %i\n", psf_radius (1));
#endif
    }
  // print_lsf (stdout);

  //m_mtf.plot (0, 1);
  //m_psf.plot (0, 5);
  m_precomputed = true;
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
get_new_mtf (struct mtf_parameters &p, progress_info *progress)
{
  return new mtf (p);
}

static lru_cache<mtf_parameters, mtf, mtf *, get_new_mtf, 10>
    mtf_cache ("Modulation transfer functions");

mtf *
mtf::get_mtf (const mtf_parameters &mtfp, progress_info *p)
{
  return mtf_cache.get (const_cast<mtf_parameters &> (mtfp), p);
}

void
mtf::release_mtf (mtf *m)
{
  mtf_cache.release (m);
}

bool
mtf_parameters::save_psf (progress_info *progress, const char *write_table,
                          const char **error) const
{
  mtf mtf (*this);
  return mtf.precompute (progress, write_table, error);
}

bool
mtf_parameters::print_csv_header (FILE *f) const
{
  return fprintf (f, "difraction f-stop 0/%.0f (effective 0/%.0f) wavelength %f.0nm magnification %f pixel pitch %f	defocus %.5fmm	"
                   "sigma=%.2fpx	lens	sensor fill factor %.1f	estimated\n",
                   f_stop, effective_f_stop (), wavelength, pixel_pitch / (25400.0 / scan_dpi), pixel_pitch, defocus, sigma, sensor_fill_factor) >= 0;
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
          if (fprintf (
                  f,
                  "%1.3f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f\n",
                  freq, lens_difraction_mtf (freq) * 100,
                  stokseth_defocus_mtf (freq) * 100,
                  gaussian_blur_mtf (freq, sigma) * 100, lens_mtf (freq) * 100,
                  sensor_mtf (freq) * 100, system_mtf (freq) * 100)
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

luminosity_t
mtf_parameters::estimate_parameters (const mtf_parameters &par,
                                     const char *write_table,
                                     progress_info *progress,
                                     const char **error,
				     bool verbose)
{
  *this = par;
  clear_data ();

  mtf_solver s (*this, par, progress);
  simplex<luminosity_t, mtf_solver> (s, "optimizing lens parameters",
                                     progress);
  wavelength = s.get_wavelength (s.start);
  sigma = s.get_sigma (s.start);
  defocus = s.get_defocus (s.start);
  blur_diameter = s.get_blur_diameter (s.start);
  clear_data ();

  if (write_table)
    {
      FILE *f = fopen (write_table, "wt");
      if (!f)
        {
          *error = "failed to open CSV file for writting";
          return -1;
        }
      if (fprintf ( f, "frequency	measured MTF	") < 0
	  || !print_csv_header (f))
        {
          *error = "write error in CSV file";
          return -1;
        }
      for (size_t i = 0; i < par.size (); i++)
        {
          luminosity_t freq = par.get_freq (i);
          luminosity_t c = par.get_contrast (i);
          if (fprintf (
                  f,
                  "%1.3f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f	%2.2f\n",
                  freq, c, lens_difraction_mtf (freq) * 100,
                  stokseth_defocus_mtf (freq) * 100,
                  gaussian_blur_mtf (freq, sigma) * 100, lens_mtf (freq) * 100,
                  sensor_mtf (freq) * 100, system_mtf (freq) * 100)
              < 0)
            {
              *error = "write error in CSV file";
              return -1;
            }
        }
      if (fclose (f))
        {
          *error = "error closing CSV file";
          return -1;
        }
    }

  if (mtf_solver::be_verbose)
    {
      for (size_t i = 0; i < par.size (); i++)
        {
          luminosity_t freq = par.get_freq (i);
          luminosity_t v1 = par.get_contrast (i);
          luminosity_t v2 = system_mtf (freq) * 100;
          debug_data (freq, v1, v2);
        }
    }
  return s.objfunc (s.start);
}

}
