#include "include/mesh.h"
#include "lru-cache.h"
#include "loadsave.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <omp.h>

namespace colorscreen
{

namespace {
  struct mesh_inverse_params
  {
    const mesh *m;
    uint64_t id;
    int_optional_image_area area;

    bool operator== (const mesh_inverse_params &other) const
    {
      return id == other.id && area == other.area;
    }
  };

  std::unique_ptr<mesh>
  get_inverse_mesh (mesh_inverse_params &params, progress_info *progress)
  {
    return params.m->compute_inverse_uncached (params.area, progress);
  }

  lru_cache<mesh_inverse_params, mesh, get_inverse_mesh, 16> inverse_mesh_cache ("inverse_meshes");
}

/* Initialize 2D mesh transformation.  XSHIFT and YSHIFT are the range shifts,
   XSTEP and YSTEP are the grid step sizes.  WIDTH and HEIGHT are dimensions
   of the mesh in points.  */
mesh::mesh (coord_t xshift, coord_t yshift, coord_t xstep, coord_t ystep,
            int width, int height)
    : id (lru_caches::get ()), m_xshift (xshift), m_yshift (yshift),
      m_xstep (xstep), m_ystep (ystep), m_xstepinv (1.0f / xstep),
      m_ystepinv (1.0f / ystep), m_width (width), m_height (height)
{
  m_data.resize (width * height);
}

/* Initialize 2D mesh transformation for given AREA with given XSTEP and YSTEP.  */
mesh::mesh (int_image_area area, coord_t xstep, coord_t ystep)
    : mesh (area.xshift (), area.yshift (), xstep, ystep,
            (area.width + xstep - 1) / xstep, (area.height + ystep - 1) / ystep)
{
}

/* Destructor.  */
mesh::~mesh ()
{
}

/* Print mesh dimensions and point grid to file F.  */
void
mesh::print (FILE *f) const
{
  fprintf (f, "Mesh %ix%i shift:%fx%f steps:%fx%f\n", m_width, m_height,
           (double)m_xshift, (double)m_yshift, (double)m_xstep, (double)m_ystep);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
        {
          fprintf (f, " (%4.2f,%4.2f)", (double)m_data[y * m_width + x].x,
                   (double)m_data[y * m_width + x].y);
        }
      fprintf (f, "\n");
    }
}

/* Precompute indices for inverse lookup.  This speeds up invert() by mapping
   output coordinates back to mesh cells.  */
