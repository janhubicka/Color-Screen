#ifndef SENSITIVITY_H
#define SENSITIVITY_H
#include "dllpublic.h"
#include "color.h"
#include <tuple>
#include <cmath>
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
  /* Horizontal shift or offset. Corresponds to the center of the linear region.  */
  luminosity_t M;
  /* Asymmetry parameter. Controls where the inflection point occurs relative
     to the asymptotes. v=1 gives the standard symmetric logistic curve.  */
  luminosity_t v;
  /* If true, the curve defines X as a function of Y. This helps maintain 
     numerical stability for extremely steep characteristic curves.  */
  bool is_inverse;

  constexpr richards_curve_parameters (luminosity_t new_A, luminosity_t new_K, luminosity_t new_B, luminosity_t new_M, luminosity_t new_v, bool new_inverse)
  : A(new_A), K(new_K), B(new_B), M(new_M), v(new_v), is_inverse(new_inverse)
  {}
  
  bool operator== (const richards_curve_parameters &o) const
  {
    return A == o.A && K == o.K && B == o.B && M == o.M && v == o.v && is_inverse == o.is_inverse;
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

  bool is_inverted_p() const {
    luminosity_t gamma = (linear2x != linear1x) 
                         ? std::abs((linear2y - linear1y) / (linear2x - linear1x)) : 1e10;
    luminosity_t toe = (linear1x != minx) 
                       ? std::abs((linear1y - miny) / (linear1x - minx)) : 1e10;
    luminosity_t shoulder = (maxx != linear2x) 
                            ? std::abs((maxy - linear2y) / (maxx - linear2x)) : 1e10;
    return (toe > gamma || shoulder > gamma || (gamma > 10.0 && gamma != 1e10));
  }

  void adjust_M(double old_M, double new_M) {
    double delta = new_M - old_M;
    if (is_inverted_p()) {
      miny += delta; linear1y += delta; linear2y += delta; maxy += delta;
    } else {
      minx += delta; linear1x += delta; linear2x += delta; maxx += delta;
    }
  }

  void adjust_B(double old_B, double new_B, double M) {
    double ratio = old_B / new_B;
    if (is_inverted_p()) {
      miny = M + (miny - M) * ratio;
      linear1y = M + (linear1y - M) * ratio;
      linear2y = M + (linear2y - M) * ratio;
      maxy = M + (maxy - M) * ratio;
    } else {
      minx = M + (minx - M) * ratio;
      linear1x = M + (linear1x - M) * ratio;
      linear2x = M + (linear2x - M) * ratio;
      maxx = M + (maxx - M) * ratio;
    }
  }

  void adjust_A(double old_A, double new_A, double K) {
    auto f = [&](double v) {
      if (std::abs(K - old_A) < 1e-8) return v;
      return new_A + (v - old_A) * (K - new_A) / (K - old_A);
    };
    if (is_inverted_p()) {
      minx = f(minx); linear1x = f(linear1x); linear2x = f(linear2x); maxx = f(maxx);
    } else {
      miny = f(miny); linear1y = f(linear1y); linear2y = f(linear2y); maxy = f(maxy);
    }
  }

  void adjust_K(double old_K, double new_K, double A) {
    auto f = [&](double v) {
      if (std::abs(old_K - A) < 1e-8) return v;
      return A + (v - A) * (new_K - A) / (old_K - A);
    };
    if (is_inverted_p()) {
      minx = f(minx); linear1x = f(linear1x); linear2x = f(linear2x); maxx = f(maxx);
    } else {
      miny = f(miny); linear1y = f(linear1y); linear2y = f(linear2y); maxy = f(maxy);
    }
  }

  void adjust_v(double old_v, double new_v, double B, double M) {
    auto f = [&](double in) {
      double Z = B * (in - M);
      // Height S = (1 + exp(-Z))^(-1/v) remains constant
      double inner = std::expm1((new_v / old_v) * std::log1p(std::exp(-Z)));
      if (inner <= 1e-20) inner = 1e-20;
      return M - std::log(inner) / B;
    };
    
    // 1. Move knots using precise formula
    double old_l1, old_l2, new_l1, new_l2, old_min, old_max, new_min, new_max;
    if (is_inverted_p()) {
      old_l1 = linear1y; old_l2 = linear2y; old_min = miny; old_max = maxy;
      linear1y = f(linear1y); linear2y = f(linear2y);
      new_l1 = linear1y; new_l2 = linear2y;
    } else {
      old_l1 = linear1x; old_l2 = linear2x; old_min = minx; old_max = maxx;
      linear1x = f(linear1x); linear2x = f(linear2x);
      new_l1 = linear1x; new_l2 = linear2x;
    }

    // 2. Adjust endpoints to satisfy solver's heuristic: v = d_low / d_high
    // d_low = |min - l_toe|, d_high = |max - l_shoulder|
    double old_d_toe = std::abs(old_l1 - old_min);
    double old_d_shoulder = std::abs(old_max - old_l2);
    double old_knot_span = std::abs(old_l2 - old_l1);
    double new_knot_span = std::abs(new_l2 - new_l1);
    
    // Scale distances such that ratio changes by new_v / old_v
    double scale = (old_knot_span > 1e-8) ? (new_knot_span / old_knot_span) : 1.0;
    // d_low_new = d_low_old * scale * (v_new / v_old)
    // d_high_new = d_high_old * scale
    double new_d_toe = old_d_toe * scale * (new_v / old_v);
    double new_d_shoulder = old_d_shoulder * scale;
    
    if (is_inverted_p()) {
      miny = (linear1y < linear2y) ? (linear1y - new_d_toe) : (linear1y + new_d_toe);
      maxy = (linear2y > linear1y) ? (linear2y + new_d_shoulder) : (linear2y - new_d_shoulder);
    } else {
      minx = (linear1x < linear2x) ? (linear1x - new_d_toe) : (linear1x + new_d_toe);
      maxx = (linear2x > linear1x) ? (linear2x + new_d_shoulder) : (linear2x - new_d_shoulder);
    }
  }

  void adjust_richards(const richards_curve_parameters &old_rp, const richards_curve_parameters &new_rp) {
    if (old_rp.is_inverse != new_rp.is_inverse) return; // Should not happen in incremental GUI use
    if (old_rp.A != new_rp.A) adjust_A(old_rp.A, new_rp.A, old_rp.K);
    if (old_rp.K != new_rp.K) adjust_K(old_rp.K, new_rp.K, new_rp.A);
    if (old_rp.M != new_rp.M) adjust_M(old_rp.M, new_rp.M);
    if (old_rp.B != new_rp.B) adjust_B(old_rp.B, new_rp.B, new_rp.M);
    if (old_rp.v != new_rp.v) adjust_v(old_rp.v, new_rp.v, new_rp.B, new_rp.M);
  }
};

