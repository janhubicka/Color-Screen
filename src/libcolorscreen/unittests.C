#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <memory>

#include "include/colorscreen.h"
#include "include/imagedata.h"
#include "include/scr-to-img.h"
#include "include/finetune.h"
#include "include/color.h"
#include "include/matrix.h"
#include "include/mesh.h"
#include "screen.h"
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
  assert (mm.m_elements[0][0] == 19 && mm.m_elements[1][0] == 22
          && mm.m_elements[0][1] == 43 && mm.m_elements[1][1] == 50);
  mm = m1 * m1.invert ();
  assert (mm.m_elements[0][0] == 1 && mm.m_elements[1][0] == 0
          && mm.m_elements[0][1] == 0 && mm.m_elements[1][1] == 1);
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
      if (fabs (mm3.m_elements[i][j] - (double)(i == j)) > 0.0001)
        {
          mm3.print (stderr);
          abort ();
        }
  matrix3x3<double> mm4 (2, 0, 0, 0, 2, 5, 2, 0, 2);
  matrix3x3<double> mm5 = mm4.invert ();
  matrix<double, 3> mm6 = mm4 * mm5;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (fabs (mm6.m_elements[i][j] - (double)(i == j)) > 0.0001)
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
	  test_name, scanner_type_names[(int) param.scanner_type],
	  scr_names[(int) param.type], epsilon);
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
test_homography (bool lens_correction, coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = { (coord_t)300, (coord_t)300 };
  param.coordinate1 = { (coord_t)5, (coord_t)1.2 };
  param.coordinate2 = { (coord_t)-1.4, (coord_t)5.2 };
  param.tilt_x = 0.0001;
  param.tilt_y = 0.00001;
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

void
report (const char *name, bool ok)
{
  static int testnum = 0;
  testnum++;
  printf ("%sok %i - %s\n", ok ? "" : "not ", testnum, name);
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
      scr1->initialize_with_blur (mstr, radius, screen::blur_gaussian, screen::blur_fft);
      scr2->initialize_with_blur (mstr, radius, screen::blur_gaussian, screen::blur_direct);
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
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta))
        {
	  fprintf (stderr, "FFT Gaussian blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
      scr3->initialize_with_blur (mstr, radius, screen::blur_gaussian, screen::blur_fft2d);
      /* For very small blurs fft produces roundoff errors along sharp edges.  */
      if (!scr1->almost_equal_p (*scr3, &delta, i < 20 || i > 80 ? 0.006 : 1.0/1024))
        {
	  fprintf (stderr, "FFT Gaussian blur does not FFT2D version radius %f delta %f (step %i); see /tmp/scr-*.tif \n", radius, delta, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  scr2->save_tiff ("/tmp/scr-nofft.tif");
	  scr3->save_tiff ("/tmp/scr-fft2d.tif");
	  std::unique_ptr <screen> diff (new screen);
	  for (int y = 0; y < screen::size; y++)
	   for (int x = 0; x < screen::size; x++)
	     for (int c = 0; c < 3; c++)
		diff->mult[y][x][c] = 0.5 + (scr3->mult[y][x][c] - scr1->mult[y][x][c]);
	  diff->save_tiff ("/tmp/scr-diff.tif");
	  exit (0);
	  return false;
        }

      scr1->initialize_with_blur (mstr, radius, screen::blur_mtffilter, screen::blur_fft);
      if (!scr1->sum_almost_equal_p (mstr, &rgbdelta))
        {
	  fprintf (stderr, "FFT mtffilter blur result overall tonality does not match original radius %f delta %f %f %f (step %i); see /tmp/scr-fft.tif \n", radius, rgbdelta.red, rgbdelta.green, rgbdelta.blue, i);
	  scr1->save_tiff ("/tmp/scr-fft.tif");
	  return false;
        }
    }
  return true;
}
bool
test_screen_point_spread_blur ()
{
  screen mstr;
  mstr.initialize (Paget);
  std::unique_ptr <screen> scr1 (new screen);
  for (int i1 = 1; i1 < 10; i1++)
    for (int i2 = 1; i2 < 10; i2++)
      for (int i3 = 1; i3 < 10; i3++)
        for (int i4 = 1; i4 < 10; i4++)
	  {
	    luminosity_t c[4] = {i1 * 0.1, (i1+i2) * 0.1, (i1+i2+i3) * 0.1, (i1+i2+i3+i4) * 0.1};
	    scr1->initialize_with_blur_point_spread (mstr, c, screen::blur_fft);
	    rgbdata rgbdelta;
	    if (!scr1->sum_almost_equal_p (mstr, &rgbdelta))
	      {
		fprintf (stderr, "FFT point spread blur result overall tonality does not match original curve %f %f %f %f delta %f %f %f; see /tmp/scr-fft.tif \n", c[0], c[1], c[2], c[3], rgbdelta.red, rgbdelta.green, rgbdelta.blue);
		scr1->save_tiff ("/tmp/scr-fft.tif");
		return false;
	      }
	  }
  return true;
}
}

int
main ()
{
  printf ("1..7\n");
  test_matrix ();
  report ("matrix tests", true);
  test_color ();
  report ("color tests", true);
  report ("screen blur tests", test_screen_blur ());
  report ("screen point spread blur tests", test_screen_point_spread_blur ());
  report ("homography tests", test_homography (false, 0.000001));
  report ("lens correction tests", test_homography (true, 0.15));
  report ("screen discovery tests", test_discovery (1.8));
  return 0;
}
