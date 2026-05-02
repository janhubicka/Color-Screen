#ifndef MESH_H
#define MESH_H
#include <memory>
#include <vector>
#include "dllpublic.h"
#include "base.h"
#include "matrix.h"

namespace colorscreen
{

/* Class implementing 2D mesh transformation.  It maps image coordinates
   to screen coordinates and vice versa using bilinear interpolation.  */
class mesh
{
  static const bool debug = colorscreen_checking;

public:
  /* Conserve memory; we do not need to be that precise here since we
     interpolate across small regions.  */
  using mesh_coord_t = float;

  /* Initialize mesh for range [XSHIFT, YSHIFT] with given XSTEP and YSTEP
     and dimensions WIDTH x HEIGHT.  */
  mesh (coord_t xshift, coord_t yshift, coord_t xstep, coord_t ystep,
        int width, int height);
  /* Initialize mesh for given AREA with given XSTEP and YSTEP.  */
  mesh (int_image_area area, coord_t xstep, coord_t ystep);

  /* Destructor.  */
  DLL_PUBLIC ~mesh ();

  /* Structure representing a point in the mesh.  */
  struct mesh_point
  {
    mesh_coord_t x, y;
  };

  /* Set point at index SRC to value DST.  */
  void
  set_point (int_point_t src, point_t dst)
  {
    if (debug && (src.y < 0 || src.y >= m_height || src.x < 0 || src.x >= m_width))
      abort ();
    m_data[src.y * m_width + src.x] = {(mesh_coord_t)dst.x, (mesh_coord_t)dst.y};
  }

  /* Get mesh point at index P.  */
  pure_attr point_t
  get_point (int_point_t p) const
  {
    if (debug && (p.y < 0 || p.y >= m_height || p.x < 0 || p.x >= m_width))
      abort ();
    return {(coord_t) m_data[p.y * m_width + p.x].x, (coord_t) m_data[p.y * m_width + p.x].y};
  }

  /* Get source coordinate of mesh point at index P.  */
  pure_attr point_t
  get_screen_point (int_point_t p) const
  {
    if (debug && (p.y < 0 || p.y >= m_height || p.x < 0 || p.x >= m_width))
      abort ();
    return {(coord_t) (p.x * m_xstep - m_xshift), (coord_t) (p.y * m_ystep - m_yshift)};
  }

  /* Apply mesh transformation to point P.  */
  point_t pure_attr
  apply (point_t p) const
  {
    int ix, iy;
    p.x = my_modf ((p.x + m_xshift) * m_xstepinv, &ix);
    p.y = my_modf ((p.y + m_yshift) * m_ystepinv, &iy);
    if (ix < 0)
      {
        p.x = 0;
        ix = 0;
      }
    if (iy < 0)
      {
        p.y = 0;
        iy = 0;
      }
    if (ix >= m_width - 1)
      {
        p.x = 1;
        ix = m_width - 2;
      }
    if (iy >= m_height - 1)
      {
        p.y = 1;
        iy = m_height - 2;
      }
    mesh_point np = { (mesh_coord_t)p.x, (mesh_coord_t)p.y };
    np = interpolate (m_data[iy * m_width + ix], m_data[iy * m_width + ix + 1],
                      m_data[(iy + 1) * m_width + ix],
                      m_data[(iy + 1) * m_width + ix + 1], np);
    return { np.x, np.y };
  }

  /* Return true if X, Y are in the range covered by mesh.  */
  bool
  in_range_p (coord_t x, coord_t y) const
  {
    x = (x + m_xshift) * m_xstepinv;
    y = (y + m_yshift) * m_ystepinv;
    return (x >= 0 && y >= 0 && x < m_width && y < m_height);
  }

  /* Invert mesh transformation for point IP.  */
  point_t pure_attr
  DLL_PUBLIC invert (point_t ip) const;

  /* Determine range in image coordinates covering [X1, Y1]..[X2, Y2]
     transformed by TRANS. Result is stored in XMIN, XMAX, YMIN, YMAX.  */
  void get_range (matrix2x2<coord_t> trans, coord_t x1, coord_t y1,
                  coord_t x2, coord_t y2, coord_t *xmin,
                  coord_t *xmax, coord_t *ymin, coord_t *ymax) const;

  /* Print mesh content to file F.  */
  void print (FILE *f) const;

  /* Precompute inverse lookup table.  */
  DLL_PUBLIC void precompute_inverse () const;

  /* Compute an inverse mesh. If AREA is provided and set, the inverse mesh covers the specified area in image coordinates.
     Otherwise, it computes the inverse mesh for the bounding box of the whole original mesh target coordinates.
     XSTEPS and YSTEPS are automatically chosen to be reasonably precise without increasing the mesh size by more than 32 times. */
  DLL_PUBLIC std::shared_ptr<mesh> compute_inverse (int_optional_image_area area = {}, class progress_info *progress = nullptr) const;

  /* Helper to perform the actual inverse mesh computation without using the cache.  */
  DLL_PUBLIC std::unique_ptr<mesh> compute_inverse_uncached (int_optional_image_area area = {}, class progress_info *progress = nullptr) const;

  /* Apply matrix TRANS to every point in the mesh and return a new mesh.  */
  DLL_PUBLIC std::unique_ptr<mesh> transformed (matrix3x3<coord_t> trans) const;

