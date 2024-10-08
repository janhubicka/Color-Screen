#include <cctype>
#include "include/tiff-writer.h"
#include "backlight-correction.h"
#include "loadsave.h"
#include "lru-cache.h"
namespace colorscreen
{
bool
backlight_correction_parameters::alloc (int width, int height, bool enabled[4])
{
  m_luminosities = (entry *)calloc (width * height, sizeof (entry));
  m_width = width;
  m_height = height;
  for (int i = 0; i < 4; i++)
    m_channel_enabled[i] = enabled[i];
  return (m_luminosities != NULL);
}

backlight_correction_parameters::~backlight_correction_parameters ()
{
  if (m_luminosities)
    free (m_luminosities);
}
bool
memory_buffer::load_file (FILE *f)
{
  fseek (f, 0L, SEEK_END);
  len = ftell (f);
  fseek (f, 0L, SEEK_SET);
  data = malloc (len);
  if (!data)
    {
      fclose (f);
      return false;
    }
  size_t len2 = fread (data, 1, len, f);
  if (len2 != len)
    {
      fclose (f);
      return false;
    }
  pos = 0;
  fclose (f);
  return true;
}

backlight_correction_parameters::backlight_correction_parameters ()
    : id (lru_caches::get ()), m_width (0), m_height (0),
      m_luminosities (NULL), m_channel_enabled{ true, true, true, false }
{
}

backlight_correction_parameters *
backlight_correction_parameters::analyze_scan (image_data &scan,
                                               luminosity_t gamma)
{
  const int width = 111;
  const int height = 84;
  bool enabled[4] = { 0, 0, 0, 0 };
  if (scan.data)
    enabled[(int)ir] = true;
  if (scan.rgbdata)
    enabled[(int)red] = enabled[(int)green] = enabled[(int)blue] = true;
  backlight_correction_parameters *ret
      = new backlight_correction_parameters ();
  ret->alloc (width, height, enabled);
  luminosity_t table[65536];
  for (int i = 0; i < scan.maxval; i++)
    table[i] = apply_gamma ((i + (luminosity_t)0.5) / scan.maxval, gamma);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        int xstart = x * scan.width / width;
        int ystart = y * scan.height / height;
        int xsize = scan.width / width;
        int ysize = scan.height / height;
        std::vector<uint16_t> values[4];
        for (int i = 0; i < 4; i++)
          if (enabled[i])
            values[i].reserve (xsize * ysize);
        for (int yy = ystart; yy < ystart + ysize; yy++)
          for (int xx = xstart; xx < xstart + xsize; xx++)
            {
              if (scan.data)
                values[ir].push_back (scan.data[y][x]);
              if (scan.rgbdata)
                {
                  values[red].push_back (scan.rgbdata[yy][xx].r);
                  values[green].push_back (scan.rgbdata[yy][xx].g);
                  values[blue].push_back (scan.rgbdata[yy][xx].b);
                }
            }
        for (int i = 0; i < 4; i++)
          if (enabled[i])
            {
              std::sort (values[i].begin (), values[i].end ());
              size_t len = xsize * (size_t)ysize;
              int n = 0;
              luminosity_t sum = 0;
              for (size_t j = len / 4; j < 3 * len / 4; j++)
                {
                  sum += table[values[i][j]];
                  n++;
                }
              sum /= n;
              ret->set_luminosity (x, y, sum, (channel)i);
            }
      }
#if 0
  luminosity_t sum[4] = {0,0,0,0};
  for (int x = 0; x < width * height; x++)
    for (int i = 0;i < 4; i++)
      sum[i] += ret->m_weights[x].mult[i];
  luminosity_t correct[4];
  for (int i = 0;i < 4; i++)
    if (sum[i])
      correct[i] = width*height / sum[i];
    else
      correct[i] = 1;
  for (int x = 0; x < width * height; x++)
    for (int i = 0;i < 4; i++)
      ret->m_weights[x].mult[i] *= correct[i];
#endif
  return ret;
}
bool
backlight_correction_parameters::save (FILE *f)
{
  if (fprintf (f, "  backlight_correction_dimensions: %i %i\n", m_width,
               m_height)
      < 0)
    return false;
  if (fprintf (f, "  backlight_correction_channels:%s%s%s%s\n",
               m_channel_enabled[0] ? " red" : "",
               m_channel_enabled[1] ? " green" : "",
               m_channel_enabled[2] ? " blue" : "",
               m_channel_enabled[3] ? " ir" : "")
      < 0)
    return false;
  if (fprintf (f, "  backlight_correction_lums:") < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n                             ");
      for (int x = 0; x < m_width; x++)
        for (int i = 0; i < 4; i++)
          if (m_channel_enabled[i])
            if (fprintf (f, " %f", m_luminosities[y * m_width + x].lum[i]) < 0)
              return false;
    }
  if (fprintf (f, "\n  backlight_correction_end\n") < 0)
    return false;
  return true;
}
const char *
backlight_correction_parameters::save_tiff (const char *filename)
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
      printf ("%i\n", y);
      for (int x = 0; x < m_width; x++)
        {
          out.put_hdr_pixel (x, m_luminosities[y * m_width + x].lum[0] * 0.5,
                             m_luminosities[y * m_width + x].lum[1] * 0.5,
                             m_luminosities[y * m_width + x].lum[2] * 0.5);
        }
      if (!out.write_row ())
        return "Write error";
    }
  return NULL;
}
bool
backlight_correction_parameters::load (FILE *f, const char **error)
{
  if (!expect_keyword (f, "backlight_correction_dimensions:"))
    {
      *error = "expected backlight_correction_dimensions";
      return false;
    }
  int width, height;
  if (fscanf (f, "%i %i", &width, &height) != 2)
    {
      *error = "failed to parse backlight_correction_dimensions";
      return false;
    }
  bool enabled[4] = { 0, 0, 0, 0 };
  if (!expect_keyword (f, "backlight_correction_channels:"))
    {
      *error = "expected backlight_correction_channels";
      return false;
    }
  while (true)
    {
      int c = getc (f);
      if (c == EOF)
        {
          *error = "failed to parse backlight_correction_channels";
          return false;
        }
      if (c == '\n')
        break;
      if (c == '\r' || isspace (c))
        continue;
      bool failed = false;
      if (c == 'r')
        {
          if (getc (f) == 'e' && getc (f) == 'd')
            enabled[0] = true;
          else
            failed = true;
        }
      else if (c == 'g')
        {
          if (getc (f) == 'r' && getc (f) == 'e' && getc (f) == 'e'
              && getc (f) == 'n')
            enabled[1] = true;
          else
            failed = true;
        }
      else if (c == 'b')
        {
          if (getc (f) == 'l' && getc (f) == 'u' && getc (f) == 'e')
            enabled[2] = true;
          else
            failed = true;
        }
      else if (c == 'i')
        {
          if (getc (f) == 'r')
            enabled[3] = true;
          else
            failed = true;
        }
      if (failed)
        {
          *error
              = "failed to parse a channel of backlight_correction_channels";
          return false;
        }
    }
  if (!expect_keyword (f, "backlight_correction_lums:"))
    {
      *error = "expected backlight_correction_lums";
      return false;
    }
  alloc (width, height, enabled);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
        for (int i = 0; i < 4; i++)
          if (m_channel_enabled[i])
            {
              float sx;
              if (fscanf (f, "%f", &sx) != 1)
                {
                  *error = "failed to parse backlight correction luminosities";
                  return false;
                }
              m_luminosities[y * m_width + x].lum[i] = sx;
#if 0
	      float sx, sy;
	      if (!expect_keyword (f, "(")
		  || fscanf (f, "%f", &sx) != 1
		  || ! expect_keyword (f, ",")
		  || fscanf (f, "%f", &sy) != 1
		  || ! expect_keyword (f, ")"))
		{
		  *error = "failed to parse backlight correction data";
		  return false;
		}
	      m_weights[y * m_width +x].add[i] = sx;
	      m_weights[y * m_width +x].mult[i] = sy;
#endif
            }
    }
  if (!expect_keyword (f, "backlight_correction_end"))
    {
      *error = "expected backlight_correction_end";
      return false;
    }
  return true;
}
backlight_correction::backlight_correction (
    backlight_correction_parameters &params, int width, int height,
    luminosity_t black, bool white_balance, progress_info *progress)
    : m_params (params),
      /*m_img_width (width), m_img_height (height),*/ m_width (params.m_width),
      m_height (params.m_height),
      m_img_width_rec (params.m_width / (coord_t)width),
      m_img_height_rec (params.m_height / (coord_t)height), m_weights (NULL)
{
  if (progress)
    progress->set_task ("computing backlight correction", 1);
  m_weights = (entry *)malloc (m_width * m_height * sizeof (entry));
  m_black = black;
  const luminosity_t epsilon = 1.0 / 256;

  if (!m_weights)
    return;
  luminosity_t sum[4] = { 0, 0, 0, 0 };
  for (int x = 0; x < m_width * m_height; x++)
    for (int i = 0; i < 4; i++)
      sum[i] += m_params.m_luminosities[x].lum[i] - black;
  if (white_balance)
    sum[0] = sum[1] = sum[2] = (sum[0] + sum[1] + sum[2]) / 3;
  luminosity_t correct[4];
  for (int i = 0; i < 4; i++)
    if (sum[i] > epsilon * (m_width * (int64_t)m_height))
      correct[i] = sum[i] / (m_width * (int64_t)m_height);
    else
      correct[i] = 1;

  for (int x = 0; x < m_width * m_height; x++)
    for (int i = 0; i < 4; i++)
      {
        if ((m_params.m_luminosities[x].lum[i] - black) > epsilon)
          m_weights[x].mult[i]
              = correct[i] / (m_params.m_luminosities[x].lum[i] - black);
        else
          m_weights[x].mult[i] = correct[i];
      }
}
}
