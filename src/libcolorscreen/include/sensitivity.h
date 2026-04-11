#ifndef SENSITIVITY_H
#define SENSITIVITY_H
#include "dllpublic.h"
#include "color.h"
namespace colorscreen
{
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


/* Description of a typical HD curve.  */
struct hd_curve_parameters
{
  /* Point where minimal density is reached.  */
  luminosity_t minx, miny;
  /* First point of the linear segment.  */
  luminosity_t linear1x, linear1y;
  /* Last point of the linear segment.  */
  luminosity_t linear2x, linear2y;
  /* Point where the maximal density is reached.  */
  luminosity_t maxx, maxy;

  constexpr hd_curve_parameters ()
  : minx(-6), miny(5), linear1x (-5), linear1y (5), linear2x(5), linear2y(-5), maxx(6), maxy(-5)
  {
  }
  constexpr hd_curve_parameters (luminosity_t new_minx, luminosity_t new_miny, luminosity_t new_linear1x, luminosity_t new_linear1y, luminosity_t new_linear2x, luminosity_t new_linear2y, luminosity_t new_maxx, luminosity_t new_maxy)
  : minx(new_minx), miny(new_miny), linear1x (new_linear1x), linear1y (new_linear1y), linear2x(new_linear2x), linear2y(new_linear2y), maxx(new_maxx), maxy(new_maxy)
  {
  }
  bool
  operator== (const hd_curve_parameters &other) const
  {
    return minx == other.minx && miny == other.miny
	   && linear1x == other.linear1x
	   && linear1y == other.linear1y
	   && linear2x == other.linear2x
	   && linear2y == other.linear2y
	   && maxx == other.maxx && maxy == other.maxy;
  }
};

/* Sensitivity curve of an "ideal" digital camera with safety buffer in upper 90%.  */
extern DLL_PUBLIC struct hd_curve_parameters safe_output_curve_params, safe_reversal_output_curve_params, input_curve_params;

/* Produce a synthetic HD curve.  */
class synthetic_hd_curve : public hd_curve
{
public:
  synthetic_hd_curve (int points, struct hd_curve_parameters p)
    {
      if (!(p.minx < p.maxx))
	p.maxx = p.minx + 1;
      bool dostart = p.minx < p.linear1x && p.linear1x < p.linear2x && p.linear2x <= p.maxx;
      bool doend = p.minx <= p.linear1x && p.linear1x < p.linear2x && p.linear2x < p.maxx;
      int n1 = dostart ? points : 1;
      n = n1 + (doend ? points : 1);
      xs = (luminosity_t *)malloc (n * sizeof (*xs));
      ys = (luminosity_t *)malloc (n * sizeof (*ys));
      luminosity_t slope = p.linear2y != p.linear1y ? (p.linear2x - p.linear1x) / (p.linear2y - p.linear1y) : 0;
      xs[0] = p.minx;
      ys[0] = p.miny;
      xs[n - 1] = p.maxx;
      ys[n - 1] = p.maxy;

      luminosity_t start_middlex = p.linear1x - (p.linear1y - p.miny) * slope;
      luminosity_t start_middley = p.miny;

      if (start_middlex < p.minx && slope != 0)
        {
          start_middlex = p.minx;
          start_middley = p.linear1y - (p.linear1x - p.minx) / slope;
        }

      luminosity_t end_middlex = p.linear2x + (p.maxy - p.linear2y) * slope;
      luminosity_t end_middley = p.maxy;

      if (end_middlex > p.maxx && slope != 0)
        {
          end_middlex = p.maxx;
          end_middley = p.linear2y + (p.maxx - p.linear2x) / slope;
        }

      for (int i = 0; i < points; i++)
	{
	  if (dostart)
	    bezier (&xs[i], &ys[i], p.minx, p.miny, 
		    start_middlex, start_middley,
		    p.linear1x,p.linear1y,
		    i / (luminosity_t)(points - 1));
	  if (doend)
	    bezier (&xs[i+n1], &ys[i+n1], p.linear2x, p.linear2y, 
		    end_middlex, end_middley,
		    p.maxx, p.maxy,
		    i / (luminosity_t)(points - 1));
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
  film_sensitivity (hd_curve *c, luminosity_t preflash = 0.1, luminosity_t exp = 100, luminosity_t m_boost = 1)
  : m_curve (c), m_preflash (preflash), m_boost (m_boost), m_exposure (exp), m_clip (false)
  {
  }
  static struct hd_curve ilfrod_galerie_FB1;
  static struct hd_curve fujicolor_crystal_archive_digital_pearl_paper;
  static struct hd_curve kodachrome_25_red;
  static struct hd_curve kodachrome_25_green;
  static struct hd_curve kodachrome_25_blue;
  DLL_PUBLIC static struct hd_curve linear_sensitivity;

  struct hd_curve_description
  {
    const char *name;
    const char *pretty_name;
    hd_curve_parameters params;
  };

  enum hd_curves
  {
    linear_reversal_sensitivity,
    linear_negative_sensitivity,
    safe_linear_reversal_sensitivity,
    safe_linear_negative_sensitivity,
    spicer_dufay_low,
    spicer_dufay_mid,
    spicer_dufay_high,
    spicer_dufay_reversal_low,
    spicer_dufay_reversal_mid,
    spicer_dufay_reversal_high,
    hurley_video,
    hd_curves_max,
  };

  static constexpr DLL_PUBLIC struct hd_curve_description hd_curves_properties[] = {
    {
      "linear-reversal", "Linear reversal film",
      {-6, 5, -5, 5, 5, -5, 6, -5}
    },
    {
      "linear-negative", "Linear negative film",
      {-6, -5, -5, -5, 5, 5, 6, 5}
    },
    {
      "safe-linear-reversal", "Linear reversal film",
      {0,1,0,1,0.7,0.3,3,0}
    },
    {
      "safe-linear-negative", "Linear negative film",
      {0, 0, 0, 0, 0.7, 0.7, 3, 1}
    },
    {
      "spicer-dufay-low", "Spicer-Dufay low development",
      {0.005596021177603383, 0.13326648483876236,
       0.9264367078453395, 0.25357372051981475,
       3.8351612385689076, 1.7539186587518052,
       3.8351612385689076, 1.7539186587518052}
    },
    {
      "spicer-dufay-mid", "Spicer-Dufay mid development",
       {0.005596021177603383, 0.13326648483876236,
        0.7346061286699825, 0.3354524306112632,
        2.7042515642547724, 2.207183539226697,
        2.7042515642547724, 2.207183539226697}
    },
    {
      "spicer-dufay-high", "Spicer-Dufay high development",
       {0.005596021177603383, 0.13326648483876236,
        0.664193807155463, 0.430406706240976,
        1.5716733515161239, 2.2480065778918665,
        1.5716733515161239, 2.2480065778918665}
    },
    {
      "spicer-dufay-reversal-low", "Spicer-Dufay reversal low development",
      {
	0.10817262955238283, 1.7389218674795455,
	0.10817262955238283, 1.7389218674795455,
	3.1461832183539227, 0.19716428686026033,
	3.990309642226858, 0.10466468795122763}
    },
    {
      "spicer-dufay-reversal-mid", "Spicer-Dufay reversal mid development",
       {1.2834525910476495, 2.253437349590888,
	1.2834525910476495, 2.253437349590888,
	3.262801219316541, 0.27367639980747693,
	3.990309642226858, 0.10466468795122763}
    },
    {
      "spicer-dufay-reversal-high", "Spicer-Dufay reversal high development",
	{2.446334028557678, 2.2031926841007543,
	 2.446334028557678, 2.2031926841007543,
	 3.409735279961495, 0.34393951548211144,
	 3.9904123215145204, 0.13004572437028772}
    },
    {
      "hurley-video", "Frank Hurley's laboratory correction",
        { -2.745997, 3.133772,
 	  -1.930210, 2.190697,
 	  -0.970248, 1.208836,
 	  -0.299072, -0.399532 }
    }
  };

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
      y = std::log10 (y);

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
}
#endif
