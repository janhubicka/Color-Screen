#ifndef SCREEN_H
#define SCREEN_H
#include "color.h"
#include "scr-to-img.h"
/* Representation of the screen wich can then be superposed to the image
   using render_superpose_img.  */
struct DLL_PUBLIC screen
{
  /* Size of the arrays holding the screen.  Must be power of 2.  */
  static const int size=128;
  /* Mult specify how much one should multiply, add how much add
     and keep how much keep in the color.  */
  luminosity_t mult[size][size][3];
  luminosity_t add[size][size][3];

  /* Initialize empty screen (so rendering will show original image).  */
  void empty ();
  /* Initialize to a given screen.  */
  void initialize (enum scr_type type, coord_t dufay_red_strip_width = 0, coord_t dufay_green_strip_width = 0);
  /* Initialize to a given screen for preview window.  */
  void initialize_preview (enum scr_type type);
  /* Initialize imitating lens blur.  */
  void initialize_with_blur (screen &scr, coord_t blur_radius);
  /* Initialize screen to the dufaycolor screen plate.  */
  void dufay (coord_t red_strip_width, coord_t green_strip_width);
private:
  /* Initialize screen to the thames screen plate.  */
  void thames ();
  /* Initialize screen to the paget/finlay screen plate.  */
  void paget_finlay ();
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview ();
  void preview_dufay ();
};
#endif
