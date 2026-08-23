/* MTF based deconvolution.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef DECONVOLVE_H
#define DECONVOLVE_H
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/progress-info.h"
#include "include/render-parameters.h"
#include "mtf.h"
#include "fft.h"
#include <array>
#include <memory>
#include <mutex>
#include <omp.h>
#include <vector>
namespace colorscreen
{

/* Reflect COORDINATE into [0, SIZE).  The left edge follows the historical
   deconvolution convention -1 -> 1, while the right edge is repeated once,
   SIZE -> SIZE - 1.  The periodic form remains correct when the requested
   border is wider than the complete image.  */
inline int
reflect_deconvolution_coordinate (long long coordinate, int size)
{
  if (size <= 1)
    return 0;
  const long long period = (long long)2 * size - 1;
  coordinate %= period;
  if (coordinate < 0)
    coordinate = -coordinate;
  if (coordinate >= size)
    coordinate = period - coordinate;
  return (int)coordinate;
}

/* Class managing deconvolution process.
   T is the data type (float or double).  */
template <typename T>
class deconvolution
{
public:
  /* Supported deconvolution modes.  */
  enum mode
  {
    /* Blur the image using the MTF.  */
    blur,
    /* Sharpen the image using Wiener filter.  */
    sharpen,
    /* Sharpen the image using Richardson-Lucy deconvolution.  */
    richardson_lucy_sharpen,
    /* Same as blur.  */
    blur_deconvolution
  };
  /* Set up deconvolution for given MTF and MTF_SCALE.  MTF is interpreted as
     a radial, zero-phase transfer magnitude.  SNR is the effective
     signal/noise power ratio in the scalar Wiener regularizer; lower values
     suppress more high-frequency noise.
     SIGMA specifies damping parameter for Richardson-Lucy.
     MAX_THREADS specifies number of threads.
     FILTER_MODE is deconvolution mode.
     ITERATIONS specifies number of iterations for Richardson-Lucy.
     SUPERSAMPLE specifies supersampling factor.
     RESAMPLING specifies the reconstruction kernel used to create the
     supersampled image.  */
  deconvolution (mtf *mtf, luminosity_t mtf_scale,
		 luminosity_t snr, luminosity_t sigma, int max_threads,
		 enum mode filter_mode, int iterations, int supersample,
		 enum sharpen_parameters::resampling_kernel resampling
		     = sharpen_parameters::lanczos3_resampling);
  /* Destructor.  */
  ~deconvolution ();

  /* Size of tile processed without borders.  */
  int
  get_basic_tile_size () const noexcept
  {
    return m_tile_size - m_border_size * 2;
  }

  /* Size of tile processed with borders.  */
  int
  get_tile_size_with_borders () const noexcept
  {
    return m_tile_size;
  }

  /* Size of border.  */
  int
  get_border_size () const noexcept
  {
    return m_border_size;
  }

  /* Allocate memory for tile for given THREAD_ID.  */
  void init (int thread_id);

  /* Put pixel VAL at coordinates X, Y for given THREADID.  */
  void
  put_pixel (int threadid, int x, int y, T val)
  {
    m_data[threadid].tile[y * m_tile_size + x] = val;
  }

  /* Get pixel at coordinates X, Y for given THREADID.  */
  T
  get_pixel (int threadid, int x, int y) const
  {
    return m_data[threadid].tile[y * m_tile_size + x];
  }

  /* Apply sharpening/blurring to the tile for given THREAD_ID.  */
  void process_tile (int thread_id, progress_info *progress);


private:

  /* Put pixel VAL to enlarged tile at X, Y for given THREADID.  */
  void
  put_enlarged_pixel (int threadid, int x, int y, T val)
  {
    (*m_data[threadid].enlarged_tile)[y * m_enlarged_tile_size + x] = val;
  }

