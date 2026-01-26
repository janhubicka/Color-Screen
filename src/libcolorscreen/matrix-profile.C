#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/colorscreen.h"
#include "solver.h"
#include "render-interpolate.h"
#include "gsl-utils.h"
#include "nmsimplex.h"
namespace colorscreen
{

#define C(i) (gsl_vector_get(c,(i)))

namespace
{

/* Nonlinear optimizer for minizing deltaE2000.
   This essentially computes matrix profile to convert
   n colors to targets (specified either in XYZ or RGB) as well
   as possible.

   If targets are specified in RGB they are assumed to be in process color space
   while render is used to convert them to XYZ to evaulate deltaE.  */
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
  luminosity_t epsilon ()
  {
    return 0.000001;
  }
  luminosity_t scale ()
  {
    return 2;
  }
  bool verbose ()
  {
    return false;
  }
  void
  constrain (luminosity_t *vals)
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
    else if (dark_point_elts == 0)
      {
        start[9] = 0;
	start[10] = 0;
	start[11] = 0;
      }
    else
      abort ();
  }

  color_matrix
  matrix_by_vals (luminosity_t *vals)
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
    /* If there is no way to render XYZ just minimize difference.  */
    else if (!r)
      {
	ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &color2.x, &color2.y, &color2.z);
        return fabs (rgbtargets[i].red - color2.x) + fabs (rgbtargets[i].green - color2.y) + fabs (rgbtargets[i].blue - color2.z);
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
  objfunc (luminosity_t *vals)
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
determine_color_matrix (rgbdata *colors, xyz *targets, rgbdata *rgbtargets,
                        int n, xyz white, int dark_point_elts,
                        std::vector<color_match> *report, render *r,
                        rgbdata proportions, progress_info *progress)
{
  rgbdata avg1 = { 0, 0, 0 };
  xyz avg2 = { 0, 0, 0 };
  rgbdata avg3 = { 0, 0, 0 };
  for (int i = 0; i < n; i++)
    {
      avg1 += colors[i];
      if (targets)
        avg2 += targets[i];
      else
        avg3 += rgbtargets[i];
    }
  /* Normalize values to reduce rounoff errors.  */
  luminosity_t scale1 = 3 * n / (avg1.red + avg1.green + avg1.blue);
  luminosity_t scale2 = targets ? 3 * n / (avg2.x + avg2.y + avg2.z)
                                : 3 * n / (avg3.red + avg3.green + avg3.blue);

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
              gsl_matrix_set (X, i * 3 + j, 3 * k + 0,
                              j == k ? colors[i].red * scale1 : 0);
              gsl_matrix_set (X, i * 3 + j, 3 * k + 1,
                              j == k ? colors[i].green * scale1 : 0);
              gsl_matrix_set (X, i * 3 + j, 3 * k + 2,
                              j == k ? colors[i].blue * scale1 : 0);
            }
          if (dark_point_elts == 1)
            gsl_matrix_set (X, i * 3 + j, 9, 1);
          else if (dark_point_elts == 3)
            {
              gsl_matrix_set (X, i * 3 + j, 9, j == 0);
              gsl_matrix_set (X, i * 3 + j, 10, j == 1);
              gsl_matrix_set (X, i * 3 + j, 11, j == 2);
            }
          gsl_vector_set (y, i * 3 + j,
                          (targets ? targets[i][j] : rgbtargets[i][j])
                              * scale2);
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
      gsl_multifit_linear_workspace *work
          = gsl_multifit_linear_alloc (nequations, nvariables);
      gsl_matrix *cov;
      cov = gsl_matrix_alloc (nvariables, nvariables);
      gsl_multifit_wlinear (X, w, y, c, cov, &chisq, work);
      gsl_matrix_free (cov);
      gsl_multifit_linear_free (work);
    }
  if (verbose)
    print_system (stdout, X, y, w, c);
  luminosity_t s = (scale1 / scale2);
  color_matrix ret (C (0) * s, C (1) * s, C (2) * s,
                    dark_point_elts == 1   ? C (9) * s
                    : dark_point_elts == 3 ? C (9) / scale2
                                           : 0,
                    C (3) * s, C (4) * s, C (5) * s,
                    dark_point_elts == 1   ? C (9) * s
                    : dark_point_elts == 3 ? C (10) / scale2
                                           : 0,
                    C (6) * s, C (7) * s, C (8) * s,
                    dark_point_elts == 1   ? C (9) * s
                    : dark_point_elts == 3 ? C (11) / scale2
                                           : 0,
                    0, 0, 0, 1);
  color_solver solver;
  solver.dark_point_elts = dark_point_elts;
  solver.init_by_matrix (ret);
  solver.colors = colors;
  solver.targets = targets;
  solver.rgbtargets = rgbtargets;
  solver.n = n;
  solver.white = white;
  solver.r = r;
  solver.proportions = proportions;
  if (verbose)
    printf ("Delta E2000 before nonlinear optimization %f\n",
            solver.objfunc (solver.start));
  simplex<luminosity_t, color_solver> (solver, "optimizing color profile",
                                       progress);
  if (verbose)
    printf ("Delta E2000 after nonlinear optimization %f\n",
            solver.objfunc (solver.start));
  ret = solver.matrix_by_vals (solver.start);
  // printf ("Optimized\n");
  // ret.print (stdout);

  ret.verify_last_row_0001 ();
  gsl_set_error_handler (old_handler);
  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  if ((verbose || report) && (targets || r))
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
              ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue,
                                &color2.x, &color2.y, &color2.z);
            }
          else
            {
              rgbdata c = rgbtargets[i];
	      /* Adjustments needs to be in normalized scale, but we compensated
	         (which is used for the initial matrix profile).
		 So compensate back.  */
              c.red = r->adjust_luminosity_ir (c.red / proportions.red);
              c.green = r->adjust_luminosity_ir (c.green / proportions.green);
              c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);
              r->set_linear_hdr_color (c.red, c.green, c.blue, &color1.x,
                                       &color1.y, &color1.z);

              ret.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue,
                                &c.red, &c.green, &c.blue);
              c.red = r->adjust_luminosity_ir (c.red / proportions.red);
              c.green = r->adjust_luminosity_ir (c.green / proportions.green);
              c.blue = r->adjust_luminosity_ir (c.blue / proportions.blue);
              r->set_linear_hdr_color (c.red, c.green, c.blue, &color2.x,
                                       &color2.y, &color2.z);
            }
          luminosity_t d = deltaE2000 (color1, color2, white);
          if (report)
            report->push_back ({ color2, color1, d });
          desum += d;
          if (demax < d)
            demax = d;
        }
      if (verbose)
        {
          // ret.print (stdout);
          printf ("Optimized color matrix DeltaE2000 avg %f, max %f\n",
                  desum / n, demax);
        }
    }
  return ret;
}

