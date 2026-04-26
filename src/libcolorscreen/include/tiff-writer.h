/* TIFF writer for libcolorscreen.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef TIFFWRITER_H
#define TIFFWRITER_H
#include <tiffio.h>
#include <memory>
#include "base.h"
#include "color.h"
#include "dllpublic.h"

namespace colorscreen {
class progress_info;

/** Convert a 32-bit float F to a 16-bit half-precision float.
    This is used for HDR TIFF output when 16-bit depth is requested.  */
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
        return (uint16_t)lsign;
      mantissa = (mantissa | 0x00800000) >> (1 - exponent);
      if (mantissa & 0x00001000)
        mantissa += 0x00002000;
      return (uint16_t)(lsign | (mantissa >> 13));
    }
  else if (exponent == 0xff - (127 - 15))
    {
      if (mantissa == 0)
        return (uint16_t)(lsign | 0x7c00);
      else
        return (uint16_t)(lsign | 0x7c00 | (mantissa >> 13));
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
    return (uint16_t)(lsign | 0x7c00); // infinity with the same sign as f.
  return (uint16_t)(lsign | (exponent << 10) | (mantissa >> 13));
}

/** Parameters for tiff_writer class.  */
struct tiff_writer_params
{
  /* Fields that must be always filled in.  */
  const char *filename = nullptr;
  int width = 0;
  int height = 0;
  /* Depth will always default to 16bit.  */
  int depth = 16;
  /* DPI.  0 means unknown.  */
  double xdpi = 0;
  double ydpi = 0;
  /* True if alpha channel should be used.  Defaults to false.  */
  bool alpha = false;
  /* True if HDR should be used.  Defaults to false.  */
  bool hdr = false;
  /* True if we xoffset and yoffset should be stored in file.  */
  bool tile = false;
  /* ICC profile.  nullptr will lead to embedding sRGB.  */
  const void *icc_profile = nullptr;
  size_t icc_profile_len = 0;
  /* Offset of the tile.  */
  int xoffset = 0;
  int yoffset = 0;
  bool dng = false;
  color_matrix dye_to_xyz;
  int black = 0;
  /* True if parallelism is done.  In this case multiple rows will be
     written at once.  */
  bool parallel = false;

  tiff_writer_params () = default;
};

/** Class to write TIFF files, possibly in parallel.  */
class tiff_writer
{
public:
  /** Initialize TIFF writer with parameters P.
      If error occurs, ERROR is set to a descriptive string.  */
  DLL_PUBLIC tiff_writer (tiff_writer_params &p, const char **error);
  DLL_PUBLIC ~tiff_writer ();

  /** Write current row to the file.  Returns true on success.  */
  DLL_PUBLIC bool write_row ();

  /** Write multiple rows to the file, reporting progress via PROGRESS.
      Returns true on success.  */
  DLL_PUBLIC bool write_rows (progress_info *progress = nullptr);

  /** Return number of rows in the buffer.  */
  int
  get_n_rows () const
  {
    return n_rows;
  }

  /** Return pointer to 16-bit row data for row N.  */
  uint16_t *
  row16bit (int n)
  {
    assert (!colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((uint16_t *)outrow.get () + stride * n);
  }

  /** Return pointer to 16-bit row data for the only row in buffer.  */
  uint16_t *
  row16bit ()
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return (row16bit (0));
  }

  /** Return pointer to 8-bit row data for row N.  */
  uint8_t *
  row8bit (int n)
  {
    assert (!colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((uint8_t *)outrow.get () + stride * n);
  }

  /** Return pointer to 8-bit row data for the only row in buffer.  */
  uint8_t *
  row8bit ()
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return (row8bit (0));
  }

  /** Return pointer to 32-bit float row data for row N.  */
  float *
  row32bit_float (int n)
  {
    assert (!colorscreen_checking || ((n >= 0) && (n <= get_n_rows ())));
    return ((float *)outrow.get () + stride * n);
  }

  /** Return pointer to 32-bit float row data for the only row in buffer.  */
  float *
  row32bit_float ()
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return row32bit_float (0);
  }

  /** Put pixel with color R, G, B at position X in ROW.  */
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

  /** Put pixel with color R, G, B at position X in the only row.  */
  void
  put_pixel (int x, int r, int g, int b)
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return (put_pixel (x, 0, r, g, b));
  }

  /** Mark pixel at position X in ROW as transparent.  */
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

  /** Mark pixel at position X in the only row as transparent.  */
  void
  kill_pixel (int x)
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return (kill_pixel (x, 0));
  }

  /** Put HDR pixel with color R, G, B at position X in ROW.  */
  void
  put_hdr_pixel (int x, int row, luminosity_t r, luminosity_t g, luminosity_t b)
  {
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

  /** Put HDR pixel with color R, G, B at position X in the only row.  */
  void
  put_hdr_pixel (int x, luminosity_t r, luminosity_t g, luminosity_t b)
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
    return (put_hdr_pixel (x, 0, r, g, b));
  }

  /** Mark HDR pixel at position X in ROW as transparent.  */
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

  /** Mark HDR pixel at position X in the only row as transparent.  */
  void
  kill_hdr_pixel (int x)
  {
    assert (!colorscreen_checking || get_n_rows () == 1);
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
  std::unique_ptr<uint8_t[]> outrow;
  int y;
  int n_rows;
  int stride;
  int bytestride;
  int height;
};
}
#endif