  /* Get pixel from enlarged tile at X, Y for given THREADID.  */
  T
  get_enlarged_pixel (int threadid, int x, int y) const
  {
    return (*m_data[threadid].enlarged_tile)[y * m_enlarged_tile_size + x];
  }
  /* Size of border that is not sharpened correctly (in original tile).  */
  int m_border_size = 0;
  /* Size of tapering along edges (in enlarged tile).  */
  int m_taper_size = 0;
  /* Size of original tile being sharpened (including borders).  */
  int m_tile_size = 1;
  /* Size of enlarged tile being sharpened (including borders).  */
  int m_enlarged_tile_size = 1;
  /* Size of the FFT problem.  */
  int m_fft_size = 0;
  /* Supersampling factor. */
  int m_supersample = 1;
  /* Reconstruction kernel selected for supersampling.  */
  enum sharpen_parameters::resampling_kernel m_resampling
      = sharpen_parameters::lanczos3_resampling;
  /* Number of nonzero samples on either side of the Lanczos kernel.  */
  int m_kernel_support = 3;
  /* Number of coefficient slots per phase.  Lanczos 3 is padded from six to
     eight slots so its inner loop maps efficiently to SIMD vector widths.  */
  int m_kernel_stride = 8;
  /* Kernel for blurring or sharpening.  */
  fft_unique_ptr<T> m_blur_kernel = nullptr;

  /* True if Richardson-Lucy deconvolution is used.  */
  bool m_richardson_lucy = false;
  /* Sigma parameter for Richardson-Lucy.  */
  T m_sigma = 0;
  /* Number of iterations for Richardson-Lucy.  */
  int m_iterations = 0;

  /* Weights of edge tapering.  */
  std::vector<T, fft_allocator<T>> m_weights;

  /* Reconstruction kernels for resampling.  Coefficients are kept in double
     even for the single-precision image FFT; the table is tiny and its
     rounding error would otherwise be accumulated for every resampled pixel.  */
  std::vector<double, fft_allocator<double>> m_resampling_kernels;

  /* FFT plans.  */
  fft_plan<T> m_plan_2d_inv, m_plan_2d;
  /* Initialize shared FFT plans exactly once.  */
  std::once_flag m_plan_once;

  /* Data for a single tile.  */
  struct tile_data
  {
    /* FFT buffer.  */
    fft_unique_ptr<T> in = nullptr;
    /* Tile data.  */
    std::vector<T, fft_allocator<T>> tile;
    /* Pointer to enlarged tile data.  */
    std::vector<T, fft_allocator<T>> *enlarged_tile = nullptr;
    /* Enlarged tile data.  */
    std::vector<T, fft_allocator<T>> enlarged_tile_data;
    /* Horizontally resampled rows.  The second, vertical pass reads this buffer
       row-wise instead of gathering and scattering one cache-unfriendly column
       at a time.  */
    std::vector<T, fft_allocator<T>> resample_intermediate;
    /* Buffers for Richardson-Lucy.  */
    std::vector<T, fft_allocator<T>> ratios;
    std::vector<T, fft_allocator<T>> observed;
    /* True if initialized.  */
    bool initialized = false;
  };
  /* Data for all threads.  */
  std::vector<tile_data> m_data;
};