static void
optimize_color_model_colors_collect (scr_to_img_parameters *param,
                                     image_data &img, int x, int y,
                                     rgbdata &proportions,
                                     render_parameters &rparam,
                                     std::vector<point_t> &points,
                                     rgbdata *colors, rgbdata *targets,
                                     progress_info *progress)
{
  render_parameters my_rparam = rparam;
  image_data *cimg = &img;
  /* For stitched project look up, if there is any sample to analyze and return
     early otherwise.  */
  if (img.stitch)
    {
      size_t i;
      for (i = 0; i < points.size (); i++)
        {
          point_t scr = points[i];
          int tx = -1, ty = -1;
          if (img.stitch->tile_for_scr (NULL, scr.x, scr.y, &tx, &ty, false)
              && x == tx && y == ty)
            break;
        }
      /* No samples on this tile.  */
      if (i == points.size ())
        return;
      /* Set up image and parameters of the tile.  */
      cimg = img.stitch->images[y][x].img.get ();
      param = &img.stitch->images[y][x].param;
      rparam.get_tile_adjustment (img.stitch, x, y).apply (&my_rparam);
    }

  /* Set up scr-to-img map.  */
  scr_to_img map;
  map.set_parameters (*param, *cimg);
  proportions = map.patch_proportions (&my_rparam);

  /* First renderer is interpolated with normal data collection with unadjusted
     mode.  */
  render_interpolate r (*param, *cimg, my_rparam, 255);
  r.set_unadjusted ();

  /* Second renderer is interpolated with original color collection with
     unadjusted mode.  */
  //render_parameters my_rparam2;
  //my_rparam2.original_render_from (my_rparam, true, true);
  render_interpolate r2 (*param, *cimg, my_rparam, 255);
  r2.set_unadjusted ();
  r2.original_color (false);

  r.precompute_all (progress);
  r2.precompute_all (progress);

  for (size_t i = 0; i < points.size (); i++)
    {
      int px = points[i].x;
      int py = points[i].y;
      if (img.stitch)
        {
          point_t scr = points[i];
          int tx, ty;
          if (!img.stitch->tile_for_scr (NULL, scr.x, scr.y, &tx, &ty, false)
              || x != tx || y != ty)
            continue;
          scr = img.stitch->images[y][x].common_scr_to_img_scr (scr);
          px = scr.x;
          py = scr.y;
        }
      const int range = 2;
      rgbdata color = { 0, 0, 0 };
      rgbdata target = { 0, 0, 0 };
      for (int y = py - range; y <= py + range; y++)
        for (int x = px - range; x <= px + range; x++)
          {
            target += r.sample_pixel_scr (x, y);
            color += r2.sample_pixel_scr (x, y);
          }
      target *= (1 / (luminosity_t)((2 * range + 1) * (2 * range + 1)));
      color *= (1 / (luminosity_t)((2 * range + 1) * (2 * range + 1)));

      /* We collect normalized patches with R but non-normalized with R2.
         Compensate.  */
      target.red *= proportions.red;
      target.green *= proportions.green;
      target.blue *= proportions.blue;
      targets[i] = target;
      colors[i] = color;
    }
}

