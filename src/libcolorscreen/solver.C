#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <memory>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "gsl-utils.h"
#include "include/colorscreen.h"
#include "include/screen-map.h"
#include "solver.h"
#include "nmsimplex.h"

const char *solver_parameters::point_color_names[(int)max_point_color] = {"red", "green", "blue"};

namespace
{
bool debug_output = false;
bool debug = colorscreen_checking;

inline int fast_rand16(unsigned int *g_seed) {
    *g_seed = (214013* *g_seed+2531011);
    return ((*g_seed)>>16)&0x7FFF;
}

/* Random number generator used by RANSAC.  It is re-initialized every time RANSAC is run
   so results are deterministic.  */
inline int fast_rand32(unsigned int *g_seed) {
  return fast_rand16(g_seed) | (fast_rand16 (g_seed) << 15);
}

/* Return number of variables needed to produce homography solution
   with specified degrees of freedoom.  */

int
equation_variables (int flags)
{
  if (flags & homography::solve_rotation)
    return 8;
  if (flags & homography::solve_free_rotation)
    return 10;
  return 6;
}

class translation_scale_matrix: public trans_4d_matrix
{
  public:
  translation_scale_matrix (coord_t tx, coord_t ty, coord_t s)
  {
    m_elements[0][0] = s; m_elements[1][0] = 0; m_elements[2][0] = s*tx; m_elements[3][0] = 0;
    m_elements[0][1] = 0; m_elements[1][1] = s; m_elements[2][1] = s*ty; m_elements[3][1] = 0;
    m_elements[0][2] = 0; m_elements[1][2] = 0; m_elements[2][2] = 1;    m_elements[3][2] = 0;
    m_elements[0][3] = 0; m_elements[1][3] = 0; m_elements[2][3] = 0;    m_elements[3][3] = 1;
  }
};

/* Used to do two passes across set of points and produces a translation and
   scale matrix which normalizes them so they have center in 0 and scale sqrt(2).
   This improves numerical stability of homography computations  */

class
normalize_points
{
public:
  normalize_points(int nn)
  : avg_x (0), avg_y (0), dist_sum (0), n (nn)
  {
  }

  void
  account1 (point_t p, enum scanner_type type)
  {
    if (is_fixed_lens (type))
      {
	avg_x += p.x;
	avg_y += p.y;
      }
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
    dist_sum += sqrt ((p.x - avg_x) * (p.x - avg_x) + (p.y - avg_y) * (p.y - avg_y));
  }
  trans_4d_matrix get_matrix ()
  {
    return translation_scale_matrix (-avg_x, -avg_y, sqrt (2) * n / dist_sum);
  }

private:
  coord_t avg_x, avg_y;
  coord_t dist_sum;
  int n;
};

/* Produce two rows of homography equation converting point S to D at index N.  */

inline void
init_equation (gsl_matrix *A, gsl_vector *v, int n, bool invert, int flags, enum scanner_type scanner_type, point_t s, point_t d, trans_4d_matrix ts, trans_4d_matrix td)
{
  ts.perspective_transform (s.x, s.y, s.x, s.y);
  td.perspective_transform (d.x, d.y, d.x, d.y);
  if (invert)
    std::swap (s, d);
  gsl_vector_set (v, n * 2    , d.x);
  gsl_vector_set (v, n * 2 + 1, d.y);

  gsl_matrix_set (A, n * 2    , 0, s.x);
  gsl_matrix_set (A, n * 2    , 1, s.y);
  gsl_matrix_set (A, n * 2    , 2, 1);
  gsl_matrix_set (A, n * 2    , 3, 0);
  gsl_matrix_set (A, n * 2    , 4, 0);
  gsl_matrix_set (A, n * 2    , 5, 0);
  if (flags & (homography::solve_rotation | homography::solve_free_rotation))
    {
      if ((flags & homography::solve_free_rotation) || scanner_type != lens_move_horisontally)
	{
	  gsl_matrix_set (A, n * 2, 6, -d.x*s.x);
	  gsl_matrix_set (A, n * 2, 7, -d.x*s.y);
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
            gsl_matrix_set (A, n * 2 + 1, 6, -d.y*s.x);
            gsl_matrix_set (A, n * 2 + 1, 7, -d.y*s.y);
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
      gsl_matrix_set (A, n * 2 + 1, 8, -d.y*s.x);
      gsl_matrix_set (A, n * 2 + 1, 9, -d.y*s.y);
    }
}

/* Turn solution V to a matrix.
   FLAGS and TYPE deterine the setup of the equations.  If INVERSER is true we are determining inverse matrix.
   TS and TD are normalization matrices for source and destination coordinates.
   If KEEP_VECTOR is false, free vector V.  */
inline trans_4d_matrix
solution_to_matrix (gsl_vector *v, int flags, enum scanner_type type, bool inverse, trans_4d_matrix ts, trans_4d_matrix td, bool keep_vector = false)
{
  trans_4d_matrix ret;
  if (inverse)
    std::swap (ts, td);
  ret.m_elements[0][0] = gsl_vector_get (v, 0);
  ret.m_elements[1][0] = gsl_vector_get (v, 1);
  ret.m_elements[2][0] = gsl_vector_get (v, 2);
  ret.m_elements[3][0] = 0;

  ret.m_elements[0][1] = gsl_vector_get (v, 3);
  ret.m_elements[1][1] = gsl_vector_get (v, 4);
  ret.m_elements[2][1] = 0;
  ret.m_elements[3][1] = gsl_vector_get (v, 5);

  if ((flags & (homography::solve_rotation)
       && type != lens_move_horisontally)
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
  if (!keep_vector)
    gsl_vector_free (v);
  //printf ("Pre\n");
  if (debug_output)
    ret.print (stdout);
  //printf ("Td\n");
  //td.print (stdout);
  //printf ("TS\n");
  //ts.print (stdout);
  td = td.invert ();
  ret = td * ret;
  ret = ret * ts;

  //printf ("Unnorm\n");
    //ret.print (stdout);
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
      ret.m_elements[x][y]/=ret.m_elements[3][3];
  //printf ("Unnorm2\n");
    //ret.print (stdout);
  return ret;
}

/* Determine homography matrix matching points specified by POINT and N.
   Update PARAM for desired transformations.
   If solve_screen_weights or solve_image_weights are set in FLAGS then
   WCENTER_X and WCENTER_Y spedifies point where top optimize for.
   If FINAL is true output info on results.  */

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, int n, solver_parameters::solver_point_t *points,
	coord_t wcenter_x, coord_t wcenter_y,
	int flags, bool final = false)
{
  if (debug_output && final)
    {
      printf ("Old Translation %f %f\n", param->center.x, param->center.y);
      printf ("Old coordinate1 %f %f\n", param->coordinate1.x, param->coordinate1.y);
      printf ("Old coordinate2 %f %f\n", param->coordinate2.x, param->coordinate2.y);
    }
  /* Clear previous map.  */
  param->center = {(coord_t)0, (coord_t)0};
  param->coordinate1 = {(coord_t)1, (coord_t)0};
  param->coordinate2 = {(coord_t)0, (coord_t)1};
  if (flags & homography::solve_rotation)
    {
      param->tilt_x = 0;
      param->tilt_y = 0;
    }
  /* This map applies only non-linear part of corrections (that are not optimized).  */
  scr_to_img map;
  map.set_parameters (*param, img_data);

  double chisq;
  bool do_ransac = /*(flags & (homography::solve_rotation | homography::solve_free_rotation)) &&*/
    !(flags & (homography::solve_image_weights | homography::solve_screen_weights));
  trans_4d_matrix h;
  if (do_ransac)
     h = homography::get_matrix_ransac (points, n, flags,
					param->scanner_type, &map,
					wcenter_x, wcenter_y, &chisq, final);
  else
     h = homography::get_matrix (points, n, flags,
				 param->scanner_type, &map,
				 wcenter_x, wcenter_y, &chisq);
  coord_t center_x, center_y, coordinate1_x, coordinate1_y, coordinate2_x, coordinate2_y;

  /* Determine center and coordinate vectors.  */
  h.perspective_transform (0, 0, center_x, center_y);
  h.perspective_transform (1, 0, coordinate1_x, coordinate1_y);
  h.perspective_transform (0, 1, coordinate2_x, coordinate2_y);

  point_t center = map.to_img ({center_x, center_y});
  point_t coordinate1 = map.to_img ({coordinate1_x, coordinate1_y}) - center;
  point_t coordinate2 = map.to_img ({coordinate2_x, coordinate2_y}) - center;

  param->center = center;
  param->coordinate1 = coordinate1;
  param->coordinate2 = coordinate2;
  /* TODO: Can we decompose matrix in the way scr_to_img expects the parameters?  */
  if (flags & homography::solve_rotation)
    {
      coord_t tilt_x_min=-0.003, tilt_x_max=0.003;
      int tilt_x_steps = 21;
      coord_t tilt_y_min=-0.003, tilt_y_max=0.003;
      int tilt_y_steps = 21;
      coord_t minsq = INT_MAX;
      coord_t best_tilt_x = 1, best_tilt_y = 1;
      scr_to_img map2;
      map2.set_parameters (*param, img_data);
      for (int i = 0; i < 10; i++)
	{
	  coord_t txstep = (tilt_x_max - tilt_x_min) / (tilt_x_steps - 1);
	  coord_t tystep = (tilt_y_max - tilt_y_min) / (tilt_y_steps - 1);
	  for (int tx = 0; tx < tilt_x_steps; tx++)
	      for (int ty = 0; ty < tilt_y_steps; ty++)
		{
		  param->tilt_x = tilt_x_min + txstep * tx;
		  param->tilt_y = tilt_y_min + tystep * ty;
		  coord_t sq = 0;
		  map2.update_linear_parameters (*param);
		  for (int sy = -100; sy <= 100; sy+=100)
		    for (int sx = -100; sx <= 100; sx+=100)
		      {
			point_t t = map2.to_img ({(coord_t)sx, (coord_t)sy});
			point_t p;
			h.perspective_transform (sx, sy, p.x, p.y);
			p = map.to_img (p);
			sq += p.dist_sq2_from (t);
		      }

		  if (sq < minsq)
		    {
		      minsq = sq;
		      best_tilt_x = param->tilt_x;
		      best_tilt_y = param->tilt_y;
		      //printf ("Tilts %f %f %f %i\n", best_tilt_x, best_tilt_y, minsq, i);
		    }
		  }
	    param->tilt_x = best_tilt_x;
	    param->tilt_y = best_tilt_y;
	    tilt_x_min = best_tilt_x - txstep;
	    tilt_x_max = best_tilt_x + txstep;
	    tilt_y_min = best_tilt_y - tystep;
	    tilt_y_max = best_tilt_y + tystep;
	  }
	param->tilt_x = best_tilt_x;
	param->tilt_y = best_tilt_y;
	//printf ("Tilts %f %f %f\n", best_tilt_x, best_tilt_y, minsq);
#if 0
	/* Compute RQ decomposition.  */
	gsl_matrix *A = gsl_matrix_alloc (3, 3);
	gsl_matrix *P = gsl_matrix_alloc (3, 3);
	gsl_matrix *Q = gsl_matrix_alloc (3, 3);
	gsl_matrix *R = gsl_matrix_alloc (3, 3);
	gsl_vector *tau = gsl_vector_alloc (3);
      h.print (stdout);
      for (int i = 0; i < 3; i++)
	for (int j = 0; j < 3; j++)
	  gsl_matrix_set (P, i, j, 2-j==i);
      print_matrix (stdout, "P", P);

      for (int i = 0; i < 3; i++)
	for (int j = 0; j < 3; j++)
	  gsl_matrix_set (A, i, j, h.m_elements[j][i]);
      /* h is 4x4 and Y shift is in the last row.  */
      gsl_matrix_set (A, 1, 2, h.m_elements[3][1]);

      print_matrix (stdout, "A", A);
      gsl_matrix_swap_rows (A, 0, 2);
      print_matrix (stdout, "A rows reversed", A);
      gsl_matrix_transpose (A);
      print_matrix (stdout, "A transposed", A);

      gsl_linalg_QR_decomp (A, tau);
      gsl_linalg_QR_unpack (A, tau, Q, R);
      gsl_matrix_swap_rows (R, 0, 2);
      gsl_matrix_transpose (R);
      gsl_matrix_swap_rows (R, 0, 2);
      print_matrix (stdout, "R", R);
      gsl_matrix_transpose (Q);
      gsl_matrix_swap_rows (Q, 0, 2);
      print_matrix (stdout, "Q", Q);
      gsl_matrix_free (P);
      gsl_matrix_free (R);
      gsl_matrix_free (A);
      gsl_vector_free (tau);
      double sy = -gsl_matrix_get (Q, 2, 0);
      sy *= param->projection_distance;
      if (fabs (sy) > 1)
	printf ("tilt y out of range %f\n", sy);
      param->tilt_y = asin (sy) * 180 / M_PI;
      printf ("solver sy: %f tilt %f\n", sy, param->tilt_y);
      double cy = cos (asin (sy));
      double sx = gsl_matrix_get (Q, 2, 1) / cy;
      sx *= param->projection_distance;
      if (fabs (sx) > 1)
	printf ("tilt x out of range %f\n", sx);
      param->tilt_x = asin (sx) * 180 / M_PI;
      printf ("slver sx: %f tilt x %f\n", sx, param->tilt_x);
#endif
    }
  if (debug_output && final)
    {
      printf ("New Translation %f %f\n", param->center.x, param->center.y);
      printf ("New coordinate1 %f %f\n", param->coordinate1.x, param->coordinate1.y);
      printf ("New coordinate2 %f %f\n", param->coordinate2.x, param->coordinate2.y);
    }
  if (final && ((debug || debug_output)))
    {
      scr_to_img map2;
      map2.set_parameters (*param, img_data);
      //map2.m_matrix.print (stdout);
      bool found = false;
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	  point_t t = map2.to_img ({xs, ys});
	  point_t p;
	  h.perspective_transform (xs, ys, p.x, p.y);
	  p = map.to_img (p);

#if 0
	  map.to_img (px, py, &px, &py);
	  if (debug_output)
	    printf ("image: %g %g screen %g %g translated %g %g translated by solver %g %g dist %g\n", xi, yi, xs, ys, xt, yt,
		px, py, sqrt ((xt-xi)*(xt-xi)+(yt-yi)*(yt-yi)));
#endif
	  if (!p.almost_eq (t, 1))
	    {
	      printf ("Solver model mismatch %f %f should be %f %f (ideally %f %f)\n", t.x, t.y, p.x, p.y, xi, yi);
	      found = true;
	    }
	}
      if (found)
	h.print (stdout);
    }
  return chisq;
}

} 

