#include "lcms2.h"
#include "include/color.h"
#include "include/scr-to-img.h"
#include "include/spectrum-to-xyz.h"
namespace colorscreen
{
void
rgbdata::print (FILE *f)
{
  rgbdata c = (*this * (luminosity_t)255).clamp (0, 255);
  fprintf (f, "red:%f green:%f blue:%f #%02x%02x%02x\n", red, green, blue, (int)(c.red + 0.5), (int)(c.green + 0.5), (int)(c.blue + 0.5));
}
void xyz::print_sRGB (FILE *f, bool verbose)
{
  rgbdata c;
  to_srgb (&c.red,&c.green,&c.blue);
  c = c * (luminosity_t)255;
  if (verbose)
    fprintf (f, "sRGB r:%f g:%f b:%f " , c.red, c.green, c.blue);
  c = c.clamp (0, 255);
  fprintf (f, "#%02x%02x%02x", (int)(c.red + 0.5), (int)(c.green + 0.5), (int)(c.blue + 0.5));
}
void
xyz::print (FILE *f)
{
  fprintf (f, "x:%f y:%f z:%f ", x, y, z);
  print_sRGB (f, true);
  fprintf (f, "\n");
}

color_matrix
matrix_by_dye_xy (luminosity_t rx, luminosity_t ry,
		  luminosity_t gx, luminosity_t gy,
		  luminosity_t bx, luminosity_t by)
{
  xyz r = xyY_to_xyz (rx, ry, 1);
  xyz g = xyY_to_xyz (gx, gy, 1);
  xyz b = xyY_to_xyz (bx, by, 1);
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  xyz white = srgb_white;
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
  return m;
}

color_matrix
matrix_by_dye_xyz (xyz r, xyz g, xyz b)
{
  color_matrix m (r.x, g.x, b.x, 0,
		  r.y, g.y, b.y, 0,
		  r.z, g.z, b.z, 0,
		  0,   0,   0,   1);
  return m;
}

cie_lab::cie_lab (xyz c, xyz white)
{
  luminosity_t x, y, z;
  assert (white.y == 1);
  
  x = c.x / white.x;
  y = c.y / white.y;
  z = c.z / white.z;
  
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
  //c.print (stdout);
  //printf ("%f %f %f\n",l,a,b);
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

    C1ab = my_sqrt (c1.a * c1.a + c1.b * c1.b);
    C2ab = my_sqrt (c2.a * c2.a + c2.b * c2.b);
    Cab = 0.5 * (C1ab + C2ab);
    Cab7 = pow (Cab, 7.0);
    G = 0.5 * (1.0 - my_sqrt (Cab7 / (Cab7 + 6103515625.0)));
    a1 = (1.0 + G) * c1.a;
    a2 = (1.0 + G) * c2.a;
    C1 = my_sqrt (a1 * a1 + c1.b * c1.b);
    C2 = my_sqrt (a2 * a2 + c2.b * c2.b);

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

    dH = 2.0 * my_sqrt (C1 * C2) * sin (DEG2RAD (0.5 * dh));
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

    SL = 1.0 + (0.015 * L50sq) / my_sqrt (20.0 + L50sq);
    SC = 1.0 + 0.045 * C;
    SH = 1.0 + 0.015 * C * T;

    dLsq = dL / SL;
    dCsq = dC / SC;
    dHsq = dH / SH;

    hh = (h - 275.0) / 25.0;
    ddeg = 30.0 * exp (-hh * hh);
    C7 = pow (C, 7.0);
    RC = 2.0 * my_sqrt (C7 / (C7 + 6103515625.0));
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
  return my_sqrt((c1.l - c2.l) * (c1.l - c2.l) + (c1.a - c2.a) * (c1.a - c2.a) + (c1.b - c2.b) * (c1.b - c2.b));
}
luminosity_t 
deltaE2000(cie_lab c1, cie_lab c2)
{
  //printf ("LAB %f %f %f  -  %f %f %f\n",c1.l,c1.a,c1.b, c2.l,c2.a,c2.b);
  return my_sqrt (deltaE2000_squared (c1, c2));
  //return scr_to_img::my_sqrt((c1.l - c2.l) * (c1.l - c2.l) + (c1.a - c2.a) * (c1.a - c2.a) + (c1.b - c2.b) * (c1.b - c2.b));
}
luminosity_t
deltaE(xyz c1, xyz c2, xyz white)
{
  //printf ("XYZ %f %f %f  -  %f %f %f\n",c1.x,c1.y,c1.z, c2.x,c2.y,c2.z);
  cie_lab lc1 (c1, white);
  cie_lab lc2 (c2, white);
  return deltaE(lc1, lc2);
}
luminosity_t
deltaE2000(xyz c1, xyz c2, xyz white)
{
  //printf ("XYZ %f %f %f  -  %f %f %f\n",c1.x,c1.y,c1.z, c2.x,c2.y,c2.z);
  cie_lab lc1 (c1, white);
  cie_lab lc2 (c2, white);
  return deltaE2000(lc1, lc2);
}

/* Compute intersection vector (x1, y1)->(x2, y2) and line segment (x3,y3)-(x4,y4).
   Return true if it lies on the line sergment and in positive direction of the vector.
   If true is returned, t is initialized to position of the point in the line.  */
static bool
intersect (luminosity_t x1, luminosity_t y1, luminosity_t x2, luminosity_t y2,
	   luminosity_t x3, luminosity_t y3, luminosity_t x4, luminosity_t y4, luminosity_t *t)
{
  luminosity_t a = x1;
  luminosity_t b = x2-x1;
  luminosity_t c = x3;
  luminosity_t d = x4-x3;
  luminosity_t e = y1;
  luminosity_t f = y2-y1;
  luminosity_t g = y3;
  luminosity_t h = y4-y3;

  luminosity_t rec = 1/(d*f-b*h);
  if (d == 0)
    return false;
  luminosity_t p1 = (a*h-c*h-d*e+d*g) * rec;
  luminosity_t p2 = (e*f - b*e + b*g - c*f) * rec;
  if (p1 < 0 || p2 < 0 || p2 > 1)
    return false;
  *t = p2;

  return true;
}

/* Dominant wavelength is computed as intersection of the line from whitepoint to a given
   color with the horsehoe of CIE1931.
   Not all colors have dominant wavelengths; return 0 if they does not  */

luminosity_t
dominant_wavelength (xy_t color, xy_t whitepoint)
{
  for (int i = 0; i < SPECTRUM_SIZE - 1; i++)
    {
      /* Compute points of the horsehose observer.  */
      xy_t c1 ((xyz){cie_cmf_x[i], cie_cmf_y[i], cie_cmf_z[i]});
      xy_t c2 ((xyz){cie_cmf_x[i+1], cie_cmf_y[i+1], cie_cmf_z[i+1]});
      luminosity_t t;
      /* Compute the intersection.  */
      if (intersect (whitepoint.x, whitepoint.y, color.x, color.y,
		     c1.x, c1.y, c2.x, c2.y, &t))
	return SPECTRUM_START + (i + t) * SPECTRUM_STEP;
    }
  return 0;
}

/* Find whitepoint so that red, green and blue dominant wavelengths matches
   as well as possible to the specified values. */

xy_t
find_best_whitepoint (xyz red, xyz green, xyz blue,
		      luminosity_t red_dominating_wavelength,
		      luminosity_t green_dominating_wavelength,
		      luminosity_t blue_dominating_wavelength)
{
  xy_t best_white(-1, -1);
  bool best_white_found = 0;
  luminosity_t best_white_dist = 0;
  for (luminosity_t x = 0.01; x < 1; x += 0.01)
    for (luminosity_t y = 0.01; y < 1; y += 0.01)
      {
	xy_t w (x, y);
	luminosity_t d1 = dominant_wavelength (red, w);
	luminosity_t d2 = dominant_wavelength (green, w);
        luminosity_t d3 = dominant_wavelength (blue, w);
	if (!d1 || !d2 || !d3)
	  continue;
	luminosity_t d = fabs (red_dominating_wavelength - d1) + fabs (green_dominating_wavelength - d2) + fabs (blue_dominating_wavelength - d3);
	if (!best_white_found || d < best_white_dist)
	{
	  best_white = w;
	  best_white_dist = d;
	  best_white_found = true;
	}
      }
  return best_white;
}
}
