#include <tiffio.h>
#include "screen-map.h"

/* Write one row.  */
static bool
write_row (TIFF * out, int y, uint16_t * outrow, const char **error, progress_info *progress)
{
  if (progress && progress->cancel_requested ())
    {
      free (outrow);
      TIFFClose (out);
      *error = "Cancelled";
      return false;
    }
  if (TIFFWriteScanline (out, outrow, y, 0) < 0)
    {
      free (outrow);
      TIFFClose (out);
      *error = "Write error";
      return false;
    }
   if (progress)
     progress->inc_progress ();
   return true;
}
bool
screen_map::write_outliers_info (const char *filename, int imgwidth, int imgheight, scr_to_img &map, const char **error, progress_info *progress)
{
  const int scale = 20;
  struct summary {coord_t x, y;};
  int infowidth = imgwidth / scale + 1;
  int infoheight = imgheight / scale + 1;
  struct summary *info = (struct summary *)calloc (infowidth * infoheight, sizeof (struct summary));
  if (!info)
    {
      *error = "out of memory";
      return false;
    }
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (known_p (x - xshift, y - yshift))
        {
	  coord_t ix1, ix2, iy1, iy2;
	  coord_t sx, sy;
	  get_coord (x - xshift, y - yshift, &ix1, &iy1);
	  get_screen_coord (x - xshift, y - yshift, &sx, &sy);
	  map.to_img (sx, sy, &ix2, &iy2);
	  if (ix1 < 0 || ix1 >= imgwidth || iy1 < 0 || iy1 >= imgheight)
		  continue;
	  struct summary &i = info[((int)ix1) / scale + (((int)iy1) / scale) * infowidth];
	  i.x = std::max (i.x, fabs (ix1-ix2)+1);
	  i.y = std::max (i.y, fabs (iy1-iy2)+1);
        }
  TIFF *out = TIFFOpen (filename, "wb");
  if (!out)
    {
      *error = "can not open output file";
      return false;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, infowidth)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, infoheight)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB))
    {
      *error = "write error";
      return false;
    }
  uint16_t *outrow = (uint16_t *) malloc (infowidth * 2 * 3);
  if (!outrow)
    {
      *error = "Out of memory allocating output buffer";
      return false;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving outliers info", infoheight);
    }
  for (int y = 0; y < infoheight; y++)
    {
      for (int x =0 ; x < infowidth; x++)
        {
	  struct summary &i = info[y * infowidth + x];
	  if (i.x < 1)
	    {
	      outrow[x*3] = 0;
	      outrow[x*3+1] = 0;
	      outrow[x*3+2] = 65535;
	    }
	  else
	    {
	      outrow[x*3] = std::min ((i.x-1) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+1] = std::min ((i.y-1) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+2] = 0;
	    }
        }
      const char *error;
      if (!write_row (out, y, outrow, &error, progress))
        {
	  free (outrow);
	  TIFFClose (out);
	  return false;
        }
    }
  free (outrow);
  free (info);
  TIFFClose (out);
  return true;
}
