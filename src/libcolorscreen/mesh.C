#include "include/mesh.h"
#include "lru-cache.h"
#include "loadsave.h"
mesh::mesh (coord_t xshift, coord_t yshift, coord_t xstep, coord_t ystep,
            int width, int height)
    : id (lru_caches::get ()), m_data (NULL), m_invdata (NULL),
      m_xshift (xshift), m_yshift (yshift), m_xstep (xstep), m_ystep (ystep),
      m_xstepinv (1 / xstep), m_ystepinv (1 / ystep), m_width (width),
      m_height (height)
{
  m_data = (mesh_point *)malloc (width * height * sizeof (mesh_point));
}
mesh::~mesh ()
{
  free (m_data);
  free (m_invdata);
}
void
mesh::print (FILE *f) const
{
  fprintf (f, "Mesh %ix%i shift:%fx%f steps:%fx%f\n", m_width, m_height,
           m_xshift, m_yshift, m_xstep, m_ystep);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
        {
          fprintf (f, " (%4.2f,%4.2f)", m_data[y * m_width + x].x,
                   m_data[y * m_width + x].y);
        }
      fprintf (f, "\n");
    }
}
void
mesh::precompute_inverse ()
{
  if (m_invdata)
    return;

  mesh_coord_t minx = m_data[0].x, maxx = m_data[0].x, miny = m_data[0].y,
               maxy = m_data[0].y;
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        minx = std::min (m_data[y * m_width + x].x, minx);
        maxx = std::max (m_data[y * m_width + x].x, maxx);
        miny = std::min (m_data[y * m_width + x].y, miny);
        maxy = std::max (m_data[y * m_width + x].y, maxy);
      }
  m_invxshift = -minx;
  m_invyshift = -miny;
  m_invwidth = m_width * 2;
  m_invheight = m_height * 2;
  m_invxstep = (maxx - minx) / (m_width - 1);
  m_invystep = (maxy - miny) / (m_height - 1);
  m_invxstepinv = 1 / m_invxstep;
  m_invystepinv = 1 / m_invystep;
  m_invdata = (struct mesh_inverse *)malloc (m_invwidth * m_invheight
                                             * sizeof (struct mesh_inverse));
  for (int i = 0; i < m_invwidth * m_invheight; i++)
    {
      m_invdata[i].minx = m_width;
      m_invdata[i].maxx = 0;
      m_invdata[i].miny = m_height;
      m_invdata[i].maxy = 0;
    }

  for (int y = 0; y < m_height - 1; y++)
    for (int x = 0; x < m_width - 1; x++)
      {
        mesh_coord_t minx = m_data[y * m_width + x].x;
        mesh_coord_t maxx = m_data[y * m_width + x].x;
        mesh_coord_t miny = m_data[y * m_width + x].y;
        mesh_coord_t maxy = m_data[y * m_width + x].y;

        minx = std::min (m_data[y * m_width + x + 1].x, minx);
        maxx = std::max (m_data[y * m_width + x + 1].x, maxx);
        miny = std::min (m_data[y * m_width + x + 1].y, miny);
        maxy = std::max (m_data[y * m_width + x + 1].y, maxy);
        minx = std::min (m_data[(y + 1) * m_width + x].x, minx);
        maxx = std::max (m_data[(y + 1) * m_width + x].x, maxx);
        miny = std::min (m_data[(y + 1) * m_width + x].y, miny);
        maxy = std::max (m_data[(y + 1) * m_width + x].y, maxy);
        minx = std::min (m_data[(y + 1) * m_width + x + 1].x, minx);
        maxx = std::max (m_data[(y + 1) * m_width + x + 1].x, maxx);
        miny = std::min (m_data[(y + 1) * m_width + x + 1].y, miny);
        maxy = std::max (m_data[(y + 1) * m_width + x + 1].y, maxy);

        int iminx = floor ((minx + m_invxshift) * m_invxstepinv);
        int imaxx = floor ((maxx + m_invxshift) * m_invxstepinv);
        int iminy = floor ((miny + m_invyshift) * m_invystepinv);
        int imaxy = floor ((maxy + m_invyshift) * m_invystepinv);
        if (iminx < 0 || iminx >= m_invwidth || imaxx < 0
            || imaxx >= m_invwidth || iminy < 0 || iminy >= m_invheight
            || imaxy < 0 || imaxy >= m_invheight)
          {
            printf ("%i %i %i %i\n", iminx, imaxx, iminy, imaxy);
            abort ();
          }
        for (int yy = iminy; yy <= imaxy; yy++)
          for (int xx = iminx; xx <= imaxx; xx++)
            {
              m_invdata[yy * m_invwidth + xx].minx
                  = std::min ((int)m_invdata[yy * m_invwidth + xx].minx, x);
              m_invdata[yy * m_invwidth + xx].maxx
                  = std::max ((int)m_invdata[yy * m_invwidth + xx].maxx, x);
              m_invdata[yy * m_invwidth + xx].miny
                  = std::min ((int)m_invdata[yy * m_invwidth + xx].miny, y);
              m_invdata[yy * m_invwidth + xx].maxy
                  = std::max ((int)m_invdata[yy * m_invwidth + xx].maxy, y);
            }
      }
}

