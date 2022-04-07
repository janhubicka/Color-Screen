#include <cstring>
#include <cstdlib>
#include <tiffio.h>
#include <turbojpeg.h>
#include "include/imagedata.h"

image_data::~image_data ()
{
  if (!own)
    return;
  if (data)
    {
      free (*data);
      free (data);
    }
  if (rgbdata)
    {
      free (*rgbdata);
      free (rgbdata);
    }
}

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
      if (!rgbdata)
	{
	  free (*data);
	  free (data);
	  data = NULL;
	  return false;
	}
      rgbdata[0] = (pixel *)calloc (width * height, sizeof (**rgbdata));
      if (!rgbdata [0])
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
	  for (int x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[4 * x+0];
	      rgbdata[row][x].g = buf2[4 * x+1];
	      rgbdata[row][x].b = buf2[4 * x+2];
	      data[row][x] = buf2[4 * x+3];
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
	  for (int x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[4*x+0];
	      rgbdata[row][x].g = buf2[4*x+1];
	      rgbdata[row][x].b = buf2[4*x+2];
	      data[row][x] = buf2[4*x+3];
	    }
	}
      else
	abort ();
    }
  _TIFFfree(buf);
  TIFFClose (tif);
  return true;
}
bool
image_data::load_jpg (const char *name, const char **error)
{
  FILE *jpegFile;
  if ((jpegFile = fopen(name, "rb")) == NULL)
    {
      *error = "can not open file";
      return false;
    }
  size_t size;
  if (fseek(jpegFile, 0, SEEK_END) < 0 || ((size = ftell(jpegFile)) < 0) ||
      fseek(jpegFile, 0, SEEK_SET) < 0)
    {
      *error = "can not determine input file size";
      fclose (jpegFile);
      return false;
    }
  if (size == 0)
    {
      *error = "input file is empty";
      fclose (jpegFile);
      return false;
    }
  unsigned long jpegSize = (unsigned long)size;
  unsigned char *jpegBuf = NULL;
  if ((jpegBuf = (unsigned char *)tjAlloc(jpegSize)) == NULL)
    {
      *error = "input file is empty";
      fclose (jpegFile);
      return false;
    }
  if (fread(jpegBuf, jpegSize, 1, jpegFile) < 1)
    {
      *error = "can not read file";
      fclose (jpegFile);
      tjFree(jpegBuf);
      return false;
    }
  fclose(jpegFile);  
  tjhandle tjInstance = tjInitDecompress();
  if (!tjInstance)
    {
      *error = "can not initialize jpeg decompressor";
      tjFree(jpegBuf);
      return false;
    }
  int inSubsamp, inColorspace;
  if (tjDecompressHeader3(tjInstance, jpegBuf, jpegSize, &width, &height,
			  &inSubsamp, &inColorspace) < 0)
    {
      *error = "can not read header";
      tjFree(jpegBuf);
      return false;
    }
  /* RGB is 0 and gray is 2.  */
  if (inColorspace != 1 && inColorspace != 2)
    {
      *error = "only grayscale and rgb jpeg files are supported";
      tjFree(jpegBuf);
      tjDestroy(tjInstance);
      return false;
    }
  int pixelFormat = TJPF_GRAY;
  unsigned char *imgBuf = (unsigned char *)tjAlloc(width * height *
                                           tjPixelSize[pixelFormat]);
  if (!imgBuf)
    {
      *error = "can not allocate decompressed image buffer";
      tjFree(jpegBuf);
      tjDestroy(tjInstance);
      return false;
    }
  if (tjDecompress2(tjInstance, jpegBuf, jpegSize, imgBuf, width, 0, height,
		    pixelFormat, TJFLAG_ACCURATEDCT) < 0)
    {
      *error = "can not allocate decompressed image buffer";
      tjFree (jpegBuf);
      tjFree (imgBuf);
      tjDestroy(tjInstance);
      return false;
    }
  free (jpegBuf);
  tjDestroy(tjInstance);
  if (!allocate (false))
    {
      *error = "out of memory allocating image";
      tjFree (imgBuf);
      return false;
    }
  maxval = 255;
  for (uint32_t y = 0; y < height; y++)
    for (uint32_t x = 0; x < width; x++)
      data[y][x] = imgBuf[y * width + x];
  tjFree (imgBuf);
  return true;
}

static bool
has_suffix (const char *name, const char *suffix)
{
  int l1 = strlen (name), l2 = strlen (suffix);
  if (l1 < l2)
    return false;
  return !strcmp (suffix, name + l1 - l2);
}

bool
image_data::load (const char *name, const char **error)
{
  if (has_suffix (name, ".tif") || has_suffix (name, ".tiff"))
    return load_tiff (name, error);
  else if (has_suffix (name, ".jpg") || has_suffix (name, ".jpeg"))
    return load_jpg (name, error);
  else
    {
      *error = "only files with extensions tif, tiff, jpg or jpeg are supported";
      return false;
    }
}
