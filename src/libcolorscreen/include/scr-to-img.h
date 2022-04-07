#ifndef SCR_TO_IMG_H
#define SCR_TO_IMG_H
#include "matrix.h"

typedef float coord_t;
typedef matrix4x4<coord_t> trans_matrix;

/* Types of supported screens.  */
enum scr_type
{
   Paget,
   Thames,
   Finlay,
   Dufay,
   max_scr_type
};

/* This implements to translate image coordiantes to coordinates of the viewing screen.
   In the viewing screen the coordinats (0,0) describe a green dot and
   the screen is periodic with period 1: that is all integer coordinates describes
   gren dots again.  */

struct scr_to_img_parameters
{
  scr_to_img_parameters ()
  : center_x (0), center_y (0), coordinate1_x(5), coordinate1_y (0), coordinate2_x (0), coordinate2_y (5), type (Finlay)
  { }
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
  enum scr_type type;
};

/* Mapping between screen and image.  */
class scr_to_img
{
public:
  void set_parameters (scr_to_img_parameters param);
  void get_range (int img_width, int img_height,
		  int *scr_xshift, int *scr_yshift,
		  int *scr_width, int *scr_height);
  void get_range (coord_t x1, coord_t y1,
      		  coord_t x2, coord_t y2,
		  int *scr_xshift, int *scr_yshift,
		  int *scr_width, int *scr_height);
  /* Map screen coordinates to image coordinates.  */
  void
  to_img (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_matrix.perspective_transform (x,y, *xp, *yp);
  }
  /* Map image coordinats to screen.  */
  void
  to_scr (coord_t x, coord_t y, coord_t *xp, coord_t *yp)
  {
    m_matrix.inverse_perspective_transform (x,y, *xp, *yp);
  }
  enum scr_type
  get_type ()
  {
    return m_param.type;
  }
private:
  /* Screen->image translation matrix.  */
  trans_matrix m_matrix;
  scr_to_img_parameters m_param;
};
#endif
