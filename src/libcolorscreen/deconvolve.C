/* MTF based deconvolution.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "fft.h"
#include "deconvolve.h"
#include "cubic-interpolate.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>
namespace colorscreen
{

/* Avoid sharp edges along end of the tile.  */
static const bool taper_edges = true;

/* FFTW execute is thread safe. Everything else is not.  */

namespace
{

/* Return floor (NUMERATOR / DENOMINATOR) for positive DENOMINATOR.  C++
   integer division truncates toward zero and therefore needs adjustment for
   negative coordinates.  */

inline int
floor_div (int numerator, int denominator)
{
  int quotient = numerator / denominator;
  if (numerator < 0 && numerator % denominator)
    quotient--;
  return quotient;
}

/* Interpolate the midpoint of P0, P1, P2 and P3 with the Catmull--Rom
   cubic used by CUBIC_INTERPOLATE.  The exact midpoint weights are
   (-1, 9, 9, -1) / 16.  Accumulating in double prevents the generic
   luminosity_t helper from truncating a deconvolution<double> path to float.  */
template <typename T>
inline double
cubic_midpoint (T p0, T p1, T p2, T p3)
{
  return (-(double)p0 + 9.0 * (double)p1 + 9.0 * (double)p2
          - (double)p3)
         / 16.0;
}

/* Return sin (pi * X) / (pi * X), including its removable value at zero.  */
inline double
sinc_pi (double x)
{
  if (std::abs (x) < 1e-12)
    return 1.0;
  const double pix = M_PI * x;
  return std::sin (pix) / pix;
}

/* Return the Lanczos coefficient at X for radius SUPPORT.  */
inline double
lanczos_coefficient (double x, int support)
{
  if (support <= 0 || std::abs (x) >= support)
    return 0.0;
  return sinc_pi (x) * sinc_pi (x / support);
}

/* Lanczos resample INPUT of length IN_LEN into contiguous OUTPUT of length
   OUT_LEN.  SUPERSAMPLE is the ratio between output and input sample spacing.
   KERNELS contains one normalized STRIDE-slot kernel for every output phase.
   A coefficients on each side are nonzero; any remaining slots are explicit
   zero padding used to make the short kernel vectorize efficiently.  */
template <typename T, int A, int STRIDE>
inline __attribute__ ((always_inline))
void
resample_line (T *output, const T *input, int out_len, int in_len,
               int supersample,
               const std::vector<double, fft_allocator<double>> &kernels)
{
  if (supersample <= 1 || supersample > 1024 || in_len <= 0 || out_len < 0)
    abort ();

  for (int i = 0; i < out_len; i++)
    {
      /* Input sample centers are at J + 0.5.  Output sample centers, in input
         coordinates, are at (I + 0.5) / SUPERSAMPLE.  */
      int base = floor_div (2 * i + 1 - supersample, 2 * supersample);
      int start = base - A + 1;
      const double *kernel = kernels.data () + (i % supersample) * STRIDE;
      double sum = 0;

      if (start >= 0 && start + STRIDE <= in_len)
        {
#pragma omp simd reduction(+:sum)
          for (int tap = 0; tap < STRIDE; tap++)
            sum += (double)input[start + tap] * kernel[tap];
        }
      else
        for (int tap = 0; tap < STRIDE; tap++)
          sum += (double)input[reflect_deconvolution_coordinate (
                               start + tap, in_len)]
                 * kernel[tap];

      output[i] = (T)sum;
    }
}

/* Resample the vertical dimension of row-major INPUT.  INPUT has IN_LEN rows
   and WIDTH columns; OUTPUT has OUT_LEN rows and WIDTH columns.  Processing a
   complete output row at once avoids the previous cache-unfriendly gather and
   scatter of every individual column.  SUPERSAMPLE, KERNELS, A and STRIDE have
   the same meaning as in RESAMPLE_LINE.  */
template <typename T, int A, int STRIDE>
inline void
resample_columns (T *output, const T *input, int out_len, int width,
                  int in_len, int supersample,
                  const std::vector<double, fft_allocator<double>> &kernels)
{
  if (supersample <= 1 || supersample > 1024 || in_len <= 0 || out_len < 0
      || width <= 0)
    abort ();

  for (int y = 0; y < out_len; y++)
    {
      const int base
          = floor_div (2 * y + 1 - supersample, 2 * supersample);
      const int start = base - A + 1;
      const double *kernel = kernels.data () + (y % supersample) * STRIDE;
      T *output_row = output + y * width;

      if (start >= 0 && start + STRIDE <= in_len)
        {
#pragma omp simd
          for (int x = 0; x < width; x++)
            {
              double sum = 0;
              for (int tap = 0; tap < STRIDE; tap++)
                sum += (double)input[(start + tap) * width + x]
                       * kernel[tap];
              output_row[x] = (T)sum;
            }
        }
      else
        {
          int source_rows[STRIDE];
          for (int tap = 0; tap < STRIDE; tap++)
            source_rows[tap]
                = reflect_deconvolution_coordinate (start + tap, in_len);
#pragma omp simd
          for (int x = 0; x < width; x++)
            {
              double sum = 0;
              for (int tap = 0; tap < STRIDE; tap++)
                sum += (double)input[source_rows[tap] * width + x]
                       * kernel[tap];
              output_row[x] = (T)sum;
            }
        }
    }
}

/* Supersample square INPUT of size IN_LEN into square OUTPUT of size OUT_LEN.
   INTERMEDIATE stores the horizontally reconstructed IN_LEN by OUT_LEN image.
   SUPERSAMPLE and KERNELS select phases and coefficients.  A is the Lanczos
   support and STRIDE is the padded number of coefficient slots per phase.  */
template <typename T, int A, int STRIDE>
inline void
upsample_tile (T *output, T *intermediate, const T *input, int out_len,
               int in_len, int supersample,
               const std::vector<double, fft_allocator<double>> &kernels)
{
  for (int y = 0; y < in_len; y++)
    resample_line<T, A, STRIDE> (intermediate + y * out_len,
                                 input + y * in_len, out_len, in_len,
                                 supersample, kernels);
  resample_columns<T, A, STRIDE> (output, intermediate, out_len, out_len,
                                  in_len, supersample, kernels);
}
}

