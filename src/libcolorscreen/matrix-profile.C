#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/solver.h"
#include "render-interpolate.h"
#include "gsl-utils.h"
#include "nmsimplex.h"

#define C(i) (gsl_vector_get(c,(i)))

namespace
{

/* Nonlinear optimizer for minizing deltaE.  */
struct
color_solver
{
  /* Input colors  */
  rgbdata *colors;

  /* Target colors (either XYZ or RGB)  */
  xyz *targets;
  rgbdata *rgbtargets;

  /* Number of targets.  */
  int n;

  /* Whitepoint used for DeltaE2000 computation.  */
  xyz white;

  /* If 0 the dark point is assumed to be 0.  If 1 it is assumed to be same in all channes,
     If 3 RGB value of dark point is determined.  */
  int dark_point_elts;

  /* Render instance used to convert RGB values to XYZ for DeltaE.  */
  render *r;

  /* Proportions of patches in the color screen.  */
  rgbdata proportions;

  /* 3x3 matrix plus dark point.  */
  int num_values ()
  {
    return 9 + dark_point_elts;
  }
  coord_t epsilon ()
  {
    return 0.000001;
  }
  coord_t scale ()
  {
    return 2;
  }
  bool verbose ()
  {
    return false;
  }
  void
  constrain (coord_t *vals)
  {
  }

  void
  init_by_matrix (color_matrix &m)
  {
    start[0] = m.m_elements[0][0]; start[1] = m.m_elements[1][0]; start[2] = m.m_elements[2][0];
    start[3] = m.m_elements[0][1]; start[4] = m.m_elements[1][1]; start[5] = m.m_elements[2][1];
    start[6] = m.m_elements[0][2]; start[7] = m.m_elements[1][2]; start[8] = m.m_elements[2][2];
    if (dark_point_elts == 1)
      {
        start[9] = m.m_elements[3][0];
	start[10] = 0;
	start[11] = 0;
      }
    else if (dark_point_elts == 3)
      {
	start[9]  = m.m_elements[3][0];
	start[10]  = m.m_elements[3][1];
	start[11] = m.m_elements[3][2];
      }
    else
      {
        start[9] = 0;
	start[10] = 0;
	start[11] = 0;
      }
  }

  color_matrix
  matrix_by_vals (coord_t *vals)
  {
    return color_matrix (vals[0], vals[1], vals[2], dark_point_elts == 1? vals[9]: dark_point_elts == 3 ? vals[9]: 0,
			 vals[3], vals[4], vals[5], dark_point_elts == 1? vals[9]: dark_point_elts == 3 ? vals[10]: 0,
			 vals[6], vals[7], vals[8], dark_point_elts == 1? vals[9]: dark_point_elts == 3 ? vals[11]: 0,
			 0, 0, 0, 1);
  }

  luminosity_t
  get_deltaE (color_matrix ret, int i, xyz *ret_color1 = NULL, xyz *ret_color2 = NULL)
  {
    xyz color1, color2;
    if (targets)
      {
	color1 = targets[i];
	ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &color2.x, &color2.y, &color2.z);
      }
    else
      {
	rgbdata c = rgbtargets[i];
	c.red = r->adjust_luminosity_ir (c.red / proportions.red);
	c.green = r->adjust_luminosity_ir (c.green / proportions.green);
	c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);

	r->set_linear_hdr_color (c.red, c.green, c.blue, &color1.x, &color1.y, &color1.z);
	ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &c.red, &c.green, &c.blue);
	c.red = r->adjust_luminosity_ir (c.red / proportions.red);
	c.green = r->adjust_luminosity_ir (c.green / proportions.green);
	c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);
	r->set_linear_hdr_color (c.red, c.green, c.blue, &color2.x, &color2.y, &color2.z);
      }
    if (ret_color1)
      *ret_color1 = color1;
    if (ret_color2)
      *ret_color2 = color2;
    return deltaE2000 (color1, color2, white);
  }

  luminosity_t
  objfunc (coord_t *vals)
  {
    color_matrix ret = matrix_by_vals (vals);
    luminosity_t desum = 0;
    for (int i = 0; i < n; i++)
      desum += get_deltaE (ret, i);
    return desum / n;
  }
  luminosity_t start[12];
};

}

/* Determine matrix profile based on colors and targets.
   Targets can be xyz or RGB
   if dark_point_elts == 0 then dark point will be (0, 0, 0)
   if dark_point_elts == 1 then dark point will be (x, x, x) for some constant x
   if dark_point_elts == 3 then dark point is arbirtrary
   TODO: We should not minimize least squares, we want smallest deltaE2000  */

