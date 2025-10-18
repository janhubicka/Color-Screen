#include <memory>
#include "gsl-utils.h"
#include "include/solver-parameters.h"
#include "homography.h"
#include "solver.h"
namespace colorscreen
{
namespace
{
bool debug_output = false;

inline int
fast_rand16 (unsigned int *g_seed)
{
  *g_seed = (214013 * *g_seed + 2531011);
  return ((*g_seed) >> 16) & 0x7FFF;
}

/* Random number generator used by RANSAC.  It is re-initialized every time
   RANSAC is run so results are deterministic.  */
inline int
fast_rand32 (unsigned int *g_seed)
{
  return fast_rand16 (g_seed) | (fast_rand16 (g_seed) << 15);
}

class translation_scale_matrix : public trans_4d_matrix
{
public:
  translation_scale_matrix (coord_t tx, coord_t ty, coord_t s)
  {
    m_elements[0][0] = s; m_elements[1][0] = 0; m_elements[2][0] = s * tx; m_elements[3][0] = 0;
    m_elements[0][1] = 0; m_elements[1][1] = s; m_elements[2][1] = s * ty; m_elements[3][1] = 0;
    m_elements[0][2] = 0; m_elements[1][2] = 0; m_elements[2][2] = 1;      m_elements[3][2] = 0;
    m_elements[0][3] = 0; m_elements[1][3] = 0; m_elements[2][3] = 0;      m_elements[3][3] = 1;
  }
};

/* Used to do two passes across set of points and produces a translation and
   scale matrix which normalizes them so they have center in 0 and scale
   sqrt(2). This improves numerical stability of homography computations.  */

class normalize_points
{
public:
  normalize_points (int nn) : avg_x (0), avg_y (0), dist_sum (0), n (nn) {}

