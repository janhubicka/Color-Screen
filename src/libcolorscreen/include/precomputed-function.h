#ifndef PRECOMPUTED_FUNCTION_H
#define PRECOMPUTED_FUNCTION_H

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <memory>
#include "base.h"

namespace colorscreen
{

/* Lookup table defined 1D function with linear interpolation.

   This class stores a precomputed table of a 1D function f(x) over a specified
   range [MIN_X, MAX_X]. It provides O(1) evaluation using linear interpolation
   and O(log N) inversion for monotone functions.

   It uses std::vector for memory management and supports functional initialization.
   Moving is supported, but copying is disabled to avoid accidental overhead.  */

template <typename T> class precomputed_function
{
public:
  /* Construct an empty precomputed function.  */
  constexpr
  precomputed_function ()
      : m_min_x (0), m_max_x (0), m_step (0), m_step_inv (0), increasing (true)
  {
  }

  /* Set the range of the function to be between MIN and MAX.  */
  void
  set_range (T min, T max)
  {
    m_min_x = min;
    m_max_x = max;
  }

  /* Construct an empty function with a specified range [MIN_X, MAX_X].  */
  constexpr
  precomputed_function (T min_x, T max_x)
      : m_min_x (min_x), m_max_x (max_x), m_step (0), m_step_inv (0), increasing (true)
  {
  }

  /* Construct a function from a list of Y values starting at MIN_X and ending
     at MAX_X.  LEN specifies the number of Y values.  */
  precomputed_function (T min_x, T max_x, const T *y, int len)
      : m_min_x (min_x), m_max_x (max_x)
  {
    init_by_y_values (y, len);
  }

  /* Construct a function using an evaluation function F over the range
     [MIN_X, MAX_X].  LEN specifies the number of entries in the lookup table.
     If PARALLEL is true, initialization is performed in parallel using
     OpenMP.  */
  template <typename Func>
  precomputed_function (T min_x, T max_x, int len, Func f, bool parallel = false)
      : m_min_x (min_x), m_max_x (max_x)
  {
    init_by_function (len, f, parallel);
  }

  /* Construct by linear interpolation between known X and Y values over the
     specified range [MIN_X, MAX_X].  LEN is the number of table entries.
     NPOINTS specifies the number of control points.  */
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
    init_by_x_y_values (x, y, npoints, len);
  }

  /* Construct by linear interpolation between known points in TABLE over the
     specified range [MIN_X, MAX_X].  LEN is the number of table entries.
     NPOINTS specifies the number of control points.  */
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