/* Set up deconvolution for given MTF and MTF_SCALE.  SNR specifies signal to
   noise ratio for Wiener filter.  SIGMA specifies damping parameter for
   Richardson-Lucy.  MAX_THREADS specifies number of threads.  FILTER_MODE is
   deconvolution mode.  ITERATIONS specifies number of iterations for
   Richardson-Lucy.  SUPERSAMPLE specifies supersampling factor.  RESAMPLING
   specifies the reconstruction kernel used to create the fine grid.  */
template <typename T>
deconvolution<T>::deconvolution (mtf *mtf, luminosity_t mtf_scale,
                                 luminosity_t snr, luminosity_t sigma,
                                 int max_threads, enum mode filter_mode,
                                 int iterations, int supersample,
                                 enum sharpen_parameters::resampling_kernel
                                     resampling)
    : m_supersample (std::clamp (supersample, 1, 16)),
      m_resampling (resampling == sharpen_parameters::lanczos8_resampling
                        ? sharpen_parameters::lanczos8_resampling
                        : sharpen_parameters::lanczos3_resampling),
      m_kernel_support (
          m_resampling == sharpen_parameters::lanczos8_resampling ? 8 : 3),
      m_kernel_stride (
          m_resampling == sharpen_parameters::lanczos8_resampling ? 16 : 8),
      m_sigma (my_isfinite ((double)sigma) && sigma > 0 ? (T)sigma : (T)0),
      m_iterations (std::max (iterations, 0))
{
  if (!mtf || !mtf->precompute ())
    abort ();

  const double effective_mtf_scale
      = my_isfinite ((double)mtf_scale) && mtf_scale > 0
            ? (double)mtf_scale
            : 0.0;
  const bool apply_wiener
      = filter_mode == sharpen && my_isfinite ((double)snr) && snr > 0;
  const bool apply_richardson_lucy
      = filter_mode == richardson_lucy_sharpen && m_iterations > 0;
  const double k_const = apply_wiener ? 1.0 / (double)snr : 0.0;

  m_border_size = std::max (mtf->psf_radius (effective_mtf_scale), 0);
  if (m_border_size == 0)
    m_border_size = 1;

  if (taper_edges)
    {
      m_taper_size = m_border_size * m_supersample;
      m_border_size *= 2;
    }
  /* The copied output region must also be separated from the tile boundary by
     the support of the interpolation filter.  Otherwise supersampling can use
     reflected tile-edge samples at every tile seam even when the optical PSF
     itself is very small.  */
  if (m_supersample > 1)
    m_border_size = std::max (m_border_size, m_kernel_support);

  while (m_enlarged_tile_size < m_border_size * 4 * m_supersample)
    m_enlarged_tile_size *= 2;
  m_tile_size = (m_enlarged_tile_size + m_supersample - 1) / m_supersample;

  m_data.resize (std::max (max_threads, 1));
  /* The result of a real FFT is Hermitian.  We need only N / 2 + 1 complex
     values per row.  The assumed optical transfer function is rotationally
     symmetric and has zero phase, so its entries are real up to roundoff.  */
  m_fft_size = m_enlarged_tile_size / 2 + 1;
  m_blur_kernel = fft_alloc_complex<T> (m_enlarged_tile_size * m_fft_size);
  const double fft_scale
      = 1.0 / ((double)m_enlarged_tile_size * m_enlarged_tile_size);
  const double frequency_step
      = (double)m_supersample * effective_mtf_scale / m_enlarged_tile_size;
#pragma omp parallel for default(none) collapse(2) \
  shared(fft_scale, mtf, frequency_step, filter_mode, k_const, apply_wiener, \
         apply_richardson_lucy)
  for (int y = 0; y < m_fft_size; y++)
    for (int x = 0; x < m_fft_size; x++)
      {
        /* Prepare the optical transfer and Wiener denominator in double.  The
           large image transform may use FFTW single precision, but rounding a
           weak MTF before regularization needlessly changes the inverse gain.  */
        double transfer = mtf->get_mtf ((double)x, (double)y, frequency_step);
        if (!my_isfinite (transfer))
          transfer = 0;
        transfer = std::clamp (transfer, 0.0, 1.0);
        double kernel = transfer;

        /* Wiener inverse filter:
             estimate = image * H / (H^2 + 1 / SNR).
           The radial model is real and zero-phase.  A nonpositive or nonfinite
           SNR disables sharpening and therefore gives the identity transfer.  */
        if (filter_mode == sharpen)
          kernel = apply_wiener
                       ? transfer / (transfer * transfer + k_const)
                       : 1.0;
        else if (filter_mode == richardson_lucy_sharpen
                 && !apply_richardson_lucy)
          kernel = 1.0;
        const T stored_kernel = (T)(kernel * fft_scale);
        m_blur_kernel[y * m_fft_size + x][0] = stored_kernel;
        m_blur_kernel[y * m_fft_size + x][1] = 0;
        if (y)
          {
            m_blur_kernel[(m_enlarged_tile_size - y) * m_fft_size + x][0]
                = stored_kernel;
            m_blur_kernel[(m_enlarged_tile_size - y) * m_fft_size + x][1] = 0;
          }
      }

  /* Preserve the image mean and prevent invalid MTF data from turning the
     complete image into NaNs.  M_BLUR_KERNEL already contains FFT_SCALE, so
     validate the unscaled DC transfer.  For a 4096 by 4096 single-precision
     FFT, FFT_SCALE is smaller than float epsilon even when the physical DC
     response is exactly one; testing the stored coefficient against epsilon
     would therefore replace a perfectly valid optical kernel by identity.  */
  const double dc_transfer = (double)m_blur_kernel[0][0] / fft_scale;
  if (!my_isfinite (dc_transfer)
      || std::abs (dc_transfer) <= std::numeric_limits<T>::epsilon ())
    for (int i = 0; i < m_fft_size * m_enlarged_tile_size; i++)
      {
        m_blur_kernel[i][0] = (T)fft_scale;
        m_blur_kernel[i][1] = 0;
      }
  else
    {
      const double normalization = 1.0 / dc_transfer;
      if (normalization != 1.0)
        for (int i = 0; i < m_fft_size * m_enlarged_tile_size; i++)
          {
            m_blur_kernel[i][0]
                = (T)((double)m_blur_kernel[i][0] * normalization);
            m_blur_kernel[i][1]
                = (T)((double)m_blur_kernel[i][1] * normalization);
          }
    }

  m_richardson_lucy = apply_richardson_lucy;
  if (taper_edges)
    {
      m_weights.resize (m_taper_size);
      for (int i = 0; i < m_taper_size; i++)
        /* Cosine bell curve: 0 at the edge and 1 immediately after the
           taper.  */
        m_weights[i]
            = (T)(0.5
                  * (1.0 - std::cos (M_PI * (double)i / m_taper_size)));
    }

  if (m_supersample > 1)
    {
      m_resampling_kernels.assign (m_kernel_stride * m_supersample, 0.0);
      for (int phase = 0; phase < m_supersample; phase++)
        {
          /* Express the fine-grid phase as an exact rational number before
             converting it to T.  Computing

               (PHASE + 0.5) / SUPERSAMPLE - 0.5

             directly in floating point is unsafe with -ffast-math.  For
             example, GCC can evaluate the exact zero for phase 1 at 3x as a
             tiny negative number; floor then changes from 0 to -1 and shifts
             the Lanczos kernel by one fine-grid sample.  Keep BASE consistent
             with RESAMPLE_LINE by using the same integer floor division, and
             convert only the nonnegative fractional remainder to T.  */
          const int numerator = 2 * phase + 1 - m_supersample;
          const int denominator = 2 * m_supersample;
          const int base = floor_div (numerator, denominator);
          const int remainder = numerator - base * denominator;
          const double fraction = (double)remainder / denominator;
          double sum = 0;
          for (int tap = 0; tap < m_kernel_support * 2; tap++)
            {
              double coefficient
                  = lanczos_coefficient (
                      (double)tap - m_kernel_support + 1 - fraction,
                      m_kernel_support);
              m_resampling_kernels[phase * m_kernel_stride + tap]
                  = coefficient;
              sum += coefficient;
            }
          if (std::abs (sum) <= std::numeric_limits<double>::epsilon ())
            abort ();
          for (int tap = 0; tap < m_kernel_support * 2; tap++)
            m_resampling_kernels[phase * m_kernel_stride + tap]
                /= sum;
        }
    }
}
/* Allocate memory for tiles and initialize fftw plans for given THREAD_ID.  */
template <typename T>
void
deconvolution<T>::init (int thread_id)
{
  if (m_data[thread_id].initialized)
    return;
  m_data[thread_id].in = fft_alloc_complex<T> (m_enlarged_tile_size * m_fft_size);
  m_data[thread_id].tile.resize (m_tile_size * m_tile_size);
  if (m_supersample > 1)
    {
      m_data[thread_id].enlarged_tile_data.resize (m_enlarged_tile_size
                                                   * m_enlarged_tile_size);
      m_data[thread_id].enlarged_tile = &m_data[thread_id].enlarged_tile_data;
      m_data[thread_id].resample_intermediate.resize (
          m_tile_size * m_enlarged_tile_size);
    }
  else
    m_data[thread_id].enlarged_tile = &m_data[thread_id].tile;
  if (m_richardson_lucy)
    {
      m_data[thread_id].ratios.resize (m_enlarged_tile_size
                                       * m_enlarged_tile_size);
      m_data[thread_id].observed.resize (m_enlarged_tile_size
                                         * m_enlarged_tile_size);
    }
  std::call_once (m_plan_once, [this, thread_id] {
    m_plan_2d_inv
        = fft_plan_c2r_2d<T> (m_enlarged_tile_size, m_enlarged_tile_size,
                              m_data[thread_id].in.get (),
                              m_data[thread_id].enlarged_tile->data ());
    m_plan_2d
        = fft_plan_r2c_2d<T> (m_enlarged_tile_size, m_enlarged_tile_size,
                              m_data[thread_id].enlarged_tile->data (),
                              m_data[thread_id].in.get ());
  });
  m_data[thread_id].initialized = true;
}

