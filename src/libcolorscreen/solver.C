/* Geometry and lens warp solvers.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <memory>
#include <array>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_eigen.h>
#include <algorithm>
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

/* Determine homography matrix matching points specified by POINTS.
   Update PARAM for desired transformations.
   W_CENTER specifies point where to optimize for if solve_screen_weights
   or solve_image_weights are set in FLAGS.
   If FINAL_RUN is true output info on results.  */

nodiscard_attr coord_t
solver (scr_to_img_parameters *param, const image_data &img_data,
        const std::vector<solver_parameters::solver_point_t> &points,
        const point_t w_center, int flags, bool final_run = false)
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
                                       &map, w_center, &chisq, final_run);
  else
    h = homography::get_matrix (points, flags, param->scanner_type, &map,
                                w_center, &chisq);
  /* Determine center and coordinate vectors.  */
  point_t center = h.perspective_transform ({ (coord_t) 0, (coord_t) 0 });
  point_t coordinate1 = h.perspective_transform ({ (coord_t) 1, (coord_t) 0 });
  point_t coordinate2 = h.perspective_transform ({ (coord_t) 0, (coord_t) 1 });

  param->center = map.inverse_early_correction (center);
  param->coordinate1 = map.inverse_early_correction (coordinate1) - param->center;
  param->coordinate2 = map.inverse_early_correction (coordinate2) - param->center;
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
		      p = h.perspective_transform ({ (coord_t)sx, (coord_t)sy });
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
	    p = h.perspective_transform (scr);
	    p = map.inverse_early_correction (p);
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
	    p = h.inverse_perspective_transform (p);
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
  return (coord_t)chisq;
}

/* Pick N nearest points from POINTS to P.  If SCREEN is true use screen coordinates,
   otherwise use image coordinates.  Store results in OUT.  */

void
pick_nearest_points (std::vector<solver_parameters::solver_point_t> &out,
		     const std::vector<solver_parameters::solver_point_t> &points,
		     point_t p, int n, bool screen)
{
  if ((int)points.size () <= n)
    {
      out = points;
      return;
    }
  struct entry
  {
    int index;
    coord_t dist_sq;
    bool operator< (const entry &other) const
    {
      return dist_sq < other.dist_sq;
    }
  };
  std::vector<entry> heap;
  heap.reserve (n);

  for (int i = 0; i < (int)points.size (); i++)
    {
      coord_t d = screen ? points[i].scr.dist_sq2_from (p)
			 : points[i].img.dist_sq2_from (p);
      if ((int)heap.size () < n)
	{
	  heap.push_back ({ i, d });
	  if ((int)heap.size () == n)
	    std::make_heap (heap.begin (), heap.end ());
	}
      else if (d < heap.front ().dist_sq)
	{
	  std::pop_heap (heap.begin (), heap.end ());
	  heap.back () = { i, d };
	  std::push_heap (heap.begin (), heap.end ());
	}
    }

  out.clear ();
  out.reserve (n);
  for (const auto &e : heap)
    out.push_back (points[e.index]);
}

/* Nonlinear optimizer for determining radial lens warp parameters.  */
class lens_solver
{
public:
  lens_solver (scr_to_img_parameters &param, const image_data &img_data,
               const solver_parameters &sparam, progress_info *progress)
      : m_param (param), m_img_data (img_data), m_sparam (sparam),
        m_progress (progress), m_start{ (coord_t)0.5, (coord_t)0.5, (coord_t)0, (coord_t)0, (coord_t)0 }
  {
    if (num_coordinates () == 1)
      m_start[1] = 0;
  }
  scr_to_img_parameters &m_param;
  const image_data &m_img_data;
  const solver_parameters &m_sparam;
  progress_info *m_progress;
  static constexpr coord_t scale_kr = 128;

  /* Return number of lens center coordinates.  */
  int
  num_coordinates () const
  {
    return is_fixed_lens (m_param.scanner_type) ? 2 : 1;
  }

