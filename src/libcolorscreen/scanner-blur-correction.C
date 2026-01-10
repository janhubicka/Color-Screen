#include <cctype>
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
      m_corrections (NULL), m_mode (max_correction)
{
}
bool
scanner_blur_correction_parameters::alloc (int width, int height, enum correction_mode mode)
{
  m_corrections = (coord_t *)calloc (width * height, sizeof (luminosity_t));
  m_width = width;
  m_height = height;
  m_mode = mode;
  return (m_corrections != NULL);
}

scanner_blur_correction_parameters::~scanner_blur_correction_parameters ()
{
  if (m_corrections)
    free (m_corrections);
}
bool
scanner_blur_correction_parameters::save (FILE *f)
{
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
bool
scanner_blur_correction_parameters::load (FILE *f, const char **error)
{
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
	  *error = "expected scanner_blur_correction_blur_dimater_pxs";
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
  alloc (width, height, mode);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
        for (int i = 0; i < 4; i++)
            {
              float sx;
              if (fscanf (f, "%f", &sx) != 1)
                {
                  *error = "failed to parse scanner blur correction gaussian blurs";
                  return false;
                }
              m_corrections[y * m_width + x] = sx;
            }
    }
  if (!expect_keyword (f, "scanner_blur_correction_end"))
    {
      *error = "expected scanner_blur_correction_end";
      return false;
    }
  return true;
}
}
