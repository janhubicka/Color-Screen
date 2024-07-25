#ifndef TIFFWRITER_H
#define TIFFWRITER_H
#include <tiffio.h>
#include "base.h"
#include "color.h"
#include "dllpublic.h"
inline uint16_t
float_to_half (float f)
{
  union
  {
    float f;
    uint32_t i;
  } tmp;

  tmp.f = f;
  const int32_t lsign = (tmp.i >> 16) & 0x00008000;
  int32_t exponent = ((tmp.i >> 23) & 0x000000ff) - (127 - 15);
  int32_t mantissa = tmp.i & 0x007fffff;
  if (exponent <= 0)
    {
      if (exponent < -10)
        {
          return (uint16_t)lsign;
        }
      mantissa = (mantissa | 0x00800000) >> (1 - exponent);
      if (mantissa & 0x00001000)
        mantissa += 0x00002000;
      return (uint16_t)(lsign | (mantissa >> 13));
    }
  else if (exponent == 0xff - (127 - 15))
    {
      if (mantissa == 0)
        {
          return (uint16_t)(lsign | 0x7c00);
        }
      else
        {
          return (uint16_t)(lsign | 0x7c00 | (mantissa >> 13));
        }
    }
  if (mantissa & 0x00001000)
    {
      mantissa += 0x00002000;
      if (mantissa & 0x00800000)
        {
          mantissa = 0;  // overflow in significand,
          exponent += 1; // adjust exponent
        }
    }
  if (exponent > 30)
    {
      return (uint16_t)(lsign | 0x7c00); // infinity with the same sign as f.
    }
  return (uint16_t)(lsign | (exponent << 10) | (mantissa >> 13));
}

struct tiff_writer_params
{
  /* Fields that must be always filled in.  */
  const char *filename;
  int width, height;
  /* Depth will always default to 16bit.  */
  int depth;
  /* DPI.  0 means unknown.  */
  double xdpi, ydpi;
  /* True if alpha channel should be used.  Defaults to false.  */
  bool alpha;
  /* True if HDR should be used.  Defaults to false.  */
  bool hdr;
  /* True if we xoffset and yoffset should be stored in file.  */
  bool tile;
  /* ICC profile.  NULL will lead to embedding sRGB.  */
  void *icc_profile;
  size_t icc_profile_len;
  /* Offset of the tile.  */
  int xoffset, yoffset;
  bool dng;
  color_matrix dye_to_xyz;
  int black;
  /* True if parlallelism is done.  In this case multiple rows will be
     written at once.  */
  bool parallel;
  tiff_writer_params ()
      : filename (NULL), width (0), height (0), depth (16), xdpi (0), ydpi (0),
        alpha (false), hdr (false), tile (false), icc_profile (NULL),
        icc_profile_len (0), dng (false), black (0), parallel (false)
  {
  }
};
class tiff_writer
{
public:
  DLL_PUBLIC tiff_writer (tiff_writer_params &p, const char **error);
  DLL_PUBLIC ~tiff_writer ();
  DLL_PUBLIC bool write_row ();
  DLL_PUBLIC bool write_rows ();
  int
  get_n_rows ()
  {
    return n_rows;
  }

