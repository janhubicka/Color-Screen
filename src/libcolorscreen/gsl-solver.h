#ifndef GSL_SOLVER_H
#define GSL_SOLVER_H

/**
 * GSL-based Nonlinear Optimization Templates
 * ==========================================
 * 
 * This file provides template wrappers for GSL's nonlinear optimization algorithms.
 * It supports two main strategies:
 * 
 * 1. gsl_simplex: A derivative-free Nelder-Mead simplex algorithm.
 * 2. gsl_multifit: A Levenberg-Marquardt trust-region algorithm for least squares.
 * 
 * Base Class Requirements
 * -----------------------
 * Both templates expect a solver class `C` that provides specific methods.
 * 
 * COMMON METHODS:
 * - int num_values(): Returns the number of parameters to optimize.
 * - T start[N]: An array or pointer to the initial parameter values.
 * - double epsilon(): Convergence tolerance.
 * - void constrain(T *params): Optional. Projects parameters back into valid range.
 * - bool verbose(): Optional. If true, prints iteration progress to stdout.
 * 
 * GSL_SIMPLEX METHODS:
 * - T scale(): Initial size of the simplex (step size for parameters).
 * - T objfunc(const T *params): Returns the scalar value to minimize.
 * 
 * GSL_MULTIFIT METHODS:
 * - int num_observations(): Returns the number of independent residuals (measurements).
 * - void residuals(const T *params, T *f_vec): Fills f_vec with individual residuals.
 *   Note: GSL minimizes the sum of squares of these residuals.
 * - void jacobian(const T *params, gsl_matrix *J): OPTIONAL. Fills J with partial 
 *   derivatives. If missing, GSL uses finite differences.
 * - void fvv(const T *params, const gsl_vector *v, gsl_vector *fvv): OPTIONAL. 
 *   Computes second derivative for geodesic acceleration.
 * 
 * Why use one over the other?
 * ---------------------------
 * - Use gsl_simplex when you only have a single "score" (objective function) and 
 *   cannot easily break it down into individual errors.
 * - Use gsl_multifit when your error is the sum of squares of many measurements. 
 *   It is significantly faster as it uses the structure of the least-squares 
 *   problem to approximate the curvature.
 */

#include <gsl/gsl_multimin.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_blas.h>
#include <vector>
#include <type_traits>
#include <utility>
#include <cmath>
#include "include/progress-info.h"

namespace colorscreen
{

// SFINAE for jacobian
template <typename C, typename T, typename = void>
struct has_jacobian : std::false_type {};

template <typename C, typename T>
struct has_jacobian<C, T, decltype(std::declval<C>().jacobian(static_cast<const T*>(nullptr), static_cast<gsl_matrix*>(nullptr)), void())> : std::true_type {};

// SFINAE for fvv
template <typename C, typename T, typename = void>
struct has_fvv : std::false_type {};

template <typename C, typename T>
struct has_fvv<C, T, decltype(std::declval<C>().fvv(static_cast<const T*>(nullptr), static_cast<const gsl_vector*>(nullptr), static_cast<gsl_vector*>(nullptr)), void())> : std::true_type {};


// Bridge templates to safely call optional methods
template <typename T, typename C, bool Enabled>
struct jacobian_bridge {
    static void call(C &c, const T *params, gsl_matrix *J) {
        c.jacobian(params, J);
    }
};

template <typename T, typename C>
struct jacobian_bridge<T, C, false> {
    static void call(C &, const T *, gsl_matrix *) {}
};

template <typename T, typename C, bool Enabled>
struct fvv_bridge {
    static void call(C &c, const T *params, const gsl_vector *v, gsl_vector *fvv) {
        c.fvv(params, v, fvv);
    }
};

template <typename T, typename C>
struct fvv_bridge<T, C, false> {
    static void call(C &, const T *, const gsl_vector *, gsl_vector *) {}
};


template <typename T, typename C>
struct gsl_solver_proxy
{
  C &c;
  std::vector<T> temp_params;
  std::vector<T> temp_residuals;

  static int
  f (const gsl_vector *v, void *params, gsl_vector *f_vec)
  {
    gsl_solver_proxy *proxy = static_cast<gsl_solver_proxy *> (params);
    int p = proxy->temp_params.size ();
    int n = f_vec->size;

    for (int i = 0; i < p; i++)
      proxy->temp_params[i] = gsl_vector_get (v, i);
    
    proxy->c.constrain (proxy->temp_params.data ());
    
    if (proxy->temp_residuals.size() != (size_t)n)
      proxy->temp_residuals.resize(n);

    proxy->c.residuals (proxy->temp_params.data (), proxy->temp_residuals.data ());

    for (int i = 0; i < n; i++)
      gsl_vector_set (f_vec, i, proxy->temp_residuals[i]);

    return GSL_SUCCESS;
  }

