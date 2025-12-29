#include "deconvolute.h"
#include <complex>
namespace colorscreen
{

static const bool mirror = false;
static const bool taper_edges = true;
static const int max_border_size = 523;

/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fftw_lock;

/* Determine border to be added to tiles when doing deconvolution.
   This corresponds to the PSF kernel size.  */
int
deconvolute_border_size (precomputed_function<luminosity_t> *mtf)
{
  const int psf_size = max_border_size * 2;
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
  //printf ("Radius %i\n", rr);
  return rr;
}

deconvolution::deconvolution (precomputed_function<luminosity_t> *mtf, luminosity_t snr,
                              int max_threads, bool sharpen)
    : m_border_size (0),
      m_taper_size (0),
      m_tile_size (1),
      m_mem_tile_size (1),
      m_blur_kernel (NULL)
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

  m_plans.resize (max_threads);
  for (int i = 0; i < max_threads; i++)
    m_plans[i].initialized = false;
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
        if (sharpen)
          ker = conj (ker) / (std::norm (ker) + k_const);
	  //ker = ((deconvolution_data_t)1)/ker;
        ker = ker * scale;
        m_blur_kernel[y * m_fft_size + x][0] = real (ker);
        m_blur_kernel[y * m_fft_size + x][1] = imag (ker);
        if (y)
          {
            m_blur_kernel[(m_mem_tile_size - y) * m_fft_size + x][0] = real (ker);
            m_blur_kernel[(m_mem_tile_size - y) * m_fft_size + x][1] = imag (ker);
          }
      }
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
  if (m_plans[thread_id].initialized)
    return;
  fftw_lock.lock ();
  m_plans[thread_id].in = new fftw_complex[m_mem_tile_size * m_fft_size];
  m_plans[thread_id].tile.resize (m_mem_tile_size * m_mem_tile_size);
  m_plans[thread_id].plan_2d_inv
      = fftw_plan_dft_c2r_2d (m_mem_tile_size, m_mem_tile_size, m_plans[thread_id].in,
                              m_plans[thread_id].tile.data (), FFTW_ESTIMATE);
  m_plans[thread_id].plan_2d = fftw_plan_dft_r2c_2d (
      m_mem_tile_size, m_mem_tile_size, m_plans[thread_id].tile.data (),
      m_plans[thread_id].in, FFTW_ESTIMATE);
  m_plans[thread_id].initialized = true;
  fftw_lock.unlock ();
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

  fftw_execute (m_plans[thread_id].plan_2d);
  fftw_complex *in = m_plans[thread_id].in;
  for (int i = 0; i < m_fft_size * m_mem_tile_size; i++)
    {
      std::complex w (m_blur_kernel[i][0], m_blur_kernel[i][1]);
      std::complex v (in[i][0], in[i][1]);
      in[i][0] = real (v * w);
      in[i][1] = imag (v * w);
    }
  fftw_execute (m_plans[thread_id].plan_2d_inv);
}

deconvolution::~deconvolution ()
{
  delete m_blur_kernel;
  fftw_lock.lock ();
  for (size_t i = 0; i < m_plans.size (); i++)
    if (m_plans[i].initialized)
      {
        fftw_destroy_plan (m_plans[i].plan_2d);
        fftw_destroy_plan (m_plans[i].plan_2d_inv);
        delete (m_plans[i].in);
      }
  fftw_lock.unlock ();
}

}
