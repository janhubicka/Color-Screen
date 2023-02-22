#include <stdlib.h>
#include "include/tiff-writer.h"
extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;
tiff_writer::tiff_writer (const char *filename, int width, int height, int depth, bool alpha, const char **error)
{
  *error = NULL;
  out = TIFFOpen (filename, "wb");
  outrow = 0;
  y = 0;
  
  if (!out)
    {
      *error = "can not open tiff file";
      return;
    }
  static uint16_t extras[] = {EXTRASAMPLE_UNASSALPHA};
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, (uint32_t) width)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, (uint32_t) height)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, alpha ? 4 : 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, depth)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || (alpha && !TIFFSetField (out, TIFFTAG_EXTRASAMPLES, 1, extras))
      || !TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_LZW)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, (uint32_t) sRGB_icc_len, sRGB_icc))
    {
      *error = "write error";
      TIFFClose (out);
      out = NULL;
      return;
    }
  outrow = malloc (width * (size_t)depth * (alpha ? 4 : 3) / 8);
  if (!outrow)
    {
      *error = "Out of memory allocating output buffer";
      TIFFClose (out);
      out = NULL;
      return;
    }
}
bool
tiff_writer::write_row ()
{
  if (TIFFWriteScanline (out, outrow, y++, 0) < 0)
    {
      return false;
    }
  return true;
}
tiff_writer::~tiff_writer()
{
   if (out)
     TIFFClose (out);
   out = NULL;
   free (outrow);
   outrow = NULL;
}