coord_t
simple_solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
{
  if (progress)
    progress->set_task ("determing geometry by linear regression", 1);
  return solver (param, img_data, sparam.npoints, sparam.point, sparam.center.x, sparam.center.y, (sparam.weighted ? homography::solve_image_weights : 0), true);
}

namespace
{

/* Nonlinear optimizer for determining radial lens warp parameters.  */
class lens_solver
{
public:
  lens_solver (scr_to_img_parameters &param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
  : m_param (param), m_img_data (img_data), m_sparam (sparam), m_progress (progress), start {0.5,0.5,0,0,0}
  {
    if (num_coordinates () == 1)
      start[1] = 0;
  }
  scr_to_img_parameters &m_param;
  image_data &m_img_data;
  solver_parameters &m_sparam;
  progress_info *m_progress;
  static const constexpr coord_t scale_kr = 128;

  int num_coordinates ()
  {
    return is_fixed_lens (m_param.scanner_type) ? 2 : 1;
  }
  int num_values ()
  {
    return num_coordinates () + 3;
  }
  coord_t start[5];
  coord_t epsilon ()
  {
    return 0.00000001;
  }
  coord_t scale ()
  {
    return 1;
  }
  bool verbose ()
  {
    return false;
  }
  void
  constrain (coord_t *vals)
  {
    int n = num_coordinates ();
    if (vals[0] < 0)
      vals[0] = 0;
    if (vals[0] > 1)
      vals[0] = 1;
    if (n == 2)
      {
	if (vals[1] < 0)
	  vals[1] = 0;
	if (vals[1] > 1)
	  vals[1] = 1;
      }
    for (int i = n; i < n+3; i++)
      {
	if (vals[i] < -0.1 * scale_kr)
	  vals[i] = -0.1 * scale_kr;
	if (vals[i] > 0.1 * scale_kr)
	  vals[i] = 0.1 * scale_kr;
      }
  }
  coord_t
  objfunc (coord_t *vals)
  {
    static const coord_t bad_val = 100000000;
    m_param.center = {(coord_t)0, (coord_t)0};
    m_param.coordinate1 = {(coord_t)1, (coord_t)0};
    m_param.coordinate2 = {(coord_t)0, (coord_t)1};
    int n = num_coordinates ();
    if (is_fixed_lens (m_param.scanner_type))
      m_param.lens_correction.center = {vals[0], vals[1]};
    else if (m_param.scanner_type == lens_move_horisontally)
      m_param.lens_correction.center = {0, vals[0]};
    else if (m_param.scanner_type == lens_move_vertically)
      m_param.lens_correction.center = {vals[0], 0};
    m_param.lens_correction.kr[1] = vals[n] * (1 / scale_kr);
    m_param.lens_correction.kr[2] = vals[n + 1] * (1 / scale_kr);
    m_param.lens_correction.kr[3] = vals[n + 2] * (1 / scale_kr);
    if (!m_param.lens_correction.is_monotone ())
      {
	printf ("Non monotone lens correction %f %f: %f %f %f %f\n", m_param.lens_correction.center.x, m_param.lens_correction.center.y, m_param.lens_correction.kr[0], m_param.lens_correction.kr[1], m_param.lens_correction.kr[2], m_param.lens_correction.kr[3]);
	return bad_val;
      }
    m_param.lens_correction.normalize ();
    scr_to_img map;
    map.set_parameters (m_param, m_img_data);
    coord_t chi = -5;
#if 0
    /* Ransac is unstable.  */
    homography::get_matrix_ransac (m_sparam.point, m_sparam.npoints,  (m_sparam.weighted ? homography::solve_image_weights : 0) | (m_sparam.npoints > 10 ? homography::solve_rotation : 0) | homography::solve_limit_ransac_iterations,
				   m_param.scanner_type, &map, 0, 0, &chi, false);
#endif
    homography::get_matrix (m_sparam.point, m_sparam.npoints,  (m_sparam.weighted ? homography::solve_image_weights : 0) | (m_sparam.npoints > 10 ? homography::solve_rotation : 0) | homography::solve_limit_ransac_iterations,
			    m_param.scanner_type, &map, 0, 0, &chi);
#if 0
    printf ("Lens correction center %f,%f: k0 %f k1 %f k2 %f k3 %f chi %f\n", m_param.lens_correction.center.x, m_param.lens_correction.center.y, m_param.lens_correction.kr[0], m_param.lens_correction.kr[1], m_param.lens_correction.kr[2], m_param.lens_correction.kr[3], chi);
#endif
    if (!(chi >= 0 && chi < bad_val))
      {
        printf ("Bad chi %f\n", chi);
	return bad_val;
      }
    return chi;
  }
};
}

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
{
  if (sparam.npoints < 3)
    return 0;

  if (param->mesh_trans)
    abort ();

  bool optimize_lens = sparam.optimize_lens && sparam.npoints > 100;
  bool optimize_rotation = (sparam.optimize_tilt && sparam.npoints > 10);


  if (optimize_lens)
    {
      lens_solver s (*param, img_data, sparam, progress);
      simplex<coord_t, lens_solver>(s, "optimizing lens correction", progress);
      int n = s.num_coordinates ();
      if (is_fixed_lens (param->scanner_type))
	param->lens_correction.center = {s.start[0], s.start[1]};
      else if (param->scanner_type == lens_move_horisontally)
	param->lens_correction.center = {0, s.start[0]};
      else if (param->scanner_type == lens_move_vertically)
      param->lens_correction.center = {s.start[0], 0};
      param->lens_correction.kr[1] = s.start[n] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[2] = s.start[n + 1] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[3] = s.start[n + 2] * (1 / lens_solver::scale_kr);
      param->lens_correction.normalize ();
#if 0
      if (progress)
	progress->pause_stdout ();
      printf ("Lens correction center: %f,%f k0 %f k1 %f k2 %f k3 %f\n",
	      param->lens_correction.center.x, param->lens_correction.center.y,
	      param->lens_correction.kr[0], param->lens_correction.kr[1], param->lens_correction.kr[2], param->lens_correction.kr[3]);
      if (progress)
	progress->resume_stdout ();
#endif
    }
  if (progress)
    progress->set_task ("optimizing perspective correction", 1);
  return solver (param, img_data, sparam.npoints, sparam.point, sparam.center.x, sparam.center.y, (sparam.weighted ? homography::solve_image_weights : 0) | (optimize_rotation ? homography::solve_rotation : 0), true);
}

static void
compute_mesh_point (solver_parameters &sparam, scanner_type type,
                    mesh *mesh_trans, int x, int y)
{
  int_point_t e = {x, y};
  point_t scrp = mesh_trans->get_screen_point (e);
  trans_4d_matrix h = homography::get_matrix (
      sparam.point, sparam.npoints,
      homography::solve_screen_weights /*homography::solve_limit_ransac_iterations
                                          | homography::solve_free_rotation*/
      ,
      type, NULL, scrp.x, scrp.y, NULL);
  point_t imgp;
  h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);