void
mesh::precompute_inverse () const
{
  if (m_data.empty () || !m_invdata.empty ())
    return;

  mesh_coord_t minx = std::numeric_limits<mesh_coord_t>::max ();
  mesh_coord_t maxx = std::numeric_limits<mesh_coord_t>::lowest ();
  mesh_coord_t miny = std::numeric_limits<mesh_coord_t>::max ();
  mesh_coord_t maxy = std::numeric_limits<mesh_coord_t>::lowest ();

  /* Find bounding box of the mesh.  */
#pragma omp parallel for reduction(min:minx, miny) reduction(max:maxx, maxy)
  for (int i = 0; i < (int)m_data.size (); i++)
    {
      minx = std::min (m_data[i].x, minx);
      maxx = std::max (m_data[i].x, maxx);
      miny = std::min (m_data[i].y, miny);
      maxy = std::max (m_data[i].y, maxy);
    }

  m_invxshift = -minx;
  m_invyshift = -miny;
  m_invwidth = m_width * 2;
  m_invheight = m_height * 2;

  /* Handle meshes with single point in a dimension to avoid division by zero.  */
  m_invxstep = (m_width > 1) ? (maxx - minx) / (m_width - 1) : 1.0f;
  m_invystep = (m_height > 1) ? (maxy - miny) / (m_height - 1) : 1.0f;

  /* Avoid division by zero when mesh has zero width or height in image area.  */
  m_invxstepinv = (m_invxstep > 1e-9f) ? 1.0f / m_invxstep : 0.0f;
  m_invystepinv = (m_invystep > 1e-9f) ? 1.0f / m_invystep : 0.0f;

  m_invdata.resize (m_invwidth * m_invheight);

  /* Initialize inverse lookup table.  */
#pragma omp parallel for
  for (int i = 0; i < (int)m_invdata.size (); i++)
    {
      m_invdata[i].minx = m_width;
      m_invdata[i].maxx = 0;
      m_invdata[i].miny = m_height;
      m_invdata[i].maxy = 0;
    }

  /* Fill inverse lookup table with cells covering each output region.  */
#pragma omp parallel for collapse(2)
  for (int y = 0; y < m_height - 1; y++)
    for (int x = 0; x < m_width - 1; x++)
      {
        mesh_coord_t cell_minx = m_data[y * m_width + x].x;
        mesh_coord_t cell_maxx = m_data[y * m_width + x].x;
        mesh_coord_t cell_miny = m_data[y * m_width + x].y;
        mesh_coord_t cell_maxy = m_data[y * m_width + x].y;

        /* Check all 4 corners of the cell.  */
        for (int dy = 0; dy <= 1; dy++)
          for (int dx = 0; dx <= 1; dx++)
            {
              cell_minx = std::min (m_data[(y + dy) * m_width + x + dx].x, cell_minx);
              cell_maxx = std::max (m_data[(y + dy) * m_width + x + dx].x, cell_maxx);
              cell_miny = std::min (m_data[(y + dy) * m_width + x + dx].y, cell_miny);
              cell_maxy = std::max (m_data[(y + dy) * m_width + x + dx].y, cell_maxy);
            }

        int iminx = floor ((cell_minx + m_invxshift) * m_invxstepinv);
        int imaxx = floor ((cell_maxx + m_invxshift) * m_invxstepinv);
        int iminy = floor ((cell_miny + m_invyshift) * m_invystepinv);
        int imaxy = floor ((cell_maxy + m_invyshift) * m_invystepinv);

        iminx = std::clamp (iminx, 0, m_invwidth - 1);
        imaxx = std::clamp (imaxx, 0, m_invwidth - 1);
        iminy = std::clamp (iminy, 0, m_invheight - 1);
        imaxy = std::clamp (imaxy, 0, m_invheight - 1);

        /* Update coverage info for each overlapping lookup bucket.  */
        for (int yy = iminy; yy <= imaxy; yy++)
          for (int xx = iminx; xx <= imaxx; xx++)
            {
              int idx = yy * m_invwidth + xx;
#pragma omp critical(mesh_inv_update)
              {
                m_invdata[idx].minx = std::min (m_invdata[idx].minx, (unsigned int)x);
                m_invdata[idx].maxx = std::max (m_invdata[idx].maxx, (unsigned int)x);
                m_invdata[idx].miny = std::min (m_invdata[idx].miny, (unsigned int)y);
                m_invdata[idx].maxy = std::max (m_invdata[idx].maxy, (unsigned int)y);
              }
            }
      }
}

/* Find source point corresponding to image point IP using the inverse lookup table.  */
point_t
mesh::invert (point_t ip) const
{
  mesh_point p = { (mesh_coord_t)ip.x, (mesh_coord_t)ip.y };
  int ix = std::clamp ((int)((ip.x + m_invxshift) * m_invxstepinv), 0, m_invwidth - 1);
  int iy = std::clamp ((int)((ip.y + m_invyshift) * m_invystepinv), 0, m_invheight - 1);
  if (ix >= 0 && iy >= 0 && ix < m_invwidth && iy < m_invheight && !m_invdata.empty ())
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

            /* Check if point is above or below diagonal.  */
            mesh_coord_t sgn1 = sign (p, p1, p4);
            if (sgn1 > 0)
              {
                /* Check if point is inside of the triangle.  */
                if (sign (p, p4, p3) < 0 || sign (p, p3, p1) < 0)
                  continue;
                mesh_coord_t rx, ry;
                intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p3.x,
                                   p3.y, p4.x - p3.x, p4.y - p3.y, &rx, &ry);
                rx = 1.0f / rx;
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
                rx = 1.0f / rx;
                return { (rx + x) * m_xstep - m_xshift,
                         (ry * rx + y) * m_ystep - m_yshift };
              }
          }
    }
  
  /* Fallback: return mesh boundaries.  */
  point_t ret;
  if (ix < m_invwidth / 2)
    ret.x = -m_xshift;
  else
    ret.x = -m_xshift + m_xstep * (m_width - 1);
  if (iy < m_invheight / 2)
    ret.y = -m_yshift;
  else
    ret.y = -m_yshift + m_ystep * (m_height - 1);
  return ret;
}

