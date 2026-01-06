#include <cmath>
#include "mtf.h"
#include "nmsimplex.h"
#include <complex>
#include "include/tiff-writer.h"
#include "lru-cache.h"
namespace colorscreen
{

namespace
{
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

/* Return MTF of given blur circle diameter and given frequency */
double
defocus_mtf (double freq, double blur_circle_diameter)
{
  /* If perfectly in focus or frequency is almost 0, return 1  */
  if (blur_circle_diameter < 1e-9 || freq < 1e-6)
    return 1;

  /* The transfer function of a circular blur is 2*J1(x)/x  */
  double arg = M_PI * freq * blur_circle_diameter;
  return std::abs(2.0 * j1(arg) / arg);
}

/* Reutrn MTF of gaussian blur with a given sigma.  */
double
gaussian_blur_mtf (double freq, double sigma)
{
  return std::exp (-2.0 * M_PI * M_PI * sigma * sigma * freq * freq);
}

#if 0
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
  printf ("freq %1.2f measured %2.2f obtained %2.2f ", freq,
	  v1, v2);
  for (int j = 0; j < len+1; j++)
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
  mtf_solver (const mtf &mtf, const mtf_parameters &params, progress_info *progress)
      : m_mtf (mtf), m_params (params), m_progress (progress), start{ 0.5, 0.5, 0.5 }
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
    return false;
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
      vals [sigma_index] = 0;
    if (blur_index >= 0 && vals[blur_index] < 0)
      vals [blur_index] = 0;
    if (wavelength_index >= 0)
      vals [wavelength_index] = std::clamp (vals [wavelength_index], (luminosity_t)0, (luminosity_t)1);
  }
  luminosity_t
  get_wavelength (luminosity_t *vals)
  {
    if (wavelength_index < 0)
      return m_params.wavelength;
    return vals[wavelength_index] * (nanometers_max - nanometers_min) + nanometers_min;
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
    printf ("%f %f %f %f %f %i %i %i\n", vals[0],vals[1],vals[2], p.sigma, p.blur_diameter, sigma_index, blur_index, nvalues);
    for (size_t i = 0; i < m_mtf.size (); i++)
      {
        luminosity_t freq = m_mtf.get_freq (i);
        /* Do not care about values above Nyquist.  */
        if (freq > 0.5)
          continue;
        luminosity_t contrast = m_mtf.get_contrast (i);
        luminosity_t contrast2 = p.system_mtf (freq) * 100;
        sum += (contrast - contrast2) * (contrast - contrast2);
	debug_data (freq, contrast, contrast2);
      }
    return sum;
  }
  const mtf &m_mtf;
  mtf_parameters m_params;
  progress_info *m_progress;
  coord_t start[maxvals];
  bool difraction;
  int nvalues;
  int sigma_index;
  int wavelength_index;
  int blur_index;
};

luminosity_t
determine_lens (const mtf &mtf, mtf_parameters &params, progress_info *progress)
{
  mtf_solver s (mtf, params, progress);
  simplex<luminosity_t, mtf_solver> (s, "optimizing lens parameters", progress);
  params.wavelength = s.get_wavelength (s.start);
  params.sigma = s.get_sigma (s.start);
  params.defocus = s.get_defocus (s.start);
  params.blur_diameter = s.get_blur_diameter (s.start);
  params.clear_data ();

  if (1)
    {

      for (size_t i = 0; i < mtf.size (); i++)
	{
	  luminosity_t freq = mtf.get_freq (i);
	  luminosity_t v1 = mtf.get_contrast (i);
	  luminosity_t v2 = params.system_mtf (freq) * 100;
	  debug_data (freq, v1, v2);
	}
    }
  return s.objfunc (s.start);
}

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
#if 0
      printf ("Psf size is too large; last ratio %f\n",
	      psf[size / 2 - 1] / peak);
#endif
    }
  else if (ok)
    *ok = true;
  return this_psf_radius;
}

}

