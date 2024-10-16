#ifndef MESH_H
#define MESH_H
#include <memory>
#include "dllpublic.h"
#include "base.h"
#include "matrix.h"

namespace colorscreen
{

class mesh
{
  static const bool debug = colorscreen_checking;

public:
  /* Conserve memory; we do not need to be that precise here since we
     interpolate across small regions.  */
  typedef float mesh_coord_t;

  mesh (coord_t xshift, coord_t yshift, coord_t xstep, coord_t ystep,
        int width, int height);
  DLL_PUBLIC ~mesh ();
  struct mesh_point
  {
    mesh_coord_t x, y;
  };
  void
  set_point (int_point_t src, point_t dst)
  {
    if (debug && (src.y < 0 || src.y >= m_height || src.x < 0 || src.x >= m_width))
      abort ();
    m_data[src.y * m_width + src.x] = {(mesh_coord_t)dst.x, (mesh_coord_t)dst.y};
  }
  pure_attr point_t
  get_point (int_point_t p) const
  {
    if (debug && (p.y < 0 || p.y >= m_height || p.x < 0 || p.x >= m_width))
      abort ();
    return {(coord_t) m_data[p.y * m_width + p.x].x, (coord_t) m_data[p.y * m_width + p.x].y};
  }
  pure_attr point_t
  get_screen_point (int_point_t p) const
  {
    if (debug && (p.y < 0 || p.y >= m_height || p.x < 0 || p.x >= m_width))
      abort ();
    return {(coord_t) (p.x * m_xstep - m_xshift), (coord_t) (coord_t) (p.y * m_ystep - m_yshift)};
  }
  point_t pure_attr
  apply (point_t p) const
  {
    int ix, iy;
    p.x = my_modf ((p.x + m_xshift) * m_xstepinv, &ix);
    p.y = my_modf ((p.y + m_yshift) * m_ystepinv, &iy);
    if (ix < 0)
      {
        // x += ix * m_xstep;
        p.x = 0;
        ix = 0;
      }
    if (iy < 0)
      {
        // y += iy * m_ystep;
        p.y = 0;
        iy = 0;
      }
    if (ix >= m_width - 1)
      {
        // x -= ix * (ix - m_width + 1);
        p.x = 1;
        ix = m_width - 2;
      }
    if (iy >= m_height - 1)
      {
        // y -= iy * (iy - m_height + 1);
        p.y = 1;
        iy = m_height - 2;
      }
    mesh_point np = { (mesh_coord_t)p.x, (mesh_coord_t)p.y };
    np = interpolate (m_data[iy * m_width + ix], m_data[iy * m_width + ix + 1],
                      m_data[(iy + 1) * m_width + ix],
                      m_data[(iy + 1) * m_width + ix + 1], np);
    return { np.x, np.y };
  }
  /* Return true if x, y are in the range covered by mesh.  */
  bool
  in_range_p (coord_t x, coord_t y) const
  {
    x = (x + m_xshift) * m_xstepinv;
    y = (y + m_yshift) * m_ystepinv;
    return (x >= 0 && y >= 0 && x < m_width && y < m_height);
  }
  point_t pure_attr
  invert (point_t ip) const
  {
    mesh_point p = { (mesh_coord_t)ip.x, (mesh_coord_t)ip.y };
    int ix = (ip.x + m_invxshift) * m_invxstepinv;
    int iy = (ip.y + m_invyshift) * m_invystepinv;
    if (ix >= 0 && iy >= 0 && ix < m_width - 1 && iy < m_height - 1)
      {
        int pp = iy * m_invwidth + ix;
        for (int y = m_invdata[pp].miny; y <= (int)m_invdata[pp].maxy; y++)
          for (int x = m_invdata[pp].minx; x <= (int)m_invdata[pp].maxx; x++)
            {
              /* Determine cell corners.  */
              mesh_point p1 = m_data[y * m_width + x];
              mesh_point p2 = m_data[y * m_width + x + 1];
              mesh_point p3 = m_data[(y + 1) * m_width + x];
              mesh_point p4 = m_data[(y + 1) * m_width + x + 1];

              /* Check if point is above or bellow diagonal.  */
              mesh_coord_t sgn1 = sign (p, p1, p4);
              if (sgn1 > 0)
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p3) < 0 || sign (p, p3, p1) < 0)
                    continue;
                  mesh_coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p3.x,
                                     p3.y, p4.x - p3.x, p4.y - p3.y, &rx, &ry);
                  rx = 1 / rx;
                  return { (ry * rx + x) * m_xstep - m_xshift,
                           (rx + y) * m_ystep - m_yshift };
                }
              else
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p2) > 0 || sign (p, p2, p1) > 0)
                    continue;
                  mesh_coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p2.x,
                                     p2.y, p4.x - p2.x, p4.y - p2.y, &rx, &ry);
                  rx = 1 / rx;
                  return { (rx + x) * m_xstep - m_xshift,
                           (ry * rx + y) * m_ystep - m_yshift };
                }
            }
      }
    point_t ret;
    if (ix < m_invwidth / 2)
      ret.x = -m_xshift;
    else
      ret.x = -m_xshift + m_xstep * m_width;
    if (iy < m_invwidth / 2)
      ret.y = -m_yshift;
    else
      ret.y = -m_yshift + m_ystep * m_height;
    return ret;
  }
  void get_range (matrix2x2<coord_t> trans, coord_t x1, coord_t y1,
                  coord_t x2, coord_t y2, coord_t *xmin,
                  coord_t *xmax, coord_t *ymin, coord_t *ymax) const;
  void print (FILE *f) const;
  void precompute_inverse ();
  bool grow (int left, int right, int top, int bottom);
  pure_attr bool need_to_grow_left (int width, int height) const;
  pure_attr bool need_to_grow_top (int width, int height) const;
  pure_attr bool need_to_grow_right (int width, int height) const;
  pure_attr bool need_to_grow_bottom (int width, int height) const;
  int get_width () const
  {
    return m_width;
  }
  int
  get_height () const
  {
    return m_height;
  }
  coord_t
  get_xshift () const
  {
    return m_xshift;
  }
  coord_t
  get_yshift () const
  {
    return m_yshift;
  }
  coord_t
  get_xstep () const
  {
    return m_xstep;
  }
  coord_t
  get_ystep () const
  {
    return m_ystep;
  }
  bool save (FILE *f) const;
  static std::unique_ptr <mesh> load (FILE *f, const char **error);
  /* Unique id of the mesh (used for caching).  */
  uint64_t id;

private:
  struct mesh_inverse
  {
    unsigned int minx, miny, maxx, maxy;
  };
  mesh_point *m_data;
  mesh_inverse *m_invdata;
  mesh_coord_t m_xshift, m_yshift, m_xstep, m_ystep, m_xstepinv, m_ystepinv;
  int m_width, m_height;
  mesh_coord_t m_invxshift, m_invyshift, m_invxstep, m_invystep, m_invxstepinv,
      m_invystepinv;
  int m_invwidth, m_invheight;

  /* Entry is useful if it is in the image area or its neightbours are.  */
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

  static mesh_coord_t
  sign (mesh_point p1, mesh_point p2, mesh_point p3)
  {
    return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
  }
  /* Compute a, b such that
     x1 + dx1 * a = x2 + dx2 * b
     y1 + dy1 * a = y2 + dy2 * b  */
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

  /* Smoothly map triangle (0,0), (1,0), (1,1) to trangle z, x, y */

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

  /* tl is a top left point, tr is top right, bl is bottom left and br is
     bottom right point of a square cell.  Interpolate point p accordingly.  */
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
  point_t push_to_range (int x, int y, coord_t x1, coord_t y1, coord_t x2, coord_t y2) const;
};
}
#endif
