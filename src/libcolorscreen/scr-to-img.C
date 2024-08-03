#include <math.h>
#include <algorithm>
#include "include/colorscreen.h"
#include "include/scr-to-img.h"
#include "include/imagedata.h"
#include "solver.h"
#include "render-to-scr.h"
#include "spline.h"
namespace colorscreen
{
std::atomic_ulong scr_to_img::m_nwarnings;
namespace
{

class rotation_distance_matrix : public matrix4x4<coord_t>
{
public:
  rotation_distance_matrix (double distance, double tiltx, double tilty,
                            enum scanner_type type)
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
    m_elements[0][0] = cy;
    m_elements[1][0] = sy * sx;
    m_elements[2][0] = 0;
    m_elements[3][0] = 0;
    m_elements[0][1] = 0;
    m_elements[1][1] = cx;
    m_elements[2][1] = 0;
    m_elements[3][1] = 0;
    m_elements[0][2] = -sy;
    m_elements[1][2] = cy * sx;
    m_elements[2][2] = distance;
    m_elements[3][2] = 0;
    m_elements[0][3] = -sy;
    m_elements[1][3] = cy * sx;
    m_elements[2][3] = 0;
    m_elements[3][3] = distance;
    /* Disable perspective corrections along the lens movement axis.  */
    if (type == lens_move_horisontally)
      m_elements[0][2] = 0, m_elements[1][2] = 0 /*, m_elements[2][2]=1*/;
    if (type == lens_move_vertically)
      m_elements[0][3] = 0, m_elements[1][3] = 0 /*, m_elements[3][3]=1*/;
  }
};

/* Translate center to given coordinates (x,y).  */
class translation_3x3matrix : public matrix3x3<coord_t>
{
public:
  translation_3x3matrix (point_t center)
  {
    m_elements[2][0] = center.x;
    m_elements[2][1] = center.y;
  }
};

/* Change basis to a given coordinate vectors.  */
class change_of_basis_3x3matrix : public matrix3x3<coord_t>
{
public:
  change_of_basis_3x3matrix (point_t c1, point_t c2)
  {
    m_elements[0][0] = c1.x;
    m_elements[1][0] = c2.x;
    m_elements[0][1] = c1.y;
    m_elements[1][1] = c2.y;
  }
};

class rotation_2x2matrix : public matrix2x2<coord_t>
{
public:
  rotation_2x2matrix (double rotation)
  {
    rotation *= M_PI / 180;
    double s = sin (rotation);
    double c = cos (rotation);
    m_elements[0][0] = c;
    m_elements[1][0] = -s;
    m_elements[0][1] = s;
    m_elements[1][1] = c;
  }
};

}

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

  double r = m_param.final_angle * M_PI / 180;
  matrix2x2<coord_t> fm (1, 0, cos (r) * m_param.final_ratio,
                         sin (r) * m_param.final_ratio);

  /* By Dufacyolor manual grid is rotated by 23 degrees.  In reality it seems
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
          = -asin (d.x / my_sqrt (d.x * d.x + d.y * d.y)) * 180 / M_PI;
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

  m_perspective_matrix.inverse_perspective_transform (
      corrected_center.x, corrected_center.y, corrected_center.x,
      corrected_center.y);
  m_perspective_matrix.inverse_perspective_transform (c1.x, c1.y, c1.x, c1.y);
  m_perspective_matrix.inverse_perspective_transform (c2.x, c2.y, c2.x, c2.y);
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
    m_perspective_matrix.perspective_transform (tx, ty, zero.x, zero.y);
    m_matrix.apply (1000, 0, &tx, &ty);
    m_perspective_matrix.perspective_transform (tx, ty, x.x, x.y);
    m_matrix.apply (0, 1000, &tx, &ty);
    m_perspective_matrix.perspective_transform (tx, ty, y.x, y.y);
    m_matrix.apply (1000, 1000, &tx, &ty);
    m_perspective_matrix.perspective_transform (tx, ty, xpy.x, xpy.y);
    m_matrix.apply (2000, 3000, &tx, &ty);
    m_perspective_matrix.perspective_transform (tx, ty, txpy.x, txpy.y);
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
              m_perspective_matrix.perspective_transform (px, py, px, py);
              m_scr_to_img_homography_matrix.perspective_transform (x, y, xt,
                                                                    yt);
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
              m_img_to_scr_homography_matrix.perspective_transform (px, py, bx,
                                                                    by);
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

/* Initilalize the translation matrix to PARAM.  */
void
scr_to_img::set_parameters (const scr_to_img_parameters &param,
                            const image_data &img, coord_t rotation_adjustment,
                            bool need_inverse)
{
  /* We do not need to copy motor corrections since we already constructed the
   * function.  */
  m_param.copy_from_cheap (param);
  m_inverted_projection_distance = 1 / param.projection_distance;
  m_nwarnings = 0;
  m_rotation_adjustment = rotation_adjustment;

  /* Initialize motor correction.  */
  m_motor_correction = NULL;
  if (param.n_motor_corrections && !is_fixed_lens (param.scanner_type))
    {
      int len = param.scanner_type == lens_move_horisontally
                        || param.scanner_type
                               == fixed_lens_sensor_move_horisontally
                    ? img.width
                    : img.height;
      if (param.n_motor_corrections > 2 && 0)
        {
          spline<coord_t> spline (param.motor_correction_x,
                                  param.motor_correction_y,
                                  param.n_motor_corrections);
          m_motor_correction = spline.precompute (0, len, len);
        }
      else
        m_motor_correction = new precomputed_function<coord_t> (
            0, len, len, param.motor_correction_x, param.motor_correction_y,
            param.n_motor_corrections);
    }

  /* Next initialize lens correction.
     Lens center is specified in scan coordinates, so apply previous
     corrections.  */
  point_t center = apply_motor_correction (
      { param.lens_correction.center.x * img.width,
        param.lens_correction.center.y * img.height });
  point_t c1 = apply_motor_correction ({ (coord_t)0, (coord_t)0 });
  point_t c2 = apply_motor_correction ({ (coord_t)img.width, (coord_t)0 });
  point_t c3 = apply_motor_correction ({ (coord_t)0, (coord_t)img.height });
  point_t c4
      = apply_motor_correction ({ (coord_t)img.width, (coord_t)img.height });
  m_lens_correction.set_parameters (param.lens_correction);
  if (m_param.scanner_type == lens_move_horisontally)
    c1.x = c2.x = c3.x = c4.x = center.x = param.lens_correction.center.x;
  if (m_param.scanner_type == lens_move_vertically)
    c1.y = c2.y = c3.y = c4.y = center.y = param.lens_correction.center.y;
  m_lens_correction.precompute (center, c1, c2, c3, c4, need_inverse);
  initialize ();
}

