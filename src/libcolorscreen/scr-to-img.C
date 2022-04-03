#include <algorithm>
#include "include/scr-to-img.h"

/* Initilalize the translation matrix to PARAM.  */
void
scr_to_img::set_parameters (scr_to_img_parameters param)
{
  m_param = param;
  /* Translate (0,0) to xstart/ystart.  */
  matrix4x4 translation;
  translation.m_elements[0][2] = param.center_x;
  translation.m_elements[1][2] = param.center_y;

  /* Change-of-basis matrix.  */
  matrix4x4 basis;
  basis.m_elements[0][0] = param.coordinate1_x;
  basis.m_elements[1][0] = param.coordinate1_y;
  basis.m_elements[0][1] = param.coordinate2_x;
  basis.m_elements[1][1] = param.coordinate2_y;
  m_matrix = translation * basis;
}

/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (double x1, double y1,
		       double x2, double y2,
		       int *scr_xshift, int *scr_yshift,
		       int *scr_width, int *scr_height)
{
  /* Compute all the corners.  */
  double xul,xur,xdl,xdr;
  double yul,yur,ydl,ydr;

  to_scr (x1, y1, &xul, &yul);
  to_scr (x2, y1, &xur, &yur);
  to_scr (x1, y2, &xdl, &ydl);
  to_scr (x2, y2, &xdr, &ydr);

  /* Find extremas.  */
  double minx = std::min (std::min (std::min (xul, xur), xdl), xdr);
  double miny = std::min (std::min (std::min (yul, yur), ydl), ydr);
  double maxx = std::max (std::max (std::max (xul, xur), xdl), xdr);
  double maxy = std::max (std::max (std::max (yul, yur), ydl), ydr);

  /* Determine the coordinates.  */
  *scr_xshift = -minx - 1;
  *scr_yshift = -miny - 1;
  *scr_width = maxx-minx + 2;
  *scr_height = maxy-miny + 2;
}
/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (int img_width, int img_height,
		       int *scr_xshift, int *scr_yshift,
		       int *scr_width, int *scr_height)
{
  get_range (0.0, 0.0, (double)img_width, (double)img_height,
	     scr_xshift, scr_yshift,
	     scr_width, scr_height);
}