  /* API exists in two forms.  For users which sets parallel == false
     we do not need to know row number since there is always 1.
     For users supporting prallelism we have extra paraleter specifying
     row.  */
  uint16_t *
  row16bit (int n)
  {
    assert (colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((uint16_t *)outrow + stride * n);
  }
  uint16_t *
  row16bit ()
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (row16bit (0));
  }
  uint8_t *
  row8bit (int n)
  {
    assert (colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((uint8_t *)outrow + stride * n);
  }
  uint8_t *
  row8bit ()
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (row8bit (0));
  }
  float *
  row32bit_float (int n)
  {
    assert (colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((float *)outrow + stride * n);
  }
  float *
  row32bit_float ()
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return row32bit_float (0);
  }
  void
  put_pixel (int x, int row, int r, int g, int b)
  {
    if (pixel_format == pixel_8bit)
      {
        row8bit (row)[3 * x] = r;
        row8bit (row)[3 * x + 1] = g;
        row8bit (row)[3 * x + 2] = b;
      }
    else if (pixel_format == pixel_8bit_alpha)
      {
        row8bit (row)[4 * x] = r;
        row8bit (row)[4 * x + 1] = g;
        row8bit (row)[4 * x + 2] = b;
        row8bit (row)[4 * x + 3] = 255;
      }
    else if (pixel_format == pixel_16bit)
      {
        row16bit (row)[3 * x] = r;
        row16bit (row)[3 * x + 1] = g;
        row16bit (row)[3 * x + 2] = b;
      }
    else if (pixel_format == pixel_16bit_alpha)
      {
        row16bit (row)[4 * x] = r;
        row16bit (row)[4 * x + 1] = g;
        row16bit (row)[4 * x + 2] = b;
        row16bit (row)[4 * x + 3] = 65535;
      }
    else
      abort ();
  }
  void
  put_pixel (int x, int r, int g, int b)
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (put_pixel (x, 0, r, g, b));
  }
  void
  kill_pixel (int x, int row)
  {
    if (pixel_format == pixel_8bit_alpha)
      {
        row8bit (row)[4 * x] = 0;
        row8bit (row)[4 * x + 1] = 0;
        row8bit (row)[4 * x + 2] = 0;
        row8bit (row)[4 * x + 3] = 0;
      }
    if (pixel_format == pixel_16bit_alpha)
      {
        row16bit (row)[4 * x] = 0;
        row16bit (row)[4 * x + 1] = 0;
        row16bit (row)[4 * x + 2] = 0;
        row16bit (row)[4 * x + 3] = 0;
      }
    else
      abort ();
  }
  void
  kill_pixel (int x)
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (kill_pixel (x, 0));
  }
  void
  put_hdr_pixel (int x, int row, luminosity_t r, luminosity_t g, luminosity_t b)
  {
#if 0
      if (r < 0)
	      r = 0;
      if (g < 0)
	      g = 0;
      if (b < 0)
	      b = 0;
      if (g > 1)
	      g = 1;
      if (r > 1)
	      r = 1;
      if (b > 1)
	      b = 1;
      printf ("Pixel %f %f %f\n",r,g,b);
#endif
    if (pixel_format == pixel_32bit_hdr)
      {
        row32bit_float (row)[3 * x] = r;
        row32bit_float (row)[3 * x + 1] = g;
        row32bit_float (row)[3 * x + 2] = b;
      }
    else if (pixel_format == pixel_32bit_hdr_alpha)
      {
        row32bit_float (row)[4 * x] = r;
        row32bit_float (row)[4 * x + 1] = g;
        row32bit_float (row)[4 * x + 2] = b;
        row32bit_float (row)[4 * x + 3] = 1;
      }
    else if (pixel_format == pixel_16bit_hdr)
      {
        row16bit (row)[3 * x] = float_to_half (r);
        row16bit (row)[3 * x + 1] = float_to_half (g);
        row16bit (row)[3 * x + 2] = float_to_half (b);
      }
    else if (pixel_format == pixel_16bit_hdr_alpha)
      {
        row16bit (row)[4 * x] = float_to_half (r);
        row16bit (row)[4 * x + 1] = float_to_half (g);
        row16bit (row)[4 * x + 2] = float_to_half (b);
        row16bit (row)[4 * x + 3] = float_to_half (1);
      }
    else
      abort ();
  }
  void
  put_hdr_pixel (int x, luminosity_t r, luminosity_t g, luminosity_t b)
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (put_hdr_pixel (x, 0, r, g, b));
  }
  void
  kill_hdr_pixel (int x, int row)
  {
    if (pixel_format == pixel_32bit_hdr_alpha)
      {
        row32bit_float (row)[4 * x] = 0;
        row32bit_float (row)[4 * x + 1] = 0;
        row32bit_float (row)[4 * x + 2] = 0;
        row32bit_float (row)[4 * x + 3] = 0;
      }
    else if (pixel_format == pixel_16bit_hdr_alpha)
      {
        row16bit (row)[4 * x] = float_to_half (0);
        row16bit (row)[4 * x + 1] = float_to_half (0);
        row16bit (row)[4 * x + 2] = float_to_half (0);
        row16bit (row)[4 * x + 3] = float_to_half (0);
      }
    else
      abort ();
  }
  void
  kill_hdr_pixel (int x)
  {
    assert (colorscreen_checking || get_n_rows () == 1);
    return (kill_hdr_pixel (x, 0));
  }

private:
  enum pixel_format
  {
    pixel_8bit,
    pixel_8bit_alpha,
    pixel_16bit,
    pixel_16bit_alpha,
    pixel_32bit_hdr,
    pixel_32bit_hdr_alpha,
    pixel_16bit_hdr,
    pixel_16bit_hdr_alpha
  } pixel_format;
  TIFF *out;
  void *outrow;
  int y;
  int n_rows;
  int stride;
  int bytestride;
  int height;
};
#endif
