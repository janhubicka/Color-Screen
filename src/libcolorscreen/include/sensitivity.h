#ifndef SENSITIVITY_H
#define SENSITIVITY_H
#include "dllpublic.h"
#include "color.h"
/* Hurter–Driffield characteristic curve based on data points
   with simple linear interpolation.  */
struct hd_curve
{
  /* x coordinates of measured values  */
  luminosity_t *xs;
  /* y coordinates of measured values  */
  luminosity_t *ys;
  /* Number of points.  */
  int n;

  /* Return curve in position in.  */
  luminosity_t apply (luminosity_t in)
    {
      if (in <= xs[0])
	return ys[0];
      for (int i = 1; i < n; i++)
	{
	  if (in <= xs[i])
	    {
	      return(ys[i-1] + (in - xs[i-1]) * \
		     ((ys[i] - ys[i-1]) / (xs[i] - xs[i-1])));
	    }
	}
      return ys[n-1];
    }
  /* Get minimal value multiplied by boost.  */
  luminosity_t get_fog (luminosity_t boost)
    {
      luminosity_t min = ys[0] * boost;
      for (int i = 1; i <= n; i++)
	min = std::min (min, ys[i]) * boost;
      return min;
    }
  /* Get maximal value.  */
  luminosity_t get_max ()
    {
      luminosity_t max = ys[0];
      for (int i = 1; i <= n; i++)
	max = std::max (max, ys[i]);
      return max;
    }
  /* Output GNU plottable data.  */
  void print (FILE *f)
    {
      for (int i = 0 ; i < n; i++)
	fprintf (f, "%f %f\n", xs[i], ys[i]);
    }
};

inline luminosity_t
interpolate(luminosity_t n1, luminosity_t n2, luminosity_t perc)
{
    luminosity_t diff = n2 - n1;

    return n1 + ( diff * perc );
}

/* Compute cubic bezier curve passing trhoug y1,y2 and x3,y3
   with pint x2,y2 determining derivatives at the endpoints.  */
inline void
bezier (luminosity_t *rx, luminosity_t *ry,
        luminosity_t x1, luminosity_t y1,
        luminosity_t x2, luminosity_t y2,
        luminosity_t x3, luminosity_t y3,
	luminosity_t t)
{
    luminosity_t xa = interpolate (x1, x2, t);
    luminosity_t ya = interpolate (y1, y2, t);
    luminosity_t xb = interpolate (x2, x3, t);
    luminosity_t yb = interpolate (y2, y3, t);

    *rx = interpolate(xa, xb, t);
    *ry = interpolate(ya, yb, t);
}


/* Descriptio of a typical HD curve.  */
struct synthetic_hd_curve_parameters
{
  /* Point where minimal density is reached.  */
  luminosity_t minx, miny;
  /* First point of the linear segment.  */
  luminosity_t linear1x, linear1y;
  /* Last point of the linear segment.  */
  luminosity_t linear2x, linear2y;
  /* Point where the maximal density is reached.  */
  luminosity_t maxx, maxy;
};

/* Densitivity curve of an "ideal" digital camera with safety buffer in upper 90%.  */
extern struct synthetic_hd_curve_parameters safe_output_curve_params, safe_reversal_output_curve_params, input_curve_params;

/* Produce a synthetic HD curve.  */
class synthetic_hd_curve : public hd_curve
{
public:
  synthetic_hd_curve (int points, struct synthetic_hd_curve_parameters p)
    {
      bool dostart = p.minx != p.linear1x;
      bool doend = p.linear2x != p.maxx;
      int n1 = dostart ? points : 1;
      n = n1 + (doend ? points : 1);
      xs = (luminosity_t *)malloc (n * sizeof (*xs));
      ys = (luminosity_t *)malloc (n * sizeof (*ys));
      luminosity_t slope = (p.linear2x - p.linear1x) / (p.linear2y - p.linear1y);
      xs[0] = p.minx;
      ys[0] = p.miny;
      xs[n - 1] = p.maxx;
      ys[n - 1] = p.maxy;
      for (int i = 0; i < points; i++)
	{
	  if (dostart)
	    bezier (&xs[i], &ys[i], p.minx, p.miny, 
		    p.linear1x - (p.linear1y - p.miny) * slope,
		    p.miny, p.linear1x,p.linear1y,
		    i / (luminosity_t)(n - 1));
	  if (doend)
	    bezier (&xs[i+n1], &ys[i+n1], p.linear2x, p.linear2y, 
		    p.linear2x + (p.maxy - p.linear2y) * slope,
		    p.maxy, p.maxx, p.maxy,
		    i / (luminosity_t)(n - 1));
	}
#if 0
	  FILE *f = fopen("/tmp/shd.dat", "wt");
	  print (f);
	  fclose (f);
#endif
    }
  ~synthetic_hd_curve()
    {
      free (xs);
      free (ys);
    }
};


class
film_sensitivity
{
public:
  film_sensitivity (hd_curve *c, luminosity_t preflash = 0.1, luminosity_t exp = 100)
  : m_curve (c), m_preflash (preflash), m_boost (1), m_exposure (exp), m_clip (false)
  {
  }
  static struct hd_curve ilfrod_galerie_FB1;
  static struct hd_curve fujicolor_crystal_archive_digital_pearl_paper;
  static struct hd_curve kodachrome_25_red;
  static struct hd_curve kodachrome_25_green;
  static struct hd_curve kodachrome_25_blue;
  DLL_PUBLIC static struct hd_curve linear_sensitivity;

  void
  precompute ()
    {
      if (m_clip)
        Dfog = m_curve->get_fog (m_boost);
      else
	Dfog = 0;
      Dmax = m_curve->get_max ();
      mid = m_curve->apply (Dmax / 2);
      if (m_curve->n < 2)
	abort ();
    }
  luminosity_t
  apply (luminosity_t y)
    {
      /* Preflash.  */
      y += m_preflash / 100;

      /* Apply exposure */
      y *= m_exposure;

      /* Scale emulsion response and logarithmize.  */
      y = log10 (y);

      /* Adjust contrast.  */
      //y = (y - mid) * m_contrast + mid;
      
      /* Apply HD curve. */
      y = m_curve->apply (y);

      /* Apply density boost.  */
      y *= m_boost;

      /* Compensate fog  */
      y -= Dfog;

      /* Get back to linear.  */
      y = pow (10, -y);
      return y;
    }
  luminosity_t
  unapply (luminosity_t y)
  {
    float min = 0;
    float max = 1;
    while (true)
      {
	luminosity_t yy = (min + max) / 2;
	luminosity_t ap = apply (yy);
	//fprintf (stderr, "%f:%f %f %f %f\n", y, min, max, yy, ap);
	if (fabs (ap - y) < 0.000001 || max - min < 0.000001)
	  return yy;
	if (ap < y)
	  max = yy;
	else
	  min = yy;
      }
  }
  void
  print (FILE *f)
  {
    for (float l = 0; l <= 1; l+=0.01)
      fprintf (f, "%f %f\n",l,apply(l));
  }
  void
  print_hd (FILE *f)
  {
    m_curve->print (f);
  }
private:
  hd_curve *m_curve;
  luminosity_t m_preflash;
  luminosity_t m_boost;
  luminosity_t m_exposure;
  bool m_clip;

  luminosity_t Dfog, Dmax, mid;
};
#endif
