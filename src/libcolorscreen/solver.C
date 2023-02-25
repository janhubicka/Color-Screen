#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/solver.h"
#include "include/colorscreen.h"
#include "screen-map.h"
#include "sharpen.h"

const char *solver_parameters::point_color_names[(int)max_point_color] = {"red", "green", "blue"};

namespace
{
bool debug_output = false;
bool debug = true;


#if 1
int
print_matrix(FILE *f, const char *name, const gsl_matrix *m)
{
        int status, n = 0;
	printf ("Matrix %s\n", name);

        for (size_t i = 0; i < m->size1; i++) {
                for (size_t j = 0; j < m->size2; j++) {
                        if ((status = fprintf(f, "%4.2f ", gsl_matrix_get(m, i, j))) < 0)
                                return -1;
                        n += status;
                }

                if ((status = fprintf(f, "\n")) < 0)
                        return -1;
                n += status;
        }

        return n;
}
int
print_system(FILE *f, const gsl_matrix *m, gsl_vector *v)
{
        int status, n = 0;

        for (size_t i = 0; i < m->size1; i++) {
                for (size_t j = 0; j < m->size2; j++) {
                        if ((status = fprintf(f, "%4.2f ", gsl_matrix_get(m, i, j))) < 0)
                                return -1;
                        n += status;
                }

                if ((status = fprintf(f, "| %4.2f\n", gsl_vector_get (v, i))) < 0)
                        return -1;
                n += status;
        }

        return n;
}
#endif

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

/* Produce two rows of homography equation converting point S to D at index N.  */

void
init_equation (gsl_matrix *A, gsl_vector *v, int n, bool invert, int flags, enum scanner_type scanner_type, point_t s, point_t d)
{
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

/* Turn solution to a matrix.  */
trans_4d_matrix
solution_to_matrix (gsl_vector *v, int flags, enum scanner_type type)
{
  trans_4d_matrix ret;
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
  gsl_vector_free (v);
  if (debug_output)
    ret.print (stdout);
  return ret;
}

/* Wrapper for homography::get_matrix that translates result back to PARAM.  */

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, int n, solver_parameters::point_t *points,
	/*bool weights, bool scrweights,*/
	coord_t wcenter_x, coord_t wcenter_y,
	int flags, bool final = false)
{
  if (debug_output && final)
    {
      printf ("Old Translation %f %f\n", param->center_x, param->center_y);
      printf ("Old coordinate1 %f %f\n", param->coordinate1_x, param->coordinate1_y);
      printf ("Old coordinate2 %f %f\n", param->coordinate2_x, param->coordinate2_y);
    }
  /* Clear previous map.  */
  param->center_x = 0;
  param->center_y = 0;
  param->coordinate1_x = 1;
  param->coordinate1_y = 0;
  param->coordinate2_x = 0;
  param->coordinate2_y = 1;
  if (flags & homography::solve_rotation)
    {
      param->tilt_x = 0;
      param->tilt_y = 0;
    }
  /* This map applies only non-linear part of corrections (that are not optimized).  */
  scr_to_img map;
  map.set_parameters (*param, img_data);

  double chisq;
  trans_4d_matrix h = homography::get_matrix (points, n, flags,
					      param->scanner_type, &map,
					      wcenter_x, wcenter_y, &chisq);
  coord_t center_x, center_y, coordinate1_x, coordinate1_y, coordinate2_x, coordinate2_y;

  /* Determine center and coordinate vectors.  */
  h.perspective_transform (0, 0, center_x, center_y);
  h.perspective_transform (1, 0, coordinate1_x, coordinate1_y);
  h.perspective_transform (0, 1, coordinate2_x, coordinate2_y);

  map.to_img (center_x, center_y, &center_x, &center_y);
  map.to_img (coordinate1_x, coordinate1_y, &coordinate1_x, &coordinate1_y);
  map.to_img (coordinate2_x, coordinate2_y, &coordinate2_x, &coordinate2_y);

  param->center_x = center_x;
  param->center_y = center_y;
  param->coordinate1_x = coordinate1_x - center_x;
  param->coordinate1_y = coordinate1_y - center_y;
  param->coordinate2_x = coordinate2_x - center_x;
  param->coordinate2_y = coordinate2_y - center_y;
  /* TODO: Can we decompose matrix in the way scr_to_img expects the parameters?  */
  if (flags & homography::solve_rotation)
    {
      coord_t tilt_x_min=-3, tilt_x_max=3;
      int tilt_x_steps = 21;
      coord_t tilt_y_min=-3, tilt_y_max=3;
      int tilt_y_steps = 21;
      coord_t minsq = INT_MAX;
      coord_t best_tilt_x = 1, best_tilt_y = 1;
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
		  scr_to_img map2;
		  map2.set_parameters (*param, img_data);
		  for (int sy = -1; sy <= 1; sy++)
		    for (int sx = -1; sx <= 1; sx++)
		      {
			coord_t xt, yt;
			map2.to_img (sx, sy, &xt, &yt);
			coord_t px, py;
			h.perspective_transform (sx, sy, px, py);
			map.to_img (px, py, &px, &py);
			sq += (px - xt) * (px - xt) + (py - yt) * (py - yt);
		      }

#if 0
		    for (int i = 0; i < n; i++)
		      {
			coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
			coord_t xt, yt;
			map2.to_img (xs, ys, &xt, &yt);
			coord_t px, py;
			h.perspective_transform (xs, ys, px, py);
			map.to_img (px, py, &px, &py);
			sq += (px - xt) * (px - xt) + (py - yt) * (py - yt);
		      }
#endif
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
      printf ("New Translation %f %f\n", param->center_x, param->center_y);
      printf ("New coordinate1 %f %f\n", param->coordinate1_x, param->coordinate1_y);
      printf ("New coordinate2 %f %f\n", param->coordinate2_x, param->coordinate2_y);
    }
  if (final && (debug || debug_output))
    {
      scr_to_img map2;
      map2.set_parameters (*param, img_data);
      //map2.m_matrix.print (stdout);
      bool found = false;
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	  coord_t xt, yt;
	  map2.to_img (xs, ys, &xt, &yt);
	  coord_t px, py;
	  h.perspective_transform (xs, ys, px, py);
	  map.to_img (px, py, &px, &py);

#if 0
	  map.to_img (px, py, &px, &py);
	  if (debug_output)
	    printf ("image: %g %g screen %g %g translated %g %g translated by solver %g %g dist %g\n", xi, yi, xs, ys, xt, yt,
		px, py, sqrt ((xt-xi)*(xt-xi)+(yt-yi)*(yt-yi)));
#endif
	  if (fabs (px - xt) > 1 || fabs (py - yt) > 1)
	    {
	      printf ("Solver model mismatch %f %f should be %f %f (ideally %f %f)\n", xt, yt, px, py, xi, yi);
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
    progress->set_task ("optimizing", 1);
  return solver (param, img_data, sparam.npoints, sparam.point, sparam.center_x, sparam.center_y, (sparam.weighted ? homography::solve_image_weights : 0), true);
}

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
{
  if (sparam.npoints < 3)
    return 0;

  if (param->mesh_trans)
    abort ();


  coord_t chimin = solver (param, img_data, sparam.npoints, sparam.point, sparam.center_x, sparam.center_y, (sparam.weighted ? homography::solve_image_weights : 0) | (sparam.npoints > 10 ? homography::solve_rotation : 0), true);
#if  0
  coord_t best_tiltx = param->tilt_x, best_tilty = param->tilt_y;
  coord_t tilt_x_min=-3, tilt_x_max=3;
  int tilt_x_steps = 21;
  coord_t tilt_y_min=-3, tilt_y_max=3;
  int tilt_y_steps = 21;
  int nbest = 0;
  int iterations = 10;
  printf ("Chimin1 %f\n", chimin);
  chimin = solver (param, img_data, sparam.npoints, sparam.point, sparam.center_x, sparam.center_y, (sparam.weighted ? homography::solve_image_weights : 0), true);
  if (sparam.npoints > 10)
    {
      if (progress)
	progress->set_task ("optimizing", tilt_x_steps * tilt_y_steps * iterations);
      for (int i = 0; i < 10; i++)
	{
	  coord_t txstep = (tilt_x_max - tilt_x_min) / (tilt_x_steps - 1);
	  coord_t tystep = (tilt_y_max - tilt_y_min) / (tilt_y_steps - 1);
	  if (!progress || !progress->cancel_requested ())
	    for (int tx = 0; tx < tilt_x_steps; tx++)
	      if (!progress || !progress->cancel_requested ())
		for (int ty = 0; ty < tilt_y_steps; ty++)
		  {
		    param->tilt_x = tilt_x_min + txstep * tx;
		    param->tilt_y = tilt_y_min + tystep * ty;
		    coord_t chi = solver (param, img_data, sparam.npoints, sparam.point, sparam.center_x, sparam.center_y, (sparam.weighted ? homography::solve_image_weights : 0));
		    if (chi < chimin)
		      {
			chimin = chi;
			best_tiltx = param->tilt_x;
			best_tilty = param->tilt_y;
			nbest++;
		      }
		    if (progress)
		      progress->inc_progress ();
		  }
	  param->tilt_x = best_tiltx;
	  param->tilt_y = best_tilty;
	  tilt_x_min = best_tiltx - txstep;
	  tilt_x_max = best_tiltx + txstep;
	  tilt_y_min = best_tilty - tystep;
	  tilt_y_max = best_tilty + tystep;
	}
      //printf ("Found %i\n", nbest);
      return solver (param, img_data, sparam.npoints, sparam.point, sparam.center_x, sparam.center_y,  (sparam.weighted ? homography::solve_image_weights : 0), true);
    }
  else
#endif
    return chimin;
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
    progress->set_task ("computing mesh", height);
  mesh *mesh_trans = new mesh (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) shared(progress, xshift, yshift, step, width, height, sparam, img_data, mesh_trans, param)
  for (int y = 0; y < height; y++)
    {
      // TODO: copying motor corrections is unnecesary and expensive.
      scr_to_img_parameters lparam = *param;
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < width; x++)
	  {
	    coord_t xx, yy;
	    solver (&lparam, img_data, sparam.npoints, sparam.point, x * step - xshift, y * step - yshift, homography::solve_screen_weights);
	    scr_to_img map2;
	    map2.set_parameters (lparam, img_data);
	    map2.to_img (x * step - xshift, y * step - yshift, &xx, &yy);
	    mesh_trans->set_point (x,y, xx, yy);
	  }
      if (progress)
	progress->inc_progress ();
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
compute_mesh_point (screen_map &smap, solver_parameters &sparam, scr_to_img_parameters &lparam, image_data &img_data, mesh *mesh_trans, int x, int y)
{
  coord_t sx = x * mesh_trans->get_xstep () - mesh_trans->get_xshift ();
  coord_t sy = y * mesh_trans->get_ystep () - mesh_trans->get_yshift ();
  coord_t xx, yy;
  smap.get_solver_points_nearby (sx, sy, 100, sparam);
  if (0
      && (sparam.point[0].screen_x == sx
	  || sparam.point[0].screen_y == sy))
    {
      xx = sparam.point[0].img_x;
      yy = sparam.point[0].img_y;
    }
  else
    {
      solver (&lparam, img_data, sparam.npoints, sparam.point, sx, sy, 0);
      scr_to_img map2;
      map2.set_parameters (lparam, img_data);
      map2.to_img (sx, sy, &xx, &yy);
    }
  mesh_trans->set_point (x,y, xx, yy);
}
mesh *
solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam2, screen_map &smap, progress_info *progress)
{
  int xshift, yshift, width, height;
  const int step = 3;
  if (param->mesh_trans)
    abort ();
  scr_to_img map;
  map.set_parameters (*param, img_data);
  map.get_range (img_data.width, img_data.height, &xshift, &yshift, &width, &height);
  width = (width + step - 1) / step;
  height = (height + step - 1) / step;
  if (progress)
    progress->set_task ("computing mesh", height);
  mesh *mesh_trans = new mesh (xshift, yshift, step, step, width, height);
#pragma omp parallel for default(none) shared(progress, xshift, yshift, step, width, height, img_data, mesh_trans, param, smap)
  for (int y = 0; y < height; y++)
    {
      // TODO: copying motor corrections is unnecesary and expensive.
      scr_to_img_parameters lparam = *param;
      solver_parameters sparam;/* = sparam2;*/
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < width; x++)
	  compute_mesh_point (smap, sparam, lparam, img_data, mesh_trans, x, y);
      if (progress)
	progress->inc_progress ();
    }
  if (progress)
    progress->set_task ("growing mesh", height);
  scr_to_img_parameters lparam = *param;
  int miter = width + height;
#if 1
  while (miter > 0)
    {
      int grow_left = mesh_trans->need_to_grow_left (img_data.width, img_data.height) ? 1 : 0;
      int grow_right = mesh_trans->need_to_grow_right (img_data.width, img_data.height) ? 1 : 0;
      int grow_top = mesh_trans->need_to_grow_top (img_data.width, img_data.height) ? 1 : 0;
      int grow_bottom = mesh_trans->need_to_grow_bottom (img_data.width, img_data.height) ? 1 : 0;
      miter --;
      if (!grow_left && !grow_right && !grow_top && !grow_bottom)
	break;
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
    }
  if (!miter)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Maximum number of iterations reached.\n");
      if (progress)
	progress->resume_stdout ();
    }
#endif
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

/* Given known portion of screen collect color samples and optimize to PARAM.
   M, XSHIFT, YSHIFT, KNOWN_PATCHES are results of screen analysis. */
void
optimize_screen_colors (scr_detect_parameters *param, image_data *img, mesh *m, int xshift, int yshift, bitmap_2d *known_patches, luminosity_t gamma, progress_info *progress, FILE *report)
{
  int count = 0;
  const int range = 2;
  for (int y = 0; y < known_patches->height; y++)
    for (int x = 0; x < known_patches->width; x++)
      if (known_patches->test_range (x, y, range))
	count++;
  const int samples = 1000;
  int nnr = 0, nng = 0, nnb = 0;
  color_t reds[samples*2];
  color_t greens[samples];
  color_t blues[samples];
  luminosity_t *lookup_table = render::get_lookup_table (gamma, img->maxval);

  for (int y = -yshift, nf = 0, next =0, step = count / samples; y < known_patches->height - yshift; y++)
    for (int x = -xshift; x < known_patches->width - xshift; x++)
      if (known_patches->test_range (x + xshift,y + yshift, range) && nf++ > next)
	{
	  coord_t ix, iy;
	  next += step;
	  m->apply (x, y, &ix, &iy);
	  if (nng < samples && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))

	    {
	      greens[nng].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      greens[nng].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      greens[nng].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
	      nng++;
	    }
	  m->apply ((x)+0.5, y, &ix, &iy);
	  if (nnb < samples && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      blues[nnb].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      blues[nnb].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      blues[nnb].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
	      nnb++;
	    }
	  m->apply ((x), y + 0.5, &ix, &iy);
	  if (nnr < samples * 2 && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      reds[nnr].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
	      nnr++;
	    }
	  m->apply ((x) + 0.5, y + 0.5, &ix, &iy);
	  if (nnr < samples * 2 && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      reds[nnr].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
	      nnr++;
	    }
	}
  render::release_lookup_table (lookup_table);
  if (nnr < 10 || nnb < 10 || nng < 10)
    {
      fprintf (stderr, "Failed to find enough samples\n");
      abort ();
    }
  optimize_screen_colors (param, gamma, reds, nnr, greens, nng, blues, nnb, progress, report);
}

/* Helper for sharpening part of the scan.  */
struct imgtile
{
  luminosity_t *lookup_table;
  int xstart, ystart;
  image_data *img;
};

static
rgbdata get_pixel (struct imgtile *sec, int x, int y, int, int)
{
  rgbdata ret = {0,0,0};
  x += sec->xstart;
  y += sec->ystart;
  if (x < 0 || y < 0 || x >= sec->img->width || y >= sec->img->height)
    return ret;
  ret.red = sec->lookup_table [sec->img->rgbdata[y][x].r];
  ret.green = sec->lookup_table [sec->img->rgbdata[y][x].g];
  ret.blue = sec->lookup_table [sec->img->rgbdata[y][x].b];
  return ret;
}
struct entry {
	rgbdata sharpened_color;
	rgbdata orig_color;
	luminosity_t priority;
};
bool
compare_priorities(struct entry &e1, struct entry &e2)
{
  return e2.priority < e1.priority;
}

bool
optimize_screen_colors (scr_detect_parameters *param, image_data *img, luminosity_t gamma, int x, int y, int width, int height, progress_info *progress, FILE *report)
{
  const double sharpen_amount = 0;
  const double sharpen_radius = 3;
  int clen = fir_blur::convolve_matrix_length (sharpen_radius);
  rgbdata *sharpened = (rgbdata*) malloc ((width + clen) * (height + clen) * sizeof (rgbdata));
  if (!sharpened)
    return false;
  luminosity_t *lookup_table = render::get_lookup_table (gamma, img->maxval);
  struct imgtile section = {lookup_table, x - clen / 2, y - clen / 2, img};
  sharpen<rgbdata, imgtile *, int, &get_pixel> (sharpened, &section, 0, width + clen, height + clen, sharpen_radius, sharpen_amount, progress);
  std::vector<entry> pixels;
  for (int yy = y ; yy < y + height; yy++)
    for (int xx = x ; xx < x + width; xx++)
      {
	struct entry e;
	e.orig_color = get_pixel (&section, xx-x+clen/2, yy-y+clen/2, 0, 0);
	e.sharpened_color = sharpened[(yy-y+clen/2) * (width + clen) + xx -x + clen/2];
	e.priority = 3 - (e.orig_color.red + e.orig_color.green + e.orig_color.blue);
	pixels.push_back (e);
      }
  render::release_lookup_table (lookup_table);
  free (sharpened);

  sort (pixels.begin (), pixels.end (), compare_priorities);
  int pos = pixels.size () / 2;
  luminosity_t min_density = pixels[pos].orig_color.red + pixels[pos].orig_color.green + pixels[pos].orig_color.blue;
  //printf ("\n min density %f\n", min_density);

  for (entry &e : pixels)
    e.priority = e.sharpened_color.red / std::max (e.sharpened_color.green + e.sharpened_color.blue, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<color_t> reds;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      reds.push_back ((color_t){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      //printf ("%f %f %f %f\n", e.orig_color.red, e.orig_color.green, e.orig_color.blue, e.priority);
      if (reds.size () > pixels.size () / 1000)
	break;
    }
  

  for (entry &e : pixels)
    e.priority = e.sharpened_color.green / std::max (e.sharpened_color.red + e.sharpened_color.blue, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<color_t> greens;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      greens.push_back ((color_t){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      if (greens.size () > pixels.size () / 1000)
	break;
    }
  

  for (entry &e : pixels)
    e.priority = e.sharpened_color.blue / std::max (e.sharpened_color.red + e.sharpened_color.green, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<color_t> blues;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      blues.push_back ((color_t){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      if (blues.size () > pixels.size () / 1000)
	break;
    }
  //printf ("%i %i %i\n",reds.size (), greens.size (), blues.size ());
  if (!reds.size () || !greens.size () || !blues.size ()
      || (reds.size () + greens.size () + blues.size ()) < 4 * 3)
    return false;
  optimize_screen_colors (param, gamma, reds.data (), reds.size (), greens.data (), greens.size (), blues.data (), blues.size (), progress, report);
  return true;
}

/* Optimize screen colors based on known red, green and blue samples
   and store resulting black, red, green and blue colors to PARAM.  */
void
optimize_screen_colors (scr_detect_parameters *param,
			luminosity_t gamma,
			color_t *reds,
			int nreds,
			color_t *greens,
			int ngreens,
			color_t *blues,
			int nblues, progress_info *progress, FILE *report)
{
  if (!nreds || !ngreens || !nblues)
    abort ();
  int n = nreds + ngreens + nblues;
  int matrixw = 4 * 3;
  int matrixh = n * 3;
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (matrixh, matrixw);
  gsl_matrix *X, *cov;
  gsl_vector *y, *w, *c;
  X = gsl_matrix_alloc (matrixh, matrixw);
  y = gsl_vector_alloc (matrixh);
  w = gsl_vector_alloc (matrixh);
  c = gsl_vector_alloc (matrixw);
  cov = gsl_matrix_alloc (matrixw, matrixw);

  double min_chisq = 0;
  bool found = false;
  color_t bestdark, bestred, bestgreen, bestblue;
  luminosity_t bestlum = 0;
  const int steps = 100;
  if (progress)
    progress->set_task ("optimizing colors", steps);
  std::vector<luminosity_t> lums;
  for (int i = 0; i < nreds; i++)
    lums.push_back (reds[i].red + reds[i].green + reds[i].blue);
  for (int i = 0; i < ngreens; i++)
    lums.push_back (greens[i].red + greens[i].green + greens[i].blue);
  for (int i = 0; i < nblues; i++)
    lums.push_back (blues[i].red + blues[i].green + blues[i].blue);
  sort(lums.begin (), lums.end ());
  luminosity_t maxlum = lums[(int)(lums.size () * 0.5)];
  for (int step = 0; step < steps ; step++)
    {
      luminosity_t mm = step * maxlum / steps;
      if (progress)
        progress->inc_progress ();
      for (int i = 0; i < nreds; i++)
	{
	  int e = i * 3;
	  coord_t ii = reds[i].red + reds[i].green + reds[i].blue - mm;
	  for (int j = 0; j < 3; j++)
	    {
	      gsl_matrix_set (X, e+j, 0, j==0);
	      gsl_matrix_set (X, e+j, 1, j==1);
	      gsl_matrix_set (X, e+j, 2, j==2);
	      gsl_matrix_set (X, e+j, 3, j==0 ? ii : 0);
	      gsl_matrix_set (X, e+j, 4, j==1 ? ii : 0);
	      gsl_matrix_set (X, e+j, 5, j==2 ? ii : 0);
	      gsl_matrix_set (X, e+j, 6, 0);
	      gsl_matrix_set (X, e+j, 7, 0);
	      gsl_matrix_set (X, e+j, 8, 0);
	      gsl_matrix_set (X, e+j, 9, 0);
	      gsl_matrix_set (X, e+j, 10, 0);
	      gsl_matrix_set (X, e+j, 11, 0);
	      gsl_vector_set (w, e+j, 1);
	    }
	  gsl_vector_set (y, e, reds[i].red);
	  gsl_vector_set (y, e+1, reds[i].green);
	  gsl_vector_set (y, e+2, reds[i].blue);
	}
      for (int i = 0; i < ngreens; i++)
	{
	  int e = (i + nreds) * 3;
	  coord_t ii = greens[i].red + greens[i].green + greens[i].blue - mm;
	  for (int j = 0; j < 3; j++)
	    {
	      gsl_matrix_set (X, e+j, 0, j==0);
	      gsl_matrix_set (X, e+j, 1, j==1);
	      gsl_matrix_set (X, e+j, 2, j==2);
	      gsl_matrix_set (X, e+j, 3, 0);
	      gsl_matrix_set (X, e+j, 4, 0);
	      gsl_matrix_set (X, e+j, 5, 0);
	      gsl_matrix_set (X, e+j, 6, j==0 ? ii : 0);
	      gsl_matrix_set (X, e+j, 7, j==1 ? ii : 0);
	      gsl_matrix_set (X, e+j, 8, j==2 ? ii : 0);
	      gsl_matrix_set (X, e+j, 9, 0);
	      gsl_matrix_set (X, e+j, 10, 0);
	      gsl_matrix_set (X, e+j, 11, 0);
	      gsl_vector_set (w, e+j, 1);
	    }
	  gsl_vector_set (y, e, greens[i].red);
	  gsl_vector_set (y, e+1, greens[i].green);
	  gsl_vector_set (y, e+2, greens[i].blue);
	  //printf ("%f %f %f\n",greens[i].red, greens[i].green, greens[i].blue);
	}
      for (int i = 0; i < nblues; i++)
	{
	  int e = (i + nreds + ngreens) * 3;
	  coord_t ii = blues[i].red + blues[i].green + blues[i].blue - mm;
	  for (int j = 0; j < 3; j++)
	    {
	      gsl_matrix_set (X, e+j, 0, j==0);
	      gsl_matrix_set (X, e+j, 1, j==1);
	      gsl_matrix_set (X, e+j, 2, j==2);
	      gsl_matrix_set (X, e+j, 3, 0);
	      gsl_matrix_set (X, e+j, 4, 0);
	      gsl_matrix_set (X, e+j, 5, 0);
	      gsl_matrix_set (X, e+j, 6, 0);
	      gsl_matrix_set (X, e+j, 7, 0);
	      gsl_matrix_set (X, e+j, 8, 0);
	      gsl_matrix_set (X, e+j, 9, j==0 ? ii : 0);
	      gsl_matrix_set (X, e+j, 10, j==1 ? ii : 0);
	      gsl_matrix_set (X, e+j, 11, j==2 ? ii : 0);
	      gsl_vector_set (w, e+j, 1);
	    }
	  gsl_vector_set (y, e, blues[i].red);
	  gsl_vector_set (y, e+1, blues[i].green);
	  gsl_vector_set (y, e+2, blues[i].blue);
	}
      double chisq;
      gsl_multifit_wlinear (X, w, y, c, cov,
			    &chisq, work);
#define C(i) (gsl_vector_get(c,(i)))
      if (!found || chisq < min_chisq)
	{
	  min_chisq = chisq;
	  found = true;
	  bestdark.red = C(0);
	  bestdark.green = C(1);
	  bestdark.blue = C(2);
	  bestred.red = C(3);
	  bestred.green = C(4);
	  bestred.blue = C(5);
	  bestgreen.red = C(6);
	  bestgreen.green = C(7);
	  bestgreen.blue = C(8);
	  bestblue.red = C(9);
	  bestblue.green = C(10);
	  bestblue.blue = C(11);
	  bestlum = mm;
	}
    }
#if 0
  param->black = bestdark.sgngamma (1/gamma);
  param->red = (bestred + bestdark).sgngamma (1/gamma);
  param->green = (bestgreen + bestdark).sgngamma (1/gamma);
  param->blue = (bestblue + bestdark).sgngamma (1/gamma);
#else
  param->black = bestdark;
  param->red = bestred + bestdark;
  param->green = bestgreen + bestdark;
  param->blue = bestblue + bestdark;
#endif
#if 1
  if (report)
    {
      fprintf (report, "Color optimization:\n  Dark %f %f %f (gamma %f lum %f chisq %f)\n", bestdark.red, bestdark.green, bestdark.blue, gamma, bestlum, min_chisq);
      fprintf (report, "  Red %f %f %f\n", bestred.red, bestred.green, bestred.blue);
      fprintf (report, "  Green %f %f %f\n", bestgreen.red, bestgreen.green, bestgreen.blue);
      fprintf (report, "  Blue %f %f %f\n", bestblue.red, bestblue.green, bestblue.blue);
      save_csp (report, NULL, param, NULL, NULL);
    }
#endif
  gsl_multifit_linear_free (work);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  gsl_matrix_free (cov);
}

/* Return homography matrix determined from POINTS using least squares
   method.  Trainsform image coordinates by MAP if non-NULL.
   If FLAGS is set to solve_screen_weights or solve_image_weights
   then adjust weight according to distance from WCENTER_X and WCENTER_Y.
   If CHISQ_RET is non-NULL initialize it to square of errors.  */
trans_4d_matrix
homography::get_matrix (solver_parameters::point_t *points, int n, int flags,
			enum scanner_type scanner_type,
			scr_to_img *map,
			coord_t wcenter_x, coord_t wcenter_y,
			coord_t *chisq_ret)
{
  int nvariables = equation_variables (flags);
  gsl_matrix *X, *cov;
  gsl_vector *y, *w, *c;
  X = gsl_matrix_alloc (n * 2, nvariables);
  y = gsl_vector_alloc (n * 2);
  w = gsl_vector_alloc (n * 2);
  c = gsl_vector_alloc (nvariables);
  cov = gsl_matrix_alloc (nvariables, nvariables);
  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      coord_t xt, yt;
      /* Apply non-linear transformations.  */
      if (map)
        map->to_scr (xi, yi, &xt, &yt);
      else
	xt = xi, xt = yi;

      init_equation (X, y, i, false, flags, scanner_type, {xs, ys}, {xt, yt});

      /* Weight should be 1 / (error^2).  */
      if (!(flags & (solve_screen_weights | solve_image_weights)))
	{
	  gsl_vector_set (w, i * 2, 1.0);
	  gsl_vector_set (w, i * 2 + 1, 1.0);
	}
      if (flags & solve_image_weights)
	{
	  coord_t dist = sqrt ((points[i].img_x - wcenter_x) * (points[i].img_x - wcenter_x) + (points[i].img_y - wcenter_y) * (points[i].img_y - wcenter_y));
	  double weight = 1 / (dist + 0.5);
	  gsl_vector_set (w, i * 2, weight);
	  gsl_vector_set (w, i * 2 + 1, weight);
	}
      else 
	{
	  //coord_t dist = /*sqrt*/ pow ((points[i].screen_x - wcenter_x) * (points[i].screen_x - wcenter_x) + (points[i].screen_y - wcenter_y) * (points[i].screen_y - wcenter_y), 4);
	  coord_t dist = /*sqrt*/ ((points[i].screen_x - wcenter_x) * (points[i].screen_x - wcenter_x) + (points[i].screen_y - wcenter_y) * (points[i].screen_y - wcenter_y));
	  double weight = 1 / (dist + 0.5);
	  gsl_vector_set (w, i * 2, weight);
	  gsl_vector_set (w, i * 2 + 1, weight);
	}
    }
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (n*2, nvariables);
  double chisq;
  gsl_multifit_wlinear (X, w, y, c, cov,
			&chisq, work);
  gsl_multifit_linear_free (work);
  /* TODO: robust solvers does not seem to work in practice.  Replace them by RANSAC?  */
#if 0
  gsl_multifit_robust_workspace * work
    = gsl_multifit_robust_alloc (gsl_multifit_robust_default, n*2, 6);
  work->maxiter = 10000;
  gsl_multifit_robust (X, y, c, cov,
		      work);
  chisq = 0;
  gsl_multifit_robust_free (work);
#endif
  if (chisq_ret)
    *chisq_ret = chisq;
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_matrix_free (cov);
  return solution_to_matrix (c, flags, scanner_type);
}

/* Return homography matrix M.
   Applying M.perspective_transofrm on (0,0) will yield to ZERO.
   Applying M.perspective_transofrm on (1,0) will yield to X.
   Applying M.perspective_transofrm on (0,1) will yield to Y.
   Applying M.perspective_transofrm on (1,1) will yield to XPY (X+Y).
   If INVERT is true then inverse of this transofrmation is computed.

   This assumes fixed lens projection.  */
trans_4d_matrix
homography::get_matrix_4points (bool invert, enum scanner_type type, point_t zero, point_t x, point_t y, point_t xpy)
{
  gsl_matrix *A = gsl_matrix_alloc (8, 8);
  gsl_vector *v = gsl_vector_alloc (8);
  init_equation (A, v, 0, invert, solve_rotation, type, {0, 0}, zero);
  init_equation (A, v, 1, invert, solve_rotation, type, {1, 0}, x);
  init_equation (A, v, 2, invert, solve_rotation, type, {0, 1}, y);
  init_equation (A, v, 3, invert, solve_rotation, type, {1, 1}, xpy);

  if (debug_output)
    print_system (stdout, A,v);
  gsl_linalg_HH_svx (A, v);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_rotation, type);
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
homography::get_matrix_5points (bool invert, point_t zero, point_t x, point_t y, point_t xpy, point_t txpy)
{
  gsl_matrix *A = gsl_matrix_alloc (10, 10);
  gsl_vector *v = gsl_vector_alloc (10);
  init_equation (A, v, 0, invert, solve_free_rotation, fixed_lens, {0, 0}, zero);
  init_equation (A, v, 1, invert, solve_free_rotation, fixed_lens, {1, 0}, x);
  init_equation (A, v, 2, invert, solve_free_rotation, fixed_lens, {0, 1}, y);
  init_equation (A, v, 3, invert, solve_free_rotation, fixed_lens, {1, 1}, xpy);
  init_equation (A, v, 4, invert, solve_free_rotation, fixed_lens, {2, 3}, txpy);

  if (debug_output)
    print_system (stdout, A,v);
  gsl_linalg_HH_svx (A, v);
  gsl_matrix_free (A);
  return solution_to_matrix (v, solve_free_rotation, fixed_lens);
}
