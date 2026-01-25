#ifndef SCR_TO_IMG_PARAMETERS_H
#define SCR_TO_IMG_PARAMETERS_H
#include <cmath>
#include <memory>
#include "base.h"
#include "dllpublic.h"
#include "lens-warp-correction-parameters.h"
namespace colorscreen
{
class mesh;

/* Windows does not seem to define this by default.  */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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
  DioptichromeB,
  ImprovedDioptichromeB,
  WarnerPowrie,
  Joly,
  Omnicolore,
  max_scr_type
};

inline bool
paget_like_screen_p (enum scr_type t)
{
  return t == Paget || t == Thames || t == Finlay;
}

inline bool
dufay_like_screen_p (enum scr_type t)
{
  return t == Dufay || t == DioptichromeB || t == ImprovedDioptichromeB || t == Omnicolore;
}

/* Return true if screen is integrated to the emulsoin and can not move.  */
inline bool
integrated_screen_p (enum scr_type t)
{
  return t == Dufay;
}

inline bool
screen_with_vertical_strips_p (enum scr_type t)
{
  return t == WarnerPowrie || t == Joly;
}

inline bool
screen_with_varying_strips_p (enum scr_type t)
{
  return dufay_like_screen_p (t) || screen_with_vertical_strips_p (t);
}

struct scr_type_property_t {
  const char *name;
  const char *pretty_name;
  const char *help;
  coord_t frequency;
};
DLL_PUBLIC extern const scr_type_property_t scr_names[max_scr_type];
pure_attr DLL_PUBLIC rgbdata patch_proportions (enum scr_type t,
                                                const render_parameters *);

/* Type of a scanner used.  */
enum scanner_type
{
  fixed_lens,
  fixed_lens_sensor_move_horisontally,
  fixed_lens_sensor_move_vertically,
  lens_move_horisontally,
  lens_move_vertically,
  max_scanner_type
};

inline bool
is_fixed_lens (scanner_type type)
{
  return type == fixed_lens || type == fixed_lens_sensor_move_horisontally
         || type == fixed_lens_sensor_move_vertically;
}

DLL_PUBLIC extern const property_t scanner_type_names[max_scanner_type];

/* This implements to translate image coordiantes to coordinates of the viewing
   screen. In the viewing screen the coordinats (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates
   describes gren dots again.

   In order to turn scan coordinates to screen the following transformations
   are performed
     1) motor correction
     2) translation to move lens_center to (0,0)
     3) lens correction
     4) perspective correction (with tilt applied)
     5) translation to move center to (0,0)
     6) change of basis so center+coordinate1 becomes (1.0) and
   center+coordinate2 becomes (0,1)
*/

struct scr_to_img_parameters
{
  /* Coordinates (in the image) of the center of the screen (a green dot).  */
  point_t center;
  /* First coordinate vector:
     image's (center.x+coordinate1.x, centr.y+coordinate1.y) should describe
     a green dot just on the right side of (center_x, center_y).  */
  point_t coordinate1;
  /* Second coordinate vector:
     image's (center.x+coordinate1.x, centr_y+coordinate1.y) should describe
     a green dot just below (center.x, center.y).  */
  point_t coordinate2;

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

  std::shared_ptr<mesh> mesh_trans;

  enum scr_type type;
  enum scanner_type scanner_type;

  lens_warp_correction_parameters lens_correction;

