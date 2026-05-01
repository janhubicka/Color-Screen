/* Mapping between screen and scan coordinates.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of ColorScreen.  */

#include <cmath>
#include <algorithm>
#include "include/colorscreen.h"
#include "include/scr-to-img.h"
#include "include/imagedata.h"
#include "homography.h"
#include "render-to-scr.h"
#include "spline.h"
#include "include/histogram.h"
namespace colorscreen
{
std::atomic_ulong scr_to_img::m_nwarnings;

const render_parameters::capture_type_property render_parameters::capture_properties[] = {
};
namespace
{

/* Matrix performing perspective transformation including tilts and shifting
   to a given DISTANCE.  */
class rotation_distance_matrix : public matrix4x4<coord_t>
{
public:
  rotation_distance_matrix (coord_t distance, coord_t tilt_x, coord_t tilt_y,
                            enum scanner_type type)
  {
    const coord_t rad_x = tilt_x * (coord_t)M_PI / 180;
    const coord_t rad_y = tilt_y * (coord_t)M_PI / 180;
    const coord_t sy = std::sin (rad_x); /*s*/
    const coord_t cy = std::cos (rad_x); /*c*/
    const coord_t sx = std::sin (rad_y); /*t*/
    const coord_t cx = std::cos (rad_y); /*d*/
    /* Rotation matrix is the following:
       cy sy*sx  cx*sy
       0  cx    -sx
      -sy cy*sx  cy*cx
       We want to rotate plane with center in 0 and stretch the rest.  */
    (*this)(0, 0) = cy;
    (*this)(0, 1) = sy * sx;
    (*this)(0, 2) = 0;
    (*this)(0, 3) = 0;
    (*this)(1, 0) = 0;
    (*this)(1, 1) = cx;
    (*this)(1, 2) = 0;
    (*this)(1, 3) = 0;
    (*this)(2, 0) = -sy;
    (*this)(2, 1) = cy * sx;
    (*this)(2, 2) = distance;
    (*this)(2, 3) = 0;
    (*this)(3, 0) = -sy;
    (*this)(3, 1) = cy * sx;
    (*this)(3, 2) = 0;
    (*this)(3, 3) = distance;
    /* Disable perspective corrections along the lens movement axis.  */
    if (type == lens_move_horizontally)
      (*this)(2, 0) = 0, (*this)(2, 1) = 0 /*, (*this)(2, 2)=1*/;
    if (type == lens_move_vertically)
      (*this)(3, 0) = 0, (*this)(3, 1) = 0 /*, (*this)(3, 3)=1*/;
  }
};

/* Translate center to given coordinates (x,y).  */
class translation_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize translation to CENTER.  */
  translation_3x3matrix (point_t center)
  {
    (*this)(0, 2) = center.x;
    (*this)(1, 2) = center.y;
  }
};

/* Change basis to a given coordinate vectors.  */
class change_of_basis_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize change of basis to C1 and C2.  */
  change_of_basis_3x3matrix (point_t c1, point_t c2)
  {
    (*this)(0, 0) = c1.x;
    (*this)(0, 1) = c2.x;
    (*this)(1, 0) = c1.y;
    (*this)(1, 1) = c2.y;
  }
};

/* Rotate by given angle.  */
class rotation_2x2matrix : public matrix2x2<coord_t>
{
public:
  /* Initialize rotation by ROTATION degrees.  */
  rotation_2x2matrix (coord_t rotation)
  {
    rotation *= (coord_t)M_PI / 180;
    const coord_t s = std::sin (rotation);
    const coord_t c = std::cos (rotation);
    (*this)(0, 0) = c; (*this)(0, 1) = -s;
    (*this)(1, 0) = s; (*this)(1, 1) = c;
  }
};

}

