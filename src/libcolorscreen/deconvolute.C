#include "deconvolute.h"
#include <complex>
namespace colorscreen
{

static const bool mirror = false;
static const bool taper_edges = true;
static const int max_border_size = 523;

/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fftw_lock;

/* Turn MTF to PSF scaled by SCALE.
   PSF is an array of SIZE.  */
void
mtf_to_2d_psf (precomputed_function<luminosity_t> *mtf,
	       double scale,
	       int size,
	       double *psf)
{
  int fft_size = size / 2 + 1;
  double step = scale / size;
  std::vector<fftw_complex> mtf_kernel (size * fft_size);
  for (int y = 0; y < fft_size; y++)
    for (int x = 0; x < fft_size; x++)
      {
	std::complex ker (
	    std::clamp (mtf->apply (sqrt (x * x + y * y) * step),
			(luminosity_t)0, (luminosity_t)1),
	    (luminosity_t)0);
	mtf_kernel[y * fft_size + x][0] = real (ker);
	mtf_kernel[y * fft_size + x][1] = imag (ker);
	if (y)
	  {
	    mtf_kernel[(size - y) * fft_size + x][0] = real (ker);
	    mtf_kernel[(size - y) * fft_size + x][1] = imag (ker);
	  }
      }
#if 0
  if (mtf_kernel [fft_size - 1][0])
    {
      printf ("MTF size is too large (max size %i, value at max %f, scale %f)\n", fft_size - 1, mtf_kernel [fft_size -1 ][0], scale);
      abort ();
    }
#endif
  fftw_lock.lock ();
  fftw_plan plan
      = fftw_plan_dft_c2r_2d (size, size, mtf_kernel.data (), psf, FFTW_ESTIMATE);
  fftw_lock.unlock ();
  fftw_execute (plan);
  fftw_lock.lock ();
  fftw_destroy_plan (plan);
  fftw_lock.unlock ();
}

/* Determine PSF kernel radius.  */
int
get_psf_radius (double *psf, int size)
{
  double peak = 0;
  for (int i = 0; i < size; i++)
    {
      //printf ("%i %f\n", i, psf[i]);
      if (psf[i] > peak)
	peak = psf[i];
    }
  int this_psf_radius = 0;
  /* Center of the PSF kernel is at 0  */
  for (int i = 1; i < size / 2 - 1; i++)
    if (psf[i] > peak * 0.0001f)
      this_psf_radius = i;
  if (this_psf_radius == size / 2 - 2)
    printf ("Psf size is too large; last ratio %f\n", psf[size / 2 - 1] / peak);
  return this_psf_radius;
}


/* Determine border to be added to tiles when doing deconvolution.
   This corresponds to the PSF kernel size.  */
int
deconvolute_border_size (precomputed_function<luminosity_t> *mtf)
{
  const int psf_size = max_border_size * 2;
#if 0
  const int fft_size = psf_size / 2 + 1;
  fftw_complex mtf_kernel[fft_size];
  double psf[psf_size];

  fftw_lock.lock ();
  fftw_plan plan
      = fftw_plan_dft_c2r_1d (psf_size, mtf_kernel, psf, FFTW_ESTIMATE);
  fftw_lock.unlock ();

  for (int x = 0; x < fft_size; x++)
    {
      mtf_kernel[x][0] = mtf->apply (x * (1.0 / psf_size));
      mtf_kernel[x][1] = 0;
      //printf ("mtf %i %f\n", x, mtf_kernel[x][0]);
    }

  fftw_execute (plan);

  double peak = 0;
  for (int i = 0; i < psf_size; i++)
    {
      //printf ("%i %f\n", i, psf[i]);
      if (psf[i] > peak)
        peak = psf[i];
    }
  int rr = 0;
  /* Center of the PSF kernel is at 0  */
  for (int i = 1; i < psf_size / 2 - 1; i++)
    if (psf[i] > peak * 0.001f)
      rr = i;

  fftw_lock.lock ();
  fftw_destroy_plan (plan);
  fftw_lock.unlock ();
  return rr;
  //printf ("Radius %i\n", rr);
#endif
  std::vector <double> psf (psf_size * psf_size);
  mtf_to_2d_psf (mtf, 1, psf_size, psf.data ());
  return get_psf_radius (psf.data (), psf_size);
}

deconvolution::deconvolution (precomputed_function<luminosity_t> *mtf, luminosity_t snr,
                              int max_threads, enum mode mode, int iterations)
    : m_border_size (0),
      m_taper_size (0),
      m_tile_size (1),
      m_mem_tile_size (1),
      m_blur_kernel (NULL),
      m_snr (snr),
      m_iterations (iterations),
      m_plans_exists (false)
{
  deconvolution_data_t k_const = 1.0f / snr;
  m_border_size = deconvolute_border_size (mtf);
  if (taper_edges)
    {
      m_taper_size = m_border_size;
      m_border_size *= 2;
    }

  while (m_tile_size < m_border_size * 4)
    m_tile_size *= 2;

  m_mem_tile_size = m_tile_size * (mirror ? 2 : 1);

  m_data.resize (max_threads);
  for (int i = 0; i < max_threads; i++)
    m_data[i].initialized = false;
  /* Result of real fft is symmetric.  We need only N /2 + 1 complex values.
     Moreover point spread functions we compute are symmetric real functions so
     the FFT result is again a real function (all complex values should be 0 up
     to roundoff errors).  */
  m_fft_size = m_mem_tile_size / 2 + 1;
  m_blur_kernel = new fftw_complex[m_mem_tile_size * m_fft_size];
  deconvolution_data_t scale = 1.0 / (m_mem_tile_size * m_mem_tile_size);
  deconvolution_data_t rev_tile_size = 1 / (deconvolution_data_t)m_mem_tile_size;
  for (int y = 0; y < m_fft_size; y++)
    for (int x = 0; x < m_fft_size; x++)
      {
        std::complex ker (std::clamp ((deconvolution_data_t)mtf->apply (
				      sqrt (x * x + y * y) * rev_tile_size), (deconvolution_data_t)0, (deconvolution_data_t)1),
                          (deconvolution_data_t)0);

        // If sharpening, apply Wiener Filter
        // Result = Img * conj(Ker) / (|Ker|^2 + 1/SNR)
        if (mode == sharpen)
          ker = conj (ker) / (std::norm (ker) + k_const);
	  //ker = ((deconvolution_data_t)1)/ker;
	if (mode != richardson_lucy_sharpen)
          ker = ker * scale;
        m_blur_kernel[y * m_fft_size + x][0] = real (ker);
        m_blur_kernel[y * m_fft_size + x][1] = imag (ker);
        if (y)
          {
            m_blur_kernel[(m_mem_tile_size - y) * m_fft_size + x][0] = real (ker);
            m_blur_kernel[(m_mem_tile_size - y) * m_fft_size + x][1] = imag (ker);
          }
      }
  m_richardson_lucy = mode == richardson_lucy_sharpen;
  if (taper_edges)
    {
      m_weights.resize (m_taper_size);
      for (int i = 0; i < m_taper_size; i++)
	// Cosine bell curve: 0.0 at edge, 1.0 at taper_width
	m_weights[i] = 0.5f * (1.0f - cosf(M_PI * i / m_taper_size));
    }
}

/* Allocate memory for tiles and initialize fftw plans for given thread.  */
void
deconvolution::init (int thread_id)
{
  if (m_data[thread_id].initialized)
    return;
  m_data[thread_id].in = new fftw_complex[m_mem_tile_size * m_fft_size];
  m_data[thread_id].tile.resize (m_mem_tile_size * m_mem_tile_size);
  if (m_richardson_lucy)
    m_data[thread_id].ratios.resize (m_mem_tile_size * m_mem_tile_size);
  m_data[thread_id].initialized = true;
  if (!m_plans_exists)
    {
      fftw_lock.lock ();
      if (m_plans_exists)
        {
          fftw_lock.unlock ();
	  return;
        }
      m_plan_2d_inv
	  = fftw_plan_dft_c2r_2d (m_mem_tile_size, m_mem_tile_size, m_data[thread_id].in,
				  m_data[thread_id].tile.data (), FFTW_ESTIMATE);
      m_plan_2d = fftw_plan_dft_r2c_2d (
	  m_mem_tile_size, m_mem_tile_size, m_data[thread_id].tile.data (),
	  m_data[thread_id].in, FFTW_ESTIMATE);
      fftw_lock.unlock ();
    }
}

/* Apply the kernel.  */
void
deconvolution::process_tile (int thread_id)
{
  if (taper_edges)
    {
      deconvolution_data_t sum = 0;

      /* Compute average pixel */
      for (int y = 0; y < m_taper_size; y++)
        for (int x = 0; x < m_tile_size; x++)
	  sum += get_pixel (thread_id, x, y);
      for (int y = 0; y < m_taper_size; y++)
        for (int x = 0; x < m_tile_size; x++)
	  sum += get_pixel (thread_id, x, y + m_tile_size - m_taper_size);
      for (int y = m_taper_size; y < m_tile_size - m_taper_size; y++)
	{
	  for (int x = 0; x < m_taper_size; x++)
	    sum += get_pixel (thread_id, x, y);
	  for (int x = 0; x < m_taper_size; x++)
	    sum += get_pixel (thread_id, x + m_tile_size - m_taper_size, y);
	}
      sum /= m_tile_size * m_taper_size * 2 + (m_tile_size - 2 * m_taper_size) * m_taper_size * 2;
      /* Taper top edge  */
      for (int y = 0; y < m_taper_size; y++)
	{
	  float weight = m_weights[y];
	  for (int x = 0; x < y; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[x]);
	  for (int x = y; x < m_tile_size - y; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * weight);
	  for (int x = m_tile_size - y; x < m_tile_size; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[m_tile_size - 1 - x]);
	}
      /* Taper left and right edge  */
      for (int y = m_taper_size; y < m_tile_size - m_taper_size; y++)
        {
	  for (int x = 0; x < m_taper_size; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[x]);
	  for (int x = m_tile_size - m_taper_size; x < m_tile_size; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[m_tile_size - 1 - x]);
        }
      /* Taper bottom edge  */
      for (int y = m_tile_size - m_taper_size; y < m_tile_size; y++)
	{
	  int d = m_tile_size - 1 - y;
	  float weight = m_weights[d];
	  for (int x = 0; x < d; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[x]);
	  for (int x = d; x < m_tile_size - d; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * weight);
	  for (int x = m_tile_size - d; x < m_tile_size; x++)
	    put_pixel (thread_id, x, y, sum + (get_pixel (thread_id, x, y) - sum) * m_weights[m_tile_size - 1 - x]);
	}
    }
  if (mirror)
    {
      for (int y = 0; y < m_tile_size; y++)
        for (int x = 0; x < m_tile_size; x++)
          {
	    deconvolution_data_t p = get_pixel (thread_id, x, y);
	    put_pixel (thread_id, m_mem_tile_size - 1 - x, y, p);
	    put_pixel (thread_id, x, m_mem_tile_size - 1 - y, p);
	    put_pixel (thread_id, m_mem_tile_size - 1 - x, m_mem_tile_size - 1 - y, p);
          }
    }

  if (!m_richardson_lucy)
    {
      fftw_complex *in = m_data[thread_id].in;
      fftw_execute_dft_r2c (m_plan_2d, m_data[thread_id].tile.data (), in);
      for (int i = 0; i < m_fft_size * m_mem_tile_size; i++)
	{
	  std::complex w (m_blur_kernel[i][0], m_blur_kernel[i][1]);
	  std::complex v (in[i][0], in[i][1]);
	  in[i][0] = real (v * w);
	  in[i][1] = imag (v * w);
	}
      fftw_execute_dft_c2r (m_plan_2d_inv, in, m_data[thread_id].tile.data ());
    }
  else
    {
      std::vector<deconvolution_data_t> observed = m_data[thread_id].tile;
      std::vector<deconvolution_data_t> &estimate = m_data[thread_id].tile;
      std::vector<deconvolution_data_t> &ratios = m_data[thread_id].ratios;
      /* TODO: We can pre-scale blur_kernel just as we do for normal bluring.  */
      deconvolution_data_t scale = 1.0 / (m_mem_tile_size * m_mem_tile_size);
      fftw_complex *in = m_data[thread_id].in;
      deconvolution_data_t sigma = m_snr ? 1.0f / m_snr : 1;
      for (int iteration = 0; iteration < m_iterations; iteration++)
        {
	  /* Step A: Re-blur the current estimate.  */

	  /* Blur current estimate to IN.  */
          fftw_execute_dft_r2c (m_plan_2d, estimate.data (), in);
	  for (int i = 0; i < m_fft_size * m_mem_tile_size; i++)
	    {
	      std::complex w (m_blur_kernel[i][0], m_blur_kernel[i][1]);
	      std::complex v (in[i][0], in[i][1]);
	      in[i][0] = real (v * w);
	      in[i][1] = imag (v * w);
	    }
	  fftw_execute_dft_c2r (m_plan_2d_inv, in, ratios.data ());

	  /* Step B: ratio = observed / (re-blurred + epsilon)  */

	  deconvolution_data_t epsilon = 1e-12 /*1e-7 for float*/;
	  printf ("sigma :%f\n",sigma);

	  /* RATIOS is now blurred ESTIMATE; compute ratios  */
	  if (sigma != 1)
	    for (int i = 0; i < m_mem_tile_size * m_mem_tile_size; i++)
	      {
		deconvolution_data_t reblurred = ratios[i] * scale;
		deconvolution_data_t diff = observed[i] - reblurred;
		if (reblurred > epsilon && std::abs (diff) > 2 * sigma)
		  ratios[i] = 1.0 + (reblurred * diff) / (reblurred * reblurred + sigma * sigma);
		else
		  ratios[i] = 1.0;
	      }
	  else
	    for (int i = 0; i < m_mem_tile_size * m_mem_tile_size; i++)
	      {
		deconvolution_data_t reblurred = ratios[i] * scale;
		if (reblurred > epsilon)
		  ratios[i] = observed[i] / reblurred;
		else
		  ratios[i] = 1.0;
	      }


	  /* Step C: Update estimate
	     FFT(ratio) -> multiply by FFT(PSF_flipped) -> IFFT
	     estimate = estimate * result_of_Step_C  */

	  /* Do FFT of ratio */
          fftw_execute_dft_r2c (m_plan_2d, ratios.data (), in);
	  /* Scale by complex conjugate of blur kernel  */
	  for (int i = 0; i < m_fft_size * m_mem_tile_size; i++)
	    {
	      std::complex w (m_blur_kernel[i][0], -m_blur_kernel[i][1]);
	      std::complex v (in[i][0], in[i][1]);
	      ///* Blurred kernel is pre-scalled taking into account the inverse FFT.  */
	      //w *= m_mem_tile_size * m_mem_tile_size;
	      in[i][0] = real (v * w);
	      in[i][1] = imag (v * w);
	    }
	  /* Now initialize ratios  */
	  fftw_execute_dft_c2r (m_plan_2d_inv, in, ratios.data ());

	  /* estimate = estimate * result_of_Step_C  */
	  for (int i = 0; i < m_mem_tile_size * m_mem_tile_size; i++)
	    estimate[i] *= ratios[i] * scale;
        }
    }
}

deconvolution::~deconvolution ()
{
  delete m_blur_kernel;
  for (size_t i = 0; i < m_data.size (); i++)
    if (m_data[i].initialized)
      delete (m_data[i].in);
  fftw_lock.lock ();
  if (m_plans_exists)
    {
      fftw_destroy_plan (m_plan_2d);
      fftw_destroy_plan (m_plan_2d_inv);
    }
  fftw_lock.unlock ();
}

}