  /* We need to set weight assymetrically based on image distance.  Problem is
     that without knowing the image distance we can not set one, so iteratively
     find right one.  */
  if (type != fixed_lens)
    {
      int i;
      for (i = 0; i < 100; i++)
        {
          point_t last_imgp = imgp;
          trans_4d_matrix h = homography::get_matrix (
              sparam.point, sparam.npoints,
              homography::solve_image_weights,
	      /*homography::solve_limit_ransac_iterations | homography::solve_free_rotation*/
              type, NULL, imgp.x, imgp.y, NULL);
          h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}

mesh *
solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
{
  if (sparam.npoints < 10)
    return NULL;
  int xshift, yshift, width, height;
  int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  map.get_range (img_data.width, img_data.height, &xshift, &yshift, &width, &height);
  width = (width + step - 1) / step;
  height = (height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh", width * height);
  mesh *mesh_trans = new mesh (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(progress, xshift, yshift, step, width, height, sparam, img_data, mesh_trans, param)
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
       if (!progress || !progress->cancel_requested ())
	  {
	    compute_mesh_point (sparam, param->scanner_type, mesh_trans, x, y);
	    if (progress)
	      progress->inc_progress ();
	  }
  int miter = width + height;
  if (progress)
    progress->set_task ("growing mesh", miter);
  while (miter > 0)
    {
      int grow_left = mesh_trans->need_to_grow_left (img_data.width, img_data.height) ? 1 : 0;
      int grow_right = mesh_trans->need_to_grow_right (img_data.width, img_data.height) ? 1 : 0;
      int grow_top = mesh_trans->need_to_grow_top (img_data.width, img_data.height) ? 1 : 0;
      int grow_bottom = mesh_trans->need_to_grow_bottom (img_data.width, img_data.height) ? 1 : 0;
      miter --;
      if (!grow_left && !grow_right && !grow_top && !grow_bottom)
	break;
      if (progress && progress->cancel_requested ())
	{
	  delete mesh_trans;
	  return NULL;
	}
      if (!mesh_trans->grow (grow_left, grow_right, grow_top, grow_bottom))
	break;
      if (grow_left || grow_right)
        {
	  for (int y = 0; y < mesh_trans->get_height (); y++)
	    {
	      if (grow_left)
	        compute_mesh_point (sparam, param->scanner_type, mesh_trans, 0, y);
	      if (grow_right)
	        compute_mesh_point (sparam, param->scanner_type, mesh_trans, mesh_trans->get_width () - 1, y);
	    }
        }
      if (grow_top || grow_bottom)
        {
	  for (int x = 0; x < mesh_trans->get_width (); x++)
	    {
	      if (grow_top)
	        compute_mesh_point (sparam, param->scanner_type, mesh_trans, x, 0);
	      if (grow_bottom)
	        compute_mesh_point (sparam, param->scanner_type, mesh_trans, x, mesh_trans->get_height () - 1);
	    }
        }
      if (progress)
	progress->inc_progress ();
    }
  if (!miter)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Maximum number of iterations reached.\n");
      if (progress)
	progress->resume_stdout ();
    }
  if (progress && progress->cancel_requested ())
    {
      delete mesh_trans;
      return NULL;
    }
  if (progress && progress->cancel_requested ())
    {
      delete mesh_trans;
      return NULL;
    }
  //mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}

static void
compute_mesh_point (screen_map &smap, solver_parameters &sparam,
                    scr_to_img_parameters &lparam, image_data &img_data,
                    mesh *mesh_trans, int x, int y)
{
  int_point_t e = { x, y };
  point_t scrp = mesh_trans->get_screen_point (e);
  smap.get_solver_points_nearby (scrp.x, scrp.y, 100, sparam);
  trans_4d_matrix h = homography::get_matrix (
      sparam.point, sparam.npoints, homography::solve_screen_weights,
      lparam.scanner_type, NULL, scrp.x, scrp.y, NULL);
  point_t imgp;
  h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);
  /* We need to set weight assymetrically based on image distance.  Problem is
     that without knowing the image distance we can not set one, so iteratively
     find right one.  */
  if (lparam.scanner_type != fixed_lens)
    {
      int i;
      for (i = 0; i < 100; i++)
        {
          point_t last_imgp = imgp;
          trans_4d_matrix h = homography::get_matrix (
              sparam.point, sparam.npoints,
              homography:: solve_image_weights,
              lparam.scanner_type, NULL, imgp.x, imgp.y, NULL);
          h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}
mesh *
solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam2, screen_map &smap, progress_info *progress)
{
  int xshift, yshift, width, height;
  const int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  map.get_range (img_data.width, img_data.height, &xshift, &yshift, &width, &height);
  width = (width + step - 1) / step;
  height = (height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh from detected points", width * height);
  mesh *mesh_trans = new mesh (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2) shared(progress, xshift, yshift, step, width, height, img_data, mesh_trans, param, smap)
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (!progress || !progress->cancel_requested ())
	{
	  // TODO: copying motor corrections is unnecesary and expensive.
	  scr_to_img_parameters lparam = *param;
	  solver_parameters sparam;/* = sparam2;*/
	  compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, x, y);
	  if (progress)
	    progress->inc_progress ();
	}
  scr_to_img_parameters lparam = *param;
  int miter = width + height;
  if (progress)
    progress->set_task ("growing mesh", miter);
  while (miter > 0)
    {
      int grow_left = mesh_trans->need_to_grow_left (img_data.width, img_data.height) ? 1 : 0;
      int grow_right = mesh_trans->need_to_grow_right (img_data.width, img_data.height) ? 1 : 0;
      int grow_top = mesh_trans->need_to_grow_top (img_data.width, img_data.height) ? 1 : 0;
      int grow_bottom = mesh_trans->need_to_grow_bottom (img_data.width, img_data.height) ? 1 : 0;
      miter --;
      if (!grow_left && !grow_right && !grow_top && !grow_bottom)
	break;
      if (progress && progress->cancel_requested ())
	{
	  delete mesh_trans;
	  return NULL;
	}
      if (!mesh_trans->grow (grow_left, grow_right, grow_top, grow_bottom))
	break;
      solver_parameters sparam;/* = sparam2;*/
      if (grow_left || grow_right)
        {
	  for (int y = 0; y < mesh_trans->get_height (); y++)
	    {
	      if (grow_left)
		compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, 0, y);
	      if (grow_right)
		compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, mesh_trans->get_width () - 1, y);
	    }
        }
      if (grow_top || grow_bottom)
        {
	  for (int x = 0; x < mesh_trans->get_width (); x++)
	    {
	      if (grow_top)
		compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, x, 0);
	      if (grow_bottom)
		compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, x, mesh_trans->get_height () - 1);
	    }
        }
      if (progress)
	progress->inc_progress ();
    }
  if (!miter)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Maximum number of iterations reached.\n");
      if (progress)
	progress->resume_stdout ();
    }
  if (progress && progress->cancel_requested ())
    {
      delete mesh_trans;
      return NULL;
    }
  //mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}

