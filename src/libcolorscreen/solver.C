#include <gsl/gsl_multifit.h>
#include "include/solver.h"

const char *solver_parameters::point_color_names[(int)max_point_color] = {"red", "green", "blue"};

namespace
{
bool debug_output = false;
bool debug = true;

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, int n, solver_parameters::point_t *points,
	bool weights, bool scrweights, coord_t wcenter_x, coord_t wcenter_y,
      	bool final = false)
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
  /* This map applies only non-linear part of corrections (that are not optimized).  */
  scr_to_img map;
  map.set_parameters (*param, img_data);
  double chisq;

  gsl_matrix *X, *cov;
  gsl_vector *y, *w, *c;
  X = gsl_matrix_alloc (n * 2, 6);
  y = gsl_vector_alloc (n * 2);
  w = gsl_vector_alloc (n * 2);

  c = gsl_vector_alloc (6);
  cov = gsl_matrix_alloc (6, 6);

  for (int i = 0; i < n; i++)
    {
      coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
      coord_t xt, yt;
      /* Apply non-linear transformations.  */
      map.to_scr (xi, yi, &xt, &yt);

      if (debug_output && final)
	printf ("image: %g %g adjusted %g %g screen %g %g\n", xi, yi, xt, yt, xs, ys);

      gsl_matrix_set (X, i * 2, 0, 1.0);
      gsl_matrix_set (X, i * 2, 1, 0.0);
      gsl_matrix_set (X, i * 2, 2, xs);
      gsl_matrix_set (X, i * 2, 3, 0);
      gsl_matrix_set (X, i * 2, 4, ys);
      gsl_matrix_set (X, i * 2, 5, 0);

      gsl_matrix_set (X, i * 2+1, 0, 0.0);
      gsl_matrix_set (X, i * 2+1, 1, 1.0);
      gsl_matrix_set (X, i * 2+1, 2, 0);
      gsl_matrix_set (X, i * 2+1, 3, xs);
      gsl_matrix_set (X, i * 2+1, 4, 0);
      gsl_matrix_set (X, i * 2+1, 5, ys);

      gsl_vector_set (y, i * 2, xt);
      gsl_vector_set (y, i * 2 + 1, yt);

      /* Weight should be 1 / (error^2).  */
      if (!weights && !scrweights)
	{
	  gsl_vector_set (w, i * 2, 1.0);
	  gsl_vector_set (w, i * 2 + 1, 1.0);
	}
      else if (weights)
	{
	  coord_t dist = sqrt ((points[i].img_x - wcenter_x) * (points[i].img_x - wcenter_x) + (points[i].img_y - wcenter_y) * (points[i].img_y - wcenter_y));
	  double weight = 1 / (dist + 0.5);
	  gsl_vector_set (w, i * 2, weight);
	  gsl_vector_set (w, i * 2 + 1, weight);
	}
      else if (scrweights)
	{
	  coord_t dist = sqrt ((points[i].screen_x - wcenter_x) * (points[i].screen_x - wcenter_x) + (points[i].screen_y - wcenter_y) * (points[i].screen_y - wcenter_y));
	  double weight = 1 / (dist + 0.5);
	  gsl_vector_set (w, i * 2, weight);
	  gsl_vector_set (w, i * 2 + 1, weight);
	}
    }

  {
    gsl_multifit_linear_workspace * work
      = gsl_multifit_linear_alloc (n*2, 6);
    gsl_multifit_wlinear (X, w, y, c, cov,
                          &chisq, work);
    gsl_multifit_linear_free (work);
  }

#define C(i) (gsl_vector_get(c,(i)))
#define COV(i,j) (gsl_matrix_get(cov,(i),(j)))
  if (debug_output && final)
    {
      printf ("Uncorrected translation %f %f\n", C(0), C(1));
      printf ("Uncorrected coordinate1 %f %f\n", C(2), C(3));
      printf ("Uncorrected coordinate2 %f %f\n", C(4), C(5));
      printf ("covariance matrix:\n");
      for (int j = 0; j < 6; j++)
      {
	for (int i = 0; i < 6; i++)
	  printf (" %2.2f", COV(j,i));
	printf ("\n");
      }
    }
  //printf ("chisq: %f\n", chisq);

  /* Write resulting map.  */
  coord_t center_x = C(0);
  coord_t center_y = C(1);
