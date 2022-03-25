/* Mult specify how much one should multiply, add how much add
   and keep how much keep in the color.  */
struct screen
{
  static const int size=256;
  double mult[size][size][3];
  double add[size][size][3];
  /* Initialize screen to the thames screen plate (unused).  */
  void thames (int maxval);
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview (int maxval);
};
