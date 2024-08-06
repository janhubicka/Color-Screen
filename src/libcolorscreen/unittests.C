#include <stdlib.h>
#include <math.h>
#include "include/matrix.h"
#include <assert.h>
#include "include/color.h"
#include "include/mesh.h"

#include "include/imagedata.h"
#include "include/colorscreen.h"
#include "include/scr-to-img.h"
using namespace colorscreen;
namespace {
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
  matrix2x2<double> m1 (1,2,
	        	3,4);
  matrix2x2<double> m2 (5,6,
			7,8);
  matrix<double, 2> mm = m1 * m2;
  assert (mm.m_elements[0][0]==19 && mm.m_elements[1][0]==22 && mm.m_elements[0][1]==43 && mm.m_elements[1][1]==50);
  mm = m1 * m1.invert ();
  assert (mm.m_elements[0][0]==1 && mm.m_elements[1][0]==0 && mm.m_elements[0][1]==0 && mm.m_elements[1][1]==1);
  matrix4x4<double> m;

  double xr, yr;
  for (int i = 0; i < 100; i++)
    {
      m.randomize ();
      double x = rand () % 100;
      double y = rand () % 100;
      m.perspective_transform (x,y,xr,yr);
      m.inverse_perspective_transform (xr,yr,xr,yr);
      if (fabs (x - xr) > 0.1 || fabs (y - yr) > 0.1)
	{
          printf ("%f %f %f %f %f %f\n",xr,yr,x,y, fabs (x-xr), fabs (y-yr));
	  abort ();
	}
    }

  matrix4x4<double> mm1 (2,0,0,1,
			 0,2,5,0,
			 2,0,2,0,
			 0,0,0,2);
  matrix4x4<double> mm2 = mm1.invert ();
  matrix<double,4> mm3 = mm1 * mm2;
  for (int i = 0; i <4; i++)
    for (int j = 0; j < 4; j++)
       if (fabs (mm3.m_elements[i][j]-(double)(i==j)) > 0.0001)
	  {
            mm3.print (stderr);
	    abort ();
	  }
  matrix3x3<double> mm4 (2,0,0,
			 0,2,5,
			 2,0,2);
  matrix3x3<double> mm5 = mm4.invert ();
  matrix<double,3> mm6 = mm4 * mm5;
  for (int i = 0; i <3; i++)
    for (int j = 0; j < 3; j++)
       if (fabs (mm6.m_elements[i][j]-(double)(i==j)) > 0.0001)
	  {
            mm6.print (stderr);
	    abort ();
	  }
}
void
test_color ()
{
  xyz white = xyz::from_srgb (1, 1, 1);
  assert (fabs (white.x - 0.9505) < 0.0001 && fabs (white.y - 1) < 0.0001 && fabs (white.z - 0.9505) < 1.0888);
  luminosity_t r,g,b;
  xyz_to_srgb (0.25, 0.40, 0.1, &r, &g, &b);
  assert (fabs (r - 0.4174) < 0.0001 && fabs (g - 0.7434) < 0.0001 && fabs (b - 0.2152) < 1.0888);
}

bool
do_test_homography (scr_to_img_parameters &param, int width, int height, bool lens_correction, coord_t epsilon)
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
	point_t img = {(coord_t)x, (coord_t)y};
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
  coord_t chi = solver (&param2, img, sparam);
  scr_to_img map2;
  map2.set_parameters (param2, img);
  coord_t sum = 0, maxv = 0;
  for (int y = 0; y < height; y += 5)
    for (int x = 0; x < width; x += 5)
      {
	point_t img = {(coord_t)x, (coord_t)y};
	point_t scr1 = map.to_scr (img);
	point_t scr2 = map2.to_scr (img);
	coord_t dist = scr1.dist_from (scr2);
	sum += dist;
	if (maxv < dist)
	  maxv = dist;
      }
  coord_t avg = sum / (width * (coord_t)height);
  printf ("\nHomography test with scanner %s: average distance %f, max %f, chi %f\n",scanner_type_names [(int)param.scanner_type], avg, maxv, chi);
  printf ("Coordinate1 original: %f,%f solved: %f,%f dist:%f \n", param.coordinate1.x, param.coordinate1.y, param2.coordinate1.x, param2.coordinate1.y, param.coordinate1.dist_from (param2.coordinate1));
  printf ("Coordinate2 original: %f,%f solved: %f,%f dist:%f \n", param.coordinate2.x, param.coordinate2.y, param2.coordinate2.x, param2.coordinate2.y, param.coordinate2.dist_from (param2.coordinate2));
  printf ("tilts original: %f,%f solved: %f,%f\n", param.tilt_x, param.tilt_y, param2.tilt_x, param2.tilt_y);
  if (lens_correction)
    {
      printf ("Lens center original: %f,%f solved: %f,%f dist:%f\n", param.lens_correction.center.x, param.lens_correction.center.y, param2.lens_correction.center.x, param2.lens_correction.center.y,param.lens_correction.center.dist_from (param2.lens_correction.center));
      printf ("Lens correction coeeficients original: %f,%f,%f,%f solved: %f,%f,%f,%f\n",
		      param.lens_correction.kr[0],
		      param.lens_correction.kr[1],
		      param.lens_correction.kr[2],
		      param.lens_correction.kr[3],
		      param2.lens_correction.kr[0],
		      param2.lens_correction.kr[1],
		      param2.lens_correction.kr[2],
		      param2.lens_correction.kr[3]);
    }
  bool ok = maxv < epsilon;
  if (!ok)
    {
      printf ("\nInput:\n");
      save_csp (stdout, &param, NULL, NULL, &sparam);
      printf ("\nSolution:\n");
      save_csp (stdout, &param2, NULL, NULL, NULL);
    }
  return ok;
}
bool
test_homography (bool lens_correction, coord_t epsilon)
{
  scr_to_img_parameters param;
  bool ok = true;
  param.center = {(coord_t)300, (coord_t)300};
  param.coordinate1 = {(coord_t)5, (coord_t)1.2};
  param.coordinate2 = {(coord_t)-1.4, (coord_t)5.2};
  param.tilt_x = 0.0001;
  param.tilt_y = 0.00001;
  if (lens_correction)
    {
      param.lens_correction.center = {0.4,0.6};
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

int testnum = 0;
void
report (const char *name, bool ok)
{
  testnum++;
  printf ("%sok %i - %s\n", ok ? "" : "not ", testnum, name);
  fflush (stdout);
}
}

int
main()
{
  printf ("1..4\n");
  test_matrix ();
  report ("matrix tests", true);
  test_color ();
  report ("color tests", true);
  report ("homography tests", test_homography (false, 0.000001));
  report ("lens correction tests", test_homography (true, 0.022));
  return 0;
}
