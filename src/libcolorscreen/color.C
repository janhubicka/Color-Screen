#include "lcms2.h"
#include "include/color.h"
#include "include/scr-to-img.h"
color_matrix
matrix_by_dye_xy (luminosity_t rx, luminosity_t ry,
		  luminosity_t gx, luminosity_t gy,
		  luminosity_t bx, luminosity_t by)
{
  xyz r = xyY_to_xyz (rx, ry, 0.2126 );
  xyz g = xyY_to_xyz (gx, gy, 0.7152 );
  xyz b = xyY_to_xyz (bx, by, 0.0722 );
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  xyz white = xyz::from_linear_srgb (1, 1, 1);
  m.normalize_grayscale (white.x, white.y, white.z);
  return m;
}

color_matrix
matrix_by_dye_xyY (xyY red, xyY green, xyY blue)
{
  xyz r = red;
  xyz g = green;
  xyz b = blue;
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  //xyz white;
  //srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
  //m.normalize_grayscale (white.x, white.y, white.z);
  return m;
}

cie_lab::cie_lab (xyz c)
{
  luminosity_t x, y, z;
  const luminosity_t refX = 0.95047, refY = 1, refZ = 1.08883;
  
  x = c.x / refX; y = c.y / refY; z = c.z / refZ;
  
  if (x > 0.008856)
    x = my_pow(x, (luminosity_t)(1 / 3.0));
  else x = (7.787 * x) + (16.0 / 116.0);
  
  if (y > 0.008856)
    y = my_pow(y, (luminosity_t)(1 / 3.0));
  else y = (7.787 * y) + (16.0 / 116.0);
  
  if (z > 0.008856)
    z = my_pow(z, (luminosity_t)(1 / 3.0));
  else z = (7.787 * z) + (16.0 / 116.0);
  
  l = 116 * y - 16;
  a = 500 * (x - y);
  b = 200 * (y - z);
}


/* Computatoin of DeltaE 2000
   Taken from ArgyllCMS.  */
static luminosity_t
deltaE2000_squared (cie_lab c1, cie_lab c2)
{
  double C1, C2;
  double h1, h2;
  double dL, dC, dH;
  double dsq;

  /* The trucated value of PI is needed to ensure that the */
  /* test cases pass, as one of them lies on the edge of */
  /* a mathematical discontinuity. The precision is still */
  /* enough for any practical use. */
#define RAD2DEG(xx) (180.0/M_PI * (xx))
#define DEG2RAD(xx) (M_PI/180.0 * (xx))

  /* Compute Cromanance and Hue angles */
  {
    double C1ab, C2ab;
    double Cab, Cab7, G;
    double a1, a2;

    C1ab = sqrt (c1.a * c1.a + c1.b * c1.b);
    C2ab = sqrt (c2.a * c2.a + c2.b * c2.b);
    Cab = 0.5 * (C1ab + C2ab);
    Cab7 = pow (Cab, 7.0);
    G = 0.5 * (1.0 - sqrt (Cab7 / (Cab7 + 6103515625.0)));
    a1 = (1.0 + G) * c1.a;
    a2 = (1.0 + G) * c2.a;
    C1 = sqrt (a1 * a1 + c1.b * c1.b);
    C2 = sqrt (a2 * a2 + c2.b * c2.b);

    if (C1 < 1e-9)
      h1 = 0.0;
    else
      {
	h1 = RAD2DEG (atan2 (c1.b, a1));
	if (h1 < 0.0)
	  h1 += 360.0;
      }

    if (C2 < 1e-9)
      h2 = 0.0;
    else
      {
	h2 = RAD2DEG (atan2 (c2.b, a2));
	if (h2 < 0.0)
	  h2 += 360.0;
      }
  }

  /* Compute delta L, C and H */
  {
    double dh;

    dL = c2.l - c1.l;
    dC = C2 - C1;
    if (C1 < 1e-9 || C2 < 1e-9)
      {
	dh = 0.0;
      }
    else
      {
	dh = h2 - h1;
	if (dh > 180.0)
	  dh -= 360.0;
	else if (dh < -180.0)
	  dh += 360.0;
      }

    dH = 2.0 * sqrt (C1 * C2) * sin (DEG2RAD (0.5 * dh));
  }

  {
    double L, C, h, T;
    double hh, ddeg;
    double C7, RC, L50sq, SL, SC, SH, RT;
    double dLsq, dCsq, dHsq, RCH;

    L = 0.5 * (c1.l + c2.l);
    C = 0.5 * (C1 + C2);

    if (C1 < 1e-9 || C2 < 1e-9)
      {
	h = h1 + h2;
      }
    else
      {
	h = h1 + h2;
	if (fabs (h1 - h2) > 180.0)
	  {
	    if (h < 360.0)
	      h += 360.0;
	    else if (h >= 360.0)
	      h -= 360.0;
	  }
	h *= 0.5;
      }

    T = 1.0 - 0.17 * cos (DEG2RAD (h - 30.0)) + 0.24 * cos (DEG2RAD (2.0 * h))
      + 0.32 * cos (DEG2RAD (3.0 * h + 6.0)) -
      0.2 * cos (DEG2RAD (4.0 * h - 63.0));
    L50sq = (L - 50.0) * (L - 50.0);

    SL = 1.0 + (0.015 * L50sq) / sqrt (20.0 + L50sq);
    SC = 1.0 + 0.045 * C;
    SH = 1.0 + 0.015 * C * T;

    dLsq = dL / SL;
    dCsq = dC / SC;
    dHsq = dH / SH;

    hh = (h - 275.0) / 25.0;
    ddeg = 30.0 * exp (-hh * hh);
    C7 = pow (C, 7.0);
    RC = 2.0 * sqrt (C7 / (C7 + 6103515625.0));
    RT = -sin (DEG2RAD (2 * ddeg)) * RC;

    RCH = RT * dCsq * dHsq;

    dLsq *= dLsq;
    dCsq *= dCsq;
    dHsq *= dHsq;

    dsq = dLsq + dCsq + dHsq + RCH;
  }

  return dsq;

#undef RAD2DEG
#undef DEG2RAD
}

luminosity_t 
deltaE(cie_lab c1, cie_lab c2)
{
  //printf ("LAB %f %f %f  -  %f %f %f\n",c1.l,c1.a,c1.b, c2.l,c2.a,c2.b);
  return scr_to_img::my_sqrt (deltaE2000_squared (c1, c2));
  //return scr_to_img::my_sqrt((c1.l - c2.l) * (c1.l - c2.l) + (c1.a - c2.a) * (c1.a - c2.a) + (c1.b - c2.b) * (c1.b - c2.b));
}
luminosity_t
deltaE(xyz c1, xyz c2)
{
  //printf ("XYZ %f %f %f  -  %f %f %f\n",c1.x,c1.y,c1.z, c2.x,c2.y,c2.z);
  cie_lab lc1 (c1);
  cie_lab lc2 (c2);
  return deltaE(lc1, lc2);
}
