#ifndef DECONVOLUTE_H
#define DECONVOLUTE_H
#include <omp.h>
#include "include/base.h"
#include "include/color.h"
#include "include/precomputed-function.h"
#include "include/progress-info.h"
#include <fftw3.h>
#include <mutex>
namespace colorscreen
{
extern std::mutex fftw_lock;

class deconvolution
{
public:
  deconvolution (precomputed_function<luminosity_t> *mtf, int max_threads, bool sharpen = true);
  typedef double deconvolution_data_t;
  ~deconvolution ();

  void process_tile (int thread_id);
  int
  get_basic_tile_size () const
  {
    return m_tile_size - m_border_size * 2;
  }
  int
  get_tile_size_with_borders () const
  {
    return m_tile_size;
  }
  int
  get_border_size () const
  {
    return m_border_size;
  }

  void init (int thread_id);

  void
  put_pixel (int threadid, int x, int y, deconvolution_data_t val)
  {
    m_plans[threadid].tile[y * m_tile_size + x] = val;
  }
  deconvolution_data_t
  get_pixel (int threadid, int x, int y)
  {
    return m_plans[threadid].tile[y * m_tile_size + x];
  }

private:
  int m_border_size;
  int m_tile_size;
  int m_fft_size;
  fftw_complex *m_blur_kernel;
  struct fftw_plans
  {
    fftw_plan plan_2d_inv, plan_2d;
    fftw_complex *in;
    std::vector<deconvolution_data_t> tile;
    bool initialized;
  };
  std::vector <fftw_plans> m_plans;
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
             precomputed_function<luminosity_t> *mtf, progress_info *progress,
             bool parallel = true)
{
  int nthreads = parallel ? omp_get_max_threads () : 1;
  deconvolution d (mtf, nthreads);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  if (progress)
    progress->set_task ("Deconvolution sharpening", xtiles * ytiles);
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancelled ())
	  continue;
	int id = parallel ? omp_get_thread_num (): 0;
	d.init (id);
	//printf ("%i %i\n",x,y);
        for (int yy = 0; yy < d.get_tile_size_with_borders (); yy++)
          for (int xx = 0; xx < d.get_tile_size_with_borders (); xx++)
            {
              O pixel;
              if (x + xx - d.get_border_size () >= 0
                  && x + xx - d.get_border_size () < width
                  && y + yy - d.get_border_size () >= 0
                  && y + yy - d.get_border_size () < height)
                pixel = getdata (data, x + xx - d.get_border_size (),
                                 y + yy - d.get_border_size (), width, param);
              d.put_pixel(id, xx, yy, pixel);
            }
        d.process_tile (id);
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
	    if (y + yy < height && x + xx < width)
	      out[(y + yy) * width + x + xx]
		  = d.get_pixel (id, xx + d.get_border_size (), yy + d.get_border_size ());
        if (progress)
          progress->inc_progress ();
      }
  return true;
}
}
#endif