/* Convert 4-point HD curve parameters to Richards curve parameters.  */
inline struct richards_curve_parameters
hd_to_richards_curve_parameters (const hd_curve_parameters &p)
{
  bool is_inverse = p.is_inverted_p();
  
  luminosity_t eps = 1e-4;
  luminosity_t v = 1.0;
  
  luminosity_t z1 = is_inverse ? p.linear1y : p.linear1x;
  luminosity_t z2 = is_inverse ? p.linear2y : p.linear2x;
  luminosity_t min_z = is_inverse ? p.miny : p.minx;
  luminosity_t max_z = is_inverse ? p.maxy : p.maxx;

  luminosity_t d_low = std::abs(z1 - min_z);
  luminosity_t d_high = std::abs(max_z - z2);

  if (d_high > eps) v = d_low / d_high;
  
  if (v < 0.01) v = 0.01;
  if (v > 10.0) v = 10.0;

  luminosity_t A, K, B, M;

  auto fit = [&](luminosity_t in1, luminosity_t in2, luminosity_t out1, luminosity_t out2, luminosity_t a, luminosity_t k) {
      if (std::abs(out1 - a) < eps) out1 = a + (k > a ? eps : -eps);
      if (std::abs(out1 - k) < eps) out1 = k - (k > a ? eps : -eps);
      if (std::abs(out2 - a) < eps) out2 = a + (k > a ? eps : -eps);
      if (std::abs(out2 - k) < eps) out2 = k - (k > a ? eps : -eps);
      
      luminosity_t vv1 = std::pow((k - a) / (out1 - a), v) - 1.0;
      luminosity_t vv2 = std::pow((k - a) / (out2 - a), v) - 1.0;
      if (vv1 > 1e30 || std::isinf(vv1)) vv1 = 1e30;
      if (vv2 > 1e30 || std::isinf(vv2)) vv2 = 1e30;
      if (vv1 <= 0) vv1 = eps;
      if (vv2 <= 0) vv2 = eps;
      
      luminosity_t L1 = std::log(vv1);
      luminosity_t L2 = std::log(vv2);
      
      luminosity_t denom = in1 - in2;
      if (std::abs(denom) < eps) denom = (denom >= 0 ? eps : -eps);
      
      B = (L2 - L1) / denom;
      if (std::abs(B) < eps) B = eps;
      M = in1 + L1 / B;
  };

  if (!is_inverse)
    {
      A = p.miny; K = p.maxy;
      fit(p.linear1x, p.linear2x, p.linear1y, p.linear2y, A, K);
    }
  else
    {
      A = p.minx; K = p.maxx;
      fit(p.linear1y, p.linear2y, p.linear1x, p.linear2x, A, K);
    }
  return richards_curve_parameters(A, K, B, M, v, is_inverse);
}