  /* Return total number of parameters to optimize.  */
  int
  num_values () const
  {
    return num_coordinates () + 3;
  }
  std::array<coord_t, 5> m_start;
  coord_t *start = m_start.data ();

  /* Return epsilon for solver convergence.  */
  coord_t
  epsilon () const
  {
    return (coord_t)0.00000001;
  }

  /* Return perturbation for finite differences derivatives.  */
  coord_t
  derivative_perturbation () const
  {
    return (coord_t)0.0001;
  }

  /* Return initial scale for simplex solver.  */
  coord_t
  scale () const
  {
    return (coord_t)0.3;
  }

  /* Return true if solver should be verbose.  */
  bool
  verbose () const
  {
    return false;
  }

  /* Constrain parameters to valid range.
     VALS are parameters to be constrained.  */
  void
  constrain (coord_t *vals) const
  {
    int n = num_coordinates ();
    /* Also consider the case that lens center is outside of the scan.  */
    if (vals[0] < (coord_t)-10)
      vals[0] = (coord_t)-10;
    if (vals[0] > (coord_t)10)
      vals[0] = (coord_t)10;
    if (n == 2)
      {
        if (vals[1] < (coord_t)-10)
          vals[1] = (coord_t)-10;
        if (vals[1] > (coord_t)10)
          vals[1] = (coord_t)10;
      }
    /*
       Coefficient	Typical Range	  Extreme Range 
       k1 (3rd Order)   ±0.001-±0.05      ±0.15
       k2 (5th Order)   ±0.0001-±0.01     ±0.05
       k3 (7th Order)   ≈0 (often unused) ±0.01  */
    constexpr coord_t range [3] = {0.15, 0.05, 0.01};
    for (int i = n, j = 0; i < n + 3; i++, j++)
      {
        if (vals[i] < (coord_t)-range[j] * scale_kr)
          vals[i] = (coord_t)-range[j] * scale_kr;
        if (vals[i] > (coord_t)range[j] * scale_kr)
          vals[i] = (coord_t)range[j] * scale_kr;
      }
  }

  /* Solve for lens parameters.
     VALS are current lens parameters.
     CHISQ is updated with resulting chi square.
     TRANSFORMED is updated with transformed points.  */
  bool
  solve (const coord_t *vals, coord_t *chisq, std::vector <point_t> *transformed) const
  {
    static constexpr coord_t bad_val = 100000000;
    m_param.center = { (coord_t)0, (coord_t)0 };
    m_param.coordinate1 = { (coord_t)1, (coord_t)0 };
    m_param.coordinate2 = { (coord_t)0, (coord_t)1 };
    int n = num_coordinates ();
    if (is_fixed_lens (m_param.scanner_type))
      m_param.lens_correction.center = { vals[0], vals[1] };
    else if (m_param.scanner_type == lens_move_horizontally)
      m_param.lens_correction.center = { 0, vals[0] };
    else if (m_param.scanner_type == lens_move_vertically)
      m_param.lens_correction.center = { vals[0], 0 };
    else
      abort ();
    m_param.lens_correction.kr[0] = 1;
    m_param.lens_correction.kr[1] = vals[n] * (1 / scale_kr);
    m_param.lens_correction.kr[2] = vals[n + 1] * (1 / scale_kr);
    m_param.lens_correction.kr[3] = vals[n + 2] * (1 / scale_kr);
    if (!m_param.lens_correction.is_monotone ())
      {
	if (colorscreen_checking)
          printf ("Non monotone lens correction %f %f: %f %f %f %f\n",
                  m_param.lens_correction.center.x,
                  m_param.lens_correction.center.y,
                  m_param.lens_correction.kr[0], m_param.lens_correction.kr[1],
                  m_param.lens_correction.kr[2], m_param.lens_correction.kr[3]);
	if (chisq)
	  *chisq = bad_val;
        if (transformed)
          for (size_t i = 0; i < m_sparam.points.size (); i++)
            (*transformed)[i] = m_sparam.points[i].scr;
        return false;
      }
    if (!m_param.lens_correction.normalize ())
      {
        if (chisq)
          *chisq = bad_val;
        if (transformed)
          for (size_t i = 0; i < m_sparam.points.size (); i++)
            (*transformed)[i] = m_sparam.points[i].scr;
        return false;
      }
    scr_to_img map;
    /* We may save some inversions if points are relatively few.  */
    bool ok;
    if (m_sparam.n_points () * 2 < lens_warp_correction::size)
      ok = map.set_parameters_for_early_correction (m_param, m_img_data.width, m_img_data.height);
    else
      ok = map.set_parameters (m_param, m_img_data);
    if (!ok)
      return false;
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
    return (chi >= 0 && chi < bad_val);
  }
  /* Return number of observations.  */
  int
  num_observations ()
  {
    return m_sparam.points.size () * (screen_with_vertical_strips_p (m_param.type) ? 1 : 2);
  }