bool
optimize_color_model_colors (scr_to_img_parameters *param, image_data &img,
                             render_parameters &rparam,
                             std::vector<point_t> &points,
                             std::vector<color_match> *report,
                             progress_info *progress)
{
  bool verbose = false;
  int n = points.size ();
  render_parameters my_rparam = rparam;
  my_rparam.output_profile = render_parameters::output_profile_xyz;
  std::vector<rgbdata> colors (points.size ());
  std::vector<rgbdata> targets (points.size ());
  rgbdata proportions;
  if (img.stitch)
    {
      std::vector<bool> used (img.stitch->params.width
                              * img.stitch->params.height);
      std::fill (used.begin (), used.end (), 0);
      for (size_t i = 0; i < points.size (); i++)
        {
          point_t scr = points[i];
          int tx = -1, ty = -1;
          int n;
          if (img.stitch->tile_for_scr (NULL, scr.x, scr.y, &tx, &ty, false))
            {
              if (!used[ty * img.stitch->params.width + tx])
                n++;
              used[ty * img.stitch->params.width + tx] = true;
            }
          else
            colors[i] = targets[i] = { 0, 0, 0 };
        }
      if (progress)
        progress->set_task ("analyzing colors in tiles", n);
      for (int y = 0; y < img.stitch->params.height; y++)
        for (int x = 0; x < img.stitch->params.width; x++)
          if (used[y * img.stitch->params.width + x])
            {
              int stack = 0;
	      {
		sub_task task (progress);
		optimize_color_model_colors_collect (
		    param, img, x, y, proportions, my_rparam, points,
		    colors.data (), targets.data (), progress);
	      }
              if (progress)
                progress->inc_progress ();
            }
    }
  else
    optimize_color_model_colors_collect (param, img, -1, -1, proportions,
                                         my_rparam, points, colors.data (),
                                         targets.data (), progress);
  if (n >= 4)
    {
      render r (img, my_rparam, 255);
      if (!r.precompute_all (false, true, proportions, progress))
	return false;
      color_matrix c = determine_color_matrix (
          colors.data (), NULL, targets.data (), n, d50_white, 3, report, &r,
          proportions, progress);
      if (!progress && progress->cancel_requested ())
        return false;
      /* Do basic sanity check.  All the values should be relative close to
         range 0...1  */
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
          if (!(c.m_elements[i][j] > -10000 && c.m_elements[i][j] < 10000))
            return false;
      color_matrix ci = c.invert ();
      rparam.profiled_dark
          = { ci.m_elements[3][0], ci.m_elements[3][1], ci.m_elements[3][2] };

      /* Now obtain scanner response to process red, green and blue.  */
      c.m_elements[3][0] = 0;
      c.m_elements[3][1] = 0;
      c.m_elements[3][2] = 0;
      c = c.invert ();
      rparam.profiled_red
          = { c.m_elements[0][0], c.m_elements[0][1], c.m_elements[0][2] };
      rparam.profiled_green
          = { c.m_elements[1][0], c.m_elements[1][1], c.m_elements[1][2] };
      rparam.profiled_blue
          = { c.m_elements[2][0], c.m_elements[2][1], c.m_elements[2][2] };

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
        }

      return true;
    }
  return false;
}
}