  void
  account1 (point_t p, enum scanner_type type)
  {
    if (is_fixed_lens (type))
      {
        avg_x += p.x;
        avg_y += p.y;
      }
  }
  /* Screens with vertical strips has only x coordinage.  */
  void
  account1_xonly (point_t p, enum scanner_type type)
  {
    if (is_fixed_lens (type))
      avg_x += p.x;
  }
  void
  finish1 ()
  {
    avg_x /= n;
    avg_y /= n;
  }
  void
  account2 (point_t p)
  {
    dist_sum += sqrt ((p.x - avg_x) * (p.x - avg_x)
                      + (p.y - avg_y) * (p.y - avg_y));
  }
  void
  account2_xonly (point_t p)
  {
    dist_sum += fabs(p.x - avg_x);
  }
  trans_4d_matrix
  get_matrix ()
  {
    return translation_scale_matrix (-avg_x, -avg_y, sqrt (2) * n / dist_sum);
  }

private:
  coord_t avg_x, avg_y;
  coord_t dist_sum;
  int n;
};

/* Return number of variables needed to produce homography solution
   with specified degrees of freedoom.  */

int
equations_per_sample (int flags)
{
  if (flags & homography::solve_vertical_strips)
    return 1;
  return 2;
}

/* Return number of variables needed to produce homography solution
   with specified degrees of freedoom.  */

int
equation_variables (int flags)
{
  if (flags & homography::solve_rotation)
    return (flags & homography::solve_vertical_strips) ? 5 : 8;
  if (flags & homography::solve_free_rotation)
    return (flags & homography::solve_vertical_strips) ? 5 : 10;
  return (flags & homography::solve_vertical_strips) ? 3 : 6;
}

/* Produce two rows of homography equation converting
   point S to D at index N.  */

inline void
init_equation (gsl_matrix *A, gsl_vector *v, int n, bool invert, int flags,
               enum scanner_type scanner_type, point_t s, point_t d,
               trans_4d_matrix ts, trans_4d_matrix td)
{
  ts.perspective_transform (s.x, s.y, s.x, s.y);
  td.perspective_transform (d.x, d.y, d.x, d.y);
  if (!(flags & homography::solve_vertical_strips))
    {
      if (invert)
	std::swap (s, d);
      gsl_vector_set (v, n * 2, d.x);
      gsl_vector_set (v, n * 2 + 1, d.y);

      gsl_matrix_set (A, n * 2, 0, s.x);
      gsl_matrix_set (A, n * 2, 1, s.y);
      gsl_matrix_set (A, n * 2, 2, 1);
      gsl_matrix_set (A, n * 2, 3, 0);
      gsl_matrix_set (A, n * 2, 4, 0);
      gsl_matrix_set (A, n * 2, 5, 0);
      if (flags & (homography::solve_rotation | homography::solve_free_rotation))
	{
	  if ((flags & homography::solve_free_rotation)
	      || scanner_type != lens_move_horisontally)
	    {
	      gsl_matrix_set (A, n * 2, 6, -d.x * s.x);
	      gsl_matrix_set (A, n * 2, 7, -d.x * s.y);
	    }
	  else
	    {
	      gsl_matrix_set (A, n * 2, 6, 0);
	      gsl_matrix_set (A, n * 2, 7, 0);
	    }
	  if (flags & homography::solve_free_rotation)
	    {
	      gsl_matrix_set (A, n * 2, 8, 0);
	      gsl_matrix_set (A, n * 2, 9, 0);
	    }
	}

      gsl_matrix_set (A, n * 2 + 1, 0, 0);
      gsl_matrix_set (A, n * 2 + 1, 1, 0);
      gsl_matrix_set (A, n * 2 + 1, 2, 0);
      gsl_matrix_set (A, n * 2 + 1, 3, s.x);
      gsl_matrix_set (A, n * 2 + 1, 4, s.y);
      gsl_matrix_set (A, n * 2 + 1, 5, 1);
      if (flags & homography::solve_rotation)
	{
	  if (scanner_type != lens_move_vertically)
	    {
	      gsl_matrix_set (A, n * 2 + 1, 6, -d.y * s.x);
	      gsl_matrix_set (A, n * 2 + 1, 7, -d.y * s.y);
	    }
	  else
	    {
	      gsl_matrix_set (A, n * 2 + 1, 6, 0);
	      gsl_matrix_set (A, n * 2 + 1, 7, 0);
	    }
	}
      else if (flags & homography::solve_free_rotation)
	{
	  gsl_matrix_set (A, n * 2 + 1, 6, 0);
	  gsl_matrix_set (A, n * 2 + 1, 7, 0);
	  gsl_matrix_set (A, n * 2 + 1, 8, -d.y * s.x);
	  gsl_matrix_set (A, n * 2 + 1, 9, -d.y * s.y);
	}
    }
  else
    {
      /* Normally we compute error in screen coordinates, so the solver does not
         increase size of screen to reduce square error.
	 This is not possible when we do linear solution.

	 We look for matrix 3x2 (missing row for s.y) as follows:

         /s.x\   /v0 v1 v2 \ /d.x\
         \  1/ = \v3 v4 v5 / |d.y|
			     \1  /

           s.x = v0*d.x + v1*d.y + v2             | equation (1)
             1 = v3*d.x + v4*d.y + v5             | equation (2)
	 multiply (1) by s.x
           s.x = v3*d.x*s.x + v4*d.y*s.x + v5*s.x | equation (3)
	 now (3)-(1)
	     0 = v0*d.x + v1*d.y + v2 - v3*d.x*s.x - v4*d.y*s.x - v5*s.x 
	 Fix v5=1 and reorder
           s.x = v0*d.x + v1*d.y + v2 - v3*d.x*s.x - v4*d.y*s.x  */

      gsl_matrix_set (A, n, 0, d.x);
      gsl_matrix_set (A, n, 1, d.y);
      gsl_matrix_set (A, n, 2, 1);
      if ((flags & homography::solve_rotation)
	  || (flags & homography::solve_free_rotation))
	{
	  gsl_matrix_set (A, n, 3, -d.x * s.x);
	  gsl_matrix_set (A, n, 4, -d.y * s.x);
	}
      gsl_vector_set (v, n, s.x);
      assert (!colorscreen_checking || !invert);
    }
}

/* Turn solution V to a matrix.
   FLAGS and TYPE deterine the setup of the equations.  If INVERSER is true we
   are determining inverse matrix. TS and TD are normalization matrices for
   source and destination coordinates. If KEEP_VECTOR is false, free vector V.
 */
inline trans_4d_matrix
solution_to_matrix (gsl_vector *v, int flags, enum scanner_type type,
                    bool inverse, trans_4d_matrix ts, trans_4d_matrix td,
                    bool keep_vector = false)
{
  trans_4d_matrix ret;
  if (inverse)
    std::swap (ts, td);
  if (!(flags & homography::solve_vertical_strips))
    {
      ret.m_elements[0][0] = gsl_vector_get (v, 0);
      ret.m_elements[1][0] = gsl_vector_get (v, 1);
      ret.m_elements[2][0] = gsl_vector_get (v, 2);
      ret.m_elements[3][0] = 0;

      ret.m_elements[0][1] = gsl_vector_get (v, 3);
      ret.m_elements[1][1] = gsl_vector_get (v, 4);
      ret.m_elements[2][1] = 0;
      ret.m_elements[3][1] = gsl_vector_get (v, 5);

      if ((flags & (homography::solve_rotation) && type != lens_move_horisontally)
	  || (flags & homography::solve_free_rotation))
	{
	  ret.m_elements[0][2] = gsl_vector_get (v, 6);
	  ret.m_elements[1][2] = gsl_vector_get (v, 7);
	}
      else
	{
	  ret.m_elements[0][2] = 0;
	  ret.m_elements[1][2] = 0;
	}
      ret.m_elements[2][2] = 1;
      ret.m_elements[3][2] = 0;

      if (flags & homography::solve_free_rotation)
	{
	  ret.m_elements[0][3] = gsl_vector_get (v, 8);
	  ret.m_elements[1][3] = gsl_vector_get (v, 9);
	}
      else if ((flags & homography::solve_rotation)
	       && type != lens_move_vertically)
	{
	  ret.m_elements[0][3] = gsl_vector_get (v, 6);
	  ret.m_elements[1][3] = gsl_vector_get (v, 7);
	}
      else
	{
	  ret.m_elements[0][3] = 0;
	  ret.m_elements[1][3] = 0;
	}
      ret.m_elements[2][3] = 0;
      ret.m_elements[3][3] = 1;
    }
  else
    {
      ret.m_elements[0][0] = gsl_vector_get (v, 0);
      ret.m_elements[1][0] = gsl_vector_get (v, 1);
      ret.m_elements[2][0] = gsl_vector_get (v, 2);
      ret.m_elements[3][0] = 0;

      ret.m_elements[0][1] = -gsl_vector_get (v, 1);
      ret.m_elements[1][1] = gsl_vector_get (v, 0);
      ret.m_elements[2][1] = 0;
      ret.m_elements[3][1] = 0;

      if ((flags & homography::solve_free_rotation)
	  || (flags & homography::solve_rotation))
	{
	  ret.m_elements[0][2] = gsl_vector_get (v, 3);
	  ret.m_elements[1][2] = gsl_vector_get (v, 4);
	}
      else
	{
	  ret.m_elements[0][2] = 0;
	  ret.m_elements[1][2] = 0;
	}
      ret.m_elements[2][2] = 1;
      ret.m_elements[3][2] = 0;

      if ((flags & homography::solve_free_rotation)
	  || (flags & homography::solve_rotation))
	{
	  ret.m_elements[0][3] = gsl_vector_get (v, 3);
	  ret.m_elements[1][3] = gsl_vector_get (v, 4);
	}
      else
	{
	  ret.m_elements[0][3] = 0;
	  ret.m_elements[1][3] = 0;
	}
      ret.m_elements[2][3] = 0;
      ret.m_elements[3][3] = 1;
      fprintf (stdout, "Inverse homography\n");
      ret.print (stdout);
      fprintf (stdout, "Homography\n");
      ret = ret.invert ();
      ret.print (stdout);
    }
  if (!keep_vector)
    gsl_vector_free (v);
  if (debug_output)
    ret.print (stdout);
  td = td.invert ();
  ret = td * ret;
  ret = ret * ts;

  /* Make things prettier. There is a redundancy between thrid and 4th row.  */
  ret.m_elements[2][0] += ret.m_elements[3][0];
  ret.m_elements[3][0] = 0;
  ret.m_elements[3][1] += ret.m_elements[2][1];
  ret.m_elements[2][1] = 0;
  ret.m_elements[2][2] += ret.m_elements[3][2];
  ret.m_elements[3][2] = 0;
  ret.m_elements[3][3] += ret.m_elements[2][3];
  ret.m_elements[2][3] = 0;
  for (int x = 0; x < 4; x++)
    for (int y = 0; y < 4; y++)
      ret.m_elements[x][y] /= ret.m_elements[3][3];
  return ret;
}
}

static double
screen_compute_chisq (int flags, std::vector <solver_parameters::solver_point_t> &points, trans_4d_matrix homography)
{
  double chisq = 0;
  if (!(flags & homography::solve_vertical_strips))
    for (auto point : points)
      {
	point_t t;
	homography.inverse_perspective_transform (point.img.x, point.img.y, t.x, t.y);
	coord_t dist = point.scr.dist_sq2_from (t);
	if (dist > 10000)
	  dist = 10000;
	chisq += dist;
      }
  else
    for (auto point : points)
      {
	point_t t;
	homography.inverse_perspective_transform (point.img.x, point.img.y, t.x, t.y);
	coord_t dist = (point.img.x - t.x) * (point.img.x - t.x);
	if (dist > 10000)
	  dist = 10000;
	chisq += dist;
      }
  return chisq;
}

namespace homography
{

/* Return homography matrix determined from POINTS using least squares
   method.  If MAP is non-null apply early corrections (such as lens correction).
   If FLAGS is set to solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER_X and WCENTER_Y.
   If CHISQ_RET is non-NULL initialize it to square of errors.  */
trans_4d_matrix
get_matrix_ransac (std::vector <solver_parameters::solver_point_t> &points, int flags,
                   enum scanner_type scanner_type, scr_to_img *map,
                   point_t wcenter, coord_t *chisq_ret,
                   bool final_run)
{
  unsigned int seed = 0;
  int niterations = 500;
  int nvariables = equation_variables (flags);
  int eq_per_sample = equations_per_sample (flags);
  int nsamples = nvariables / eq_per_sample;
  trans_4d_matrix ret;
  int max_inliners = 0;
  double min_chisq = INT_MAX;
  double min_inliner_chisq = INT_MAX;
  int iteration;
  coord_t dist = 1;
  int n = points.size ();
  coord_t scr_dist = 0.1;

  /* Fix non-fixed lens the rotations are specified only by X or Y coordinates.
     We need enough variables in that system, so just double number of samples.
     ??? 5 values are enough  */
  if ((flags & homography::solve_rotation) && !is_fixed_lens (scanner_type))
    nsamples++;
  gsl_matrix *A = gsl_matrix_alloc (nsamples * eq_per_sample, nvariables);
  gsl_vector *v = gsl_vector_alloc (nsamples * eq_per_sample);
  std::vector <solver_parameters::solver_point_t> tpoints_vec;
  /* Apply non-linear transformations.  */
  if (map)
    {
      tpoints_vec.resize (n);
      for (int i = 0; i < n; i++)
        {
          tpoints_vec[i].img = map->apply_early_correction (points[i].img);
          tpoints_vec[i].scr = points[i].scr;
        }
    }
  std::vector <solver_parameters::solver_point_t> &tpoints = map ? tpoints_vec : points;
  if (nsamples > n)
    {
      fprintf (stderr, "Too few samples in RANSAC\n");
      abort ();
    }

  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();

  gsl_vector *i_c = NULL;
  gsl_matrix *i_cov = NULL;
  gsl_multifit_linear_workspace *i_work = NULL;

  if (nsamples * eq_per_sample != nvariables)
    {
      i_c = gsl_vector_alloc (nvariables);
      i_cov = gsl_matrix_alloc (nvariables, nvariables);
      i_work = gsl_multifit_linear_alloc (nsamples * eq_per_sample, nvariables);
    }

  for (iteration = 0; iteration < niterations; iteration++)
    {
      const int maxsamples = 10;
      int sample[maxsamples];
      bool colinear = false;
      int nattempts = 0;
      trans_4d_matrix ts;
      trans_4d_matrix td;
      do
        {
          nattempts++;
          /* Produce random sample.  */
          for (int i = 0; i < nsamples; i++)
            {
              bool ok;
              do
                {
                  sample[i] = fast_rand32 (&seed) % n;
                  ok = true;
                  for (int j = 0; j < i; j++)
                    if (sample[i] == sample[j])
                      {
                        ok = false;
                        break;
                      }
                }
              while (!ok);
            }

          /* Normalize input.  */
          normalize_points scrnorm (nsamples), imgnorm (nsamples);
	  if (flags & homography::solve_vertical_strips)
	    for (int i = 0; i < nsamples; i++)
	      {
		int p = sample[i];
		scrnorm.account1_xonly (tpoints[p].scr, scanner_type);
		imgnorm.account1 (tpoints[p].img, scanner_type);
	      }
	  else
	    for (int i = 0; i < nsamples; i++)
	      {
		int p = sample[i];
		scrnorm.account1 (tpoints[p].scr, scanner_type);
		imgnorm.account1 (tpoints[p].img, scanner_type);
	      }
          scrnorm.finish1 ();
          imgnorm.finish1 ();
	  if (flags & homography::solve_vertical_strips)
	    for (int i = 0; i < nsamples; i++)
	      {
		int p = sample[i];
		scrnorm.account2_xonly (tpoints[p].scr);
		imgnorm.account2 (tpoints[p].img);
	      }
	  else
	    for (int i = 0; i < nsamples; i++)
	      {
		int p = sample[i];
		scrnorm.account2 (tpoints[p].scr);
		imgnorm.account2 (tpoints[p].img);
	      }

          ts = scrnorm.get_matrix ();
          td = imgnorm.get_matrix ();
          /* Proudce equations.  */
          for (int i = 0; i < nsamples; i++)
            {
              int p = sample[i];
              init_equation (A, v, i, false, flags, scanner_type,
                             { tpoints[p].scr }, { tpoints[p].img }, ts, td);
            }
          if (!i_work)
            colinear = (gsl_linalg_HH_svx (A, v) != GSL_SUCCESS);
          else
            {
              double chisq;
              colinear
                  = (gsl_multifit_linear (A, v, i_c, i_cov, &chisq, i_work))
                    != GSL_SUCCESS;
              /* This can not be vector_memcpy since sizes of vectors does not
                 match.  */
              for (int i = 0; i < nvariables; i++)
                gsl_vector_set (v, i, gsl_vector_get (i_c, i));
            }
        }
      while (colinear && nattempts < 10000);

      if (nattempts > 10000)
        {
          printf ("Points are always colinear");
          break;
        }

      trans_4d_matrix cur
          = solution_to_matrix (v, flags, scanner_type, false, ts, td, true);
      // cur.print (stdout);
      int ninliners = 0;
      double cur_chisq = 0, cur_inliner_chisq = 0;
      if (!(flags & homography::solve_vertical_strips))
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t t;
	      cur.perspective_transform (tpoints[i].scr.x, tpoints[i].scr.y, t.x,
					 t.y);
	      cur_chisq += t.dist_sq2_from (tpoints[i].img);
	      if (t.almost_eq (tpoints[i].img, dist))
		{
		  ninliners++;
		  cur_inliner_chisq += t.dist_sq2_from (tpoints[i].img);
		}
	    }
	}
      else
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t s;
	      cur.inverse_perspective_transform (tpoints[i].img.x, tpoints[i].img.y, s.x, s.y);
	      cur_chisq += (tpoints[i].scr.x - s.x) * (tpoints[i].scr.x - s.x);
	      if (fabs (tpoints[i].scr.x - s.x) < scr_dist)
		{
		  ninliners++;
		  cur_inliner_chisq += (tpoints[i].scr.x - s.x) * (tpoints[i].scr.x - s.x);
		}
	    }
	}
      if (ninliners < nsamples)
        continue;
      if ((ninliners > max_inliners)
          || (ninliners == max_inliners
              && cur_inliner_chisq < min_inliner_chisq))
        {
          ret = cur;
          max_inliners = ninliners;
          min_chisq = cur_chisq;
          min_inliner_chisq = cur_inliner_chisq;
          //cur.print (stdout);
          //printf ("Iteration %i inliners %i out of %i, chisq %f\n", iteration, ninliners, n, min_chisq);
          if (ninliners == n)
            break;
          if (flags & solve_limit_ransac_iterations)
            {
              niterations
                  = log (1 - 0.99) / log (1 - pow (ninliners / (coord_t)n, 4));
              if (cur_chisq < n)
                break;
            }
        }
    }
  if (i_c)
    {
      gsl_multifit_linear_free (i_work);
      gsl_vector_free (i_c);
      gsl_matrix_free (i_cov);
    }
  gsl_set_error_handler (old_handler);
  gsl_matrix_free (A);
  gsl_vector_free (v);

  // if (final)
  // printf ("Iteration %i inliners %i out of %i, chisq %f inliner chisq %f\n",
  // iteration, max_inliners, n, min_chisq, min_inliner_chisq);
  if (max_inliners > nsamples)
    {
      gsl_matrix *X = gsl_matrix_alloc (max_inliners * eq_per_sample, nvariables);
      gsl_vector *y = gsl_vector_alloc (max_inliners * eq_per_sample);
      gsl_vector *c = gsl_vector_alloc (nvariables);
      gsl_matrix *cov = gsl_matrix_alloc (nvariables, nvariables);

      trans_4d_matrix ts;
      trans_4d_matrix td;
      normalize_points scrnorm (max_inliners), imgnorm (max_inliners);
      if (!(flags & homography::solve_vertical_strips))
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t scr = tpoints[i].scr;
	      point_t t;
	      ret.perspective_transform (scr.x, scr.y, t.x, t.y);
	      if (tpoints[i].img.almost_eq (t, dist))
		{
		  scrnorm.account1 (scr, scanner_type);
		  imgnorm.account1 (tpoints[i].img, scanner_type);
		}
	    }
	}
      else
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t img = tpoints[i].img;
	      point_t scr = tpoints[i].scr;
	      point_t s;
	      ret.inverse_perspective_transform (img.x, img.y, s.x, s.y);
	      if (fabs (scr.x - s.x) < scr_dist)
		{
		  scrnorm.account1_xonly (scr, scanner_type);
		  imgnorm.account1 (tpoints[i].img, scanner_type);
		}
	    }
	}
      scrnorm.finish1 ();
      imgnorm.finish1 ();
      if (!(flags & homography::solve_vertical_strips))
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t scr = tpoints[i].scr;
	      point_t t;
	      ret.perspective_transform (scr.x, scr.y, t.x, t.y);
	      if (tpoints[i].img.almost_eq (t, dist))
		{
		  scrnorm.account2 (scr);
		  imgnorm.account2 (tpoints[i].img);
		}
	    }
	}
      else
	{
	  for (int i = 0; i < n; i++)
	    {
	      point_t img = tpoints[i].img;
	      point_t scr = tpoints[i].scr;
	      point_t s;
	      ret.inverse_perspective_transform (img.x, img.y, s.x, s.y);
	      if (fabs (scr.x - s.x) < scr_dist)
		{
		  scrnorm.account2_xonly (scr);
		  imgnorm.account2 (tpoints[i].img);
		}
	    }
	}
      ts = scrnorm.get_matrix ();
      td = imgnorm.get_matrix ();

      int p = 0;
      if (!(flags & homography::solve_vertical_strips))
	for (int i = 0; i < n; i++)
	  {
	    point_t scr = tpoints[i].scr;
	    point_t t;
	    ret.perspective_transform (scr.x, scr.y, t.x, t.y);
	    if (tpoints[i].img.almost_eq (t, dist))
	      {
		init_equation (X, y, p, false, flags, scanner_type, scr,
			       tpoints[i].img, ts, td);
		p++;
	      }
	  }
      else
	for (int i = 0; i < n; i++)
	  {
	    point_t img = tpoints[i].img;
	    point_t scr = tpoints[i].scr;
	    point_t s;
	    ret.inverse_perspective_transform (img.x, img.y, s.x, s.y);
	    if (fabs (scr.x - s.x) < scr_dist)
	      {
		init_equation (X, y, p, false, flags, scanner_type, scr,
			       tpoints[i].img, ts, td);
		p++;
	      }
	  }
      gsl_multifit_linear_workspace *work
          = gsl_multifit_linear_alloc (n * eq_per_sample, nvariables);
      gsl_multifit_linear (X, y, c, cov, &min_chisq, work);
      gsl_multifit_linear_free (work);
      gsl_matrix_free (X);
      gsl_vector_free (y);
      gsl_matrix_free (cov);
      ret = solution_to_matrix (c, flags, scanner_type, false, ts, td);
      /* Use screen so we do not get biass with lens correction.  */
      min_chisq = screen_compute_chisq (flags, tpoints, ret);
    }
  else if (final_run)
    printf ("Failed to find inliners for ransac\n");
  // if (final)
  // printf ("chisq %f after least squares\n", min_chisq);

  if (chisq_ret)
    *chisq_ret = min_chisq;
  return ret;
}