  /* Compute residuals.  VALS is current set of parameters, F_VEC is the
     output vector of residuals.  */
  int
  residuals(const coord_t *vals, coord_t *f_vec)
  {
    std::vector<point_t> transformed (m_sparam.points.size ());
    coord_t chisq;
    if (!solve (vals, &chisq, &transformed))
      {
	for (int i = 0; i < (int)num_observations (); i++)
	  f_vec[i] = 1000;
	return GSL_EDOM;
      }
    if (!screen_with_vertical_strips_p (m_param.type))
      for (size_t i = 0; i < m_sparam.points.size (); i++)
	{
	  f_vec[2 * i] = transformed[i].x - m_sparam.points[i].scr.x;
	  f_vec[2 * i + 1] = transformed[i].y - m_sparam.points[i].scr.y;
	  //printf ("%f %f %f %f\n",transformed[i].x, transformed[i].y, m_sparam.points[i].scr.x, m_sparam.points[i].scr.y);
	}
    else
      for (size_t i = 0; i < m_sparam.points.size (); i++)
      {
	f_vec[i] = transformed[i].x - m_sparam.points[i].scr.x;
      }
    //printf ("%g %g %g %g %g error sq %.10g\n",vals[0], vals[1], vals[2], vals[3], vals[4], chisq);
    return GSL_SUCCESS;
  }

  /* Objective function for simplex solver.  */
  coord_t
  objfunc (coord_t *vals)
  {
    coord_t chisq;
    solve (vals, &chisq, nullptr);
    return chisq;
  }
};
}

/* Determine geometry using linear regression.  PARAM is updated with results.
   IMG_DATA is the source image.  SPARAM contains solver points.
   PROGRESS is used for progress reporting.  */

coord_t
simple_solver (scr_to_img_parameters *param, const image_data &img_data,
               const solver_parameters &sparam, progress_info *progress)
{
  if (progress)
    progress->set_task ("determining geometry by linear regression", 1);
  return solver (param, img_data, sparam.points.read (), sparam.center,

                 (sparam.weighted ? homography::solve_image_weights : 0),
                 true);
}


/* Determine geometry and lens warp.  PARAM is updated with results.
   IMG_DATA is the source image.  SPARAM contains solver points.
   PROGRESS is used for progress reporting.  */

