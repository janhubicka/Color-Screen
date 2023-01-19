#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
#include <atomic>
#include "base.h"
#include "dllpublic.h"
#include "matrix.h"
#include "precomputed-function.h"
#include "mesh.h"

/* Windows does not seem to define this by default.  */
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


typedef matrix4x4<coord_t> trans_4d_matrix;
typedef matrix3x3<coord_t> trans_3d_matrix;
typedef matrix2x2<coord_t> trans_2d_matrix;

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
   gren dots again.
 
   In order to turn scan coordinates to screen the following transformations
   are performed
     1) motor correction
     2) translation to move lens_center to (0,0)
     3) lens correction
     4) perspective correction (with tilt applied)
     5) translation to move center to (0,0)
     6) change of basis so center+coordinate1 becomes (1.0) and center+coordinate2 becomes (0,1)
*/

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
  /* Distance of the perspective pane from the camera coordinate.  */
  coord_t projection_distance;
  /* Perspective tilt in x and y coordinate in degrees.  */
  coord_t tilt_x, tilt_y;
  coord_t k1;

  /* Rotation from screen coordinates to final coordinates.  */
  coord_t final_rotation;

  /* Stepping motor correction is described by a spline.  */
  coord_t *motor_correction_x, *motor_correction_y;
  int n_motor_corrections;

  mesh *mesh_trans;

  enum scr_type type;
  enum scanner_type scanner_type;

  scr_to_img_parameters ()
  : center_x (0), center_y (0), coordinate1_x(5), coordinate1_y (0), coordinate2_x (0), coordinate2_y (5),
    lens_center_x (0), lens_center_y (0), projection_distance (1), tilt_x (0), tilt_y(0), k1(0),
    final_rotation (0), motor_correction_x (NULL), motor_correction_y (NULL),
    n_motor_corrections (0), mesh_trans (NULL), type (Finlay), scanner_type (fixed_lens)
  { }
  scr_to_img_parameters (const scr_to_img_parameters &from)
  : center_x (from.center_x), center_y (from.center_y),
    coordinate1_x(from.coordinate1_x), coordinate1_y (from.coordinate1_y),
    coordinate2_x (from.coordinate2_x), coordinate2_y (from.coordinate2_y),
    lens_center_x (from.lens_center_x), lens_center_y (from.lens_center_y),
    projection_distance (from.projection_distance), tilt_x (from.tilt_x), tilt_y(from.tilt_y) , k1(from.k1),
    final_rotation (0), motor_correction_x (NULL), motor_correction_y (NULL),
    n_motor_corrections (from.n_motor_corrections),
    mesh_trans (from.mesh_trans), type (from.type), scanner_type (from.scanner_type)
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
    projection_distance = from.projection_distance;
    tilt_x = from.tilt_x;
    tilt_y = from.tilt_y;
    k1 = from.k1;
    final_rotation = from.final_rotation;
    type = from.type;
    scanner_type = from.scanner_type;
    mesh_trans = from.mesh_trans;
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
    if (n_motor_corrections != other.n_motor_corrections)
      return false;
    for (int i = 0; i < n_motor_corrections; i++)
      if (motor_correction_x[i] != other.motor_correction_x[i]
	  || motor_correction_y[i] != other.motor_correction_y[i])
	return false;
    return center_x == other.center_x
	   && center_y == other.center_y
	   && coordinate1_x == other.coordinate1_x
	   && coordinate1_y == other.coordinate1_y
	   && coordinate2_x == other.coordinate2_x
	   && coordinate2_y == other.coordinate2_y
	   && lens_center_x == other.lens_center_x
	   && lens_center_y == other.lens_center_y
	   && projection_distance == other.projection_distance
	   && k1 == other.k1
	   && final_rotation == other.final_rotation
	   && tilt_x == other.tilt_x
	   && tilt_y == other.tilt_y
	   && type == other.type
	   && scanner_type == other.scanner_type;
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
  coord_t
  screen_per_inch ()
  {
    if (type == Dufay)
      /* Dufaycolor manual promises 500 lines per inch.  Screen has 2 lines  */
      return 250;
    else
      /* 2 squares per screen.  */
      return 25.4 / 2;
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
  void get_final_range (int img_width, int img_height,
			int *final_xshift, int *final_yshift,
			int *final_width, int *final_height);
  void get_final_range (coord_t x1, coord_t y1,
			coord_t x2, coord_t y2,
			int *final_xshift, int *final_yshift,
			int *final_width, int *final_height);

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
    x -= m_corrected_lens_center_x;
    y -= m_corrected_lens_center_y;
    x *= m_inverted_projection_distance;
    y *= m_inverted_projection_distance;
    apply_lens_correction (x, y, xr, yr);
  }
  void
  inverse_early_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    inverse_lens_correction (x, y, &x, &y);
    x *= m_param.projection_distance;
    y *= m_param.projection_distance;
    x += m_corrected_lens_center_x;
    y += m_corrected_lens_center_y;
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
    if (m_param.mesh_trans)
      {
 	m_param.mesh_trans->apply (x, y, xp, yp);
	return;
      }
    m_matrix.apply (x,y, &x, &y);
    m_perspective_matrix.perspective_transform (x,y, x, y);
    inverse_early_correction (x, y, xp, yp);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    coord_t xx = x, yy = y;
    if (m_param.mesh_trans)
      {
	m_param.mesh_trans->invert (x, y, xp, yp);
	/* (-1,-1) is used to sgnalize that inverse is not computed.  */
	if (debug && *xp == -1 && *yp == -1)
	  return;
      }
    else
      {
	apply_early_correction (xx, yy, &xx, &yy);
	m_perspective_matrix.inverse_perspective_transform (xx,yy, xx, yy);
	m_inverse_matrix.apply (xx,yy, xp, yp);
      }

    /* Verify that inverse is working.  */
    if (debug)
      {
        to_img (*xp, *yp, &xx, &yy);
	if (fabs (xx - x) + fabs (yy - y) > 0.1 && m_nwarnings < 10)
	  {
	    printf ("Warning: to_scr is not inverted by to_img %f %f turns to %f %f\n", x, y, xx, yy);
	    m_nwarnings++;
	    //abort ();
	  }
      }
  }
  void
  scr_to_final (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_scr_to_final_matrix.apply_to_vector (x, y, xp, yp);
  }
  void
  final_to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_final_to_scr_matrix.apply_to_vector (x, y, xp, yp);
  }
  void
  img_to_final (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    to_scr (x, y, &x, &y);
    scr_to_final (x, y, xp, yp);
  }
  void
  final_to_img (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    final_to_scr (x, y, &x, &y);
    to_img (x, y, xp, yp);
  }
  enum scr_type
  get_type ()
  {
    return m_param.type;
  }
  void dump (FILE *f);