/* Update the mapping for new linear parameters FINAL_RATIO and FINAL_ANGLE.  */
void
scr_to_img::update_scr_to_final_parameters (coord_t final_ratio,
                                            coord_t final_angle)
{
  // m_scr_to_img_homography_matrix = homography_matrix_4points (false, zero,
  // x, y, xpy); m_img_to_scr_homography_matrix = homography_matrix_4points
  // (true, zero, x, y, xpy);
  //
  m_param.final_angle = final_angle;
  m_param.final_ratio = final_ratio;
  /* On only scan I have horizontal cycle is 10.1592036279 pixels (along blue line)
     and vertical cycle (slopy rad/green lines) are 8.15944528388.  */
  if (m_param.type == ImprovedDioptichromeB)
    {
      m_param.final_angle = 107.773559;
      m_param.final_ratio = 0.803158;
    }
  if (m_param.type == Omnicolore)
    {
      /* This is based on on ony one test scan.  */
      m_param.final_ratio =  13.738610 / 15.632711;
    }
  if (screen_with_vertical_strips_p (m_param.type))
    {
      m_param.final_angle = 90;
      m_param.final_ratio = 1.0/3;
    }

  double r = m_param.final_angle * M_PI / 180;
#if 0
  matrix2x2<coord_t> fm (1, 0,
			 cos (r) * m_param.final_ratio, sin (r) * m_param.final_ratio);
#endif
  matrix2x2<coord_t> fm (1, cos (r) * m_param.final_ratio,
			 0, sin (r) * m_param.final_ratio);

  /* By Dufaycolor manual grid is rotated by 23 degrees.  In reality it seems
     to be 23.77. We organize the grid making red lines horizontal, so rotate
     by additional 90 degrees to get image right.  */
  coord_t rotate = m_param.final_rotation;

  /* Depending on angle of screen detected we need to either rotate left or
     right. This makes flipped scans to come out right.  */
  if (!m_rotation_adjustment && m_param.type == Dufay)
    {
      const coord_t dufay_angle = 23 + 0.77;
      point_t z = to_img ({ (coord_t)0, (coord_t)0 });
      point_t d = to_img ({ (coord_t)1, (coord_t)0 }) - z;
      coord_t angle
          = -std::asin (d.x / my_sqrt (d.x * d.x + d.y * d.y)) * 180 / (coord_t)M_PI;
      // angle += 23 - 99 + 0.77;
      for (int b = 0; b < 360; b += 90)
        {
          // coord_t error = ((int)fabs (angle - m_rotation_adjustment) + 360)
          // % 360; printf ("try %f %f %f %f\n", b - dufay_angle, b +
          // dufay_angle, m_rotation_adjustment, error);
          if (!b
              || ((int)fabs (angle - b - dufay_angle) + 360) % 360
                     < ((int)fabs (angle - m_rotation_adjustment) + 360) % 360)
            m_rotation_adjustment = b + dufay_angle;
          if (((int)fabs (angle - b + dufay_angle) + 360) % 360
              < ((int)fabs (angle - m_rotation_adjustment) + 360) % 360)
            m_rotation_adjustment = b - dufay_angle;
        }
      // printf ("%f %f\n", m_rotation_adjustment, angle);
      m_rotation_adjustment += 90;
      // m_rotation_adjustment -= 90;
      // printf ("%f\n",angle);
      // m_rotation_adjustment = angle;
      // m_rotation_adjustment -= dufay_angle * 2;

#if 0
      coord_t m_rotation_adjustment = 23 - 90 + 0.77;
      coord_t zx, zy, xx, yy;
      to_img (0, 0, &zx, &zy);
      rotation_2x2matrix (m_rotation_adjustment).apply_to_vector (1, 0, &xx, &yy);
      to_img (xx, yy, &xx, &yy);
      xx -= zx;
      yy -= zy;
      if (fabs (xx) * 0.5 < fabs (yy) && fabs (yy) < fabs (xx) * 2)
	m_rotation_adjustment = -m_rotation_adjustment + 180;
#endif
    }
  rotate += m_rotation_adjustment;
  m_scr_to_final_matrix = rotation_2x2matrix (rotate) * fm;
  m_final_to_scr_matrix = m_scr_to_final_matrix.invert ();
}
/* Initialize matrices from mapping parameters.  */
void
scr_to_img::initialize ()
{
  /* Now set up the projection matrix that combines tilts and shifting to a
   * given distance.  */
  rotation_distance_matrix rd (m_param.projection_distance, m_param.tilt_x,
                               m_param.tilt_y, m_param.scanner_type);
  m_perspective_matrix = rd;
  /* Center and base vectors are in the scan coordinates.  */
  point_t corrected_center
      = apply_early_correction (m_param.center);
  point_t c1
      = apply_early_correction (m_param.center + m_param.coordinate1);
  point_t c2
      = apply_early_correction (m_param.center + m_param.coordinate2);

  corrected_center = m_perspective_matrix.inverse_perspective_transform (corrected_center);
  c1 = m_perspective_matrix.inverse_perspective_transform (c1);
  c2 = m_perspective_matrix.inverse_perspective_transform (c2);
  c1 -= corrected_center;
  c2 -= corrected_center;

  /* Change-of-basis matrix.  */
  trans_3d_matrix mm;
  change_of_basis_3x3matrix basis (c1, c2);
  translation_3x3matrix trans (corrected_center);
  mm = basis * mm;
  mm = trans * mm;

  m_matrix = mm;
  m_inverse_matrix = m_matrix.invert ();

  /* TODO: We support homography matrix only for true homography
     transformations (which maps lines to lines). If scanner has moving lens,
     straight lines are not preserved. However we probably still do 6 point
     based matrix rather than manually doing direct and inverse transforms.  */
  {
    m_do_homography = (m_param.scanner_type == fixed_lens
                       || (m_param.tilt_x == 0 && m_param.tilt_y == 0));
    point_t zero;
    point_t x;
    point_t y;
    point_t xpy;
    point_t txpy;
    coord_t tx, ty;
    m_matrix.apply (0, 0, &tx, &ty);
    zero = m_perspective_matrix.perspective_transform ({ tx, ty });
    m_matrix.apply (1000, 0, &tx, &ty);
    x = m_perspective_matrix.perspective_transform ({ tx, ty });
    m_matrix.apply (0, 1000, &tx, &ty);
    y = m_perspective_matrix.perspective_transform ({ tx, ty });
    m_matrix.apply (1000, 1000, &tx, &ty);
    xpy = m_perspective_matrix.perspective_transform ({ tx, ty });
    m_matrix.apply (2000, 3000, &tx, &ty);
    txpy = m_perspective_matrix.perspective_transform ({ tx, ty });
    m_scr_to_img_homography_matrix = homography::get_matrix_5points (
        false, m_param.scanner_type, zero, x, y, xpy, txpy);
    if (m_do_homography)
      m_img_to_scr_homography_matrix = homography::get_matrix_5points (
          true, m_param.scanner_type, zero, x, y, xpy, txpy);
#if 0
      printf ("Matrix\n");
      m_matrix.print (stdout);
      printf ("Perspective\n");
      m_perspective_matrix.print (stdout);
      printf ("Homography\n");
      m_scr_to_img_homography_matrix.print (stdout);
#endif
    if (debug)
      {
        bool found = false;
        for (int x = 0; x <= 1000 && !found; x += 10)
          for (int y = 0; y <= 1000 && !found; y += 10)
            {
              coord_t px, py;
              coord_t xt, yt;
              m_matrix.apply (x, y, &px, &py);
              point_t pt = m_perspective_matrix.perspective_transform ({ px, py });
              px = pt.x; py = pt.y;
              point_t xtyt = m_scr_to_img_homography_matrix.perspective_transform ({ (coord_t) x, (coord_t) y });
              xt = xtyt.x; yt = xtyt.y;
              if (fabs (px - xt) > 1 || fabs (py - yt) > 1)
                {
                  printf ("scr-to-img forward mismatch %i %i: %f %f should be "
                          "%f %f\n",
                          x, y, px, py, xt, yt);
                  found = true;
                }
              if (!m_do_homography)
                continue;
              coord_t bx, by;
              point_t bxby = m_img_to_scr_homography_matrix.perspective_transform ({ px, py });
              bx = bxby.x; by = bxby.y;
              if (fabs (x - bx) > 1 || fabs (y - by) > 1)
                {
                  printf ("scr-to-img backward mismatch %i %i: %f %f should "
                          "be %f %f\n",
                          x, y, px, py, bx, by);
                  found = true;
                }
            }
      }
  }

  update_scr_to_final_parameters (m_param.final_ratio, m_param.final_angle);
}

