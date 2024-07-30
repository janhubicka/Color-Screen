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
#include "scr-to-img-parameters.h"

/* Windows does not seem to define this by default.  */
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


typedef matrix4x4<coord_t> trans_4d_matrix;
typedef matrix3x3<coord_t> trans_3d_matrix;
typedef matrix2x2<coord_t> trans_2d_matrix;

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
  coord_t get_rotation_adjustment () const
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
  pure_attr point_t
  apply_early_correction (point_t p) const
  {
    p = apply_motor_correction (p);
    return m_lens_correction.scan_to_corrected (p) * m_inverted_projection_distance;
  }
  pure_attr point_t
  inverse_early_correction (point_t p) const
  {
    p = m_lens_correction.corrected_to_scan (p * m_param.projection_distance);
    return inverse_motor_correction (p);
  }

  pure_attr point_t
  apply_lens_correction (point_t sp) const
  {
    point_t shift = {0, 0};
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.corrected_to_scan (sp-shift)+shift;
  }
  pure_attr point_t
  inverse_lens_correction (point_t sp) const
  {
    point_t shift = {0, 0};
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.scan_to_corrected (sp-shift)+shift;
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
    point_t p = inverse_early_correction ({x, y});
    *xp = p.x;
    *yp = p.y;
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
	point_t p = apply_early_correction ({xx, yy});
#if 0
        coord_t xx2, yy2;
        m_img_to_scr_homography_matrix.perspective_transform (xx, yy, xx2, yy2);
	m_perspective_matrix.inverse_perspective_transform (xx,yy, xx, yy);
	m_inverse_matrix.apply (xx,yy, xp, yp);
        assert (fabs (xx2-*xp) < 0.1);
        assert (fabs (yy2-*yp) < 0.1);
#else
	if (m_do_homography)
	  m_img_to_scr_homography_matrix.perspective_transform (p.x, p.y, *xp, *yp);
	else
	  {
	    m_perspective_matrix.inverse_perspective_transform (p.x, p.y, xx, yy);
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
	    if (colorscreen_checking && 0)
	      abort ();
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
  pure_attr point_t
  apply_motor_correction (point_t p) const
  {
    if (!m_motor_correction)
      return p;
    if (m_param.scanner_type == lens_move_horisontally || m_param.scanner_type == fixed_lens_sensor_move_horisontally)
      p.x = m_motor_correction->apply (p.x);
    else
      p.y = m_motor_correction->apply (p.y);
    return p;
  }
  pure_attr point_t
  inverse_motor_correction (point_t p) const
  {
    if (!m_motor_correction)
      return p;
    if (m_param.scanner_type == lens_move_horisontally || m_param.scanner_type == fixed_lens_sensor_move_vertically)
      p.x = m_motor_correction->invert (p.x);
    else
      p.y = m_motor_correction->invert (p.y);
    return p;
  }
  static const bool debug = colorscreen_checking;
  void initialize ();
};

#endif
