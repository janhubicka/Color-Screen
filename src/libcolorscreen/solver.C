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
#include "gsl-solver.h"
#include "homography.h"
namespace colorscreen
{
const char *const solver_parameters::point_color_names[(int)max_point_color]
    = { "red", "green", "blue" };

namespace
{
bool debug_output = false;
bool debug = colorscreen_checking;

/* Determine homography matrix matching points specified by POINT and N.
   Update PARAM for desired transformations.
   If solve_screen_weights or solve_image_weights are set in FLAGS then
   WCENTER_X and WCENTER_Y spedifies point where top optimize for.
   If FINAL is true output info on results.  */

coord_t
solver (scr_to_img_parameters *param, image_data &img_data,
        std::vector<solver_parameters::solver_point_t> &points,
        point_t wcenter, int flags, bool final_run = false)
{
  if (debug_output && final_run)
    {
      printf ("Old Translation %f %f\n", param->center.x, param->center.y);
      printf ("Old coordinate1 %f %f\n", param->coordinate1.x,
              param->coordinate1.y);
      printf ("Old coordinate2 %f %f\n", param->coordinate2.x,
              param->coordinate2.y);
    }
  /* Clear previous map.  */
  param->center = { (coord_t)0, (coord_t)0 };
  param->coordinate1 = { (coord_t)1, (coord_t)0 };
  param->coordinate2 = { (coord_t)0, (coord_t)1 };
  if (flags & homography::solve_rotation)
    {
      param->tilt_x = 0;
      param->tilt_y = 0;
    }
  /* This map applies only non-linear part of corrections (that are not
     optimized).  */
  scr_to_img map;
  map.set_parameters (*param, img_data);

  double chisq;
  if (screen_with_vertical_strips_p (param->type))
    flags |= homography::solve_vertical_strips;
  bool do_ransac = !(flags
		     & (homography::solve_image_weights
			| homography::solve_screen_weights));

  trans_4d_matrix h;
  if (do_ransac)
    h = homography::get_matrix_ransac (points, flags, param->scanner_type,
                                       &map, wcenter, &chisq, final_run);
  else
    h = homography::get_matrix (points, flags, param->scanner_type, &map,
                                wcenter, &chisq);
  coord_t center_x, center_y, coordinate1_x, coordinate1_y, coordinate2_x,
      coordinate2_y;

  /* Determine center and coordinate vectors.  */
  h.perspective_transform (0, 0, center_x, center_y);
  h.perspective_transform (1, 0, coordinate1_x, coordinate1_y);
  h.perspective_transform (0, 1, coordinate2_x, coordinate2_y);

  param->center = map.inverse_early_correction ({ center_x, center_y });
  param->coordinate1 = map.inverse_early_correction ({ coordinate1_x, coordinate1_y }) - param->center;
  param->coordinate2 = map.inverse_early_correction ({ coordinate2_x, coordinate2_y }) - param->center;
  /* TODO: Can we decompose matrix in the way scr_to_img expects the
     parameters?  */
  coord_t minsq = INT_MAX;
  coord_t best_tilt_x = 1, best_tilt_y = 1;
  if (flags & homography::solve_rotation)
    {
      coord_t tilt_x_min = -0.003, tilt_x_max = 0.003;
      int tilt_x_steps = 21;
      coord_t tilt_y_min = -0.003, tilt_y_max = 0.003;
      int tilt_y_steps = 21;
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
		for (int sy = -100; sy <= 100; sy += 100)
		  for (int sx = -100; sx <= 100; sx += 100)
		    {
		      point_t t = map2.to_img ({ (coord_t)sx, (coord_t)sy });
		      point_t p;
		      h.perspective_transform (sx, sy, p.x, p.y);
		      p = map.inverse_early_correction (p);
		      sq += p.dist_sq2_from (t);
		    }

                if (sq < minsq)
                  {
                    minsq = sq;
                    best_tilt_x = param->tilt_x;
                    best_tilt_y = param->tilt_y;
                     //printf ("Tilts %f %f %f %i\n", best_tilt_x, best_tilt_y,  minsq, i);
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
  if (debug_output && final_run)
    {
      printf ("New Translation %f %f\n", param->center.x, param->center.y);
      printf ("New coordinate1 %f %f\n", param->coordinate1.x,
              param->coordinate1.y);
      printf ("New coordinate2 %f %f\n", param->coordinate2.x,
              param->coordinate2.y);
    }
  /* This tests that we found corect rotations and homography matrix determined
     corresponds to what is in the scr_to_img parameters.  */
  if (final_run && ((debug || debug_output)))
    {
      scr_to_img map2;
      map2.set_parameters (*param, img_data);
      // map2.m_matrix.print (stdout);
      bool found = false;
      /* For vertical strips we need to compare screen coordinates only,
         since solver points have no meaningful y coordinate.  */
      if (!(flags & homography::solve_vertical_strips))
	for (auto point : points)
	  {
	    point_t img = point.img;
	    point_t scr = point.scr;
	    point_t t = map2.to_img (scr);
	    point_t p;
	    h.perspective_transform (scr.x, scr.y, p.x, p.y);
	    p = map.inverse_early_correction (p);
#if 0
	    map.to_img (px, py, &px, &py);
	    if (debug_output)
	      printf ("image: %g %g screen %g %g translated %g %g translated by solver %g %g dist %g\n", xi, yi, xs, ys, xt, yt,
		  px, py, sqrt ((xt-xi)*(xt-xi)+(yt-yi)*(yt-yi)));
#endif
	    if (!p.almost_eq (t, 1))
	      {
		printf ("Solver model mismatch %f %f should be %f %f (ideally "
			"%f %f)\n",
			t.x, t.y, p.x, p.y, img.x, img.y);
		found = true;
	      }
	  }
      else
	for (auto point : points)
	  {
	    point_t img = point.img;
	    point_t t = map2.to_scr (img);
	    point_t p = map.apply_early_correction (img);
	    h.inverse_perspective_transform (p.x, p.y, p.x, p.y);
	    if (!p.almost_eq (t, 0.03))
	      {
		printf ("Solver model mismatch %f %f should be %f %f (ideally "
			"first coord %f)\n",
			t.x, t.y, p.x, p.y, point.scr.x);
		found = true;
	      }
	  }
      if (found)
        {
          h.print (stdout);
          printf ("Tilts %f %f %f\n", best_tilt_x, best_tilt_y, minsq);
        }
    }
  return chisq;
}
/* Nonlinear optimizer for determining radial lens warp parameters.  */
class lens_solver
{
public:
  lens_solver (scr_to_img_parameters &param, image_data &img_data,
               solver_parameters &sparam, progress_info *progress)
      : m_param (param), m_img_data (img_data), m_sparam (sparam),
        m_progress (progress), start{ 0.5, 0.5, 0, 0, 0 }
  {
    if (num_coordinates () == 1)
      start[1] = 0;
  }
  scr_to_img_parameters &m_param;
  image_data &m_img_data;
  solver_parameters &m_sparam;
  progress_info *m_progress;
  static const constexpr coord_t scale_kr = 128;

  int
  num_coordinates ()
  {
    return is_fixed_lens (m_param.scanner_type) ? 2 : 1;
  }
  int
  num_values ()
  {
    return num_coordinates () + 3;
  }
  coord_t start[5];
  coord_t
  epsilon ()
  {
    return 0.00000001;
  }
  coord_t
  scale ()
  {
    return 0.3;
  }
  bool
  verbose ()
  {
    return false;
  }
  void
  constrain (coord_t *vals)
  {
    int n = num_coordinates ();
    /* Also consider the case that lens center is outside of the scan.  */
    if (vals[0] < -10)
      vals[0] = -10;
    if (vals[0] > 10)
      vals[0] = 10;
    if (n == 2)
      {
        if (vals[1] < -10)
          vals[1] = -1;
        if (vals[1] > 1)
          vals[1] = 1;
      }
    for (int i = n; i < n + 3; i++)
      {
        if (vals[i] < -0.1 * scale_kr)
          vals[i] = -0.1 * scale_kr;
        if (vals[i] > 0.1 * scale_kr)
          vals[i] = 0.1 * scale_kr;
      }
  }
  void
  solve (const coord_t *vals, coord_t *chisq, std::vector <point_t> *transformed)
  {
    static const coord_t bad_val = 100000000;
    m_param.center = { (coord_t)0, (coord_t)0 };
    m_param.coordinate1 = { (coord_t)1, (coord_t)0 };
    m_param.coordinate2 = { (coord_t)0, (coord_t)1 };
    int n = num_coordinates ();
    if (is_fixed_lens (m_param.scanner_type))
      m_param.lens_correction.center = { vals[0], vals[1] };
    else if (m_param.scanner_type == lens_move_horisontally)
      m_param.lens_correction.center = { 0, vals[0] };
    else if (m_param.scanner_type == lens_move_vertically)
      m_param.lens_correction.center = { vals[0], 0 };
    m_param.lens_correction.kr[0] = 1;
    m_param.lens_correction.kr[1] = vals[n] * (1 / scale_kr);
    m_param.lens_correction.kr[2] = vals[n + 1] * (1 / scale_kr);
    m_param.lens_correction.kr[3] = vals[n + 2] * (1 / scale_kr);
    if (debug && !m_param.lens_correction.is_monotone ())
      {
        printf ("Non monotone lens correction %f %f: %f %f %f %f\n",
                m_param.lens_correction.center.x,
                m_param.lens_correction.center.y,
                m_param.lens_correction.kr[0], m_param.lens_correction.kr[1],
                m_param.lens_correction.kr[2], m_param.lens_correction.kr[3]);
	if (chisq)
	  *chisq = bad_val;
        return;
      }
    m_param.lens_correction.normalize ();
    scr_to_img map;
    /* We may save some inversions if points are relatively few.  */
    if (m_sparam.n_points () * 2 < lens_warp_correction::size)
      map.set_parameters_for_early_correction (m_param, m_img_data.width, m_img_data.height);
    else
      map.set_parameters (m_param, m_img_data);
    coord_t chi = -5;
    /* Do not use ransac here, since it is not smooth and will confuse solver.  */
    homography::get_matrix (m_sparam.points, 
		    (screen_with_vertical_strips_p (m_param.type) ? homography::solve_vertical_strips : 0)
		    | homography::solve_rotation, m_param.scanner_type, &map, m_sparam.center, &chi, transformed);
    if (chisq)
    {
      if (!(chi >= 0 && chi < bad_val))
	{
	  printf ("Bad chi %f\n", chi);
	  *chisq = bad_val;
	}
      else
	*chisq = chi;
    }
  }
  coord_t
  objfunc (coord_t *vals)
  {
    coord_t chisq;
    solve (vals, &chisq, NULL);
    return chisq;
  }
  /* TODO: Joly has only half of them.  */
  int
  num_observations ()
  {
    return m_sparam.points.size () * 2;
  }
  void
  residuals(const coord_t *vals, coord_t *f_vec)
  {
    std::vector<point_t> transformed (m_sparam.points.size ());
    coord_t chisq;
    solve (vals, &chisq, &transformed);
    for (int i = 0; i < m_sparam.points.size (); i++)
    {
      f_vec[2 * i] = transformed[i].x - m_sparam.points[i].scr.x;
      f_vec[2 * i + 1] = transformed[i].y - m_sparam.points[i].scr.y;
      //printf ("%f %f %f %f\n",transformed[i].x, transformed[i].y, m_sparam.points[i].scr.x, m_sparam.points[i].scr.y);
    }
    //printf ("%g %g %g %g %g error sq %.10g\n",vals[0], vals[1], vals[2], vals[3], vals[4], chisq);
  }

};
}

coord_t
simple_solver (scr_to_img_parameters *param, image_data &img_data,
               solver_parameters &sparam, progress_info *progress)
{
  if (progress)
    progress->set_task ("determing geometry by linear regression", 1);
  return solver (param, img_data, sparam.points, sparam.center,
                 (sparam.weighted ? homography::solve_image_weights : 0),
                 true);
}


coord_t
solver (scr_to_img_parameters *param, image_data &img_data,
        solver_parameters &sparam, progress_info *progress)
{
  /* 3 points may be enough for strips; we only solve homography on 1d.  */
  if (sparam.n_points () < (screen_with_vertical_strips_p (param->type) ? 4 : 3))
    return 0;

  param->mesh_trans = NULL;

  /* Require more points for strips; we only can verify 1d info.  */
  bool optimize_lens = sparam.optimize_lens && (sparam.n_points () > (screen_with_vertical_strips_p (param->type) ? 200 : 100));
  bool optimize_rotation = sparam.optimize_tilt && (sparam.n_points () > (screen_with_vertical_strips_p (param->type) ? 20 : 10));

  if (optimize_lens)
    {
      lens_solver s (*param, img_data, sparam, progress);
      bool use_simplex = true;
      bool use_multifit = true;
      if (use_simplex)
	simplex<coord_t, lens_solver> (s, "optimizing lens correction",
				       progress);
      if (use_multifit)
	gsl_multifit<coord_t, lens_solver> (s, "optimizing lens correction pass 2",
				       progress);
      int n = s.num_coordinates ();
      if (is_fixed_lens (param->scanner_type))
        param->lens_correction.center = { s.start[0], s.start[1] };
      else if (param->scanner_type == lens_move_horisontally)
        param->lens_correction.center = { 0, s.start[0] };
      else if (param->scanner_type == lens_move_vertically)
        param->lens_correction.center = { s.start[0], 0 };
      param->lens_correction.kr[0] = 1;
      param->lens_correction.kr[1] = s.start[n] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[2]
          = s.start[n + 1] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[3]
          = s.start[n + 2] * (1 / lens_solver::scale_kr);
      param->lens_correction.normalize ();
    }
  if (progress)
    progress->set_task ("optimizing perspective correction", 1);
  return solver (param, img_data, sparam.points, sparam.center,
                 (sparam.weighted ? homography::solve_image_weights : 0)
                     | (optimize_rotation ? homography::solve_rotation : 0),
                 true);
}

static void
compute_mesh_point (solver_parameters &sparam, scanner_type type,
                    mesh *mesh_trans, int x, int y)
{
  int_point_t e = { x, y };
  point_t scrp = mesh_trans->get_screen_point (e);
  trans_4d_matrix h = homography::get_matrix (
      sparam.points,
      homography::
          solve_screen_weights /*homography::solve_limit_ransac_iterations
                                  | homography::solve_free_rotation*/
      ,
      type, NULL, scrp, NULL);
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
              sparam.points, homography::solve_image_weights,
              /*homography::solve_limit_ransac_iterations |
                 homography::solve_free_rotation*/
              type, NULL, imgp, NULL);
          h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}

std::unique_ptr <mesh>
solver_mesh (scr_to_img_parameters *param, image_data &img_data,
             solver_parameters &sparam, progress_info *progress)
{
  if (sparam.n_points () < 10)
    return NULL;
  int xshift, yshift, width, height;
  int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  map.get_range (img_data.width, img_data.height, &xshift, &yshift, &width,
                 &height);
  width = (width + step - 1) / step;
  height = (height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh", width * height);
  std::unique_ptr <mesh> mesh_trans = std::make_unique<mesh> (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(progress, xshift, yshift, step, width, height, sparam, img_data,   \
               mesh_trans, param)
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (!progress || !progress->cancel_requested ())
        {
          compute_mesh_point (sparam, param->scanner_type, mesh_trans.get (), x, y);
          if (progress)
            progress->inc_progress ();
        }
  int miter = width + height;
  if (progress)
    progress->set_task ("growing mesh", miter);
  while (miter > 0)
    {
      int grow_left
          = mesh_trans->need_to_grow_left (img_data.width, img_data.height)
                ? 1
                : 0;
      int grow_right
          = mesh_trans->need_to_grow_right (img_data.width, img_data.height)
                ? 1
                : 0;
      int grow_top
          = mesh_trans->need_to_grow_top (img_data.width, img_data.height) ? 1
                                                                           : 0;
      int grow_bottom
          = mesh_trans->need_to_grow_bottom (img_data.width, img_data.height)
                ? 1
                : 0;
      miter--;
      if (!grow_left && !grow_right && !grow_top && !grow_bottom)
        break;
      if (progress && progress->cancel_requested ())
        return NULL;
      if (!mesh_trans->grow (grow_left, grow_right, grow_top, grow_bottom))
        break;
      if (grow_left || grow_right)
        {
          for (int y = 0; y < mesh_trans->get_height (); y++)
            {
              if (grow_left)
                compute_mesh_point (sparam, param->scanner_type, mesh_trans.get (), 0,
                                    y);
              if (grow_right)
                compute_mesh_point (sparam, param->scanner_type, mesh_trans.get (),
                                    mesh_trans->get_width () - 1, y);
            }
        }
      if (grow_top || grow_bottom)
        {
          for (int x = 0; x < mesh_trans->get_width (); x++)
            {
              if (grow_top)
                compute_mesh_point (sparam, param->scanner_type, mesh_trans.get (), x,
                                    0);
              if (grow_bottom)
                compute_mesh_point (sparam, param->scanner_type, mesh_trans.get (), x,
                                    mesh_trans->get_height () - 1);
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
      return NULL;
    }
  if (progress && progress->cancel_requested ())
    {
      return NULL;
    }
  // mesh_trans->print (stdout);
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
      sparam.points, homography::solve_screen_weights, lparam.scanner_type,
      NULL, scrp, NULL);
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
              sparam.points, homography::solve_image_weights,
              lparam.scanner_type, NULL, imgp, NULL);
          h.perspective_transform (scrp.x, scrp.y, imgp.x, imgp.y);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}
std::unique_ptr <mesh>
solver_mesh (scr_to_img_parameters *param, image_data &img_data,
             solver_parameters &sparam2, screen_map &smap,
             progress_info *progress)
{
  int xshift, yshift, width, height;
  const int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  map.get_range (img_data.width, img_data.height, &xshift, &yshift, &width,
                 &height);
  width = (width + step - 1) / step;
  height = (height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh from detected points", width * height);
  std::unique_ptr<mesh> mesh_trans = std::make_unique<mesh> (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(progress, xshift, yshift, step, width, height, img_data,           \
               mesh_trans, param, smap, sparam2)
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (!progress || !progress->cancel_requested ())
        {
          // TODO: copying motor corrections is unnecesary and expensive.
          scr_to_img_parameters lparam = *param;
          solver_parameters sparam;
          sparam.copy_without_points (sparam2);
          compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans.get (), x,
                              y);
          if (progress)
            progress->inc_progress ();
        }
  scr_to_img_parameters lparam = *param;
  int miter = width + height;
  if (progress)
    progress->set_task ("growing mesh", miter);
  while (miter > 0)
    {
      int grow_left
          = mesh_trans->need_to_grow_left (img_data.width, img_data.height)
                ? 1
                : 0;
      int grow_right
          = mesh_trans->need_to_grow_right (img_data.width, img_data.height)
                ? 1
                : 0;
      int grow_top
          = mesh_trans->need_to_grow_top (img_data.width, img_data.height) ? 1
                                                                           : 0;
      int grow_bottom
          = mesh_trans->need_to_grow_bottom (img_data.width, img_data.height)
                ? 1
                : 0;
      miter--;
      if (!grow_left && !grow_right && !grow_top && !grow_bottom)
        break;
      if (progress && progress->cancel_requested ())
        return NULL;
      if (!mesh_trans->grow (grow_left, grow_right, grow_top, grow_bottom))
        break;
      solver_parameters sparam;
      sparam.copy_without_points (sparam2);
      if (grow_left || grow_right)
        {
          for (int y = 0; y < mesh_trans->get_height (); y++)
            {
              if (grow_left)
                compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans.get (),
                                    0, y);
              if (grow_right)
                compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans.get (),
                                    mesh_trans->get_width () - 1, y);
            }
        }
      if (grow_top || grow_bottom)
        {
          for (int x = 0; x < mesh_trans->get_width (); x++)
            {
              if (grow_top)
                compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans.get (),
                                    x, 0);
              if (grow_bottom)
                compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans.get (),
                                    x, mesh_trans->get_height () - 1);
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
    return NULL;
  // mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}

void
solver_parameters::dump (FILE *out)
{
  for (int i = 0; i < n_points (); i++)
    {
      fprintf (out, "point %i img %f %f maps to scr %f %f color %i\n", i,
               points[i].img.x, points[i].img.y, points[i].scr.x,
               points[i].scr.y, (int)points[i].color);
    }
}

solver_parameters::point_location *
solver_parameters::get_point_locations (enum scr_type type, int *n)
{
  static struct point_location paget_points[]
      = { /* Green.  */
          { 0, 0, solver_parameters::green },
          { 1, 0, solver_parameters::green },
          { 0, 1, solver_parameters::green },
          { 1, 1, solver_parameters::green },
          { 0.5, 0.5, solver_parameters::green },
          /* Red  */
          { 0, 0.5, solver_parameters::red },
          { 0.5, 0, solver_parameters::red },
          { 1, 0.5, solver_parameters::red },
          { 0.5, 1, solver_parameters::red }
        };
  static struct point_location dufay_points[] = {
    /* Green.  */
    { 0, 0, solver_parameters::green },  { 0.5, 0, solver_parameters::blue },
    { 1, 0, solver_parameters::green },  { 0, 1, solver_parameters::green },
    { 0.5, 1, solver_parameters::blue }, { 1, 1, solver_parameters::green },
  };
  static struct point_location DioptichromeB_points[] = {
    { 0, 0, solver_parameters::red },  { 0.5, 0, solver_parameters::blue },
    { 1, 0, solver_parameters::red },  { 0, 1, solver_parameters::red },
    { 0.5, 1, solver_parameters::blue }, { 1, 1, solver_parameters::red },
  };
  static struct point_location ImprovedDioptichromeB_points[] = {
    /* Green.  */
    { 0, 0, solver_parameters::green },  { 0.5, 0, solver_parameters::red },
    { 1, 0, solver_parameters::green },  { 0, 1, solver_parameters::green },
    { 0.5, 1, solver_parameters::red }, { 1, 1, solver_parameters::green },
  };
  static struct point_location WarnerPowrie_points[] = {
    /* Green.  */
    { 0, 0, solver_parameters::green }, 
    { 1.0/3, 0, solver_parameters::blue },
    { 2.0/3, 1, solver_parameters::red }, 
  };
  static struct point_location Joly_points[] = {
    /* Green.  */
    { 0, 0, solver_parameters::green }, 
    { 1.0/3, 0, solver_parameters::red },
    { 2.0/3, 1, solver_parameters::blue }, 
  };

  switch (type)
    {
    case Paget:
    case Thames:
    case Finlay:
      *n = sizeof (paget_points) / sizeof (point_location);
      return paget_points;
    case Dufay:
      *n = sizeof (dufay_points) / sizeof (point_location);
      return dufay_points;
    case DioptichromeB:
      *n = sizeof (DioptichromeB_points) / sizeof (point_location);
      return DioptichromeB_points;
    case ImprovedDioptichromeB:
    case Omnicolore:
      *n = sizeof (ImprovedDioptichromeB_points) / sizeof (point_location);
      return ImprovedDioptichromeB_points;
    case WarnerPowrie:
      *n = sizeof (WarnerPowrie_points) / sizeof (point_location);
      return WarnerPowrie_points;
    case Joly:
      *n = sizeof (Joly_points) / sizeof (point_location);
      return Joly_points;
    default:
      abort ();
    }
}
}
