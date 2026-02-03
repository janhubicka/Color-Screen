#ifndef ANALYZE_SCANNER_BLUR_H
#define ANALYZE_SCANNER_BLUR_H
#include "colorscreen.h"
#include "scanner-blur-correction-parameters.h"
#include "histogram.h"
#include "scr-to-img-parameters.h"
#include "imagedata.h"
#include "finetune.h"
namespace colorscreen
{
class
analyze_scanner_blur_worker
{
  public:
  analyze_scanner_blur_worker (scr_to_img_parameters &param1, render_parameters &rparam1, image_data &scan1)
  : param (param1), rparam (rparam1), scan (scan1), strip_xsteps (0), strip_ysteps (0), xsteps (0), ysteps (0), xsubsteps (0), ysubsteps (0), flags (finetune_position | finetune_no_progress_report | finetune_scanner_mtf_defocus), reoptimize_strip_widths (false), skipmin (25), skipmax (25), tolerance (-1), progress (NULL), verbose (false)
  {
  }
  scr_to_img_parameters &param;
  render_parameters rparam;
  image_data &scan;
  int strip_xsteps, strip_ysteps;
  int xsteps, ysteps;
  int xsubsteps, ysubsteps;
  uint64_t flags;
  bool reoptimize_strip_widths;
  coord_t skipmin; coord_t skipmax;
  coord_t tolerance;
  progress_info *progress;
  bool verbose;

  DLL_PUBLIC bool step1 ();
  bool do_strips ()
  {
    return prepass.size () > 0;
  }
  DLL_PUBLIC bool analyze_strips (int x, int y);
  DLL_PUBLIC bool step2 ();
  DLL_PUBLIC bool analyze_blur (int x, int y);
  DLL_PUBLIC std::unique_ptr <scanner_blur_correction_parameters> step3 ();
private:
  scanner_blur_correction_parameters::correction_mode mode;
  histogram uncertainity_hist;
  histogram red_hist;
  histogram green_hist;
  histogram blur_hist;
  std::vector<finetune_result> prepass;
  std::vector<finetune_result> mainpass;
};
}
#endif
