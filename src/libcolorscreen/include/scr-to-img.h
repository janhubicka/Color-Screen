#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
#include <atomic>
#include <climits>
#include "base.h"
#include "color.h"
#include "dllpublic.h"
#include "matrix.h"
#include "precomputed-function.h"
#include "mesh.h"
#include "lens-correction.h"

/* Windows does not seem to define this by default.  */
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


typedef matrix4x4<coord_t> trans_4d_matrix;
typedef matrix3x3<coord_t> trans_3d_matrix;
typedef matrix2x2<coord_t> trans_2d_matrix;
class render;
struct render_parameters;

/* Types of supported screens.  */
enum scr_type
{
   Random,
   Paget,
   Thames,
   Finlay,
   Dufay,
   max_scr_type
};
DLL_PUBLIC extern const char * const scr_names[max_scr_type];
DLL_PUBLIC rgbdata patch_proportions (enum scr_type t, render_parameters *);

/* Type of a scanner used.  */
enum scanner_type {
	fixed_lens,
	fixed_lens_sensor_move_horisontally,
	fixed_lens_sensor_move_vertically,
	lens_move_horisontally,
	lens_move_vertically,
	max_scanner_type
};

inline bool is_fixed_lens (scanner_type type)
{
  return type == fixed_lens || type == fixed_lens_sensor_move_horisontally || type == fixed_lens_sensor_move_vertically;
}

DLL_PUBLIC extern const char * const scanner_type_names[max_scanner_type];

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

struct scr_to_img_parameters
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

  /* Distance of the perspective pane from the camera coordinate.  */
  coord_t projection_distance;
  /* Perspective tilt in x and y coordinate in degrees.  */
  coord_t tilt_x, tilt_y;

  /* Rotation from screen coordinates to final coordinates.  */
  coord_t final_rotation;
  /* Angle of the screen X and Y axis in the final coordinates.  */
  coord_t final_angle;
  /* Ratio of the X and Y axis in the final coordinates.  */
  coord_t final_ratio;

  /* Stepping motor correction is described by a spline.  */
  coord_t *motor_correction_x, *motor_correction_y;
  int n_motor_corrections;

  mesh *mesh_trans;

  enum scr_type type;
  enum scanner_type scanner_type;

  lens_warp_correction_parameters lens_correction;

  scr_to_img_parameters ()
  : center_x (0), center_y (0), coordinate1_x(5), coordinate1_y (0), coordinate2_x (0), coordinate2_y (5),
    projection_distance (1), tilt_x (0), tilt_y(0), 
    final_rotation (0), final_angle (90), final_ratio (1), motor_correction_x (NULL), motor_correction_y (NULL),
    n_motor_corrections (0), mesh_trans (NULL), type (Finlay), scanner_type (fixed_lens), lens_correction ()
  { }
  scr_to_img_parameters (const scr_to_img_parameters &from)
  : center_x (from.center_x), center_y (from.center_y),
    coordinate1_x(from.coordinate1_x), coordinate1_y (from.coordinate1_y),
    coordinate2_x (from.coordinate2_x), coordinate2_y (from.coordinate2_y),
    projection_distance (from.projection_distance), tilt_x (from.tilt_x), tilt_y(from.tilt_y), 
    final_rotation (from.final_rotation), final_angle (from.final_angle), final_ratio (from.final_ratio), motor_correction_x (NULL), motor_correction_y (NULL),
    n_motor_corrections (from.n_motor_corrections),
    mesh_trans (from.mesh_trans), type (from.type), scanner_type (from.scanner_type), lens_correction (from.lens_correction)
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
    projection_distance = from.projection_distance;
    tilt_x = from.tilt_x;
    tilt_y = from.tilt_y;
    final_rotation = from.final_rotation;
    final_angle = from.final_angle;
    final_ratio = from.final_ratio;
    type = from.type;
    scanner_type = from.scanner_type;
    mesh_trans = from.mesh_trans;
    lens_correction = from.lens_correction;
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
	   && projection_distance == other.projection_distance
	   && final_rotation == other.final_rotation
	   && final_angle == other.final_angle
	   && final_ratio == other.final_ratio
	   && tilt_x == other.tilt_x
	   && tilt_y == other.tilt_y
	   && type == other.type
	   && scanner_type == other.scanner_type
	   && lens_correction == other.lens_correction;
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
  coord_t
  get_xlen ()
  {
    return sqrt (coordinate1_x * coordinate1_x + coordinate1_y * coordinate1_y);
  }
  coord_t
  get_ylen ()
  {
    return sqrt (coordinate2_x * coordinate2_x + coordinate2_y * coordinate2_y);
  }
  coord_t
  get_angle ()
  {
    coord_t dot = coordinate1_x*coordinate2_x + coordinate1_y*coordinate2_y;
    //coord_t det = coordinate1_x*coordinate2_y - coordinate1_y*coordinate2_x;
    //return atan2(det, dot) * 180 / M_PI;
    return acos (dot / (get_xlen () * get_ylen ())) * (180 / M_PI);
  }
};

class image_data;

