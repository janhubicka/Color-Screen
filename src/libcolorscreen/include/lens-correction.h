#ifndef LENS_CORRECTION_H
#define LENS_CORRECTION_H
#include <cstdio>
#include "base.h"
#include "precomputed-function.h"
#include "progress-info.h"
struct lens_warp_correction_parameters
{
  /* Radial correction coefficients, same as in the DNG specs.  */
  coord_t kr[4];
  /* Center in relative coordinates 0...1  */
  point_t center;

  constexpr lens_warp_correction_parameters ()
  : kr {1, 0, 0, 0}, center ({0.5,0.5})
  { }

  bool
  operator== (lens_warp_correction_parameters &other) const
  {
    return center == other.center
	   && kr[0] == other.kr[0]
	   && kr[1] == other.kr[1]
	   && kr[2] == other.kr[2]
	   && kr[3] == other.kr[3];
  }
  bool
  is_noop ()
  {
    return kr[0] == 1 && kr[1] == 0 && kr[2] == 0 && kr[3] == 0;
  }
  /* kr0 + (kr1 * r^2) + (kr2 * r^4) + (kr3 * r^6)  */
  coord_t
  get_ratio (coord_t rsq)
  {
    if (rsq > 1)
      rsq = 1;
    return kr[0] + rsq * (kr[1] + rsq * (kr[2] + rsq * kr[3]));
  }

  pure_attr bool
  is_monotone ()
  {
    coord_t l = 0;
    for (int i = 1; true; i++)
      {
	if (i > 1024*1024)
	  return false;
	coord_t p = i * (((coord_t)1)/1024);
	coord_t nl = p * get_ratio (p);
	if (!(nl > l))
	  return false;
	if (nl > 1)
	  break;
	l = nl;
      }
    return true;
  }
  void normalize ()
  {
    coord_t c = 1 / get_ratio (1);
    kr[0] *= c;
    kr[1] *= c;
    kr[2] *= c;
    kr[3] *= c;
    if (fabs (1 - get_ratio (1)) > 0.00001)
      abort ();
  }

};

struct lens_warp_correction
{
  static constexpr const bool debug = false;
  /* Size of table for inverse function.  16*1024 is probably overkill but
     should have one entry for every pixel.  */
  static constexpr const int size = 16 * 1024;
  /* Error tolerated when looking for inverse.  */
  static constexpr const coord_t epsilon = 0.001;

  lens_warp_correction ()
  : m_params (), m_inverted_ratio (NULL)
  { }

  ~lens_warp_correction ();

  void
  set_parameters (const lens_warp_correction_parameters &p)
  {
    m_params = p;
  }
  
  bool precompute (point_t center, point_t c1, point_t c2, point_t c3, point_t c4, bool need_inverse = true);

  inline pure_attr 
  point_t corrected_to_scan (point_t p)
  {
    if (m_noop)
      return p;
    /* Radial warp correction.  */
    coord_t ratio = m_params.get_ratio (p.dist_sq2_from (m_center) * m_inv_max_dist_sq2);
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
    bool too_far = false;
    coord_t dist = p.dist_from (m_center);
    if (dist > m_max_dist)
      dist = m_max_dist, too_far = true;
    point_t ret = (p - m_center) * m_inverted_ratio->apply (dist) + m_center;
    if (debug && !too_far)
      {
	point_t orig = corrected_to_scan (ret);
	if (!p.almost_eq (orig, epsilon))
	  fprintf (stderr, "Lens correction inverse broken %f, %f -> %f, %f -> %f, %f; dist %f ratio %f\n", p.x - m_center.x, p.y - m_center.y, ret.x - m_center.x, ret.y - m_center.y, orig.x - m_center.x, orig.y - m_center.y, dist, m_inverted_ratio->apply (p.dist_from (m_center)));
      }
    return ret;
  }

  pure_attr bool
  is_noop ()
  {
    return m_noop;
  }

private:
  lens_warp_correction_parameters m_params;
  /* Center in image coordinates, possibly after applying motor corrections.  */
  point_t m_center;
  coord_t m_max_dist, m_inv_max_dist_sq2;
  precomputed_function<coord_t> *m_inverted_ratio;
  //coord_t get_inverse (coord_t dist);
  coord_t m_max;
  bool m_noop;
};
#endif
