#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>


#include "include/colorscreen.h"
#include "include/imagedata.h"
#include "include/scr-to-img.h"
#include "include/finetune.h"
#include "include/color.h"
#include "include/matrix.h"
#include "include/mesh.h"
#include "screen.h"
#include "render.h"
#include "simulate.h"
#include "include/spectrum-to-xyz.h"
#include "lru-cache.h"
#include "include/histogram.h"


using namespace colorscreen;
namespace
{
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

void
test_matrix ()
{
  /* Simple unit test that inversion works. */
  matrix2x2<double> m1 (1, 2, 3, 4);
  matrix2x2<double> m2 (5, 6, 7, 8);
  matrix<double, 2> mm = m1 * m2;
  /* Test that operator() works and points to the right elements.  */
  assert (m1(0, 0) == 1 && m1(0, 1) == 2 && m1(1, 0) == 3 && m1(1, 1) == 4);
  assert (mm(0, 0) == 19 && mm(1, 1) == 50);
  assert (mm(0, 0) == 19 && mm(0, 1) == 22 && mm(1, 0) == 43 && mm(1, 1) == 50);
  mm = m1 * m1.invert ();
  assert (mm(0, 0) == 1 && mm(1, 0) == 0 && mm(0, 1) == 0 && mm(1, 1) == 1);
  matrix4x4<double> m;

  double xr, yr;
  for (int i = 0; i < 100; i++)
    {
      m.randomize ();
      double x = rand () % 100;
      double y = rand () % 100;
      m.perspective_transform (x, y, xr, yr);
      m.inverse_perspective_transform (xr, yr, xr, yr);
      if (fabs (x - xr) > 0.1 || fabs (y - yr) > 0.1)
        {
          printf ("%f %f %f %f %f %f\n", xr, yr, x, y, fabs (x - xr),
                  fabs (y - yr));
          abort ();
        }
    }

  matrix4x4<double> mm1 (2, 0, 0, 1, 0, 2, 5, 0, 2, 0, 2, 0, 0, 0, 0, 2);
  matrix4x4<double> mm2 = mm1.invert ();
  matrix<double, 4> mm3 = mm1 * mm2;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (fabs (mm3(j, i) - (double)(i == j)) > 0.0001)
        {
          mm3.print (stderr);
          abort ();
        }
  matrix3x3<double> mm4 (2, 0, 0, 0, 2, 5, 2, 0, 2);
  matrix3x3<double> mm5 = mm4.invert ();
  matrix<double, 3> mm6 = mm4 * mm5;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (fabs (mm6(j, i) - (double)(i == j)) > 0.0001)
        {
          mm6.print (stderr);
          abort ();
        }
}
void
test_color ()
{
  xyz white = xyz::from_srgb (1, 1, 1);
  assert (fabs (white.x - 0.9505) < 0.0001 && fabs (white.y - 1) < 0.0001
          && fabs (white.z - 0.9505) < 1.0888);
  luminosity_t r, g, b;
  xyz_to_srgb (0.25, 0.40, 0.1, &r, &g, &b);
  assert (fabs (r - 0.4174) < 0.0001 && fabs (g - 0.7434) < 0.0001
          && fabs (b - 0.2152) < 1.0888);
}
bool
compare_scr_to_img (const char *test_name, scr_to_img_parameters & param,
		    scr_to_img_parameters & param2, solver_parameters *sparam,
		    image_data & img, bool keep0, bool lens_correction,
		    coord_t epsilon)
{
  scr_to_img map, map2;
  map.set_parameters (param, img);
  map2.set_parameters (param2, img);
  const int grid = 5;

  struct data
  {
    coord_t avg, max;
    point_t avgoffset;
  } data[grid][grid];

  point_t offset = { 0, 0 };
  point_t doffset = { 0, 0 };
  if (!keep0)
    {
      point_t imgp
	= { (coord_t) (img.width / 2), (coord_t) (img.height / 2) };
      point_t scr1 = map.to_scr (imgp);
      point_t scr2 = map2.to_scr (imgp);
      if (param.type == Finlay || param.type == Paget || param.type == Thames)
	{
	  int_point_t int_offset = ((scr1 - scr2) * 2).nearest ();
	  offset.x = int_offset.x * (coord_t) 0.5;
	  offset.y = int_offset.y * (coord_t) 0.5;
	}
      else
	{
	  int_point_t int_offset = (scr1 - scr2).nearest ();
	  offset.x = int_offset.x;
	  offset.y = int_offset.y;
	}
      doffset = scr1 - scr2;
    }

  for (int y = 0; y < grid; y++)
    for (int x = 0; x < grid; x++)
      {
	int xmin = x * img.width / grid;
	int xmax = (x + 1) * img.width / grid;
	int ymin = y * img.height / grid;
	int ymax = (y + 1) * img.height / grid;
	coord_t sum = 0, maxv = 0;
	point_t offavg = { 0, 0 };
	for (int y = ymin; y < ymax; y += 5)
	  for (int x = xmin; x < xmax; x += 5)
	    {
	      point_t imgp1 = { (coord_t) x, (coord_t) y };
	      // point_t scr1 = map.to_scr (imgp1);
	      point_t scr2 = map2.to_scr (imgp1) + offset;
	      point_t imgp2 = map.to_img (scr2);
	      coord_t dist = imgp1.dist_from (imgp2);
	      offavg += imgp2 - imgp1;
	      sum += dist;
	      if (maxv < dist)
		maxv = dist;
	    }
	coord_t scale = 1 / ((xmax - xmin - 1) * (coord_t) (ymax - ymin - 1));
	offavg *= scale;
	data[y][x].avg = sum * scale;
	data[y][x].max = maxv;
	data[y][x].avgoffset = offavg;
      }

  bool ok = true;
  printf ("\n%s test with scanner %s, process %s and tolerance %f\naverage "
	  "distances:",
	  test_name, scanner_type_names[(int) param.scanner_type].name,
	  scr_names[(int) param.type].name, epsilon);
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %5.4f%c", data[y][x].avg,
		  data[y][x].avg > epsilon ? '!' : ' ');
	  if (data[y][x].avg > epsilon)
	    ok = false;
	}
    }
  printf ("\nmax distances:");
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %5.4f%c", data[y][x].max,
		  data[y][x].max > epsilon ? '!' : ' ');
	  if (data[y][x].max > epsilon)
	    ok = false;
	}
    }
  printf ("\noffsets (offset in the center %f,%f compensated to %f,%f):",
	  doffset.x, doffset.y, offset.x, offset.y);
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
	{
	  printf (" %+5.4f,%+5.4f%c", data[y][x].avgoffset.x,
		  data[y][x].avgoffset.y,
		  data[y][x].avgoffset.length () > epsilon ? '!' : ' ');
	  if (data[y][x].avgoffset.length () > epsilon)
	    ok = false;
	}
    }

  printf ("\nCoordinate1 original: %f,%f solved: %f,%f dist:%f \n",
	  param.coordinate1.x, param.coordinate1.y, param2.coordinate1.x,
	  param2.coordinate1.y,
	  param.coordinate1.dist_from (param2.coordinate1));
  printf ("Coordinate2 original: %f,%f solved: %f,%f dist:%f \n",
	  param.coordinate2.x, param.coordinate2.y, param2.coordinate2.x,
	  param2.coordinate2.y,
	  param.coordinate2.dist_from (param2.coordinate2));
  printf ("tilts original: %f,%f solved: %f,%f\n", param.tilt_x, param.tilt_y,
	  param2.tilt_x, param2.tilt_y);
  if (lens_correction)
    {
      printf ("Lens center original: %f,%f solved: %f,%f dist:%f\n",
	      param.lens_correction.center.x, param.lens_correction.center.y,
	      param2.lens_correction.center.x,
	      param2.lens_correction.center.y,
	      param.lens_correction.center.dist_from (param2.lens_correction.
						      center));
      printf ("Lens correction coeeficients original: %f,%f,%f,%f solved: "
	      "%f,%f,%f,%f\n", param.lens_correction.kr[0],
	      param.lens_correction.kr[1], param.lens_correction.kr[2],
	      param.lens_correction.kr[3], param2.lens_correction.kr[0],
	      param2.lens_correction.kr[1], param2.lens_correction.kr[2],
	      param2.lens_correction.kr[3]);
    }

  if (param.scanner_type != param2.scanner_type)
    {
      printf ("Scanner type mismatch\n");
      ok = false;
    }
  if (!ok)
    {
      printf ("\nInput:\n");
      save_csp (stdout, &param, NULL, NULL, sparam);
      printf ("\nSolution:\n");
      save_csp (stdout, &param2, NULL, NULL, NULL);
    }
  fflush (stdout);
  return ok;
}