  static int
  df (const gsl_vector *v, void *params, gsl_matrix *J)
  {
    gsl_solver_proxy *proxy = static_cast<gsl_solver_proxy *> (params);
    int p = proxy->temp_params.size ();
    for (int i = 0; i < p; i++)
      proxy->temp_params[i] = gsl_vector_get (v, i);
    
    proxy->c.constrain (proxy->temp_params.data ());
    jacobian_bridge<T, C, has_jacobian<C, T>::value>::call(proxy->c, proxy->temp_params.data (), J);
    return GSL_SUCCESS;
  }

  static int
  fvv (const gsl_vector *v, const gsl_vector *v_vec, void *params, gsl_vector *fvv_vec)
  {
    gsl_solver_proxy *proxy = static_cast<gsl_solver_proxy *> (params);
    int p = proxy->temp_params.size ();
    for (int i = 0; i < p; i++)
      proxy->temp_params[i] = gsl_vector_get (v, i);
    
    proxy->c.constrain (proxy->temp_params.data ());
    fvv_bridge<T, C, has_fvv<C, T>::value>::call(proxy->c, proxy->temp_params.data (), v_vec, fvv_vec);
    return GSL_SUCCESS;
  }
};


template <typename T, typename C>
double
gsl_multifit (C &c, const char *task = NULL, progress_info *progress = NULL,
              bool progress_report = true)
{
  int p = c.num_values ();
  int n = c.num_observations ();
  T eps = c.epsilon ();
  const int MAX_IT = 10000;

  if (progress && progress_report)
    progress->set_task (task, MAX_IT);

  gsl_solver_proxy<T, C> proxy{ c, std::vector<T> (p), std::vector<T> (n) };

  gsl_multifit_nlinear_fdf fdf;
  fdf.f = gsl_solver_proxy<T, C>::f;
  fdf.df = has_jacobian<C, T>::value ? gsl_solver_proxy<T, C>::df : NULL;
  fdf.fvv = has_fvv<C, T>::value ? gsl_solver_proxy<T, C>::fvv : NULL;
  fdf.p = p;
  fdf.n = n;
  fdf.params = &proxy;

  gsl_multifit_nlinear_parameters fdf_params = gsl_multifit_nlinear_default_parameters ();
  fdf_params.h_df = c.derivative_perturbation ();
  
  /* Disable GSL default error handler to prevent aborts on singular matrices etc. */
  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();

  /* Select Levenberg-Marquardt.  */
  const gsl_multifit_nlinear_type *T_type = gsl_multifit_nlinear_trust;
  gsl_multifit_nlinear_workspace *w = gsl_multifit_nlinear_alloc (T_type, &fdf_params, n, p);

  if (!w)
    {
      if (c.verbose ())
        {
	  if (progress)
	    progress->pause_stdout ();
          fprintf (stderr, "GSL multifit: failed to allocate workspace (check n >= p)\n");
	  if (progress)
	    progress->resume_stdout ();
        }
      gsl_set_error_handler (old_handler);
      return NAN;
    }

  gsl_vector *x = gsl_vector_alloc (p);
  for (int i = 0; i < p; i++)
    gsl_vector_set (x, i, c.start[i]);

  double final_chisq = NAN;
  int status = gsl_multifit_nlinear_init (x, &fdf, w);

  if (status == GSL_SUCCESS)
    {
      int info;
      int iter = 0;

      do
        {
          iter++;
          status = gsl_multifit_nlinear_iterate (w);

          if (status > 0)
	    {
	      if (status == GSL_ENOPROG)
	        {
		  if (progress)
		    progress->pause_stdout ();
		  if (c.verbose ())
		    printf ("Multifit did not improve solution\n");
		  if (progress)
		    progress->resume_stdout ();
	        }
	      else
		{
		  if (progress)
		    progress->pause_stdout ();
		  fprintf (stderr, "GSL multifit finished with error: %s\n", gsl_strerror (status));
		  if (progress)
		    progress->resume_stdout ();
		}
	      break;
	    }

          /* test for convergence */
          status = gsl_multifit_nlinear_test (eps, eps, eps, &info, w);

          if (c.verbose ())
            {
              double r_norm = gsl_blas_dnrm2 (w->f);
	      if (progress)
		progress->pause_stdout ();
              printf ("Iteration %d: |f(x)| = %.10g\n", iter, r_norm);
	      if (progress)
		progress->resume_stdout ();
            }

          if (progress && progress_report)
            progress->inc_progress ();
          if (progress && progress->cancel_requested ())
            break;

        }
      while (status == GSL_CONTINUE && iter < MAX_IT);
      if (status == GSL_SUCCESS)
	{
	  double r_norm = gsl_blas_dnrm2 (w->f);
	  if (c.verbose ())
	    {
	      if (progress)
		progress->pause_stdout ();
	      printf ("Finished after %d iterations: |f(x)| = %.10g\n", iter, r_norm);
	      if (progress)
		progress->resume_stdout ();
	    }
	}

      for (int i = 0; i < p; i++)
        c.start[i] = gsl_vector_get (w->x, i);

      //c.constrain (c.start);

      double norm = gsl_blas_dnrm2 (w->f);
      final_chisq = norm * norm;
    }
  else if (c.verbose ())
    {
      if (progress)
	progress->pause_stdout ();
      fprintf (stderr, "GSL multifit initialization failed: %s\n", gsl_strerror (status));
      if (progress)
	progress->resume_stdout ();
    }

  gsl_vector_free (x);
  gsl_multifit_nlinear_free (w);
  gsl_set_error_handler (old_handler);

  return final_chisq;
}

template <typename T, typename C>
struct gsl_simplex_proxy
{
  C &c;
  std::vector<T> temp_vals;

