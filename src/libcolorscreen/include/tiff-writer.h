#ifndef TIFFWRITER_H
#include <tiffio.h>
#include "color.h"
inline uint16_t float_to_half(float f)
{
    union {
	float f;
	uint32_t i;
    } tmp;

    tmp.f = f;
    const int32_t lsign = (tmp.i >> 16) & 0x00008000;
    int32_t exponent = ((tmp.i >> 23) & 0x000000ff) - (127 - 15);
    int32_t mantissa = tmp.i & 0x007fffff;
    if (exponent <= 0) {
	if (exponent < -10) {
	    return (uint16_t)lsign;
	}
	mantissa = (mantissa | 0x00800000) >> (1 - exponent);
	if (mantissa &  0x00001000)
	    mantissa += 0x00002000;
	return (uint16_t)(lsign | (mantissa >> 13));
    } else if (exponent == 0xff - (127 - 15)) {
	if (mantissa == 0) {
	    return (uint16_t)(lsign | 0x7c00);
	} else {
	    return (uint16_t)(lsign | 0x7c00 | (mantissa >> 13));
	}
    }
    if (mantissa & 0x00001000) {
	mantissa += 0x00002000;
	if (mantissa & 0x00800000) {
	    mantissa = 0;           // overflow in significand,
	    exponent += 1;          // adjust exponent
	}
    }
    if (exponent > 30) {
	return (uint16_t)(lsign | 0x7c00); // infinity with the same sign as f.
    }
    return (uint16_t)(lsign | (exponent << 10) | (mantissa >> 13));
}


struct tiff_writer_params
{
  const char *filename;
  int width;
  int height;
  int depth;
  bool alpha;
  bool hdr;
  void *icc_profile;
  size_t icc_profile_len;
  tiff_writer_params ()
    : filename (NULL), width (0), height (0), depth (16), alpha (false), hdr (false), icc_profile (NULL), icc_profile_len (0)
  {
  }
};
class tiff_writer
{
public:
  tiff_writer (tiff_writer_params &p, const char **error);
  ~tiff_writer ();
  bool write_row ();
  uint16_t *row16bit ()
  {
    return (uint16_t *)outrow;
  }
  uint8_t *row8bit ()
  {
    return (uint8_t *)outrow;
  }
  float *row32bit_float ()
  {
    return (float *)outrow;
  }
  void put_pixel (int x, int r, int g, int b)
    {
      if (pixel_format == pixel_8bit)
        {
	  row8bit ()[3 * x] = r;
	  row8bit ()[3 * x + 1] = g;
	  row8bit ()[3 * x + 2] = b;
        }
      else if (pixel_format == pixel_16bit)
        {
	  row16bit ()[3 * x] = r;
	  row16bit ()[3 * x + 1] = g;
	  row16bit ()[3 * x + 2] = b;
        }
      else
	abort ();
    }
  void put_hdr_pixel (int x, luminosity_t r, luminosity_t g, luminosity_t b)
    {
      if (pixel_format == pixel_32bit_hdr)
        {
	  row32bit_float ()[3 * x] = r;
	  row32bit_float ()[3 * x + 1] = g;
	  row32bit_float ()[3 * x + 2] = b;
        }
      else
	{
	  row16bit ()[3 * x] = float_to_half (r);
	  row16bit ()[3 * x + 1] = float_to_half (g);
	  row16bit ()[3 * x + 2] = float_to_half (b);
	}
    }

private:
  enum pixel_format {
	  pixel_8bit,
	  pixel_16bit,
	  pixel_32bit_hdr,
	  pixel_16bit_hdr 
  } pixel_format;
  TIFF *out;
  void *outrow;
  int y;
};
#endif