bool
do_test_homography (scr_to_img_parameters &param, int width, int height,
                    bool lens_correction, coord_t epsilon)
{
  scr_to_img map;
  image_data img;
  unsigned int g_seed = 0;
  img.set_dimensions (width, height);
  map.set_parameters (param, img);
  solver_parameters sparam;
  sparam.optimize_lens = lens_correction;
  int xstep = (width + 99) / 11;
  int ystep = (height + 99) / 10;
  for (int y = 0; y < height; y += ystep)
    for (int x = 0; x < width; x += xstep)
      {
        point_t img = { (coord_t)x, (coord_t)y };
        point_t scr = map.to_scr (img);
        /* Add 20% outliers */
        if (!lens_correction && !(fast_rand16 (&g_seed) % 4))
          {
            img.x += (fast_rand16 (&g_seed) % 16) - 8;
            img.y += (fast_rand16 (&g_seed) % 16) - 8;
          }
        sparam.add_point (img, scr, solver_parameters::red);
      }
  scr_to_img_parameters param2;
  param2.scanner_type = param.scanner_type;
  solver (&param2, img, sparam);
  return compare_scr_to_img (
      lens_correction ? "Lens correction" : "RANSAC homography", param, param2,
      &sparam, img, true, lens_correction, epsilon);
}
bool
test_homography (bool lens_correction, bool joly, coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = { (coord_t)300, (coord_t)300 };
  param.coordinate1 = { (coord_t)5, (coord_t)1.2 };
  param.coordinate2 = { (coord_t)-1.4, (coord_t)5.2 };
  param.tilt_x = 0.0001;
  param.tilt_y = 0.00001;
  if (joly)
    param.type = Joly;
  if (lens_correction)
    {
      param.lens_correction.center = { 0.4, 0.6 };
      param.lens_correction.kr[1] = 0.01;
      param.lens_correction.kr[2] = 0.03;
      param.lens_correction.kr[3] = 0.05;
      param.lens_correction.normalize ();
    }
  for (int scanner = 0; scanner < max_scanner_type; scanner++)
    {
      param.scanner_type = (enum scanner_type)scanner;
      ok &= do_test_homography (param, 1024, 1024, lens_correction, epsilon);
    }
  return ok;
}

bool
do_test_discovery (scr_to_img_parameters &param, int width, int height, coord_t epsilon)
{
  image_data img;
  scr_detect_parameters dparam;
  render_parameters rparam;
  rparam.gamma = 1.0;
  rparam.screen_blur_radius = 1;
  rparam.sharpen.scanner_mtf_scale = 0;
  render_screen (img, param, rparam, dparam, width, height);
  detect_regular_screen_params dsparams;
  dsparams.min_screen_percentage=90;

  dsparams.scanner_type = param.scanner_type;
  dsparams.gamma = rparam.gamma;
  bool ok = true;
  /* TODO: Disable mesh testing for now; it is broken.  */
  for (int m = 0; m < /*param.type == Dufay ? 2 :*/ 1; m++)
    {
      solver_parameters sparam;
      dsparams.do_mesh = m;
      /* TODO: slow floodfill is broken.  */
      for (int alg = 0; alg < 2; alg++)
	{
	  dsparams.fast_floodfill = alg != 2;
	  dsparams.slow_floodfill = alg != 1;

	  /* Save some time with slow floodfill.  */
	  if (!dsparams.fast_floodfill && m)
	    continue;
	  /* Lens solving is slow with many points and this way we stress more mesh transformations.  */
	  if (m)
	    sparam.optimize_lens = sparam.optimize_tilt = false;
	  printf ("Mesh: %i, fast floodfill %i slow floodfill %i\n", dsparams.do_mesh, dsparams.fast_floodfill, dsparams.slow_floodfill);
	  auto detected
	      = detect_regular_screen (img, dparam, sparam, &dsparams, NULL, NULL);
	  if (!detected.success)
	    {
	      printf ("Screen discovery failed; saving screen to out.tif\n");
	      img.save_tiff ("out.tif");
	      return false;
	    }
	  ok &= compare_scr_to_img ("Screen discovery", param, detected.param, &sparam,
				    img, false, false, epsilon);
	  if (!ok)
	    {
	      printf ("Screen discovery out of tolerance; saving screen to out.tif\n");
	      img.save_tiff ("out.tif");
	      return false;
	    }
	}
    }
  return ok;
}

