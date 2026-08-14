#include <cctype>
#include <climits>
#include <cstdlib>
#include <limits>
#include <utility>
#include "include/tiff-writer.h"
#include "include/scanner-blur-correction-parameters.h"
#include "loadsave.h"
#include "lru-cache.h"
namespace colorscreen
{

const char *scanner_blur_correction_parameters::correction_names[] = {
	"blur-radius",
	"mtf-defocus",
	"mtf-blur-diameter",
};
const char *scanner_blur_correction_parameters::pretty_correction_names[] = {
	"blur radius",
	"mtf defocus",
	"mtf blur diameter",
};
scanner_blur_correction_parameters::scanner_blur_correction_parameters ()
    : id (lru_caches::get ()), m_width (0), m_height (0),
      m_corrections (NULL), m_diagnostics (NULL), m_mode (max_correction)
{
}
bool
scanner_blur_correction_parameters::alloc (int width, int height, enum correction_mode mode)
{
  if (width <= 0 || height <= 0 || mode < blur_radius
      || mode >= max_correction)
    return false;
  if ((size_t)width > std::numeric_limits<size_t>::max () / (size_t)height)
    return false;
  const size_t count = (size_t)width * (size_t)height;
  if (count > INT_MAX
      || count > std::numeric_limits<size_t>::max () / sizeof (luminosity_t))
    return false;
  luminosity_t *corrections
      = (luminosity_t *)calloc (count, sizeof (luminosity_t));
  if (!corrections)
    return false;

  free (m_corrections);
  free (m_diagnostics);
  m_corrections = corrections;
  m_diagnostics = NULL;
  m_width = width;
  m_height = height;
  m_mode = mode;
  /* A successful replacement changes every cache key that refers to this
     table, even when its dimensions and address happen to stay the same.  */
  id = lru_caches::get ();
  return true;
}

scanner_blur_correction_parameters::~scanner_blur_correction_parameters ()
{
  if (m_corrections)
    free (m_corrections);
  if (m_diagnostics)
    free (m_diagnostics);
}

/* Allocate zero-filled reduction diagnostics for the current table.  */
bool
scanner_blur_correction_parameters::alloc_diagnostics ()
{
  if (!m_corrections || m_width <= 0 || m_height <= 0)
    return false;
  const size_t count = (size_t)m_width * (size_t)m_height;
  if (count > std::numeric_limits<size_t>::max ()
                  / sizeof (cell_diagnostics))
    return false;
  cell_diagnostics *diagnostics
      = (cell_diagnostics *)calloc (count, sizeof (cell_diagnostics));
  if (!diagnostics)
    return false;
  free (m_diagnostics);
  m_diagnostics = diagnostics;
  return true;
}

bool
scanner_blur_correction_parameters::save (FILE *f)
{
  if (!f || !m_corrections || m_width <= 0 || m_height <= 0
      || m_mode < blur_radius || m_mode >= max_correction)
    return false;
  if (fprintf (f, "  scanner_blur_correction_dimensions: %i %i\n", m_width,
               m_height)
      < 0)
    return false;
  if (fprintf (f, "  scanner_blur_correction_type: %s\n", correction_names[(int)m_mode]) < 0)
    return false;
  switch (m_mode)
    {
    case blur_radius:
      if (fprintf (f, "  scanner_blur_correction_gaussian_blurs:") < 0)
	return false;
      break;
    case mtf_blur_diameter:
      if (fprintf (f, "  scanner_blur_correction_blur_diameter_pxs:") < 0)
	return false;
      break;
    case mtf_defocus:
      if (fprintf (f, "  scanner_blur_correction_defocus_mms:") < 0)
	return false;
      break;
    default:
      abort ();
    }
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n                             ");
      for (int x = 0; x < m_width; x++)
        for (int i = 0; i < 4; i++)
          if (fprintf (f, " %e", m_corrections[y * m_width + x]) < 0)
             return false;
    }
  if (fprintf (f, "\n  scanner_blur_correction_end\n") < 0)
    return false;
  return true;
}
const char *
scanner_blur_correction_parameters::save_tiff (const char *filename)
{
  if (!filename || !m_corrections || m_width <= 0 || m_height <= 0
      || m_mode < blur_radius || m_mode >= max_correction)
    return "No scanner blur correction data";
  tiff_writer_params tp;
  tp.filename = filename;
  tp.width = m_width;
  tp.height = m_height;
  tp.hdr = true;
  tp.depth = 32;
  const char *error;
  tiff_writer out (tp, &error);
  if (error)
    return error;
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
        {
          out.put_hdr_pixel (x, m_corrections[y * m_width + x] * 2,
                             m_corrections[y * m_width + x] * 2,
                             m_corrections[y * m_width + x] * 2);
        }
      if (!out.write_row ())
        return "Write error";
    }
  return NULL;
}

