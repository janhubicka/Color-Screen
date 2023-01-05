#ifndef GAUSIAN_BLUR_H
#define GAUSIAN_BLUR_H
#include <functional>

#define BLUR_EPSILON 0.000001

/* Finite Impulse Response (FIR)  */

class fir_blur
{
public:
  static int
  gen_convolve_matrix (luminosity_t sigma, luminosity_t **cmatrix)
    {
      int    clen;
      luminosity_t *cmatrix_p;

      clen = convolve_matrix_length (sigma);

      *cmatrix  = (luminosity_t *) malloc (sizeof (luminosity_t) * clen);
      if (!cmatrix)
	return 0;
      cmatrix_p = *cmatrix;

      if (clen == 1)
	cmatrix_p [0] = 1;
      else
	{
	  int    i;
	  luminosity_t sum = 0;
	  int    half_clen = clen / 2;

	  for (i = 0; i < clen; i++)
	    {
	      cmatrix_p [i] = gaussian_func_1d (i - half_clen, sigma);
	      sum += cmatrix_p [i];
	    }

	  for (i = 0; i < clen; i++)
	    cmatrix_p [i] /= sum;
	}
      return clen;
    }

  /* Helper for bluring.  Apply horisontal blur on DATA line Y of given WIDTH and write to OUT.
     CLEN and CMATRIX are precomputed using code above.
     For performance reasons do not use lambda function since it won't get inlined.  */
  template<typename T,typename P, luminosity_t (*getdata)(T data, int x, int y, int width, P param)>
  inline static void
  blur_horisontal(luminosity_t *out, T data, P param, int y, int width, int clen, luminosity_t *cmatrix)
  {
    if (width < clen)
    {
      for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
      {
	luminosity_t sum = 0;
	for (int d = std::max (- clen / 2, -x); d < std::min (clen / 2, width - x); d++)
	  sum += cmatrix[d + clen / 2] * getdata (data, x + d, y, width, param);
	out[x] = sum;
      }
      return;
    }
     
    for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
      {
	luminosity_t sum = 0;
	for (int d = -x; d < clen / 2; d++)
	  sum += cmatrix[d + clen / 2] * getdata (data, x + d, y, width, param);
	out[x] = sum;
      }
    for (int x = clen / 2; x < width - clen / 2; x++)
      {
	luminosity_t sum = 0;
	for (int d = - clen / 2; d < clen / 2; d++)
	  sum += cmatrix[d + clen / 2] * getdata (data, x + d, y, width, param);
	out[x] = sum;
      }
    for (int x = width - clen / 2; x < width; x++)
      {
	luminosity_t sum = 0;
	for (int d = - clen / 2; d < width - x; d++)
	  sum += cmatrix[d + clen / 2] * getdata (data, x + d, y, width, param);
	out[x] = sum;
      }
  }
private:

  static int
  convolve_matrix_length (luminosity_t sigma)
    {
      /* an arbitrary precision */
      int clen = sigma > BLUR_EPSILON ? ceil (sigma * 6.5) : 1;
      clen = clen + ((clen + 1) % 2);
      return clen;
    }

  static luminosity_t
  gaussian_func_1d (luminosity_t x,
		    luminosity_t sigma)
    {
      return (1.0 / (sigma * sqrt (2.0 * M_PI))) *
	      exp (-(x * x) / (2.0 * sigma * sigma));
    }
};

#endif