luminosity_t
mtf_parameters::lens_mtf (double pixel_freq)
{
  if (simulate_difraction_p ())
    {
      double freq = pixel_freq / (pixel_pitch * (1.0 / 1000));
      double wavelength_mm = wavelength / 1e6;
      double cutoff_freq = 1.0 / (wavelength_mm * f_stop);
      /* Normalize frequency (nu) */
      double s = freq / cutoff_freq;
      if (s >= 1.0) return 0.0;
      if (s <= 0.0) return 1.0* gaussian_blur_mtf (pixel_freq, blur_diameter);
      double mtf_diff = (2.0 / M_PI) * (std::acos(s) - s * sqrt(1.0 - s * s));
      //double blur_diameter = std::abs(defocus) / f_stop;
      //return mtf_diff * gaussian_blur_mtf (pixel_freq, sigma) * defocus_mtf (freq, blur_diameter);
     
      
    // 3. Calculate Wavefront Defocus (W020)
    // W020 = delta_z / (8 * f_number^2)
    double W020 = defocus / (8.0 * std::pow(f_stop, 2));

    // 4. Hopkins Defocus Factor
    // Z = (2 * pi / wavelength) * W020 * 4 * nu * (1 - nu)
    double Z = (2.0 * M_PI / wavelength_mm) * W020 * 4.0 * s * (1.0 - s);

    // 5. Apply the Defocus Transfer Function using Bessel J1
    // Handle the limit where Z -> 0 to avoid division by zero
    double defocus_factor = 1.0;
    if (std::abs(Z) > 1e-9) {
        defocus_factor = 2.0 * std::cyl_bessel_j(1, Z) / Z;
    }
    return mtf_diff * gaussian_blur_mtf (pixel_freq, sigma) * fabs (defocus_factor);
    }
  else
    return gaussian_blur_mtf (pixel_freq, sigma) * defocus_mtf (pixel_freq, blur_diameter);
}

luminosity_t
mtf_parameters::system_mtf (double pixel_freq)
{
  double sensor_mtf = std::abs (sinc (pixel_freq));
  return sensor_mtf * lens_mtf (pixel_freq);
}

/* Compute PSF as 2D FFT of circular MTF.
   MAX_RADIUS is an estimate of radius.  SUBSCALE is a size of
   pixel we compute at (smaller pixel means more precise PSF)  */
void
mtf::compute_psf (int max_radius, luminosity_t subscale)
{
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
      std::vector<double> psf_data (psf_size * psf_size);
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
      if (0)
	{
	  tiff_writer_params pp;
	  int width = psf_size;
	  int height = psf_size;
	  pp.width = width;
	  pp.height = height;
	  pp.depth = 16;
	  const char *error;
	  pp.filename = "/tmp/psf-big.tif";
	  tiff_writer renderedu (pp, &error);
	  luminosity_t err = 0, m =0;
	  for (int y = 0; y < psf_size / 2; y++)
	    for (int x = 0; x < psf_size / 2; x++)
	      {
		luminosity_t val = get_psf (x, y, 1/subscale);
		luminosity_t diff = fabs (val - psf_data[y * psf_size + x]);
		if (val > m)
		  m = val;
		if (diff > err)
		  err = diff;
		psf_data[y * psf_size + x] = val;
	      }
	  for (int y = 0; y < height; y++)
	    {
	      for (int x = 0; x < width; x++)
		{
		  int v = std::clamp ((int)(invert_gamma (psf_data [y * psf_size + x]/m, -1) * (65535) + 0.5), 0, 65535);
		  renderedu.put_pixel (x, v, v, v);
		}
	      if (!renderedu.write_row ())
		{
		  printf ("Write error line %i\n", y);
		  break;
		}
	    }
	  printf ("Max %f, err %f normalized %f\n", m, err, err/m);
	}
      return;
    }
}