/* Find mesh inverse that is close to entry (x,y) but within range of x1, y1,
 * x2, y2.  */
point_t
mesh::push_to_range (int x, int y, coord_t x1, coord_t y1, coord_t x2,
                     coord_t y2) const
{
  coord_t xx = m_data[y * m_width + x].x;
  coord_t yy = m_data[y * m_width + x].y;
  if (xx >= x1 && xx <= x2 && yy >= y1 && yy <= y2)
    return { x * m_xstep - m_xshift, y * m_ystep - m_yshift };
  if (xx < x1)
    xx = x1;
  if (xx > x2)
    xx = x2;
  if (yy < y1)
    yy = y1;
  if (yy > y2)
    yy = y2;
  return invert ({ xx, yy });
}

/* Get rectangular range of source coordinates which covers range given by
   x1,y1,x2,y2 transformed by trans in image coordinates.  */
void
mesh::get_range (matrix2x2<coord_t> trans, coord_t x1, coord_t y1, coord_t x2,
                 coord_t y2, coord_t *xmin, coord_t *xmax, coord_t *ymin,
                 coord_t *ymax) const
{
  coord_t ixmin = 0;
  coord_t ixmax = 0;
  coord_t iymin = 0;
  coord_t iymax = 0;
  bool found = false;
  for (int y = 0; y < m_height - 1; y++)
    for (int x = 0; x < m_width - 1; x++)
      {
        // matrix2x2 <mesh_coord_t> identity;
        mesh_coord_t xx, yy;
        // trans.apply_to_vector (m_data [y * m_width + x].x, m_data [y *
        // m_width + x].y, &xx, &yy);
        xx = m_data[y * m_width + x].x;
        yy = m_data[y * m_width + x].y;
        mesh_coord_t mminx = xx;
        mesh_coord_t mminy = yy;
        mesh_coord_t mmaxx = xx;
        mesh_coord_t mmaxy = yy;

        // trans.apply_to_vector (m_data [y * m_width + x + 1].x, m_data [y *
        // m_width + x + 1].y, &xx, &yy);
        xx = m_data[y * m_width + x + 1].x;
        yy = m_data[y * m_width + x + 1].y;
        mminx = std::min ((mesh_coord_t)xx, mminx);
        mmaxx = std::max ((mesh_coord_t)xx, mmaxx);
        mminy = std::min ((mesh_coord_t)yy, mminy);
        mmaxy = std::max ((mesh_coord_t)yy, mmaxy);

        // trans.apply_to_vector (m_data [(y + 1) * m_width + x].x, m_data [(y
        // + 1) * m_width + x].y, &xx, &yy);
        xx = m_data[(y + 1) * m_width + x].x;
        yy = m_data[(y + 1) * m_width + x].y;
        mminx = std::min ((mesh_coord_t)xx, mminx);
        mmaxx = std::max ((mesh_coord_t)xx, mmaxx);
        mminy = std::min ((mesh_coord_t)yy, mminy);
        mmaxy = std::max ((mesh_coord_t)yy, mmaxy);

        // trans.apply_to_vector (m_data [(y + 1) * m_width + x + 1].x, m_data
        // [(y + 1) * m_width + x + 1].y, &xx, &yy);
        xx = m_data[(y + 1) * m_width + x + 1].x;
        yy = m_data[(y + 1) * m_width + x + 1].y;
        mminx = std::min ((mesh_coord_t)xx, mminx);
        mmaxx = std::max ((mesh_coord_t)xx, mmaxx);
        mminy = std::min ((mesh_coord_t)yy, mminy);
        mmaxy = std::max ((mesh_coord_t)yy, mmaxy);

        if (x1 > mmaxx || y1 > mmaxy)
          continue;
        if (x2 < mminx || y2 < mminy)
          continue;
        coord_t px, py;
        point_t p = push_to_range (x, y, x1, y1, x2, y2);
        trans.apply_to_vector (p.x, p.y, &px, &py);
        coord_t pxmin = px;
        coord_t pxmax = px;
        coord_t pymin = py;
        coord_t pymax = py;
        p = push_to_range (x + 1, y, x1, y1, x2, y2);
        trans.apply_to_vector (p.x, p.y, &px, &py);
        pxmin = std::min (pxmin, px);
        pxmax = std::max (pxmax, px);
        pymin = std::min (pymin, py);
        pymax = std::max (pymax, py);
        p = push_to_range (x, y + 1, x1, y1, x2, y2);
        trans.apply_to_vector (p.x, p.y, &px, &py);
        pxmin = std::min (pxmin, px);
        pxmax = std::max (pxmax, px);
        pymin = std::min (pymin, py);
        pymax = std::max (pymax, py);
        p = push_to_range (x + 1, y + 1, x1, y1, x2, y2);
        trans.apply_to_vector (p.x, p.y, &px, &py);
        pxmin = std::min (pxmin, px);
        pxmax = std::max (pxmax, px);
        pymin = std::min (pymin, py);
        pymax = std::max (pymax, py);
        if (!found)
          {
            ixmin = pxmin;
            ixmax = pxmax;
            iymin = pymin;
            iymin = pymax;
            found = true;
          }
        else
          {
            ixmin = std::min (ixmin, pxmin);
            ixmax = std::max (ixmax, pxmax);
            iymin = std::min (iymin, pymin);
            iymax = std::max (iymax, pymax);
          }
      }
  *xmin = ixmin;
  *xmax = ixmax;
  *ymin = iymin;
  *ymax = iymax;
}