bool error_found = false;

void
report (const char *name, bool ok)
{
  static int testnum = 0;
  testnum++;
  printf ("%sok %i - %s\n", ok ? "" : "not ", testnum, name);
  if (!ok)
    error_found = true;
  fflush (stdout);
}
bool
test_discovery (coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = { (coord_t)300, (coord_t)300 };
  param.coordinate1 = { (coord_t)10, (coord_t)1.2 };
  param.coordinate2 = { (coord_t)-1.4, (coord_t)10 };
  param.tilt_x = 0.001;
  param.tilt_y = 0.0001;
  param.lens_correction.center = {0.3,0.7};
  param.lens_correction.kr[1] = -0.01;
  param.lens_correction.kr[2] = 0.02;
  param.lens_correction.kr[3] = 0.03;
  param.lens_correction.normalize ();
  param.type = Finlay;
  param.scanner_type = fixed_lens;
  ok &= do_test_discovery (param, 1024, 1024, epsilon);
  param.type = Dufay;
  ok &= do_test_discovery (param, 1024, 1024, epsilon);
  return ok;
}

bool
test_screen_blur ()
{
  screen mstr;
  mstr.initialize (Paget);
  std::unique_ptr <screen> scr1 (new screen);
  std::unique_ptr <screen> scr2 (new screen);
  std::unique_ptr <screen> scr3 (new screen);

  for (int i = 0; i < 100; i++)
    {
      luminosity_t radius = i * screen::max_blur_radius / 100;
      scr1->initialize_with_blur (mstr, radius, screen::blur_fft);
      scr2->initialize_with_blur (mstr, radius, screen::blur_direct);
      luminosity_t delta;

      /* For very small blurs fft produces roundoff errors along sharp edges.  */
      if (!scr1->almost_equal_p (*scr2, &delta, i < 20 ? 0.006 : 1.0/2048))
        {
	  fprintf (stderr, "FFT Gaussian blur does not match direct version radius %f delta %f (step %i); see /tmp/scr-*.tif \n", radius, delta, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  scr2->save_tiff ("/tmp/scr-nofft.tif");
	  std::unique_ptr <screen> diff (new screen);
	  for (int y = 0; y < screen::size; y++)
	   for (int x = 0; x < screen::size; x++)
	     for (int c = 0; c < 3; c++)
		diff->mult[y][x][c] = 0.5 + (scr2->mult[y][x][c] - scr1->mult[y][x][c]);
	  diff->save_tiff ("/tmp/scr-diff.tif");
	  return false;
        }
      rgbdata rgbdelta;
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.005))
        {
	  fprintf (stderr, "FFT Gaussian blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
      scr3->initialize_with_blur (mstr, radius, screen::blur_fft2d);
      /* For very small blurs fft produces roundoff errors along sharp edges.  */
      if (!scr2->almost_equal_p (*scr3, &delta, i < 20 || i > 80 ? 0.006 : 1.0/1024))
        {
	  fprintf (stderr, "FFT Gaussian blur does not FFT2D version radius %f delta %f (step %i); see /tmp/scr-*.tif \n", radius, delta, i);
	  //scr1->save_tiff ("/tmp/scr-fft.tif");
	  scr2->save_tiff ("/tmp/scr-nofft.tif");
	  scr3->save_tiff ("/tmp/scr-fft2d.tif");
	  std::unique_ptr <screen> diff (new screen);
	  for (int y = 0; y < screen::size; y++)
	   for (int x = 0; x < screen::size; x++)
	     for (int c = 0; c < 3; c++)
		diff->mult[y][x][c] = 0.5 + (scr3->mult[y][x][c] - scr2->mult[y][x][c]);
	  diff->save_tiff ("/tmp/scr-diff.tif");
	  return false;
        }

      scr1->initialize_with_blur (mstr, radius, screen::blur_fft);
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.006))
        {
	  fprintf (stderr, "FFT mtffilter blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
    }
  return true;
}

/* Internal unit test for the precomputed_function class.  */
static bool
test_precomputed_function ()
{
  bool ok = true;
  /* Test functional constructor with x^2.  */
  precomputed_function<double> f (0, 10, 101, [] (double x) { return x * x; });

  for (double x = 0; x <= 10; x += 0.5)
    {
      double val = f.apply (x);
      if (std::abs (val - x * x) > 0.01)
        {
          printf ("FAILED: precomputed_function x^2 mismatch at %f: "
                  "expected %f, got %f\n",
                  (double)x, (double)(x * x), (double)val);
          ok = false;
        }
    }

  /* Test move constructor.  */
  precomputed_function<double> f2 = std::move (f);
  if (std::abs (f2.apply (5.0) - 25.0) > 0.01)
    {
      printf ("FAILED: precomputed_function move constructor failed!\n");
      ok = false;
    }

  /* Test monotonicity and inverse.  */
  if (std::abs (f2.invert (49.0) - 7.0) > 0.01)
    {
      printf ("FAILED: precomputed_function inverse mismatch: "
              "expected 7.0, got %f\n",
              (double)f2.invert (49.0));
      ok = false;
    }

  return ok;
}

/* Internal unit test for the histogram parallel collection.  */
static bool
test_histogram_parallel ()
{
  printf ("Testing histogram parallel collection...\n");
  histogram h;
  const int n = 1000000;

  /* Stage 1: Parallel range.  */
#pragma omp parallel for reduction(histogram_range : h)
  for (int i = 0; i < n; i++)
    h.pre_account ((luminosity_t)(i % 1000));

  if (h.find_min (0) != 0 || h.find_max (0) != 999)
    {
      printf ("FAILED: Parallel range mismatch! Min: %f Max: %f\n",
              (double)h.find_min (0), (double)h.find_max (0));
      return false;
    }

  h.finalize_range (1000);

  /* Stage 2: Parallel entries.  */
#pragma omp parallel for reduction(histogram_entries : h)
  for (int i = 0; i < n; i++)
    h.account ((luminosity_t)(i % 1000));

  h.finalize ();

  if (h.num_samples () != n)
    {
      printf ("FAILED: Total count mismatch! Expected %i, got %i\n", n,
              h.num_samples ());
      return false;
    }

  for (int i = 0; i < 1000; i++)
    if (h.entry (i) != 1000)
      {
        printf ("FAILED: Entry %i count mismatch! Expected 1000, got %llu\n", i,
                (unsigned long long)h.entry (i));
        return false;
      }

  return true;
}

bool
test_richards_curve ()
{
  bool ok = true;
  // Test direct curve
  luminosity_t A = -5;
  luminosity_t K = 5;
  luminosity_t B = 1.2;
  luminosity_t M = 0;
  luminosity_t v = 0.8;
  
  hd_curve_parameters params_direct = richards_to_hd_curve_parameters({A, K, B, M, v, false});
  richards_hd_curve curve_direct(1000, params_direct);
  
  for (int i = 5; i < 95; i++)
    {
      luminosity_t X = params_direct.minx + i * (params_direct.maxx - params_direct.minx) / 100.0;
      luminosity_t expected = A + (K - A) / std::pow(1.0 + std::exp(-B * (X - M)), 1.0 / v);
      luminosity_t actual = curve_direct.apply(X);
      if (std::abs(expected - actual) > 0.3)
        {
          printf ("Direct Richards curve mismatch at x=%f: expected %f, got %f\n", X, expected, actual);
          ok = false;
        }
    }

  // Test inverse curve
  luminosity_t Ax = -6;
  luminosity_t Kx = 6;
  luminosity_t Bx = 1.0;
  luminosity_t Mx = 0;
  luminosity_t vx = 1.0;
  
  hd_curve_parameters params_inverse = richards_to_hd_curve_parameters({Ax, Kx, Bx, Mx, vx, true});
  // Test inverse curve with more points for better logit resolution
  richards_hd_curve curve_inverse(10000, params_inverse);
  
  for (int i = 48; i < 52; i++)
    {
      luminosity_t Y = params_inverse.miny + i * (params_inverse.maxy - params_inverse.miny) / 100.0;
      luminosity_t expected_X = Ax + (Kx - Ax) / std::pow(1.0 + std::exp(-Bx * (Y - Mx)), 1.0 / vx);
      
      // Probing Inverse Richards Curve (X as function of Y)
      luminosity_t actual_Y = curve_inverse.apply(expected_X);
      if (std::abs(Y - actual_Y) > 1.0)
        {
          printf ("Inverse Richards curve mismatch at X=%f: expected Y=%f, got Y=%f\n", expected_X, Y, actual_Y);
          ok = false;
        }
    }
  return ok;
}

bool
test_richards_symmetry ()
{
  bool ok = true;
  hd_curve_parameters p;
  p.minx = -2.274010; p.miny = 3.400111;
  p.linear1x = -1.341965; p.linear1y = 1.402846;
  p.linear2x = -0.789100; p.linear2y = 0.927726;
  p.maxx = -0.437047; p.maxy = -0.003900;
  
  // 1. Toggling inverse should match swapping X and Y
  hd_curve_parameters p_swapped(p.miny, p.minx, p.linear1y, p.linear1x, p.linear2y, p.linear2x, p.maxy, p.maxx);
  
  auto r1 = hd_to_richards_curve_parameters(p); // detected direct (if gamma is low) or inverse
  auto r2 = hd_to_richards_curve_parameters(p_swapped);
  
  // They should have same B, M, v but A, K swapped and is_inverse toggled
  if (std::abs(r1.B - r2.B) > 1e-4 || std::abs(r1.v - r2.v) > 1e-4)
    {
       printf("Richards Symmetry FAIL: B1=%f B2=%f, v1=%f v2=%f\n", r1.B, r2.B, r1.v, r2.v);
       ok = false;
    }
    
  // 2. richards_to_hd should also be symmetric
  auto p1 = richards_to_hd_curve_parameters({-2, 2, 1.5, 0, 0.5, true});
  auto p2 = richards_to_hd_curve_parameters({-2, 2, 1.5, 0, 0.5, false});
  
  // p1.miny should be p2.minx, etc.
  if (std::abs(p1.miny - p2.minx) > 1e-4 || std::abs(p1.maxx - p2.maxy) > 1e-4)
    {
       printf("Richards Reverse Symmetry FAIL: p1.miny=%f p2.minx=%f\n", p1.miny, p2.minx);
       ok = false;
    }
    
  return ok;
}

bool
test_richards_reversibility ()
{
  bool ok = true;
  /* Test points: A, K, B, M, v, is_inverse */
  struct richards_curve_parameters test_params[] = {
    {0.0, 1.0, 1.5, 0.5, 1.0, false},
    {0.1, 2.5, 2.0, -1.0, 0.8, false},
    {-0.5, 3.0, 0.5, 2.0, 1.5, false},
    {0.0, 4.0, 1.0, 2.0, 1.0, true},
    {1.0, 5.0, 0.7, 3.0, 1.2, true},
    /* User case: Negative slope curve (requires negative B in inverse mode) */
    {-2.274, -0.437, -3.986, 0.997, 2.647, true},
    /* User case: Negative slope curve in direct mode (requires x-swapping) */
    {3.4, 0.0, 1.7, 1.0, 1.0, false}
  };

  for (auto &p : test_params)
    {
       hd_curve_parameters hdp = richards_to_hd_curve_parameters(p);
       richards_curve_parameters rp = hd_to_richards_curve_parameters(hdp);
       
       /* We use heuristic to determine v, so it is not recovered perfectly.  */
       if (std::abs(rp.A - p.A) > 1e-4 || std::abs(rp.K - p.K) > 1e-4 ||
           std::abs(rp.B - p.B) > 0.2 || std::abs(rp.M - p.M) > 0.1 ||
           std::abs(rp.v - p.v) > 0.2 || rp.is_inverse != p.is_inverse)
         {
            printf ("Richards reversibility failed for %s mode!\n", p.is_inverse ? "inverse" : "direct");
            printf ("Expected: A=%f, K=%f, B=%f, M=%f, v=%f\n", p.A, p.K, p.B, p.M, p.v);
            printf ("Got:      A=%f, K=%f, B=%f, M=%f, v=%f\n", rp.A, rp.K, rp.B, rp.M, rp.v);
            ok = false;
         }
    }
  return ok;
}

bool
test_richards_functional_inverse ()
{
  bool ok = true;
  /* Test that Richards_inv(Richards_dir(X)) == X (functionally)
     Note: In our implementation, richards_hd_curve(p, true) maps LogE -> Density 
     using the inverted formula. So curve_inv.apply(f_dir(y)) should be y. */
     
  luminosity_t A = 0.5, K = 3.5, B = 1.2, M = 1.0, v = 0.8;
  richards_curve_parameters rp_dir(A, K, B, M, v, false);
  richards_curve_parameters rp_inv(A, K, B, M, v, true);
  
  richards_hd_curve curve_dir(1000, rp_dir);
  richards_hd_curve curve_inv(1000, rp_inv);
  
  // Probing: density y -> exposure x=Richards(y) -> recovered_y = curve_inv.apply(x)
  for (int i = 20; i < 80; i++)
    {
      luminosity_t y = -5.0 + i * 10.0 / 100.0;
      // Step 1: Calculate LogE from Density using Direct Richards formula
      luminosity_t x = A + (K - A) / std::pow(1.0 + std::exp(-B * (y - M)), 1.0 / v);
      
      // Step 2: Use Inverted H&D curve to map LogE back to Density
      luminosity_t recovered_y = curve_inv.apply(x);
      
      if (std::abs(y - recovered_y) > 0.1)
        {
          printf ("Richards functional inverse failed at y=%f: x=%f, recovered_y=%f\n", y, x, recovered_y);
          ok = false;
        }
    }
    
  return ok;
}

bool
test_hd_reversibility ()
{
  bool ok = true;
  /* Test that HD -> Richards -> HD' -> Richards is stable.
     Starting with user provided sample parameters. */
  hd_curve_parameters hurley = {
      -2.745997, 3.133772,
      -1.930210, 2.190697,
      -0.970248, 1.208836,
      -0.299072, -0.399532
  };
  
  // Step 1: Fit Richards model to original H&D
  richards_curve_parameters rp1 = hd_to_richards_curve_parameters(hurley);

  /* Check that the Richards model actually passes through the original knots. 
     Our analytic solver in hd_to_richards is designed to be exact for knots. */
  luminosity_t y1_fit = richards_hd_curve::eval_richards(rp1, hurley.linear1x);
  luminosity_t y2_fit = richards_hd_curve::eval_richards(rp1, hurley.linear2x);
  
  if (std::abs(y1_fit - hurley.linear1y) > 1e-3 || std::abs(y2_fit - hurley.linear2y) > 1e-3)
    {
       printf ("Richards fit fidelity failed for Hurley parameters!\n");
       printf ("L1: expected %f, got %f\n", hurley.linear1y, y1_fit);
       printf ("L2: expected %f, got %f\n", hurley.linear2y, y2_fit);
       ok = false;
    }
  
  // Step 2: Generate new H&D points from that Richards model
  hd_curve_parameters hdp_prime = richards_to_hd_curve_parameters(rp1);
  
  // They represent the same curve. Check that the reconstructed knots are on the original model.
  luminosity_t y1_prime_fit = richards_hd_curve::eval_richards(rp1, hdp_prime.linear1x);
  luminosity_t y2_prime_fit = richards_hd_curve::eval_richards(rp1, hdp_prime.linear2x);

  if (std::abs(y1_prime_fit - hdp_prime.linear1y) > 1e-4 || 
      std::abs(y2_prime_fit - hdp_prime.linear2y) > 1e-4)
    {
      printf ("Reconstructed H&D points are not on the Richards sigmoid!\n");
      ok = false;
    }

  return ok;
}

bool
test_hd_incremental_update ()
{
  bool ok = true;
  /* Test that adjusting Richards parameters via geometric transformations 
     actually produces a valid and consistent new model. */
  hd_curve_parameters hurley = {
      -2.745997, 3.133772,
      -1.930210, 2.190697,
      -0.970248, 1.208836,
      -0.299072, -0.399532
  };
  
  auto rp1 = hd_to_richards_curve_parameters(hurley);
  
  // 1. Test M adjustment (Translation)
  {
    auto rp_new = rp1;
    rp_new.M += 0.5;
    auto hurley_new = hurley;
    hurley_new.adjust_M(rp1.M, rp_new.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental M adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.M - rp_new.M) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental M adjustment: parameter drift! M: expected %f got %f, B: expected %f got %f\n",
		rp_new.M, rp_fit.M, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  // 2. Test B adjustment (Scaling)
  {
    auto rp_new = rp1;
    rp_new.B *= 1.2;
    auto hurley_new = hurley;
    hurley_new.adjust_B(rp1.B, rp_new.B, rp1.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental B adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.B - rp_new.B) > 0.05 || std::abs(rp_fit.M - rp_new.M) > 1e-3)
      {
	printf ("Incremental B adjustment: parameter drift! B: expected %f got %f, M: expected %f got %f\n",
		rp_new.B, rp_fit.B, rp_new.M, rp_fit.M);
	ok = false;
      }
  }

  // 3. Test v adjustment (Non-linear)
  {
    auto rp_new = rp1;
    rp_new.v *= 0.8;
    auto hurley_new = hurley;
    hurley_new.adjust_v(rp1.v, rp_new.v, rp1.B, rp1.M);
    
    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental v adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.v - rp_new.v) > 0.1 || std::abs(rp_fit.B - rp_new.B) > 0.1 || std::abs(rp_fit.M - rp_new.M) > 0.05)
      {
	printf ("Incremental v adjustment: parameter drift! v: exp %f got %f, B: exp %f got %f, M: exp %f got %f\n",
		rp_new.v, rp_fit.v, rp_new.B, rp_fit.B, rp_new.M, rp_fit.M);
	ok = false;
      }
  }

  // 4. Test A adjustment (Lower asymptote)
  {
    auto rp_new = rp1;
    rp_new.A -= 0.2;
    auto hurley_new = hurley;
    hurley_new.adjust_A(rp1.A, rp_new.A, rp1.K);

    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental A adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.A - rp_new.A) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental A adjustment: parameter drift! A: exp %f got %f, B: exp %f got %f\n",
		rp_new.A, rp_fit.A, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  // 5. Test K adjustment (Upper asymptote)
  {
    auto rp_new = rp1;
    rp_new.K += 0.3;
    auto hurley_new = hurley;
    hurley_new.adjust_K(rp1.K, rp_new.K, rp1.A);

    luminosity_t y1_fit = richards_hd_curve::eval_richards(rp_new, hurley_new.linear1x);
    if (std::abs(y1_fit - hurley_new.linear1y) > 1e-3)
      {
        printf ("Incremental K adjustment: point moved off curve! expected %f, got %f\n", hurley_new.linear1y, y1_fit);
        ok = false;
      }
    auto rp_fit = hd_to_richards_curve_parameters(hurley_new);
    if (std::abs(rp_fit.K - rp_new.K) > 1e-3 || std::abs(rp_fit.B - rp_new.B) > 0.05)
      {
	printf ("Incremental K adjustment: parameter drift! K: exp %f got %f, B: exp %f got %f\n",
		rp_new.K, rp_fit.K, rp_new.B, rp_fit.B);
	ok = false;
      }
  }

  return ok;
}