/* Deconvolution worker. Sharpen DATA to OUT which both has dimensions
   WIDTH*HEIGHT. DATA are accessed using GETDATA function and PARAM can be used
   to pass extra data around. SHARPEN specifies sharpening parameters.
   PROGRESS is progress info. PARALLEL is true if parallel execution is
   requested. O is output type name, MEM_O is memory output type, T is data
   type name, P is extra bookkeeping parameter type. DT is a type to do
   deconvolution in.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
deconvolve (mem_O *out, T data, P param, int width, int height,
            const sharpen_parameters &sharpen, progress_info *progress,
            bool parallel = true)
{
  if (!out || width <= 0 || height <= 0)
    return false;
  int nthreads = parallel ? omp_get_max_threads () : 1;
  typename deconvolution<DT>::mode mode;
  if (progress)
    progress->set_task ("initializing mtf based deconvolution", 1);
  switch (sharpen.mode)
    {
    case sharpen_parameters::richardson_lucy_deconvolution:
      mode = deconvolution<DT>::richardson_lucy_sharpen;
      break;
    case sharpen_parameters::wiener_deconvolution:
      mode = deconvolution<DT>::sharpen;
      break;
    case sharpen_parameters::blur_deconvolution:
      mode = deconvolution<DT>::blur;
      break;
    default:
      abort ();
    }
  std::shared_ptr<mtf> scanner_mtf = mtf::get_mtf (sharpen.scanner_mtf, progress);
  if (!scanner_mtf || !scanner_mtf->precompute (progress, parallel))
    return false;
  deconvolution<DT> d (scanner_mtf.get (), sharpen.scanner_mtf_scale,
                   sharpen.scanner_snr, sharpen.richardson_lucy_sigma,
                   nthreads, mode, sharpen.richardson_lucy_iterations,
		   sharpen.supersample, sharpen.resampling);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution<DT>::sharpen)
        progress->set_task ("deconvolution sharpening (Wiener filter)",
                            xtiles * ytiles);
      else if (mode == deconvolution<DT>::blur)
        progress->set_task ("deconvolution blurring",
                            xtiles * ytiles);
      else
        progress->set_task ("deconvolution sharpening (Richardson-Lucy)",
                            xtiles * ytiles);
    }
#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, out, param, parallel, data) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              /* Mirror repeatedly when the deconvolution border is wider
                 than the image itself.  */
              px = reflect_deconvolution_coordinate (px, width);
              py = reflect_deconvolution_coordinate (py, height);
              d.put_pixel (id, xx, yy, getdata (data, {px, py}, width, param));
            }
        d.process_tile (id, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx] = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Deconvolution worker for rgbdata and related types.  Sharpen DATA to OUT.
   DATA are accessed using GETDATA function and PARAM can be used to pass extra
   data around.  SHARPEN contains independent parameters for red, green and
   blue.  The three channels must use the same deconvolution mode, while their
   MTFs and other controls may differ.  Each channel keeps its natural PSF
   support and FFT tile geometry so this combined RGB path remains numerically
   equivalent to three scalar deconvolution passes.  Results are written
   directly to the corresponding component of interleaved OUT storage.
   PROGRESS reports all three channel passes as one RGB operation.  PARALLEL is
   true if parallel execution is requested.  O is output type name, MEM_O is
   memory output type, T is data type name, P is extra bookkeeping parameter
   type.  DT is the type used for deconvolution.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
                const std::array<sharpen_parameters, 3> &sharpen,
                progress_info *progress, bool parallel = true)
{
  if (!out || width <= 0 || height <= 0)
    return false;

  const sharpen_parameters::sharpen_mode effective_mode
      = sharpen[0].get_mode ();
  if (!sharpen[0].deconvolution_p ())
    return false;
  for (int channel = 1; channel < 3; channel++)
    if (sharpen[channel].get_mode () != effective_mode)
      return false;

  typename deconvolution<DT>::mode mode;
  switch (effective_mode)
    {
    case sharpen_parameters::richardson_lucy_deconvolution:
      mode = deconvolution<DT>::richardson_lucy_sharpen;
      break;
    case sharpen_parameters::wiener_deconvolution:
      mode = deconvolution<DT>::sharpen;
      break;
    case sharpen_parameters::blur_deconvolution:
      mode = deconvolution<DT>::blur;
      break;
    default:
      return false;
    }

  if (progress)
    progress->set_task ("initializing RGB MTF based deconvolution", 1);
  const int nthreads = parallel ? omp_get_max_threads () : 1;
  std::array<std::shared_ptr<mtf>, 3> scanner_mtf;
  std::array<std::unique_ptr<deconvolution<DT>>, 3> filter;
  int total_tiles = 0;
  for (int channel = 0; channel < 3; channel++)
    {
      scanner_mtf[channel]
          = mtf::get_mtf (sharpen[channel].scanner_mtf, progress);
      if (!scanner_mtf[channel]
          || !scanner_mtf[channel]->precompute (progress, parallel))
        return false;
      filter[channel] = std::make_unique<deconvolution<DT>> (
          scanner_mtf[channel].get (), sharpen[channel].scanner_mtf_scale,
          sharpen[channel].scanner_snr,
          sharpen[channel].richardson_lucy_sigma, nthreads, mode,
          sharpen[channel].richardson_lucy_iterations,
          sharpen[channel].supersample, sharpen[channel].resampling);
      const int basic_tile_size = filter[channel]->get_basic_tile_size ();
      total_tiles
          += ((width + basic_tile_size - 1) / basic_tile_size)
             * ((height + basic_tile_size - 1) / basic_tile_size);
    }

  if (progress)
    {
      if (mode == deconvolution<DT>::blur)
        progress->set_task ("RGB deconvolution blurring", total_tiles);
      else if (mode == deconvolution<DT>::sharpen)
        progress->set_task ("RGB deconvolution sharpening (Wiener filter)",
                            total_tiles);
      else
        progress->set_task (
            "RGB deconvolution sharpening (Richardson-Lucy)", total_tiles);
    }

  for (int channel = 0; channel < 3; channel++)
    {
      deconvolution<DT> &d = *filter[channel];
      const int basic_tile_size = d.get_basic_tile_size ();
      const int tile_size = d.get_tile_size_with_borders ();
      const int border_size = d.get_border_size ();
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared (width, height, d, progress, out, param, parallel, data, channel,   \
            basic_tile_size, tile_size, border_size) if (parallel)
      for (int y = 0; y < height; y += basic_tile_size)
        for (int x = 0; x < width; x += basic_tile_size)
          {
            if (progress && progress->cancel_requested ())
              continue;
            const int id = parallel ? omp_get_thread_num () : 0;
            d.init (id);

            for (int yy = 0; yy < tile_size; yy++)
              for (int xx = 0; xx < tile_size; xx++)
                {
                  int px = x + xx - border_size;
                  int py = y + yy - border_size;

                  /* Mirror repeatedly when the deconvolution border is wider
                     than the image itself.  */
                  px = reflect_deconvolution_coordinate (px, width);
                  py = reflect_deconvolution_coordinate (py, height);
                  O pixel = getdata (data, {px, py}, width, param);
                  const auto value = channel == 0   ? pixel.red
                                     : channel == 1 ? pixel.green
                                                    : pixel.blue;
                  d.put_pixel (id, xx, yy, value);
                }
            d.process_tile (id, progress);
            for (int yy = 0; yy < basic_tile_size; yy++)
              for (int xx = 0; xx < basic_tile_size; xx++)
                if (y + yy < height && x + xx < width)
                  {
                    const auto value = d.get_pixel (
                        id, xx + border_size, yy + border_size);
                    mem_O &pixel = out[(y + yy) * width + x + xx];
                    if (channel == 0)
                      pixel.red = value;
                    else if (channel == 1)
                      pixel.green = value;
                    else
                      pixel.blue = value;
                  }
            if (progress)
              progress->inc_progress ();
          }
      if (progress && progress->cancelled ())
        return false;
    }
  return true;
}

