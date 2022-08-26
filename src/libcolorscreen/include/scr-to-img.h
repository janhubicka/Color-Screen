#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
#include "dllpublic.h"
#include "matrix.h"

typedef float coord_t;
typedef matrix4x4<coord_t> trans_matrix;

/* Types of supported screens.  */
enum scr_type
{
   Paget,
   Thames,
   Finlay,
   Dufay,
   max_scr_type
};

/* This implements to translate image coordiantes to coordinates of the viewing screen.
   In the viewing screen the coordinats (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates describes
   gren dots again.  */

struct DLL_PUBLIC scr_to_img_parameters
{
  scr_to_img_parameters ()
  : center_x (0), center_y (0), coordinate1_x(5), coordinate1_y (0), coordinate2_x (0), coordinate2_y (5),
    tilt_x_x (0), tilt_x_y(0), tilt_y_x (0), tilt_y_y (0), k1(0), type (Finlay)
  { }
  /* Coordinates (in the image) of the center of the screen (a green dot).  */
  coord_t center_x, center_y;
  /* First coordinate vector:
     image's (center_x+coordinate1_x, centr_y+coordinate1_y) should describe
     a green dot just on the right side of (center_x, center_y).  */
  coord_t coordinate1_x, coordinate1_y;
  /* Second coordinate vector:
     image's (center_x+coordinate1_x, centr_y+coordinate1_y) should describe
     a green dot just below (center_x, center_y).  */
  coord_t coordinate2_x, coordinate2_y;
  /* Perspective tilt in x and y coordinate in degrees which results in perspective
     error in x coordinate.  */
  coord_t tilt_x_x, tilt_x_y;
  /* Same for perspective errors in y coordinate. */
  coord_t tilt_y_x, tilt_y_y;
  /* 1st radial distortion coefficient for Brown–Conrady lens distortion model.  */
  coord_t k1;
  enum scr_type type;

  bool operator== (scr_to_img_parameters &other) const
  {
    return center_x == other.center_x
	   && center_y == other.center_y
	   && coordinate1_x == other.coordinate1_x
	   && coordinate1_y == other.coordinate1_y
	   && coordinate2_x == other.coordinate2_x
	   && coordinate2_y == other.coordinate2_y
	   && tilt_x_x == other.tilt_x_x
	   && tilt_x_y == other.tilt_x_y
	   && tilt_y_x == other.tilt_y_x
	   && tilt_y_y == other.tilt_y_y;
  }
  bool operator!= (scr_to_img_parameters &other) const
  {
    return !(*this == other);
  }
};

class image_data;

/* Mapping between screen and image.  */
class DLL_PUBLIC scr_to_img
{
public:
  void set_parameters (scr_to_img_parameters param, image_data &img);
  void get_range (int img_width, int img_height,
		  int *scr_xshift, int *scr_yshift,
		  int *scr_width, int *scr_height);
  void get_range (coord_t x1, coord_t y1,
      		  coord_t x2, coord_t y2,
		  int *scr_xshift, int *scr_yshift,
		  int *scr_width, int *scr_height);