/* Small helper to push mesh entry (X, Y) to image range [X1, Y1]..[X2, Y2].  */
point_t
mesh::push_to_range (int x, int y, image_area area) const
{
  coord_t x1 = area.x;
  coord_t y1 = area.y;
  coord_t x2 = area.x + area.width;
  coord_t y2 = area.y + area.height;
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

/* Determine range in target coordinates covering AREA transformed by TRANS.  */
image_area
mesh::get_range (matrix2x2<coord_t> trans, image_area area_in) const noexcept
{
  image_area area;

  for (int y = 0; y < m_height - 1; y++)
    for (int x = 0; x < m_width - 1; x++)
      {
        image_area mesh_cell_area;
        for (int dy = 0; dy <= 1; dy++)
          for (int dx = 0; dx <= 1; dx++)
            mesh_cell_area.extend (m_data[(y + dy) * m_width + x + dx]);

        if (!mesh_cell_area.overlap_p (area_in))
          continue;

        /* For overlapping cells, transform corners to source coordinates.  */
        for (int dy = 0; dy <= 1; dy++)
          for (int dx = 0; dx <= 1; dx++)
            {
              point_t p = push_to_range (x + dx, y + dy, area_in);
              area.extend (trans.apply_to_vector (p));
            }
      }
  return area;
}

/* Determine range in src coordinates covering AREA of target.  */
image_area
mesh::get_src_range (image_area area_in) const noexcept
{
  image_area area;

  int xmin = std::clamp ((int)my_floor ((area_in.x + m_xshift) * m_xstepinv), 0, m_width);
  int xmax = std::clamp ((int)my_ceil ((area_in.x + area_in.width + m_xshift) * m_xstepinv), 0, m_width);
  int ymin = std::clamp ((int)my_floor ((area_in.y + m_yshift) * m_ystepinv), 0, m_height);
  int ymax = std::clamp ((int)my_ceil ((area_in.y + area_in.height + m_yshift) * m_ystepinv), 0, m_height);

  for (int x = xmin; x <= xmax; x++)
    for (int y = ymin; y <= ymax; y++)
      area.extend (m_data[y * m_width + x]);
  return area;
}

/* Save mesh dimensions, shifts, steps and point grid to file F.  */
bool
mesh::save (FILE *f) const
{
  if (fprintf (f, "  mesh_dimensions: %i %i\n", m_width, m_height) < 0)
    return false;
  if (fprintf (f, "  mesh_shifts: %f %f\n", (double)m_xshift, (double)m_yshift) < 0)
    return false;
  if (fprintf (f, "  mesh_steps: %f %f\n", (double)m_xstep, (double)m_ystep) < 0)
    return false;
  if (fprintf (f, "  mesh_points:") < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n              ");
      for (int x = 0; x < m_width; x++)
        if (fprintf (f, " (%4.2f, %4.2f)", (double)m_data[y * m_width + x].x,
                     (double)m_data[y * m_width + x].y)
            < 0)
          return false;
    }
  if (fprintf (f, "\n  mesh_end\n") < 0)
    return false;
  return true;
}

/* Load mesh from file F. Descriptions of parse errors are stored in ERROR.  */
std::unique_ptr<mesh>
mesh::load (FILE *f, const char **error)
{
  if (!expect_keyword (f, "mesh_dimensions:"))
    {
      *error = "expected mesh_dimensions";
      return nullptr;
    }
  int width, height;
  if (fscanf (f, "%i %i", &width, &height) != 2)
    {
      *error = "failed to parse mesh_dimensions";
      return nullptr;
    }
  float xshift, yshift;
  if (!expect_keyword (f, "mesh_shifts:"))
    {
      *error = "expected mesh_shifts";
      return nullptr;
    }
  if (fscanf (f, "%f %f", &xshift, &yshift) != 2)
    {
      *error = "failed to parse mesh_shifts";
      return nullptr;
    }
  float xstep, ystep;
  if (!expect_keyword (f, "mesh_steps:"))
    {
      *error = "expected mesh_steps";
      return nullptr;
    }
  if (fscanf (f, "%f %f", &xstep, &ystep) != 2)
    {
      *error = "failed to parse mesh_steps";
      return nullptr;
    }
  if (!expect_keyword (f, "mesh_points:"))
    {
      *error = "expected mesh_points";
      return nullptr;
    }
  auto m = std::make_unique<mesh> (xshift, yshift, xstep, ystep, width, height);
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
          float sx, sy;
          if (!expect_keyword (f, "(") || fscanf (f, "%f", &sx) != 1
              || !expect_keyword (f, ",") || fscanf (f, "%f", &sy) != 1
              || !expect_keyword (f, ")"))
            {
              *error = "failed to parse mesh points";
              return nullptr;
            }
          m->set_point ({ x, y }, { sx, sy });
        }
    }
  if (!expect_keyword (f, "mesh_end"))
    {
      *error = "expected mesh_end";
      return nullptr;
    }
  return m;
}

