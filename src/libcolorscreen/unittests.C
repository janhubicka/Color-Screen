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
#include "deconvolve.h"
#include "denoise.h"
#include "include/paget.h"
#include "include/dufaycolor.h"
#include "demosaic.h"


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
      point_t p = m.perspective_transform ({ (coord_t) x, (coord_t) y });
      p = m.inverse_perspective_transform (p);
      xr = p.x; yr = p.y;
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
  if (!map.set_parameters (param, img) || !map2.set_parameters (param2, img))
    {
      printf ("set-parameters failed\n");
      return false;
    }
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
      if (!save_csp (stdout, &param, NULL, NULL, sparam))
	{
	  /* Ignore failure.  */
	}
      printf ("\nSolution:\n");
      if (!save_csp (stdout, &param2, NULL, NULL, NULL))
	{
	  /* Ignore failure.  */
	}
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
  if (!img.set_dimensions (width, height))
    return false;
  if (!map.set_parameters (param, img))
    {
      printf ("Set parameters failed\n");
      return false;
    }
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
      param.lens_correction.kr[3] = 0.01;
      assert (param.lens_correction.normalize ());
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
  if (!render_screen (img, param, rparam, dparam, width, height))
    return false;
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
  param.lens_correction.kr[3] = 0.01;
  assert (param.lens_correction.normalize ());
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
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.001))
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
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta, 0.001))
        {
	  fprintf (stderr, "FFT mtffilter blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
    }
  return true;
}
bool
test_screen_sharpening ()
{
  std::unique_ptr <screen> scr (new screen);
  std::unique_ptr <screen> mstr (new screen);
  mstr->initialize (Paget);

  sharpen_parameters sp;
  sp.scanner_mtf.f_stop = 8;
  sp.scanner_mtf.wavelength = 750;
  /* Pixel pitch 3.7/10 micrometers as requested.  */
  sp.scanner_mtf.pixel_pitch = 3.7  /*/ 10.0*/;
  sp.scanner_mtf.scan_dpi = 4000;
  sp.scanner_mtf_scale = 0.01;
  //sp.scanner_snr = 2000;
  
  sharpen_parameters *par[3] = {&sp, &sp, &sp};

  for (int m = 0; m < 3; m++)
    {
      if (m == 0)
	{
	  sp.mode = sharpen_parameters::wiener_deconvolution;
	  sp.richardson_lucy_iterations = 0;
	}
      else if (m == 1)
	{
	  sp.mode = sharpen_parameters::richardson_lucy_deconvolution;
	  sp.richardson_lucy_iterations = 5;
	}
      else
	{
	  sp.mode = sharpen_parameters::blur_deconvolution;
	  sp.richardson_lucy_iterations = 0;
	}

      for (int i = 0; i <= 100; i+=5)
	{
	  double defocus = 12.0*i/100.0; // 0 to 2mm in 5 steps
	  sp.scanner_mtf.defocus = defocus;
	  scr->initialize_with_sharpen_parameters (*mstr, par, m != 2, true);

	  /* Disable debug tiffs */
	  if (0)
	    {
	      char buf[256];
	      sprintf (buf, "/tmp/scr-sharpen-%s-defocus-%.1f.tif", 
		       m == 0 ? "wiener" : m == 1 ? "richardson-lucy" : "blur", defocus);
	      scr->save_tiff (buf);
	    }
	  rgbdata rgbdelta;
	  if (!scr->sum_almost_equal_p (*mstr, &rgbdelta, 0.001))
	    {
	      fprintf (stderr, "MTF %s defocus %f delta %f %f %f (step %i); see /tmp/scr-mtf.tif \n", m == 0 ? "wiener" : m == 1 ? "richardson-lucy" : "blur", defocus, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	      scr->save_tiff ("/tmp/scr-mtf.tif");
	      std::unique_ptr <screen> diff (new screen);
	      for (int y = 0; y < screen::size; y++)
	       for (int x = 0; x < screen::size; x++)
		 for (int c = 0; c < 3; c++)
		    diff->mult[y][x][c] = 0.5 + (scr->mult[y][x][c] - mstr->mult[y][x][c]);
	      diff->save_tiff ("/tmp/scr-diff.tif");
	      return false;
	    }
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
  if (!img.set_dimensions (65536, 1, true, false))
    return false;
  for (int i = 0; i < 65536; i++)
    img.put_rgb_pixel (i, 0, {(image_data::gray)i, (image_data::gray)i, (image_data::gray)i});
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
	  if (fabs (linear - ren.get_data_red ({(int)i, 0})) > 1.0/655350)
	    {
	      printf ("Bad linearization of %i: %f should be %f\n", i, linear, ren.get_data_red ({(int)i, 0}));
	      ok = false;
	    }
	  /* Now out_lookup_table is applied.  */
	  int_rgbdata out_int = ren.out_color.final_color ({ren.get_data_red ({(int)i,0}), ren.get_data_green ({(int)i,0}), ren.get_data_blue ({(int)i,0})});
	  r = out_int.red; g = out_int.green; b = out_int.blue;
	  if (i > mins[gamma_idx] && (r != i || g != i || b != i))
	    {
	      printf ("Render is non-linear at gamma %f linear: %i becomes %i %i %i (with table)\n",
		      gamma, i, r, g, b);
	      ok = false;
	    }
	  luminosity_t hr,hg,hb;
	  rgbdata out_hdr = ren.out_color.hdr_final_color ({ren.get_data_red ({(int)i,0}), ren.get_data_green ({(int)i,0}), ren.get_data_blue ({(int)i,0})});
	  hr = out_hdr.red; hg = out_hdr.green; hb = out_hdr.blue;
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
  dyes.set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
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
    //{ "Nikon Coolscan 9000ED", { 0.99508, 0.0245411, -0.0521967, 0.0325757 }, { 0.560586, 0.482547 } },
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
      if (!lw.precompute_inverse ())
	return false;

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

/* Test the simulated photographic darkroom process.
   This verifies the symmetry of the 'apply' and 'unapply' functions
   in film_sensitivity, modeling the chain from scanned transmittance
   to print transmittance and back.  */
bool
test_darkroom ()
{
  bool ok = true;
  luminosity_t xs[] = { 1.5, 2.0, 2.5, 3.0, 3.5 };
  luminosity_t ys[] = { 0.1, 0.5, 1.5, 2.0, 2.2 };
  hd_curve paper (xs, ys, 5);

  /* Preflash=0.5, Exposure=100, Boost=1.2.  */
  film_sensitivity sens (&paper, 0.5, 100.0, 1.2);
  sens.precompute ();

  /* Test symmetry for values within the paper's dynamic range.  */
  luminosity_t test_vals[] = { 0.4, 0.6, 0.8 };
  for (luminosity_t v : test_vals)
    {
      luminosity_t t = sens.apply (v);
      luminosity_t v_inv = sens.unapply (t);
      if (fabs (v - v_inv) > 1e-4)
        {
          printf ("FAILED: Darkroom symmetry mismatch for V=%f! Expected %f, got %f\n",
                  v, v, v_inv);
          ok = false;
        }
    }

  /* Test that preflash affects the output (fog lifting).
     Preflash adds exposure, which increases density and DECREASES transmittance.  */
  film_sensitivity sens_no_pre (&paper, 0.0, 100.0, 1.2);
  sens_no_pre.precompute ();
  if (sens.apply (0.5) >= sens_no_pre.apply (0.5))
    {
      printf ("FAILED: Preflash should increase density (decrease transmittance)!\n");
      ok = false;
    }

  return ok;
}
static bool
test_get_src_range ()
{
  bool ok = true;
  /* Create a simple 2x2 cell mesh (3x3 points).  */
  int width = 3;
  int height = 3;
  coord_t xshift = 0;
  coord_t yshift = 0;
  coord_t xstep = 10.0;
  coord_t ystep = 10.0;

  std::unique_ptr<mesh> m (new mesh (xshift, yshift, xstep, ystep, width, height));
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      m->set_point ({(int64_t)x, (int64_t)y}, {(coord_t)(x * xstep), (coord_t)(y * ystep)});

  /* Input area in source coordinates: [5, 15] x [5, 15].
     This covers parts of all 4 cells.
     The corners of the area are at (5,5), (15,5), (5,15), (15,15).
     The target coordinates should be the same as source for this mesh.
     So result range should be [5, 15] x [5, 15].  */
  image_area area_in (5.0, 5.0, 10.0, 10.0);
  image_area result = m->get_src_range (area_in);

  if (fabs (result.x - 5.0) > 0.001 || fabs (result.y - 5.0) > 0.001
      || fabs (result.width - 10.0) > 0.001 || fabs (result.height - 10.0) > 0.001)
    {
      printf ("FAILED: get_src_range clipping failed: got [%f, %f] %fx%f, expected [5, 5] 10x10\n",
              result.x, result.y, result.width, result.height);
      ok = false;
    }

  /* Test with area entirely inside one cell.  */
  image_area area_in2 (2.0, 2.0, 1.0, 1.0);
  image_area result2 = m->get_src_range (area_in2);
  if (fabs (result2.x - 2.0) > 0.001 || fabs (result2.y - 2.0) > 0.001
      || fabs (result2.width - 1.0) > 0.001 || fabs (result2.height - 1.0) > 0.001)
    {
      printf ("FAILED: get_src_range inner clipping failed: got [%f, %f] %fx%f, expected [2, 2] 1x1\n",
              result2.x, result2.y, result2.width, result2.height);
      ok = false;
    }

  return ok;
}

static bool
test_mesh_inversion ()
{
  bool ok = true;
  /* Create a mesh with non-trivial warp.  */
  int width = 10;
  int height = 10;
  coord_t xshift = 0;
  coord_t yshift = 0;
  coord_t xstep = 10.0;
  coord_t ystep = 10.0;
  
  std::unique_ptr<mesh> m (new mesh(xshift, yshift, xstep, ystep, width, height));
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
         /* Add some non-linear distortion. */
         coord_t target_x = x * xstep + std::sin(y * 0.5) * 1.0;
         coord_t target_y = y * ystep + std::cos(x * 0.5) * 1.0;
         m->set_point({(int64_t)x, (int64_t)y}, {target_x, target_y});
      }
      
  /* Precompute inverse to use m->invert(ip).  */
  m->precompute_inverse();
  
  /* Get the cached inverse mesh covering the bounding box. */
  std::shared_ptr<mesh> inv_m = m->compute_inverse();
  
  /* Test inversion precision by verifying roundtrip. */
  for (int y = 1; y < height - 2; y++)
    for (int x = 1; x < width - 2; x++)
      {
         point_t src = {(coord_t)(x * xstep + xstep / 2.0), (coord_t)(y * ystep + ystep / 2.0)};
         point_t target = m->apply(src);
         point_t expected_src = m->invert(target);
         point_t recovered_src = inv_m->apply(target);
         
         if (src.dist_from(expected_src) > 0.05)
           {
             printf("FAILED: m->invert error too large at %f, %f: recovered %f, %f\n", 
                 src.x, src.y, expected_src.x, expected_src.y);
             ok = false;
           }

         if (src.dist_from(recovered_src) > 1.0)
           {
             printf("FAILED: inv_m->apply roundtrip error too large at %f, %f: recovered %f, %f\n", 
                 src.x, src.y, recovered_src.x, recovered_src.y);
             ok = false;
           }
      }
      
  /* Test optional area caching. */
  int_optional_image_area area;
  area.set = true;
  area.x = 20;
  area.y = 20;
  area.width = 40;
  area.height = 40;
  
  std::shared_ptr<mesh> inv_m_area = m->compute_inverse(area);
  
  point_t target = {40, 40};
  point_t recovered_src_area = inv_m_area->apply(target);
  point_t expected_src = m->invert(target);
  
  if (recovered_src_area.dist_from(expected_src) > 0.5)
    {
      printf("FAILED: Mesh inversion area mismatch at %f, %f: expected %f, %f, got %f, %f\n", 
          target.x, target.y, expected_src.x, expected_src.y, recovered_src_area.x, recovered_src_area.y);
      ok = false;
    }
  
  return ok;
}
bool
test_cow_points ()
{
  solver_parameters sp1;
  sp1.add_point ({1, 1}, {2, 2}, solver_parameters::red);

  solver_parameters sp2 = sp1;
  /* They should share the same data.  */
  if (sp1.points.raw_data () != sp2.points.raw_data ())
    {
      printf ("FAILED: sp1 and sp2 do not share points array after copy\n");
      return false;
    }

  /* Modifying sp2 should trigger COW.  */
  sp2.add_point ({3, 3}, {4, 4}, solver_parameters::blue);
  if (sp1.points.raw_data () == sp2.points.raw_data ())
    {
      printf ("FAILED: sp1 and sp2 still share points array after modification\n");
      return false;
    }

  if (sp1.n_points () != 1 || sp2.n_points () != 2)
    {
      printf ("FAILED: points count mismatch after COW\n");
      return false;
    }

  return true;
}
bool
test_image_area ()
{
  bool ok = true;
  /* Test int_image_area (exclusive).  */
  int_image_area ia_int (0, 0, 1, 1); // [0, 1) x [0, 1)
  if (ia_int.empty_p ())
    {
      printf ("FAILED: int_image_area incorrectly reported as empty\n");
      ok = false;
    }
  if (!ia_int.contains_p (int_point_t{0, 0}))
    {
      printf ("FAILED: int_image_area does not contain its origin\n");
      ok = false;
    }
  if (ia_int.contains_p (int_point_t{1, 0}))
    {
      printf ("FAILED: int_image_area incorrectly contains exclusive upper bound\n");
      ok = false;
    }

  /* Test image_area (inclusive).  */
  image_area ia_fp (0.0, 0.0, 1.0, 1.0); // [0, 1] x [0, 1]
  if (ia_fp.empty_p ())
    {
      printf ("FAILED: image_area incorrectly reported as empty\n");
      ok = false;
    }
  if (!ia_fp.contains_p (point_t{0.0, 0.0}))
    {
      printf ("FAILED: image_area does not contain its origin\n");
      ok = false;
    }
  if (!ia_fp.contains_p (point_t{1.0, 1.0}))
    {
      printf ("FAILED: image_area does not contain inclusive upper bound\n");
      ok = false;
    }

  /* Test conversion and rounding.  */
  image_area fp_area (0.1, 0.2, 0.9, 0.8); // [0.1, 1.0] x [0.2, 1.0]
  int_image_area int_area (fp_area);
  if (int_area.x != 0 || int_area.y != 0 || int_area.width != 2 || int_area.height != 2)
    {
      printf ("FAILED: conversion from image_area to int_image_area failed rounding requirements\n");
      ok = false;
    }

  return ok;
}

static luminosity_t get_test_gray(image_data *img, int_point_t p, int, void *)
{
  return (luminosity_t)img->get_pixel(p.x, p.y) / 65535.0f;
}

static bool test_slanted_edge_mtf()
{
  bool verbose = false;
  if (verbose)
    printf("Testing slanted edge MTF (realistic anti-aliased model)...\n");
  for (int disp = 0 ; disp < 10; disp++)
    {
      int w = 128, h = 128;
      int scale = 16;
      int w_hi = w * scale, h_hi = h * scale;
      
      image_data img_hi;
      if (!img_hi.set_dimensions(w_hi, h_hi, false, true))
	return false;
      img_hi.maxval = 65535;
      
      double angle = 5.0 * M_PI / 180.0;
      double cos_a = std::cos(angle);
      double sin_a = std::sin(angle);
      for (int y = 0; y < h_hi; y++)
	for (int x = 0; x < w_hi; x++)
	  {
	    double d = (x - w_hi/2.0) * cos_a + (y - h_hi/2.0) * sin_a;
	    img_hi.put_pixel(x, y, d > 0 ? 10000 : 5000);
	  }
	  
      // Setup blur parameters for 16x resolution
      sharpen_parameters sp_hi;
      sp_hi.mode = sharpen_parameters::blur_deconvolution;
      sp_hi.scanner_mtf.f_stop = 8;
      sp_hi.scanner_mtf.scan_dpi = 4000; 
      sp_hi.scanner_mtf.defocus = 0.01 * disp; // 10 microns displacement
      sp_hi.scanner_mtf.pixel_pitch = 3.76;
      sp_hi.scanner_mtf.wavelength = 750; // IR lifht
      sp_hi.scanner_mtf_scale = scale;
      sp_hi.supersample = 1;

      std::vector<float> blurred_hi(w_hi * h_hi);
      
      if (!deconvolve<luminosity_t, float, image_data *, void *, get_test_gray, float>(
	    blurred_hi.data(), &img_hi, nullptr, w_hi, h_hi, sp_hi, nullptr, true))
	{
	  printf("Blurring failed\n");
	  return false;
	}

      image_data blurred;
      if (!blurred.set_dimensions(w, h, true, true))
	return false;
      blurred.maxval = 65535;
      
      // Downscale by averaging 16x16 blocks
      for (int y = 0; y < h; y++)
	for (int x = 0; x < w; x++)
	  {
	    double sum = 0;
	    for (int dy = 0; dy < scale; dy++)
	      for (int dx = 0; dx < scale; dx++)
		sum += blurred_hi[(y * scale + dy) * w_hi + (x * scale + dx)];
	    uint16_t val = (uint16_t)std::clamp(sum / (scale * scale) * 65535.0, 0.0, 65535.0);
	    blurred.put_pixel(x, y, val);
	    blurred.put_rgb_pixel(x, y, {val, val, val});
	  }
	
      // Analyze edge
      render_parameters rparam;
      rparam.gamma = 1.0;
      
      slanted_edge_parameters params;
      slanted_edge_results res = slanted_edge_mtf(rparam, blurred, blurred.get_area(), params, nullptr);
      
      if (!res.success)
	{
	  printf("Slanted edge detection failed\n");
	  return false;
	}
	
      if (verbose)
        printf("Edge found: (%f,%f) - (%f,%f)\n", (double)res.edge_p1.x, (double)res.edge_p1.y, (double)res.edge_p2.x, (double)res.edge_p2.y);
      
      // Verify MTF
      if (rparam.sharpen.scanner_mtf.measurements.empty())
	{
	  printf("No MTF measurement generated\n");
	  return false;
	}
	
      auto &measurement = rparam.sharpen.scanner_mtf.measurements[0];
      if (verbose)
        printf("MTF size: %zu\n", measurement.size());
      
      if (measurement.size() < 10)
	{
	  printf("MTF too small\n");
	  return false;
	}

      mtf m (sp_hi.scanner_mtf);
      if (! m.precompute (NULL))
	{
	  printf("MTF precomputation\n");
	  return false;
	}
	
      bool ok = true;
      for (size_t i = 0; i < measurement.size () && ok; i++)
	if (fabs (measurement.get_contrast (i) - m.get_mtf (measurement.get_freq (i)) * 100) > 5)
	  ok = false;
      if (!ok)
	{
	  for (size_t i = 0; i < measurement.size (); i++)
	     printf ("Freq %.3f %f.1%% should be %f.1%%, diff %f.1%%\n", measurement.get_freq (i),
		     measurement.get_contrast (i), m.get_mtf (measurement.get_freq (i)) * 100,
		     fabs (measurement.get_contrast (i) - m.get_mtf (measurement.get_freq (i)) * 100));
	  blurred.save_tiff("slanted_edge_test_realistic.tif");
	  printf("Saved test image to slanted_edge_test_realistic.tif\n");
	  return false;
	}
#if 0
      // Check some MTF values (should be in percentage 0..100)
      printf("Freq 0.1: %f%% should be %f%%, Freq 0.4: %f%% should be %f%%\n", 
	     (double)measurement.get_contrast(measurement.size() / 10), 
	     (double)m.get_mtf (measurement.get_freq (measurement.size() / 10)) * 100.0,
	     (double)measurement.get_contrast(4 * measurement.size() / 10),
	     (double)m.get_mtf (measurement.get_freq (4 * measurement.size() / 10)) * 100.0);
#endif
	     
      if (measurement.get_contrast(measurement.size() / 10) < measurement.get_contrast(4 * measurement.size() / 10))
	{
	  printf("MTF is not decreasing!\n");
	  return false;
	}

      if (measurement.get_contrast(1) < 50.0)
	{
	  printf("MTF values seem too low (expecting percentages 0..100)\n");
	  return false;
	}
    }
    
  return true;
}

static bool
test_denoise ()
{
  const int width = 64;
  const int height = 64;
  std::vector<float> original (width * height);
  std::vector<float> noisy (width * height);

  unsigned int seed = 123;
  /* Create a simple gradient image.  */
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        float val = (float)x / width;
        original[y * width + x] = val;
        /* Add some noise.  */
        float noise = (float)(fast_rand16 (&seed) % 100) / 1000.0f;
        noisy[y * width + x] = val + noise;
      }

  auto get_float_pixel = [] (const std::vector<float> &data, int_point_t p, int width, void *) -> float
  {
    return data[p.y * width + p.x];
  };

  denoise_parameters::denoise_mode modes[] = {
    denoise_parameters::bilateral,
    denoise_parameters::nl_means,
    denoise_parameters::nl_fast
  };

  for (auto mode : modes)
    {
      std::vector<float> denoised (width * height);
      denoise_parameters params;
      params.mode = mode;
      params.strength = 0.1f;
      params.patch_radius = 1;
      params.search_radius = 3;
      params.bilateral_sigma_s = 2.0f;
      params.bilateral_sigma_r = 0.1f;

      if (!denoise<float, float, const std::vector<float> &, void *, get_float_pixel, float> (
              denoised.data (), noisy, NULL, width, height, params, NULL, false))
        return false;

      /* Calculate MSE for noisy and denoised images.  */
      double mse_noisy = 0;
      double mse_denoised = 0;
      for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
          {
            double diff_noisy = noisy[y * width + x] - original[y * width + x];
            double diff_denoised = denoised[y * width + x] - original[y * width + x];
            mse_noisy += diff_noisy * diff_noisy;
            mse_denoised += diff_denoised * diff_denoised;
          }

      if (mse_denoised >= mse_noisy)
        {
          printf ("Denoising mode %i failed to reduce MSE: noisy %f, denoised %f\n", (int)mode, mse_noisy, mse_denoised);
          return false;
        }
    }

  return true;
}
/* Unit test for Dufaycolor RCD demosaicing.  */
template <typename GEOMETRY>
class fake_analyze
{
public:
  int_image_area m_area;
  fake_analyze (int w, int h) : m_area ({ 0, 0, w, h }) {}

  int_image_area
  demosaiced_area () const
  {
    return m_area;
  }

  bool
  populate_demosaiced_data (std::vector<rgbdata> &data, render *r,
                            int_image_area area, progress_info *progress)
  {
    for (int y = 0; y < m_area.height; y++)
      for (int x = 0; x < m_area.width; x++)
        {
          int color = GEOMETRY::demosaic_entry_color (x, y);
          if (color != base_geometry::none)
            {
              int tx = x / 16;
              int ty = y / 16;
              /* Each 16x16 tile has a unique solid color.  */
              rgbdata tile_color;
              tile_color.red = (luminosity_t)((tx * 17) % 256) / 255.0;
              tile_color.green = (luminosity_t)((ty * 23) % 256) / 255.0;
              tile_color.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;

              if (color == base_geometry::red)
                data[y * m_area.width + x].red = tile_color.red;
              else if (color == base_geometry::green)
                data[y * m_area.width + x].green = tile_color.green;
              else if (color == base_geometry::blue)
                data[y * m_area.width + x].blue = tile_color.blue;
            }
        }
    return true;
  }
};

template <typename GEOMETRY, typename DEMOSAICER>
bool
test_demosaic_loop (fake_analyze<GEOMETRY> &fake, DEMOSAICER &demosaicer,
                    render_parameters::screen_demosaic_t alg, const char *alg_name)
{
  if (!demosaicer.demosaic (&fake, NULL, alg, denoise_parameters (), NULL))
    {
      printf ("Demosaic %s failed to run\n", alg_name);
      return false;
    }

  int w = fake.m_area.width;
  int h = fake.m_area.height;
  bool ok = true;
  /* Skip border tiles as some algorithms (RCD 4x4) have large kernels.  */
  for (int ty = 1; ty < h / 16 - 1; ty++)
    {
      for (int tx = 1; tx < w / 16 - 1; tx++)
        {
          rgbdata expected;
          expected.red = (luminosity_t)((tx * 17) % 256) / 255.0;
          expected.green = (luminosity_t)((ty * 23) % 256) / 255.0;
          expected.blue = (luminosity_t)(((tx + ty) * 11) % 256) / 255.0;

          /* Check the middle 8x8 of each 16x16 tile.  */
          for (int y = ty * 16 + 4; y < ty * 16 + 12; y++)
            {
              for (int x = tx * 16 + 4; x < tx * 16 + 12; x++)
                {
                  rgbdata actual = demosaicer.demosaiced_data (x, y);
                  /* Use a slightly larger tolerance (0.1) for sparse patterns like Dufay.  */
                  if (fabs (actual.red - expected.red) > 0.1
                      || fabs (actual.green - expected.green) > 0.1
                      || fabs (actual.blue - expected.blue) > 0.1)
                    {
                      printf ("Demosaic %s mismatch at (%i, %i): expected (%f, %f, %f), got (%f, %f, %f)\n",
                              alg_name, x, y, expected.red, expected.green, expected.blue,
                              actual.red, actual.green, actual.blue);
                      ok = false;
                      break;
                    }
                }
              if (!ok) break;
            }
          if (!ok) break;
        }
      if (!ok) break;
    }
  return ok;
}

bool
test_demosaic_paget ()
{
  int w = 256, h = 256;
  fake_analyze<paget_geometry> fake (w, h);
  demosaic_paget_base<fake_analyze<paget_geometry>> demosaicer;
  bool ok = true;

  if (!test_demosaic_loop (fake, demosaicer, render_parameters::hamilton_adams_demosaic, "Paget Hamilton-Adams"))
    ok = false;
  if (!test_demosaic_loop (fake, demosaicer, render_parameters::ahd_demosaic, "Paget AHD"))
    ok = false;
  if (!test_demosaic_loop (fake, demosaicer, render_parameters::amaze_demosaic, "Paget AMaZE"))
    ok = false;
  if (!test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Paget RCD"))
    ok = false;
  if (!test_demosaic_loop (fake, demosaicer, render_parameters::lmmse_demosaic, "Paget LMMSE"))
    ok = false;

  return ok;
}

bool
test_demosaic_dufay ()
{
  int w = 256, h = 256;
  fake_analyze<dufay_geometry> fake (w, h);
  demosaic_dufay_base<fake_analyze<dufay_geometry>> demosaicer;
  
  return test_demosaic_loop (fake, demosaicer, render_parameters::rcd_demosaic, "Dufay RCD");
}

bool
test_demosaic ()
{
  bool ok = true;
  if (!test_demosaic_paget ())
    ok = false;
  if (!test_demosaic_dufay ())
    ok = false;
  return ok;
}
}





int
main (int argc, char **argv)
{
  struct test_entry
  {
    const char *name;
    const char *description;
    bool (*func) ();
  };

  test_entry tests[] = {
    { "matrix", "matrix tests", [] () { test_matrix (); return true; } },
    { "color", "color tests", [] () { test_color (); return true; } },
    { "linearity", "render linearity tests", [] () { return (bool)test_render_linearity (); } },
    { "blur", "screen blur tests", [] () { return test_screen_blur (); } },
    { "sharpening", "screen sharpening tests", [] () { return test_screen_sharpening (); } },
    { "homography", "homography tests", [] () { return (bool)test_homography (false, false, 0.000001); } },
    { "warp", "lens warp tests", [] () { return test_lens_warp (); } },
    { "lens_correction", "lens correction tests", [] () { return (bool)test_homography (true, false, 0.15); } },
    { "1d_homography", "1d homography and lens correction tests", [] () { return (bool)test_homography (true, true, 0.15); } },
    { "discovery", "screen discovery tests", [] () { return (bool)test_discovery (1.8); } },
    { "precomputed", "precomputed function tests", [] () { return test_precomputed_function (); } },
    { "histogram", "histogram parallel tests", [] () { return test_histogram_parallel (); } },
    { "richards", "richards curve tests", [] () { return test_richards_curve (); } },
    { "richards_symmetry", "richards symmetry tests", [] () { return test_richards_symmetry (); } },
    { "richards_reversibility", "richards reversibility tests", [] () { return test_richards_reversibility (); } },
    { "richards_functional_inverse", "richards functional inverse tests", [] () { return test_richards_functional_inverse (); } },
    { "hd_reversibility", "hd reversibility tests", [] () { return test_hd_reversibility (); } },
    { "hd_incremental", "hd incremental update tests", [] () { return test_hd_incremental_update (); } },
    { "hd_validity", "hd validity tests", [] () { return test_hd_validity (); } },
    { "hd_sorting", "hd sorting tests", [] () { return test_hd_sorting (); } },
    { "tone_curve", "custom tone curve tests", [] () { return test_custom_tone_curve (); } },
    { "lru_cache", "lru cache concurrency tests", [] () { return test_lru_cache_concurrency (); } },
    { "spectrum", "spectrum to xyz tests", [] () { return test_spectrum_dyes_to_xyz (); } },
    { "whitepoint", "whitepoint consistency tests", [] () { return test_whitepoint_constants (); } },
    { "darkroom", "darkroom simulation tests", [] () { return test_darkroom (); } },
    { "mesh_src_range", "mesh get_src_range tests", [] () { return test_get_src_range (); } },
    { "mesh_inversion", "mesh inversion tests", [] () { return test_mesh_inversion (); } },
    { "cow_points", "cow points tests", [] () { return test_cow_points (); } },
    { "image_area", "image area tests", [] () { return test_image_area (); } },
    { "slanted_edge", "slanted edge MTF tests", [] () { return test_slanted_edge_mtf (); } },
    { "denoising", "denoising tests", [] () { return test_denoise (); } },
    { "demosaic", "dufay and paget demosaicing tests", [] () { return test_demosaic (); } },
    { NULL, NULL, NULL }
  };

  int num_to_run = 0;
  for (int i = 0; tests[i].name; i++)
    {
      bool run = (argc == 1);
      for (int j = 1; j < argc; j++)
        if (strcmp (argv[j], tests[i].name) == 0)
          run = true;
      if (run)
        num_to_run++;
    }

  printf ("1..%i\n", num_to_run);

  for (int i = 0; tests[i].name; i++)
    {
      bool run = (argc == 1);
      for (int j = 1; j < argc; j++)
        if (strcmp (argv[j], tests[i].name) == 0)
          run = true;
      if (run)
        report (tests[i].description, tests[i].func ());
    }

  return error_found;
}
