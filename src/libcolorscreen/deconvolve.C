#include "deconvolve.h"
#include <complex>
namespace colorscreen
{

/* Mirror duplicates the tile to get better periodicity. Seems unnecessary.  */
static const bool mirror = false;
/* Avoid sharp edges along end of the tile.  */
static const bool taper_edges = true;

/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fftw_lock;

deconvolution::deconvolution (mtf *mtf, luminosity_t mtf_scale, luminosity_t snr,
			      luminosity_t sigma, int max_threads, enum mode mode,
			      int iterations)
    : m_border_size (0),
      m_taper_size (0),
      m_tile_size (1),
      m_mem_tile_size (1),
      m_blur_kernel (NULL),
      m_snr (snr),
      m_sigma (sigma),
      m_iterations (iterations),
      m_plans_exists (false)
{
  mtf->precompute ();
  deconvolution_data_t k_const = 1.0f / snr;
  m_border_size = mtf->psf_radius (mtf_scale);
  //printf ("Border size %i scale %f\n", mtf->psf_size (mtf_scale), mtf_scale);
    //deconvolute_border_size (mtf);
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
  deconvolution_data_t rev_tile_size = mtf_scale / (deconvolution_data_t)m_mem_tile_size;
  for (int y = 0; y < m_fft_size; y++)
    for (int x = 0; x < m_fft_size; x++)
      {
        std::complex ker (std::clamp ((deconvolution_data_t)mtf->get_mtf (x, y, rev_tile_size),
				      (deconvolution_data_t)0, (deconvolution_data_t)1),
                          (deconvolution_data_t)0);

        // If sharpening, apply Wiener Filter
        // Result = Img * conj(Ker) / (|Ker|^2 + 1/SNR)
        if (mode == sharpen)
          ker = conj (ker) / (std::norm (ker) + k_const);
	  //ker = ((deconvolution_data_t)1)/ker;
	//if (mode != richardson_lucy_sharpen)
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
      deconvolution_data_t scale = 1;
      fftw_complex *in = m_data[thread_id].in;
      deconvolution_data_t sigma = m_sigma;
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
	  //printf ("sigma :%f\n",sigma);

	  /* RATIOS is now blurred ESTIMATE; compute ratios  */
	  if (sigma > 0)
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
