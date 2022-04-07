#include <tiffio.h>
#include "include/imagedata.h"

bool
image_data::allocate (bool rgb)
{
  data = (gray **)malloc (sizeof (*data) * height);
  if (!data)
    return false;
  data[0] = (gray *)calloc (width * height, sizeof (**data));
  if (!data [0])
    {
      free (data);
      data = NULL;
      return false;
    }
  for (int i = 1; i < height; i++)
    data[i] = data[0] + i * width;
  if (rgb)
    {
      rgbdata = (pixel **)malloc (sizeof (*rgbdata) * height);
      if (rgbdata)
	{
	  free (*data);
	  free (data);
	  data = NULL;
	  return false;
	}
      rgbdata[0] = (pixel *)calloc (width * height, sizeof (**rgbdata));
      if (rgbdata [0])
	{
	  free (*data);
	  free (data);
	  data = NULL;
	  free (rgbdata);
	  rgbdata = NULL;
	  return false;
	}
      for (int i = 1; i < height; i++)
	rgbdata[i] = rgbdata[0] + i * width;
    }
  own = true;
  return true;
}
bool
image_data::load_pnm (FILE *grayfile, FILE *colorfile, const char **error)
{
  data = pgm_readpgm (grayfile, &width, &height, &maxval);
  rgbdata = NULL;
  if (!data)
    {
      *error = "failed to read scan";
      return false;
    }
  if (colorfile)
    {
      int rgb_width, rgb_height;
      pixval rgb_maxval;

      rgbdata = ppm_readppm (colorfile, &rgb_width, &rgb_height, &rgb_maxval);
      if (!rgbdata)
	{
	  *error = "failed to read RGB scan";
	  return false;
	}
      if (width != rgb_width || height != rgb_height)
	{
	  *error = "scan and RGB scan must have same dimensions";
	  return false;
	}
      if (maxval != rgb_maxval)
	{
	  *error = "scan and RGB scan must have same bit depth";
	  return false;
	}
    }
  return true;
}

static char emsg[1024];

bool
image_data::load_tiff (const char *name, const char **error)
{
  TIFF* tif = TIFFOpen(name, "r");
  if (!tif)
    {
      *error = "can not open file";
      return false;
    }
  uint32_t imagelength, w, h, bitspersample, photometric, samples;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
  if (bitspersample != 8 && bitspersample != 16)
    {
      *error = "bit depth should be 8 or 16";
      TIFFClose (tif);
      return false;
    }
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples);
  if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric))
    {
      if (photometric == PHOTOMETRIC_MINISBLACK && samples == 1)
	;
      if (photometric != PHOTOMETRIC_RGB && samples == 4)
	{
	  *error = "only RGBa or grayscale images are suppored";
	  TIFFClose (tif);
	  return false;
	}
    }
  printf ("%i %i\n", bitspersample, samples);
  tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
  if (!buf)
    {
      *error = "out of memory allocating tiff scanline";
      TIFFClose (tif);
      return false;
    }
  width = w;
  height = h;
  if (!allocate (samples == 4))
    {
      *error = "out of memory allocating image";
      TIFFClose (tif);
      _TIFFfree(buf);
      return false;
    }
  if (bitspersample == 8)
    maxval = 255;
  else if (bitspersample == 16)
    maxval = 65535;
  else
    abort ();
  for (uint32_t row = 0; row < h; row++)
    {
      if (!TIFFReadScanline(tif, buf, row))
	{
	  *error = "scanline decoding failed";
	  TIFFClose (tif);
	  return false;
	}
      if (bitspersample == 8 && samples == 1)
	{
	  uint8_t *buf2 = (uint8_t *)buf;
	  for (int x = 0; x < w; x++)
	    data[row][x] = buf2[x];
	}
      else if (bitspersample == 8 && samples == 4)
	{
	  uint8_t *buf2 = (uint8_t *)buf;
	  for (int x = 0; x < w; x+=4)
	    {
	      rgbdata[row][x].r = buf2[x+0];
	      rgbdata[row][x].g = buf2[x+1];
	      rgbdata[row][x].b = buf2[x+2];
	      data[row][x] = buf2[x+3];
	    }
	}
      else if (bitspersample == 16 && samples == 1)
	{
	  uint16_t *buf2 = (uint16_t *)buf;
	  for (int x = 0; x < w; x++)
	    data[row][x] = buf2[x];
	}
      else if (bitspersample == 16 && samples == 4)
	{
	  uint16_t *buf2 = (uint16_t *)buf;
	  for (int x = 0; x < w; x+=4)
	    {
	      rgbdata[row][x].r = buf2[x+0];
	      rgbdata[row][x].g = buf2[x+1];
	      rgbdata[row][x].b = buf2[x+2];
	      data[row][x] = buf2[x+3];
	    }
	}
      else
	abort ();
    }
  _TIFFfree(buf);
  TIFFClose (tif);
  return true;
}
