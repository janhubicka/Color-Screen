#include <cctype>
#include "include/tiff-writer.h"
#include "include/scanner-blur-correction-parameters.h"
#include "loadsave.h"
#include "lru-cache.h"
namespace colorscreen
{
scanner_blur_correction_parameters::scanner_blur_correction_parameters ()
    : id (lru_caches::get ()), m_width (0), m_height (0),
      m_gaussian_blurs (NULL)
{
}
bool
scanner_blur_correction_parameters::alloc (int width, int height)
{
  m_gaussian_blurs = (coord_t *)calloc (width * height, sizeof (luminosity_t));
  m_width = width;
  m_height = height;
  return (m_gaussian_blurs != NULL);
}

scanner_blur_correction_parameters::~scanner_blur_correction_parameters ()
{
  if (m_gaussian_blurs)
    free (m_gaussian_blurs);
}
bool
scanner_blur_correction_parameters::save (FILE *f)
{
  if (fprintf (f, "  scanner_blur_correction_dimensions: %i %i\n", m_width,
               m_height)
      < 0)
    return false;
  if (fprintf (f, "  scanner_blur_correction_type: gaussian_blur\n") < 0)
    return false;
  if (fprintf (f, "  scanner_blur_correction_gaussian_blurs:") < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n                             ");
      for (int x = 0; x < m_width; x++)
        for (int i = 0; i < 4; i++)
          if (fprintf (f, " %f", m_gaussian_blurs[y * m_width + x]) < 0)
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
          out.put_hdr_pixel (x, m_gaussian_blurs[y * m_width + x] * 0.5,
                             m_gaussian_blurs[y * m_width + x] * 0.5,
                             m_gaussian_blurs[y * m_width + x] * 0.5);
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
  if (!expect_keyword (f, "scanner_blur_correction_type: gaussian_blur"))
    {
      *error = "expected scanner_blur_correction_type";
      return false;
    }
  if (!expect_keyword (f, "scanner_blur_correction_gaussian_blurs:"))
    {
      *error = "expected scanner_blur_correction_gaussian_blurs";
      return false;
    }
  alloc (width, height);
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
              m_gaussian_blurs[y * m_width + x] = sx;
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
