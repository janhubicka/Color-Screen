#include <netpbm/pgm.h>
#include "matrix.h"
/* This implements to translate image coordiantes to coordinates of the viewing screen.
   In the viewing screen the coordinats (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates describes
   gren dots again.  */

struct scr_to_img_parameters
{
  /* Coordinates (in the image) of the center of the screen (a green dot).  */
  double center_x, center_y;
  /* First base vector:
     image coordinates (center_x+base1_x, centr_y+base1_y) should describe
     a green dot just on the right side of (center_x, center_y).  */
  double base1_x, base1_y;
  /* Second bsae vector:
     image coordinates (center_x+base1_x, centr_y+base1_y) should describe
     a green dot just below (center_x, center_y).  */
  double base2_x, base2_y;
};

/* Mapping between screen and image.  */
class scr_to_img
{
  void set_parameters (scr_to_img_parameters param);
  /* Map screen coordinates to image coordinates.  */
  void
  to_img (double x, double y, double *xp, double *yp)
  {
    m_matrix.perspective_transform (x,y, *xp, *yp);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (double x, double y, double *xp, double *yp)
  {
    m_matrix.inverse_perspective_transform (x,y, *xp, *yp);
  }
private:
  /* Screen->image translation matrix.  */
  matrix4x4 m_matrix;
  scr_to_img_parameters m_param;
};
