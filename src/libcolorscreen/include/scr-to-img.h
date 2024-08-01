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

typedef matrix4x4<coord_t> trans_4d_matrix;
typedef matrix3x3<coord_t> trans_3d_matrix;
typedef matrix2x2<coord_t> trans_2d_matrix;

class image_data;

/* Mapping between screen and image.  */
class scr_to_img
{
public:
  DLL_PUBLIC void set_parameters (const scr_to_img_parameters &param,
                                  const image_data &img,
                                  coord_t rotation_adjustment = 0,
                                  bool need_inverse = true);
  void update_linear_parameters (scr_to_img_parameters &param);
  void update_scr_to_final_parameters (coord_t final_ratio,
                                       coord_t final_angle);
  void get_range (int img_width, int img_height, int *scr_xshift,
                  int *scr_yshift, int *scr_width, int *scr_height);
  void get_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2,
                  int *scr_xshift, int *scr_yshift, int *scr_width,
                  int *scr_height);
  void get_final_range (int img_width, int img_height, int *final_xshift,
                        int *final_yshift, int *final_width,
                        int *final_height);
  void get_final_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2,
                        int *final_xshift, int *final_yshift, int *final_width,
                        int *final_height);
  coord_t
  get_rotation_adjustment () const
  {
    return m_rotation_adjustment;
  }

  scr_to_img () : m_motor_correction (NULL) {}
  ~scr_to_img ()
  {
    if (m_motor_correction)
      delete m_motor_correction;
  }

  /* Apply corrections that fix scanner optics that does not fit into the
     linear transformation matrix.  */
  pure_attr point_t
  apply_early_correction (point_t p) const
  {
    p = apply_motor_correction (p);
    return m_lens_correction.scan_to_corrected (p)
           * m_inverted_projection_distance;
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
    point_t shift = { 0, 0 };
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.corrected_to_scan (sp - shift) + shift;
  }
  pure_attr point_t
  inverse_lens_correction (point_t sp) const
  {
    point_t shift = { 0, 0 };
    if (m_param.scanner_type == lens_move_horisontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.scan_to_corrected (sp - shift) + shift;
  }

  /* Map screen coordinates to image coordinates.  */
  pure_attr inline point_t
  to_img (point_t p) const
  {
    if (m_param.mesh_trans)
      return m_param.mesh_trans->apply (p);
    m_scr_to_img_homography_matrix.perspective_transform (p.x, p.y, p.x, p.y);
    return inverse_early_correction (p);
  }
  pure_attr inline bool
  to_img_in_mesh_range (point_t p) const
  {
    if (!m_param.mesh_trans)
      return true;
    return m_param.mesh_trans->in_range_p (p.x, p.y);
  }
  /* Map image coordinats to screen.  */
  pure_attr inline point_t
  to_scr (point_t p) const
  {
    point_t inp = p;
    if (m_param.mesh_trans)
      return m_param.mesh_trans->invert (p);
    else
      {
        p = apply_early_correction (p);
        /* For scanners with moving lens the inverse transform may not
         * correspond to homography.  */
        if (m_do_homography)
          m_img_to_scr_homography_matrix.perspective_transform (p.x, p.y, p.x,
                                                                p.y);
        else
          {
            m_perspective_matrix.inverse_perspective_transform (p.x, p.y, p.x,
                                                                p.y);
            m_inverse_matrix.apply (p.x, p.y, &p.x, &p.y);
          }
      }

    /* Verify that inverse is working.  */
    if (debug)
      {
        point_t np = to_img (p);
        if (!np.almost_eq (inp) && m_nwarnings < 10)
          {
            printf ("Warning: to_scr is not inverted by to_img %f %f turns to "
                    "%f %f\n",
                    inp.x, inp.y, np.x, np.y);
            m_nwarnings++;
            if (colorscreen_checking && 0)
              abort ();
          }
      }
    return p;
  }
  pure_attr inline point_t
  scr_to_final (point_t p) const
  {
    m_scr_to_final_matrix.apply_to_vector (p.x, p.y, &p.x, &p.y);
    return p;
  }
  pure_attr inline point_t
  final_to_scr (point_t p) const
  {
    m_final_to_scr_matrix.apply_to_vector (p.x, p.y, &p.x, &p.y);
    return p;
  }
  pure_attr inline point_t
  img_to_final (point_t p) const
  {
    return scr_to_final (to_scr (p));
  }
  pure_attr inline point_t
  final_to_img (point_t p) const
  {
    return to_img (final_to_scr (p));
  }
  enum scr_type
  get_type () const
  {
    return m_param.type;
  }
  const scr_to_img_parameters &
  get_param () const
  {
    return m_param;
  }
  rgbdata
  patch_proportions (const render_parameters *rparam) const
  {
    return ::patch_proportions (m_param.type, rparam);
  }
  pure_attr coord_t
  pixel_size (int img_width, int img_height) const
  {
    coord_t bx = img_width / 2, by = img_height / 2;
    return to_scr ({ bx + 0, by + 0 }).dist_from (to_scr ({ bx + 1, by + 0 }));
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
  DLL_PUBLIC static std::atomic_ulong m_nwarnings;
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
  inline pure_attr point_t
  apply_motor_correction (point_t p) const
  {
    if (!m_motor_correction)
      return p;
    if (m_param.scanner_type == lens_move_horisontally
        || m_param.scanner_type == fixed_lens_sensor_move_horisontally)
      p.x = m_motor_correction->apply (p.x);
    else
      p.y = m_motor_correction->apply (p.y);
    return p;
  }
  inline pure_attr point_t
  inverse_motor_correction (point_t p) const
  {
    if (!m_motor_correction)
      return p;
    if (m_param.scanner_type == lens_move_horisontally
        || m_param.scanner_type == fixed_lens_sensor_move_vertically)
      p.x = m_motor_correction->invert (p.x);
    else
      p.y = m_motor_correction->invert (p.y);
    return p;
  }
  static const bool debug = colorscreen_checking;
  void initialize ();
};
#endif