  scr_to_img_parameters ()
      : center{ (coord_t)0, (coord_t)0 },
        coordinate1{ (coord_t)1, (coord_t)0 },
        coordinate2{ (coord_t)0, (coord_t)1 }, projection_distance (1),
        tilt_x (0), tilt_y (0), final_rotation (0), final_angle (90),
        final_ratio (1), motor_correction_x (NULL), motor_correction_y (NULL),
        n_motor_corrections (0), mesh_trans (NULL), type (Random),
        scanner_type (fixed_lens), lens_correction ()
  {
  }
  scr_to_img_parameters (const scr_to_img_parameters &from)
      : center (from.center), coordinate1 (from.coordinate1), coordinate2 (from.coordinate2), 
        projection_distance (from.projection_distance), tilt_x (from.tilt_x),
        tilt_y (from.tilt_y), final_rotation (from.final_rotation),
        final_angle (from.final_angle), final_ratio (from.final_ratio),
        motor_correction_x (NULL), motor_correction_y (NULL),
        n_motor_corrections (from.n_motor_corrections),
        mesh_trans (from.mesh_trans), type (from.type),
        scanner_type (from.scanner_type),
        lens_correction (from.lens_correction)
  {
    if (n_motor_corrections)
      {
        motor_correction_x
            = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
        motor_correction_y
            = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
        memcpy (motor_correction_x, from.motor_correction_x,
                n_motor_corrections * sizeof (coord_t));
        memcpy (motor_correction_y, from.motor_correction_y,
                n_motor_corrections * sizeof (coord_t));
      }
  }
  scr_to_img_parameters &
  operator= (const scr_to_img_parameters &other)
  {
    free (motor_correction_x);
    free (motor_correction_y);
    n_motor_corrections = 0;
    copy_from_cheap (other);
    n_motor_corrections = other.n_motor_corrections;
    if (n_motor_corrections)
      {
	motor_correction_x
	    = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
	motor_correction_y
	    = (coord_t *)malloc (n_motor_corrections * sizeof (coord_t));
	memcpy (motor_correction_x, other.motor_correction_x,
		n_motor_corrections * sizeof (coord_t));
	memcpy (motor_correction_y, other.motor_correction_y,
		n_motor_corrections * sizeof (coord_t));
      }
    return *this;
  }
  /* Copy everything except for motor corrections.  */
  void
  copy_from_cheap (const scr_to_img_parameters &from)
  {
    center = from.center;
    coordinate1 = from.coordinate1;
    coordinate2 = from.coordinate2;
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
  bool
  operator== (const scr_to_img_parameters &other) const
  {
    if (n_motor_corrections != other.n_motor_corrections)
      return false;
    for (int i = 0; i < n_motor_corrections; i++)
      if (motor_correction_x[i] != other.motor_correction_x[i]
          || motor_correction_y[i] != other.motor_correction_y[i])
        return false;
    return center == other.center 
           && coordinate1 == other.coordinate1
           && coordinate2 == other.coordinate2
           && projection_distance == other.projection_distance
           && final_rotation == other.final_rotation
           && final_angle == other.final_angle
           && final_ratio == other.final_ratio && tilt_x == other.tilt_x
           && tilt_y == other.tilt_y && type == other.type
           && scanner_type == other.scanner_type
           && lens_correction == other.lens_correction
	   && mesh_trans == other.mesh_trans;
  }
  void
  merge_solver_solution (const scr_to_img_parameters &other)
  {
    center = other.center;
    coordinate1 = other.coordinate1;
    coordinate2 = other.coordinate2;
    tilt_x = other.tilt_x;
    tilt_y = other.tilt_y;
    lens_correction = other.lens_correction;
    projection_distance = other.projection_distance;
    mesh_trans = other.mesh_trans;
  }
  bool
  operator!= (const scr_to_img_parameters &other) const
  {
    return !(*this == other);
  }
  int
  add_motor_correction_point (coord_t x, coord_t y)
  {
    int p = 0;

    motor_correction_x
        = (coord_t *)realloc ((void *)motor_correction_x,
                              (n_motor_corrections + 1) * sizeof (coord_t));
    motor_correction_y
        = (coord_t *)realloc ((void *)motor_correction_y,
                              (n_motor_corrections + 1) * sizeof (coord_t));
    for (p = n_motor_corrections; p > 0 && motor_correction_x[p - 1] > x; p--)
      ;
    for (int p2 = n_motor_corrections; p2 > p; p2--)
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
  screen_per_inch () const
  {
    if (type == Dufay)
      /* Dufaycolor manual promises 500 lines per inch.  Screen has 2 lines  */
      return 250;
    else
      /* 2 squares per screen.  */
      return 25.4 / 2;
  }
  pure_attr coord_t
  get_xlen () const
  {
    return coordinate1.length ();
  }
  pure_attr coord_t
  get_ylen () const
  {
    return coordinate2.length ();
  }
  pure_attr coord_t
  get_angle () const
  {
    coord_t dot
        = coordinate1.x * coordinate2.x + coordinate1.y * coordinate2.y;
    // coord_t det = coordinate1_x*coordinate2_y - coordinate1_y*coordinate2_x;
    // return atan2(det, dot) * 180 / M_PI;
    return acos (dot / (get_xlen () * get_ylen ())) * (180 / M_PI);
  }
  DLL_PUBLIC coord_t
  estimate_dpi (coord_t pixel_size) const;
};
}
#endif
