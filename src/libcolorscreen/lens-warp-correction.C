/* Lens warp correction implementation.
   Precomputes and caches the inverse radial distortion mapping using an
   LRU-cached lookup table for fast scan-to-corrected coordinate transforms.  */
#include "include/lens-correction.h"
#include "lru-cache.h"
namespace colorscreen
{
namespace
{
/* Parameters passed to the LRU cache for computing inverse mapping.  */
struct lens_inverse_parameters
{
  lens_warp_correction_parameters param;
  coord_t max_dist;
  coord_t inv_max_dist_sq2;

  /* Return true if this cache parameter set is equal to O.  */
  bool
  operator== (const lens_inverse_parameters &o) const
  {
    return param == o.param
           && max_dist == o.max_dist
           && inv_max_dist_sq2 == o.inv_max_dist_sq2;
  }
};

/* Compute the inverse radial distortion for DIST by binary search.
   PARAM are the lens parameters.
   MAX is the maximum search radius.
   INV_MAX_DIST_SQ2 is 1 / (max_dist^2) for normalization.
   Returns the ratio R / DIST such that corrected_to_scan(R) == DIST.  */
pure_attr inline coord_t
get_inverse (const lens_warp_correction_parameters &param, coord_t dist,
             coord_t max, coord_t inv_max_dist_sq2)
{
  coord_t min = 0;
  if (!dist)
    return 1;
  while (true)
    {
      coord_t r = (min + max) * (coord_t)0.5;
      coord_t ra = r * param.get_ratio (r * r * inv_max_dist_sq2);
      if (my_fabs (ra - dist) < 1.0 / (4 * 65536.0) || min == r || max == r)
        {
          if (lens_warp_correction::debug
              && my_fabs (ra - dist) > lens_warp_correction::epsilon / 2)
            printf ("Inexact lens inverse: %f:%f %f %f %ff\n", dist, ra, r,
                    r / dist, my_sqrt (r * r * inv_max_dist_sq2));
          return r / dist;
        }
      else if (ra < dist)
        min = r;
      else
        max = r;
    }
}

/* Return the upper corrected/output radius needed while inverting source
   radii up to MAX_DIST for PARAM.

   DNG defines the polynomial through normalized radius one.  Color-Screen's
   get_ratio() keeps the edge ratio constant outside that domain.  Therefore,
   when the edge ratio is below one, source radius MAX_DIST corresponds to
   corrected radius MAX_DIST / edge_ratio.  The direct inverse and cached
   inverse must use the same domain.  */
pure_attr inline coord_t
inverse_search_limit (const lens_warp_correction_parameters &param,
                      coord_t max_dist)
{
  const coord_t edge_ratio = param.get_ratio (1);
  if (!(edge_ratio > 0) || !my_isfinite (edge_ratio)
      || !(max_dist > 0) || !my_isfinite (max_dist))
    return 0;
  return edge_ratio < 1 ? max_dist / edge_ratio : max_dist;
}

/* Precompute the inverse radial distortion function for parameters P.
   Provides progress updates via PROG.  */
std::unique_ptr<precomputed_function<coord_t>>
get_new_inverse (struct lens_inverse_parameters &p, progress_info *prog)
{
  (void)prog;

  const coord_t max = inverse_search_limit (p.param, p.max_dist);
  if (!(max > 0) || !my_isfinite (max))
    return nullptr;

  /* Now precompute inverse.  */
  return std::make_unique<precomputed_function<coord_t>> (
      0, p.max_dist, lens_warp_correction::size,
      [&] (coord_t x) {
        return get_inverse (p.param, x, max, p.inv_max_dist_sq2);
      },
      true);
}
static lru_cache<lens_inverse_parameters, precomputed_function<coord_t>,
                 get_new_inverse, 4>
    lens_inverse_cache ("lens inverse functions");
}

/* Precompute everything needed to apply lens distortion.
   CENTER is lens center in image coordinates.
   C1, C2, C3 and C4 are corners of the scan.
   Returns true on success.  */
bool
lens_warp_correction::precompute (point_t center, point_t c1, point_t c2,
                                   point_t c3, point_t c4)
{
  assert (!m_inverted_ratio);
  if (m_params.is_noop ())
    {
      m_noop = true;
      return true;
    }
  if (!m_params.is_monotone ())
    return false;
  m_noop = false;
  const coord_t max_dist_sq2
      = std::max (c1.dist_sq2_from (center),
                  std::max (c2.dist_sq2_from (center),
                            std::max (c3.dist_sq2_from (center),
                                      c4.dist_sq2_from (center))));
  m_center = center;
  if (!(max_dist_sq2 > 0) || !my_isfinite (max_dist_sq2))
    return false;

  /* Keep the squared normalization before taking the square root.  Squaring
     M_MAX_DIST again needlessly perturbs the rectangle-corner squared radius
     and, under fast-math, can make the DNG forward mapping target-dependent.  */
  m_max_dist = my_sqrt (max_dist_sq2);
  m_inv_max_dist_sq2 = 1 / max_dist_sq2;
  return my_isfinite (m_max_dist) && my_isfinite (m_inv_max_dist_sq2);
}

/* Precompute the inverse radial distortion function lookup table.
   Returns true on success.  */
bool
lens_warp_correction::precompute_inverse ()
{
  if (m_noop)
    return true;
  lens_inverse_parameters p
      = { m_params, m_max_dist, m_inv_max_dist_sq2 };
  m_inverted_ratio = lens_inverse_cache.get (p, nullptr);
  return m_inverted_ratio != nullptr;
}

lens_warp_correction::~lens_warp_correction ()
{
}

/* Transform point P from scan coordinates to corrected image coordinates
   without using the precomputed table.  Uses binary search for inversion.  */
pure_attr point_t
lens_warp_correction::nonprecomputed_scan_to_corrected (point_t p) const
{
  if (m_noop)
    return p;
  bool too_far = false;
  coord_t dist = p.dist_from (m_center);
  const coord_t max_search = inverse_search_limit (m_params, m_max_dist);
  if (dist > m_max_dist)
    dist = m_max_dist, too_far = true;

  point_t ret
      = (p - m_center)
            * get_inverse (m_params, dist, max_search, m_inv_max_dist_sq2)
        + m_center;
  if (debug && !too_far)
    {
      point_t orig = corrected_to_scan (ret);
      if (!p.almost_eq (orig, epsilon))
        fprintf (stderr,
                 "Lens correction inverse broken %f, %f -> %f, %f -> %f, "
                 "%f; dist %f ratio %f\n",
                 p.x - m_center.x, p.y - m_center.y, ret.x - m_center.x,
                 ret.y - m_center.y, orig.x - m_center.x, orig.y - m_center.y,
                 dist,
                 m_inverted_ratio
                     ? m_inverted_ratio->apply (p.dist_from (m_center))
                     : (coord_t)-1.0);
    }
  return ret;
}
}