/* Deconvolution worker for RGB data using one common sharpening transfer for
   all three channels.  Keep this path separate from the independent-channel
   overload so screen simulation and finetune do not construct three identical
   FFT kernels.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
                const sharpen_parameters &sharpen, progress_info *progress,
                bool parallel = true)
{
  if (!out || width <= 0 || height <= 0)
    return false;
  int nthreads = parallel ? omp_get_max_threads () : 1;
  typename deconvolution<DT>::mode mode;
  if (progress)
    progress->set_task ("initializing mtf based deconvolution", 1);
  switch (sharpen.mode)
    {
    case sharpen_parameters::richardson_lucy_deconvolution:
      mode = deconvolution<DT>::richardson_lucy_sharpen;
      break;
    case sharpen_parameters::wiener_deconvolution:
      mode = deconvolution<DT>::sharpen;
      break;
    case sharpen_parameters::blur_deconvolution:
      mode = deconvolution<DT>::blur;
      break;
    default:
      abort ();
    }
  std::shared_ptr<mtf> scanner_mtf
      = mtf::get_mtf (sharpen.scanner_mtf, progress);
  if (!scanner_mtf || !scanner_mtf->precompute (progress, parallel))
    return false;
  deconvolution<DT> d (
      scanner_mtf.get (), sharpen.scanner_mtf_scale, sharpen.scanner_snr,
      sharpen.richardson_lucy_sigma, nthreads * 3, mode,
      sharpen.richardson_lucy_iterations, sharpen.supersample,
      sharpen.resampling);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution<DT>::blur)
        progress->set_task ("deconvolution blurring", xtiles * ytiles);
      else if (mode == deconvolution<DT>::sharpen)
        progress->set_task ("deconvolution sharpening (Wiener filter)",
                            xtiles * ytiles);
      else
        progress->set_task ("deconvolution sharpening (Richardson-Lucy)",
                            xtiles * ytiles);
    }
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared (width, height, d, progress, out, param, parallel, data)            \
    if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancel_requested ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (3 * id);
        d.init (3 * id + 1);
        d.init (3 * id + 2);

        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              /* Mirror repeatedly when the deconvolution border is wider
                 than the image itself.  */
              px = reflect_deconvolution_coordinate (px, width);
              py = reflect_deconvolution_coordinate (py, height);
              O pixel = getdata (data, {px, py}, width, param);
              d.put_pixel (3 * id, xx, yy, pixel.red);
              d.put_pixel (3 * id + 1, xx, yy, pixel.green);
              d.put_pixel (3 * id + 2, xx, yy, pixel.blue);
            }
        d.process_tile (3 * id, progress);
        d.process_tile (3 * id + 1, progress);
        d.process_tile (3 * id + 2, progress);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              {
                out[(y + yy) * width + x + xx].red = d.get_pixel (
                    3 * id, xx + d.get_border_size (),
                    yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].green = d.get_pixel (
                    3 * id + 1, xx + d.get_border_size (),
                    yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].blue = d.get_pixel (
                    3 * id + 2, xx + d.get_border_size (),
                    yy + d.get_border_size ());
              }
        if (progress)
          progress->inc_progress ();
      }
  if (progress && progress->cancelled ())
    return false;
  return true;
}

