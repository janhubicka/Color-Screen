#include <cctype>
#include "include/backlight-correction.h"
#include "loadsave.h"
#include "include/tiff-writer.h"
backlight_correction *
backlight_correction::analyze_scan (image_data &scan, luminosity_t gamma)
{
  const int width = 111;
  const int height = 84;
  bool enabled[4] = {0,0,0,0};
  if (scan.data)
    enabled[(int)ir] = true;
  if (scan.rgbdata)
    enabled[(int)red] = enabled[(int)green] = enabled[(int)blue] = true;
  backlight_correction *ret = new backlight_correction ();
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
      for (int yy = ystart; yy < ystart+ysize; yy++)
        for (int xx = xstart; xx < xstart+xsize; xx++)
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
	    int len = xsize * ysize;
	    luminosity_t sum = 0;
	    for (int j = len / 4; j < 3 * len /4; j++)
	      sum += table[values[i][j]];
	    sum /= len / 2;
	    ret->set_weight (x, y, 1/sum, 0, (channel)i);
	  }
    }
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
  return ret;
}
bool
backlight_correction::save (FILE *f)
{
  if (fprintf (f, "  backlight_correction_dimensions: %i %i\n", m_width, m_height) < 0)
    return false;
  if (fprintf (f, "  backlight_correction_channels:%s%s%s%s\n", m_channel_enabled[0]?" red":"", m_channel_enabled[1]?" green":"", m_channel_enabled[2]?" blue":"", m_channel_enabled[3]?" ir":"") < 0)
    return false;
  if (fprintf (f, "  backlight_correction_data:" ) < 0)
    return false;
  for (int y = 0; y < m_height; y++)
    {
      if (y)
        fprintf (f, "\n                             ");
      for (int x = 0; x < m_width; x++)
	for (int i = 0; i < 4; i++)
	  if (m_channel_enabled[i])
	    if (fprintf (f, " (%f, %f)", m_weights[y * m_width +x].add[i],  m_weights[y * m_width +x].mult[i])<0)
	      return false;
    }
  if (fprintf (f, "\n  backlight_correction_end\n" ) < 0)
    return false;
  return true;
}
const char *
backlight_correction::save_tiff (const char *filename)
{
  tiff_writer_params tp;
  tp.filename = filename;
  tp.width = m_width;
  tp.height = m_height;
  tp.hdr = true;
  tp.depth = 32;
  const char *error;
  tiff_writer out(tp, &error);
  if (error)
    return error;
  for (int y = 0; y < m_height; y++)
    {
	      printf ("%i\n",y);
      for (int x = 0; x < m_width; x++)
      {
        out.put_hdr_pixel (x, m_weights[y * m_width +x].mult[0] * 0.5,
			      m_weights[y * m_width +x].mult[1] * 0.5,
			      m_weights[y * m_width +x].mult[2] * 0.5);
      }
      if (!out.write_row ())
	return "Write error";
    }
  return NULL;
}
bool
backlight_correction::load (FILE *f, const char **error)
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
   bool enabled [4] = {0, 0, 0, 0};
   if (!expect_keyword (f, "backlight_correction_channels:"))
     {
       *error = "expected backlight_correction_dimensions";
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
	 if (getc (f) == 'r' && getc (f) == 'e' && getc (f) == 'e' && getc(f) == 'n')
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
	 *error = "failed to parse a channel of backlight_correction_channels";
	 return false;
       }
   }
   if (!expect_keyword (f, "backlight_correction_data:"))
     {
       *error = "expected backlight_correction_data";
       return false;
     }
  alloc (width, height, enabled);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	for (int i = 0; i < 4; i++)
	  if (m_channel_enabled[i])
	    {
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
	    }
    }
   if (!expect_keyword (f, "backlight_correction_end"))
     {
       *error = "expected backlight_correction_end";
       return false;
     }
   return true;
}
