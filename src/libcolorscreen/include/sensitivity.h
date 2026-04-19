#ifndef SENSITIVITY_H
#define SENSITIVITY_H
#include "color.h"
#include "dllpublic.h"
#include <cmath>
#include <tuple>
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
  luminosity_t
  apply (luminosity_t in)
  {
    if (in <= xs[0])
      return ys[0];
    for (int i = 1; i < n; i++)
      {
        if (in <= xs[i])
          {
            return (ys[i - 1]
                    + (in - xs[i - 1])
                          * ((ys[i] - ys[i - 1]) / (xs[i] - xs[i - 1])));
          }
      }
    return ys[n - 1];
  }
  /* Get minimal value multiplied by boost.  */
  luminosity_t
  get_fog (luminosity_t boost)
  {
    luminosity_t min = ys[0] * boost;
    for (int i = 1; i <= n; i++)
      min = std::min (min, ys[i]) * boost;
    return min;
  }
  /* Get maximal value.  */
  luminosity_t
  get_max ()
  {
    luminosity_t max = ys[0];
    for (int i = 1; i <= n; i++)
      max = std::max (max, ys[i]);
    return max;
  }
  /* Output GNU plottable data.  */
  void
  print (FILE *f)
  {
    for (int i = 0; i < n; i++)
      fprintf (f, "%f %f\n", xs[i], ys[i]);
  }
};

inline luminosity_t
interpolate (luminosity_t n1, luminosity_t n2, luminosity_t perc)
{
  luminosity_t diff = n2 - n1;

  return n1 + (diff * perc);
}

/* The generalized logistic function, also known as Richards' curve,
   is a flexible growth model that provides an alternative to the standard
   logisitic or sigmoid functions. It is used here to model the H&D
   (Hurter-Driffield) characteristic curve of film.

   The formula used is:
   Y(x) = A + (K - A) / (1 + exp(-B * (x - M)))^(1/v)

   If is_inverse is true, we calculate the curve as X(y) instead of Y(x)
   to handle very steep regions (high gamma).  */
struct richards_curve_parameters
{
  /* Lower asymptote. In direct mode, this represents the minimal density.  */
  luminosity_t A;
  /* Upper asymptote. In direct mode, this represents the maximal density.  */
  luminosity_t K;
  /* Growth rate or slope factor. Controls the steepness of the curve.  */
  luminosity_t B;
  /* Horizontal shift or offset. Corresponds to the center of the linear
   * region.  */
  luminosity_t M;
  /* Asymmetry parameter. Controls where the inflection point occurs relative
     to the asymptotes. v=1 gives the standard symmetric logistic curve.  */
  luminosity_t v;
  /* If true, the curve defines X as a function of Y. This helps maintain
     numerical stability for extremely steep characteristic curves.  */
  bool is_inverse;

  constexpr richards_curve_parameters (luminosity_t new_A, luminosity_t new_K,
                                       luminosity_t new_B, luminosity_t new_M,
                                       luminosity_t new_v, bool new_inverse)
      : A (new_A), K (new_K), B (new_B), M (new_M), v (new_v),
        is_inverse (new_inverse)
  {
  }

