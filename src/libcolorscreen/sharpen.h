#ifndef SHAPREN_H
#define SHAPREN_H
#include "gaussian-blur.h"

/* Kernel for "sharpening" with with either radius or amount being zero.
   Flattened so avoid doing unnecesary stuff.  */

template<typename O, typename mem_O, typename T,typename P, O (*getdata)(T data, int x, int y, int width, P param)>
flatten_attr void
non_sharpen(mem_O *out, T data, P param, int width, int height, progress_info *progress)
{
#pragma omp parallel for shared(progress,out,width, height, param, data) default(none)
  for (int y = 0; y < height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < width; x++)
	  out[y * width + x] = (mem_O) getdata (data, x, y, width, param);
    }
}
template<typename O, typename mem_O, typename T,typename P, O (*getdata)(T data, int x, int y, int width, P param)>
inline void
do_sharpen_loop(mem_O *out, O *hblur, int clen, luminosity_t *rotated_cmatrix, T data,int width, int y, luminosity_t amount, P param)
{
  for (int x = 0; x < width; x++)
    {
      O sum = hblur[0 * width + x] * rotated_cmatrix[0];
      for (int d = 1; d < clen; d++)
	sum += hblur[d * width + x] * rotated_cmatrix[d];
      O orig = getdata (data, x, y, width, param);
      out[y * width + x] = (mem_O) (orig + (orig - sum) * amount);
    }
}

/* Kernel for actual shaprening.
   Flattened so avoid doing unnecesary stuff.  */
template<typename O, typename mem_O, typename T,typename P, O (*getdata)(T data, int x, int y, int width, P param)>
flatten_attr void
do_sharpen(mem_O *out, T data, P param, int width, int height, int clen, luminosity_t *cmatrix, luminosity_t amount, progress_info *progress)
{
#pragma omp parallel shared(progress,out,clen,cmatrix,width, height, amount, param, data) default(none)
  {
    O *hblur = (O *)calloc (width * clen, sizeof (O));
    luminosity_t *rotated_cmatrix = (luminosity_t *)malloc (clen * sizeof (luminosity_t));
#ifdef _OPENMP
    int tn = omp_get_thread_num ();
    int threads = omp_get_max_threads ();
#else
    int tn = 0;
    int threads = 1;
#endif
    int ystart = tn * height / threads;
    int yend = (tn + 1) * height / threads - 1;
    O *line = (O *)malloc (width * sizeof (O));

    for (int d = -clen/2; d < clen/2 - 1; d++)
      {
	int yp = ystart + d;
	int tp = (yp + clen) % clen;
	if (yp < 0 || yp > height)
	  memset ((void *)(hblur + tp * width), 0, sizeof (O) * width);
	else
	{
	  for (int x = 0; x < width ; x++)
	    line[x] =getdata (data, x, yp, width, param);
	  fir_blur::blur_horisontal<O, T> (hblur + tp * width, line, width, clen, cmatrix);
	}
      }
    if (!progress || !progress->cancel_requested ())
      for (int y = ystart; y <= yend; y++)
	{
          if (progress && progress->cancel_requested ())
	    break;
	  if (y + clen / 2 - 1 < height)
	    for (int x = 0; x < width ; x++)
	      line[x] =getdata (data, x, y + clen / 2 - 1, width, param);
	  if (y + clen / 2 - 1 < height)
	    switch (clen)
	      {
	      case 1:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 3:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 5:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 7:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 9:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 11:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 13:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 15:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 17:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 19:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 21:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 23:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 25:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 27:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 29:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      case 31:fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); break;
	      default:
	        if (clen < 32 || !(clen & 1))
		  abort ();/*__builtin_unreachable ();*/
	        fir_blur::blur_horisontal<O, T> (hblur + ((y + clen / 2 - 1 + clen) % clen) * width, line, width, clen, cmatrix); 
	      }
	  else
	    memset ((void*)(hblur + ((y + clen / 2 - 1 + clen) % clen) * width), 0, sizeof (O) * width);
	  for (int d = 0; d < clen; d++)
	    rotated_cmatrix[(y + d - clen / 2 + clen) % clen] = cmatrix[d];
	  /* Specialize the inner loop for small clens.  */
	  switch (clen)
	    {
	      case 1: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 3: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 5: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 7: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 9: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 11: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 13: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 15: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 17: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 19: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 21: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 23: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 25: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 27: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 29: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      case 31: do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param); break;
	      default:
	       if (clen < 32 || !(clen & 1))
		 abort ();/*__builtin_unreachable ();*/
	       do_sharpen_loop<O, mem_O, T, P, getdata> (out, hblur, clen, rotated_cmatrix, data, width, y, amount, param);
	       break;
	    }
	  if (progress)
	     progress->inc_progress ();
	}
    free (rotated_cmatrix);
    free (hblur);
    free (line);
  }
}

/* Sharpening worker. Sharpen DATA to OUT which both has dimensions WIDTH*HEIGHT.
   DATA are accessed using getdata function and PARAM can be used to pass extra data around.
   RADIOUS and AMOUNT are usual parameters of unsharp masking.

   For performance reasons do not use lambda function since it won't get inlined.
     O is output type name, T is data type name, P is extra bookeeping parameter type.  */
template<typename O, typename mem_O, typename T,typename P, O (*getdata)(T data, int x, int y, int width, P param)>
bool
sharpen(mem_O *out, T data, P param, int width, int height, luminosity_t radius, luminosity_t amount, progress_info *progress)
{
  luminosity_t *cmatrix;
  /* Fast path if we do no sharpening.  */
  if (!radius || !amount)
    {
      if (progress)
	progress->set_task ("converting to linear HDR image", height);
      non_sharpen<O,mem_O,T,P,getdata> (out, data, param, width, height, progress);
      if (progress && progress->cancelled ())
	return false;
      return true;
    }
  int clen = fir_blur::gen_convolve_matrix (radius, &cmatrix);
  if (!clen)
    return false;
  if (progress)
    progress->set_task ("sharpening", height);
  do_sharpen<O,mem_O,T,P,getdata> (out, data, param, width, height, clen, cmatrix, amount, progress);

  free (cmatrix);
  if (progress && progress->cancelled ())
    return false;
  return true;
}
#endif