bool
test_hd_validity ()
{
  bool ok = true;
  // S-shape Direct
  hd_curve_parameters p1 (-3, 0, -2, 1, 2, 3, 3, 4);
  if (!p1.is_valid_for_richards_curve())
    {
      printf("H&D validity failed for valid direct curve\n");
      ok = false;
    }
  
  // Non-monotonic X (X-loop)
  hd_curve_parameters p2 (-3, 0, 1, 1, -2, 3, 3, 4);
  if (p2.is_valid_for_richards_curve())
    {
      printf("H&D validity failed: accepted non-monotonic X\n");
      ok = false;
    }
    
  // Non-monotonic Y (Y-loop)
  hd_curve_parameters p3 (-3, 0, -2, 3, 2, 1, 3, 4);
  if (p3.is_valid_for_richards_curve())
    {
      printf("H&D validity failed: accepted non-monotonic Y\n");
      ok = false;
    }

  return ok;
}

bool
test_hd_sorting ()
{
  bool ok = true;
  // Decreasing X
  hd_curve_parameters p1 (5, 0, 4, 1, 1, 3, 0, 4);
  p1.sort_by_x();
  if (p1.minx != 0 || p1.maxx != 5 || p1.linear1x != 1 || p1.linear2x != 4)
    {
       printf("H&D sorting failed to reverse decreasing X\n");
       ok = false;
    }
  return ok;
}