private:
  precomputed_function<coord_t> *m_motor_correction;
  /* Center of lenses after the motor corrections.  */
  coord_t m_corrected_lens_center_x, m_corrected_lens_center_y;
  /* Radius in pixels of the lens circle and its inverse.  */
  coord_t m_lens_radius, m_inverse_lens_radius;
  /* Inversed m_params.projection_distance.  */
  coord_t m_inverted_projection_distance;
  /* Perspective correction matrix.  */
  trans_4d_matrix m_perspective_matrix;
  /* final matrix producing screen coordinates.  */
  trans_3d_matrix m_matrix;
  /* Invertedd matrix.  */
  trans_3d_matrix m_inverse_matrix;
  std::atomic_ulong m_nwarnings;

  /* Matrix transforming final cordinates to screen coordinates.  */
  trans_2d_matrix m_final_to_scr_matrix;
  trans_2d_matrix m_scr_to_final_matrix;

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
    coord_t powradius = (x * x + y * y) * m_inverse_lens_radius * m_inverse_lens_radius;
    *xr = x + x * powradius * k1;
    *yr = y + y * powradius * k1;
  }
  /* Invert lens correction.  */
  void
  inverse_lens_correction_1 (coord_t x, coord_t y, coord_t *xr, coord_t *yr, double k1)
  {
    coord_t xd = x * m_inverse_lens_radius;
    coord_t yd = y * m_inverse_lens_radius;
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

    *xr = xd / r * radius * m_lens_radius;
    *yr = yd / r * radius * m_lens_radius;
  }
  const bool debug = false;
};
#endif
