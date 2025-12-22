#include "deconvolute.h"
#include <complex>
namespace colorscreen
{

/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fftw_lock;

deconvolution::deconvolution (precomputed_function<luminosity_t> *mtf, luminosity_t snr,
                              int max_threads, bool sharpen)
    : m_border_size (256), m_tile_size (4 * 256), m_blur_kernel (NULL)
{
  double k_const = 1.0f / snr;

  m_plans.resize (max_threads);
  for (int i = 0; i < max_threads; i++)
    m_plans[i].initialized = false;
  /* Result of real fft is symmetric.  We need only N /2 + 1 complex values.
     Moreover point spread functions we compute are symmetric real functions so
     the FFT result is again a real function (all complex values should be 0 up
     to roundoff errors).  */
  m_fft_size = m_tile_size / 2 + 1;
  m_blur_kernel = new fftw_complex[m_tile_size * m_fft_size];
  double scale = 1.0 / (m_tile_size * m_tile_size);
  double rev_tile_size = 1 / (double)m_tile_size;
  for (int y = 0; y < m_fft_size; y++)
    for (int x = 0; x < m_fft_size; x++)
      {
        std::complex ker ((deconvolution_data_t)mtf->apply (
                              sqrt (x * x + y * y) * rev_tile_size),
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
            m_blur_kernel[(m_tile_size - y) * m_fft_size + x][0] = real (ker);
            m_blur_kernel[(m_tile_size - y) * m_fft_size + x][1] = imag (ker);
          }
      }
}

/* Allocate memory for tiles and initialize fftw plans for given thread.  */
void
deconvolution::init (int thread_id)
{
  if (m_plans[thread_id].initialized)
    return;
  fftw_lock.lock ();
  m_plans[thread_id].in = new fftw_complex[m_tile_size * m_fft_size];
  m_plans[thread_id].tile.resize (m_tile_size * m_tile_size);
  m_plans[thread_id].plan_2d_inv
      = fftw_plan_dft_c2r_2d (m_tile_size, m_tile_size, m_plans[thread_id].in,
                              m_plans[thread_id].tile.data (), FFTW_ESTIMATE);
  m_plans[thread_id].plan_2d = fftw_plan_dft_r2c_2d (
      m_tile_size, m_tile_size, m_plans[thread_id].tile.data (),
      m_plans[thread_id].in, FFTW_ESTIMATE);
  m_plans[thread_id].initialized = true;
  fftw_lock.unlock ();
}

/* Apply the kernel.  */
void
deconvolution::process_tile (int thread_id)
{
  fftw_execute (m_plans[thread_id].plan_2d);
  fftw_complex *in = m_plans[thread_id].in;
  for (int i = 0; i < m_fft_size * m_tile_size; i++)
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
