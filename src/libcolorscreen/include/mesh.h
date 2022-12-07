#ifndef MESH_H
#define MESH_H
#include "matrix.h"
#include "render.h"


struct mesh_point
{
	coord_t x,y;
};
/* Compute a, b such that
   x1 + dx1 * a = x2 + dx2 * b
   y1 + dy1 * a = y2 + dy2 * b  */
inline void
intersect_vectors (coord_t x1, coord_t y1, coord_t dx1, coord_t dy1,
		   coord_t x2, coord_t y2, coord_t dx2, coord_t dy2,
		   coord_t *a, coord_t *b)
{
  matrix2x2<coord_t> m (dx1, dy1,
			-dx2, -dy2);
  m = m.invert ();
  m.apply_to_vector (x2 - x1, y2 - y1, a, b);
#if 0
  m.print (stderr);
  printf ("%f %f\n", x1 + dx1 * *a, x2 + dx2 * *b);
  printf ("%f %f\n", y1 + dy1 * *a, y2 + dy2 * *b);
#endif
}

/* Smoothly map triangle (0,0), (1,0), (1,1) to trangle z, x, y */

inline mesh_point
triangle_interpolate (mesh_point z, mesh_point x, mesh_point y, mesh_point p)
{
  mesh_point ret;
  if (p.x != 0)
    {
      coord_t yp = p.y / p.x;
      coord_t x1 = x.x * (1 - yp) + y.x * yp;
      coord_t y1 = x.y * (1 - yp) + y.y * yp;
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

/* Inverse of triangle_intrepolate.  */

inline mesh_point
inverse_triangle_interpolate (mesh_point z, mesh_point x, mesh_point y, mesh_point p)
{
  mesh_point ret;
  if (p.x != z.x || p.y != z.y)
    {
      /* Simplify so things z is (0.0).  */
      x.x -= z.x;
      x.y -= z.y;
      y.x -= z.x;
      y.y -= z.y;
      p.x -= z.x;
      p.y -= z.y;
      intersect_vectors (0, 0, p.x, p.y, x.x, x.y, y.x - x.x, y.y - x.y, &ret.x, &ret.y);
      ret.x = 1 / ret.x;
      ret.y *= ret.x;
    }
  else
    {
      ret.x = 0;
      ret.y = 0;
    }
  return ret;
}

/* tl is a top left point, tr is top right, bl is bottom left and br is bottom right point
   of a square cell.  Interpolate point p accordingly.  */
inline mesh_point
mesh_interpolate (mesh_point tl, mesh_point tr, mesh_point bl, mesh_point br, mesh_point p)
{
  bool swap = (p.x < p.y);
  //printf ("s:%i",swap);
  if (swap)
    {
      std::swap (p.x, p.y);
      std::swap (tr, bl);
    }
  p = triangle_interpolate (tl, tr, br, p);
  //if (swap)
    //std::swap (p.x, p.y);
  return p;
}

inline mesh_point
mesh_inverse_interpolate (mesh_point tl, mesh_point tr, mesh_point bl, mesh_point br, mesh_point p)
{
  if (p.x == tl.x && p.y == tl.y)
    {
      mesh_point ret = {0, 0};
      return ret;
    }
  bool swap = (p.x - tl.x) > 0 ? (((p.y - tl.y) * (br.x - tl.x) / (p.x - tl.x) ) > (br.y - tl.y)) : (p.y - tl.y) > 0;
  //printf ("t:%i",swap);
  if (swap)
    {
      //std::swap (p.x, p.y);
      std::swap (tr, bl);
    }
  mesh_point ret = inverse_triangle_interpolate (tl, tr, br, p);
  if (swap)
    std::swap (ret.x, ret.y);
  return ret;
}

class mesh
{
public:
  mesh(coord_t xshift, coord_t yshift, coord_t xstep, coord_t ystep, int width, int height)
  : m_data (NULL), m_invdata (NULL), m_xshift (xshift), m_yshift (yshift), m_xstep (xstep), m_ystep (ystep), m_xstepinv (1/xstep), m_ystepinv (1/ystep), m_width (width), m_height (height)
  {
    m_data = (mesh_point *)malloc (width * height * sizeof (mesh_point));
  }
  ~mesh()
  {
    free (m_data);
    free (m_invdata);
  }
  void
  set_point (int x, int y, coord_t xx, coord_t yy)
  {
    m_data[y * m_width + x].x = xx;
    m_data[y * m_width + x].y = yy;
  }
  void
  get_point (int x, int y, coord_t *xx, coord_t *yy)
  {
    *xx = m_data[y * m_width + x].x;
    *yy = m_data[y * m_width + x].y;
  }
  void
  apply (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    int ix, iy;
    x= my_modf ((x + m_xshift) * m_xstepinv, &ix);
    y= my_modf ((y + m_yshift) * m_ystepinv, &iy);
    if (ix < 0)
      {
	//x += ix * m_xstep;
	x = 0;
	ix = 0;
      }
    if (iy < 0)
      {
	//y += iy * m_ystep;
	y = 0;
	iy = 0;
      }
    if (ix >= m_width - 1)
      {
	//x -= ix * (ix - m_width + 1);
	x = 1;
	ix = m_width - 2;
      }
    if (iy >= m_height - 1)
      {
	//y -= iy * (iy - m_height + 1);
	y = 1;
	iy = m_height - 2;
      }
    mesh_point p = {x, y};
    p = mesh_interpolate (m_data[iy * m_width + ix], m_data[iy * m_width + ix + 1], m_data[(iy + 1) * m_width + ix], m_data[(iy + 1) * m_width + ix + 1], p);
    *xx = p.x;
    *yy = p.y;
  }
  void
  invert (coord_t x, coord_t y, coord_t *xx, coord_t *yy)
  {
    mesh_point p = {x, y};
    int ix = (x + m_invxshift) * m_invxstepinv;
    int iy = (y + m_invyshift) * m_invystepinv;
    if (ix >= 0 && iy >= 0 && ix < m_width - 1 && iy < m_height - 1)
      {
	int pp = iy * m_invwidth + ix;
	for (int y = m_invdata[pp].miny; y <= (int)m_invdata[pp].maxy; y++)
	  for (int x = m_invdata[pp].minx; x <= (int)m_invdata[pp].maxx; x++)
	    {
	      mesh_point q = mesh_inverse_interpolate (m_data[y * m_width + x], m_data[y * m_width + x + 1], m_data[(y + 1) * m_width + x], m_data[(y + 1) * m_width + x + 1], p);
#if 0
	      if ((!x || q.x >= 0)
		  && (!y || q.y >= 0)
		  && (x == m_width - 2 || q.x <= 1)
		  && (y == m_height - 2 || q.y <= 1))
#endif
	      if (q.x >= 0 && q.x <= 1 && q.y >= 0 && q.y <= 1)
		{
		  *xx = (q.x + x) * m_xstep - m_xshift;
		  *yy = (q.y + y) * m_ystep - m_yshift;
		  return;
		}
	    }
      }
    if (ix < m_invwidth / 2)
      *xx = -m_xshift;
    else
      *xx = -m_xshift + m_xstep * m_width;
    if (iy < m_invwidth / 2)
      *yy = -m_yshift;
    else
      *yy = -m_yshift + m_ystep * m_height;
  }
  void
  print (FILE *f);
  void precompute_inverse();
private:
  struct mesh_inverse
    {
      unsigned int minx, miny, maxx, maxy;
    };
  mesh_point *m_data;
  mesh_inverse *m_invdata;
  coord_t m_xshift, m_yshift, m_xstep, m_ystep, m_xstepinv, m_ystepinv;
  int m_width, m_height;
  coord_t m_invxshift, m_invyshift, m_invxstep, m_invystep, m_invxstepinv, m_invystepinv;
  int m_invwidth, m_invheight;
};
#endif
