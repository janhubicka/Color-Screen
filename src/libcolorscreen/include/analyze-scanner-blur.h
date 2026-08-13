#ifndef ANALYZE_SCANNER_BLUR_H
#define ANALYZE_SCANNER_BLUR_H
#include <cassert>
#include "colorscreen.h"
#include "finetune.h"
#include "histogram.h"
#include "imagedata.h"
#include "scanner-blur-correction-parameters.h"
#include "scr-to-img-parameters.h"
namespace colorscreen
{
/* Staged worker used to estimate scanner blur or optical defocus across a
   scan.  STEP1 prepares a coarse strip-width/focus prepass, STEP2 reduces the
   prepass and prepares the dense grid, ANALYZE_BLUR evaluates one dense-grid
   sample, and STEP3 robustly reduces the samples to the correction table.

   The worker is intentionally split into stages so callers can schedule the
   expensive FINETUNE calls in their own thread pool.  A worker instance is
   single-use: run the stages in the order documented above.  */
class analyze_scanner_blur_worker
{
public:
  /* Construct a worker for geometry PARAM1, rendering parameters RPARAM1 and
     source image SCAN1.  RPARAM is copied because STEP2 replaces its global
     blur/defocus and strip widths by the robust prepass estimates.  */
  analyze_scanner_blur_worker (scr_to_img_parameters &param1,
                               render_parameters &rparam1, image_data &scan1)
      : param (param1), rparam (rparam1), scan (scan1), strip_xsteps (0),
        strip_ysteps (0), xsteps (0), ysteps (0), xsubsteps (0), ysubsteps (0),
        flags (finetune_position | finetune_no_progress_report
               | finetune_scanner_mtf_defocus),
        reoptimize_strip_widths (false), skipmin (25), skipmax (25),
        tolerance (-1), progress (NULL), verbose (false)
  {
  }

  /* Geometry and source image used by all FINETUNE calls.  */
  scr_to_img_parameters &param;
  render_parameters rparam;
  image_data &scan;

  /* Number of coarse prepass samples.  Zero selects an aspect-ratio-aware
     default.  */
  int strip_xsteps, strip_ysteps;
  /* Dimensions of the final correction table.  */
  int xsteps, ysteps;
  /* Number of independent FINETUNE samples reduced into each table entry.  */
  int xsubsteps, ysubsteps;
  /* FINETUNE flags.  Exactly one stored correction family (legacy blur or MTF
     focus) must be selected.  Sigma may be fitted together with MTF focus.
     Per-channel fits are currently reduced to their scalar mean because the
     correction-table format stores one value per cell.  */
  uint64_t flags;
  /* Re-estimate strip widths in every dense-grid sample instead of fixing the
     values determined by the prepass.  */
  bool reoptimize_strip_widths;
  /* Percentages removed from the low and high ends of robust histograms.  */
  coord_t skipmin;
  coord_t skipmax;
  /* Maximum accepted robust correction range within one table entry.
     Negative values disable the check.  */
  coord_t tolerance;
  /* Optional progress/cancellation object.  */
  progress_info *progress;
  bool verbose;

  /* Prepare dimensions and the coarse prepass.  Return false for invalid
     settings or a cancellation request.  */
  DLL_PUBLIC bool step1 ();
  /* Return true when STEP1 prepared a coarse prepass.  */
  bool
  do_strips () const
  {
    return !prepass.empty ();
  }
  /* Evaluate coarse prepass sample X,Y.  Store fitted strip widths when the
     optional output pointers are nonnull.  */
  DLL_PUBLIC bool analyze_strips (int x, int y,
                                  coord_t *red_strip_width = NULL,
                                  coord_t *green_strip_width = NULL);
  /* Robustly reduce the coarse prepass, update the worker's RPARAM, and
     allocate the dense-grid result storage.  */
  DLL_PUBLIC bool step2 ();
  /* Evaluate dense-grid sample X,Y.  Store its scalar correction in all three
     components of DISPLACEMENTS when requested.  */
  DLL_PUBLIC bool analyze_blur (int x, int y, rgbdata *displacements = NULL);
  /* Robustly reduce dense samples and return the adaptive correction table.
     Return null on cancellation, invalid data, or tolerance failure.  */
  DLL_PUBLIC std::unique_ptr<scanner_blur_correction_parameters> step3 ();

private:
  /* Return coarse result X,Y.  Keeping indexing here prevents accidental use
     of the dense-grid stride.  */
  finetune_result &
  prepass_result (int x, int y)
  {
    assert (x >= 0 && x < strip_xsteps && y >= 0 && y < strip_ysteps);
    return prepass[(size_t)y * strip_xsteps + x];
  }

  /* Return dense result X,Y.  X and Y are absolute sub-sample coordinates,
     not correction-table coordinates.  */
  finetune_result &
  mainpass_result (int x, int y)
  {
    const int width = xsteps * xsubsteps;
    const int height = ysteps * ysubsteps;
    assert (x >= 0 && x < width && y >= 0 && y < height);
    return mainpass[(size_t)y * width + x];
  }

  scanner_blur_correction_parameters::correction_mode mode;
  histogram red_hist;
  histogram green_hist;
  histogram blur_hist;
  std::vector<finetune_result> prepass;
  std::vector<finetune_result> mainpass;
};
} // namespace colorscreen
#endif