bool
test_custom_tone_curve ()
{
  bool ok = true;
  // Test default points
  tone_curve c1 (tone_curve::tone_curve_custom);
  if (fabs (c1.apply (0.21764) - 0.46303) > 0.001)
    {
      printf ("Default custom tone curve mismatch at 0.21764: got %f, expected 0.46303\n", c1.apply (0.21764));
      ok = false;
    }

  // Test explicit points
  std::vector<point_t> cp = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
  tone_curve c2 (cp);
  for (float x = 0; x <= 1.0; x += 0.1)
    {
      if (fabs (c2.apply (x) - x) > 0.001)
	{
	  printf ("Linear custom tone curve mismatch at %f: got %f, expected %f\n", x, c2.apply (x), x);
	  ok = false;
	}
    }

  // Test non-linear interpolation
  std::vector<point_t> cp3 = {{0.0, 0.0}, {0.5, 0.25}, {1.0, 1.0}};
  tone_curve c3 (cp3);
  if (fabs (c3.apply (0.5) - 0.25) > 0.001)
    {
      printf ("Non-linear custom tone curve mismatch at 0.5: got %f, expected 0.25\n", c3.apply (0.5));
      ok = false;
    }

  return ok;
}

int
test_render_linearity ()
{
  render_parameters rparam;
  image_data img;
  img.set_dimensions (65536, 1, true, false);
  for (int i = 0; i < 65536; i++)
    {
      img.rgbdata[0][i].r=i;
      img.rgbdata[0][i].g=i;
      img.rgbdata[0][i].b=i;
    }
  bool ok = true;
  luminosity_t gammas[] = {-1, 1, 1.8, 2.2, 2.8};

  /* sRGB and linear gamma should be handled perfectly.
     Gammas about 1.5 are steep enough so initial segment has too large
     gradient for out_lookup_table_size, so only check larger values.  */
  int mins[] = {0, 0, 40, 220, 1000};
  for (unsigned gamma_idx = 0; gamma_idx < sizeof (gammas) / sizeof (luminosity_t); gamma_idx ++)
    {
      luminosity_t gamma = gammas[gamma_idx];
      rparam.gamma = gamma;
      rparam.output_gamma = gamma;
      rparam.output_profile = render_parameters::output_profile_original;
      render ren (img, rparam, 65535);
      if (!ren.precompute_all (true, false, {1, 1, 1}, NULL))
	return false;
      for (int i = 0; i < 65535; i++)
	{
	  int r, g, b;
	  luminosity_t linear = apply_gamma (i / (luminosity_t)65535, gamma);

	  /* Gamma should be invertible.  */
	  int gg = (int)(invert_gamma (linear, gamma) * 65535 + 0.5);
	  if (gg != i)
	    {
	      printf ("Gamma is non-invertible at gamma %f: %i becomes %i\n", gamma, i, gg);
	      ok = false;
	    }
	  /* Check that linearization works as expected.  */
	  if (fabs (linear - ren.get_data_red (i, 0)) > 1.0/655350)
	    {
	      printf ("Bad linearization of %i: %f should be %f\n", i, linear, ren.get_data_red (i, 0));
	      ok = false;
	    }
	  /* Now out_lookup_table is applied.  */
	  ren.out_color.final_color (ren.get_data_red (i,0), ren.get_data_green (i,0), ren.get_data_blue (i,0),
			 &r, &g, &b);
	  if (i > mins[gamma_idx] && (r != i || g != i || b != i))
	    {
	      printf ("Render is non-linear at gamma %f linear: %i becomes %i %i %i (with table)\n",
		      gamma, i, r, g, b);
	      ok = false;
	    }
	  luminosity_t hr,hg,hb;
	  ren.out_color.hdr_final_color (ren.get_data_red (i,0), ren.get_data_green (i,0), ren.get_data_blue (i,0),
			     &hr, &hg, &hb);
	  int rr = hr * 65535 + 0.5;
	  gg = hg * 65535 + 0.5;
	  int bb = hb * 65535 + 0.5;
	  if (rr != i || gg != i || bb != i)
	    {
	      printf ("Render is non-linear at gamma %f linear: %i becomes %i %i %i (with hdr)\n",
		      gamma, i, r, g, b);
	      ok = false;
	    }
	}
    }
  return ok;
}

