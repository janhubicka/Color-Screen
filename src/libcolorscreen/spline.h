#ifndef SPLINE_H
#define SPLINE_H
#include <stdlib.h>
#include <stdio.h>
#include "include/precomputed-function.h"

/* Cubic spline implementation.  Based on numerical recipes in C. */

template<typename T>class spline
{
public:
  /* Define spline with points X and Y.  The arrays are owned by caller but stay referenced
     by the class constructed.  */
  spline (T *x, T *y, int n)
  : m_x (x), m_y (y), m_n (n)
  {
    T p, qn, sig, un, *u;
    const T alpha = 1.0;
    const T yp1 = 3e30 * alpha;
    const T ypn = 3e30 * alpha;

    if (n < 2)
      abort ();

    u = (T *) malloc ((unsigned) (n - 1) * sizeof (T));
    m_y2 = (T *) malloc ((unsigned) (n) * sizeof (T));

    //printf ("Spline %i\n",n);
    //for (int i = 0; i < n; i++)
      //printf ("%f %f\n", x[i],y[i]);


    if (!u || !m_y2)
      {
	fprintf (stderr, "Out of memory allocating spline\n");
	abort ();
      }
    if (yp1 > (T) 0.99e30)
      m_y2[0] = u[0] = 0.0;
    else
      {
	m_y2[0] = -(T)0.5;
	u[0] = ((T)3.0 / (x[1] - x[0])) * ((y[1] - y[0]) / (x[1] - x[0]) - yp1);
      }
    for (int i = 1; i <= n - 2; i++)
      {
	sig = (x[i] - x[i - 1]) / (x[i + 1] - x[i - 1]);
	p = sig * m_y2[i - 1] + (T)2.0;
	m_y2[i] = (sig - (T)1.0) / p;
	u[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]) - (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
	u[i] = ((T)6.0 * u[i] / (x[i + 1] - x[i - 1]) - sig * u[i - 1]) / p;
      }
    if (ypn > (T)0.99e30)
      qn = un = (T)0.0;
    else
      {
	qn = (T)0.5;
	un = ((T)3.0 / (x[n - 1] - x[n - 2])) * (ypn - (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]));
      }
    m_y2[n - 1] = (un - qn * u[n - 2]) / (qn * m_y2[n - 2] + (T)1.0);
    for (int k = n - 2; k >= 0; k--)
      m_y2[k] = m_y2[k] * m_y2[k + 1] + u[k];
    //for (int k = 0; k < n; k++)
      //printf ("%f:%f %f\n",x[k],y[k], apply(x[k]));
    free (u);
  }

  ~spline ()
  {
    free (m_y2);
    /* m_x and m_y are owned by the caller.  */
  }

  /* Compute spline for given x.  */
  T
  apply (T x)
  {
    int k;
    T h, b, a;
    int klo = 0, khi = m_n - 1;

    /* TODO: For performance improvement we may cache lookup; add optional argument for that.  */

    while (khi - klo > 1)
      {
	k = (khi + klo) >> 1;
	if (m_x[k] > x)
	  khi = k;
	else
	  klo = k;
      }
    h = m_x[khi] - m_x[klo];
    if (h > (T)0.0)
      {
	a = (m_x[khi] - x) / h;
	b = (x - m_x[klo]) / h;
	return a * m_y[klo] + b * m_y[khi] + ((a * a * a - a) * m_y2[klo] +
					  (b * b * b -
					   b) * m_y2[khi]) * (h * h) * (T) (1.0 / 6.0);
      }
    else
      return m_y[khi];
  }

  /* Return precomputed function in the range MIN...MAX with STEPS entries.  */
  precomputed_function<T> *precompute(T min, T max, int steps)
  {
    T *yvals = (T *) malloc (steps * sizeof (T));
    if (!yvals)
      return NULL;
    if (min < m_x[0])
      min = m_x[0];
    if (max > m_x[m_n - 1])
      max = m_x[m_n - 1];
    for (int i = 0; i < steps; i++)
      {
	yvals[i] = apply (min + ((max - min) / (steps - 1)) * i);
	//printf ("%f %f\n", min + ((max - min) / steps) * i, yvals[i]);
      }
    precomputed_function<T> *fn = new precomputed_function<T>(min, max, yvals, steps);
    free (yvals);
    return fn;
  }
private:
  T *m_x, *m_y, *m_y2;
  int m_n;
};
#endif