bool
mtf::precompute (progress_info *progress)
{

  if (m_precomputed)
    return true;
  m_lock.lock ();
  if (m_precomputed)
    {
      m_lock.unlock ();
      return true;
    }
  m_precomputed = true;

  /* Determine sigma of data.  Used only for mtf measurements with
     too few data points.  */
  if (size ())
    {
      mtf_parameters params = m_params;
      luminosity_t sqsum = determine_lens (*this, params, progress);
      if (progress)
        progress->pause_stdout ();
      printf ("Estimation sqsum %f\n", sqsum);
      printf ("Estimated sigma %f px\n", params.sigma);
      if (params.simulate_difraction_p ())
	{
	  printf ("Estimated wavelength %f nm\n", params.wavelength);
	  printf ("Estimated defocus %f mm\n", params.defocus);
	}
      else
	printf ("Estimated blur diameter %f px\n", params.blur_diameter);
      if (progress)
        progress->resume_stdout ();
    }

  /* If there seeems enough data point, use actual MTF data.  */
  if (size () > 3)
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
          printf ("Sorry, unimplemented: MTF has irregular steps. \n");
          abort ();
        }
      else
        {
          std::vector<luminosity_t> contrasts (size () + 2);
          for (size_t i = 0; i < size (); i++)
            contrasts[i] = get_contrast (i) * 0.01 * m_params.lens_mtf (get_freq (i));
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
	    if (fabs (get_contrast (freq) * 0.01 * m_params.lens_mtf (get_freq (i)) - get_mtf (freq)) > 0.001)
	      abort ();
	  }
      compute_psf ();

    }
  /* Use lens model.  */
  else
    {
      const int entries = 512;
      std::vector<luminosity_t> contrasts (entries);
      luminosity_t step = 1.0 / (entries - 2);
      for (int i = 0; i < entries - 2; i++)
        contrasts[i] = m_params.system_mtf (i * step);
      contrasts[entries - 2] = contrasts[entries - 1] = 0;
      m_mtf.set_range (0, 1 + step);
      m_mtf.init_by_y_values (contrasts.data (), entries);
      int radius;

      if (colorscreen_checking)
	for (int i = 0; i < 1000; i++)
	  if (fabs (m_params.system_mtf (i / 1000.0) - m_mtf.apply (i / 1000.0)) > 0.001)
	    {
	      printf ("Mismatch %f %f\n", m_params.system_mtf (i / 1000.), m_mtf.apply (i / 1000.0));
	      abort ();
	    }

      /* FIXME: For some reason this still gives too small kernels.  */
      for (radius = 0; calculate_system_lsf (radius, m_params.sigma) > /*0.0001*/0.000001; radius++)
	;
      radius++;
      if (radius < 3)
        radius = 3;
#if 0
      printf ("Estimated radius for sigma %f: %i\n", m_sigma, radius);
#endif
      compute_psf (radius);
#if 0
      printf ("Final radius: %i\n", psf_radius (1));
#endif

#if 0
      const luminosity_t subscale = 1 / 32.0;
      int radius;
      for (radius = 1; calculate_system_lsf (radius, sigma) > 0.0001; radius++)
        ;
      m_psf_radius = radius;
      int size = radius / subscale + 2;
      m_lsf.set_range (0, radius + 2 * subscale);
      std::vector<luminosity_t> lsf (size);
      for (int i = 0; i < size - 2; i++)
        lsf[i] = calculate_system_lsf (i * subscale, sigma);
      lsf[size - 2] = lsf[size - 1] = 1;
      m_lsf.init_by_y_values (contrasts.data (), 256);
#endif
    }
  //print_lsf (stdout);

  m_mtf.plot (0, 1);
  m_psf.plot (0, 5);
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


mtf*
mtf::get_mtf (mtf_parameters &mtfp, progress_info *p)
{
  return mtf_cache.get (mtfp, p);
}

void
mtf::release_mtf (mtf *m)
{
  mtf_cache.release (m);
}
}
