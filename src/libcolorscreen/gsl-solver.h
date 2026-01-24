#ifndef GSL_SOLVER_H
#define GSL_SOLVER_H

#include <gsl/gsl_multimin.h>
#include <gsl/gsl_errno.h>
#include <vector>
#include "include/progress-info.h"

namespace colorscreen
{

template <typename T, typename C>
struct gsl_solver_proxy
{
  C &c;
  std::vector<T> temp_vals;

  static double
  f (const gsl_vector *v, void *params)
  {
    gsl_solver_proxy *proxy = static_cast<gsl_solver_proxy *> (params);
    int n = proxy->temp_vals.size ();
    for (int i = 0; i < n; i++)
      proxy->temp_vals[i] = gsl_vector_get (v, i);
    
    proxy->c.constrain (proxy->temp_vals.data ());
    return proxy->c.objfunc (proxy->temp_vals.data ());
  }
};

template <typename T, typename C>
double
gsl_simplex (C &c, const char *task = NULL, progress_info *progress = NULL,
             bool progress_report = true)
{
  int n = c.num_values ();
  T scale = c.scale ();
  T eps = c.epsilon ();
  const int MAX_IT = 10000;

  if (progress && progress_report)
    progress->set_task (task, MAX_IT);

  gsl_solver_proxy<T, C> proxy{ c, std::vector<T> (n) };

  gsl_multimin_function minex_func;
  minex_func.n = n;
  minex_func.f = gsl_solver_proxy<T, C>::f;
  minex_func.params = &proxy;

  gsl_vector *x = gsl_vector_alloc (n);
  gsl_vector *ss = gsl_vector_alloc (n);

  for (int i = 0; i < n; i++)
    {
      gsl_vector_set (x, i, c.start[i]);
      gsl_vector_set (ss, i, scale);
    }

  const gsl_multimin_fminimizer_type *MT = gsl_multimin_fminimizer_nmsimplex2;
  gsl_multimin_fminimizer *s = gsl_multimin_fminimizer_alloc (MT, n);

  gsl_multimin_fminimizer_set (s, &minex_func, x, ss);

  int status;
  int iter = 0;
  double min_val = 0;

  do
    {
      iter++;
      status = gsl_multimin_fminimizer_iterate (s);

      if (status)
        break;

      double size = gsl_multimin_fminimizer_size (s);
      status = gsl_multimin_test_size (size, eps);

      if (status == GSL_SUCCESS)
        {
          if (c.verbose ())
            printf ("Converged to minimum at\n");
        }

      if (c.verbose ())
        {
          printf ("Iteration %d: size = %.3e, f = %.10f\n", iter, size, s->fval);
        }

      if (progress && progress_report)
        progress->inc_progress ();
      if (progress && progress->cancel_requested ())
        break;

    }
  while (status == GSL_CONTINUE && iter < MAX_IT);

  min_val = s->fval;

  for (int i = 0; i < n; i++)
    {
      c.start[i] = gsl_vector_get (s->x, i);
    }
  
  // Re-apply constraints to final result
  c.constrain(c.start);

  gsl_vector_free (x);
  gsl_vector_free (ss);
  gsl_multimin_fminimizer_free (s);

  return min_val;
}

} // namespace colorscreen

#endif
