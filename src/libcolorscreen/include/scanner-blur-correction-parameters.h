#ifndef SCANNER_BLUR_CORRECTION_PARAMETERS_H
#define SCANNER_BLUR_CORRECTION_PARAMETERS_H
#include "base.h"
#include "color.h"
namespace colorscreen
{
class scanner_blur_correction_parameters
{
public:
  enum correction_mode
  {
    blur_radius,
    mtf_defocus,
    mtf_blur_diameter,
    max_correction
  };

  /* Diagnostics describing how one correction-table entry was reduced from
     its local FINETUNE samples.  ROBUST_SPREAD is the retained high-low
     correction range in the same units as GET_CORRECTION.  MEAN_CONTRAST is
     the mean fitted screen modulation of the accepted samples.  */
  struct cell_diagnostics
  {
    luminosity_t robust_spread;
    luminosity_t mean_contrast;
    int accepted_samples;
    int total_samples;
  };

  DLL_PUBLIC static const char *correction_names [max_correction];
  DLL_PUBLIC static const char *pretty_correction_names [max_correction];
  DLL_PUBLIC scanner_blur_correction_parameters ();
  /* Allocate a zero-filled WIDTH by HEIGHT correction table.  On failure the
     existing table is left unchanged.  */
  DLL_PUBLIC bool alloc (int width, int height, enum correction_mode mode);
  DLL_PUBLIC ~scanner_blur_correction_parameters ();
  DLL_PUBLIC bool save (FILE *f);
  DLL_PUBLIC const char *save_tiff (const char *name);
  /* Save one CSV row per correction cell, including correction, robust
     spread, accepted sample support and mean fitted contrast.  Diagnostics
     are deliberately stored separately from CSP files so older Color-Screen
     versions can continue to load the correction table.  */
  DLL_PUBLIC bool save_diagnostics (FILE *f) const;
  DLL_PUBLIC bool load (FILE *f, const char **);

  /* Allocate zero-filled per-cell reduction diagnostics for the existing
     correction-table dimensions.  On failure existing diagnostics are left
     unchanged.  */
  DLL_PUBLIC bool alloc_diagnostics ();
  inline bool has_diagnostics () const
  {
    return m_diagnostics != NULL;
  }
  inline void set_diagnostics (int x, int y,
                               const cell_diagnostics &diagnostics)
  {
    m_diagnostics[y * m_width + x] = diagnostics;
  }
  inline const cell_diagnostics *get_diagnostics (int x, int y) const
  {
    return m_diagnostics ? &m_diagnostics[y * m_width + x] : NULL;
  }
  inline void set_correction (int x, int y, luminosity_t radius)
  {
    m_corrections[y * m_width + x] = radius;
  }
  inline luminosity_t get_correction (int x, int y) const
  {
    return m_corrections[y * m_width + x];
  }
  inline int get_width () const
  {
    return m_width;
  }
  inline int get_height () const
  {
    return m_height;
  }
  correction_mode get_mode () const
  {
    return m_mode;
  }

  /* Unique id of the image (used for caching).  */
  uint64_t id;

private:
  int m_width, m_height;
  luminosity_t *m_corrections;
  cell_diagnostics *m_diagnostics;
  enum correction_mode m_mode;
};
}
#endif