solver_parameters::point_location *
solver_parameters::get_point_locations (enum scr_type type, int *n)
{
  static struct point_location paget_points[] =
    {
      /* Green.  */
      {0, 0, solver_parameters::green}, {1, 0, solver_parameters::green}, {0, 1, solver_parameters::green},
      {1, 1, solver_parameters::green}, {0.5, 0.5, solver_parameters::green},
      /* Red  */
      {0,  0.5, solver_parameters::red},  {0.5, 0, solver_parameters::red},
      {1, 0.5, solver_parameters::red}, {0.5, 1, solver_parameters::red}
    };
  static struct point_location dufay_points[] =
    {
      /* Green.  */
      {0, 0, solver_parameters::green},
      {0.5, 0, solver_parameters::blue},
      {1, 0, solver_parameters::green},
      {0, 1, solver_parameters::green},
      {0.5, 1, solver_parameters::blue},
      {1, 1, solver_parameters::green},
    };

  switch (type)
    {
      case Paget:
      case Thames:
      case Finlay:
	*n = sizeof (paget_points)/sizeof (point_location);
	return paget_points;
      case Dufay:
	*n = sizeof (dufay_points)/sizeof (point_location);
	return dufay_points;
      default: abort ();
    }
}


#if 0
static double
compute_chisq (solver_parameters::point_t *points, int n, trans_4d_matrix homography)
{
  double chisq = 0;
  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      coord_t xt, yt;
      homography.perspective_transform (xs, ys, xt, yt);
      chisq += (xt - xi) * (xt - xi) + (yt - yi) * (yt - yi);
    }
  return chisq;
}
#endif