    init_by_table (table, npoints, len);
  }

  /* Default move semantics.  */
  precomputed_function (precomputed_function &&) = default;
  precomputed_function &operator= (precomputed_function &&) = default;

  /* Explicitly disable copying to avoid accidental performance hits.  */
  precomputed_function (const precomputed_function &) = delete;
  precomputed_function &operator= (const precomputed_function &) = delete;

  ~precomputed_function () = default;

  /* Evaluate the function at X using linear interpolation.  */
  T pure_attr
  apply (T x) const noexcept
  {
    if (m_table.empty ())
      return x;
    int index = my_floor ((x - m_min_x) * m_step_inv);
    index = std::max (std::min (index, (int)m_table.size () - 1), 0);
    return m_table[index].add + m_table[index].slope * x;
  }

  /* Determine the inverse of the function for value Y.  Works only for
     monotone functions.  */
  T pure_attr
  invert (T y) const noexcept
  {
    unsigned int min = 0;
    unsigned int max = m_table.size ();
    while (max != min)
      {
        unsigned int ix = (min + max) / 2;
        T xx = m_min_x + ix * m_step;
        T val = m_table[ix].add + m_table[ix].slope * xx;
        T val2 = val + m_table[ix].slope * m_step;
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
    T ret = m_table[min].slope
                ? (y - m_table[min].add) / m_table[min].slope
                : m_min_x + min * m_step;

    /* Verify that inverse works. This also crashes if the function is not
       monotone.  */
    if (debug && std::abs (apply (ret) - y) > epsilon)
      {
        printf ("%i %i:%f...%i:%f min:%i (%f..%f) ret:%f %f should be %f\n",
                increasing, 0, m_min_x, (int)m_table.size () - 1, m_max_x, min,
                m_min_x + min * m_step, m_min_x + (min + 1) * m_step, (double)ret,
                (double)apply (ret), (double)y);
        abort ();
      }
    return ret;
  }

  /* Initialize the table using Y values at equidistant X points.
     LEN specifies the number of Y values.  */
  void
  init_by_y_values (const T *y, int len)
  {
    if (len < 2)
      abort ();
    int n_entries = len - 1;
    m_table.resize (n_entries);
    m_step = (m_max_x - m_min_x) / (T)n_entries;
    m_step_inv = 1 / m_step;
    for (int i = 0; i < n_entries; i++)
      {
        T xleft = m_min_x + i * m_step;
        m_table[i].slope = (y[i + 1] - y[i]) * m_step_inv;
        m_table[i].add = y[i] - xleft * m_table[i].slope;
      }
    increasing = y[0] < y[len - 1];
  }

  /* Initialize the table using an evaluation function F.  LEN specifies
     the number of entries in the lookup table.  If PARALLEL is true,
     initialization is performed in parallel using OpenMP.  */
  template <typename Func>
  void
  init_by_function (int len, Func f, bool parallel = false)
  {
    if (len < 2)
      abort ();
    std::vector<T> y (len);
    T step = (m_max_x - m_min_x) / (T)(len - 1);

    if (parallel)
      {
#pragma omp parallel for
        for (int i = 0; i < len; i++)
          y[i] = f (m_min_x + i * step);
      }
    else
      {
        for (int i = 0; i < len; i++)
          y[i] = f (m_min_x + i * step);
      }
    init_by_y_values (y.data (), len);
  }

  /* Initialize using linear interpolation between control points X and Y.
     NPOINTS specifies the number of control points.  LEN is the number
     of lookup table entries.  */
  void
  init_by_x_y_values (const T *x, const T *y, int npoints, int len)
  {
    /* If there are only 2 npoints we represent linear function.  */
    if (npoints <= 2)
      len = 2;

    init_by_function (len, [&](T xx) {
      if (!npoints)
        return xx;
      if (npoints == 1)
        return xx + y[0] - x[0];

      int p = 0;
      while (p < npoints - 1 && x[p + 1] < xx)
        p++;
      return y[p] + (y[p + 1] - y[p]) * (xx - x[p]) / (x[p + 1] - x[p]);
    });
  }

  /* Initialize using linear interpolation between control points in TABLE.
     NPOINTS specifies the number of control points.  LEN is the number
     of lookup table entries.  */
  void
  init_by_table (const T table[][2], int npoints, int len)
  {
    if (npoints <= 2)
      len = 2;

    init_by_function (len, [&](T xx) {
      if (!npoints)
        return xx;
      if (npoints == 1)
        return xx + table[0][1] - table[0][0];

      int p = 0;
      while (p < npoints - 1 && table[p + 1][0] < xx)
        p++;
      return table[p][1]
             + (table[p + 1][1] - table[p][1]) * (xx - table[p][0])
                   / (table[p + 1][0] - table[p][0]);
    });
  }

  /* Compare two precomputed functions.  */
  bool
  operator== (const precomputed_function &o) const
  {
    if (m_min_x != o.m_min_x || m_max_x != o.m_max_x
        || m_table.size () != o.m_table.size ())
      return false;
    for (size_t i = 0; i < m_table.size (); i++)
      if (m_table[i].slope != o.m_table[i].slope
          || m_table[i].add != o.m_table[i].add)
        return false;
    return true;
  }

  /* Inequality operator.  */
  bool
  operator!= (const precomputed_function &o) const
  {
    return !(*this == o);
  }

  /* Return the maximum X value.  */
  T
  get_max () const
  {
    return m_max_x;
  }

  /* Plot the function to stdout for debugging between MIN and MAX.  */
  void
  plot (T min, T max) const
  {
    int lines = 25;
    T ma = apply (min);
    T mi = ma;
    for (int i = 1; i <= lines; i++)
      {
        T v = apply (min + (max - min) * i / lines);
        ma = std::max (ma, v);
        mi = std::min (mi, v);
      }
    if (ma == mi)
      ma = mi + 1;
    for (int i = 0; i <= lines; i++)
      {
        T x = min + (max - min) * i / lines;
        T y = apply (x);
        int w = (y - mi) * 80 / (ma - mi);
        printf ("%6.2f:", (double)x);
        for (int j = 0; j < w; j++)
          printf (" ");
        printf ("* %6.2f\n", (double)y);
      }
  }

private:
  static constexpr const T epsilon = 0.001;
  T m_min_x, m_max_x, m_step, m_step_inv;
  struct entry
  {
    T slope, add;
  };
  std::vector<entry> m_table;
  bool increasing;
  static constexpr const bool debug = colorscreen_checking;
};
} // namespace colorscreen
#endif
