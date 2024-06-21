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
  /* blur radius is in screen coordiates. 0.25 makes almost invisible.  */
  constexpr static const coord_t max_blur_radius = 0.25;
  /* Mult specify how much one should multiply, add how much add
     and keep how much keep in the color.  */
  luminosity_t mult[size][size][3];
  luminosity_t add[size][size][3];

  /* Return multiplicative factor of point p with bilinear interpolation.  */
  inline pure_attr rgbdata
  interpolated_mult (point_t p)
  {
    int sx, sy;
    coord_t rx = my_modf (p.x * size, &sx);
    coord_t ry = my_modf (p.y * size, &sy);
    int sx1 = ((unsigned)sx + 1u) & (unsigned)(size - 1);
    int sy1 = ((unsigned)sy + 1u) & (unsigned)(size - 1);
    sx = (unsigned)sx & (unsigned)(size - 1);
    sy = (unsigned)sy & (unsigned)(size - 1);
    rgbdata d1 = {mult[sy][sx][0], mult[sy][sx][1], mult[sy][sx][2]};
    rgbdata d2 = {mult[sy][sx1][0], mult[sy][sx1][1], mult[sy][sx1][2]};
    rgbdata i1 = d1 * (1 - rx) + d2 * rx;
    rgbdata dd1 = {mult[sy1][sx][0], mult[sy1][sx][1], mult[sy1][sx][2]};
    rgbdata dd2 = {mult[sy1][sx1][0], mult[sy1][sx1][1], mult[sy1][sx1][2]};
    rgbdata i2 = dd1 * (1 - rx) + dd2 * rx;
    return i1 * (1 - ry) + i2 * ry;
  }
  /* Return multiplicative factor of point p with bilinear interpolation.  */
  inline pure_attr rgbdata
  noninterpolated_mult (point_t p)
  {
    int ix = (uint64_t) nearest_int ((p.x) * size) & (unsigned)(size - 1);
    int iy = (uint64_t) nearest_int ((p.y) * size) & (unsigned)(size - 1);
    return {mult[iy][ix][0], mult[iy][ix][1], mult[iy][ix][2]};
  }

  /* Initialize empty screen (so rendering will show original image).  */
  void empty ();
  /* Initialize to a given screen.  */
  void initialize (enum scr_type type, coord_t dufay_red_strip_width = 0, coord_t dufay_green_strip_width = 0);
  /* Initialize to a given screen for preview window.  */
  void initialize_preview (enum scr_type type);
  /* Initialize imitating lens blur.  */
  void initialize_with_blur (screen &scr, coord_t blur_radius);
  /* Same but specify different blur for each color.  */
  void initialize_with_blur (screen &scr, rgbdata blur_radius);
  void initialize_with_fft_blur (screen &scr, rgbdata blur_radius);
  void initialize_with_fft_blur (screen &scr, coord_t blur_radius);
  /* Initialize screen to the dufaycolor screen plate.  */
  void dufay (coord_t red_strip_width, coord_t green_strip_width);
  bool save_tiff (const char *filename);
private:
  /* Initialize screen to the thames screen plate.  */
  void thames ();
  /* Initialize screen to the paget/finlay screen plate.  */
  void paget_finlay ();
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview ();
  void preview_dufay ();
  __attribute__ ((always_inline)) inline void initialize_with_blur (screen &scr, int clen, luminosity_t *cmatrix, luminosity_t *hblur, int channel);
  void initialize_with_blur (screen &scr, coord_t blur_radius, int channel);
};
#endif