/* Update map for new parameters.  It can only differ in those describing the
 * linear transforms.  */
void
scr_to_img::update_linear_parameters (scr_to_img_parameters &param)
{
  m_param = param;
  initialize ();
}

/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2,
                       int *scr_xshift, int *scr_yshift, int *scr_width,
                       int *scr_height)
{
  coord_t minx, miny, maxx, maxy;
  if (!m_param.mesh_trans)
    {
      /* Compute all the corners.  */
      coord_t xul, xur, xdl, xdr;
      coord_t yul, yur, ydl, ydr;

      point_t ul = to_scr ({ x1, y1 });
      point_t ur = to_scr ({ x2, y1 });
      point_t dl = to_scr ({ x1, y2 });
      point_t dr = to_scr ({ x2, y2 });

      /* Find extremas.  */
      minx = std::min (std::min (std::min (ul.x, ur.x), dl.x), dr.x);
      miny = std::min (std::min (std::min (ul.y, ur.y), dl.y), dr.y);
      maxx = std::max (std::max (std::max (ul.x, ur.x), dl.x), dr.x);
      maxy = std::max (std::max (std::max (ul.y, ur.y), dl.y), dr.y);

      /* Hack warning: if we correct lens distortion the corners may not be
       * extremes.  */
      if (!m_lens_correction.is_noop () || m_param.tilt_x || m_param.tilt_y)
        {
          const int steps = 16 * 1024;
          for (int i = 1; i < steps; i++)
            {
              point_t p = to_scr ({ x1 + (x2 - x1) * i / steps, y1 });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = to_scr ({ x1 + (x2 - x1) * i / steps, y2 });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = to_scr ({ x1, y1 + (y2 - y1) * i / steps });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = to_scr ({ x2, y1 + (y2 - y1) * i / steps });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
            }
        }
    }
  else
    {
      matrix2x2<coord_t> identity;
      m_param.mesh_trans->get_range (identity, x1, y1, x2, y2, &minx, &maxx,
                                     &miny, &maxy);
    }

  /* Determine the coordinates.  */
  *scr_xshift = -minx - 1;
  *scr_yshift = -miny - 1;
  *scr_width = maxx - minx + 2;
  *scr_height = maxy - miny + 2;
}
/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (int img_width, int img_height, int *scr_xshift,
                       int *scr_yshift, int *scr_width, int *scr_height)
{
  get_range (0.0, 0.0, (coord_t)img_width, (coord_t)img_height, scr_xshift,
             scr_yshift, scr_width, scr_height);
}
/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_final_range (coord_t x1, coord_t y1, coord_t x2, coord_t y2,
                             int *final_xshift, int *final_yshift,
                             int *final_width, int *final_height)
{
  coord_t minx, miny, maxx, maxy;
  /* Do not use mesh.get_range since it is way too conservative.  */
  if (!m_param.mesh_trans)
    {
      /* Compute all the corners.  */
      coord_t xul, xur, xdl, xdr;
      coord_t yul, yur, ydl, ydr;

      point_t ul = img_to_final ({ x1, y1 });
      point_t ur = img_to_final ({ x2, y1 });
      point_t dl = img_to_final ({ x1, y2 });
      point_t dr = img_to_final ({ x2, y2 });

      /* Find extremas.  */
      minx = std::min (std::min (std::min (ul.x, ur.x), dl.x), dr.x);
      miny = std::min (std::min (std::min (ul.y, ur.y), dl.y), dr.y);
      maxx = std::max (std::max (std::max (ul.x, ur.x), dl.x), dr.x);
      maxy = std::max (std::max (std::max (ul.y, ur.y), dl.y), dr.y);

      /* If we correct lens distortion the corners may not be extremes.  */
      if (!m_lens_correction.is_noop () || m_param.tilt_x || m_param.tilt_y
          || m_param.mesh_trans)
        {
          const int steps = 16 * 1024;
          for (int i = 1; i < steps; i++)
            {
              point_t p = img_to_final ({ x1 + (x2 - x1) * i / steps, y1 });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = img_to_final ({ x1 + (x2 - x1) * i / steps, y2 });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = img_to_final ({ x1, y1 + (y2 - y1) * i / steps });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
              p = img_to_final ({ x2, y1 + (y2 - y1) * i / steps });
              minx = std::min (minx, p.x);
              miny = std::min (miny, p.y);
              maxx = std::max (maxx, p.x);
              maxy = std::max (maxy, p.y);
            }
        }
    }
  else
    m_param.mesh_trans->get_range (m_scr_to_final_matrix, x1, y1, x2, y2,
                                   &minx, &maxx, &miny, &maxy);

  /* Determine the coordinates.  */
  *final_xshift = -floor (minx);
  *final_yshift = -floor (miny);
  *final_width = ceil (maxx - floor (minx));
  if (*final_width <= 0)
    *final_width = 1;
  *final_height = ceil (maxy - floor (miny));
  if (*final_height <= 0)
    *final_height = 1;
}

/* Determine rectangular section of the final coordiantes to which the whole
   image with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_final_range (int img_width, int img_height, int *final_xshift,
                             int *final_yshift, int *final_width,
                             int *final_height)
{
  get_final_range (0, 0, (coord_t)img_width, (coord_t)img_height, final_xshift,
                   final_yshift, final_width, final_height);
}

void
scr_to_img::dump (FILE *f)
{
  fprintf (f, "scr to img dump:\n");
  if (m_param.mesh_trans)
    fprintf (f, "have mesh trans\n");
  save_csp (f, &m_param, NULL, NULL, NULL);
}
}