/* Return homography matrix determined from POINTS using least squares
   method.  If MAP is non-null apply early corrections (such as lens correction).
   If FLAGS is set to solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER_X and WCENTER_Y.
   If CHISQ_RET is non-NULL initialize it to square of errors.  */
trans_4d_matrix
get_matrix (std::vector <solver_parameters::solver_point_t> &points, int flags,
            enum scanner_type scanner_type, scr_to_img *map, point_t wcenter,
            coord_t *chisq_ret)
{
  int nvariables = equation_variables (flags);
  int n = points.size ();
  int eq_per_sample = equations_per_sample (flags);
  int nequations = n * eq_per_sample;
  gsl_matrix *X, *cov;
  gsl_vector *y, *w = NULL, *c;
  normalize_points scrnorm (n), imgnorm (n);
  std::vector <solver_parameters::solver_point_t> tpoints_vec;
  /* Apply non-linear transformations.  */
  if (map)
    {
      tpoints_vec.resize (n);
      /* For lens solving it is faster to avoid precomputing lens inverse.  */
      if (map->early_correction_precoputed ())
	for (int i = 0; i < n; i++)
	  {
	    tpoints_vec[i].img = map->apply_early_correction (points[i].img);
	    tpoints_vec[i].scr = points[i].scr;
	  }
      else
	for (int i = 0; i < n; i++)
	  {
	    tpoints_vec[i].img = map->nonprecomputed_apply_early_correction (points[i].img);
	    tpoints_vec[i].scr = points[i].scr;
	  }
    }
  std::vector <solver_parameters::solver_point_t> &tpoints = map ? tpoints_vec : points;
  for (int i = 0; i < n; i++)
    {
      scrnorm.account1 (tpoints[i].scr, scanner_type);
      imgnorm.account1 (tpoints[i].img, scanner_type);
    }
  scrnorm.finish1 ();
  imgnorm.finish1 ();
  for (int i = 0; i < n; i++)
    {
      scrnorm.account2 (tpoints[i].scr);
      imgnorm.account2 (tpoints[i].img);
    }
  trans_4d_matrix ts = scrnorm.get_matrix ();
  trans_4d_matrix td = imgnorm.get_matrix ();
  coord_t xscale = 1;
  coord_t yscale = 1;
  /* For moving lens, take into account that in one direction geometry changes
     a lot more than in the other.  */
  if (scanner_type == lens_move_horisontally)
    xscale = 100;
  if (scanner_type == lens_move_vertically)
    yscale = 100;
  coord_t normscale = 0, sumscale = 0;
  std::vector<coord_t> weights (
      (flags & (solve_screen_weights | solve_image_weights)) ? n : 0);
  if (flags & solve_image_weights)
    for (int i = 0; i < n; i++)
      {
        coord_t dist = tpoints[i].img.dist_sq2_from (wcenter, xscale, yscale);
        dist *= dist;
        double weight = 1 / (dist + 0.5);
        weights[i] = weight;
        normscale = std::max (normscale, weight);
        sumscale += weight;
      }
  else if (flags & solve_screen_weights)
    for (int i = 0; i < n; i++)
      {
        coord_t dist = tpoints[i].scr.dist_from (wcenter, xscale, yscale);
        double weight = 1 / (dist + 0.5);
        weights[i] = weight;
        normscale = std::max (normscale, weight);
        sumscale += weight;
      }
  if (flags & (solve_screen_weights | solve_image_weights))
    {
      coord_t minscale = sumscale / 100;
      if (n > 500)
        {
          do
            {
              nequations = 0;
              for (int i = 0; i < n; i++)
                if (weights[i] >= minscale)
                  nequations += eq_per_sample;
              if (nequations >= eq_per_sample * 100)
                break;
              minscale *= (coord_t)(1.0 / 8);
            }
          while (true);
        }
      else
        minscale = 0;

      if (normscale != 0)
        normscale = 1 / normscale;
      nequations = 0;
      for (int i = 0; i < n; i++)
        {
          if (weights[i] >= minscale)
            {
              weights[i] *= normscale;
              nequations += eq_per_sample;
              if (!weights[i])
                abort ();
            }
          else
            weights[i] = 0;
        }
      if (nequations * eq_per_sample < nvariables)
        abort ();
      w = gsl_vector_alloc (nequations);
    }

  X = gsl_matrix_alloc (nequations, nvariables);
  y = gsl_vector_alloc (nequations);
  c = gsl_vector_alloc (nvariables);
  cov = gsl_matrix_alloc (nvariables, nvariables);

  for (int i = 0, eq = 0; i < n; i++)
    {
      if (w && !weights[i])
        continue;
      init_equation (X, y, eq, false, flags, scanner_type, tpoints[i].scr, tpoints[i].img,
                     ts, td);

      if (w && eq_per_sample == 2)
        {
          gsl_vector_set (w, eq * 2, weights[i]);
          gsl_vector_set (w, eq * 2 + 1, weights[i]);
        }
      else if (w && eq_per_sample == 1)
        {
          gsl_vector_set (w, eq, weights[i]);
        }
      eq++;
    }
  gsl_multifit_linear_workspace *work
      = gsl_multifit_linear_alloc (nequations, nvariables);
  double chisq;
  if (w)
    gsl_multifit_wlinear (X, w, y, c, cov, &chisq, work);
  else
    gsl_multifit_linear (X, y, c, cov, &chisq, work);
  gsl_multifit_linear_free (work);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  if (w)
    gsl_vector_free (w);
  gsl_matrix_free (cov);
  trans_4d_matrix ret
      = solution_to_matrix (c, flags, scanner_type, false, ts, td);
  /* We normalize equations and thus chisq is unnaturaly small.
     To make get same range as in ransac we need to recompute.  */
  if (chisq_ret)
    {
      /* Use screen so we do not get biass with lens correction.  */
      *chisq_ret = screen_compute_chisq (flags, tpoints, ret);
    }
  return ret;
}

