#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
#include "dllpublic.h"
#include "matrix.h"
#include "precomputed-function.h"

/* Windows does not seem to define this by default.  */
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


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

/* Type of a scanner used.  */
enum scanner_type {
	fixed_lens,
	lens_move_horisontally,
	lens_move_vertically,
	max_scanner_type
};


extern const char * const scanner_type_names[max_scanner_type];

/* This implements to translate image coordiantes to coordinates of the viewing screen.
   In the viewing screen the coordinats (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates describes
   gren dots again.  */

struct DLL_PUBLIC scr_to_img_parameters
{
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

  /* Center of lens within scan image (in scan coordinates.  */
  coord_t lens_center_x, lens_center_y;
  /* Perspective tilt in x and y coordinate in degrees which results in perspective
     error in x coordinate.  */
  coord_t tilt_x_x, tilt_x_y;
  /* Same for perspective errors in y coordinate. */
  coord_t tilt_y_x, tilt_y_y;
  /* 1st radial distortion coefficient for Brown–Conrady lens distortion model.  */
  coord_t k1;

  /* Stepping motor correction is described by a spline.  */
  coord_t *motor_correction_x, *motor_correction_y;
  int n_motor_corrections;

  enum scr_type type;
  enum scanner_type scanner_type;

  scr_to_img_parameters ()
  : center_x (0), center_y (0), coordinate1_x(5), coordinate1_y (0), coordinate2_x (0), coordinate2_y (5),
    lens_center_x (0), lens_center_y (0), tilt_x_x (0), tilt_x_y(0), tilt_y_x (0), tilt_y_y (0), k1(0),
    motor_correction_x (NULL), motor_correction_y (NULL), n_motor_corrections (0),
    type (Finlay), scanner_type (fixed_lens)
  { }
  scr_to_img_parameters (const scr_to_img_parameters &from)
  : center_x (from.center_x), center_y (from.center_y),
    coordinate1_x(from.coordinate1_x), coordinate1_y (from.coordinate1_y),
    coordinate2_x (from.coordinate2_x), coordinate2_y (from.coordinate2_y),
    lens_center_x (from.lens_center_x), lens_center_y (from.lens_center_y),
    tilt_x_x (from.tilt_x_x), tilt_x_y(from.tilt_x_y), tilt_y_x (from.tilt_y_x), tilt_y_y (from.tilt_y_y), k1(from.k1),
    motor_correction_x (NULL), motor_correction_y (NULL), n_motor_corrections (from.n_motor_corrections),
    type (from.type), scanner_type (from.scanner_type)
  {
    if (n_motor_corrections)
      {
        motor_correction_x = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
        motor_correction_y = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
	memcpy (motor_correction_x, from.motor_correction_x, n_motor_corrections * sizeof (coord_t));
	memcpy (motor_correction_y, from.motor_correction_y, n_motor_corrections * sizeof (coord_t));
      }
  }
  scr_to_img_parameters &operator= (const scr_to_img_parameters &other)
  {
    free (motor_correction_x);
    free (motor_correction_y);
    n_motor_corrections = 0;
    copy_from_cheap (other);
    n_motor_corrections = other.n_motor_corrections;
    motor_correction_x = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
    motor_correction_y = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
    memcpy (motor_correction_x, other.motor_correction_x, n_motor_corrections * sizeof (coord_t));
    memcpy (motor_correction_y, other.motor_correction_y, n_motor_corrections * sizeof (coord_t));
    return *this;
  }
  /* Copy everything except for motor corrections.  */
  void
  copy_from_cheap (const scr_to_img_parameters &from)
  {
    center_x = from.center_x;
    center_y = from.center_y;
    coordinate1_x = from.coordinate1_x;
    coordinate1_y = from.coordinate1_y;
    coordinate2_x = from.coordinate2_x;
    coordinate2_y = from.coordinate2_y;
    lens_center_x = from.lens_center_x;
    lens_center_y = from.lens_center_y;
    tilt_x_x = from.tilt_x_x;
    tilt_x_y = from.tilt_x_y;
    tilt_y_x = from.tilt_y_x;
    tilt_y_y = from.tilt_y_y;
    k1 = from.k1;
    type = from.type;
    scanner_type = from.scanner_type;
    if (n_motor_corrections)
      abort ();
  }
  ~scr_to_img_parameters ()
  {
    free (motor_correction_x);
    free (motor_correction_y);
  }
  bool operator== (scr_to_img_parameters &other) const
  {
    return center_x == other.center_x
	   && center_y == other.center_y
	   && coordinate1_x == other.coordinate1_x
	   && coordinate1_y == other.coordinate1_y
	   && coordinate2_x == other.coordinate2_x
	   && coordinate2_y == other.coordinate2_y
	   // TODO: motor correction scanner type
	   && tilt_x_x == other.tilt_x_x
	   && tilt_x_y == other.tilt_x_y
	   && tilt_y_x == other.tilt_y_x
	   && tilt_y_y == other.tilt_y_y;
  }
  bool operator!= (scr_to_img_parameters &other) const
  {
    return !(*this == other);
  }
  int
  add_motor_correction_point (coord_t x, coord_t y)
  {
    int p = 0;

    motor_correction_x = (coord_t *)realloc ((void *)motor_correction_x, (n_motor_corrections + 1) * sizeof (coord_t));
    motor_correction_y = (coord_t *)realloc ((void *)motor_correction_y, (n_motor_corrections + 1) * sizeof (coord_t));
    for (p = n_motor_corrections; p > 0 && motor_correction_x[p-1] > x; p--)
      ;
    for (int p2 = n_motor_corrections; p2 > p; p2 --)
      {
	motor_correction_x[p2] = motor_correction_x[p2 - 1];
	motor_correction_y[p2] = motor_correction_y[p2 - 1];
      }
    motor_correction_x[p] = x;
    motor_correction_y[p] = y;
    n_motor_corrections++;
    return p;
  }
  void
  remove_motor_correction_point (int i)
  {
    for (; i < n_motor_corrections; i++)
    {
	motor_correction_x[i] = motor_correction_x[i + 1];
	motor_correction_y[i] = motor_correction_y[i + 1];
    }
    n_motor_corrections--;
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

  scr_to_img ()
  : m_motor_correction (NULL)
  {
  }
  ~scr_to_img ()
  {
    if (m_motor_correction)
      delete m_motor_correction;
  }

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

  /* Apply corrections that fix scanner optics that does not fit into the linear
     transformation matrix.  */
  void
  apply_early_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    apply_motor_correction (x, y, &x, &y);
    apply_lens_correction (x, y, xr, yr);
  }
  void
  inverse_early_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    inverse_lens_correction (x, y, &x, &y);
    inverse_motor_correction (x, y, xr, yr);
  }

  void
  apply_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
   if (!m_param.k1)
     {
       *xr = x;
       *yr = y;
     }
   /* Hack: Inverse correction works only for positive k1 otherwise
      it ends up with square root of a negative number.  */
   else if (m_param.k1 > 0)
      apply_lens_correction_1 (x, y, xr, yr, m_param.k1);
   else
      inverse_lens_correction_1 (x, y, xr, yr, -m_param.k1);
  }
  void
  inverse_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
   if (!m_param.k1)
     {
       *xr = x;
       *yr = y;
     }
   /* Hack: Inverse correction works only for positive k1 otherwise
      it ends up with square root of a negative number.  */
   else if (m_param.k1 > 0)
      inverse_lens_correction_1 (x, y, xr, yr, m_param.k1);
   else
      apply_lens_correction_1 (x, y, xr, yr, -m_param.k1);
  }

  /* Map screen coordinates to image coordinates.  */
  void
  to_img (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_matrix.perspective_transform (x,y, x, y);
    x += m_corrected_center_x;
    y += m_corrected_center_y;
    inverse_early_correction (x, y, xp, yp);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    coord_t xx = x, yy = y;
    apply_early_correction (xx, yy, &xx, &yy);
    xx -= m_corrected_center_x;
    yy -= m_corrected_center_y;
    m_matrix.inverse_perspective_transform (xx,yy, *xp, *yp);

    /* Verify that inverse is working.  */
    if (debug && 1)
      {
        to_img (*xp, *yp, &xx, &yy);
	if (fabs (xx - x) + fabs (yy - y) > 0.1)
	  abort ();
      }
  }
  enum scr_type
  get_type ()
  {
    return m_param.type;
  }
