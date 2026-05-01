/* Mapping between screen and scan coordinates.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of ColorScreen.  */

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
#include "imagedata.h"
namespace colorscreen
{
typedef matrix4x4<coord_t> trans_4d_matrix;
typedef matrix3x3<coord_t> trans_3d_matrix;
typedef matrix2x2<coord_t> trans_2d_matrix;

class image_data;

/* Mapping between screen and image.  */
class scr_to_img
{
public:
  /* Initialize the mapping with given parameters for image of WIDTH and HEIGHT.
     ROTATION_ADJUSTMENT can be used to adjust the rotation.
     Return true on success.  */
  nodiscard_attr DLL_PUBLIC bool set_parameters (const scr_to_img_parameters &param,
				  int width, int height,
                                  coord_t rotation_adjustment = 0);
  /* Initialize the mapping for early correction only.
     Return true on success.  */
  nodiscard_attr bool set_parameters_for_early_correction (const scr_to_img_parameters &param,
							   int width, int height);
  /* Update mapping for new linear parameters PARAM.  */
  void update_linear_parameters (scr_to_img_parameters &param);
  /* Update mapping for new FINAL_RATIO and FINAL_ANGLE.  */
  void update_scr_to_final_parameters (coord_t final_ratio,
                                       coord_t final_angle);
  /* Determine rectangular section of the screen to which image
     with dimensions IMG_WIDTH x IMG_HEIGHT fits.  */
  int_image_area get_range (int img_width, int img_height) const noexcept;
  /* Determine rectangular section of the screen to which image section
     from (X1, Y1) to (X2, Y2) fits.  */
  int_image_area get_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2) const noexcept;
  /* Determine rectangular section of the final coordinates to which image
     with dimensions IMG_WIDTH x IMG_HEIGHT fits.  */
  int_image_area get_final_range (int img_width, int img_height) const noexcept;
  /* Determine rectangular section of the final coordinates to which image section
     from (X1, Y1) to (X2, Y2) fits.  */
  int_image_area get_final_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2) const noexcept;

  /* Get area of image to which screen area fits.  */
  int_image_area get_img_range (int_image_area area) const noexcept;
  /* Return current rotation adjustment.  */
  pure_attr coord_t
  get_rotation_adjustment () const noexcept
  {
    return m_rotation_adjustment;
  }

  /* Default constructor.  */
  scr_to_img () = default;

  /* Apply corrections that fix scanner optics that does not fit into the
     linear transformation matrix.  */
  pure_attr point_t
  apply_early_correction (point_t p) const noexcept
  {
    assert (!debug || (m_early_correction_precomputed && !m_scr_to_img_mesh));
    return apply_lens_correction (p)
           * m_inverted_projection_distance;
  }

  /* Return true if early correction is precomputed.  */
  pure_attr bool
  early_correction_precomputed () const noexcept
  {
    return m_early_correction_precomputed;
  }

  /* Apply early correction to point P without using precomputed values.  */
  pure_attr point_t
  nonprecomputed_apply_early_correction (point_t p) const noexcept
  {
    return nonprecomputed_apply_lens_correction (p)
           * m_inverted_projection_distance;
  }
  pure_attr point_t
  inverse_early_correction (point_t p) const noexcept
  {
    return inverse_lens_correction (p * m_param.projection_distance);
  }


  /* Map screen coordinates to image coordinates.  */
  pure_attr inline point_t
  to_img (point_t p) const noexcept
  {
    if (m_scr_to_img_mesh)
      return m_scr_to_img_mesh->apply (p);
    p = m_scr_to_img_homography_matrix.perspective_transform (p);
    return inverse_early_correction (p);
  }
  pure_attr inline bool
  to_img_in_mesh_range (point_t p) const noexcept
  {
    if (!m_scr_to_img_mesh)
      return true;
    return m_scr_to_img_mesh->in_range_p (p.x, p.y);
  }
  /* Map image coordinates to screen.  */
  pure_attr inline point_t
  to_scr (point_t p) const noexcept
  {
    point_t inp = p;
    if (m_img_to_scr_mesh)
      return m_img_to_scr_mesh->apply (p);
    //if (m_scr_to_img_mesh)
      //return m_scr_to_img_mesh->invert (p);
    else
      {
        p = apply_early_correction (p);
        /* For scanners with moving lens the inverse transform may not
           correspond to homography.  */
        if (m_do_homography)
          p = m_img_to_scr_homography_matrix.perspective_transform (p);
        else
          {
            p = m_perspective_matrix.inverse_perspective_transform (p);
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
  scr_to_final (point_t p) const noexcept
  {
    m_scr_to_final_matrix.apply_to_vector (p.x, p.y, &p.x, &p.y);
    return p;
  }
  pure_attr inline point_t
  final_to_scr (point_t p) const noexcept
  {
    m_final_to_scr_matrix.apply_to_vector (p.x, p.y, &p.x, &p.y);
    return p;
  }
  pure_attr inline point_t
  img_to_final (point_t p) const noexcept
  {
    return scr_to_final (to_scr (p));
  }
  pure_attr inline point_t
  final_to_img (point_t p) const noexcept
  {
    return to_img (final_to_scr (p));
  }
  /* Return screen type.  */
  pure_attr enum scr_type
  get_type () const noexcept
  {
    return m_param.type;
  }
  /* Return current parameters.  */
  pure_attr const scr_to_img_parameters &
  get_param () const noexcept
  {
    return m_param;
  }
  /* Return patch proportions for RPMARAM.  */
  pure_attr rgbdata
  patch_proportions (const render_parameters *rparam) const noexcept
  {
    return colorscreen::patch_proportions (m_param.type, rparam);
  }
  /* Estimate pixel size for image of dimensions IMG_WIDTH x IMG_HEIGHT.  */
  pure_attr coord_t pixel_size (int_image_area area) const noexcept;
  /* Initialize the mapping with given parameters for IMG.
     ROTATION_ADJUSTMENT can be used to adjust the rotation.
     Return true on success.  */
  nodiscard_attr inline bool
  set_parameters (const scr_to_img_parameters &param,
		  const image_data &img,
		  coord_t rotation_adjustment = 0)
  {
    return set_parameters (param, img.width, img.height, rotation_adjustment);
  }
  /* Dump state of the mapping to file F.  */
  void dump (FILE *f) const;

private:
  /* Inversed m_params.projection_distance.  */
  coord_t m_inverted_projection_distance = 0;
  /* Perspective correction matrix.  */
  trans_4d_matrix m_perspective_matrix;
  /* final matrix producing screen coordinates.  */
  trans_3d_matrix m_matrix;
  /* Inverted matrix.  */
  trans_3d_matrix m_inverse_matrix;
  /* Homography matrix for scr to img transforms.  */
  trans_4d_matrix m_scr_to_img_homography_matrix;
  /* Homography matrix for img to scr transforms.  */
  trans_4d_matrix m_img_to_scr_homography_matrix;
  /* Number of warnings issued.  */
  DLL_PUBLIC static std::atomic_ulong m_nwarnings;
  /* True if we should use homography matrix for scr to img transforms.
     We disable it for moving lens scanners since inverse transform is not
     necessarily a homography.  */
  bool m_do_homography = false;
  /* True if early correction is precomputed.  */
  bool m_early_correction_precomputed = false;

  /* Matrix transforming final cordinates to screen coordinates.  */
  trans_2d_matrix m_final_to_scr_matrix;
  /* Matrix transforming screen coordinates to final coordinates.  */
  trans_2d_matrix m_scr_to_final_matrix;

  /* Parameters of the mapping.  */
  scr_to_img_parameters m_param;
  /* Current rotation adjustment.  */
  coord_t m_rotation_adjustment = 0;
  /* Lens correction logic.  */
  lens_warp_correction m_lens_correction;

  std::shared_ptr<mesh> m_scr_to_img_mesh;
  std::shared_ptr<mesh> m_img_to_scr_mesh;

  /* Return inverse lens correction for scan point SP.  */
  pure_attr point_t
  inverse_lens_correction (point_t sp) const noexcept
  {
    point_t shift = { 0, 0 };
    if (m_param.scanner_type == lens_move_horizontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.corrected_to_scan (sp - shift) + shift;
  }
  /* Apply lens correction to point SP without using precomputed values.  */
  pure_attr point_t
  nonprecomputed_apply_lens_correction (point_t sp) const noexcept
  {
    point_t shift = { 0, 0 };
    if (m_param.scanner_type == lens_move_horizontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.nonprecomputed_scan_to_corrected (sp - shift) + shift;
  }
  /* Apply lens correction to point SP using precomputed values.  */
  pure_attr point_t
  apply_lens_correction (point_t sp) const noexcept
  {
    point_t shift = { 0, 0 };
    if (m_param.scanner_type == lens_move_horizontally)
      shift.x = sp.x;
    if (m_param.scanner_type == lens_move_vertically)
      shift.y = sp.y;
    return m_lens_correction.scan_to_corrected (sp - shift) + shift;
  }
  /* True if checking is enabled.  */
  static const bool debug = colorscreen_checking;
  /* Initialize the mapping matrices.  */
  void initialize ();
};
}
#endif