/* Helper function to generate hd_curve_parameters perfectly representing a Richard's curve.  */
inline struct hd_curve_parameters
richards_to_hd_curve_parameters (const richards_curve_parameters &rp)
{
  luminosity_t A = rp.A, K = rp.K, B = rp.B, M = rp.M, v = rp.v;
  bool inverse = rp.is_inverse;
  luminosity_t eps = 1e-4;
  
  auto pick = [&](luminosity_t a, luminosity_t k) {
      luminosity_t delta_o = 0.1 * std::abs(k - a);
      if (delta_o == 0) delta_o = 1.0;
      
      luminosity_t o1 = a + (k > a ? delta_o : -delta_o);
      luminosity_t o2 = k - (k > a ? delta_o : -delta_o);
      
      auto solve = [&](luminosity_t out) {
          luminosity_t vv = std::max(eps, (luminosity_t)(std::pow((k - a) / (out - a), v) - 1.0));
          return M - std::log(vv) / B;
      };
      
      auto formula = [&](luminosity_t in) {
          return a + (k - a) / std::pow(1.0 + std::exp(-B * (in - M)), 1.0 / v);
      };
      
      luminosity_t i1 = solve(o1);
      luminosity_t i2 = solve(o2);
      
      // D is the characteristic interval on the independent axis.
      luminosity_t D = std::abs(i1 - i2);
      if (D == 0) D = 1.0;
      D *= 10.0; // Standardized buffer
      
      luminosity_t min_in = std::min(i1, i2) - v * D;
      luminosity_t max_in = std::max(i1, i2) + D;
      
      // Calculate exact boundary locations on the 'output' axis for H&D endpoints
      luminosity_t b1 = formula(min_in);
      luminosity_t b2 = formula(max_in);
      
      return std::make_tuple(min_in, max_in, i1, o1, i2, o2, b1, b2);
  };

  if (!inverse)
    {
      auto [minx, maxx, x1, y1, x2, y2, y_min, y_max] = pick(A, K);
      // Ensure X-axis monotonicity
      if (x1 > x2) { std::swap(x1, x2); std::swap(y1, y2); }
      // In Direct Mode, the curve must reach fog and saturation limits (A, K)
      luminosity_t r_miny = (y1 < y2) ? std::min(A, K) : std::max(A, K);
      luminosity_t r_maxy = (y1 < y2) ? std::max(A, K) : std::min(A, K);
      return hd_curve_parameters(minx, r_miny, x1, y1, x2, y2, maxx, r_maxy);
    }
  else
    {
      auto [miny, maxy, y1, x1, y2, x2, x_min, x_max] = pick(A, K);
      // Ensure X-axis (Output) monotonicity for the renderer
      luminosity_t cx1 = x1, cy1 = y1, cx2 = x2, cy2 = y2;
      if (cx1 > cx2) { std::swap(cx1, cx2); std::swap(cy1, cy2); }
      // In Inverse Mode, exposure X is bounded by the solved range (x_min, x_max)
      luminosity_t r_minx = std::min(x_min, x_max);
      luminosity_t r_maxx = std::max(x_min, x_max);
      // Pair with density boundaries
      luminosity_t r_miny = (x1 < x2) ? miny : maxy;
      luminosity_t r_maxy = (x1 < x2) ? maxy : miny;
      return hd_curve_parameters(r_minx, r_miny, cx1, cy1, cx2, cy2, r_maxx, r_maxy);
    }
}


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
      // Roundoff errors can make small differences here.
      // assert (xs[0] == p.minx && ys[0] == p.miny && xs[n-1] == p.maxx && ys[n-1] == p.maxy);
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

