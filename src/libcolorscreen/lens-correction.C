#include "include/lens-correction.h"
coord_t
lens_warp_correction::get_inverse (coord_t dist)
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
bool
lens_warp_correction::precompute (point_t center, point_t c1, point_t c2, point_t c3, point_t c4, bool need_inverse = true)
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