/* Grow mesh dimensions by given number of points to LEFT, RIGHT, TOP and BOTTOM.  */
bool
mesh::grow (int left, int right, int top, int bottom)
{
  int new_width = m_width + left + right;
  int new_height = m_height + top + bottom;
  
  if (!m_invdata.empty ())
    abort (); // Growing mesh after inverse precomputation is not supported.

  std::vector<mesh_point> new_data (new_width * new_height);
  for (int y = 0; y < m_height; y++)
    std::copy (m_data.begin () + y * m_width, 
               m_data.begin () + (y + 1) * m_width,
               new_data.begin () + (y + top) * new_width + left);

  m_data = std::move (new_data);
  m_xshift += left * m_xstep;
  m_yshift += top * m_ystep;
  m_width = new_width;
  m_height = new_height;
  return true;
}

/* Check if mesh needs growth to the left for image of WIDTH x HEIGHT.  */
bool
mesh::need_to_grow_left (int width, int height) const
{
  if (!m_width)
    return true;
  int xo = m_width == 1 ? 0 : 1;
  for (int y = 0; y < m_height; y++)
    if (entry_useful_p ({xo, y}, 0, width - 1, 0, height - 1))
      return true;
  return false;
}

/* Check if mesh needs growth to the top for image of WIDTH x HEIGHT.  */
bool
mesh::need_to_grow_top (int width, int height) const
{
  if (!m_height)
    return true;
  int yo = m_height == 1 ? 0 : 1;
  for (int x = 0; x < m_width; x++)
    if (entry_useful_p ({x, yo}, 0, width - 1, 0, height - 1))
      return true;
  return false;
}

/* Check if mesh needs growth to the right for image of WIDTH x HEIGHT.  */
bool
mesh::need_to_grow_right (int width, int height) const
{
  if (!m_width)
    return true;
  int xo = m_width == 1 ? 0 : m_width - 2;
  for (int y = 0; y < m_height; y++)
    if (entry_useful_p ({xo, y}, 0, width - 1, 0, height - 1))
      return true;
  return false;
}

/* Check if mesh needs growth to the bottom for image of WIDTH x HEIGHT.  */
bool
mesh::need_to_grow_bottom (int width, int height) const
{
  if (!m_height)
    return true;
  int yo = m_height == 1 ? 0 : m_height - 2;
  for (int x = 0; x < m_width; x++)
    if (entry_useful_p ({x, yo}, 0, width - 1, 0, height - 1))
      return true;
  return false;
}

