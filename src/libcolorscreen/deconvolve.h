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
#include <mutex>
#include <omp.h>
namespace colorscreen
{

/* Class managing deconvolution process.
   T is the data type (float or double).  */
template <typename T>
class deconvolution
{
  /* Size of Lanczos kernel.  */
  static constexpr int lanczos_a = 3;
public:
  /* Supported deconvolution modes.  */
  enum mode
  {
    /* Blur the image using the MTF.  */
    blur,
    /* Sharpen the image using Weiner filter.  */
    sharpen,
    /* Sharpen the image using Richardson-Lucy deconvolution.  */
    richardson_lucy_sharpen,
    /* Same as blur.  */
    blur_deconvolution
  };
  /* Set up deconvolution for given MTF and MTF_SCALE.
     SNR specifies signal to noise ratio for Weiner filter.
     SIGMA specifies dampening parameter for Richardson-Lucy.
     MAX_THREADS specifies number of threads.
     MODE is deconvolution mode.
     ITERATIONS specifies number of iterations for Richardson-Lucy.
     SUPERSAMPLE specifies supersampling factor.  */
  deconvolution (mtf *mtf, luminosity_t mtf_scale,
		 luminosity_t snr, luminosity_t sigma, int max_threads,
                 enum mode mode, int iterations, int supersample);
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
  /* Size of taping along edges (in enlarged tile).  */
  int m_taper_size = 0;
  /* Size of original tile being sharpened (including borders).  */
  int m_tile_size = 1;
  /* Size of enlarged tile being sharpened (including borders).  */
  int m_enlarged_tile_size = 1;
  /* Size of the FFT problem.  */
  int m_fft_size = 0;
  /* Supersampling factor. */
  int m_supersample = 1;
  /* Kernel for bluring or sharpening.  */
  fft_unique_ptr<T> m_blur_kernel = nullptr;

  /* True if Richardson-Lucy deconvolution is used.  */
  bool m_richardson_lucy = false;
  /* Sigma parameter for Richardson-Lucy.  */
  T m_sigma = 0;
  /* Number of iterations for Richardson-Lucy.  */
  int m_iterations = 0;

  /* Weights of edge tapering.  */
  std::vector<T, fft_allocator<T>> m_weights;

  /* Lanczos kernels for resampling.  */
  std::vector<T, fft_allocator<T>> m_lanczos_kernels;

  /* FFT plans.  */
  fft_plan<T> m_plan_2d_inv, m_plan_2d;
  /* True if plans exists.  */
  bool m_plans_exists = false;

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
  deconvolution<DT> d (scanner_mtf.get (), sharpen.scanner_mtf_scale,
                   sharpen.scanner_snr, sharpen.richardson_lucy_sigma,
                   nthreads, mode, sharpen.richardson_lucy_iterations,
		   sharpen.supersample);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution<DT>::sharpen)
        progress->set_task ("deconvolution sharpening (Weiner filter)",
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

              /* Do mirroring to avoid sharp edge at the border of image.  */
              if (px < 0)
                px = -px;
              if (py < 0)
                py = -py;
              if (px >= width)
                px = width - (px - width) - 1;
              if (py >= height)
                py = height - (py - height) - 1;
              px = std::clamp (px, 0, width - 1);
              py = std::clamp (py, 0, height - 1);
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

/* Deconvolution worker for rgbdata and related types. Sharpen DATA to OUT.
   DATA are accessed using GETDATA function and PARAM can be used to pass extra
   data around. SHARPEN specifies sharpening parameters. PROGRESS is progress
   info. PARALLEL is true if parallel execution is requested. O is output type
   name, MEM_O is memory output type, T is data type name, P is extra
   bookkeeping parameter type. DT is a type to do deconvolution in.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param), typename DT>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
		const sharpen_parameters &sharpen,
		progress_info *progress,
		bool parallel = true)
{
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
  deconvolution<DT> d (scanner_mtf.get (), sharpen.scanner_mtf_scale,
		   sharpen.scanner_snr, sharpen.richardson_lucy_sigma, nthreads * 3, mode,
		   sharpen.richardson_lucy_iterations,
		   sharpen.supersample);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution<DT>::blur)
        progress->set_task ("deconvolution blurring", xtiles * ytiles);
      else if (mode == deconvolution<DT>::sharpen)
        progress->set_task ("deconvolution sharpening (weiner filter)", xtiles * ytiles);
      else
        progress->set_task ("deconvolution sharpening (richardson-lucy)", xtiles * ytiles);
    }
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared (width, height,d,progress,out,param,parallel,data) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancelled ())
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

	      /* Do mirroring to avoid sharp edge at the border of image.  */
	      if (px < 0)
		px = -px;
	      if (py < 0)
		py = -py;
	      if (px >= width)
		px = width - (px - width) - 1;
	      if (py >= height)
		py = height - (py - height) - 1;
	      px = std::clamp (px, 0, width - 1);
	      py = std::clamp (py, 0, height - 1);
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
                    3 * id, xx + d.get_border_size (), yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].green = d.get_pixel (
                    3 * id + 1, xx + d.get_border_size (), yy + d.get_border_size ());
                out[(y + yy) * width + x + xx].blue = d.get_pixel (
                    3 * id + 2, xx + d.get_border_size (), yy + d.get_border_size ());
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

/* Auto-select the type for deconvolution of RGB data.
   O is output type, MEM_O is memory output type, T is data type, P is extra
   bookkeeping parameter type.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int_point_t p, int width, P param)>
nodiscard_attr bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
		const sharpen_parameters &sharpen, progress_info *progress,
		bool parallel = true)
{
  /* For many iterations use double; otherwise float is good and faster.  */
  if (sharpen.mode != sharpen_parameters::richardson_lucy_deconvolution
      || sharpen.richardson_lucy_iterations < 300)
    return deconvolve_rgb<O, mem_O, T, P, getdata, float>(out, data, param,
							 width, height, sharpen,
							 progress, parallel);
  else
    return deconvolve_rgb<O, mem_O, T, P, getdata, double>(out, data, param,
							  width, height,
							  sharpen, progress,
							  parallel);
}

}
#endif