  /* Grow mesh by given number of points to LEFT, RIGHT, TOP and BOTTOM.  */
  bool grow (int left, int right, int top, int bottom);

  /* Return true if mesh needs to grow in given direction to cover image
     of WIDTH x HEIGHT.  */
  pure_attr bool need_to_grow_left (int width, int height) const;
  pure_attr bool need_to_grow_top (int width, int height) const;
  pure_attr bool need_to_grow_right (int width, int height) const;
  pure_attr bool need_to_grow_bottom (int width, int height) const;

  /* Return width of the mesh in points.  */
  int get_width () const
  {
    return m_width;
  }

  /* Return height of the mesh in points.  */
  int
  get_height () const
  {
    return m_height;
  }

  /* Return shift in X coordinate.  */
  coord_t
  get_xshift () const
  {
    return m_xshift;
  }

  /* Return shift in Y coordinate.  */
  coord_t
  get_yshift () const
  {
    return m_yshift;
  }

  /* Return step in X coordinate.  */
  coord_t
  get_xstep () const
  {
    return m_xstep;
  }

  /* Return step in Y coordinate.  */
  coord_t
  get_ystep () const
  {
    return m_ystep;
  }

  /* Save mesh content to file F.  */
  bool save (FILE *f) const;

  /* Load mesh content from file F. Store description of error in ERROR if any.  */
  static std::unique_ptr <mesh> load (FILE *f, const char **error);

  /* Unique id of the mesh (used for caching).  */
  uint64_t id;

  /* Disable copying.  */
  mesh (const mesh &) = delete;
  mesh &operator= (const mesh &) = delete;

  /* Default move semantics.  */
  mesh (mesh &&) = default;
  mesh &operator= (mesh &&) = default;

private:
  /* Structure used for inverse lookup table.  */
  struct mesh_inverse
  {
    unsigned int minx, miny, maxx, maxy;
  };

  /* Mesh point data.  */
  std::vector<mesh_point> m_data;

  /* Inverse lookup data.  */
  mutable std::vector<mesh_inverse> m_invdata;

  mesh_coord_t m_xshift, m_yshift, m_xstep, m_ystep, m_xstepinv, m_ystepinv;
  int m_width, m_height;
  mutable mesh_coord_t m_invxshift, m_invyshift, m_invxstep, m_invystep, m_invxstepinv,
      m_invystepinv;
  mutable int m_invwidth, m_invheight;

  /* Return true if entry E is useful for image range [XMIN, XMAX, YMIN, YMAX].  */
  inline bool
  entry_useful_p (int_point_t e, int xmin, int xmax, int ymin, int ymax) const
  {
    for (int yy = std::max ((int)e.y - 1, 0); yy < std::min ((int)e.y + 2, m_height); yy++)
      for (int xx = std::max ((int)e.x - 1, 0); xx < std::min ((int)e.x + 2, m_width); xx++)
        {
	  point_t p = get_point ({xx, yy});
	  if (p.x >= xmin && p.x <= xmax && p.y >= ymin && p.y < ymax)
	    return true;
        }
    return false;
  }

  /* Compute oriented sign of triangle P1, P2, P3.  */
  static mesh_coord_t
  sign (mesh_point p1, mesh_point p2, mesh_point p3)
  {
    return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
  }

  /* Compute A, B such that
     X1 + DX1 * A = X2 + DX2 * B
     Y1 + DY1 * A = Y2 + DY2 * B  */
  static void
  intersect_vectors (mesh_coord_t x1, mesh_coord_t y1, mesh_coord_t dx1,
                     mesh_coord_t dy1, mesh_coord_t x2, mesh_coord_t y2,
                     mesh_coord_t dx2, mesh_coord_t dy2, mesh_coord_t *a,
                     mesh_coord_t *b)
  {
    matrix2x2<mesh_coord_t> m (dx1, -dx2, dy1, -dy2);
    m = m.invert ();
    m.apply_to_vector (x2 - x1, y2 - y1, a, b);
  }

  /* Smoothly map triangle (0,0), (1,0), (1,1) to triangle Z, X, Y.  */
  static mesh_point const_attr
  triangle_interpolate (mesh_point z, mesh_point x, mesh_point y, mesh_point p)
  {
    mesh_point ret;
    if (p.x != 0)
      {
        mesh_coord_t yp = p.y / p.x;
        mesh_coord_t x1 = x.x * (1 - yp) + y.x * yp;
        mesh_coord_t y1 = x.y * (1 - yp) + y.y * yp;
        ret.x = z.x * (1 - p.x) + x1 * p.x;
        ret.y = z.y * (1 - p.x) + y1 * p.x;
      }
    else
      {
        ret.x = z.x;
        ret.y = z.y;
      }
    return ret;
  }

  /* Bilinearly interpolate point P within square defined by TL, TR, BL, BR.  */
  static mesh_point const_attr
  interpolate (mesh_point tl, mesh_point tr, mesh_point bl, mesh_point br,
               mesh_point p)
  {
    bool swap = (p.x < p.y);
    if (swap)
      {
        std::swap (p.x, p.y);
        std::swap (tr, bl);
      }
    p = triangle_interpolate (tl, tr, br, p);
    return p;
  }

  /* Find coordinate in range [X1, Y1]..[X2, Y2] that is close to entry (X, Y).  */
  point_t push_to_range (int x, int y, coord_t x1, coord_t y1, coord_t x2, coord_t y2) const;
};
}
#endif
