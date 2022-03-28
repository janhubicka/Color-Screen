#ifndef SCREEN_H
#define SCREEN_H
#include "scr-to-img.h"
/* Representation of the screen wich can then be superposed to the image
   using render_superpose_img.  */
struct screen
{
  /* Size of the arrays holding the screen.  Must be power of 2.  */
  static const int size=256;
  /* Mult specify how much one should multiply, add how much add
     and keep how much keep in the color.  */
  double mult[size][size][3];
  double add[size][size][3];

  /* Initialize empty screen (so rendering will show original image).  */
  void empty ();
  /* Initialize to a given screen.  */
  void initialize (enum scr_type type)
  {
    switch (type)
    {
      case Finlay:
      case Paget:
	paget_finlay ();
	break;
      case Dufay:
	dufay ();
	break;
      case Thames:
	thames ();
	break;
      default:
	abort ();
	break;
    }
  }
  /* Initialize to a given screen for preview window.  */
  void initialize_preview (enum scr_type type)
  {
    if (type == Dufay)
      preview_dufay ();
    else
      preview ();
  }
private:
  /* Initialize screen to the thames screen plate.  */
  void thames ();
  /* Initialize screen to the paget/finlay screen plate.  */
  void paget_finlay ();
  /* Initialize screen to the dufaycolor screen plate.  */
  void dufay ();
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview ();
  void preview_dufay ();
};
#endif
