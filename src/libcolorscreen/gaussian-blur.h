#ifndef GAUSIAN_BLUR_H
#define GAUSIAN_BLUR_H

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
