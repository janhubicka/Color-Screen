#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
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
  /* First coordinate vector:
     image's (center_x+coordinate1_x, centr_y+coordinate1_y) should describe
     a green dot just on the right side of (center_x, center_y).  */
  double coordinate1_x, coordinate1_y;
  /* Second coordinate vector:
     image's (center_x+coordinate1_x, centr_y+coordinate1_y) should describe
     a green dot just below (center_x, center_y).  */
  double coordinate2_x, coordinate2_y;
};

/* Mapping between screen and image.  */
class scr_to_img
{
public:
  void set_parameters (scr_to_img_parameters param);
  void get_range (int img_width, int img_height,
		  int *scr_xshift, int *scr_yshift,
		  int *scr_width, int *scr_height);
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
#endif