/* Apply the deconvolution kernel for given THREAD_ID.  */
template <typename T>
void
deconvolution<T>::process_tile (int thread_id, progress_info *progress)
{
  if (progress && progress->cancelled ())
    return;
  if (m_supersample > 1)
    {
      T *output = m_data[thread_id].enlarged_tile->data ();
      T *intermediate = m_data[thread_id].resample_intermediate.data ();
      const T *input = m_data[thread_id].tile.data ();
      switch (m_resampling)
        {
        case sharpen_parameters::lanczos3_resampling:
          upsample_tile<T, 3, 8> (output, intermediate, input,
                                  m_enlarged_tile_size, m_tile_size,
                                  m_supersample, m_resampling_kernels);
          break;
        case sharpen_parameters::lanczos8_resampling:
          upsample_tile<T, 8, 16> (output, intermediate, input,
                                   m_enlarged_tile_size, m_tile_size,
                                   m_supersample, m_resampling_kernels);
          break;
        default:
          abort ();
        }
      if (progress && progress->cancelled ())
	return;
    }
  if (taper_edges)
    {
      double sum = 0;

      /* Compute average pixel.  */
      for (int y = 0; y < m_taper_size; y++)
        for (int x = 0; x < m_enlarged_tile_size; x++)
          sum += get_enlarged_pixel (thread_id, x, y);
      for (int y = 0; y < m_taper_size; y++)
        for (int x = 0; x < m_enlarged_tile_size; x++)
          sum += get_enlarged_pixel (thread_id, x,
                                     y + m_enlarged_tile_size - m_taper_size);
      for (int y = m_taper_size; y < m_enlarged_tile_size - m_taper_size; y++)
        {
          for (int x = 0; x < m_taper_size; x++)
            sum += get_enlarged_pixel (thread_id, x, y);
          for (int x = 0; x < m_taper_size; x++)
            sum += get_enlarged_pixel (
                thread_id, x + m_enlarged_tile_size - m_taper_size, y);
        }
      sum /= (double)(m_enlarged_tile_size * m_taper_size * 2
             + (m_enlarged_tile_size - 2 * m_taper_size) * m_taper_size * 2);
      /* Taper top edge.  */
      for (int y = 0; y < m_taper_size; y++)
        {
          T weight = m_weights[y];
#pragma omp simd
          for (int x = 0; x < y; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[x]);
#pragma omp simd
          for (int x = y; x < m_enlarged_tile_size - y; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum + (get_enlarged_pixel (thread_id, x, y) - sum) * weight);
#pragma omp simd
          for (int x = m_enlarged_tile_size - y; x < m_enlarged_tile_size; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[m_enlarged_tile_size - 1 - x]);
        }
      /* Taper left and right edge.  */
      for (int y = m_taper_size; y < m_enlarged_tile_size - m_taper_size; y++)
        {
#pragma omp simd
          for (int x = 0; x < m_taper_size; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[x]);
#pragma omp simd
          for (int x = m_enlarged_tile_size - m_taper_size;
               x < m_enlarged_tile_size; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[m_enlarged_tile_size - 1 - x]);
        }
      /* Taper bottom edge.  */
      for (int y = m_enlarged_tile_size - m_taper_size;
           y < m_enlarged_tile_size; y++)
        {
          int d = m_enlarged_tile_size - 1 - y;
          T weight = m_weights[d];
#pragma omp simd
          for (int x = 0; x < d; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[x]);
#pragma omp simd
          for (int x = d; x < m_enlarged_tile_size - d; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum + (get_enlarged_pixel (thread_id, x, y) - sum) * weight);
#pragma omp simd
          for (int x = m_enlarged_tile_size - d; x < m_enlarged_tile_size; x++)
            put_enlarged_pixel (
                thread_id, x, y,
                sum
                    + (get_enlarged_pixel (thread_id, x, y) - sum)
                          * m_weights[m_enlarged_tile_size - 1 - x]);
        }
    }
  if (progress && progress->cancelled ())
    return;

  if (!m_richardson_lucy)
    {
      typename fft_complex_t<T>::type *in = m_data[thread_id].in.get ();
      m_plan_2d.execute_r2c (m_data[thread_id].enlarged_tile->data (), in);
#pragma omp simd
      for (int i = 0; i < m_fft_size * m_enlarged_tile_size; i++)
        {
          T vr = in[i][0];
          T vi = in[i][1];
          T wr = m_blur_kernel[i][0];
          T wi = m_blur_kernel[i][1];
          in[i][0] = vr * wr - vi * wi;
          in[i][1] = vr * wi + vi * wr;
        }
      m_plan_2d_inv.execute_c2r (in, m_data[thread_id].enlarged_tile->data ());
    }
  else
    {
      std::vector<T, fft_allocator<T>> &observed = m_data[thread_id].observed;
      std::vector<T, fft_allocator<T>> &estimate
          = *m_data[thread_id].enlarged_tile;
      for (size_t i = 0; i < estimate.size (); i++)
        {
          T value = estimate[i];
          value = my_isfinite ((double)value) && value > (T)0 ? value : (T)0;
          estimate[i] = value;
          observed[i] = value;
        }
      std::vector<T, fft_allocator<T>> &ratios = m_data[thread_id].ratios;
      T scale = (T)1;
      typename fft_complex_t<T>::type *in = m_data[thread_id].in.get ();
      T sigma = m_sigma;
      for (int iteration = 0; iteration < m_iterations; iteration++)
        {
	  if (progress && progress->cancelled ())
	    return;
          /* Step A: Re-blur the current estimate.  */

          /* Blur current estimate to IN.  */
          m_plan_2d.execute_r2c (estimate.data (), in);
#pragma omp simd
          for (int i = 0; i < m_fft_size * m_enlarged_tile_size; i++)
            {
              T vr = in[i][0];
              T vi = in[i][1];
              T wr = m_blur_kernel[i][0];
              T wi = m_blur_kernel[i][1];
              in[i][0] = vr * wr - vi * wi;
              in[i][1] = vr * wi + vi * wr;
            }
          m_plan_2d_inv.execute_c2r (in, ratios.data ());

          /* Step B: ratio = observed / (re-blurred + epsilon).  */

          const T epsilon
              = std::max ((T)1e-12, std::numeric_limits<T>::epsilon () * (T)16);

          /* RATIOS is now blurred ESTIMATE; compute ratios.  */
          if (sigma > (T)0)
#pragma omp simd
            for (int i = 0; i < m_enlarged_tile_size * m_enlarged_tile_size;
                 i++)
              {
                T reblurred = ratios[i] * scale;
                T diff = observed[i] - reblurred;
                if (reblurred > epsilon && std::abs (diff) > (T)2 * sigma)
                  ratios[i] = (T)1.0
                               + (reblurred * diff)
                                     / (reblurred * reblurred + sigma * sigma);
                else
                  ratios[i] = (T)1.0;
              }
          else
#pragma omp simd
            for (int i = 0; i < m_enlarged_tile_size * m_enlarged_tile_size;
                 i++)
              {
                T reblurred = ratios[i] * scale;
                if (reblurred > epsilon)
                  ratios[i] = observed[i] / reblurred;
                else
                  ratios[i] = (T)1.0;
              }

          /* Step C: Update estimate
             FFT(ratio) -> multiply by FFT(PSF_flipped) -> IFFT
             estimate = estimate * result_of_Step_C.  */

          /* Do FFT of ratio. */
          m_plan_2d.execute_r2c (ratios.data (), in);
          /* Scale by complex conjugate of blur kernel.  */
#pragma omp simd
          for (int i = 0; i < m_fft_size * m_enlarged_tile_size; i++)
            {
              T vr = in[i][0];
              T vi = in[i][1];
              T wr = m_blur_kernel[i][0];
              T wi = -m_blur_kernel[i][1];
              in[i][0] = vr * wr - vi * wi;
              in[i][1] = vr * wi + vi * wr;
            }
          /* Now initialize ratios.  */
          m_plan_2d_inv.execute_c2r (in, ratios.data ());

          /* Richardson-Lucy is defined for nonnegative intensities.  Keep
             roundoff or a non-positive reconstructed PSF from creating NaNs
             or negative estimates that poison later iterations.  */
#pragma omp simd
          for (int i = 0; i < m_enlarged_tile_size * m_enlarged_tile_size; i++)
            {
              T updated = estimate[i] * ratios[i] * scale;
              estimate[i] = my_isfinite ((double)updated) && updated > (T)0
                                ? updated
                                : (T)0;
            }
        }
    }
  /* Sample the processed fine grid at the centers of the original pixels.
     For odd supersampling factors the center is a fine-grid sample.  For even
     factors it lies halfway between four samples and is reconstructed with
     the same bicubic interpolator previously used for 2x supersampling.  */
  if (m_supersample > 1)
    {
      if (progress && progress->cancelled ())
        return;
      const T *enlarged = m_data[thread_id].enlarged_tile->data ();
      if (m_supersample & 1)
        {
          const int center = m_supersample / 2;
          for (int y = m_border_size; y < m_tile_size - m_border_size; y++)
#pragma omp simd
            for (int x = m_border_size; x < m_tile_size - m_border_size; x++)
              put_pixel (thread_id, x, y,
                         enlarged[(y * m_supersample + center)
                                      * m_enlarged_tile_size
                                  + x * m_supersample + center]);
        }
      else
        {
          const int center_left = m_supersample / 2 - 1;
          for (int y = m_border_size; y < m_tile_size - m_border_size; y++)
#pragma omp simd
            for (int x = m_border_size; x < m_tile_size - m_border_size; x++)
              {
                int sx = x * m_supersample + center_left;
                int sy = y * m_supersample + center_left;
                const double column0 = cubic_midpoint (
                    enlarged[(sy - 1) * m_enlarged_tile_size + sx - 1],
                    enlarged[sy * m_enlarged_tile_size + sx - 1],
                    enlarged[(sy + 1) * m_enlarged_tile_size + sx - 1],
                    enlarged[(sy + 2) * m_enlarged_tile_size + sx - 1]);
                const double column1 = cubic_midpoint (
                    enlarged[(sy - 1) * m_enlarged_tile_size + sx],
                    enlarged[sy * m_enlarged_tile_size + sx],
                    enlarged[(sy + 1) * m_enlarged_tile_size + sx],
                    enlarged[(sy + 2) * m_enlarged_tile_size + sx]);
                const double column2 = cubic_midpoint (
                    enlarged[(sy - 1) * m_enlarged_tile_size + sx + 1],
                    enlarged[sy * m_enlarged_tile_size + sx + 1],
                    enlarged[(sy + 1) * m_enlarged_tile_size + sx + 1],
                    enlarged[(sy + 2) * m_enlarged_tile_size + sx + 1]);
                const double column3 = cubic_midpoint (
                    enlarged[(sy - 1) * m_enlarged_tile_size + sx + 2],
                    enlarged[sy * m_enlarged_tile_size + sx + 2],
                    enlarged[(sy + 1) * m_enlarged_tile_size + sx + 2],
                    enlarged[(sy + 2) * m_enlarged_tile_size + sx + 2]);
                put_pixel (thread_id, x, y,
                           (T)cubic_midpoint (column0, column1, column2,
                                              column3));
              }
        }
    }
}

/* Destructor.  */
template <typename T>
deconvolution<T>::~deconvolution ()
{
}
template class deconvolution<float>;
template class deconvolution<double>;
}
