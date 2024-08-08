#include "include/color.h"
#include "include/matrix.h"
#include "include/mesh.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "include/colorscreen.h"
#include "include/imagedata.h"
#include "include/scr-to-img.h"
#include "include/finetune.h"
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
compare_scr_to_img (const char *test_name, scr_to_img_parameters &param,
                    scr_to_img_parameters &param2, solver_parameters *sparam,
                    image_data &img, bool keep0, bool lens_correction,
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

  for (int y = 0; y < grid; y++)
    for (int x = 0; x < grid; x++)
      {
        int xmin = x * img.width / grid;
        int xmax = (x + 1) * img.width / grid;
        int ymin = y * img.height / grid;
        int ymax = (y + 1) * img.height / grid;
        coord_t sum = 0, maxv = 0;
        point_t offset = { 0, 0 };
        point_t offavg = { 0, 0 };
        if (!keep0)
          {
            point_t imgp
                = { (coord_t)(img.width / 2), (coord_t)(img.height / 2) };
            point_t scr1 = map.to_scr (imgp);
            point_t scr2 = map2.to_scr (imgp);
            int_point_t int_offset = (scr1 - scr2).nearest ();
            offset.x = int_offset.x;
            offset.y = int_offset.y;
          }
        for (int y = ymin; y < ymax; y += 5)
          for (int x = xmin; x < xmax; x += 5)
            {
              point_t img = { (coord_t)x, (coord_t)y };
              point_t scr1 = map.to_scr (img);
              point_t scr2 = map2.to_scr (img) + offset;
              coord_t dist = scr1.dist_from (scr2);
              offavg += scr2 - scr1;
              sum += dist;
              if (maxv < dist)
                maxv = dist;
            }
        coord_t scale = 1 / ((xmax - xmin - 1) * (coord_t)(ymax - ymin - 1));
        offavg *= scale;
        data[y][x].avg = sum * scale;
        data[y][x].max = maxv;
        data[y][x].avgoffset = offavg;
      }

  bool ok = true;
  printf ("\n%s test with scanner %s, process %s and tolerance %f\naverage "
          "distances:",
          test_name, scanner_type_names[(int)param.scanner_type],
          scr_names[(int)param.type], epsilon);
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
        {
          printf (" %5.4f", data[y][x].avg);
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
          printf (" %5.4f", data[y][x].max);
          if (data[y][x].max > epsilon)
            ok = false;
        }
    }
  printf ("\noffsets:");
  for (int y = 0; y < grid; y++)
    {
      printf ("\n  ");
      for (int x = 0; x < grid; x++)
        {
          printf (" %+5.4f,%+5.4f", data[y][x].avgoffset.x,
                  data[y][x].avgoffset.y);
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
              param2.lens_correction.center.x, param2.lens_correction.center.y,
              param.lens_correction.center.dist_from (
                  param2.lens_correction.center));
      printf ("Lens correction coeeficients original: %f,%f,%f,%f solved: "
              "%f,%f,%f,%f\n",
              param.lens_correction.kr[0], param.lens_correction.kr[1],
              param.lens_correction.kr[2], param.lens_correction.kr[3],
              param2.lens_correction.kr[0], param2.lens_correction.kr[1],
              param2.lens_correction.kr[2], param2.lens_correction.kr[3]);
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
do_test_discovery (scr_to_img_parameters &param, int width, int height)
{
  image_data img;
  solver_parameters sparam;
  scr_detect_parameters dparam;
  render_parameters rparam;
  rparam.gamma = 1.0;
  rparam.screen_blur_radius = 1;
  render_screen (img, param, rparam, dparam, width, height);
  detect_regular_screen_params dsparams;


  dsparams.scanner_type = fixed_lens;
  dsparams.gamma = 1;
  auto detected
      = detect_regular_screen (img, dparam, sparam, &dsparams, NULL, NULL);
  if (!detected.success)
    return false;
  bool ok = true;
  param.type = Dufay;
  ok &= compare_scr_to_img ("Screen discovery", param, detected.param, &sparam,
                            img, false, false, 1);
  param.type = Paget;
  ok &= compare_scr_to_img ("Screen discovery", param, detected.param, &sparam,
                            img, false, false, 1);
  return ok;
}

int testnum = 0;
void
report (const char *name, bool ok)
{
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
#if 0
  param.tilt_x = 0.0001;
  param.tilt_y = 0.00001;
  param.lens_correction.center = {0.3,0.7};
  param.lens_correction.kr[1] = 0.03;
  param.lens_correction.kr[2] = -0.02;
  param.lens_correction.kr[3] = -0.01;
  param.lens_correction.normalize ();
#endif
  param.type = Finlay;
  param.scanner_type = fixed_lens;
  ok &= do_test_discovery (param, 1024, 1024);
  return ok;
}
}

int
main ()
{
  printf ("1..5\n");
  test_matrix ();
  report ("matrix tests", true);
  test_color ();
  report ("color tests", true);
  report ("homography tests", test_homography (false, 0.000001));
  report ("lens correction tests", test_homography (true, 0.030));
  report ("screen discovery tests", test_discovery (0.030));
  return 0;
}