color_matrix
determine_color_matrix (rgbdata *colors, xyz *targets, rgbdata *rgbtargets, int n, xyz white, int dark_point_elts, std::vector <color_match> *report, render *r, rgbdata proportions)
{
  rgbdata avg1 = {0,0,0};
  xyz avg2 = {0,0,0};
  rgbdata avg3 = {0,0,0};
  for (int i = 0; i < n; i++)
    {
      avg1 += colors[i];
      if (targets)
        avg2 += targets[i];
      else
        avg3 += rgbtargets[i];
    }
  /* Normalize values to reduce rounoff errors.  */
  luminosity_t scale1 = 3*n/(avg1.red + avg1.green + avg1.blue);
  luminosity_t scale2 = targets ? 3*n/(avg2.x + avg2.y + avg2.z) : 3*n/(avg3.red + avg3.green + avg3.blue);

  int nvariables = 9;
  const bool verbose = false;
  if (dark_point_elts == 1)
    nvariables++;
  else if (dark_point_elts == 3)
    nvariables += 3;
  else
    assert (dark_point_elts == 0);
  int nequations = n * 3;
  gsl_matrix *X;
  gsl_vector *y, *w, *c;
  X = gsl_matrix_alloc (nequations, nvariables);
  y = gsl_vector_alloc (nequations);
  w = gsl_vector_alloc (nequations);
  c = gsl_vector_alloc (nvariables);
  for (int i = 0; i < n; i++)
    {
      for (int j = 0; j < 3; j++)
	{
	  for (int k = 0; k < 3; k++)
	    {
	      gsl_matrix_set (X, i * 3 + j, 3 * k + 0, j == k ? colors[i].red * scale1 : 0);
	      gsl_matrix_set (X, i * 3 + j, 3 * k + 1, j == k ? colors[i].green * scale1 : 0);
	      gsl_matrix_set (X, i * 3 + j, 3 * k + 2, j == k ? colors[i].blue * scale1 : 0);
	    }
	  if (dark_point_elts == 1)
	    gsl_matrix_set (X, i * 3 + j, 9, 1);
	  else if (dark_point_elts == 3)
	    {
	      gsl_matrix_set (X, i * 3 + j, 9, j == 0);
	      gsl_matrix_set (X, i * 3 + j, 10, j == 1);
	      gsl_matrix_set (X, i * 3 + j, 11, j == 2);
	    }
	  gsl_vector_set (y, i * 3 + j, (targets ? targets[i][j] : rgbtargets[i][j]) * scale2);
	  gsl_vector_set (w, i * 3 + j, 1);
	}
    }
  double chisq;
  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  if (nequations == nvariables && 0)
    {
      for (int i = 0; i < nvariables; i++)
	gsl_vector_set (c, i, gsl_vector_get (y, i));
      gsl_matrix *T = gsl_matrix_alloc (nequations, nvariables);
      for (int i = 0; i < nvariables; i++)
        for (int j = 0; j < nvariables; j++)
	  gsl_matrix_set (T, i, j, gsl_matrix_get (X, i, j));
      bool colinear = gsl_linalg_HH_svx (T, c);
      if (colinear)
	{
	  printf ("Colinear input\n");
	  color_matrix ret;
	  gsl_matrix_free (X);
	  gsl_matrix_free (T);
	  gsl_vector_free (y);
	  gsl_vector_free (w);
	  gsl_vector_free (c);
	  gsl_set_error_handler (old_handler);
	  return ret;
	}
      printf ("HH solved\n");
    }
  else
    {
      gsl_multifit_linear_workspace * work
	= gsl_multifit_linear_alloc (nequations, nvariables);
      gsl_matrix *cov;
      cov = gsl_matrix_alloc (nvariables, nvariables);
      gsl_multifit_wlinear (X, w, y, c, cov,
			    &chisq, work);
      gsl_matrix_free (cov);
      gsl_multifit_linear_free (work);
    }
  if (verbose)
    print_system (stdout, X, y, w, c);
  luminosity_t s = (scale1 / scale2);
  color_matrix ret (C(0) * s, C(1) * s, C(2) * s, dark_point_elts == 1? C(9) * s : dark_point_elts == 3 ? C(9) / scale2 : 0,
		    C(3) * s, C(4) * s, C(5) * s, dark_point_elts == 1? C(9) * s : dark_point_elts == 3 ? C(10) / scale2 : 0,
		    C(6) * s, C(7) * s, C(8) * s, dark_point_elts == 1? C(9) * s : dark_point_elts == 3 ? C(11) / scale2 : 0,
		    0, 0, 0, 1);
  color_solver solver;
  solver.init_by_matrix (ret);
  //printf ("Initial\n");
  //ret.print (stdout);
  //printf ("Initial\n");
  //solver.matrix_by_vals (solver.start).print (stdout);
  solver.colors = colors;
  solver.targets = targets;
  solver.rgbtargets = rgbtargets;
  solver.n = n;
  solver.white = white;
  solver.dark_point_elts = dark_point_elts;
  solver.r = r;
  solver.proportions = proportions;
  if (verbose)
    printf ("Delta E2000 before nonlinear optimization %f\n", solver.objfunc (solver.start));
  simplex<luminosity_t, color_solver>(solver);
  if (verbose)
    printf ("Delta E2000 after nonlinear optimization %f\n", solver.objfunc (solver.start));
  ret = solver.matrix_by_vals (solver.start);
  //printf ("Optimized\n");
  //ret.print (stdout);

  ret.verify_last_row_0001 ();
  gsl_set_error_handler (old_handler);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  if (verbose || report)
    {
      luminosity_t desum = 0, demax = 0;
      if (report)
	report->clear ();
      for (int i = 0; i < n; i++)
	{
	  xyz color1, color2;
	  if (targets)
	    {
	      color1 = targets[i];
	      ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &color2.x, &color2.y, &color2.z);
	    }
	  else
	    {
	      rgbdata c = rgbtargets[i];
	      c.red = r->adjust_luminosity_ir (c.red / proportions.red);
	      c.green = r->adjust_luminosity_ir (c.green / proportions.green);
	      c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);

	      r->set_linear_hdr_color (c.red, c.green, c.blue, &color1.x, &color1.y, &color1.z);
	      ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &c.red, &c.green, &c.blue);
	      c.red = r->adjust_luminosity_ir (c.red / proportions.red);
	      c.green = r->adjust_luminosity_ir (c.green / proportions.green);
	      c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);
	      r->set_linear_hdr_color (c.red, c.green, c.blue, &color2.x, &color2.y, &color2.z);
	    }
	  luminosity_t d = deltaE2000 (color1, color2, white);
