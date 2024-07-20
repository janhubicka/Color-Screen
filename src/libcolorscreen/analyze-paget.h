#ifndef ANALYZE_PAGET_H
#define ANALYZE_PAGET_H
#include "include/render-to-scr.h"
#include "include/paget.h"
#include "analyze-base.h"
template class analyze_base_worker <paget_geometry>;
class analyze_paget : public analyze_base_worker <paget_geometry>
{
public:
  /* Two blues per X coordinate.  */
  analyze_paget()
  : analyze_base_worker (0, 1, 0, 1, 1, 1)
  {
  }
  ~analyze_paget()
  {
  }
  virtual bool write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress = NULL, luminosity_t rmin = 0, luminosity_t rmax = 1, luminosity_t gmin = 0, luminosity_t gmax = 1, luminosity_t bmin = 0, luminosity_t bmax = 1);
  virtual int find_best_match (int percentake, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift, coord_t *yshift, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress = NULL);
  bool dump_patch_density (FILE *out);
private:
};
#endif
