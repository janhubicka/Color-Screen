#include "mtf.h"
#include "nmsimplex.h"
#include <complex>
#include "include/tiff-writer.h"
namespace colorscreen
{

namespace
{
// Normalized sinc function: sin(pi * x) / (pi * x)
// Used to model the sensor's pixel aperture effect.
double
sinc (double x, double lens_sigma)
{
  x = std::abs (x);
  if (x < 1e-6)
    return 1.0; /* Avoid division by zero (Taylor expansion limit)  */
  return std::sin (M_PI * x) / (M_PI * x);
}

/* Computes MTF at frequency f (cycles per unit distance)  */
double
system_mtf (double f, double lens_sigma)
{
  /* 1. Lens Component (Gaussian)
     MTF_lens = exp(-2 * pi^2 * sigma^2 * f^2)  */
  const double pi = 3.141592653589793;
  double lens_mtf
      = std::exp (-2.0 * pi * pi * lens_sigma * lens_sigma * f * f);

  /* 2. Sensor Component (Sinc)
     The aperture width is the pixel pitch d.
     MTF_sensor = |sinc(d * f)|  */
  double sensor_mtf = std::abs (sinc (/*pixel_pitch **/ f, lens_sigma));

  // 3. System MTF is the product
  return lens_mtf * sensor_mtf;
}

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

class mtf_solver
{
public:
  mtf_solver (const mtf &mtf, progress_info *progress)
      : m_mtf (mtf), m_progress (progress), start{ 1 }
  {
  }
  int
  num_values ()
  {
    return 1;
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
  }
  luminosity_t
  objfunc (luminosity_t *vals)
  {
    luminosity_t sigma = vals[0];
    luminosity_t sum = 0;
    for (size_t i = 0; i < m_mtf.size (); i++)
      {
        luminosity_t freq = m_mtf.get_freq (i);
        /* Do not care about values above Nyquist.  */
        if (freq > 0.5)
          continue;
        luminosity_t contrast = m_mtf.get_contrast (i) * 0.01;
        luminosity_t contrast2 = system_mtf (freq, sigma);
        sum += (contrast - contrast2) * (contrast - contrast2);
      }
    return sum;
  }
  const mtf &m_mtf;
  progress_info *m_progress;
  coord_t start[1];
};

luminosity_t
determine_sigma (const mtf &mtf, progress_info *progress)
{
  mtf_solver s (mtf, progress);
  simplex<luminosity_t, mtf_solver> (s, "optimizing lens sigma", progress);
  luminosity_t sigma = s.start[0];

  if (0)
    {
      printf ("Sigma %f\n", sigma);

      for (size_t i = 0; i < mtf.size (); i++)
	{
	  luminosity_t freq = mtf.get_freq (i);
	  printf ("freq %1.2f measured %1.3f obtained %1.3f\n", freq,
		  mtf.get_contrast (i), system_mtf (freq, sigma) * 100);
	}
    }
  return sigma;
}

/* Determine PSF kernel radius.  */
static int
get_psf_radius (double *psf, int size)
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
    printf ("Psf size is too large; last ratio %f\n",
            psf[size / 2 - 1] / peak);
  return this_psf_radius;
}

}

/* Compute PSF as 2D FFT of circular MTF.  */
void
mtf::compute_psf ()
{
  const luminosity_t subscale = 1 / 32.0;
  const int psf_size = 4096;
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

  int radius = get_psf_radius (psf_data.data (), psf_size);
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
  if (1)
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

  luminosity_t sigma = determine_sigma (*this, progress);

  /* Use actual MTF data.  */
  if (1)
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
            contrasts[i] = get_contrast (i) * 0.01;
          /* Be sure that MTF trails in 0.  */
          contrasts[size ()] = 0;
          contrasts[size () + 1] = 0;
          m_mtf.set_range (get_freq (0), get_freq (size () - 1) + 2 * step);
          m_mtf.init_by_y_values (contrasts.data (), size () + 2);
        }

    }
  /* Fit data into sensor + lens model and use it instead.  */
  else
    {
      std::vector<luminosity_t> contrasts (256);
      for (int i = 0; i < 254; i++)
        contrasts[i] = system_mtf (i * (0.5 / 253), sigma);
      contrasts[254] = contrasts[255] = 0;
      m_mtf.set_range (0, 0.5 + (1 / 253));
      m_mtf.init_by_y_values (contrasts.data (), 256);

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
  compute_psf ();
  //print_lsf (stdout);

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
}