bool
mesh::save (FILE *f) const
{
  if (fprintf (f, "  mesh_dimensions: %i %i\n", m_width, m_height) < 0)
    return false;
  if (fprintf (f, "  mesh_shifts: %f %f\n", m_xshift, m_yshift) < 0)
    return false;
  if (fprintf (f, "  mesh_steps: %f %f\n", m_xstep, m_ystep) < 0)
    return false;
  if (fprintf (f, "  mesh_points:") < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n              ");
      for (int x = 0; x < m_width; x++)
        if (fprintf (f, " (%4.2f, %4.2f)", m_data[y * m_width + x].x,
                     m_data[y * m_width + x].y)
            < 0)
          return false;
    }
  if (fprintf (f, "\n  mesh_end\n") < 0)
    return false;
  return true;
}

mesh *
mesh::load (FILE *f, const char **error)
{
  if (!expect_keyword (f, "mesh_dimensions:"))
    {
      *error = "expected mesh_dimensions";
      return NULL;
    }
  int width, height;
  if (fscanf (f, "%i %i", &width, &height) != 2)
    {
      *error = "failed to parse mesh_dimensions";
      return NULL;
    }
  float xshift, yshift;
  if (!expect_keyword (f, "mesh_shifts:"))
    {
      *error = "expected mesh_shifts";
      return NULL;
    }
  if (fscanf (f, "%f %f", &xshift, &yshift) != 2)
    {
      *error = "failed to parse mesh_shifts";
      return NULL;
    }
  float xstep, ystep;
  if (!expect_keyword (f, "mesh_steps:"))
    {
      *error = "expected mesh_steps";
      return NULL;
    }
  if (fscanf (f, "%f %f", &xstep, &ystep) != 2)
    {
      *error = "failed to parse mesh_steps";
      return NULL;
    }
  if (!expect_keyword (f, "mesh_points:"))
    {
      *error = "expected mesh_points";
      return NULL;
    }
  mesh *m = new mesh (xshift, yshift, xstep, ystep, width, height);
  if (!m)
    {
      *error = "failed to construct mesh";
      return NULL;
    }
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
          float sx, sy;
          if (!expect_keyword (f, "(") || fscanf (f, "%f", &sx) != 1
              || !expect_keyword (f, ",") || fscanf (f, "%f", &sy) != 1
              || !expect_keyword (f, ")"))
            // if (fscanf (f, " (%f, %f)", &sx, &sy) != 2)
            {
              delete m;
              *error = "failed to parse mesh points";
              return NULL;
            }
          m->set_point ({ x, y }, { sx, sy });
        }
    }
  if (!expect_keyword (f, "mesh_end"))
    {
      delete m;
      *error = "expected mesh_end";
      return NULL;
    }
  return m;
}
bool
mesh::grow (int left, int right, int top, int bottom)
{
  int new_xshift = m_xshift + left * m_xstep;
  int new_yshift = m_yshift + top * m_ystep;
  int new_width = m_width + left + right;
  int new_height = m_height + top + bottom;
  assert (!m_invdata);
  mesh_point *new_data
      = (mesh_point *)malloc (new_width * new_height * sizeof (mesh_point));
  if (!new_data)
    return false;
  for (int y = 0; y < m_height; y++)
    memcpy (new_data + (new_width * (y + top) + left), m_data + m_width * y,
            m_width * sizeof (mesh_point));
  free (m_data);
  m_data = new_data;
  m_width = new_width;
  m_height = new_height;
  m_xshift = new_xshift;
  m_yshift = new_yshift;
  return true;
}
bool
mesh::need_to_grow_left (int width, int height) const
{
  /* Avoid missing triangles on corners.  */
  if (!m_width)
    return true;
  int xo = m_width == 1 ? 0 : 1;
  for (int y = 0; y < m_height; y++)
    if (m_data[y * m_width + xo].x >= 0 && m_data[y * m_width + xo].x < width
        && m_data[y * m_width + xo].y >= 0
        && m_data[y * m_width + xo].y < height)
      return true;
  return false;
}
bool
mesh::need_to_grow_top (int width, int height) const
{
  /* Avoid missing triangles on corners.  */
  if (!m_height)
    return true;
  int yo = m_height == 1 ? 0 : m_width;
  for (int x = 0; x < m_width; x++)
    if (m_data[x + yo].x >= 0 && m_data[x + yo].x < width
        && m_data[x + yo].y >= 0 && m_data[x + yo].y < height)
      return true;
  return false;
}
bool
mesh::need_to_grow_right (int width, int height) const
{
  /* Avoid missing triangles on corners.  */
  if (!m_width)
    return true;
  int xo = m_width == 1 ? 0 : m_width - 2;
  for (int y = 0; y < m_height; y++)
    if (m_data[y * m_width + xo].x >= 0 && m_data[y * m_width + xo].x < width
        && m_data[y * m_width + xo].y >= 0
        && m_data[y * m_width + xo].y < height)
      return true;
  return false;
}
bool
mesh::need_to_grow_bottom (int width, int height) const
{
  /* Avoid missing triangles on corners.  */
  if (!m_height)
    return true;
  int yo = m_height == 1 ? 0 : (m_height - 2) * m_width;
  for (int x = 0; x < m_width; x++)
    if (m_data[yo + x].x >= 0 && m_data[yo + x].x < width
        && m_data[yo + x].y >= 0 && m_data[yo + x].y < height)
      return true;
  return false;
}
