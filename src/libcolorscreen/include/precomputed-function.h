#ifndef PRECOMPUTED_FUNCTION_H
#define PRECOMPUTED_FUNCTION_H
#include <memory.h>

/* Lookup table defined 1d function.  */

template<typename T> class precomputed_function
{
  public:

  /* Constructor based on a known table of LEN values rangling from MIN_X to MAX_X.  */
  precomputed_function<T> (T min_x, T max_x, T *y, int len)
  : m_min_x (min_x), m_max_x (max_x), m_step_inv ((T)len / ((max_x - min_x))), m_entries (len - 1)
    {
      m_entry = (struct entry *)malloc (sizeof (entry) * m_entries);
      if (!m_entries)
	return;
      T step = (max_x - min_x) / (T)len;
      for (int i = 0; i < len - 1; i++)
	{
	  T xleft = min_x + i * step;
	  m_entry[i].slope = (y[i+1] - y[i]) * m_step_inv;
	  m_entry[i].add = y[i] - xleft * m_entry[i].slope;
	}
      increasing = y[0] < y[len];
    }

  ~precomputed_function<T> ()
    {
      if (m_entries)
	free (m_entry);
    }

  /* Return f(x).  */
  T
  apply (T x)
    {
      int index = (x - m_min_x) * m_step_inv;
      index = std::max (std::min (index, m_entries - 1), 0);
      return m_entry[index].add + m_entry[index].slope * x;
    }

  /* Determine inverse.  Works only for monotone functions.  */
  T
  invert (T y)
  {
    T min = m_min_x;
    T max = m_max_x;
    while (true)
      {
	T xx = (min + max) * (T)0.5;
	T ap = apply (xx);
	if (fabs (ap - y) < epsilon /*|| max - min < epsilon*/)
	  {
	    //printf ("%f %f %f %i\n", y, ap, xx, increasing);
	    return xx;
	  }
	if ((ap > y) ^ increasing)
	  {
	    if (max == xx)
	      return xx;
	    max = xx;
	  }
	else
	  {
	    if (min == xx)
	      return xx;
	    min = xx;
	  }
      }
  }

private:
  const T epsilon = 0.000001;
  T m_min_x, m_max_x, m_step_inv;
  int m_entries;
  struct entry {
    T slope, add;
  } *m_entry;
  bool increasing;
};


#endif
