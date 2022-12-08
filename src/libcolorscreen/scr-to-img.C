#include <math.h>
#include <algorithm>
#include "include/scr-to-img.h"
#include "include/imagedata.h"
#include "include/spline.h"

namespace {

class rotation_distance_matrix: public matrix4x4<coord_t>
{
  public:
  rotation_distance_matrix (double distance, double tiltx, double tilty, enum scanner_type type)
  {
    double radx = (double)tiltx * M_PI / 180;
    double rady = (double)tilty * M_PI / 180;
    double sy = sin (radx); /*s*/
    double cy = cos (radx); /*c*/
    double sx = sin (rady); /*t*/
    double cx = cos (rady); /*d*/
    /* Rotation matrix is the following:
       cy sy*sx  cx*sy
       0  cx    -sx
      -sy cy*sx  cy*cx
       We want to rotate plane with center in 0 and stretch the rest.  */
    m_elements[0][0] =  cy; m_elements[1][0] = sy * sx; m_elements[2][0] = 0;        m_elements[3][0] = 0;
    m_elements[0][1] =  0 ; m_elements[1][1] = cx     ; m_elements[2][1] = 0;        m_elements[3][1] = 0;
    m_elements[0][2] = -sy; m_elements[1][2] = cy * sx; m_elements[2][2] = distance; m_elements[3][2] = 0;
    m_elements[0][3] = -sy; m_elements[1][3] = cy * sx; m_elements[2][3] = 0;        m_elements[3][3] = distance;
    /* Disable perspective corrections along the lens movement axis.  */
    if (type == lens_move_horisontally)
      m_elements[0][2] = 0, m_elements[1][2] = 0/*, m_elements[2][2]=1*/;
    if (type == lens_move_vertically)
      m_elements[0][3] = 0, m_elements[1][3] = 0/*, m_elements[3][3]=1*/;
  }
};

/* Translate center to given coordinates (x,y).  */
class translation_3x3matrix: public matrix3x3<coord_t>
{
public:
  translation_3x3matrix (coord_t center_x, coord_t center_y)
  {
    m_elements[2][0] = center_x;
    m_elements[2][1] = center_y;
  }
};

/* Change basis to a given coordinate vectors.  */
class change_of_basis_3x3matrix: public matrix3x3<coord_t>
{
public:
  change_of_basis_3x3matrix (coord_t c1_x, coord_t c1_y,
			     coord_t c2_x, coord_t c2_y)
  {
    m_elements[0][0] = c1_x; m_elements[1][0] = c2_x;
    m_elements[0][1] = c1_y; m_elements[1][1] = c2_y;
  }
};

}

/* Initilalize the translation matrix to PARAM.  */
void
scr_to_img::set_parameters (scr_to_img_parameters param, image_data &img)
{
  /* We do not need to copy motor corrections since we already constructed the function.  */
  m_param.copy_from_cheap (param);
  m_inverted_projection_distance = 1 / param.projection_distance;
  m_nwarnings = 0;

  /* Initialize motor correction.  */
  m_motor_correction = NULL;
  if (param.n_motor_corrections && param.scanner_type != fixed_lens)
    {
      int len = param.scanner_type == lens_move_horisontally ? img.width : img.height;
      if (param.n_motor_corrections > 2 && 0)
	{
	  spline<coord_t> spline (param.motor_correction_x, param.motor_correction_y, param.n_motor_corrections);
	  m_motor_correction = spline.precompute (0, len, len);
	}
      else
	m_motor_correction = new precomputed_function<coord_t> (0, len, len, param.motor_correction_x, param.motor_correction_y, param.n_motor_corrections);
    }

  /* Next initialize lens correction.
     Lens center is specified in scan coordinates, so apply previous corrections.  */
  apply_motor_correction (param.lens_center_x, param.lens_center_y, &m_corrected_lens_center_x, &m_corrected_lens_center_y);
  m_lens_radius = my_sqrt ((coord_t)(img.width * img.width + img.height * img.height));
  m_inverse_lens_radius = 1 / m_lens_radius;

  /* Now set up the projection matrix that combines tilts and shifting to a given distance.  */
  rotation_distance_matrix rd (m_param.projection_distance, param.tilt_x, param.tilt_y, param.scanner_type);
  m_perspective_matrix = rd;
  coord_t c1x, c1y;
  coord_t c2x, c2y;
  coord_t corrected_center_x;
  coord_t corrected_center_y;
  /* Center and base vectors are in the scan coordinates.  */
  apply_early_correction (m_param.center_x, m_param.center_y,
			  &corrected_center_x,	 &corrected_center_y);
  apply_early_correction (m_param.coordinate1_x + m_param.center_x,
			  m_param.coordinate1_y + m_param.center_y,
			  &c1x, &c1y);

  apply_early_correction (m_param.coordinate2_x + m_param.center_x,
			  m_param.coordinate2_y + m_param.center_y,
			  &c2x, &c2y);

  m_perspective_matrix.inverse_perspective_transform (corrected_center_x, corrected_center_y, corrected_center_x, corrected_center_y);
  m_perspective_matrix.inverse_perspective_transform (c1x, c1y, c1x, c1y);
  m_perspective_matrix.inverse_perspective_transform (c2x, c2y, c2x, c2y);
  c1x -= corrected_center_x;
  c1y -= corrected_center_y;
  c2x -= corrected_center_x;
  c2y -= corrected_center_y;
#if 0
  /* This makes the grid fixed which is sometimes useful to debug various bugs concerning
     earlier corrections.  */
  corrected_center_x = corrected_center_y = 0;
  c1x=800;
  c1y=0;
  c2x=0;
  c2y=800;
#endif

  /* Change-of-basis matrix.  */
  trans_3d_matrix mm;
  change_of_basis_3x3matrix basis (c1x, c1y, c2x, c2y);
  translation_3x3matrix trans (corrected_center_x, corrected_center_y);
  mm = basis * mm;
  mm = trans * mm;

  m_matrix = mm;
  m_inverse_matrix = m_matrix.invert ();
}