  bool
  operator== (const richards_curve_parameters &o) const
  {
    return A == o.A && K == o.K && B == o.B && M == o.M && v == o.v
           && is_inverse == o.is_inverse;
  }
};

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
      : minx (-6), miny (5), linear1x (-5), linear1y (5), linear2x (5),
        linear2y (-5), maxx (6), maxy (-5)
  {
  }
  constexpr hd_curve_parameters (luminosity_t new_minx, luminosity_t new_miny,
                                 luminosity_t new_linear1x,
                                 luminosity_t new_linear1y,
                                 luminosity_t new_linear2x,
                                 luminosity_t new_linear2y,
                                 luminosity_t new_maxx, luminosity_t new_maxy)
      : minx (new_minx), miny (new_miny), linear1x (new_linear1x),
        linear1y (new_linear1y), linear2x (new_linear2x),
        linear2y (new_linear2y), maxx (new_maxx), maxy (new_maxy)
  {
  }
  bool
  operator== (const hd_curve_parameters &other) const
  {
    return minx == other.minx && miny == other.miny
           && linear1x == other.linear1x && linear1y == other.linear1y
           && linear2x == other.linear2x && linear2y == other.linear2y
           && maxx == other.maxx && maxy == other.maxy;
  }

  bool
  is_inverted_p () const
  {
    luminosity_t gamma
        = (linear2x != linear1x)
              ? std::abs ((linear2y - linear1y) / (linear2x - linear1x))
              : 1e10;
    luminosity_t toe = (linear1x != minx)
                           ? std::abs ((linear1y - miny) / (linear1x - minx))
                           : 1e10;
    luminosity_t shoulder
        = (maxx != linear2x) ? std::abs ((maxy - linear2y) / (maxx - linear2x))
                             : 1e10;
    return (toe > gamma || shoulder > gamma
            || (gamma > 10.0 && gamma != 1e10));
  }

  bool
  is_valid_for_richards_curve () const
  {
    auto monotonic = [] (double a, double b, double c, double d)
      {
        if (std::abs (a - b) < 1e-6 || std::abs (b - c) < 1e-6
            || std::abs (c - d) < 1e-6)
          return false;
        return (a < b && b < c && c < d) || (a > b && b > c && c > d);
      };
    return monotonic (minx, linear1x, linear2x, maxx)
           && monotonic (miny, linear1y, linear2y, maxy);
  }

  void
  sort_by_x ()
  {
    if (minx > maxx)
      {
        std::swap (minx, maxx);
        std::swap (miny, maxy);
        std::swap (linear1x, linear2x);
        std::swap (linear1y, linear2y);
      }
  }

  void
  adjust_M (double old_M, double new_M)
  {
    double delta = new_M - old_M;
    if (is_inverted_p ())
      {
        miny += delta;
        linear1y += delta;
        linear2y += delta;
        maxy += delta;
      }
    else
      {
        minx += delta;
        linear1x += delta;
        linear2x += delta;
        maxx += delta;
      }
  }

  void
  adjust_B (double old_B, double new_B, double M)
  {
    double ratio = old_B / new_B;
    if (is_inverted_p ())
      {
        miny = M + (miny - M) * ratio;
        linear1y = M + (linear1y - M) * ratio;
        linear2y = M + (linear2y - M) * ratio;
        maxy = M + (maxy - M) * ratio;
      }
    else
      {
        minx = M + (minx - M) * ratio;
        linear1x = M + (linear1x - M) * ratio;
        linear2x = M + (linear2x - M) * ratio;
        maxx = M + (maxx - M) * ratio;
      }
  }

  void
  adjust_A (double old_A, double new_A, double K)
  {
    auto f = [&] (double v)
      {
        if (std::abs (K - old_A) < 1e-8)
          return v;
        return new_A + (v - old_A) * (K - new_A) / (K - old_A);
      };
    if (is_inverted_p ())
      {
        minx = f (minx);
        linear1x = f (linear1x);
        linear2x = f (linear2x);
        maxx = f (maxx);
      }
    else
      {
        miny = f (miny);
        linear1y = f (linear1y);
        linear2y = f (linear2y);
        maxy = f (maxy);
      }
  }

  void
  adjust_K (double old_K, double new_K, double A)
  {
    auto f = [&] (double v)
      {
        if (std::abs (old_K - A) < 1e-8)
          return v;
        return A + (v - A) * (new_K - A) / (old_K - A);
      };
    if (is_inverted_p ())
      {
        minx = f (minx);
        linear1x = f (linear1x);
        linear2x = f (linear2x);
        maxx = f (maxx);
      }
    else
      {
        miny = f (miny);
        linear1y = f (linear1y);
        linear2y = f (linear2y);
        maxy = f (maxy);
      }
  }

  DLL_PUBLIC void adjust_v (double old_v, double new_v, double B, double M);

  DLL_PUBLIC void adjust_richards (const richards_curve_parameters &old_rp,
                                   const richards_curve_parameters &new_rp);
};

/* Convert 4-point HD curve parameters to Richards curve parameters.  */
DLL_PUBLIC struct richards_curve_parameters hd_to_richards_curve_parameters (const hd_curve_parameters &p);
DLL_PUBLIC struct hd_curve_parameters richards_to_hd_curve_parameters (const richards_curve_parameters &rp);

/* Sensitivity curve of an "ideal" digital camera with safety buffer in upper
 * 90%.  */
extern DLL_PUBLIC struct hd_curve_parameters safe_output_curve_params,
    safe_reversal_output_curve_params, input_curve_params;

/* Produce a synthetic HD curve.  */
class synthetic_hd_curve : public hd_curve
{
public:
  synthetic_hd_curve (int points, struct hd_curve_parameters p);
  ~synthetic_hd_curve ()
  {
    free (xs);
    free (ys);
  }
};