coord_t
solver (scr_to_img_parameters *param,const  image_data &img_data,
        const solver_parameters &sparam, progress_info *progress)
{
  /* 3 points may be enough for strips; we only solve homography on 1d.  */
  if (sparam.n_points () < solver_parameters::min_points (param->type))
    return 0;

  param->mesh_trans = nullptr;

  /* Require more points for strips; we only can verify 1d info.  */
  bool optimize_lens = sparam.optimize_lens && (sparam.n_points () > solver_parameters::min_lens_points (param->type));
  bool optimize_rotation = sparam.optimize_tilt && (sparam.n_points () > solver_parameters::min_perspective_points (param->type));

  if (optimize_lens)
    {
      lens_solver s (*param, img_data, sparam, progress);
      bool use_early_multifit = false;
      bool use_simplex = true;
      bool use_gsl_simplex = false;
      bool use_multifit = false;
      if (use_early_multifit)
	gsl_multifit<coord_t, lens_solver> (s, "optimizing lens correction (multifit)",
				       progress);
      if (use_simplex)
	simplex<coord_t, lens_solver> (s, "optimizing lens correction (simplex)",
				       progress);
      if (use_gsl_simplex)
	gsl_simplex<coord_t, lens_solver> (s, "optimizing lens correction (GSL simplex)",
				       progress);
      if (use_multifit)
	gsl_multifit<coord_t, lens_solver> (s, "optimizing lens correction (multifit)",
				       progress);
      int n = s.num_coordinates ();
      if (is_fixed_lens (param->scanner_type))
        param->lens_correction.center = { s.start[0], s.start[1] };
      else if (param->scanner_type == lens_move_horizontally)
        param->lens_correction.center = { 0, s.start[0] };
      else if (param->scanner_type == lens_move_vertically)
        param->lens_correction.center = { s.start[0], 0 };
      param->lens_correction.kr[0] = 1;
      param->lens_correction.kr[1] = s.start[n] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[2]
          = s.start[n + 1] * (1 / lens_solver::scale_kr);
      param->lens_correction.kr[3]
          = s.start[n + 2] * (1 / lens_solver::scale_kr);
      if (!param->lens_correction.normalize ())
        return false;
    }
  if (progress)
    progress->set_task ("optimizing perspective correction", 1);
  return solver (param, img_data, sparam.points.read (), sparam.center,

                 (sparam.weighted ? homography::solve_image_weights : 0)
                     | (optimize_rotation ? homography::solve_rotation : 0),
                 true);
}

#if 0

