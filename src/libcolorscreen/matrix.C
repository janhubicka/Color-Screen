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
  matrix4x4<double> m;
  double xr, yr;
  luminosity_t x,y,z;
  srgb_to_xyz (1, 1, 1, &x, &y, &z);
  assert (fabs (x - 0.9505) < 0.0001 && fabs (y - 1) < 0.0001 && fabs (z - 0.9505) < 1.0888);
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

  {
#if 1
    mesh_point z = {5, 5};
    mesh_point x = {11,6};
    mesh_point y = {20, 20};
    mesh_point a = {0.6, 0.3};
#else
    mesh_point z = {0, 0};
    mesh_point x = {1,0};
    mesh_point y = {1, 1};
    mesh_point a = {0.6, 0.3};
#endif
    //mesh_point a = {0.5, 0};
    mesh_point imga = triangle_interpolate (z, x, y, a);
    mesh_point invimga = inverse_triangle_interpolate (z, x, y, imga);
    if (fabs (invimga.x - a.x) > 0.0001 || fabs (invimga.y - a.y) > 0.0001)
      {
	printf ("Wrong inverse. Point %f %f maps to %f %f inversed %f %f\n",
	        a.x, a.y, imga.x, imga.y, invimga.x, invimga.y);
        abort ();
      }
  }
  {
    mesh_point tl = {1,1};
    mesh_point tr = {2,2};
    mesh_point bl = {1,2};
    mesh_point br = {3,5};
    for (int y = 0; y < 5; y++)
    {
      for (int x = 0; x < 5; x++)
      {
	mesh_point p = {x / 4.0, y / 4.0};
	mesh_point p2 = mesh_interpolate (tl, tr, bl, br, p);
	//printf (" (%2.2f, %2.2f)->(%2.2f, %2.2f)",p.x,p.y, p2.x, p2.y);
	mesh_point pinv = mesh_inverse_interpolate (tl, tr, bl, br, p2);
	if (fabs (p.x - pinv.x) > 0.0001 || fabs (p.y - pinv.y) > 0.0001)
	  {
	     printf ("\n Inverts to %f %f\n",pinv.x, pinv.y);
	     abort ();
	  }
      }
      //printf ("\n");
    }
  }
  return 0;
}
