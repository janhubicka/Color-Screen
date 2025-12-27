#ifndef GAUSIAN_BLUR_H
#define GAUSIAN_BLUR_H
#include <functional>
namespace colorscreen
{

#define BLUR_EPSILON 0.000001
#ifndef M_PI
#define M_PI            3.14159265358979323846
#endif

/* Finite Impulse Response (FIR)  */

class fir_blur
{
public:
  __attribute__ ((always_inline))
  static inline int
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

	  luminosity_t inv = 1 / sum;

	  for (i = 0; i < clen; i++)
	    cmatrix_p [i] *= inv;
	}
      return clen;
    }

  /* Helper for bluring.  Apply horisontal blur on DATA line Y of given WIDTH and write to OUT.
     CLEN and CMATRIX are precomputed using code above.
     For performance reasons do not use lambda function since it won't get inlined.
     O is output type name, T is data type name, P is extra bookeeping parameter type.  */
  template<typename O, typename T>
  inline static void
  blur_horisontal(O *out, O *in, int width, int clen, luminosity_t *cmatrix)
  {
    if (width < clen)
    {
      for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
      {
        int m = std::max (- clen / 2, -x);
	O sum = in[x + m] * cmatrix[m + clen / 2];
	for (int d = m; d < std::min (clen / 2, width - x); d++)
	  sum += in [x + d] * cmatrix[d + clen / 2];
	out[x] = sum;
      }
      return;
    }
     
    for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
      {
	O sum = in[0] * cmatrix[-x + clen / 2];
	for (int d = -x + 1; d < clen / 2; d++)
	  sum += in[x + d] * cmatrix[d + clen / 2];
	out[x] = sum;
      }
    for (int x = clen / 2; x < width - clen / 2; x++)
      {
	int m = - clen / 2;
	O sum = in[x + m] * cmatrix[m + clen / 2];
	for (int d = m + 1; d < clen / 2; d++)
	  sum += in[x + d] * cmatrix[d + clen / 2];
	out[x] = sum;
      }
    for (int x = width - clen / 2; x < width; x++)
      {
	int m = - clen / 2;
	O sum = in[x + m] * cmatrix[m + clen / 2];
	for (int d = m + 1; d < width - x; d++)
	  sum += in[x + d] * cmatrix[d + clen / 2];
	out[x] = sum;
      }
  }

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
private:
};
}
#endif