/* Set parameters of the early correction for image of dimensions WIDTH x HEIGHT.
   Return true on success.  */
bool
scr_to_img::set_parameters_for_early_correction (
    const scr_to_img_parameters &param,
    int width, int height)
{
  m_param = param;

  if (m_param.mesh_trans)
    {
      if (m_param.mesh_trans_is_scr_to_img)
        m_scr_to_img_mesh = m_param.mesh_trans;
      else
	m_img_to_scr_mesh = m_param.mesh_trans;
      return true;
    }
  else
    m_scr_to_img_mesh = nullptr;
  m_img_to_scr_mesh = nullptr;
  
  m_inverted_projection_distance = (coord_t)1 / m_param.projection_distance;
  m_nwarnings = 0;
  assert (!debug || (width > 0 && height > 0));

  /* Next initialize lens correction.
     Lens center is specified in scan coordinates, so apply previous
     corrections.  */
  point_t center = { m_param.lens_correction.center.x * width,
    		     m_param.lens_correction.center.y * height};
  point_t c1 = { (coord_t)0, (coord_t)0 };
  point_t c2 = { (coord_t)width, (coord_t)0 };
  point_t c3 = { (coord_t)0, (coord_t)height };
  point_t c4 = { (coord_t)width, (coord_t)height };
  m_lens_correction.set_parameters (m_param.lens_correction);
  if (m_param.scanner_type == lens_move_horizontally)
    c1.x = c2.x = c3.x = c4.x = center.x = m_param.lens_correction.center.x;
  if (m_param.scanner_type == lens_move_vertically)
    c1.y = c2.y = c3.y = c4.y = center.y = m_param.lens_correction.center.y;
  if (!m_lens_correction.precompute (center, c1, c2, c3, c4))
    return false;
  /* For non-noop conversion we need to also precompute inverse.  */
  m_early_correction_precomputed = m_lens_correction.is_noop ();
  return true;
}

