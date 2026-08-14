#ifndef ANALYZE_SCANNER_BLUR_H
#define ANALYZE_SCANNER_BLUR_H
#include <cassert>
#include <string>
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
        optimize_strip_widths (true), reoptimize_strip_widths (false),
        skipmin (25), skipmax (25),
        tolerance (-1), min_contrast (finetune_default_min_contrast),
        progress (NULL),
        verbose (false),
        report_profile (false), interpolate_focus (false),
        focus_mtf_threshold ((coord_t)0.05), focus_interpolation_nodes (33),
        focus_interpolation_max (0), focus_screen_frequency (0)
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
  /* For variable-strip screens, fit strip widths in the coarse prepass.
     When false, keep the strip widths already present in RPARAM (or the
     process defaults when they are zero).  */
  bool optimize_strip_widths;
  /* Re-estimate strip widths in every dense-grid sample instead of fixing the
     values determined by the prepass/current rendering parameters.  */
  bool reoptimize_strip_widths;
  /* Percentages removed from the low and high ends of robust histograms.  */
  coord_t skipmin;
  coord_t skipmax;
  /* Maximum accepted robust correction range within one table entry.
     Negative values disable the check.  */
  coord_t tolerance;
  /* Smallest fitted positional colour contrast accepted as an identifiable
     blur/focus measurement.  Successful fits below this threshold are
     rejected separately from numerical solver failures.  */
  luminosity_t min_contrast;
  /* Optional progress/cancellation object.  */
  progress_info *progress;
  bool verbose;
  /* Print accumulated FINETUNE/cache counters after analysis.  */
  bool report_profile;
  /* During the dense scalar physical-defocus pass, approximate arbitrary
     focus values by interpolating exact filtered screens from a fixed
     nonlinear cache grid.  The coarse prepass remains exact.  */
  bool interpolate_focus;
  /* Stop the focus grid at the first defocus where the process-screen
     frequency falls to this system-MTF magnitude.  */
  coord_t focus_mtf_threshold;
  /* Number of quadratically spaced exact cache nodes, including endpoints.
     The current linked-list LRU cache has 64 entries.  */
  int focus_interpolation_nodes;

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
  /* Aggregate profiling data from all completed prepass and dense fits.  */
  DLL_PUBLIC finetune_profile get_profile () const;
  /* Print a compact accumulated profile through the worker's progress output
     discipline.  This does not require a successful correction table.  */
  DLL_PUBLIC void print_profile () const;
  /* Explain the most recent sequential-stage failure when available.
     Parallel local fits retain their own FINETUNE_RESULT errors; STEP2 and
     STEP3 summarize them without racing on this string.  */
  const std::string &
  error () const
  {
    return last_error;
  }

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
  coord_t focus_interpolation_max;
  coord_t focus_screen_frequency;
  std::string last_error;

  /* Record and print one failure detected by a sequential worker stage.  */
  void set_error (const std::string &message);
};
} // namespace colorscreen
#endif