/* Return homography matrix M.
   Applying M.perspective_transofrm on (0,0) will yield to ZERO.
   Applying M.perspective_transofrm on (1,0) will yield to X.
   Applying M.perspective_transofrm on (0,1) will yield to Y.
   Applying M.perspective_transofrm on (1,1) will yield to XPY (X+Y).
   Applying M.perspective_transofrm on (2,3) will yield to TXPY (2*X+3*Y).
   If INVERT is true then inverse of this transofrmation is computed.

   This works for either scanner type.  */
trans_4d_matrix
get_matrix_5points (bool invert, enum scanner_type scanner_type, point_t zero,
                    point_t x, point_t y, point_t xpy, point_t txpy)
{
  gsl_matrix *A = gsl_matrix_alloc (10, 10);
  gsl_vector *v = gsl_vector_alloc (10);
  normalize_points scrnorm (5), imgnorm (5);
  scrnorm.account1 ({ 0, 0 }, scanner_type);
  imgnorm.account1 (zero, scanner_type);
  scrnorm.account1 ({ 1000, 0 }, scanner_type);
  imgnorm.account1 (x, scanner_type);
  scrnorm.account1 ({ 0, 1000 }, scanner_type);
  imgnorm.account1 (y, scanner_type);
  scrnorm.account1 ({ 1000, 1000 }, scanner_type);
  imgnorm.account1 (xpy, scanner_type);
  scrnorm.account1 ({ 2000, 3000 }, scanner_type);
  imgnorm.account1 (txpy, scanner_type);
  scrnorm.finish1 ();
  imgnorm.finish1 ();
  scrnorm.account2 ({ 0, 0 });
  imgnorm.account2 (zero);
  scrnorm.account2 ({ 1000, 0 });
  imgnorm.account2 (x);
  scrnorm.account2 ({ 0, 1000 });
  imgnorm.account2 (y);
  scrnorm.account2 ({ 1000, 1000 });
  imgnorm.account2 (xpy);
  scrnorm.account2 ({ 2000, 3000 });
  imgnorm.account2 (txpy);
  trans_4d_matrix ts = scrnorm.get_matrix ();
  trans_4d_matrix td = imgnorm.get_matrix ();
  init_equation (A, v, 0, invert, solve_free_rotation, fixed_lens, { 0, 0 },
                 zero, ts, td);
  init_equation (A, v, 1, invert, solve_free_rotation, fixed_lens, { 1000, 0 },
                 x, ts, td);
  init_equation (A, v, 2, invert, solve_free_rotation, fixed_lens, { 0, 1000 },
                 y, ts, td);
  init_equation (A, v, 3, invert, solve_free_rotation, fixed_lens,
                 { 1000, 1000 }, xpy, ts, td);
  init_equation (A, v, 4, invert, solve_free_rotation, fixed_lens,
                 { 2000, 3000 }, txpy, ts, td);

  if (debug_output)
    print_system (stdout, A, v);
  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  gsl_linalg_HH_svx (A, v);
  gsl_set_error_handler (old_handler);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_free_rotation, fixed_lens, invert, ts,
                             td);
}
}
}