/* Set all parameters for image of dimensions WIDTH x HEIGHT.
   ROTATION_ADJUSTMENT can be used to adjust the rotation.
   Return true on success.  */
bool
scr_to_img::set_parameters (const scr_to_img_parameters &param,
                            int width, int height, coord_t rotation_adjustment)
{
  m_rotation_adjustment = rotation_adjustment;
  if (!scr_to_img::set_parameters_for_early_correction (param, width, height))
    return false;

  if (m_scr_to_img_mesh)
    {
      int_image_area area = {0, 0, width, height};
      m_img_to_scr_mesh = m_scr_to_img_mesh->compute_inverse (area);
      if (!m_img_to_scr_mesh)
        return false;
      update_scr_to_final_parameters (m_param.final_ratio, m_param.final_angle);
      return true;
    }
  if (m_img_to_scr_mesh)
    {
      m_scr_to_img_mesh = m_img_to_scr_mesh->compute_inverse ();
      if (!m_scr_to_img_mesh)
        return false;
      update_scr_to_final_parameters (m_param.final_ratio, m_param.final_angle);
      return true;
    }

  if (!m_lens_correction.precompute_inverse ())
    return false;
  m_early_correction_precomputed = true;
  initialize ();
  return true;
}

/* Update map for new parameters.  It can only differ in those describing the
   linear transforms.  */
void
scr_to_img::update_linear_parameters (scr_to_img_parameters &param)
{
  m_param = param;
  initialize ();
}

/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is returned as an int_image_area where x/y is the minimum
   screen coordinate and width/height cover all screen coordinates mapped
   from the image.  */
