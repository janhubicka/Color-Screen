#ifndef DECONVOLUTE_H
#define DECONVOLUTE_H
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/progress-info.h"
#include "include/render-parameters.h"
#include "mtf.h"
#include <mutex>
#include <omp.h>
namespace colorscreen
{

class deconvolution
{
static constexpr const double lanczos_a = 3;
public:
  enum mode
  {
    blur,
    sharpen,
    richardson_lucy_sharpen,
    blur_deconvolution
  };
  /* Set up deconvolution for given MTF and MTF_SCALE.
     SNR specifies signal to noise ratio
     for Weiner filter.  MAX_THREADS specifies number of threads.  */
  deconvolution (mtf *mtf, luminosity_t mtf_scale,
		 luminosity_t snr, luminosity_t sigma, int max_threads,
                 enum mode, int iterations, int supersample);
  typedef double deconvolution_data_t;
  ~deconvolution ();

  /* Size of tile processed without borders.  */
  int
  get_basic_tile_size () const
  {
    return m_tile_size - m_border_size * 2;
  }

  /* Size of tile processed with borders.  */
  int
  get_tile_size_with_borders () const
  {
    return m_tile_size;
  }

  /* Size of border.  */
  int
  get_border_size () const
  {
    return m_border_size;
  }

  /* Allocate memory for tile for given thread.  */
  void init (int thread_id);

  /* Put pixel to given thread  */
  void
  put_pixel (int threadid, int x, int y, deconvolution_data_t val)
  {
    m_data[threadid].tile[y * m_tile_size + x] = val;
  }

  /* Get pixel from given thread.  */
  deconvolution_data_t
  get_pixel (int threadid, int x, int y) const
  {
    return m_data[threadid].tile[y * m_tile_size + x];
  }

  /* Apply sharpening/blurring to the kernel.  */
  void process_tile (int thread_id);


private:

  /* Put pixel to given thread  */
  void
  put_enlarged_pixel (int threadid, int x, int y, deconvolution_data_t val)
  {
    (*m_data[threadid].enlarged_tile)[y * m_enlarged_tile_size + x] = val;
  }

  /* Get pixel from given thread.  */
  deconvolution_data_t
  get_enlarged_pixel (int threadid, int x, int y) const
  {
    return (*m_data[threadid].enlarged_tile)[y * m_enlarged_tile_size + x];
  }
  /* Size of border that is not sharpened correctly (in original tile)  */
  int m_border_size;
  /* Size of taping along edges (in enlarged tile).  */
  int m_taper_size;
  /* Size of original tile being sharpened (including borders)  */
  int m_tile_size;
  /* Size of enlarged tile being sharpened (including borders)  */
  int m_enlarged_tile_size;
  /* Size of the FFT problem  */
  int m_fft_size;
  /* Supersampling */
  int m_supersample;
  /* Kernel for bluring or sharpening.  */
  fftw_complex *m_blur_kernel;

  bool m_richardson_lucy;
  deconvolution_data_t m_sigma;
  int m_iterations;

  /* Weights of edge tapering.  */
  std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> m_weights;

  std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> m_lanczos_kernels;

  fftw_plan m_plan_2d_inv, m_plan_2d;
  bool m_plans_exists;

  /* Plans used for FFT calclation.  */
  struct tile_data
  {
    fftw_complex *in;
    std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> tile;
    std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> *enlarged_tile;
    std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> enlarged_tile_data;
    std::vector<deconvolution_data_t,fftw_allocator<deconvolution_data_t>> ratios;
    bool initialized;
  };
  std::vector<tile_data> m_data;
};

/* Deconvolution worker. Sharpen DATA to OUT which both has dimensions
   WIDTH*HEIGHT. DATA are accessed using getdata function and PARAM can be used
   to pass extra data around. MTF is MTF of scanner. O is output type name
   (must be convertible to double), T is data type name, P is extra bookeeping
   parameter type.  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int x, int y, int width, P param)>
bool
deconvolve (mem_O *out, T data, P param, int width, int height,
            const sharpen_parameters &sharpen, progress_info *progress,
            bool parallel = true)
{
  int nthreads = parallel ? omp_get_max_threads () : 1;
  deconvolution::mode mode;
  if (progress)
    progress->set_task ("initializing MTF based deconvolution", 1);
  switch (sharpen.mode)
    {
    case sharpen_parameters::richardson_lucy_deconvolution:
      mode = deconvolution::richardson_lucy_sharpen;
      break;
    case sharpen_parameters::wiener_deconvolution:
      mode = deconvolution::sharpen;
      break;
    case sharpen_parameters::blur_deconvolution:
      mode = deconvolution::blur;
      break;
    default:
      abort ();
    }
  mtf *scanner_mtf = mtf::get_mtf (sharpen.scanner_mtf, progress);
  deconvolution d (scanner_mtf, sharpen.scanner_mtf_scale,
                   sharpen.scanner_snr, sharpen.richardson_lucy_sigma,
                   nthreads, mode, sharpen.richardson_lucy_iterations,
		   sharpen.supersample);
  mtf::release_mtf (scanner_mtf);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution::sharpen)
        progress->set_task ("Deconvolution sharpening (Weiner filter)",
                            xtiles * ytiles);
      else
        progress->set_task ("Deconvolution sharpening (Richardson-Lucy)",
                            xtiles * ytiles);
    }
#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(  \
        width, height, d, progress, out, param, parallel, data) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancelled ())
          continue;
        int id = parallel ? omp_get_thread_num () : 0;
        d.init (id);
        // printf ("%i %i\n",x,y);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              int px = x + xx - d.get_border_size ();
              int py = y + yy - d.get_border_size ();

              /* Do mirroring to avoid sharp edge at the border of image  */
              if (px < 0)
                px = -px;
              if (py < 0)
                py = -py;
              if (px >= width)
                px = width - (px - width) - 1;
              if (py >= height)
                px = height - (px - height) - 1;
              px = std::clamp (px, 0, width - 1);
              py = std::clamp (py, 0, height - 1);
              d.put_pixel (id, xx, yy, getdata (data, px, py, width, param));
            }
        d.process_tile (id);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx] = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());
        if (progress)
          progress->inc_progress ();
      }
  return true;
}
/* Deconvolution worker for rgbdata and related types (having red, green and blue fields)  */
template <typename O, typename mem_O, typename T, typename P,
          O (*getdata) (T data, int x, int y, int width, P param)>
