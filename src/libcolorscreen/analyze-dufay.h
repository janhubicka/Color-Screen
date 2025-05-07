#ifndef ANALYZE_DUFAY_H
#define ANALYZE_DUFAY_H
#include "include/dufaycolor.h"
#include "render-to-scr.h"
#include "analyze-base.h"
namespace colorscreen
{

template class analyze_base_worker <dufay_geometry>;

class analyze_dufay : public analyze_base_worker <dufay_geometry>
{
public:
  analyze_dufay()
  /* We store two reds per X coordinate.  */
  : analyze_base_worker (1, 0, 0, 0, 0, 0)
  {
  }
  ~analyze_dufay()
  {
  }

  bool analyze_contrast (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, progress_info *progress = NULL);
  luminosity_t compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress);
  bool dump_patch_density (FILE *out);
private:
};
}
#endif
