#include "include/lens-correction.h"
#include "lru-cache.h"
namespace {
struct lens_inverse_parameters
{
  lens_warp_correction_parameters param;
  coord_t max_dist;

  bool
  operator==(lens_inverse_parameters &o)
  {
    return param == o.param && max_dist == o.max_dist;
  }
};
coord_t
get_inverse (lens_warp_correction_parameters &param, coord_t dist, coord_t max, coord_t inv_max_dist_sq2)
{
  coord_t min = 0;
  if (!dist)
    return 1;
  while (true)
  {
    coord_t r = (min + max) / 2;
    coord_t ra = r * param.get_ratio (r * r * inv_max_dist_sq2);
    if (ra == dist || min == r || max == r)
      {
	if (lens_warp_correction::debug && fabs (ra - dist) > lens_warp_correction::epsilon / 2)
	  printf ("Inexact lens inverse: %f:%f %f %f %ff\n", dist,ra, r, r/dist, sqrt (r * r * inv_max_dist_sq2));
	return r / dist;
      }
    else if (ra < dist)
      min = r;
    else
      max = r;
  }
}
precomputed_function<coord_t> *
get_new_inverse (struct lens_inverse_parameters &p, progress_info *)
{
  coord_t data[lens_warp_correction::size];
  coord_t inv_max_dist_sq2 = 1 / (p.max_dist * p.max_dist);

  /* Determine the start of binary search for computing inverse.
     For correction to make sense, get_ratio must be monotonously increasing
     for whole image area.  For negative correction coefficient the function
     is not monotonously increasing for large values.  So search carefully
     for max element which inverts to m_max_dist. 
    
     In common case the lens parameters will be normalized so ratio of 1 is 1.
     In this case max is max_dist. */
  coord_t max = 1;
  if (fabs (p.param.get_ratio (1) - 1) < lens_warp_correction::epsilon / p.max_dist)
    max = p.max_dist;
  else
    {
      coord_t last = 0;
      coord_t next;
      while ((next = max * p.param.get_ratio (max * max * inv_max_dist_sq2)) < p.max_dist)
	{
	  /* Did we reach point where function decreases now?
	     This means that parameters are broken, but we can cap search and get
	     somewhat sane results.  */
	  if (last > next)
	     break;
	  max = 1.2 * max;
	  last = next;
	}
    }

  /* Now precompute inverse.  */
#pragma omp parallel for default (none) shared (p,data,inv_max_dist_sq2,max)
  for (int i = 0; i < lens_warp_correction::size; i++)
    data[i] = get_inverse (p.param, i * p.max_dist / (lens_warp_correction::size - 1), max, inv_max_dist_sq2);
  return new precomputed_function<coord_t> (0, p.max_dist, data, lens_warp_correction::size);
}
static lru_cache <lens_inverse_parameters, precomputed_function<coord_t>, get_new_inverse, 4> lens_inverse_cache ("lens inverse functions");
}

bool
lens_warp_correction::precompute (point_t center, point_t c1, point_t c2, point_t c3, point_t c4, bool need_inverse)
{
  assert (!m_inverted_ratio);
  if (m_params.is_noop ())
    {
      m_noop = true;
      return true;
    }
  m_noop = false;
  m_max_dist = std::max (c1.dist_from (center), std::max (c2.dist_from (center), std::max (c3.dist_from (center), c4.dist_from (center))));
  m_center = center;
  m_inv_max_dist_sq2 = 1 / (m_max_dist * m_max_dist);
  if (need_inverse)
    {
      lens_inverse_parameters p = {m_params, m_max_dist};
      m_inverted_ratio = lens_inverse_cache.get (p, NULL);
    }
  return true;
}

lens_warp_correction::~lens_warp_correction ()
{
  if (m_inverted_ratio)
    lens_inverse_cache.release (m_inverted_ratio);
}
