#include "deconvolute.h"
#include <complex>
namespace colorscreen
{

/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fftw_lock;

deconvolution::deconvolution (precomputed_function<luminosity_t> *mtf)
    : m_border_size (256), m_tile_size (4 * 256),
      m_blur_kernel (NULL)
{
  /* Result of real fft is symmetric.  We need only N /2 + 1 complex values.
     Moreover point spread functions we compute are symmetric real functions so
     the FFT result is again a real function (all complex values should be 0 up
     to roundoff errors).  */
  m_fft_size = m_tile_size / 2 + 1;
  m_blur_kernel = new fftw_complex[m_tile_size * m_fft_size];
  double scale = 1.0 / (m_tile_size * m_tile_size);
  for (int y = 0; y < m_fft_size; y++)
    for (int x = 0; x < m_fft_size; x++)
      {
        //deconvolution_data_t w = x > 0 || y > 0 ? mtf->apply (1 / sqrt (x * x + y * y)) * scale : scale;
        deconvolution_data_t w = mtf->apply (sqrt (x * x + y * y) / m_tile_size) * scale;
	//if (w < 0.01 * scale)
	  //w = 0.01 * scale;
        m_blur_kernel[y * m_fft_size + x][0] = w;
        m_blur_kernel[y * m_fft_size + x][1] = 0;
        if (y)
          {
            m_blur_kernel[(m_tile_size - y) * m_fft_size + x][0] = w;
            m_blur_kernel[(m_tile_size - y) * m_fft_size + x][1] = 0;
          }
      }
}

void
deconvolution::blur_tile (deconvolution_data_t *d)
{
  // std::vector<deconvolution_data_t> out(m_tile_size * m_tile_size);
  fftw_plan plan_2d_inv, plan_2d;
  std::vector<fftw_complex> in (m_tile_size * m_fft_size);
  fftw_lock.lock ();
  plan_2d_inv = fftw_plan_dft_c2r_2d (m_tile_size, m_tile_size, in.data (), d,
                                      FFTW_ESTIMATE);
  plan_2d = fftw_plan_dft_r2c_2d (m_tile_size, m_tile_size, d, in.data (),
                                  FFTW_ESTIMATE);
  fftw_lock.unlock ();
  fftw_execute (plan_2d);
  for (int i = 0; i < m_fft_size * m_tile_size; i++)
    {
      std::complex w (m_blur_kernel[i][0], m_blur_kernel[i][1]);
      std::complex v (in[i][0], in[i][1]);
      in[i][0] = real (v * w);
      in[i][1] = imag (v * w);
    }
  fftw_execute (plan_2d_inv);
  fftw_lock.lock ();
  fftw_destroy_plan (plan_2d);
  fftw_destroy_plan (plan_2d_inv);
  fftw_lock.unlock ();
}
void
deconvolution::sharpen_tile (deconvolution_data_t *d)
{
  // std::vector<deconvolution_data_t> out(m_tile_size * m_tile_size);
  fftw_plan plan_2d_inv, plan_2d;
  std::vector<fftw_complex> in (m_tile_size * m_fft_size);
  fftw_lock.lock ();
  plan_2d_inv = fftw_plan_dft_c2r_2d (m_tile_size, m_tile_size, in.data (), d,
                                      FFTW_ESTIMATE);
  plan_2d = fftw_plan_dft_r2c_2d (m_tile_size, m_tile_size, d, in.data (),
                                  FFTW_ESTIMATE);
  fftw_lock.unlock ();
  fftw_execute (plan_2d);
  double scale = 1.0 / (m_tile_size * m_tile_size);
  for (int i = 0; i < m_fft_size * m_tile_size; i++)
    {
      std::complex w (m_blur_kernel[i][0], m_blur_kernel[i][1]);
      std::complex v (in[i][0], in[i][1]);
      in[i][0] = real (v / w * scale * scale);
      in[i][1] = imag (v / w * scale * scale);
    }
  fftw_execute (plan_2d_inv);
  fftw_lock.lock ();
  fftw_destroy_plan (plan_2d);
  fftw_destroy_plan (plan_2d_inv);
  fftw_lock.unlock ();
}

}
