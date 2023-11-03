#include <stdlib.h>
#include "include/tiff-writer.h"
extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;
tiff_writer::tiff_writer (tiff_writer_params &p, const char **error)
{
  *error = NULL;
  if (p.hdr)
    {
      if (p.depth == 16)
	pixel_format = pixel_16bit_hdr;
      else if(p.depth == 32)
	pixel_format = pixel_32bit_hdr;
      else
	{
	  *error = "unduported bit depth in HDR tiff file";
	  return;
	}
    }
  else
    {
      if (p.depth == 8)
	pixel_format = pixel_8bit;
      else if (p.depth == 16)
	pixel_format = pixel_16bit;
      else
	{
	  *error = "unduported bit depth in HDR tiff file";
	  return;
	}
    }
  out = TIFFOpen (p.filename, "wb");
  outrow = 0;
  y = 0;
  
  if (!out)
    {
      *error = "can not open tiff file";
      return;
    }
  static uint16_t extras[] = {EXTRASAMPLE_UNASSALPHA};
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, (uint32_t) p.width)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, (uint32_t) p.height)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, p.alpha ? 4 : 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, p.depth)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, p.hdr ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || (p.alpha && !TIFFSetField (out, TIFFTAG_EXTRASAMPLES, 1, extras))
      || !TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_LZW)
      || !TIFFSetField (out, TIFFTAG_ICCPROFILE, p.icc_profile ? (uint32_t)p.icc_profile_len : (uint32_t) sRGB_icc_len, p.icc_profile ? p.icc_profile : sRGB_icc))
    {
      *error = "write error";
      TIFFClose (out);
      out = NULL;
      return;
    }
  outrow = malloc (p.width * (size_t)p.depth * (p.alpha ? 4 : 3) / 8);
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
