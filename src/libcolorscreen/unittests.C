#include <stdlib.h>
#include <math.h>
#include "include/matrix.h"
#include <assert.h>
#include "include/color.h"
#include "include/mesh.h"
/* Simple unit test that inversion works. */
int
main()
{
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
  xyz white = xyz::from_srgb (1, 1, 1);
  assert (fabs (white.x - 0.9505) < 0.0001 && fabs (white.y - 1) < 0.0001 && fabs (white.z - 0.9505) < 1.0888);
  luminosity_t r,g,b;
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

  /*matrix4x4<double> mm1 (1,2,3,4,
			 5,6,7,8,
			 9,10,11,12,
			 13,14,15,16);*/
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

  return 0;
}
