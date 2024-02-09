#ifndef LENS_CORRECTION_H
#define LENS_CORRECTION_H
#include "base.h"
struct lens_warp_correction_parameters
{
  /* Radial correction coefficients, same as in the DNG specs.  */
  coord_t kr[4];
  /* Center in relative coordinates 0...1  */
  point_t center;
  constexpr lens_warp_correction_parameters ()
  : kr {1, 0, 0, 0}, center ({0.5,0.5})
  { }
  bool operator== (lens_warp_correction_parameters &other) const
  {
    return center == other.center
	   && kr[0] == other.kr[0]
	   && kr[1] == other.kr[1]
	   && kr[2] == other.kr[2]
	   && kr[3] == other.kr[3];
  }
  bool is_noop ()
  {
    return kr[0] == 1 && kr[1] == 0 && kr[2] == 0 && kr[3] == 0;
  }
};

struct lens_warp_correction
{
  lens_warp_correction ()
  : m_params (), m_inverted_ratio (0, 1)
  { }

  void
  set_parameters (lens_warp_correction_parameters &p)
  {
    m_params = p;
  }
  


  bool
  precompute (point_t center, point_t c1, point_t c2, point_t c3, point_t c4, bool need_inverse = true)
  {
    if (m_params.is_noop ())
      {
	m_noop = true;
	return true;
      }
    m_noop = false;
    m_max_dist = std::max (c1.dist_from (center), std::max (c2.dist_from (center), std::max (c3.dist_from (center), c4.dist_from (center))));
    m_inv_max_dist_sq2 = 1 / (m_max_dist * m_max_dist);
    m_inverted_ratio.set_range (0, m_max_dist);
    m_center = center;
    if (need_inverse)
      {
	coord_t data[size];

	/* Determine the start of binary search for computing inverse.
	   For correction to make sense, get_ratio must be monotonously increasing
	   for whole image area.  For negative correction coefficient the function
	   is not monotonously increasing for large values.  So search carefully
	   for max element which inverts to m_max_dist.  */
	coord_t max = 1;
	coord_t last = 0;
	coord_t next;
	while ((next = max * get_ratio (max * max * m_inv_max_dist_sq2)) < m_max_dist)
	  {
	    /* Did we reach point where function decreases now?
	       This means that parameters are broken, but we can cap search and get
	       somewhat sane results.  */
	    if (last > next)
	       break;
	    max = 1.2 * max;
	    last = next;
	  }
	m_max = max;

	/* Now precompute inverse.  */
	for (int i = 0; i < size; i++)
	  data[i] = get_inverse (i * m_max_dist / (size - 1));
	m_inverted_ratio.init_by_y_values (data, size);
      }
    return true;
  }

  inline pure_attr 
  point_t corrected_to_scan (point_t p)
  {
    if (m_noop)
      return p;
    /* Radial warp correction.  */
    coord_t ratio = get_ratio (p.dist_sq2_from (m_center) * m_inv_max_dist_sq2);
    point_t ret = (p - m_center) * ratio + m_center;
#if 0
    if (debug && 1 / ratio != m_inverted_ratio.apply (ret.dist_from (m_center)))
      fprintf (stderr, "Inverted ratio error %f %f\n", ratio, m_inverted_ratio.apply (ret.dist_from (m_center)));
    if (debug && fabs (ret.dist_from (m_center) - ratio * p.dist_from (m_center)) > epsilon)
      fprintf (stderr, "Inverted ratio error2 %f %f\n", ratio, m_inverted_ratio.apply (ret.dist_from (m_center)));
    //fprintf (stderr, "Lens correction %f, %f -> %f, %f\n", p.x, p.y, ret.x, ret.y);
#endif
    return ret;
  }
  inline pure_attr 
  point_t scan_to_corrected (point_t p)
  {
    if (m_noop)
      return p;
    point_t ret = (p - m_center) * m_inverted_ratio.apply (p.dist_from (m_center)) + m_center;
    if (debug)
      {
	point_t orig = corrected_to_scan (ret);
	if (!p.almost_eq (orig, epsilon))
	  fprintf (stderr, "Lens correction inverse broken %f, %f -> %f, %f -> %f, %f\n", p.x - m_center.x, p.y - m_center.y, ret.x - m_center.x, ret.y - m_center.y, orig.x - m_center.x, orig.y - m_center.y);
      }
    return ret;
  }

  pure_attr bool
  is_noop ()
  {
    return m_noop;
  }

private:
  static constexpr const int size = 16 * 1024;
  lens_warp_correction_parameters m_params;
  /* Center in image coordinates, possibly after applying motor corrections.  */
  point_t m_center;
  coord_t m_max_dist, m_inv_max_dist_sq2;
  precomputed_function<coord_t> m_inverted_ratio;
  /* kr0 + (kr1 * r^2) + (kr2 * r^4) + (kr3 * r^6)  */
  coord_t get_ratio (coord_t rsq)
  {
    return m_params.kr[0] + rsq * (m_params.kr[1] + rsq * (m_params.kr[2] + rsq * m_params.kr[3]));
  }
  coord_t get_inverse (coord_t dist)
  {
    coord_t min = 0;
    coord_t max = m_max;
    if (!dist)
      return 0;
    while (true)
    {
      coord_t r = (min + max) / 2;
      coord_t ra = r * get_ratio (r * r * m_inv_max_dist_sq2);
      if (ra == dist || min == r || max == r)
        {
	  if (debug && fabs (ra - dist) > epsilon / 2)
	    printf ("Inexact lens inverse: %f:%f %f %f\n", dist,ra, r, r/dist);
	  return r / dist;
        }
      else if (ra < dist)
	min = r;
      else
	max = r;
    }
  }
  coord_t m_max;
  bool m_noop;
  static constexpr const bool debug = true;
  static constexpr const coord_t epsilon = 0.001;
};
#endif