/* Mapping between screen and image.  */
class scr_to_img
{
public:
  DLL_PUBLIC void set_parameters (const scr_to_img_parameters &param, const image_data &img, coord_t rotation_adjustment = 0, bool need_inverse = true);
  void update_linear_parameters (scr_to_img_parameters &param);
  void update_scr_to_final_parameters (coord_t final_ratio, coord_t final_angle);
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
  coord_t get_rotation_adjustment ()
  {
    return m_rotation_adjustment;
  }

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
    point_t p = m_lens_correction.scan_to_corrected ({x,y});
    *xr = p.x * m_inverted_projection_distance;
    *yr = p.y * m_inverted_projection_distance;
  }
  void
  inverse_early_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    x *= m_param.projection_distance;
    y *= m_param.projection_distance;
    point_t p = m_lens_correction.corrected_to_scan ({x,y});
    inverse_motor_correction (p.x, p.y, xr, yr);
  }

  void
  apply_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    point_t sp = {x,y};
    point_t shift = {0, 0};
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    point_t p = m_lens_correction.corrected_to_scan (sp-shift)+shift;
    *xr = p.x;
    *yr = p.y;
  }
  void
  inverse_lens_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    point_t sp = {x,y};
    point_t shift = {0, 0};
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    point_t p = m_lens_correction.scan_to_corrected (sp-shift)+shift;
    *xr = p.x;
    *yr = p.y;
  }

  /* Map screen coordinates to image coordinates.  */
  void
  to_img (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    if (m_param.mesh_trans)
      {
 	point_t p = m_param.mesh_trans->apply ({x, y});
	*xp = p.x;
	*yp = p.y;
	return;
      }
#if 0
    coord_t xx, yy;
    m_scr_to_img_homography_matrix.perspective_transform (x, y, xx, yy);
    m_matrix.apply (x,y, &x, &y);
    m_perspective_matrix.perspective_transform (x,y, x, y);
    assert (fabs (x-xx) < 0.1);
    assert (fabs (y-yy) < 0.1);
#else
    m_scr_to_img_homography_matrix.perspective_transform (x, y, x, y);
#if 0
    if (m_do_homography)
      m_scr_to_img_homography_matrix.perspective_transform (x, y, x, y);
    else
      {
	m_matrix.apply (x,y, &x, &y);
	m_perspective_matrix.perspective_transform (x,y, x, y);
      }
#endif
#endif
    inverse_early_correction (x, y, xp, yp);
  }
  bool
  to_img_in_mesh_range (coord_t x, coord_t y)
  {
    if (!m_param.mesh_trans)
      return true;
    return m_param.mesh_trans->in_range_p (x, y);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    coord_t xx = x, yy = y;
    if (m_param.mesh_trans)
      {
	point_t p = m_param.mesh_trans->invert ({x, y});
	*xp = p.x;
	*yp = p.y;
	/* (-1,-1) is used to sgnalize that inverse is not computed.  */
	if (debug && *xp == -1 && *yp == -1)
	  return;
      }
    else
      {
	apply_early_correction (xx, yy, &xx, &yy);
#if 0
        coord_t xx2, yy2;
        m_img_to_scr_homography_matrix.perspective_transform (xx, yy, xx2, yy2);
	m_perspective_matrix.inverse_perspective_transform (xx,yy, xx, yy);
	m_inverse_matrix.apply (xx,yy, xp, yp);
        assert (fabs (xx2-*xp) < 0.1);
        assert (fabs (yy2-*yp) < 0.1);
#else
	if (m_do_homography)
	  m_img_to_scr_homography_matrix.perspective_transform (xx, yy, *xp, *yp);
	else
	  {
	    m_perspective_matrix.inverse_perspective_transform (xx,yy, xx, yy);
	    m_inverse_matrix.apply (xx,yy, xp, yp);
	  }
#endif
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
  const scr_to_img_parameters &get_param ()
  {
    return m_param;
  }
  rgbdata patch_proportions (render_parameters *rparam)
  {
    return ::patch_proportions (m_param.type, rparam);
  }
  pure_attr coord_t
  pixel_size (int img_width, int img_height)
  {
    coord_t x,x2, y, y2;
    coord_t bx = img_width / 2, by = img_height / 2;
    to_scr (bx + 0, by + 0, &x, &y);
    to_scr (bx + 1, by + 0, &x2, &y2);
    return my_sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));
  }
  void dump (FILE *f);
private:
  precomputed_function<coord_t> *m_motor_correction;
  /* Inversed m_params.projection_distance.  */
  coord_t m_inverted_projection_distance;
  /* Perspective correction matrix.  */
  trans_4d_matrix m_perspective_matrix;
  /* final matrix producing screen coordinates.  */
  trans_3d_matrix m_matrix;
  /* Inverted matrix.  */
  trans_3d_matrix m_inverse_matrix;
  trans_4d_matrix m_scr_to_img_homography_matrix;
  trans_4d_matrix m_img_to_scr_homography_matrix;
  std::atomic_ulong m_nwarnings;
  /* True if we should use homography matrix for scr to img transforms.
     We disable it for moving lens scanners since inverse transform is not
     necessarily a homography.  */
  bool m_do_homography;

  /* Matrix transforming final cordinates to screen coordinates.  */
  trans_2d_matrix m_final_to_scr_matrix;
  trans_2d_matrix m_scr_to_final_matrix;

  scr_to_img_parameters m_param;
  coord_t m_rotation_adjustment;
  lens_warp_correction m_lens_correction;

  /* Apply spline defining motor correction.  */
  void
  apply_motor_correction (coord_t x, coord_t y, coord_t *xr, coord_t *yr)
  {
    *xr = x;
    *yr = y;
    if (!m_motor_correction)
      return;
    if (m_param.scanner_type == lens_move_horisontally || m_param.scanner_type == fixed_lens_sensor_move_horisontally)
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
    if (m_param.scanner_type == lens_move_horisontally || m_param.scanner_type == fixed_lens_sensor_move_vertically)
      *xr = m_motor_correction->invert (x);
    else
      *yr = m_motor_correction->invert (y);
  }
  static const bool debug = colorscreen_checking;
  void initialize ();
};

#endif
