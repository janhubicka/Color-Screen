#ifndef PRECOMPUTED_FUNCTION_H
#define PRECOMPUTED_FUNCTION_H
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "base.h"

static inline double
my_floor (double x)
{
  return floor (x);
}
static inline float
my_floor (float x)
{
  return floorf (x);
}

/* Lookup table defined 1d function.  */

template <typename T> class precomputed_function
{
public:
  constexpr
  precomputed_function ()
      : m_min_x (0), m_max_x (0), m_entries (0)
  {
  }
  void
  set_range (T min, T max)
  {
    m_min_x = min;
    m_max_x = max;
  }
  constexpr
  precomputed_function (T min_x, T max_x)
      : m_min_x (min_x), m_max_x (max_x), m_entries (0)
  {
  }
  /* Constructor based on a known table of LEN values rangling from MIN_X to
     MAX_X.  */
  precomputed_function (T min_x, T max_x, const T *y, int len)
      : m_min_x (min_x), m_max_x (max_x)
  {
    init_by_y_values (y, len);
  }
  /* Construct linear interpolation between known X and Y values.  */
  precomputed_function (T min_x, T max_x, int len, const T *x, const T *y,
                        int npoints)
      : m_min_x (min_x), m_max_x (max_x)
  {
    /* Sanitize input. */
    if (m_min_x < x[0])
      m_min_x = x[0];
    if (m_max_x > x[npoints - 1])
      m_max_x = x[npoints - 1];
    if (m_min_x >= m_max_x)
      m_max_x = m_min_x + 1;
    /* If there are only 2 npoints we represent linear function.  */
    if (npoints <= 2)
      {
        m_entries = 1;
        len = 2;
      }
    T *yy = (T *)malloc (sizeof (entry) * len);
    T step = (m_max_x - m_min_x) / (T)(len - 1);

    /* If there is no control point just define function as f(x)=x.  */
    if (!npoints)
      {
        for (int i = 0; i < len; i++)
          yy[i] = m_min_x + i * step;
      }
    /* If there is one control point, use f(x) = x + c.  */
    else if (npoints == 1)
      {
        for (int i = 0; i < len; i++)
          yy[i] = m_min_x + i * step + y[0] - x[0];
      }
    else
      for (int i = 0, p = 0; i < len; i++)
        {
          T xx = m_min_x + i * step;
          while (p < npoints - 1 && x[p + 1] < xx)
            p++;
          yy[i] = y[p] + (y[p + 1] - y[p]) * (xx - x[p]) / (x[p + 1] - x[p]);
        }
    init_by_y_values (yy, len);
    free (yy);
  }

  /* Construct linear interpolation between known X and Y values but organized
     in single array of pairs instead of two separated arrays.  */
  precomputed_function (T min_x, T max_x, int len, const T table[][2],
                        int npoints)
      : m_min_x (min_x), m_max_x (max_x)
  {
    /* Sanitize input. */
    if (m_min_x < table[0][0])
      m_min_x = table[0][0];
    if (m_max_x > table[npoints - 1][0])
      m_max_x = table[npoints - 1][0];
    if (m_min_x >= m_max_x)
      m_max_x = m_min_x + 1;
    /* If there are only 2 npoints we represent linear function.  */
    if (npoints <= 2)
      {
        m_entries = 1;
        len = 2;
      }
    T *yy = (T *)malloc (sizeof (entry) * len);
    T step = (m_max_x - m_min_x) / (T)(len - 1);
    /* If there is no control point just define function as f(x)=x.  */
    if (!npoints)
      {
        for (int i = 0; i < len; i++)
          yy[i] = m_min_x + i * step;
      }
    /* If there is one control point, use f(x) = x + c.  */
    else if (npoints == 1)
      {
        for (int i = 0; i < len; i++)
          yy[i] = m_min_x + i * step + table[0][0] - table[0][0];
      }
    else
      for (int i = 0, p = 0; i < len; i++)
        {
          T xx = m_min_x + i * step;
          while (p < npoints - 1 && table[p + 1][0] < xx)
            p++;
          yy[i] = table[p][1]
                  + (table[p + 1][1] - table[p][1]) * (xx - table[p][0])
                        / (table[p + 1][0] - table[p][0]);
        }
    init_by_y_values (yy, len);
    free (yy);
  }

  ~precomputed_function ()
  {
    if (m_entries)
      free (m_entry);
  }

  /* Return f(x).  */
  T pure_attr
  apply (T x) const
  {
    int index = my_floor ((x - m_min_x) * m_step_inv);
    index = std::max (std::min (index, m_entries - 1), 0);
    return m_entry[index].add + m_entry[index].slope * x;
  }

  /* Determine inverse.  Works only for monotone functions.  */
  T pure_attr
  invert (T y) const
  {
    unsigned int min = 0;
    unsigned int max = m_entries;
    while (max != min)
      {
        unsigned int ix = (min + max) / 2;
        T xx = m_min_x + ix * m_step;
        T val = m_entry[ix].add + m_entry[ix].slope * xx;
        T val2 = val + m_entry[ix].slope * m_step;
        if (!increasing)
          std::swap (val, val2);
        if (val <= y && y <= val2)
          min = max = ix;
        else if ((val < y) ^ increasing)
          max = ix;
        else if (min != ix)
          min = ix;
        else
          break;
      }
    double ret = m_entry[min].slope
                     ? (y - m_entry[min].add) / m_entry[min].slope
                     : m_min_x + min * m_step;

    /* Verify that inverse works.  This also crashes if the function is not
       monotone.  */
    if (debug && fabs (apply (ret) - y) > epsilon)
      {
        printf ("%i %i:%f...%i:%f min:%i (%f..%f) ret:%f %f should be %f\n",
                increasing, 0, m_min_x, m_entries - 1, m_max_x, min,
                m_min_x + min * m_step, m_min_x + (min + 1) * m_step, ret,
                apply (ret), y);
        abort ();
      }
    return ret;
  }

  void
  init_by_y_values (const T *y, int len)
  {
    if (len < 2)
      abort ();
    m_entries = len - 1;
    m_entry = (struct entry *)malloc (sizeof (entry) * m_entries);
    if (!m_entries)
      return;
    m_step = (m_max_x - m_min_x) / (T)(len - 1);
    m_step_inv = 1 / m_step;
    for (int i = 0; i < len - 1; i++)
      {
        T xleft = m_min_x + i * m_step;
        m_entry[i].slope = (y[i + 1] - y[i]) * m_step_inv;
        m_entry[i].add = y[i] - xleft * m_entry[i].slope;
      }
    increasing = y[0] < y[len - 1];
  }

private:
  const T epsilon = 0.001;
  T m_min_x, m_max_x, m_step, m_step_inv;
  int m_entries;
  struct entry
  {
    T slope, add;
  } *m_entry;
  bool increasing;
  static const bool debug = colorscreen_checking;
};
#endif
