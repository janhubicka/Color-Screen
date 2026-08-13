/* Periodic transmission screens and their optical filtering operations.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef SCREEN_H
#define SCREEN_H
#include <memory>
#include "include/color.h"
#include "include/scr-to-img.h"
#include "include/colorscreen.h"
#include "mtf.h"
namespace colorscreen {
struct sharpen_parameters;
class screen_filter_source;
template<typename T> class precomputed_function;

/* Operation counts for one exact periodic-screen capture-transfer build.
   The caller owns the structure and may pass null when profiling is not
   required.  Counts describe numerical work, not cache lookups.  */
struct screen_filter_profile
{
  uint64_t mtf_precompute_calls = 0;
  uint64_t mtf_psf_precompute_calls = 0;
  uint64_t physical_focus_cache_hits = 0;
  uint64_t physical_focus_cache_misses = 0;
  uint64_t physical_focus_transfer_builds = 0;
  uint64_t direct_transfer_builds = 0;
  uint64_t wrapped_psf_builds = 0;
  uint64_t kernel_forward_ffts = 0;
  uint64_t screen_forward_ffts = 0;
  uint64_t screen_inverse_ffts = 0;
};

/* Periodic linear-light representation of a historical color screen.

   MULT contains multiplicative transmission and is the quantity affected by
   optical blur, capture MTF, and sharpening.  ADD is presentation-only data
   used by preview patterns; optical filtering preserves it unchanged.  Both
   arrays represent one periodic unit cell sampled on a SIZE by SIZE grid.  */
class screen
{
public:
  /* Size of the arrays holding the screen.  Must be power of 2.  */
  static const int size=128;
  /* blur radius is in screen coordiates. 0.25 makes almost invisible.  */
  constexpr static const coord_t max_blur_radius = 0.25;
  /* Multiplicative transmission and additive preview contribution.  */
  luminosity_t mult[size][size][3];
  luminosity_t add[size][size][3];

  /* Return multiplicative transmission at periodic screen coordinate P using
     bilinear interpolation.  */
  __attribute__ ((always_inline)) inline pure_attr rgbdata
  interpolated_mult (point_t p) const
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
  /* Return multiplicative transmission at the nearest periodic sample to P.  */
  inline pure_attr rgbdata
  noninterpolated_mult (point_t p) const
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
  DLL_PUBLIC void initialize_with_blur (screen &scr, coord_t blur_radius, blur_alg alg = blur_auto);
  /* Same but specify different blur for each color.  */
  DLL_PUBLIC void initialize_with_blur (screen &scr, rgbdata blur_radius, blur_alg alg = blur_auto);
  /* Initialize THIS from SCR after applying the capture transfer described by
     SHARPEN.  ANTICIPATE_SHARPENING additionally applies the selected digital
     inverse filter; when false, only the forward capture blur is applied.
     PARALLEL permits OpenMP in expensive PSF construction.  ADD is copied
     unchanged from SCR.  Return false if transfer/PSF construction fails.
     On failure THIS may be only partly initialized; the caller must discard or
     otherwise ignore it rather than attempting to preserve the old contents.  */
  nodiscard_attr DLL_PUBLIC bool
  initialize_with_sharpen_parameters (screen &scr,
                                      sharpen_parameters *sharpen[3],
                                      bool anticipate_sharpening,
                                      bool parallel = true,
                                      screen_filter_profile *profile = nullptr);
  /* Prepare the source-side Fourier state of THIS for repeated periodic
     capture filtering.  The resulting SOURCE is immutable and may be shared
     by multiple threads.  PROFILE, when nonnull, records the three forward
     channel transforms performed here.  */
  nodiscard_attr bool
  prepare_filter_source (screen_filter_source &source,
                         screen_filter_profile *profile = nullptr) const;
  /* Initialize THIS by applying SHARPEN to a source previously prepared by
     PREPARE_FILTER_SOURCE.  This path reuses the source channel FFTs and
     therefore performs only the focus-dependent transfer construction and
     inverse transforms.  For the analytical physical model it applies the
     signed OTF directly at the periodic Fourier harmonics instead of building
     and rewrapping a spatial PSF.  Richardson-Lucy sharpening is not supported
     because it iterates in the spatial domain; return false if requested.  */
  nodiscard_attr bool
  initialize_with_sharpen_parameters (
      const screen_filter_source &source,
      sharpen_parameters *sharpen[3], bool anticipate_sharpening,
      bool parallel = true, screen_filter_profile *profile = nullptr);
  /* Initialize screen to the dufaycolor screen plate.  */
  void dufay (coord_t red_strip_width, coord_t green_strip_width);
  void strip (coord_t first_strip_width, coord_t second_strip_width, int color1, int color2, int color3);
  void preview_strip (coord_t first_strip_width, coord_t second_strip_width, int color1, int color2, int color3);
  DLL_PUBLIC bool save_tiff (const char *filename, bool normalize = false, int tiles = 3) const;
  DLL_PUBLIC std::unique_ptr<simple_image> get_image (bool normalize = false, int tiles = 3) const;
  /* Clamp every multiplicative channel sample to the physical range 0..1.  */
  DLL_PUBLIC void clamp ();

  /* Return normalized total red, green, and blue transmission.  Return zero
     for an empty or nonfinite screen.  */
  DLL_PUBLIC rgbdata patch_proportions () const;

  /* Initialize THIS by filtering SCR with the three radial POINT_SPREAD
     functions at per-channel SCALE.  ADD is copied unchanged from SCR.  */
  void initialize_with_point_spread (
      screen &scr, precomputed_function<luminosity_t> *point_spread[3],
      rgbdata scale);
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
  void initialize_with_gaussian_blur (screen &scr, rgbdata blur_radius, blur_alg alg);
  void initialize_with_1D_fft (screen &scr, luminosity_t weights[size], int cmin = 0, int cmax = 3);
};

/* Immutable source-side state for repeated exact periodic-screen filtering.
   Its implementation is private so FFTW storage and plans do not leak into
   the internal screen interface.  Instances are move-only and are normally
   owned through the finetune LRU cache.  */
class screen_filter_source
{
public:
  screen_filter_source ();
  ~screen_filter_source ();
  screen_filter_source (screen_filter_source &&) noexcept;
  screen_filter_source &operator= (screen_filter_source &&) noexcept;
  screen_filter_source (const screen_filter_source &) = delete;
  screen_filter_source &operator= (const screen_filter_source &) = delete;

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
  friend class screen;
};
}
#endif
