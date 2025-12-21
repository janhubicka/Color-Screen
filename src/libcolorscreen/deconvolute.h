#ifndef DECONVOLUTE_H
#define DECONVOLUTE_H
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
  deconvolution (precomputed_function<luminosity_t> *mtf);
  typedef double deconvolution_data_t;
  ~deconvolution () { delete m_blur_kernel; }

  void blur_tile (deconvolution_data_t *d);
  void sharpen_tile (deconvolution_data_t *d);
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

private:
  int m_border_size;
  int m_tile_size;
  int m_fft_size;
  fftw_complex *m_blur_kernel;
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
  deconvolution d (mtf);

  int xtiles
      = (width + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  int ytiles
      = (height + d.get_basic_tile_size () - 1) / d.get_basic_tile_size ();
  std::vector<deconvolution::deconvolution_data_t> tile (
      d.get_tile_size_with_borders () * d.get_tile_size_with_borders ());
  if (progress)
    progress->set_task ("Deconvolution sharpening", xtiles * ytiles);
//#pragma omp parallel for shared(progress,out,width, height, param, data,d,tile) collapse(2) default(none) if (parallel)
  for (int y = 0; y < height; y += d.get_basic_tile_size ())
    for (int x = 0; x < width; x += d.get_basic_tile_size ())
      {
        if (progress && progress->cancelled ())
	  continue;
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
              tile[yy * d.get_tile_size_with_borders () + xx] = pixel;
            }
        d.sharpen_tile (tile.data ());
        for (int yy = 0; yy < d.get_basic_tile_size (); yy++)
          for (int xx = 0; xx < d.get_basic_tile_size (); xx++)
	    if (y + yy < height && x + xx < width)
	      out[(y + yy) * width + x + xx]
		  = tile[(yy + d.get_border_size ())
			     * d.get_tile_size_with_borders ()
			 + xx + d.get_border_size ()];
        if (progress)
          progress->inc_progress ();
      }
  return true;
}
}
#endif
