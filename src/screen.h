#ifndef SCREEN_H
#define SCREEN_H
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
  /* Initialize empty screen.  */
  void empty ();
  /* Initialize screen to the thames screen plate.  */
  void thames ();
  /* Initialize screen to the paget/finlay screen plate.  */
  void paget_finlay ();
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview ();
};
#endif