static double
screen_compute_chisq (solver_parameters::solver_point_t *points, int n, trans_4d_matrix homography)
{
  double chisq = 0;
  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      coord_t xt, yt;
      homography.inverse_perspective_transform (xi, yi, xt, yt);
      coord_t dist = (xt - xs) * (xt - xs) + (yt - ys) * (yt - ys);
      if (dist > 10000)
	dist = 10000;
      chisq += dist;
    }
  return chisq;
}



/* Return homography matrix determined from POINTS using least squares
   method.  Trainsform image coordinates by MAP if non-NULL.
   If FLAGS is set to solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER_X and WCENTER_Y.
   If CHISQ_RET is non-NULL initialize it to square of errors.  */
trans_4d_matrix
homography::get_matrix_ransac (solver_parameters::solver_point_t *points, int n, int flags,
			       enum scanner_type scanner_type,
			       scr_to_img *map,
			       coord_t wcenter_x, coord_t wcenter_y,
			       coord_t *chisq_ret, bool final)
{
  unsigned int seed = 0;
  int niterations = 500;
  int nvariables = equation_variables (flags);
  int nsamples = nvariables / 2;
  trans_4d_matrix ret;
  solver_parameters::solver_point_t *tpoints = points;
  int max_inliners = 0;
  double min_chisq = INT_MAX;
  double min_inliner_chisq = INT_MAX;
  int iteration;
  coord_t dist = 1;

  /* Fix non-fixed lens the rotations are specified only by X or Y coordinates.
     We need enough variables in that system, so just double number of samples.
     ??? 5 values are enough  */
  if ((flags & homography::solve_rotation)
      && !is_fixed_lens (scanner_type))
    nsamples ++;
  gsl_matrix *A = gsl_matrix_alloc (nsamples * 2, nvariables);
  gsl_vector *v = gsl_vector_alloc (nsamples * 2);
  if (map)
    {
      tpoints = (solver_parameters::solver_point_t *)malloc (sizeof (solver_parameters::solver_point_t) * n);
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	  point_t p = map->to_scr ({xi, yi});
	  tpoints[i].img_x = p.x;
	  tpoints[i].img_y = p.y;
	  tpoints[i].screen_x = xs;
	  tpoints[i].screen_y = ys;
	}
    }
  if (nsamples > n)
    {
      fprintf (stderr, "Too few samples in RANSAC\n");
      abort ();
    }

  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();

  gsl_vector *i_c = NULL;
  gsl_matrix *i_cov = NULL;
  gsl_multifit_linear_workspace * i_work = NULL;

  /* FIXME: HH method does not seem to work.  */
  if (nsamples * 2 != nvariables)
    {
      i_c = gsl_vector_alloc (nvariables);
      i_cov = gsl_matrix_alloc (nvariables, nvariables);
      i_work = gsl_multifit_linear_alloc (nsamples * 2, nvariables);
    }

  for (iteration = 0; iteration < niterations; iteration++)
    {
      const int maxsamples = 10;
      int sample[maxsamples];
      bool colinear = false;
      int nattempts = 0;
      trans_4d_matrix ts;
      trans_4d_matrix td;
      do {
	nattempts++;
	/* Produce random sample.  */
	for (int i = 0 ; i < nsamples; i++)
	  {
	    bool ok;
	    do {
	      sample[i] = fast_rand32 (&seed) % n;
	      ok = true;
	      for (int j = 0; j < i; j++)
		if (sample[i] == sample[j])
		  {
		    ok = false;
		    break;
		  }
	    } while (!ok);
	  }

	/* Normalize input.  */
	normalize_points scrnorm (nsamples), imgnorm (nsamples);
	for (int i = 0; i < nsamples; i ++)
	  {
	    int p = sample[i];
	    scrnorm.account1 ({tpoints[p].screen_x, tpoints[p].screen_y}, scanner_type);
	    imgnorm.account1 ({tpoints[p].img_x, tpoints[p].img_y}, scanner_type);
	  }
	scrnorm.finish1();
	imgnorm.finish1();
	for (int i = 0; i < nsamples; i ++)
	  {
	    int p = sample[i];
	    scrnorm.account2 ({tpoints[p].screen_x, tpoints[p].screen_y});
	    imgnorm.account2 ({tpoints[p].img_x, tpoints[p].img_y});
	  }

	ts = scrnorm.get_matrix ();
	td = imgnorm.get_matrix ();
	/* Proudce equations.  */
	for (int i = 0; i < nsamples; i ++)
	  {
	    int p = sample[i];
	    coord_t xi = tpoints[p].img_x, yi = tpoints[p].img_y, xs = tpoints[p].screen_x, ys = tpoints[p].screen_y;
	    init_equation (A, v, i, false, flags, scanner_type, {xs, ys}, {xi, yi}, ts, td);
	  }
	if (!i_work)
	  colinear = (gsl_linalg_HH_svx (A, v) != GSL_SUCCESS);
	else
	  {
	    double chisq;
	    colinear = (gsl_multifit_linear (A, v, i_c, i_cov, &chisq, i_work)) != GSL_SUCCESS;
	    /* This can not be vector_memcpy since sizes of vectors does not match.  */
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

      trans_4d_matrix cur = solution_to_matrix (v, flags, scanner_type, false, ts, td, true);
      //cur.print (stdout);
      int ninliners = 0;
      double cur_chisq = 0, cur_inliner_chisq = 0;
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = tpoints[i].img_x, yi = tpoints[i].img_y, xs = tpoints[i].screen_x, ys = tpoints[i].screen_y;
	  coord_t xt, yt;
	  cur.perspective_transform (xs, ys, xt, yt);
	  cur_chisq += (xt - xi) * (xt - xi) + (yt - yi) * (yt - yi);
	  if (fabs (xt - xi) <= dist && fabs (yt - yi) <= dist)
	    {
	      ninliners++;
	      cur_inliner_chisq += (xt - xi) * (xt - xi) + (yt - yi) * (yt - yi);
	    }
	}
      if (ninliners < nsamples)
	continue;
      if ((ninliners > max_inliners)
	  || (ninliners == max_inliners && cur_inliner_chisq < min_inliner_chisq))
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
	      niterations = log (1 - 0.99) / log (1 - pow(ninliners / (coord_t)n, 4));
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

  //if (final)
    //printf ("Iteration %i inliners %i out of %i, chisq %f inliner chisq %f\n", iteration, max_inliners, n, min_chisq, min_inliner_chisq);
  if (max_inliners > nsamples)
    {
      gsl_matrix *X = gsl_matrix_alloc (max_inliners * 2, nvariables);
      gsl_vector *y = gsl_vector_alloc (max_inliners * 2);
      gsl_vector *c = gsl_vector_alloc (nvariables);
      gsl_matrix *cov = gsl_matrix_alloc (nvariables, nvariables);

      trans_4d_matrix ts;
      trans_4d_matrix td;
      normalize_points scrnorm (max_inliners), imgnorm (max_inliners);
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = tpoints[i].img_x, yi = tpoints[i].img_y, xs = tpoints[i].screen_x, ys = tpoints[i].screen_y;
	  coord_t xt, yt;
	  ret.perspective_transform (xs, ys, xt, yt);
	  if (fabs (xt - xi) <= dist && fabs (yt - yi) <= dist)
	    {
	      scrnorm.account1 ({xs, ys}, scanner_type);
	      imgnorm.account1 ({xi, yi}, scanner_type);
	    }
	}
      scrnorm.finish1();
      imgnorm.finish1();
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = tpoints[i].img_x, yi = tpoints[i].img_y, xs = tpoints[i].screen_x, ys = tpoints[i].screen_y;
	  coord_t xt, yt;
	  ret.perspective_transform (xs, ys, xt, yt);
	  if (fabs (xt - xi) <= dist && fabs (yt - yi) <= dist)
	    {
	      scrnorm.account2 ({xs, ys});
	      imgnorm.account2 ({xi, yi});
	    }
	}
      ts = scrnorm.get_matrix ();
      td = imgnorm.get_matrix ();

      int p = 0;
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = tpoints[i].img_x, yi = tpoints[i].img_y, xs = tpoints[i].screen_x, ys = tpoints[i].screen_y;
	  coord_t xt, yt;
	  ret.perspective_transform (xs, ys, xt, yt);
	  if (fabs (xt - xi) <= dist && fabs (yt - yi) <= dist)
	    {
	      init_equation (X, y, p, false, flags, scanner_type, {xs, ys}, {xi, yi}, ts, td);
	      p++;
	    }
	}
      gsl_multifit_linear_workspace * work
	= gsl_multifit_linear_alloc (n*2, nvariables);
      gsl_multifit_linear (X, y, c, cov, &min_chisq, work);
      gsl_multifit_linear_free (work);
      gsl_matrix_free (X);
      gsl_vector_free (y);
      gsl_matrix_free (cov);
      ret = solution_to_matrix (c, flags, scanner_type, false, ts, td);
      /* Use screen so we do not get biass with lens correction.  */
      min_chisq = screen_compute_chisq (tpoints, n, ret);
    }
  else if (final)
    printf ("Failed to find inliners for ransac\n");
  //if (final)
    //printf ("chisq %f after least squares\n", min_chisq);

  if (chisq_ret)
    *chisq_ret = min_chisq;
  if (map)
    free (tpoints);
  return ret;
}