int_image_area
scr_to_img::get_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2) const noexcept
{
  if (!m_scr_to_img_mesh && !m_img_to_scr_mesh)
    {
      /* Compute all the corners.  */
      int_image_area area (int_point_t {(int64_t)my_floor (to_scr ({ x1, y1 }).x), (int64_t)my_floor (to_scr ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_scr ({ x1, y1 }).x), (int64_t)my_ceil (to_scr ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_scr ({ x2, y1 }).x), (int64_t)my_floor (to_scr ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_scr ({ x2, y1 }).x), (int64_t)my_ceil (to_scr ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_scr ({ x1, y2 }).x), (int64_t)my_floor (to_scr ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_scr ({ x1, y2 }).x), (int64_t)my_ceil (to_scr ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_scr ({ x2, y2 }).x), (int64_t)my_floor (to_scr ({ x2, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_scr ({ x2, y2 }).x), (int64_t)my_ceil (to_scr ({ x2, y2 }).y)});

      /* If we correct lens distortion the corners may not be extremes.  */
      if (!m_lens_correction.is_noop () || m_param.tilt_x || m_param.tilt_y)
        {
          const int steps = 16 * 1024;
          for (int i = 1; i < steps; i++)
            {
              point_t p = to_scr ({ x1 + (x2 - x1) * i / steps, y1 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_scr ({ x1 + (x2 - x1) * i / steps, y2 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_scr ({ x1, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_scr ({ x2, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
            }
        }
      /* Adjust slightly to be safe.  */
      area.x -= 1;
      area.y -= 1;
      area.width += 2;
      area.height += 2;
      return area;
    }
  else
    {
      coord_t minx, miny, maxx, maxy;
      matrix2x2<coord_t> identity;
      m_scr_to_img_mesh->get_range (identity, x1, y1, x2, y2, &minx, &maxx,
                                     &miny, &maxy);
      return int_image_area ((int)minx + 1, (int)miny + 1, (int)(maxx - minx) + 2, (int)(maxy - miny) + 2);
    }
}
/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.  */
int_image_area
scr_to_img::get_range (int img_width, int img_height) const noexcept
{
  return get_range (0.0, 0.0, (coord_t)img_width, (coord_t)img_height);
}
/* Determine rectangular section of the final coordinates to which image section
   from (X1, Y1) to (X2, Y2) fits.  */
int_image_area
scr_to_img::get_final_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2) const noexcept
{
  if (!m_scr_to_img_mesh)
    {
      /* Compute all the corners.  */
      int_image_area area (int_point_t {(int64_t)my_floor (img_to_final ({ x1, y1 }).x), (int64_t)my_floor (img_to_final ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (img_to_final ({ x1, y1 }).x), (int64_t)my_ceil (img_to_final ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (img_to_final ({ x2, y1 }).x), (int64_t)my_floor (img_to_final ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (img_to_final ({ x2, y1 }).x), (int64_t)my_ceil (img_to_final ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (img_to_final ({ x1, y2 }).x), (int64_t)my_floor (img_to_final ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (img_to_final ({ x1, y2 }).x), (int64_t)my_ceil (img_to_final ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (img_to_final ({ x2, y2 }).x), (int64_t)my_floor (img_to_final ({ x2, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (img_to_final ({ x2, y2 }).x), (int64_t)my_ceil (img_to_final ({ x2, y2 }).y)});

      /* If we correct lens distortion the corners may not be extremes.  */
      if (!m_lens_correction.is_noop () || m_param.tilt_x || m_param.tilt_y
          || m_scr_to_img_mesh)
        {
          const int steps = 16 * 1024;
          for (int i = 1; i < steps; i++)
            {
              point_t p = img_to_final ({ x1 + (x2 - x1) * i / steps, y1 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = img_to_final ({ x1 + (x2 - x1) * i / steps, y2 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = img_to_final ({ x1, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = img_to_final ({ x2, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
            }
        }
      return area;
    }
  else
    {
      coord_t minx, miny, maxx, maxy;
      m_scr_to_img_mesh->get_range (m_scr_to_final_matrix, x1, y1, x2, y2,
                                     &minx, &maxx, &miny, &maxy);
      /* Determine the coordinates.  */
      const int min_x = (int)my_floor (minx);
      const int min_y = (int)my_floor (miny);
      int width = (int)my_ceil (maxx - (coord_t)min_x);
      if (width <= 0)
        width = 1;
      int height = (int)my_ceil (maxy - (coord_t)min_y);
      if (height <= 0)
        height = 1;
      return int_image_area (min_x, min_y, width, height);
    }
}

/* Determine rectangular section of the final coordinates to which the whole
   image with dimension img_width x img_height fits.  */
int_image_area
scr_to_img::get_final_range (int img_width, int img_height) const noexcept
{
  return get_final_range (0, 0, (coord_t)img_width, (coord_t)img_height);
}

/* Dump mapping state to file F.  */
void
scr_to_img::dump (FILE *f) const
{
  fprintf (f, "scr to img dump:\n");
  if (m_scr_to_img_mesh)
    fprintf (f, "have mesh trans\n");
  save_csp (f, const_cast<scr_to_img_parameters *> (&m_param), NULL, NULL, NULL);
}

/* Estimate DPI for given pixel size.  */
coord_t
scr_to_img_parameters::estimate_dpi (coord_t pixel_size) const
{
    switch (type)
    {
    case Paget:
    case Finlay:
	/* Screen size scanned by Epson scanner at 1800 DPI.  */
	/*return 1800 * ((1/sqrt (13.357325*13.357325 + 0.203263 * 0.203263)) / pixel_size);*/
	/* Screen size scanned by Epson scanner at 2400 DPI.  */
	return 2400 * ((1/sqrt (19.776270*19.776270 + -0.119933*-0.119933)) / pixel_size);
    case Dufay:
	/* Screen size scanned by Nikon scanner at 4000 DPI.  */
	return 4000 * ((1/sqrt (3.045845*3.045845 + 7.194771*7.194771)) / pixel_size);
    default:
	return -1;
	break;
    }
}
/* Estimate pixel size for area.  */
pure_attr coord_t
scr_to_img::pixel_size (int_image_area area) const noexcept
{
  histogram hist;
  int steps = 11;
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 5; x++)
      {
        coord_t bx = area.x + (x+(coord_t)0.5) * area.width * (1 / (coord_t)steps);
        coord_t by = area.y + (y+(coord_t)0.5) * area.height * (1 / (coord_t)steps);
        coord_t sz = to_scr ({ bx + 0, by + 0 }).dist_from (to_scr ({ bx + 1, by + 0 }));
	hist.pre_account (sz);
      }
  hist.finalize_range (256);
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 5; x++)
      {
        coord_t bx = area.x + (x+(coord_t)0.5) * area.width * (1 / (coord_t)steps);
        coord_t by = area.y + (y+(coord_t)0.5) * area.height * (1 / (coord_t)steps);
        coord_t sz = to_scr ({ bx + 0, by + 0 }).dist_from (to_scr ({ bx + 1, by + 0 }));
	hist.account (sz);
      }
  hist.finalize ();
  return hist.find_avg (0.2, 0.2);
}


int_image_area
scr_to_img::get_img_range (int_image_area a) const noexcept
{
  coord_t x1 = a.x;
  coord_t y1 = a.y;
  coord_t x2 = a.x + a.width - 1;
  coord_t y2 = a.y + a.height - 1;
#if 0
  /* Determine region in image that is covered by screen.  */
  point_t corners[4]
      = { to_img (point_t{ (coord_t) m_area.top_left ().x,
			  (coord_t) m_area.top_left ().y }),
	  to_img (point_t{ (coord_t) m_area.top_right ().x,
			  (coord_t) m_area.top_right ().y }),
	  to_img (point_t{ (coord_t) m_area.bottom_left ().x,
			  (coord_t) m_area.bottom_left ().y }),
	  to_img (point_t{
	      (coord_t) m_area.bottom_right ().x,
	      (coord_t) m_area.bottom_right ().y }) };
  int_image_area img_area (int_point_t{ (int64_t) my_floor (corners[0].x),
					(int64_t) my_floor (corners[0].y) });
  for (int i = 0; i < 4; i++)
    {
      img_area.extend (int_point_t{ (int64_t) my_floor (corners[i].x),
				    (int64_t) my_floor (corners[i].y) });
      img_area.extend (int_point_t{ (int64_t) my_ceil (corners[i].x),
				    (int64_t) my_ceil (corners[i].y) });
    }
  return img_area;
#endif
  if (!m_scr_to_img_mesh && !m_img_to_scr_mesh)
    {
      /* Compute all the corners.  */
      int_image_area area (int_point_t {(int64_t)my_floor (to_img ({ x1, y1 }).x), (int64_t)my_floor (to_scr ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_img ({ x1, y1 }).x), (int64_t)my_ceil (to_scr ({ x1, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_img ({ x2, y1 }).x), (int64_t)my_floor (to_scr ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_img ({ x2, y1 }).x), (int64_t)my_ceil (to_scr ({ x2, y1 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_img ({ x1, y2 }).x), (int64_t)my_floor (to_scr ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_img ({ x1, y2 }).x), (int64_t)my_ceil (to_scr ({ x1, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_floor (to_img ({ x2, y2 }).x), (int64_t)my_floor (to_scr ({ x2, y2 }).y)});
      area.extend (int_point_t {(int64_t)my_ceil (to_img ({ x2, y2 }).x), (int64_t)my_ceil (to_scr ({ x2, y2 }).y)});

      /* If we correct lens distortion the corners may not be extremes.  */
      if (!m_lens_correction.is_noop () || m_param.tilt_x || m_param.tilt_y)
        {
          const int steps = 16 * 1024;
          for (int i = 1; i < steps; i++)
            {
              point_t p = to_img ({ x1 + (x2 - x1) * i / steps, y1 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_img ({ x1 + (x2 - x1) * i / steps, y2 });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_img ({ x1, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
              p = to_img ({ x2, y1 + (y2 - y1) * i / steps });
              area.extend (int_point_t {(int64_t)my_floor (p.x), (int64_t)my_floor (p.y)});
              area.extend (int_point_t {(int64_t)my_ceil (p.x), (int64_t)my_ceil (p.y)});
            }
        }
      /* Adjust slightly to be safe.  */
      area.x -= 1;
      area.y -= 1;
      area.width += 2;
      area.height += 2;
      return area;
    }
  else
    {
      coord_t minx, miny, maxx, maxy;
      matrix2x2<coord_t> identity;
      m_img_to_scr_mesh->get_range (identity, x1, y1, x2, y2, &minx, &maxx,
                                     &miny, &maxy);
      return int_image_area ((int)minx + 1, (int)miny + 1, (int)(maxx - minx) + 2, (int)(maxy - miny) + 2);
    }
}

}