/* Compute an inverse mesh covering the given AREA or the full bounding box of original mesh target coordinates.  */
std::unique_ptr<mesh>
mesh::compute_inverse_uncached (int_optional_image_area area, progress_info *progress) const
{
  if (m_width == 0 || m_height == 0 || (area.set && area.empty_p ()))
    return std::make_unique<mesh> (0, 0, 1.0f, 1.0f, 0, 0);

  mesh_coord_t minx = std::numeric_limits<mesh_coord_t>::max ();
  mesh_coord_t maxx = std::numeric_limits<mesh_coord_t>::lowest ();
  mesh_coord_t miny = std::numeric_limits<mesh_coord_t>::max ();
  mesh_coord_t maxy = std::numeric_limits<mesh_coord_t>::lowest ();

  if (area.set)
    {
      minx = area.x;
      maxx = area.x + area.width - 1;
      miny = area.y;
      maxy = area.y + area.height - 1;
    }
  else
    {
      for (int i = 0; i < (int)m_data.size (); i++)
        {
          minx = std::min (m_data[i].x, minx);
          maxx = std::max (m_data[i].x, maxx);
          miny = std::min (m_data[i].y, miny);
          maxy = std::max (m_data[i].y, maxy);
        }
    }

  /* Handle meshes with single point in a dimension to avoid division by zero.  */
  mesh_coord_t invxstep = (m_width > 1) ? (maxx - minx) / (m_width - 1) : 1.0f;
  mesh_coord_t invystep = (m_height > 1) ? (maxy - miny) / (m_height - 1) : 1.0f;

  if (invxstep <= 0.0f)
    invxstep = 1.0f;
  if (invystep <= 0.0f)
    invystep = 1.0f;

  int invwidth = my_ceil ((maxx - minx) / invxstep) + 1;
  int invheight = my_ceil ((maxy - miny) / invystep) + 1;

  int64_t max_size = (int64_t)m_width * m_height * 32;
  if ((int64_t)invwidth * invheight > max_size)
    {
      double factor = std::sqrt ((double)((int64_t)invwidth * invheight) / max_size);
      invxstep *= factor;
      invystep *= factor;
      invwidth = my_ceil ((maxx - minx) / invxstep) + 1;
      invheight = my_ceil ((maxy - miny) / invystep) + 1;
    }

  if (invwidth < 2)
    invwidth = 2;
  if (invheight < 2)
    invheight = 2;

  precompute_inverse ();

  auto inv_mesh = std::make_unique<mesh> (-minx, -miny, invxstep, invystep, invwidth, invheight);

#pragma omp parallel for collapse(2)
  for (int y = 0; y < invheight; y++)
    for (int x = 0; x < invwidth; x++)
      {
        if (progress && progress->cancel_requested ())
          continue;
        point_t ip = { (coord_t)(minx + x * invxstep), (coord_t)(miny + y * invystep) };
        point_t src = invert (ip);
        inv_mesh->set_point ({(int64_t)x, (int64_t)y}, src);
      }

  if (progress && progress->cancel_requested ())
    return nullptr;

  return inv_mesh;
}

/* Apply matrix TRANS to every point in the mesh and return a new mesh.  */
std::unique_ptr<mesh>
mesh::transformed (matrix3x3<coord_t> trans) const
{
  auto ret = std::make_unique<mesh> (m_xshift, m_yshift, m_xstep, m_ystep, m_width, m_height);
#pragma omp parallel for
  for (int i = 0; i < (int)m_data.size (); i++)
    {
      point_t p = { (coord_t)m_data[i].x, (coord_t)m_data[i].y };
      p = trans.apply (p);
      ret->m_data[i] = { (mesh_coord_t)p.x, (mesh_coord_t)p.y };
    }
  return ret;
}

std::shared_ptr<mesh>
mesh::compute_inverse (int_optional_image_area area, progress_info *progress) const
{
  mesh_inverse_params p = { this, id, area };
  return inverse_mesh_cache.get (p, progress);
}

} // namespace colorscreen