/* Return homography matrix determined from POINTS using least squares
   method.  Trainsform image coordinates by MAP if non-NULL.
   If FLAGS is set to solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER_X and WCENTER_Y.
   If CHISQ_RET is non-NULL initialize it to square of errors.  */
trans_4d_matrix
homography::get_matrix (solver_parameters::solver_point_t *points, int n, int flags,
			enum scanner_type scanner_type,
			scr_to_img *map,
			coord_t wcenter_x, coord_t wcenter_y,
			coord_t *chisq_ret)
{
  int nvariables = equation_variables (flags);
  int nequations = n * 2;
  gsl_matrix *X, *cov;
  gsl_vector *y, *w = NULL, *c;
  normalize_points scrnorm (n), imgnorm (n);
  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      point_t p;
      /* Apply non-linear transformations.  */
      if (map)
        p = map->to_scr ({xi, yi});
      else
	p.x = xi, p.y = yi;
      scrnorm.account1 ({xs, ys}, scanner_type);
      imgnorm.account1 (p, scanner_type);
    }
  scrnorm.finish1();
  imgnorm.finish1();
  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      point_t p;
      /* Apply non-linear transformations.  */
      if (map)
        p = map->to_scr ({xi, yi});
      else
	p.x = xi, p.y = yi;
      scrnorm.account2 ({xs, ys});
      imgnorm.account2 (p);
    }
  trans_4d_matrix ts = scrnorm.get_matrix ();
  trans_4d_matrix td = imgnorm.get_matrix ();
  coord_t xscale = 1;
  coord_t yscale = 1;
  /* For moving lens, take into account that in one direction geometry changes a lot
     more than in the other.  */
  if (scanner_type == lens_move_horisontally)
    xscale = 100;
  if (scanner_type == lens_move_vertically)
    yscale = 100;
  coord_t normscale = 0, sumscale = 0;
  std::vector <coord_t> weights ((flags & (solve_screen_weights | solve_image_weights)) ? n : 0);
  if (flags & solve_image_weights)
    for (int i = 0; i < n; i++)
      {
	coord_t dist = /*sqrt*/ ((points[i].img_x - wcenter_x) * (points[i].img_x - wcenter_x) * (xscale * xscale) + (points[i].img_y - wcenter_y) * (points[i].img_y - wcenter_y) * (yscale * yscale));
	dist *= dist;
	double weight = 1 / (dist + 0.5);
	weights[i] = weight;
	normscale = std::max (normscale, weight);
	sumscale += weight;
      }
  else if (flags & solve_screen_weights)
    for (int i = 0; i < n; i++)
      {
	coord_t dist = sqrt ((points[i].screen_x - wcenter_x) * (points[i].screen_x - wcenter_x) + (points[i].screen_y - wcenter_y) * (points[i].screen_y - wcenter_y));
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
	  do {
	    nequations = 0;
	    for (int i = 0; i < n; i++)
	      if (weights[i] >= minscale)
		nequations += 2;
	    if (nequations >= 2 * 100)
	      break;
	    minscale *= (coord_t)(1.0/8);
	  } while (true);
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
	      nequations += 2;
	      if (!weights[i])
		abort ();
	    }
	  else
	    weights[i] = 0;
	}
      if (nequations * 2 < nvariables )
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
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      point_t p;
      /* Apply non-linear transformations.  */
      if (map)
        p = map->to_scr ({xi, yi});
      else
	p = {xi, yi};

      init_equation (X, y, eq, false, flags, scanner_type, {xs, ys}, p, ts, td);

      if (w)
        {
	  gsl_vector_set (w, eq * 2, weights[i]);
	  gsl_vector_set (w, eq * 2 + 1, weights[i]);
        }
      eq++;
    }
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (nequations, nvariables);
  double chisq;
  if (w)
    gsl_multifit_wlinear (X, w, y, c, cov,
			  &chisq, work);
  else
    gsl_multifit_linear (X, y, c, cov, &chisq, work);
  gsl_multifit_linear_free (work);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  if (w)
    gsl_vector_free (w);
  gsl_matrix_free (cov);
  trans_4d_matrix ret = solution_to_matrix (c, flags, scanner_type, false, ts, td);
  /* We normalize equations and thus chisq is unnaturaly small.
     To make get same range as in ransac we need to recompute.  */
  if (chisq_ret)
    {
      std::unique_ptr <solver_parameters::solver_point_t []> tpoints (new solver_parameters::solver_point_t[n]);
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	  point_t p = map->to_scr ({xi, yi});
	  tpoints[i].img_x = p.x;
	  tpoints[i].img_y = p.y;
	  tpoints[i].screen_x = xs;
	  tpoints[i].screen_y = ys;
	}
      /* Use screen so we do not get biass with lens correction.  */
      *chisq_ret = screen_compute_chisq (tpoints.get(), n, ret);
    }
  return ret;
}