/* Save per-cell reduction diagnostics to F as CSV.  The correction itself is
   included so the file remains useful without the corresponding CSP file.  */
bool
scanner_blur_correction_parameters::save_diagnostics (FILE *f) const
{
  if (!f || !m_corrections || !m_diagnostics || m_width <= 0
      || m_height <= 0 || m_mode < blur_radius || m_mode >= max_correction)
    return false;
  if (fprintf (f,
               "x,y,correction_mode,correction,robust_spread,accepted_samples,"
               "total_samples,accepted_fraction,mean_contrast\n")
      < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
      {
        const size_t i = (size_t)y * m_width + x;
        const cell_diagnostics &d = m_diagnostics[i];
        const double accepted_fraction
            = d.total_samples > 0
                  ? (double)d.accepted_samples / d.total_samples
                  : 0;
        if (fprintf (f, "%d,%d,%s,%.17g,%.17g,%d,%d,%.17g,%.17g\n", x,
                     y, correction_names[(int)m_mode],
                     (double)m_corrections[i], (double)d.robust_spread,
                     d.accepted_samples, d.total_samples, accepted_fraction,
                     (double)d.mean_contrast)
            < 0)
          return false;
      }
  return !ferror (f);
}

bool
scanner_blur_correction_parameters::load (FILE *f, const char **error)
{
  if (!error)
    return false;
  *error = NULL;
  if (!f)
    {
      *error = "missing scanner blur correction input";
      return false;
    }
  if (!expect_keyword (f, "scanner_blur_correction_dimensions:"))
    {
      *error = "expected scanner_blur_correction_dimensions";
      return false;
    }
  int width, height;
  if (fscanf (f, "%i %i", &width, &height) != 2)
    {
      *error = "failed to parse scanner_blur_correction_dimensions";
      return false;
    }
  enum correction_mode mode;
  if (!expect_keyword (f, "scanner_blur_correction_type:"))
    {
      *error = "expected scanner_blur_correction_type";
      return false;
    }
  char buf[256];
  get_keyword (f, buf);
  int j;
  for (j = 0; j < max_correction; j++)
    if (!strcmp (buf, correction_names[j]))
      break;
  if (j == max_correction)
    {
      *error = "unknown correction type";
      return false;
    }
  mode = (enum correction_mode) j;
  switch (mode)
    {
    case blur_radius:
      if (!expect_keyword (f, "scanner_blur_correction_gaussian_blurs:"))
	{
	  *error = "expected scanner_blur_correction_gaussian_blurs";
	  return false;
	}
      break;
    case mtf_blur_diameter:
      if (!expect_keyword (f, "scanner_blur_correction_blur_diameter_pxs:"))
	{
	  *error = "expected scanner_blur_correction_blur_diameter_pxs";
	  return false;
	}
      break;
    case mtf_defocus:
      if (!expect_keyword (f, "scanner_blur_correction_defocus_mms:"))
	{
	  *error = "expected scanner_blur_correction_defocus_mms";
	  return false;
	}
      break;
    default:
      abort ();
    }
  scanner_blur_correction_parameters loaded;
  if (!loaded.alloc (width, height, mode))
    {
      *error = "invalid or too large scanner blur correction dimensions";
      return false;
    }
  for (int y = 0; y < loaded.m_height; y++)
    {
      for (int x = 0; x < loaded.m_width; x++)
        for (int i = 0; i < 4; i++)
            {
              float sx;
              if (fscanf (f, "%f", &sx) != 1)
                {
                  *error = "failed to parse scanner blur correction gaussian blurs";
                  return false;
                }
              loaded.m_corrections[y * loaded.m_width + x] = sx;
            }
    }
  if (!expect_keyword (f, "scanner_blur_correction_end"))
    {
      *error = "expected scanner_blur_correction_end";
      return false;
    }
  std::swap (id, loaded.id);
  std::swap (m_width, loaded.m_width);
  std::swap (m_height, loaded.m_height);
  std::swap (m_corrections, loaded.m_corrections);
  std::swap (m_diagnostics, loaded.m_diagnostics);
  std::swap (m_mode, loaded.m_mode);
  return true;
}
}