  static double
  f (const gsl_vector *v, void *params)
  {
    gsl_simplex_proxy *proxy = static_cast<gsl_simplex_proxy *> (params);
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

  gsl_simplex_proxy<T, C> proxy{ c, std::vector<T> (n) };

  gsl_multimin_function minex_func;
  minex_func.n = n;
  minex_func.f = gsl_simplex_proxy<T, C>::f;
  minex_func.params = &proxy;

  gsl_vector *x = gsl_vector_alloc (n);
  gsl_vector *ss = gsl_vector_alloc (n);

  for (int i = 0; i < n; i++)
    {
      gsl_vector_set (x, i, c.start[i]);
      gsl_vector_set (ss, i, scale);
    }

  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  const gsl_multimin_fminimizer_type *MT = gsl_multimin_fminimizer_nmsimplex2;
  gsl_multimin_fminimizer *s = gsl_multimin_fminimizer_alloc (MT, n);

  if (!s)
    {
      if (c.verbose ())
	{
	  if (progress)
	    progress->pause_stdout ();
	  fprintf (stderr, "GSL simplex: failed to allocate workspace\n");
	  if (progress)
	    progress->resume_stdout ();
	}
      gsl_vector_free (x);
      gsl_vector_free (ss);
      gsl_set_error_handler (old_handler);
      return NAN;
    }

  double min_val = NAN;
  int status = gsl_multimin_fminimizer_set (s, &minex_func, x, ss);

  if (status == GSL_SUCCESS)
    {
      int iter = 0;

      do
        {
          iter++;
          status = gsl_multimin_fminimizer_iterate (s);

          if (status > 0)
	    {
	      if (status == GSL_ENOPROG)
		{
		  if (c.verbose ())
		    {
		      if (progress)
			progress->pause_stdout ();
		      printf ("Simplex optimizer did not improve solution\n");
		      if (progress)
			progress->resume_stdout ();
		    }
		}
	      else
		{
		  if (progress)
		    progress->pause_stdout ();
		  fprintf (stderr, "GSL multifit finished with error: %s\n", gsl_strerror (status));
		  if (progress)
		    progress->resume_stdout ();
		}
	      break;
	    }

          double size = gsl_multimin_fminimizer_size (s);
          status = gsl_multimin_test_size (size, 1e-2);

          if (status == GSL_SUCCESS)
            {
              if (c.verbose ())
		{
		  if (progress)
		    progress->pause_stdout ();
		  printf ("Converged to minimum at\n");
		  if (progress)
		    progress->resume_stdout ();
		}
            }

          if (c.verbose ())
            {
	      if (progress)
		progress->pause_stdout ();
              printf ("Iteration %d: size = %.3e, f = %.10f\n", iter, size, s->fval);
	      if (progress)
		progress->resume_stdout ();
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
      //c.constrain (c.start);
    }
  else if (c.verbose ())
    {
      if (progress)
	progress->pause_stdout ();
      fprintf (stderr, "GSL simplex initialization failed: %s\n", gsl_strerror (status));
      if (progress)
	progress->resume_stdout ();
    }

  gsl_vector_free (x);
  gsl_vector_free (ss);
  gsl_multimin_fminimizer_free (s);
  gsl_set_error_handler (old_handler);

  return min_val;
}

} // namespace colorscreen

#endif