#if 0
			    printf ("Compare1 %i\n", i);
			    targets[i].print (stdout);
			    color2.print (stdout);
#endif
	  if (report)
	    report->push_back ({color2, color1, d});
	  desum += d;
	  if (demax < d)
	    demax = d;
	}
      if (verbose)
	{
          //ret.print (stdout);
          printf ("Optimized color matrix DeltaE2000 avg %f, max %f\n", desum / n, demax);
	}
    }
  return ret;
}

bool
optimize_color_model_colors (scr_to_img_parameters *param, image_data &img, render_parameters &rparam, std::vector <point_t> &points, std::vector <color_match> *report, progress_info *progress)
{
   bool verbose = false;
   /* Set up scr-to-img map.  */
   scr_to_img map;
   map.set_parameters (*param, img);
   rgbdata proportions = map.patch_proportions ();

   render_parameters my_rparam = rparam;
   my_rparam.output_profile = render_parameters::output_profile_xyz;

   /* First renderer is interpolated with normal data collection with unadjusted mode.  */
   render_interpolate r (*param, img, my_rparam, 255);
   r.set_unadjusted ();

   /* Second renderer is interpolated with original color collection with unadjusted mode.  */
   render_parameters my_rparam2;
   my_rparam2.original_render_from (my_rparam, true, true);
   render_interpolate r2 (*param, img, my_rparam2, 255);
   r2.set_unadjusted ();
   r2.original_color (false);


   r.precompute_all (progress);
   r2.precompute_all (progress);
   int n = 0;
   rgbdata *colors = (rgbdata *)malloc (sizeof (rgbdata) * points.size ());
   rgbdata *targets = (rgbdata *)malloc (sizeof (rgbdata) * points.size ());

   for (size_t i = 0; i < points.size (); i++)
     {
       int px = points[i].x;
       int py = points[i].y;
       const int range = 2;
       rgbdata color = {0, 0, 0};
#if 0
       int xmin, ymin, xmax, ymax;
       luminosity_t sx, sy;
       map.to_img (px - range, py - range, &sx, &sy);
       xmin = my_floor (sx);
       ymin = my_floor (sy);
       xmax = ceil (sx);
       ymax = ceil (sy);
       map.to_img (px + range, py - range, &sx, &sy);
       xmin = std::min (xmin, (int)my_floor (sx));
       ymin = std::min (ymin, (int)my_floor (sy));
       xmax = std::max (xmax, (int)ceil (sx));
       ymax = std::max (ymax, (int)ceil (sy));
       map.to_img (px - range, py + range, &sx, &sy);
       xmin = std::min (xmin, (int)my_floor (sx));
       ymin = std::min (ymin, (int)my_floor (sy));
       xmax = std::max (xmax, (int)ceil (sx));
       ymax = std::max (ymax, (int)ceil (sy));
       map.to_img (px - range, py - range, &sx, &sy);
       xmin = std::min (xmin, (int)my_floor (sx));
       ymin = std::min (ymin, (int)my_floor (sy));
       xmax = std::max (xmax, (int)ceil (sx));
       ymax = std::max (ymax, (int)ceil (sy));
       xmin -= 5;
       ymin -= 5;
       xmax += 5;
       ymax += 5;
       if (xmin < 0 || ymin < 0 || xmax >= img.width || ymax >= img.height)
	 continue;
       int c = 0;
       for (int y = ymin; y <= ymax; y++)
         for (int x = xmin; x <= xmax; x++)
	   {
	     luminosity_t sx, sy;
	     map.to_scr (x, y, &sx, &sy);
	     if (sx < px - range || sx >= px + range + 1
	         || sy < py - range || sy >= py + range + 1)
	       continue;
	     color += r.get_rgb_pixel (x, y);
	     c++;
	   }
       assert (c);
       color *= (1 / (luminosity_t)c);
#endif
       rgbdata target = {0, 0, 0};
       for (int y = py - range; y <= py + range; y++)
         for (int x = px - range; x <= px + range; x++)
	   {
	     target += r.sample_pixel_scr (x, y);
	     color += r2.sample_pixel_scr (x, y);
	   }
       target *= (1 / (luminosity_t)(2 * (range + 1) * 2 * (range + 1)));
       color *= (1 / (luminosity_t)(2 * (range + 1) * 2 * (range + 1)));

       /* We collect normalized patches with R but non-normalized with R2.  Compensate.  */
       target.red *= proportions.red;
       target.green *= proportions.green;
       target.blue *= proportions.blue;
       //printf ("Point %i %i-%i:%i-%i\n", i, xmin,xmax,ymin,ymax);
       //target.print (stdout);
       //color.print (stdout);
       targets [n] = target;
       colors [n++] = color;
     }
   if (n >= 4)
     {
       color_matrix c = determine_color_matrix (colors, NULL, targets, n, d50_white, 3, report, &r, proportions);
       /* Do basic sanity check.  All the values should be relative close to range 0...1  */
       for (int i = 0; i < 4; i++)
	 for (int j = 0; j < 3; j++)
	   if (!(c.m_elements[i][j] > -10000 && c.m_elements[i][j] < 10000))
	     return false;
       //printf ("Matrix\n");
       //c.print (stdout);
       /* First determine dark point of the scan.  */
       //rparam.profiled_dark  = {c.m_elements[3][0], c.m_elements[3][1], c.m_elements[3][2]};
       color_matrix ci = c.invert ();
       rparam.profiled_dark  = {ci.m_elements[3][0], ci.m_elements[3][1], ci.m_elements[3][2]};

       /* Now obtain scanner response to process red, green and blue.  */
       c.m_elements[3][0] = 0;
       c.m_elements[3][1] = 0;
       c.m_elements[3][2] = 0;
       c = c.invert ();
       rparam.profiled_red   = {c.m_elements[0][0], c.m_elements[0][1], c.m_elements[0][2]};
       rparam.profiled_green = {c.m_elements[1][0], c.m_elements[1][1], c.m_elements[1][2]};
       rparam.profiled_blue  = {c.m_elements[2][0], c.m_elements[2][1], c.m_elements[2][2]};

#if 0
       rgbdata proportions = map.patch_proportions ();
       rparam.profiled_red /= proportions.red;
       rparam.profiled_green /= proportions.green;
       rparam.profiled_blue /= proportions.blue;
#endif
#if 0
       rparam.optimized_dark  = {c.m_elements[3][0], c.m_elements[3][1], c.m_elements[3][2]};
#endif
       if (verbose)
	 {
	   printf ("Dark ");
	   rparam.profiled_dark.print (stdout);
	   printf ("Red ");
	   rparam.profiled_red.print (stdout);
	   printf ("Green ");
	   rparam.profiled_green.print (stdout);
	   printf ("Blue ");
	   rparam.profiled_blue.print (stdout);
	   //printf ("Final\n");
	   //rparam.get_profile_matrix ({1,1,1}).print (stdout);
	 }

#if 0
       auto cm = rparam.color_model;
       luminosity_t pres = rparam.presaturation;
       rparam.presaturation = 1;
       rparam.color_model = render_parameters::color_model_optimized;
       rparam.get_rgb_to_xyz_matrix (&img, true, proportions, d50_white).print (stdout);
       rparam.color_model = cm;
       rparam.presaturation = pres;
#if 0
       rgbdata proportions = map.patch_proportions ();
       rparam.optimized_red /= proportions.red;
       rparam.optimized_green /= proportions.green;
       rparam.optimized_blue /= proportions.blue;
#endif
#endif
       return true;
     }
   return false;
}
