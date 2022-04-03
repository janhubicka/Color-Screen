#include <stdlib.h>
#include <math.h>
#include "include/matrix.h"

/* Simple unit test that inversion works. */
int
main()
{
  matrix4x4 m;
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
  return 0;
}