/* Auto-select the type for deconvolution.
   O is output type, MEM_O is memory output type, T is data type, P is extra
   bookkeeping parameter type.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
deconvolve (mem_O *out, T data, P param, int width, int height,
            const sharpen_parameters &sharpen, progress_info *progress,
            bool parallel = true)
{
  /* For many iterations use double; otherwise float is good and faster.  */
  if (sharpen.mode != sharpen_parameters::richardson_lucy_deconvolution
      || sharpen.richardson_lucy_iterations < 300)
    return deconvolve<O, mem_O, T, P, getdata, float>(out, data, param, width,
						      height, sharpen, progress,
						      parallel);
  else
    return deconvolve<O, mem_O, T, P, getdata, double>(out, data, param, width,
						       height, sharpen, progress,
						       parallel);
}

/* Auto-select the type for deconvolution of RGB data with independent
   per-channel sharpening parameters.  O is output type, MEM_O is memory output
   type, T is data type and P is extra bookkeeping parameter type.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
                const std::array<sharpen_parameters, 3> &sharpen,
                progress_info *progress, bool parallel = true)
{
  bool need_double = false;
  for (const sharpen_parameters &channel : sharpen)
    if (channel.get_mode () == sharpen_parameters::richardson_lucy_deconvolution
        && channel.richardson_lucy_iterations >= 300)
      need_double = true;
  if (!need_double)
    return deconvolve_rgb<O, mem_O, T, P, getdata, float> (
        out, data, param, width, height, sharpen, progress, parallel);
  return deconvolve_rgb<O, mem_O, T, P, getdata, double> (
      out, data, param, width, height, sharpen, progress, parallel);
}

/* Auto-select the type for RGB deconvolution using one common transfer.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
                const sharpen_parameters &sharpen, progress_info *progress,
                bool parallel = true)
{
  if (sharpen.mode != sharpen_parameters::richardson_lucy_deconvolution
      || sharpen.richardson_lucy_iterations < 300)
    return deconvolve_rgb<O, mem_O, T, P, getdata, float> (
        out, data, param, width, height, sharpen, progress, parallel);
  return deconvolve_rgb<O, mem_O, T, P, getdata, double> (
      out, data, param, width, height, sharpen, progress, parallel);
}

}
#endif
