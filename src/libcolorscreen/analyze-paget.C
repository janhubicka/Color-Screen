#include "analyze-base-worker.h"
#include "analyze-paget.h"
#include "screen.h"
#include "include/tiff-writer.h"

/* For stitching we need to write screen with 2 pixels per repetition of screen pattern.  */
bool
analyze_paget::write_screen (const char *filename, bitmap_2d *known_pixels, const char **error, progress_info *progress, luminosity_t rmin, luminosity_t rmax, luminosity_t gmin, luminosity_t gmax, luminosity_t bmin, luminosity_t bmax)
{
  tiff_writer_params p;
  p.filename = filename;
  p.width = 2 * m_width;
  p.height = 2 * m_height;
  p.alpha = true;
  tiff_writer out(p, error);
  if (*error)
    return false;
  if (progress)
    progress->set_task ("Saving screen", m_height * 2);
  luminosity_t rscale = rmax > rmin ? 1/(rmax-rmin) : 1;
  luminosity_t gscale = gmax > gmin ? 1/(gmax-gmin) : 1;
  luminosity_t bscale = bmax > bmin ? 1/(bmax-bmin) : 1;
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	{
	  if (x > 0 && y / 2 > 0 && x < m_width - 1 && y / 2 < m_height - 1
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x-1, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x+1, y / 2)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2-1)
	      && (known_pixels ? known_pixels : m_known_pixels)->test_bit (x, y / 2+1))
	    {
	      luminosity_t red1 = 0, green1 = 0, blue1 = 0;
	      luminosity_t red2 = 0, green2 = 0, blue2 = 0;
	      if (!(y & 1))
	        {
		  red1 = (red (x, y) + red (x+1, y) + red (x, y - 1) + red (x, y + 1)) * 0.25;
		  green1 = green (x, y);
		  red2 = red (x+1, y);
		  green2 = (green (x, y) + green (x+1, y) + green (x, y - 1) + green (x, y + 1)) * 0.25;
	        }
	      else
	        {
		  red1 = red (x, y);
		  green1 = (green (x, y) + green (x-1, y) + green (x, y - 1) + green (x, y + 1)) * 0.25;
		  red2 = (red (x, y) + red (x+1, y) + red (x+1, y - 1) + red (x+1, y + 1)) * 0.25;
		  green2 = green (x, y);
	        }
	      blue1 = (blue (2 * x - 1, y) + blue (2 * x, y) + blue (2 * x, y - 1) + blue (2 * x - 1, y -1)) * 0.25;
	      blue2 = (blue (2 * x, y) + blue (2 * x + 1, y) + blue (2 * x, y - 1) + blue (2 * x + 1, y - 1)) * 0.25;

	      red1 = green1 = blue1 = (red1+green1+blue1) / 3;
	      red2 = green2 = blue2 = (red2+green2+blue2) / 3;
	      out.row16bit ()[2 * x * 4 + 0] = std::max (std::min (linear_to_srgb ((red1 - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 1] = std::max (std::min (linear_to_srgb ((green1 - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 2] = std::max (std::min (linear_to_srgb ((blue1 - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[2 * x * 4 + 3] = 65535;
	      out.row16bit ()[(2 * x + 1) * 4 + 0] = std::max (std::min (linear_to_srgb ((red2 - rmin) * rscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 1] = std::max (std::min (linear_to_srgb ((green2 - gmin) * gscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 2] = std::max (std::min (linear_to_srgb ((blue2 - bmin) * bscale) * 65535.5, 65535.0), 0.0);
	      out.row16bit ()[(2 * x + 1) * 4 + 3] = 65535;
	    }
	  else
	    {
	      out.row16bit ()[2 * x * 4 + 0] = 0;
	      out.row16bit ()[2 * x * 4 + 1] = 0;
	      out.row16bit ()[2 * x * 4 + 2] = 0;
	      out.row16bit ()[2 * x * 4 + 3] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 0] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 1] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 2] = 0;
	      out.row16bit ()[(2 * x + 1) * 4 + 3] = 0;
	    }
	}
      if (!out.write_row ())
	{
	  *error = "Write error";
	  return false;
	}
     if (progress)
       progress->inc_progress ();
    }
  return true;
}
int
analyze_paget::find_best_match (int percentage, int max_percentage, analyze_base &other, int cpfind, coord_t *xshift_ret, coord_t *yshift_ret, int direction, scr_to_img &map, scr_to_img &other_map, FILE *report_file, progress_info *progress)
{
  /* Top left corner of other scan in screen coordinates.  */
  coord_t lx, ly;
  other_map.to_scr (0, 0, &lx, &ly);
  if (cpfind)
    {
      if (find_best_match_using_cpfind (other, xshift_ret, yshift_ret, direction, map, other_map, 2, report_file, progress))
	return true;
    }
  /* TODO: Implement brute forcing.  */
  return false;
}

bool
analyze_paget::dump_patch_density (FILE *out)
{
  fprintf (out, "Paget dimenstion: %i %i\n", m_width, m_height);
  fprintf (out, "Red %i %i\n", m_width , 2*m_height);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", red (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Green %i %i\n", m_width , 2*m_height);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", green (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Blue %i %i\n", m_width * 2, m_height * 2);
  for (int y = 0; y < m_height * 2; y++)
    {
      for (int x = 0; x < m_width * 2; x++)
	fprintf (out, "  %f", blue (x, y));
      fprintf (out, "\n");
    }
  return true;
}
