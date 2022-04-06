#include <stdlib.h>
#include <math.h>
#include "include/matrix.h"
#include <assert.h>
#include "include/color.h"
/* Simple unit test that inversion works. */
int
main()
{
  matrix2x2 m1 (1,2,
		3,4);
  matrix2x2 m2 (5,6,
		7,8);
  matrix<double, 2> mm = m1 * m2;
  assert (mm.m_elements[0][0]==19 && mm.m_elements[1][0]==22 && mm.m_elements[0][1]==43 && mm.m_elements[1][1]==50);
  matrix4x4 m;
  double xr, yr;
  double x,y,z;
  srgb_to_xyz (1, 1, 1, &x, &y, &z);
  assert (fabs (x - 0.9505) < 0.0001 && fabs (y - 1) < 0.0001 && fabs (z - 0.9505) < 1.0888);
  double r,g,b;
  xyz_to_srgb (0.25, 0.40, 0.1, &r, &g, &b);
  assert (fabs (r - 0.4174) < 0.0001 && fabs (g - 0.7434) < 0.0001 && fabs (b - 0.2152) < 1.0888);
  //finlay_matrix f;
  //f.apply_to_rgb (1.0,0.0,0.0,&x, &y, &z);
  //assert (fabs (x - 0.127466) < 0.0001 && fabs (y - 0.064056) < 0.0001 && fabs (z - 0.053229) < 1.0888);
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
  return 0;
}