/* Unused and bitrotten.  */
#if 0
/* Return homography matrix M.
   Applying M.perspective_transofrm on (0,0) will yield to ZERO.
   Applying M.perspective_transofrm on (1,0) will yield to X.
   Applying M.perspective_transofrm on (0,1) will yield to Y.
   Applying M.perspective_transofrm on (1,1) will yield to XPY (X+Y).
   If INVERT is true then inverse of this transofrmation is computed.

   This assumes fixed lens projection.
 
   FIXME: Does not work with non-fixed lens scanner types since they need more points. */
trans_4d_matrix
homography::get_matrix_4points (bool invert, enum scanner_type type, point_t zero, point_t x, point_t y, point_t xpy)
{
  gsl_matrix *A = gsl_matrix_alloc (8, 8);
  gsl_vector *v = gsl_vector_alloc (8);
  trans_4d_matrix id;
  normalize_points scrnorm (5), imgnorm (5);
  scrnorm.account1 ({0, 0}, type);
  imgnorm.account1 (zero, type);
  scrnorm.account1 ({1000, 0}, type);
  imgnorm.account1 (x, type);
  scrnorm.account1 ({0, 1000}, type);
  imgnorm.account1 (y, type);
  scrnorm.account1 ({1000, 1000}, type);
  imgnorm.account1 (xpy, type);
  scrnorm.finish1();
  imgnorm.finish1();
  scrnorm.account2 ({0, 0});
  imgnorm.account2 (zero);
  scrnorm.account2 ({1000, 0});
  imgnorm.account2 (x);
  scrnorm.account2 ({0, 1000});
  imgnorm.account2 (y);
  scrnorm.account2 ({1000, 1000});
  imgnorm.account2 (xpy);
  trans_4d_matrix ts = scrnorm.get_matrix ();
  trans_4d_matrix td = imgnorm.get_matrix ();
  init_equation (A, v, 0, invert, solve_free_rotation, fixed_lens, {0, 0}, zero, ts, td);
  init_equation (A, v, 1, invert, solve_free_rotation, fixed_lens, {1000, 0}, x, ts, td);
  init_equation (A, v, 2, invert, solve_free_rotation, fixed_lens, {0, 1000}, y, ts, td);
  init_equation (A, v, 3, invert, solve_free_rotation, fixed_lens, {1000, 1000}, xpy, ts, td);

  if (debug_output)
    print_system (stdout, A,v);
  gsl_linalg_HH_svx (A, v);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_free_rotation, fixed_lens, invert, ts, td);

  if (debug_output)
    print_system (stdout, A,v);
  gsl_linalg_HH_svx (A, v);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_rotation, type, false, id, id);
}
#endif

