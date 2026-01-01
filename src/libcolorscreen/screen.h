#ifndef SCREEN_H
#define SCREEN_H
#include "include/color.h"
#include "include/scr-to-img.h"
#include "mtf.h"
namespace colorscreen {
class sharpen_parameters;
template<typename T> class precomputed_function;
/* Representation of the screen wich can then be superposed to the image
   using render_superpose_img.  */
class screen
{
public:
  /* Size of the arrays holding the screen.  Must be power of 2.  */
  static const int size=128;
  /* blur radius is in screen coordiates. 0.25 makes almost invisible.  */
  constexpr static const coord_t max_blur_radius = 0.25;
  /* Mult specify how much one should multiply, add how much add
     and keep how much keep in the color.  */
  luminosity_t mult[size][size][3];
  luminosity_t add[size][size][3];

  /* Return multiplicative factor of point p with bilinear interpolation.  */
  __attribute__ ((always_inline)) inline pure_attr rgbdata
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
  DLL_PUBLIC void initialize (enum scr_type type, coord_t dufay_red_strip_width = 0, coord_t dufay_green_strip_width = 0);
  /* Initialize to a given screen for preview window.  */
  void initialize_preview (enum scr_type type, coord_t dufay_red_strip_width = 0, coord_t dufay_green_strip_width = 0);
  enum blur_type
  {
    /* Gaussian blur.  */
    blur_gaussian,
    /* MTF from IMOD's mtffliter.  */
    blur_mtffilter
  };

  /* Algorithm to use for bluring.  For small blurs
     direct algorthm is better, for large blur fft wins.
     This is used primarily for testing.  */
  enum blur_alg
  {
    /* Choose best algorithm for given blur type and radius.  */
    blur_auto,
    /* Apply the kernel directly (faster for small radiuses)
       Useful only for gaussian blur.  */
    blur_direct,
    /* Apply FFT (faster for bigger radiuses)  */
    blur_fft,
    /* Apply 2dFFT (faster for bigger radiuses)  */
    blur_fft2d
  };
  /* Compare two screens.  */
  DLL_PUBLIC bool almost_equal_p (const screen &scr, luminosity_t *delta_ret = NULL, luminosity_t maxdelta = 1.0/2048) const;
  DLL_PUBLIC bool sum_almost_equal_p (const screen &scr, rgbdata *delta_ret = NULL, luminosity_t maxdelta = 1.0/2048) const;
  /* Initialize screen with single dot in middle.  Use to compute dot spread function.  */
  void initialize_dot ();
  /* Initialize imitating lens blur.  */
  DLL_PUBLIC void initialize_with_blur (screen &scr, coord_t blur_radius, enum blur_type = /*blur_mtffilter*/ blur_gaussian, blur_alg alg = blur_auto);
  /* Same but specify different blur for each color.  */
  DLL_PUBLIC void initialize_with_blur (screen &scr, rgbdata blur_radius, enum blur_type = /*blur_mtffilter*/ blur_gaussian, blur_alg alg = blur_auto);
  DLL_PUBLIC void initialize_with_blur (screen &scr, luminosity_t mtf[4], enum blur_alg alg = blur_auto);
  DLL_PUBLIC void initialize_with_blur_point_spread (screen &scr, luminosity_t ps[4], enum blur_alg alg = blur_auto);
  DLL_PUBLIC void initialize_with_sharpen_parameters (screen &scr, sharpen_parameters *sharpen[3], bool anticipate_sharpening);
  /* Initialize screen to the dufaycolor screen plate.  */
  void dufay (coord_t red_strip_width, coord_t green_strip_width);
  void strip (coord_t first_strip_width, coord_t second_strip_width, int color1, int color2, int color3);
  void preview_strip (coord_t first_strip_width, coord_t second_strip_width, int color1, int color2, int color3);
  DLL_PUBLIC bool save_tiff (const char *filename, bool normalize = false, int tiles = 3);
  DLL_PUBLIC void clamp ();
  DLL_PUBLIC rgbdata patch_proportions ();
  static void print_mtf (FILE *f, luminosity_t mtf[4], coord_t pixel_size);
  void initialize_with_point_spread (screen &scr, precomputed_function<luminosity_t> *point_spread[3], rgbdata scale);
private:
  /* Initialize screen to the thames screen plate.  */
  void thames ();
  /* Initialize screen to the paget/finlay screen plate.  */
  void paget_finlay ();
  /* Initialize screen to the preview screen that corresponds to Finlay or Paget plate.  */
  void preview ();
  void preview_dufay ();
  __attribute__ ((always_inline)) inline void initialize_with_1d_kernel (screen &scr, int clen, luminosity_t *cmatrix, luminosity_t *hblur, int c);
  //__attribute__ ((always_inline)) inline void initialize_with_2d_kernel (screen &scr, int clen, luminosity_t *cmatrix2d, int c);
  void initialize_with_gaussian_blur (screen &scr, coord_t blur_radius, int cmin, int cmax);
  void initialize_with_gaussian_blur (screen &scr, rgbdata blur_radius, blur_alg alg = blur_auto);
  void initialize_with_1D_fft (screen &scr, luminosity_t weights[size], int cmin = 0, int cmax = 3);
  void initialize_with_fft_blur (screen &scr, rgbdata blur_radius);
};
}
#endif
