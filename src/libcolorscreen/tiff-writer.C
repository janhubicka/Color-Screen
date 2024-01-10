#include <stdlib.h>
#include "include/tiff-writer.h"

static const TIFFFieldInfo tiffFields[] = {
	{TIFFTAG_FORWARDMATRIX1, -1, -1, TIFF_SRATIONAL, FIELD_CUSTOM, 1, 1, "ForwardMatrix1"},
	{TIFFTAG_PROFILETONECURVE, -1, -1, TIFF_SRATIONAL, FIELD_CUSTOM, 1, 1, "ToneCurve"},
};
    /* end DNG tags */
extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;
tiff_writer::tiff_writer (tiff_writer_params &p, const char **error)
{
  *error = NULL;
  if (p.hdr)
    {
      if (p.depth == 16)
	pixel_format = !p.alpha ? pixel_16bit_hdr : pixel_16bit_hdr_alpha;
      else if(p.depth == 32)
	pixel_format = !p.alpha ? pixel_32bit_hdr : pixel_16bit_hdr_alpha;
      else
	{
	  *error = "unduported bit depth in HDR tiff file";
	  return;
	}
    }
  else
    {
      if (p.depth == 8)
	pixel_format = !p.alpha ? pixel_8bit : pixel_8bit_alpha;
      else if (p.depth == 16)
	pixel_format = !p.alpha ? pixel_16bit : pixel_16bit_alpha;
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
  int samplesperpixel = p.alpha ? 4 : 3;
  /* We must set some DPI since offset is specified relative to it.  */
  if (p.tile && !p.xdpi)
    p.xdpi = p.ydpi = 300;
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, (uint32_t) p.width)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, (uint32_t) p.height)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, samplesperpixel)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, p.depth)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, p.hdr ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || (p.alpha && !TIFFSetField (out, TIFFTAG_EXTRASAMPLES, 1, extras))
      || !TIFFSetField (out, TIFFTAG_SOFTWARE, "Color-screen")
      || (p.xdpi && !TIFFSetField (out, TIFFTAG_XRESOLUTION, (double)p.xdpi))
      || (p.ydpi && !TIFFSetField (out, TIFFTAG_YRESOLUTION, (double)p.ydpi)))
    {
      *error = "write error";
      TIFFClose (out);
      out = NULL;
      return;
    }
  if (!p.dng)
   {
     if (!TIFFSetField (out, TIFFTAG_ICCPROFILE, p.icc_profile ? (uint32_t)p.icc_profile_len : (uint32_t) sRGB_icc_len, p.icc_profile ? p.icc_profile : sRGB_icc)
         || !TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_LZW))
       {
	 *error = "write error";
	 TIFFClose (out);
	 out = NULL;
	 return;
       }
   }
  if (p.tile)
    {
      if (!TIFFSetField (out, TIFFTAG_XPOSITION, ((double)p.xoffset / p.xdpi))
	  || !TIFFSetField (out, TIFFTAG_YPOSITION, ((double)p.yoffset / p.ydpi))
	  || !TIFFSetField (out, TIFFTAG_PIXAR_IMAGEFULLWIDTH, (long)(p.width + p.xoffset))
	  || !TIFFSetField (out, TIFFTAG_PIXAR_IMAGEFULLLENGTH, (long)(p.height + p.yoffset)))
	{
	  *error = "write error";
	  TIFFClose (out);
	  out = NULL;
	  return;
	}
    }
  if (p.dng)
    {
      TIFFMergeFieldInfo (out, tiffFields, sizeof (tiffFields) / sizeof (TIFFFieldInfo));
      long white[] = {0xffff, 0xffff, 0xffff, 0xffff};
      float forward_matrix[] = {
        (float)p.dye_to_xyz.m_elements[0][0], (float)p.dye_to_xyz.m_elements[1][0], (float)p.dye_to_xyz.m_elements[2][0],
        (float)p.dye_to_xyz.m_elements[0][1], (float)p.dye_to_xyz.m_elements[1][1], (float)p.dye_to_xyz.m_elements[2][1],
        (float)p.dye_to_xyz.m_elements[0][2], (float)p.dye_to_xyz.m_elements[1][2], (float)p.dye_to_xyz.m_elements[2][2]};
      color_matrix m = p.dye_to_xyz.invert ();
      float cam_xyz[] = {
        (float)m.m_elements[0][0], (float)m.m_elements[1][0], (float)m.m_elements[2][0],
        (float)m.m_elements[0][1], (float)m.m_elements[1][1], (float)m.m_elements[2][1],
        (float)m.m_elements[0][2], (float)m.m_elements[1][2], (float)m.m_elements[2][2]};
      ///*= { /*0.807133, 1.0, 0.913289*/ };
      luminosity_t n0, n1, n2;
      //p.dye_to_xyz.apply_to_rgb (1,1,1, &n0, &n1, &n2);
      //float neutral[3] = {(float)n0, (float)n1, (float)n2};
      float neutral[3] = {1,1,1};
      //float neutralxy[2] =
      float tonecurve[] = {0,0,1,1};
      short black[] = {(short)p.black, (short)p.black, (short)p.black, (short)p.black};
      printf ("Black :%i\n", p.black);
      /* TODO: Thumbnail should be subfiletype 1.  */
      if (!TIFFSetField (out, TIFFTAG_SUBFILETYPE, 0)
	  || !TIFFSetField (out, TIFFTAG_DNGVERSION, "\001\004\0\0")
	  || !TIFFSetField (out, TIFFTAG_DNGBACKWARDVERSION, "\001\0\0\0")
	  || !TIFFSetField (out, TIFFTAG_COLORMATRIX1, 9, cam_xyz)
	  || !TIFFSetField (out, TIFFTAG_FORWARDMATRIX1, 9, forward_matrix)
	  || !TIFFSetField (out, TIFFTAG_ASSHOTNEUTRAL, 3, neutral)
	  || !TIFFSetField (out, TIFFTAG_MAKE, "Early color photography")
	  || !TIFFSetField (out, TIFFTAG_MODEL, "Really old camera")
	  || !TIFFSetField (out, TIFFTAG_UNIQUECAMERAMODEL, "Long forgotten")
	  || !TIFFSetField (out, TIFFTAG_CALIBRATIONILLUMINANT1, 21 /*21 is D65, 17 is IL A*/)
	  || !TIFFSetField (out, TIFFTAG_BLACKLEVEL, samplesperpixel, &black)
	  || !TIFFSetField (out, TIFFTAG_WHITELEVEL, samplesperpixel, &white)
	  || !TIFFSetField (out, TIFFTAG_PROFILETONECURVE, 4,tonecurve))
	{
	  *error = "write error";
	  TIFFClose (out);
	  out = NULL;
	  return;
	}
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
      TIFFClose (out);
      out = NULL;
      return false;
    }
  return true;
}
tiff_writer::~tiff_writer()
{
  //progress->set_task ("Closing tile output file", 1);
  if (out)
    {
      TIFFWriteDirectory(out);
      TIFFClose (out);
    }
  out = NULL;
  free (outrow);
  outrow = NULL;
}
