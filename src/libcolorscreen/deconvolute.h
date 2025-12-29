#ifndef DECONVOLUTE_H
#define DECONVOLUTE_H
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/progress-info.h"
#include <fftw3.h>
#include <mutex>
#include <omp.h>
namespace colorscreen
{
extern std::mutex fftw_lock;

int deconvolute_border_size (precomputed_function<luminosity_t> *mtf);

class deconvolution
{
public:
  enum mode
  {
    blur,
    sharpen,
    richardson_lucy_sharpen
  };
  /* Set up deconvolution for given MTF. SNR specifies signal to noise ratio
     for Weiner filter.  MAX_THREADS specifies number of threads.  */
  deconvolution (precomputed_function<luminosity_t> *mtf, luminosity_t snr, int max_threads,
                 enum mode = sharpen /*richardson_lucy_sharpen*/, int iterations = 50);
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
    m_data[threadid].tile[y * m_mem_tile_size + x] = val;
  }

  /* Apply sharpening/blurring to the kernel.  */
  void process_tile (int thread_id);

  /* Get pixel from given thread.  */
  deconvolution_data_t
  get_pixel (int threadid, int x, int y)
  {
    return m_data[threadid].tile[y * m_mem_tile_size + x];
  }


private:
  /* Size of border that is not sharpened correctly  */
  int m_border_size;
  /* Size of taping along edges.  */
  int m_taper_size;
  /* Size of tile being sharpened (including borders  */
  int m_tile_size;
  /* Size of the FFT problem  */
  int m_fft_size;
  /* Size of tile in memory (may be bigger if we do mirringing)  */
  int m_mem_tile_size;
  /* Kernel for bluring or sharpening.  */
  fftw_complex *m_blur_kernel;

  bool m_richardson_lucy;
  deconvolution_data_t m_snr;
  int m_iterations;

  /* Weights of edge tapering.  */
  std::vector<deconvolution_data_t> m_weights;

  fftw_plan m_plan_2d_inv, m_plan_2d;
  bool m_plans_exists;

  /* Plans used for FFT calclation.  */
  struct tile_data
  {
    fftw_complex *in;
    std::vector<deconvolution_data_t> tile;
    std::vector<deconvolution_data_t> ratios;
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
deconvolute (mem_O *out, T data, P param, int width, int height,
             precomputed_function<luminosity_t> *mtf,
	     luminosity_t snr,
	     progress_info *progress,
             bool parallel = true)
{
  int nthreads = parallel ? omp_get_max_threads () : 1;
  deconvolution d (mtf, snr, nthreads);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    progress->set_task ("Deconvolution sharpening", xtiles * ytiles);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared (width, height,d,progress,out,param,parallel,data) if (parallel)
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
              O pixel = 0;
              if (x + xx - d.get_border_size () >= 0
                  && x + xx - d.get_border_size () < width
                  && y + yy - d.get_border_size () >= 0
                  && y + yy - d.get_border_size () < height)
                pixel = getdata (data, x + xx - d.get_border_size (),
                                 y + yy - d.get_border_size (), width, param);
              d.put_pixel (id, xx, yy, pixel);
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
deconvolute_rgb (mem_O *out, T data, P param, int width, int height,
		 precomputed_function<luminosity_t> *mtf, luminosity_t snr, progress_info *progress,
		 bool parallel = true)
{
  int nthreads = parallel ? omp_get_max_threads () : 1;
  deconvolution d (mtf, snr, nthreads);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    progress->set_task ("Deconvolution sharpening", xtiles * ytiles);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared (width, height,d,progress,out,param,parallel,data) if (parallel)
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
	      deconvolution::deconvolution_data_t pixel = 0;
              if (x + xx - d.get_border_size () >= 0
                  && x + xx - d.get_border_size () < width
                  && y + yy - d.get_border_size () >= 0
                  && y + yy - d.get_border_size () < height)
                pixel = getdata (data, x + xx - d.get_border_size (),
                                 y + yy - d.get_border_size (), width, param).red;
              d.put_pixel (id, xx, yy, pixel);
            }
        d.process_tile (id);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].red = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());

        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
	      deconvolution::deconvolution_data_t pixel = 0;
              if (x + xx - d.get_border_size () >= 0
                  && x + xx - d.get_border_size () < width
                  && y + yy - d.get_border_size () >= 0
                  && y + yy - d.get_border_size () < height)
                pixel = getdata (data, x + xx - d.get_border_size (),
                                 y + yy - d.get_border_size (), width, param).green;
              d.put_pixel (id, xx, yy, pixel);
            }
        d.process_tile (id);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].green = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());

        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
	      deconvolution::deconvolution_data_t pixel = 0;
              if (x + xx - d.get_border_size () >= 0
                  && x + xx - d.get_border_size () < width
                  && y + yy - d.get_border_size () >= 0
                  && y + yy - d.get_border_size () < height)
                pixel = getdata (data, x + xx - d.get_border_size (),
                                 y + yy - d.get_border_size (), width, param).blue;
              d.put_pixel (id, xx, yy, pixel);
            }
        d.process_tile (id);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
            if (y + yy < height && x + xx < width)
              out[(y + yy) * width + x + xx].blue = d.get_pixel (
                  id, xx + d.get_border_size (), yy + d.get_border_size ());

        if (progress)
          progress->inc_progress ();
      }
  return true;
}
}
#endif