  /* Prevent conversion to wrong data type when doing math.  */
  static inline float
  my_sqrt (float x)
  {
    return sqrtf (x);
  }
  static inline double
  my_sqrt (double x)
  {
    return sqrt (x);
  }
  static inline float
  my_cbrt (float x)
  {
    return cbrtf (x);
  }
  static inline double
  my_cbrt (double x)
  {
    return cbrt (x);
  }
  /* Apply lens correction.  */
  void
  apply_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
   if (!m_param.k1)
     {
       *xr = x;
       *yr = y;
     }
   else
     {
#if 1
       coord_t xd = (x - m_lens_center_x);
       coord_t yd = (y - m_lens_center_y);
       coord_t powradius = (xd * xd + yd * yd) * m_inverse_lens_radius * m_inverse_lens_radius;
       *xr = x + xd * powradius * m_param.k1;
       *yr = y + yd * powradius * m_param.k1;
#else
       coord_t xd = (x - m_lens_center_x) * m_inverse_lens_radius;
       coord_t yd = (y - m_lens_center_y) * m_inverse_lens_radius;
       coord_t r = sqrt (xd*xd + yd*yd);
       xd /= r;
       yd /= r;
       r += m_param.k1 * r * r *r;
       *xr = m_lens_center_x + r * xd * m_lens_radius;
       *yr = m_lens_center_y + r * yd * m_lens_radius;
#endif
     }
  }
  /* Invert lens correction.  */
  void
  inverse_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
     if (!m_param.k1)
       {
	 *xr = x;
	 *yr = y;
       }
     else
       {
	 coord_t xd = (x - m_lens_center_x) * m_inverse_lens_radius;
	 coord_t yd = (y - m_lens_center_y) * m_inverse_lens_radius;
	 coord_t rpow2 = xd * xd + yd * yd;
	 coord_t r = my_sqrt (rpow2);
	 /* An inverse as given by https://www.wolframalpha.com/input?i=x%2Bk*x*x*x-r%3D0  */
	 coord_t k1 = m_param.k1;
	 coord_t k1pow2 = k1 * k1;
	 coord_t k1pow3 = k1 * k1pow2;
	 coord_t k1pow4 = k1pow2 * k1pow2;
	 coord_t sqrt3 = 1.73205080757;
	 coord_t cbrt2 = 1.25992104989;
	 coord_t coef = my_cbrt (9 * k1pow2 * r + sqrt3 * my_sqrt (27 * k1pow4 * rpow2 + 4 * k1pow3));
	 coord_t radius = coef / (cbrt2 * (coord_t)2.08008382305 /* 3^(2/3) */ * k1) - (coord_t)0.87358046473629 /* cbrt(2/3) */ / coef;

	 *xr = xd / r * radius * m_lens_radius + m_lens_center_x;
	 *yr = yd / r * radius * m_lens_radius + m_lens_center_y;
       }
  }

  /* Map screen coordinates to image coordinates.  */
  void
  to_img (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_matrix.perspective_transform (x,y, x, y);
    x += m_corrected_center_x;
    y += m_corrected_center_y;
    inverse_lens_correction (x, y, xp, yp);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    apply_lens_correction (x, y, &x, &y);
    x -= m_corrected_center_x;
    y -= m_corrected_center_y;
    m_matrix.inverse_perspective_transform (x,y, *xp, *yp);
  }
  enum scr_type
  get_type ()
  {
    return m_param.type;
  }
private:
  /* Center of lenses: the middle of the scan.  */
  coord_t m_lens_center_x, m_lens_center_y;
  /* Center of the coordinate system in corrected coordinates.  */
  coord_t m_corrected_center_x, m_corrected_center_y;
  /* Radius in pixels of the lens circle and its inverse.  */
  coord_t m_lens_radius, m_inverse_lens_radius;
  /* Screen->image translation matrix.  */
  trans_matrix m_matrix;
  scr_to_img_parameters m_param;
};

/* Translate center to given coordinates (x,y).  */
class translation_matrix: public matrix4x4<coord_t>
{
public:
  translation_matrix (coord_t center_x, coord_t center_y)
  {
    m_elements[0][2] = center_x;
    m_elements[1][2] = center_y;
  }
};

/* Change basis to a given coordinate vectors.  */
class change_of_basis_matrix: public matrix4x4<coord_t>
{
public:
  change_of_basis_matrix (coord_t c1_x, coord_t c1_y,
			  coord_t c2_x, coord_t c2_y)
  {
    m_elements[0][0] = c1_x; m_elements[1][0] = c1_y;
    m_elements[0][1] = c2_x; m_elements[1][1] = c2_y;
  }
};

/* Rotation matrix by given angle (in degrees) which is applied to
   given coordinates (used to produce tilt).  */
class rotation_matrix: public matrix4x4<coord_t>
{
public:
  rotation_matrix (double angle, int coord1, int coord2)
  {
    double rad = (double)angle * M_PI / 180;
    m_elements[coord1][coord1] = cos (rad);  m_elements[coord2][coord1] = sin (rad);
    m_elements[coord1][coord2] = -sin (rad); m_elements[coord2][coord2] = cos (rad);
  }
};
#endif