/* Produce a Richard's HD curve.  */
class richards_hd_curve : public hd_curve
{
public:
  static luminosity_t
  eval_richards (const richards_curve_parameters &p, luminosity_t xs,
		 bool clamp = false, luminosity_t clampmin = 0, luminosity_t clampmax =0)
  {
    if (!p.is_inverse)
      {
	// Direct: Density = Richards(LogE)
	return p.A + (p.K - p.A) / std::pow(1.0 + std::exp(-p.B * (xs - p.M)), 1.0 / p.v);
      }
    else
      {
	// Inverse: Density = Richards^-1(LogE). 
	// Valid only between exposure asymptotes A and K.
	//
	luminosity_t eps_x = 1e-8 * std::abs(p.K - p.A);
	
	// Strictly clamp to open interval (A, K) to avoid log(0)
	if (p.K > p.A) {
	    if (xs <= p.A + eps_x)
	      {
	        xs = p.A + eps_x;
		if (clamp)
		  return clampmin;
	      }
	    if (xs >= p.K - eps_x)
	      {
	        xs = p.K - eps_x;
		if (clamp)
		  return clampmax;
	      }
	} else {
	    if (xs >= p.A - eps_x)
	      {
	        xs = p.A - eps_x;
		if (clamp)
		  return clampmax;
	      }
	    if (xs <= p.K + eps_x)
	      {
	        xs = p.K + eps_x;
		if (clamp)
		  return clampmin;
	      }
	}

	luminosity_t base = (p.K - p.A) / (xs - p.A);
	luminosity_t vv = std::pow(std::abs(base), p.v) - 1.0;
	if (vv <= 1e-15) vv = 1e-15;
	luminosity_t ret = p.M - std::log(vv) / p.B;
	if (clamp)
	  ret = std::clamp (ret, std::min (clampmin, clampmax), std::max (clampmin, clampmax));
	return ret;
      }
  }
  richards_hd_curve (int points, const struct hd_curve_parameters &p)
  {
    n = points;
    xs = (luminosity_t *)malloc (n * sizeof (*xs));
    ys = (luminosity_t *)malloc (n * sizeof (*ys));

    richards_curve_parameters rp = hd_to_richards_curve_parameters(p);
    sample (rp, p.minx, p.maxx, rp.is_inverse, p.miny, p.maxy);
  }

  richards_hd_curve (int points, const struct richards_curve_parameters &rp)
  {
    n = points;
    xs = (luminosity_t *)malloc (n * sizeof (*xs));
    ys = (luminosity_t *)malloc (n * sizeof (*ys));

    sample(rp, std::min (rp.A, rp.K), std::max (rp.A, rp.K));
  }
  ~richards_hd_curve()
    {
      free (xs);
      free (ys);
    }
private:
  void sample (const richards_curve_parameters &p, luminosity_t min_x, luminosity_t max_x,
	       bool clamp = false, luminosity_t clampmin = 0, luminosity_t clampmax = 0)
  {
    // Always sample Exposure (X) axis uniformly for optimal resolution in the table.
    for (int i = 0; i < n; i++)
      {
        xs[i] = min_x + i * (max_x - min_x) / (luminosity_t)(n - 1);
	ys[i] = eval_richards (p, xs[i], clamp, clampmin, clampmax);
      }
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

      if (y > 0)
	{
	  y = std::log10 (y);
	  /* Apply HD curve. */
	  y = m_curve->apply (y);
	}
      else
	y = m_curve->ys[0];

      /* Adjust contrast.  */
      //y = (y - mid) * m_contrast + mid;
      

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