struct test_params
{
  int x;
  bool
  operator== (const test_params &other) const
  {
    return x == other.x;
  }
};

std::atomic<int> get_new_calls;

std::unique_ptr<int>
get_new_test (test_params &p, progress_info *)
{
  get_new_calls++;
  std::this_thread::sleep_for (std::chrono::milliseconds (100));
  return std::make_unique<int> (p.x * 2);
}

bool
test_lru_cache_concurrency ()
{
  lru_cache<test_params, int, get_new_test, 10> cache ("test_cache");
  const int num_threads = 10;
  std::vector<std::thread> threads;
  std::vector<std::shared_ptr<int>> results (num_threads);
  test_params p = { 42 };
  get_new_calls = 0;

  for (int i = 0; i < num_threads; ++i)
    {
      threads.emplace_back ([&, i] () { results[i] = cache.get (p, NULL); });
    }

  for (auto &t : threads)
    t.join ();

  bool ok = true;
  if (get_new_calls != 1)
    {
      printf ("LRU concurrency test FAIL: get_new called %d times (expected 1)\n",
	      (int)get_new_calls);
      ok = false;
    }
  for (int i = 0; i < num_threads; ++i)
    {
      if (!results[i] || *results[i] != 84)
	{
	  printf ("LRU concurrency test FAIL: thread %d got wrong result\n", i);
	  ok = false;
	}
    }
  return ok;
}