static void
compute_mesh_point (solver_parameters &sparam, scanner_type type,
                    mesh *mesh_trans, int x, int y)
{
  int_point_t e = { x, y };
  point_t scrp = mesh_trans->get_screen_point (e);
  const std::vector<solver_parameters::solver_point_t> *points = &sparam.points;
  std::vector<solver_parameters::solver_point_t> local_points;

  if (sparam.points.size () > 100)
    {
      pick_nearest_points (local_points, sparam.points, scrp, 100, true);
      points = &local_points;
    }

  trans_4d_matrix h = homography::get_matrix (
      *points,
      homography::
          solve_screen_weights /*homography::solve_limit_ransac_iterations
                                   | homography::solve_free_rotation*/
      ,
      type, nullptr, scrp, nullptr);
  point_t imgp;
  imgp = h.perspective_transform (scrp);

  /* We need to set weight assymetrically based on image distance.  Problem is
     that without knowing the image distance we can not set one, so iteratively
     find right one.  */
  if (type != fixed_lens)
    {
      int i;
      for (i = 0; i < 100; i++)
        {
          point_t last_imgp = imgp;
	  if (sparam.points.size () > 100)
	    pick_nearest_points (local_points, sparam.points, imgp, 100, false);
          trans_4d_matrix h = homography::get_matrix (
              *points, homography::solve_image_weights,
              /*homography::solve_limit_ransac_iterations |
                 homography::solve_free_rotation*/
              type, nullptr, imgp, nullptr);
          imgp = h.perspective_transform (scrp);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}

/* Determine mesh transformation for PARAM.  IMG_DATA is the source image.
   SPARAM contains solver points.  PROGRESS is used for progress reporting.  */

std::unique_ptr <mesh>
solver_mesh (scr_to_img_parameters *param, image_data &img_data,
             solver_parameters &sparam, progress_info *progress)
{
  if (sparam.n_points () < solver_parameters::min_mesh_points (param->type))
    return nullptr;
  int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  int_image_area r1 = map.get_range (img_data.width, img_data.height);
  int width = (r1.width + step - 1) / step, height = (r1.height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh", width * height);
  std::unique_ptr <mesh> mesh_trans = std::make_unique<mesh> (r1, step, step);
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(progress, r1, step, width, height, sparam, img_data,               \
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
        return nullptr;
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
    return nullptr;
  // mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}
#endif

static void
compute_img_to_scr_mesh_point (const solver_parameters &sparam, scanner_type type,
			       mesh *mesh_trans, int_point_t e)
{
  point_t imgp = mesh_trans->get_screen_point (e);
  const std::vector<solver_parameters::solver_point_t> *points = &sparam.points.read ();

  std::vector<solver_parameters::solver_point_t> local_points;

  if (sparam.points.size () > 100)
    {
      pick_nearest_points (local_points, sparam.points, imgp, 100, false);
      points = &local_points;
    }

  trans_4d_matrix h = homography::get_matrix (
      *points,
      homography::
          solve_image_weights /*homography::solve_limit_ransac_iterations
                                   | homography::solve_free_rotation*/
      ,
      type, nullptr, imgp, nullptr);
  point_t scrp = h.inverse_perspective_transform (imgp);
  mesh_trans->set_point (e, scrp);
}

/* Determine mesh transformation for PARAM.  IMG_DATA is the source image.
   SPARAM contains solver points.  PROGRESS is used for progress reporting.  */

std::unique_ptr <mesh>
solver_mesh (const scr_to_img_parameters *param, const image_data &img_data,
             const solver_parameters &sparam, progress_info *progress)
{
  if (sparam.n_points () < solver_parameters::min_mesh_points (param->type))
    return nullptr;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  if (!map.set_parameters (*param, img_data))
    return NULL;
  int_image_area r1 = {0, 0, img_data.width, img_data.height};
  const int steps = 200;
  int step = std::max (r1.width / steps, r1.height / steps);
  if (!step)
    return NULL;
  int width = (r1.width + step - 1) / step, height = (r1.height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh", width * height);

  /* Expand the range so inversion is not using out of range points.  */
  int_image_area r2 = {-step, -step, img_data.width + 2*step, img_data.height + 2*step};
  std::unique_ptr <mesh> mesh_trans = std::make_unique<mesh> (r2, step, step);
  width = mesh_trans->get_width ();
  height = mesh_trans->get_height ();
#pragma omp parallel for default(none) schedule(dynamic) collapse(2)          \
    shared(progress, r1, step, width, height, sparam, img_data,               \
               mesh_trans, param)
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (!progress || !progress->cancel_requested ())
        {
          compute_img_to_scr_mesh_point (sparam, param->scanner_type, mesh_trans.get (), {x, y});
          if (progress)
            progress->inc_progress ();
        }
  if (progress && progress->cancel_requested ())
    return nullptr;
  // mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}

/* Determine single mesh point coordinates for SMAP.
   SPARAM is updated with nearby solver points.
   LPARAM determines the scanner geometry.  */

static void
compute_mesh_point (const screen_map &smap, solver_parameters &sparam,
                    const scr_to_img_parameters &lparam, const image_data &img_data,
                    mesh *mesh_trans, int x, int y)
{
  int_point_t e = { x, y };
  point_t scrp = mesh_trans->get_screen_point (e);
  smap.get_solver_points_nearby (scrp.x, scrp.y, 100, sparam);
  trans_4d_matrix h = homography::get_matrix (
      sparam.points.read (), homography::solve_screen_weights, lparam.scanner_type,

      nullptr, scrp, nullptr);
  point_t imgp;
  imgp = h.perspective_transform (scrp);
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
              sparam.points.read (), homography::solve_image_weights,

              lparam.scanner_type, nullptr, imgp, nullptr);
          imgp = h.perspective_transform (scrp);
          if (last_imgp.almost_eq (imgp, 0.5))
            break;
        }
      if (i == 100)
        printf ("Osclation instability\n");
    }
  mesh_trans->set_point (e, imgp);
}

/* Determine mesh transformation for PARAM based on detected points in SMAP.
   IMG_DATA is the source image.  SPARAM2 contains solver parameters.
   PROGRESS is used for progress reporting.  */

std::unique_ptr <mesh>
solver_mesh (const scr_to_img_parameters *param, const image_data &img_data,
             const solver_parameters &sparam2, const screen_map &smap,
             progress_info *progress)
{
  const int step = 10;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  int_image_area r2 = map.get_range (img_data.width, img_data.height);
  int width = (r2.width + step - 1) / step, height = (r2.height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh from detected points", width * height);
  std::unique_ptr<mesh> mesh_trans = std::make_unique<mesh> (r2, step, step);
#pragma omp parallel default(none) \
    shared(progress, r2, step, width, height, img_data, mesh_trans, param, smap, sparam2)
  {
    solver_parameters sparam;
    sparam.copy_without_points (sparam2);
    #pragma omp for schedule(dynamic) collapse(2)
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++)
        if (!progress || !progress->cancel_requested ())
          {
            compute_mesh_point (smap, sparam, *param, img_data, mesh_trans.get (), x,
                                y);
            if (progress) 
              progress->inc_progress ();
          }
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
        return nullptr;
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
    return nullptr;
  // mesh_trans->print (stdout);
  if (progress)
    progress->set_task ("inverting mesh", 1);
  mesh_trans->precompute_inverse ();
  return mesh_trans;
}

/* Dump solver points to file OUT.  */

void
solver_parameters::dump (FILE *out) const
{
  const auto &pointsVec = points.read ();
  for (size_t i = 0; i < pointsVec.size (); i++)
    {
      fprintf (out, "point %zu img %f %f maps to scr %f %f color %i\n", i,
               pointsVec[i].img.x, pointsVec[i].img.y, pointsVec[i].scr.x,
               pointsVec[i].scr.y, (int)pointsVec[i].color);
    }
}

/* Return description of individual color patches in single period of the screen
   for screen type TYPE.  N is initialized to number of patches.  */

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

/* Fit points to a line and return distance threshold for 90% of points.  */
double
solver_parameters::fit_line (point_t &origin, point_t &dir) const
{
  if (points.empty ())
    return 0;
  const auto &pointsVec = points.read ();
  if (pointsVec.size () == 1)
    {
      origin = pointsVec[0].img;
      dir = { 1, 0 };
      return 0;
    }

  double mean_x = 0, mean_y = 0;
  for (const auto &p : pointsVec)
    {
      mean_x += p.img.x;
      mean_y += p.img.y;
    }
  mean_x /= pointsVec.size ();
  mean_y /= pointsVec.size ();
  origin = { (coord_t)mean_x, (coord_t)mean_y };

  double mxx = 0, myy = 0, mxy = 0;
  for (const auto &p : pointsVec)
    {
      double dx = p.img.x - mean_x;
      double dy = p.img.y - mean_y;
      mxx += dx * dx;
      myy += dy * dy;
      mxy += dx * dy;
    }

  gsl_matrix *m = gsl_matrix_alloc (2, 2);
  gsl_matrix_set (m, 0, 0, mxx);
  gsl_matrix_set (m, 0, 1, mxy);
  gsl_matrix_set (m, 1, 0, mxy);
  gsl_matrix_set (m, 1, 1, myy);

  gsl_vector *eval = gsl_vector_alloc (2);
  gsl_matrix *evec = gsl_matrix_alloc (2, 2);
  gsl_eigen_symmv_workspace *w = gsl_eigen_symmv_alloc (2);
  gsl_eigen_symmv (m, eval, evec, w);
  gsl_eigen_symmv_sort (eval, evec, GSL_EIGEN_SORT_VAL_DESC);

  dir.x = gsl_matrix_get (evec, 0, 0);
  dir.y = gsl_matrix_get (evec, 1, 0);

  point_t normal = { (coord_t)gsl_matrix_get (evec, 0, 1),
                     (coord_t)gsl_matrix_get (evec, 1, 1) };

  gsl_eigen_symmv_free (w);
  gsl_matrix_free (m);
  gsl_vector_free (eval);
  gsl_matrix_free (evec);

  std::vector<double> dists;
  dists.reserve (pointsVec.size ());
  for (const auto &p : pointsVec)
    {
      double dx = p.img.x - mean_x;
      double dy = p.img.y - mean_y;
      dists.push_back (fabs (dx * normal.x + dy * normal.y));
    }
  std::sort (dists.begin (), dists.end ());
  return dists[(size_t)(dists.size () * 0.9)];
}
}