/* Return homography matrix M.
   Applying M.perspective_transofrm on (0,0) will yield to ZERO.
   Applying M.perspective_transofrm on (1,0) will yield to X.
   Applying M.perspective_transofrm on (0,1) will yield to Y.
   Applying M.perspective_transofrm on (1,1) will yield to XPY (X+Y).
   Applying M.perspective_transofrm on (2,3) will yield to TXPY (2*X+3*Y).
   If INVERT is true then inverse of this transofrmation is computed.
   
   This works for either scanner type.  */
trans_4d_matrix
homography::get_matrix_5points (bool invert, enum scanner_type scanner_type, point_t zero, point_t x, point_t y, point_t xpy, point_t txpy)
{
  gsl_matrix *A = gsl_matrix_alloc (10, 10);
  gsl_vector *v = gsl_vector_alloc (10);
  normalize_points scrnorm (5), imgnorm (5);
  scrnorm.account1 ({0, 0}, scanner_type);
  imgnorm.account1 (zero, scanner_type);
  scrnorm.account1 ({1000, 0}, scanner_type);
  imgnorm.account1 (x, scanner_type);
  scrnorm.account1 ({0, 1000}, scanner_type);
  imgnorm.account1 (y, scanner_type);
  scrnorm.account1 ({1000, 1000}, scanner_type);
  imgnorm.account1 (xpy, scanner_type);
  scrnorm.account1 ({2000, 3000}, scanner_type);
  imgnorm.account1 (txpy, scanner_type);
  scrnorm.finish1();
  imgnorm.finish1();
  scrnorm.account2 ({0, 0});
  imgnorm.account2 (zero);
  scrnorm.account2 ({1000, 0});
  imgnorm.account2 (x);
  scrnorm.account2 ({0, 1000});
  imgnorm.account2 (y);
  scrnorm.account2 ({1000, 1000});
  imgnorm.account2 (xpy);
  scrnorm.account2 ({2000, 3000});
  imgnorm.account2 (txpy);
  trans_4d_matrix ts = scrnorm.get_matrix ();
  trans_4d_matrix td = imgnorm.get_matrix ();
  init_equation (A, v, 0, invert, solve_free_rotation, fixed_lens, {0, 0}, zero, ts, td);
  init_equation (A, v, 1, invert, solve_free_rotation, fixed_lens, {1000, 0}, x, ts, td);
  init_equation (A, v, 2, invert, solve_free_rotation, fixed_lens, {0, 1000}, y, ts, td);
  init_equation (A, v, 3, invert, solve_free_rotation, fixed_lens, {1000, 1000}, xpy, ts, td);
  init_equation (A, v, 4, invert, solve_free_rotation, fixed_lens, {2000, 3000}, txpy, ts, td);

  if (debug_output)
    print_system (stdout, A,v);
  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  gsl_linalg_HH_svx (A, v);
  gsl_set_error_handler (old_handler);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_free_rotation, fixed_lens, invert, ts, td);
}

/* Debug facility.  */
int
print_matrix (FILE *f, const char *name, const gsl_matrix *m)
{
  int status, n = 0;
  printf ("Matrix %s\n", name);

  for (size_t i = 0; i < m->size1; i++)
    {
      for (size_t j = 0; j < m->size2; j++)
	{
	  if ((status = fprintf (f, "%4.2f ", gsl_matrix_get (m, i, j))) < 0)
	    return -1;
	  n += status;
	}

      if ((status = fprintf (f, "\n")) < 0)
	return -1;
      n += status;
    }

  return n;
}

int
print_system (FILE *f, const gsl_matrix *m, gsl_vector *v, gsl_vector *w, gsl_vector *c)
{
  int status, n = 0;

  printf ("Solution:\n");

  if (c)
    for (size_t i = 0; i < m->size2; i++)
      {
	if ((status = fprintf (f, "%+7.4f ", gsl_vector_get (c, i))) < 0)
	  return -1;
	n += status;
      }
  printf ("\n");

  for (size_t i = 0; i < m->size1; i++)
    {
      double sol = 0;
      for (size_t j = 0; j < m->size2; j++)
	{
	  if ((status = fprintf (f, "%+7.4f ", gsl_matrix_get (m, i, j))) < 0)
	    return -1;
	  n += status;
	  if (c)
	    sol += gsl_vector_get (c, j) * gsl_matrix_get (m, i, j);
	}

      if ((status = fprintf (f, "| %+7.4f", gsl_vector_get (v, i))) < 0)
	return -1;
      n += status;

      if (w)
	{
	  if ((status =
	       fprintf (f, " (weight %+7.4f)", gsl_vector_get (w, i))) < 0)
	    return -1;
	  n += status;
	}
      if (c)
	{
	  if ((status = fprintf (f, " (solution %+7.4f; error %f)", sol, sol - gsl_vector_get (v, i)) < 0))
	    return -1;
	  n += status;
	}

      if ((status = fprintf (f, "\n")) < 0)
	return -1;
      n += status;
    }

  return n;
}