/* Produce a Richard's HD curve.  */
class richards_hd_curve : public hd_curve
{
public:
  DLL_PUBLIC static luminosity_t
  eval_richards (const richards_curve_parameters &p, luminosity_t xs,
                 bool clamp = false, luminosity_t clampmin = 0,
                 luminosity_t clampmax = 0);
  DLL_PUBLIC richards_hd_curve (int points,
                                const struct hd_curve_parameters &p);

  DLL_PUBLIC richards_hd_curve (int points,
                                const struct richards_curve_parameters &rp);
  ~richards_hd_curve ()
  {
    free (xs);
    free (ys);
  }

private:
  void sample (const richards_curve_parameters &p, luminosity_t min_x,
               luminosity_t max_x, bool clamp = false,
               luminosity_t clampmin = 0, luminosity_t clampmax = 0);
};

class film_sensitivity
{
public:
  film_sensitivity (hd_curve *c, luminosity_t preflash = 0.1,
                    luminosity_t exp = 100, luminosity_t m_boost = 1)
      : m_curve (c), m_preflash (preflash), m_boost (m_boost),
        m_exposure (exp), m_clip (false)
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

  static constexpr DLL_PUBLIC struct hd_curve_description
      hd_curves_properties[]
      = { { "linear-reversal",
            "Linear reversal film",
            { -6, 5, -5, 5, 5, -5, 6, -5 } },
          { "linear-negative",
            "Linear negative film",
            { -6, -5, -5, -5, 5, 5, 6, 5 } },
          { "safe-linear-reversal",
            "Linear reversal film",
            { 0, 1, 0, 1, 0.7, 0.3, 3, 0 } },
          { "safe-linear-negative",
            "Linear negative film",
            { 0, 0, 0, 0, 0.7, 0.7, 3, 1 } },
          { "spicer-dufay-low",
            "Spicer-Dufay low development",
            { 0.005596021177603383, 0.13326648483876236, 0.9264367078453395,
              0.25357372051981475, 3.8351612385689076, 1.7539186587518052,
              3.8351612385689076, 1.7539186587518052 } },
          { "spicer-dufay-mid",
            "Spicer-Dufay mid development",
            { 0.005596021177603383, 0.13326648483876236, 0.7346061286699825,
              0.3354524306112632, 2.7042515642547724, 2.207183539226697,
              2.7042515642547724, 2.207183539226697 } },
          { "spicer-dufay-high",
            "Spicer-Dufay high development",
            { 0.005596021177603383, 0.13326648483876236, 0.664193807155463,
              0.430406706240976, 1.5716733515161239, 2.2480065778918665,
              1.5716733515161239, 2.2480065778918665 } },
          { "spicer-dufay-reversal-low",
            "Spicer-Dufay reversal low development",
            { 0.10817262955238283, 1.7389218674795455, 0.10817262955238283,
              1.7389218674795455, 3.1461832183539227, 0.19716428686026033,
              3.990309642226858, 0.10466468795122763 } },
          { "spicer-dufay-reversal-mid",
            "Spicer-Dufay reversal mid development",
            { 1.2834525910476495, 2.253437349590888, 1.2834525910476495,
              2.253437349590888, 3.262801219316541, 0.27367639980747693,
              3.990309642226858, 0.10466468795122763 } },
          { "spicer-dufay-reversal-high",
            "Spicer-Dufay reversal high development",
            { 2.446334028557678, 2.2031926841007543, 2.446334028557678,
              2.2031926841007543, 3.409735279961495, 0.34393951548211144,
              3.9904123215145204, 0.13004572437028772 } },
          { "paget-coorection1",
            "Paget correction 1 (to linear)",
            { -2.274010, 3.400111, -1.341965, 1.402846, -0.789100, 0.927726,
              -0.437047, -0.003900 } },
          { "hurley-video2",
            "Paget correction 2 (to linear)",
            { -2.500011, 4.610400, -1.003162, 1.374261, -0.718547, 1.165326,
              -0.255926, -0.118284 } } };

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

    if (y > 0)
      {
        y = std::log10 (y);
        /* Apply HD curve. */
        y = m_curve->apply (y);
      }
    else
      y = m_curve->ys[0];

    /* Adjust contrast.  */
    // y = (y - mid) * m_contrast + mid;

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
        // fprintf (stderr, "%f:%f %f %f %f\n", y, min, max, yy, ap);
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
    for (float l = 0; l <= 1; l += 0.01)
      fprintf (f, "%f %f\n", l, apply (l));
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
