#include <gsl/gsl_multifit.h>
#include "include/solver.h"

coord_t
solver (scr_to_img_parameters *param, image_data &img_data, int n, solver_point *points)
{
  printf ("Old Translation %f %f\n", param->center_x, param->center_y);
  printf ("Old coordinate1 %f %f\n", param->coordinate1_x, param->coordinate1_y);
  printf ("Old coordinate2 %f %f\n", param->coordinate2_x, param->coordinate2_y);
  /* Clear previous map.  */
  param->center_x = 0;
  param->center_y = 0;
  param->coordinate1_x = 1;
  param->coordinate1_y = 0;
  param->coordinate2_x = 0;
  param->coordinate2_y = 1;
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
      gsl_vector_set (w, i * 2, /*1.0/(ei*ei)*/ 10000.0);
      gsl_vector_set (y, i * 2 + 1, yt);
      gsl_vector_set (w, i * 2 + 1, /*1.0/(ei*ei)*/ 10000.0);
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
  printf ("Uncorrected translation %f %f\n", C(0), C(1));
  printf ("Uncorrected coordinate1 %f %f\n", C(2), C(4));
  printf ("Uncorrected coordinate2 %f %f\n", C(3), C(5));
  printf ("covariance matrix:\n");
  for (int j = 0; j < 6; j++)
  {
    for (int i = 0; i < 6; i++)
      printf (" %2.2f", COV(j,i));
    printf ("\n");
  }
  printf ("chisq: %f\n", chisq);

  /* Write resulting map.  */
  coord_t center_x = C(0);
  coord_t center_y = C(1);
  coord_t coordinate1_x = center_x + C(2);
  coord_t coordinate1_y = center_y + C(4);
  coord_t coordinate2_x = center_x + C(3);
  coord_t coordinate2_y = center_y + C(5);
  map.to_img (center_x, center_y, &center_x, &center_y);
  map.to_img (coordinate1_x, coordinate1_y, &coordinate1_x, &coordinate1_y);
  map.to_img (coordinate2_x, coordinate2_y, &coordinate2_x, &coordinate2_y);
  param->center_x = center_x;
  param->center_y = center_y;
  param->coordinate1_x = coordinate1_x - center_x;
  param->coordinate1_y = coordinate1_y - center_y;
  param->coordinate2_x = coordinate2_x - center_x;
  param->coordinate2_y = coordinate2_y - center_y;
  printf ("New Translation %f %f\n", param->center_x, param->center_y);
  printf ("New coordinate1 %f %f\n", param->coordinate1_x, param->coordinate1_y);
  printf ("New coordinate2 %f %f\n", param->coordinate2_x, param->coordinate2_y);
  {
    scr_to_img map2;
    map2.set_parameters (*param, img_data);
    for (int i = 0; i < n; i++)
      {
	coord_t xi = points[i].img_x, yi = points[i].img_y, xs = points[i].screen_x, ys = points[i].screen_y;
	coord_t xt, yt;
	map2.to_img (xs, ys, &xt, &yt);

	printf ("image: %g %g screen %g %g translated %g %g translated2 %g %g dist %g\n", xi, yi, xs, ys, xt, yt,
	         C(0) + xs * C(2) + ys * C(4),
		 C(1) + xs * C(3) + ys * C(5),
	    sqrt ((xt-xi)*(xt-xi)+(yt-yi)*(yt-yi)));
      }
  }
  
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  gsl_matrix_free (cov);
  return chisq;
}