bool
deconvolve_rgb (mem_O *out, T data, P param, int width, int height,
		const sharpen_parameters &sharpen,
		progress_info *progress,
		bool parallel = true)
{
  int nthreads = parallel ? omp_get_max_threads () : 1;
  deconvolution::mode mode;
  if (progress)
    progress->set_task ("initializing MTF based deconvolution", 1);
  switch (sharpen.mode)
    {
    case sharpen_parameters::richardson_lucy_deconvolution:
      mode = deconvolution::richardson_lucy_sharpen;
      break;
    case sharpen_parameters::wiener_deconvolution:
      mode = deconvolution::sharpen;
      break;
    case sharpen_parameters::blur_deconvolution:
      mode = deconvolution::blur;
      break;
    default:
      abort ();
    }
  mtf *scanner_mtf = mtf::get_mtf (sharpen.scanner_mtf, progress);
  deconvolution d (scanner_mtf, sharpen.scanner_mtf_scale,
		   sharpen.scanner_snr, sharpen.richardson_lucy_sigma, nthreads * 3, mode,
		   sharpen.richardson_lucy_iterations,
		   sharpen.supersample);
  mtf::release_mtf (scanner_mtf);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    {
      if (mode == deconvolution::blur)
        progress->set_task ("Deconvolution blurring", xtiles * ytiles);
      if (mode == deconvolution::sharpen)
        progress->set_task ("Deconvolution sharpening (Weiner filter)", xtiles * ytiles);
      else
        progress->set_task ("Deconvolution sharpening (Richardson-Lucy)", xtiles * ytiles);
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


        // printf ("%i %i\n",x,y);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
	      O pixel = {0, 0, 0};
	      int px = x + xx - d.get_border_size ();
	      int py = y + yy - d.get_border_size ();

	      /* Do mirroring to avoid sharp edge at the border of image  */
	      if (px < 0)
		px = -px;
	      if (py < 0)
		py = -py;
	      if (px >= width)
		px = width - (px - width) - 1;
	      if (py >= height)
		px = height - (px - height) - 1;
	      px = std::clamp (px, 0, width - 1);
	      py = std::clamp (py, 0, height - 1);
              pixel = getdata (data, px, py, width, param);
              d.put_pixel (3 * id, xx, yy, pixel.red);
              d.put_pixel (3 * id + 1, xx, yy, pixel.green);
              d.put_pixel (3 * id + 2, xx, yy, pixel.blue);
            }
        d.process_tile (3 * id);
        d.process_tile (3 * id + 1);
        d.process_tile (3 * id + 2);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].red = d.get_pixel (
                  3 * id, xx + d.get_border_size (), yy + d.get_border_size ());
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].green = d.get_pixel (
                  3 * id + 1, xx + d.get_border_size (), yy + d.get_border_size ());
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].blue = d.get_pixel (
                  3 * id + 2, xx + d.get_border_size (), yy + d.get_border_size ());

        if (progress)
          progress->inc_progress ();
      }
  return true;
}

//void mtf_to_2d_psf (precomputed_function<luminosity_t> *mtf, double scale, int size, double *psf);
//int get_psf_radius (double *psf, int size);
//int deconvolute_border_size (precomputed_function<luminosity_t> *mtf);

}
#endif