/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (coord_t x1, coord_t y1,
		       coord_t x2, coord_t y2,
		       int *scr_xshift, int *scr_yshift,
		       int *scr_width, int *scr_height)
{
  coord_t minx, miny, maxx, maxy;
  if (!m_param.mesh_trans || 1)
    {
      /* Compute all the corners.  */
      coord_t xul,xur,xdl,xdr;
      coord_t yul,yur,ydl,ydr;

      to_scr (x1, y1, &xul, &yul);
      to_scr (x2, y1, &xur, &yur);
      to_scr (x1, y2, &xdl, &ydl);
      to_scr (x2, y2, &xdr, &ydr);

      /* Find extremas.  */
      minx = std::min (std::min (std::min (xul, xur), xdl), xdr);
      miny = std::min (std::min (std::min (yul, yur), ydl), ydr);
      maxx = std::max (std::max (std::max (xul, xur), xdl), xdr);
      maxy = std::max (std::max (std::max (yul, yur), ydl), ydr);

      /* Hack warning: if we correct lens distortion the corners may not be extremes.  */
      if (m_param.k1 || m_param.tilt_x || m_param.tilt_y)
	{
	  const int steps = 16*1024;
	  for (int i = 1; i < steps; i++)
	    {
	      coord_t xx,yy;
	      to_scr (x1 + (x2 - x1) * i / steps, y1, &xx, &yy);
	      minx = std::min (minx, xx);
	      miny = std::min (miny, yy);
	      maxx = std::max (maxx, xx);
	      maxy = std::max (maxy, yy);
	      to_scr (x1 + (x2 - x1) * i / steps, y2, &xx, &yy);
	      minx = std::min (minx, xx);
	      miny = std::min (miny, yy);
	      maxx = std::max (maxx, xx);
	      maxy = std::max (maxy, yy);
	      to_scr (x1, y1 + (y2 - y1) * i / steps, &xx, &yy);
	      minx = std::min (minx, xx);
	      miny = std::min (miny, yy);
	      maxx = std::max (maxx, xx);
	      maxy = std::max (maxy, yy);
	      to_scr (x2, y1 + (y2 - y1) * i / steps, &xx, &yy);
	      minx = std::min (minx, xx);
	      miny = std::min (miny, yy);
	      maxx = std::max (maxx, xx);
	      maxy = std::max (maxy, yy);
	    }
	}
    }
  else
    m_param.mesh_trans->get_range (x1, y1, x2, y2, &minx, &maxx, &miny, &maxy);


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
  get_range (0.0, 0.0, (coord_t)img_width, (coord_t)img_height,
	     scr_xshift, scr_yshift,
	     scr_width, scr_height);
}