private:
  precomputed_function<coord_t> *m_motor_correction;
  /* Center of lenses: the middle of the scan.  */
  coord_t m_lens_center_x, m_lens_center_y;
  /* Center of the coordinate system in corrected coordinates.  */
  coord_t m_corrected_center_x, m_corrected_center_y;
  /* Radius in pixels of the lens circle and its inverse.  */
  coord_t m_lens_radius, m_inverse_lens_radius;
  /* Screen->image translation matrix.  */
  trans_matrix m_matrix;
  scr_to_img_parameters m_param;

  /* Apply spline defining motor correction.  */
  void
  apply_motor_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    *xr = x;
    *yr = y;
    if (!m_motor_correction)
      return;
    if (m_param.scanner_type == lens_move_horisontally)
      *xr = m_motor_correction->apply (x);
    else
      *yr = m_motor_correction->apply (y);
  }
  void
  inverse_motor_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    *xr = x;
    *yr = y;
    if (!m_motor_correction)
      return;
    if (m_param.scanner_type == lens_move_horisontally)
      *xr = m_motor_correction->invert (x);
    else
      *yr = m_motor_correction->invert (y);
  }
  /* Apply lens correction.  */
  void
  apply_lens_correction_1 (coord_t x, coord_t y, coord_t *xr, coord_t *yr, double k1)
  {
    coord_t xd = (x - m_lens_center_x);
    coord_t yd = (y - m_lens_center_y);
    coord_t powradius = (xd * xd + yd * yd) * m_inverse_lens_radius * m_inverse_lens_radius;
    *xr = x + xd * powradius * k1;
    *yr = y + yd * powradius * k1;
  }
  /* Invert lens correction.  */
  void
  inverse_lens_correction_1 (coord_t x, coord_t y, coord_t *xr, coord_t *yr, double k1)
  {
    coord_t xd = (x - m_lens_center_x) * m_inverse_lens_radius;
    coord_t yd = (y - m_lens_center_y) * m_inverse_lens_radius;
    coord_t rpow2 = xd * xd + yd * yd;
    coord_t r = my_sqrt (rpow2);
    /* An inverse as given by https://www.wolframalpha.com/input?i=x%2Bk*x*x*x-r%3D0  */
    coord_t k1pow2 = k1 * k1;
    coord_t k1pow3 = k1 * k1pow2;
    coord_t k1pow4 = k1pow2 * k1pow2;
    coord_t sqrt3 = 1.73205080757;
    coord_t cbrt2 = 1.25992104989;
    coord_t val = 27 * k1pow4 * rpow2 + 4 * k1pow3;
    if (!(val >= 0))
      val = 0;
    coord_t coef = my_cbrt (9 * k1pow2 * r + sqrt3 * my_sqrt (val));
    coord_t radius = coef / (cbrt2 * (coord_t)2.08008382305 /* 3^(2/3) */ * k1)
		      - (coord_t)0.87358046473629 /* cbrt(2/3) */ / coef;

    *xr = xd / r * radius * m_lens_radius + m_lens_center_x;
    *yr = yd / r * radius * m_lens_radius + m_lens_center_y;
  }
  const bool debug = false;
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
