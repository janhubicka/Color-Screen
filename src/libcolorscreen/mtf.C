#include <complex>
#include "mtf.h"
namespace colorscreen
{

/* Determine PSF kernel radius.  */
static int
get_psf_radius (double *psf, int size)
{
  double peak = 0;
  for (int i = 0; i < size; i++)
    {
      //iprintf ("%i %f\n", i, psf[i]);
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
    printf ("Psf size is too large; last ratio %f\n", psf[size / 2 - 1] / peak);
  return this_psf_radius;
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

  bool monotone = true;
  for (size_t i = 1; i < size () && monotone; i++)
    {
      if (!(get_freq (i - 1) < get_freq (i)))
        {
	  printf ("Sorry, unimplemented: MTF freqs are not in increasing order.\n");
	  monotone = false;
	  abort ();
        }
    }

  /* See if the MTF table has regular steps. In this case we can initialize
     precomputed function exactly.  */
  bool regular_steps = true;
  luminosity_t step = (get_freq(size ()-1) - get_freq(0)) / (size () - 1);
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
      for (size_t i = 0; i < size(); i++)
	contrasts[i] = get_contrast (i) * 0.01;
      /* Be sure that MTF trails in 0.  */
      contrasts[size ()] = 0;
      contrasts[size ()+1] = 0;
      m_mtf.set_range (get_freq (0), get_freq (size () - 1) + 2 * step);
      m_mtf.init_by_y_values (contrasts.data (), size () + 2);
    }

  /* Now compute PSF as 2D FFT of circular MTF.  */

  const luminosity_t subscale = 1 / 32.0;
  const int psf_size = 4096;
  int fft_size = psf_size / 2 + 1;
  const double psf_step = 1 / (psf_size * subscale);
  std::vector<fftw_complex> mtf_kernel (psf_size * fft_size);
  for (int y = 0; y < fft_size; y++)
    for (int x = 0; x < fft_size; x++)
      {
	std::complex ker (
	    std::clamp (get_mtf (x, y, psf_step),
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
  std::vector <double> psf_data (psf_size * psf_size);
  fftw_lock.lock ();
  fftw_plan plan
      = fftw_plan_dft_c2r_2d (psf_size, psf_size, mtf_kernel.data (), psf_data.data (), FFTW_ESTIMATE);
  fftw_lock.unlock ();
  fftw_execute (plan);
  fftw_lock.lock ();
  fftw_destroy_plan (plan);
  fftw_lock.unlock ();

  /* Determine PSF radius.  */

  int radius = get_psf_radius (psf_data.data (), psf_size);
  m_psf_radius = radius * subscale;
  printf ("psf radius %i %f\n", radius, m_psf_radius);

  /* Compute LSF. Circular LSF is PSF.  */
  /* Make sure PSF also trails by 0.  */
  psf_data [radius] = 0;
  psf_data [radius + 1] = 0;
  m_lsf.set_range (0, (radius + 2) * subscale);
  m_lsf.init_by_y_values (psf_data.data (), radius + 2);

  m_lock.unlock ();
  return true;
}
}
