#include "include/mesh.h"
void
mesh::print (FILE *f)
{
  fprintf (f, "Mesh %ix%i shift:%fx%f steps:%fx%f\n", m_width, m_height, m_xshift, m_yshift, m_xstep, m_ystep);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	{
	  fprintf (f, " (%4.2f,%4.2f)", m_data[y * m_width +x].x,  m_data[y * m_width +x].y);
	}
      fprintf (f, "\n");
    }
}
void
mesh::precompute_inverse()
{
  if (m_invdata)
    abort ();

  coord_t minx = m_data[0].x, maxx = m_data[0].x, miny = m_data[0].y, maxy = m_data[0].y;
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
  m_invwidth = m_width;
  m_invheight = m_height;
  m_invxstep = (maxx - minx) / (m_width - 1);
  m_invystep = (maxy - miny) / (m_height - 1);
  m_invxstepinv = 1 / m_invxstep;
  m_invystepinv = 1 / m_invystep;
  m_invdata = (struct mesh_inverse *)malloc (m_invwidth * m_invheight * sizeof (struct mesh_inverse));
  for (int i = 0 ; i < m_invwidth * m_invheight; i++)
    {
      m_invdata[i].minx = m_width;
      m_invdata[i].maxx = 0;
      m_invdata[i].miny = m_height;
      m_invdata[i].maxy = 0;
    }

  for (int y = 0; y < m_height - 1; y++)
    for (int x = 0; x < m_width - 1; x++)
      {
	coord_t minx = m_data[y * m_width + x].x;
	coord_t maxx = m_data[y * m_width + x].x;
	coord_t miny = m_data[y * m_width + x].y;
	coord_t maxy = m_data[y * m_width + x].y;
	
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
	if (iminx < 0 || iminx >= m_invwidth
	    || imaxx < 0 || imaxx >= m_invwidth
	    || iminy < 0 || iminy >= m_invheight
	    || imaxy < 0 || imaxy >= m_invheight)
	  {
	    printf ("%i %i %i %i\n", iminx, imaxx, iminy, imaxy);
	    abort();
	  }
	for (int yy = iminy; yy <= imaxy; yy++)
	  for (int xx = iminx; xx <= imaxx; xx++)
	    {
	      m_invdata[yy * m_invwidth + xx].minx = std::min ((int)m_invdata[yy * m_invwidth + xx].minx, x);
	      m_invdata[yy * m_invwidth + xx].maxx = std::max ((int)m_invdata[yy * m_invwidth + xx].maxx, x);
	      m_invdata[yy * m_invwidth + xx].miny = std::min ((int)m_invdata[yy * m_invwidth + xx].miny, y);
	      m_invdata[yy * m_invwidth + xx].maxy = std::max ((int)m_invdata[yy * m_invwidth + xx].maxy, y);
	    }
      }
#if 0
  for (int y = 0; y < m_invheight; y++)
    {
      for (int x = 0; x < m_invwidth; x++)
	printf (" %i-%i,%i-%i", m_invdata[y * m_invwidth + x].minx, m_invdata[y * m_invwidth + x].maxx, m_invdata[y * m_invwidth + x].miny, m_invdata[y * m_invwidth + x].maxy);
      printf ("\n");
    }
#endif
}
void
mesh::get_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2, coord_t *xmin, coord_t *xmax, coord_t *ymin, coord_t *ymax)
{
  int ixmin = m_width;
  int ixmax = 0;
  int iymin = m_height;
  int iymax = 0;
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        coord_t xx = m_data [y * m_width + x].x;
        coord_t yy = m_data [y * m_width + x].y;
	if (xx > x1 && xx < x2 && yy > y1 && yy < y2)
	  {
	    ixmin = std::min (ixmin, std::max (x - 1, 0));
	    ixmax = std::max (ixmax, std::min (x + 1, m_width - 1));
	    iymin = std::min (iymin, std::max (y - 1, 0));
	    iymax = std::max (iymax, std::min (y + 1, m_height - 1));
	  }
      }
  if (ixmin > ixmax)
    ixmin = ixmax = iymin = iymax = 0; 
  *xmin = floor (ixmin * m_xstep - m_xshift);
  *xmax = ceil (ixmax * m_xstep - m_xshift);
  *ymin = floor (iymin * m_ystep - m_yshift);
  *ymax = ceil (iymax * m_ystep - m_yshift);
}
