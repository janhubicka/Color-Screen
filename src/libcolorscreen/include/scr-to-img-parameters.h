/* Configuration parameters for screen-to-image mapping.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of ColorScreen.  */

#ifndef SCR_TO_IMG_PARAMETERS_H
#define SCR_TO_IMG_PARAMETERS_H
#include "base.h"
#include "dllpublic.h"
#include "lens-warp-correction-parameters.h"
#include <cmath>
#include <memory>
namespace colorscreen
{
class mesh;
struct solver_parameters;

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

/* Return true if screen T is Paget-like.  */
inline bool
paget_like_screen_p (enum scr_type t)
{
  return t == Paget || t == Thames || t == Finlay;
}

/* Return true if screen T is Dufay-like.  */
inline bool
dufay_like_screen_p (enum scr_type t)
{
  return t == Dufay || t == DioptichromeB || t == ImprovedDioptichromeB
         || t == Omnicolore;
}

/* Return true if screen T is integrated to the emulsion and can not move.  */
inline bool
integrated_screen_p (enum scr_type t)
{
  return t == Dufay;
}

/* Return true if screen T has vertical strips.  */
inline bool
screen_with_vertical_strips_p (enum scr_type t)
{
  return t == WarnerPowrie || t == Joly;
}

/* Return true if screen T has varying strips.  */
inline bool
screen_with_varying_strips_p (enum scr_type t)
{
  return dufay_like_screen_p (t) || screen_with_vertical_strips_p (t);
}

struct scr_type_property_t
{
  const char *name;
  const char *pretty_name;
  const char *help;
  coord_t frequency;
};

DLL_PUBLIC extern const scr_type_property_t scr_names[max_scr_type];

/* Return patch proportions for screen T and RPARAM.  */
pure_attr DLL_PUBLIC rgbdata patch_proportions (enum scr_type t,
                                                const render_parameters *rparam);

/* Type of a scanner used.  */
enum scanner_type
{
  fixed_lens,
  fixed_lens_sensor_move_horizontally,
  fixed_lens_sensor_move_vertically,
  horizontally_moving_lens,
  vertically_moving_lens,
  max_scanner_type,

  /* Old misspelled aliases for backward compatibility.  */
  fixed_lens_sensor_move_horisontally = fixed_lens_sensor_move_horizontally,
  horisontally_moving_lens = horizontally_moving_lens,
  lens_move_horizontally = horizontally_moving_lens,
  lens_move_horisontally = horizontally_moving_lens,
  lens_move_vertically = vertically_moving_lens
};

/* Return true if TYPE is a fixed lens scanner.  */
inline bool
is_fixed_lens (scanner_type type)
{
  return type == fixed_lens || type == fixed_lens_sensor_move_horizontally
         || type == fixed_lens_sensor_move_vertically;
}

DLL_PUBLIC extern const property_t scanner_type_names[max_scanner_type];

/* This implements to translate image coordinates to coordinates of the viewing
   screen. In the viewing screen the coordinates (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates
   describes green dots again.

   In order to turn scan coordinates to screen the following transformations
   are performed
     1) translation to move lens_center to (0,0)
     2) lens correction
     3) perspective correction (with tilt applied)
     4) translation to move center to (0,0)
     5) change of basis so center+coordinate1 becomes (1,0) and
   center+coordinate2 becomes (0,1)
*/

struct scr_to_img_parameters
{
  /* Coordinates (in the image) of the center of the screen (a green dot).  */
  point_t center = { (coord_t)0, (coord_t)0 };
  /* First coordinate vector:
     image's (center.x+coordinate1.x, center.y+coordinate1.y) should describe
     a green dot just on the right side of (center_x, center_y).  */
  point_t coordinate1 = { (coord_t)1, (coord_t)0 };
  /* Second coordinate vector:
     image's (center.x+coordinate2.x, center.y+coordinate2.y) should describe
     a green dot just below (center.x, center.y).  */
  point_t coordinate2 = { (coord_t)0, (coord_t)1 };

  /* Distance of the perspective plane from the camera coordinate.  */
  coord_t projection_distance = 1;
  /* Perspective tilt in x and y coordinate in degrees.  */
  coord_t tilt_x = 0, tilt_y = 0;

  /* Rotation from screen coordinates to final coordinates.  */
  coord_t final_rotation = 0;
  /* Angle of the screen X and Y axis in the final coordinates.  */
  coord_t final_angle = 90;
  /* Ratio of the X and Y axis in the final coordinates.  */
  coord_t final_ratio = 1;

  std::shared_ptr<mesh> mesh_trans = nullptr;
  bool mesh_trans_is_scr_to_img = false;

  enum scr_type type = Random;
  enum scanner_type scanner_type = fixed_lens;

  lens_warp_correction_parameters lens_correction;

  bool
  operator== (const scr_to_img_parameters &other) const
  {
    return center == other.center && coordinate1 == other.coordinate1
           && coordinate2 == other.coordinate2
           && projection_distance == other.projection_distance
           && final_rotation == other.final_rotation
           && final_angle == other.final_angle
           && final_ratio == other.final_ratio && tilt_x == other.tilt_x
           && tilt_y == other.tilt_y && type == other.type
           && scanner_type == other.scanner_type
           && lens_correction == other.lens_correction
           && mesh_trans == other.mesh_trans
 	   && mesh_trans_is_scr_to_img == other.mesh_trans_is_scr_to_img;
  }
  /* Merge solution from solver into the parameters.
     OTHER is the solution from the solver.  */
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
    mesh_trans_is_scr_to_img = other.mesh_trans_is_scr_to_img;
  }

  /* Return true if OTHER is not equal to this.  */
  bool
  operator!= (const scr_to_img_parameters &other) const
  {
    return !(*this == other);
  }

  /* Return estimated screen per inch for the current screen type.  */
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

  /* Return the length of the first coordinate vector.  */
  pure_attr coord_t
  get_x_len () const
  {
    return coordinate1.length ();
  }

  /* Return the length of the second coordinate vector.  */
  pure_attr coord_t
  get_y_len () const
  {
    return coordinate2.length ();
  }

  /* Return the angle between the two coordinate vectors in degrees.  */
  pure_attr coord_t
  get_angle () const
  {
    coord_t dot
        = coordinate1.x * coordinate2.x + coordinate1.y * coordinate2.y;
    // coord_t det = coordinate1.x*coordinate2.y - coordinate1.y*coordinate2.x;
    // return atan2(det, dot) * 180 / M_PI;
    return std::acos (dot / (get_x_len () * get_y_len ())) * (180 / M_PI);
  }

  /* Estimate DPI for a given PIXEL_SIZE.  */
  DLL_PUBLIC coord_t estimate_dpi (coord_t pixel_size) const;
  /* Apply given matrix to the transformation.  */
  void transform_solution (matrix3x3<coord_t> trans);

  /* Shift solution to exchange colors.
     Also update solver parameters.  */
  DLL_PUBLIC void alternate_colors (solver_parameters &params);
};
} // namespace colorscreen
#endif