#if 0
  coord_t coordinate1_x = center_x + C(2);
  coord_t coordinate1_y = center_y + C(4);
  coord_t coordinate2_x = center_x + C(3);
  coord_t coordinate2_y = center_y + C(5);
#else
  coord_t coordinate1_x = center_x + C(2);
  coord_t coordinate1_y = center_y + C(3);
  coord_t coordinate2_x = center_x + C(4);
  coord_t coordinate2_y = center_y + C(5);
#endif
  map.to_img (center_x, center_y, &center_x, &center_y);
  map.to_img (coordinate1_x, coordinate1_y, &coordinate1_x, &coordinate1_y);
  map.to_img (coordinate2_x, coordinate2_y, &coordinate2_x, &coordinate2_y);
  param->center_x = center_x;
  param->center_y = center_y;
  param->coordinate1_x = coordinate1_x - center_x;
  param->coordinate1_y = coordinate1_y - center_y;
  param->coordinate2_x = coordinate2_x - center_x;
  param->coordinate2_y = coordinate2_y - center_y;
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
      for (int i = 0; i < n; i++)
	{
	  coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	  coord_t xt, yt;
	  map2.to_img (xs, ys, &xt, &yt);
	  coord_t px = C(0) + xs * C(2) + ys * C(4);
	  coord_t py = C(1) + xs * C(3) + ys * C(5);
	  map.to_img (px, py, &px, &py);

#if 0
	  if (debug_output)
	    printf ("image: %g %g screen %g %g translated %g %g translated by solver %g %g dist %g\n", xi, yi, xs, ys, xt, yt,
		px, py, sqrt ((xt-xi)*(xt-xi)+(yt-yi)*(yt-yi)));
#endif
	  if (fabs (px - xt) > 1 || fabs (py - yt) > 1)
	    printf ("Solver model mismatch %f %f should be %f %f (ideally %f %f)\n", xt, yt, px, py, xi, yi);
	}
    }
  
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  gsl_matrix_free (cov);
  return chisq;
}

} 

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam, progress_info *progress)
{
  if (sparam.npoints < 3)
    return 0;
  coord_t tilt_x_min=-1, tilt_x_max=1;
  int tilt_x_steps = 21;
  coord_t tilt_y_min=-1, tilt_y_max=1;
  int tilt_y_steps = 21;
  int nbest = 0;


  coord_t best_tiltx = param->tilt_x, best_tilty = param->tilt_y;
  coord_t chimin = solver (param, img_data, sparam.npoints, sparam.point, sparam.weighted, false, sparam.center_x, sparam.center_y, sparam.npoints <= 10);
  if (sparam.npoints > 10)
    {
      for (int i = 0; i < 10; i++)
	{
	  coord_t txstep = (tilt_x_max - tilt_x_min) / (tilt_x_steps - 1);
	  coord_t tystep = (tilt_y_max - tilt_y_min) / (tilt_y_steps - 1);
	  for (int tx = 0; tx < tilt_x_steps; tx++)
	    for (int ty = 0; ty < tilt_y_steps; ty++)
	      {
		param->tilt_x = tilt_x_min + txstep * tx;
		param->tilt_y = tilt_y_min + tystep * ty;
		coord_t chi = solver (param, img_data, sparam.npoints, sparam.point, sparam.weighted, false, sparam.center_x, sparam.center_y);
		if (chi < chimin)
		  {
		    chimin = chi;
		    best_tiltx = param->tilt_x;
		    best_tilty = param->tilt_y;
		    nbest++;
		  }
	      }
	  param->tilt_x = best_tiltx;
	  param->tilt_y = best_tilty;
	  tilt_x_min = best_tiltx - txstep;
	  tilt_x_max = best_tiltx + txstep;
	  tilt_y_min = best_tilty - tystep;
	  tilt_y_max = best_tilty + tystep;
	}
      //printf ("Found %i\n", nbest);
      return solver (param, img_data, sparam.npoints, sparam.point, sparam.weighted, false, sparam.center_x, sparam.center_y, true);
    }
  else
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
	    solver (&lparam, img_data, sparam.npoints, sparam.point, false, true, x * step - xshift, y * step - yshift);
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