/* test_spectrum_dyes_to_xyz performs unit tests for the spectrum_dyes_to_xyz class.  */
bool
test_spectrum_dyes_to_xyz ()
{
  spectrum_dyes_to_xyz dyes;
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5000);
  xyz wp;
  
  bool ok = true;
  // Test Illuminant A (Incandescent)
  {
    spectrum_dyes_to_xyz dyesA;
    dyesA.set_backlight (spectrum_dyes_to_xyz::il_A, 2856);
    wp = dyesA.whitepoint_xyz ();
    if (fabs (wp.x - 1.0985) > 0.001 || fabs (wp.z - 0.3558) > 0.001)
      {
	printf ("FAILED: Illuminant A whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant C (Average Daylight)
  {
    spectrum_dyes_to_xyz dyesC;
    dyesC.set_backlight (spectrum_dyes_to_xyz::il_C, 6774);
    wp = dyesC.whitepoint_xyz ();
    if (fabs (wp.x - 0.9807) > 0.001 || fabs (wp.z - 1.1822) > 0.001)
      {
	printf ("FAILED: Illuminant C whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant D65
  {
    spectrum_dyes_to_xyz dyesD65;
    dyesD65.set_backlight (spectrum_dyes_to_xyz::il_D, 6504);
    wp = dyesD65.whitepoint_xyz ();
    if (fabs (wp.x - 0.9505) > 0.002 || fabs (wp.z - 1.0891) > 0.002)
      {
	printf ("FAILED: Illuminant D65 whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }

  // Test Illuminant Equal Energy (E)
  {
    spectrum_dyes_to_xyz dyesE;
    dyesE.set_backlight (spectrum_dyes_to_xyz::il_equal_energy);
    wp = dyesE.whitepoint_xyz ();
    if (fabs (wp.x - 1.0) > 0.001 || fabs (wp.y - 1.0) > 0.001 || fabs (wp.z - 1.0) > 0.001)
      {
	printf ("FAILED: Illuminant E whitepoint mismatch. Got (%f, %f, %f)\n", wp.x, wp.y, wp.z);
	ok = false;
      }
  }
  if (!ok)
    return false;

  // Test normalization
  dyes.normalize_brightness ();
  xyz wp_norm = dyes.dyes_rgb_to_xyz (1, 1, 1);
  if (fabs (wp_norm.y - 1.0) > 0.0001)
    {
      printf ("FAILED: Normalized brightness Y should be 1.0, got %f\n", wp_norm.y);
      return false;
    }

  // Test that characteristic curve setting doesn't affect dyes_rgb_to_xyz linearity.
  // The characteristic curve is applied in film_rgb_response, not dyes_rgb_to_xyz.
  dyes.set_characteristic_curve (spectrum_dyes_to_xyz::input_curve);
  if (!dyes.is_linear ())
    {
       printf ("FAILED: dyes_rgb_to_xyz should remain linear even with characteristic curve set\n");
       return false;
    }

  // Test matrix generation
  color_matrix m = dyes.xyz_matrix ();
  if (fabs (m (3, 3) - 1.0) > 0.0001)
    {
       printf ("FAILED: Matrix diagonal should be 1.0, got %f\n", m (3, 3));
       return false;
    }

  return true;
}

/* test_whitepoint_constants verifies that the spectral path whitepoints computed for various
   standard illuminants match the hardcoded xyz constants in color.h.  */
bool
test_whitepoint_constants ()
{
  bool ok = true;
  spectrum_dyes_to_xyz dyes;
  xyz wp;
  
  auto compare_wp = [&] (const char *name, xyz spec_wp, xyz const_wp, luminosity_t eps = 0.002) {
    if (!spec_wp.almost_equal_p (const_wp, eps))
      {
	printf ("FAILED: %s whitepoint mismatch!\n", name);
	printf ("  Spectral: (%f, %f, %f)\n", spec_wp.x, spec_wp.y, spec_wp.z);
	printf ("  Constant: (%f, %f, %f)\n", const_wp.x, const_wp.y, const_wp.z);
	printf ("  Diff:     (%f, %f, %f)\n", spec_wp.x - const_wp.x, spec_wp.y - const_wp.y, spec_wp.z - const_wp.z);
	ok = false;
      }
  };

  // 1. Illuminant A (Incandescent) - 2856K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_A, 2856);
  compare_wp ("Illuminant A", dyes.whitepoint_xyz (), il_A_white);

  // 2. Illuminant B (Direct Sunlight) - 4874K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_B, 4874);
  compare_wp ("Illuminant B", dyes.whitepoint_xyz (), il_B_white);

  // 3. Illuminant C (Average Daylight) - 6774K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_C, 6774);
  compare_wp ("Illuminant C", dyes.whitepoint_xyz (), il_C_white);

  // 4. Illuminant D50 - 5003K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5003);
  compare_wp ("Illuminant D50", dyes.whitepoint_xyz (), d50_white);

  // 5. Illuminant D55 - 5503K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 5503);
  compare_wp ("Illuminant D55", dyes.whitepoint_xyz (), d55_white);

  // 6. Illuminant D65 - 6504K
  dyes.set_backlight (spectrum_dyes_to_xyz::il_D, 6504);
  compare_wp ("Illuminant D65", dyes.whitepoint_xyz (), d65_white);

  // 7. sRGB Whitepoint (D65)
  // sRGB standard specifically uses D65.
  compare_wp ("sRGB/D65", dyes.whitepoint_xyz (), srgb_white);

  return ok;
}

/* Verify that lens warp correction works correctly and agrees with DNG.  */
bool
test_lens_warp ()
{
  bool ok = true;
  struct test_case {
    const char *name;
    coord_t kr[4];
    point_t center;
  } cases[] = {
    { "Synthetic Barrel", { 1.0, 0.05, 0.02, 0.01 }, { 0.5, 0.5 } },
    { "Nikon Coolscan 9000ED", { 0.99508, 0.0245411, -0.0521967, 0.0325757 }, { 0.560586, 0.482547 } },
    { "Adobe DNG Sample", { 0.999787, 0.000025, -0.000025, 0.000006 }, { 0.500029, 0.499863 } }
  };

  for (const auto& tc : cases)
    {
      lens_warp_correction_parameters p;
      for (int i = 0; i < 4; i++) p.kr[i] = tc.kr[i];
      p.center = tc.center;

      if (!p.is_monotone ())
        {
          printf ("FAILED: is_monotone should be true for %s!\n", tc.name);
          ok = false;
        }

      lens_warp_correction lw;
      lw.set_parameters (p);
      point_t img_center = { 500, 500 };
      
      /* Calculate scan corners for normalization.  */
      lens_warp_correction lw_prep;
      lw_prep.set_parameters (p);
      if (!lw_prep.precompute (img_center, { 0, 0 }, { 1000, 0 }, { 1000, 1000 }, { 0, 1000 }))
	return false;
      point_t scan_c1 = lw_prep.corrected_to_scan ({ 0, 0 });
      point_t scan_c2 = lw_prep.corrected_to_scan ({ 1000, 0 });
      point_t scan_c3 = lw_prep.corrected_to_scan ({ 1000, 1000 });
      point_t scan_c4 = lw_prep.corrected_to_scan ({ 0, 1000 });

      if (!lw.precompute (img_center, scan_c1, scan_c2, scan_c3, scan_c4))
	return false;
      lw.precompute_inverse ();

      /* Verify center is fixed.  */
      point_t c_scan = lw.corrected_to_scan (img_center);
      if (!img_center.almost_eq (c_scan, 1e-6))
        {
          printf ("FAILED: %s center should be fixed point!\n", tc.name);
          ok = false;
        }

      /* Verify round-trip accuracy on a grid.  */
      for (int y = 0; y <= 1000; y += 250)
        for (int x = 0; x <= 1000; x += 250)
          {
            point_t orig = { (coord_t) x, (coord_t) y };
            point_t scan = lw.corrected_to_scan (orig);
            point_t corrected = lw.scan_to_corrected (scan);
            if (!orig.almost_eq (corrected, 0.01))
              {
                printf ("FAILED: %s roundtrip mismatch at (%i,%i)\n", tc.name, x, y);
                ok = false;
              }
          }
    }

  /* Broken parameters: non-monotone.  */
  lens_warp_correction_parameters p2;
  p2.kr[0] = 1.0;
  p2.kr[1] = -1.0;
  p2.kr[2] = 0.0;
  p2.kr[3] = 0.0;
  /* Derivative is f(x) = 1 - 3x. For x > 1/3, f(x) < 0.  */
  if (p2.is_monotone ())
    {
      printf ("FAILED: is_monotone should be false for p2!\n");
      ok = false;
    }

  /* Adobe DNG Specification 1.3 Worked Example.
     Image 1000x1000, center (500,500), corner (0,0) defines r=1.0.
     Parameters: kr = [1.0, 0.05, -0.02, 0.005]
     Point (600, 700) -> Delta (100, 200), r^2 = (100^2+200^2)/(500^2+500^2) = 0.1.
     Distortion ratio f(0.1) = 1.0 + 0.05(0.1) - 0.02(0.01) + 0.005(0.001) = 1.004805.
     Expected point: (500 + 100*1.004805, 500 + 200*1.004805) = (600.4805, 700.961).  */
  {
    lens_warp_correction_parameters p_ref;
    p_ref.kr[0] = 1.0; p_ref.kr[1] = 0.05; p_ref.kr[2] = -0.02; p_ref.kr[3] = 0.005;
    p_ref.center = { 0.5, 0.5 };
    lens_warp_correction lw_ref;
    lw_ref.set_parameters (p_ref);
    if (!lw_ref.precompute ({ 500, 500 }, { 0, 0 }, { 1000, 0 }, { 1000, 1000 }, { 0, 1000 }))
      return false;
    
    point_t p_in = { 600, 700 };
    point_t p_out = lw_ref.corrected_to_scan (p_in);
    point_t p_expected = { 600.4805, 700.9610 };
    if (!p_out.almost_eq (p_expected, 1e-4))
      {
        printf ("FAILED: DNG Specification Worked Example mismatch!\n");
        printf ("Expected (%f, %f), got (%f, %f)\n", p_expected.x, p_expected.y, p_out.x, p_out.y);
        ok = false;
      }
  }

  return ok;
}
}


int
main ()
{
  printf ("1..23\n");

  test_matrix ();
  report ("matrix tests", true);
  test_color ();
  report ("color tests", true);
  report ("render linearity tests", test_render_linearity ());
  report ("screen blur tests", test_screen_blur ());
  report ("homography tests", test_homography (false, false, 0.000001));
  report ("lens warp tests", test_lens_warp ());
  report ("lens correction tests", test_homography (true, false, 0.15));
  report ("1d homography and lens correction tests", test_homography (true, true, 0.15));
  report ("screen discovery tests", test_discovery (1.8));
  report ("precomputed function tests", test_precomputed_function ());
  report ("histogram parallel tests", test_histogram_parallel ());
  report ("richards curve tests", test_richards_curve ());
  report ("richards symmetry tests", test_richards_symmetry ());
  report ("richards reversibility tests", test_richards_reversibility ());
  report ("richards functional inverse tests", test_richards_functional_inverse ());
  report ("hd reversibility tests", test_hd_reversibility ());
  report ("hd incremental update tests", test_hd_incremental_update ());
  report ("hd validity tests", test_hd_validity ());
  report ("hd sorting tests", test_hd_sorting ());
  report ("custom tone curve tests", test_custom_tone_curve ());
  report ("lru cache concurrency tests", test_lru_cache_concurrency ());
  report ("spectrum to xyz tests", test_spectrum_dyes_to_xyz ());
  report ("whitepoint consistency tests", test_whitepoint_constants ());
  return error_found;

}
