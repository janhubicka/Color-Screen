#include <cstring>
#include <assert.h>
#include <cstdlib>
#include <cmath>
#include <tiffio.h>
#include <turbojpeg.h>
#include "lru-cache.h"
#include "include/imagedata.h"

class image_data_loader
{
public:
  virtual bool init_loader (const char *name, const char **error) = 0;
  virtual bool load_part (int *permille, const char **error) = 0;
  virtual ~image_data_loader ()
  { }
  bool grayscale;
  bool rgb;
};

class jpg_image_data_loader: public image_data_loader
{
public:
  jpg_image_data_loader (image_data *img)
  : m_img (img), m_jpeg_buf (NULL), m_tj_instance (NULL), m_img_buf (NULL)
  { }
  virtual bool init_loader (const char *name, const char **error);
  virtual bool load_part (int *permille, const char **error);
  virtual ~jpg_image_data_loader ()
  {
    if (m_jpeg_buf)
      tjFree (m_jpeg_buf);
    if (m_img_buf)
      tjFree (m_img_buf);
    if (m_tj_instance)
      tjDestroy(m_tj_instance);
  }
private:
  image_data *m_img;
  unsigned char *m_jpeg_buf;
  tjhandle m_tj_instance;
  unsigned char *m_img_buf;
};

class tiff_image_data_loader: public image_data_loader
{
public:
  tiff_image_data_loader (image_data *img)
  : m_tif (NULL), m_img (img), m_buf (NULL)
  { }
  virtual bool init_loader (const char *name, const char **error);
  virtual bool load_part (int *permille, const char **error);
  virtual ~tiff_image_data_loader ()
  {
    if (m_tif)
      TIFFClose (m_tif);
    if (m_buf)
      _TIFFfree(m_buf);
  }
private:
  static const bool debug = false;
  TIFF *m_tif;
  image_data *m_img;
  tdata_t m_buf;
  uint16_t m_bitspersample;
  uint16_t m_samples;
  uint32_t m_row;
};

image_data::image_data ()
: data (NULL), rgbdata (NULL), icc_profile (NULL), width (0), height (0), maxval (0), icc_profile_size (0), id (lru_caches::get ()), loader (NULL), own (false)
{ 
}

image_data::~image_data ()
{
  if (loader)
    delete loader;
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
image_data::allocate_grayscale ()
{
  assert (loader != NULL);
  return loader->grayscale;
}

bool
image_data::allocate_rgb ()
{
  assert (loader != NULL);
  return loader->rgb;
}

bool
image_data::allocate ()
{
  if (allocate_grayscale ())
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
    }
  if (allocate_rgb ())
    {
      rgbdata = (pixel **)malloc (sizeof (*rgbdata) * height);
      if (!rgbdata)
	{
	  free (*data);
	  if (data)
	    free (data);
	  data = NULL;
	  return false;
	}
      rgbdata[0] = (pixel *)calloc (width * height, sizeof (**rgbdata));
      if (!rgbdata [0])
	{
	  free (*data);
	  if (data)
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

/* Silence warnings.
   They happen commonly while loading scans.  */
static
void warning_handler(const char* module, const char* fmt, va_list ap)
{
}

bool
tiff_image_data_loader::init_loader (const char *name, const char **error)
{
  if (debug)
    printf("TIFFopen\n");
  TIFFSetWarningHandler (warning_handler);
  m_tif = TIFFOpen(name, "r");
  if (!m_tif)
    {
      *error = "can not open file";
      return false;
    }
  uint32_t w, h;
  uint16_t photometric;
  if (debug)
    printf("checking width/height\n");
  TIFFGetField(m_tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(m_tif, TIFFTAG_IMAGELENGTH, &h);
  if (debug)
    printf("checking bits per sample\n");
  TIFFGetFieldDefaulted(m_tif, TIFFTAG_BITSPERSAMPLE, &m_bitspersample);
  if (m_bitspersample != 8 && m_bitspersample != 16)
    {
      *error = "bit depth should be 8 or 16";
      return false;
    }
  if (debug)
    printf("checking smaples per pixel\n");
  TIFFGetFieldDefaulted(m_tif, TIFFTAG_SAMPLESPERPIXEL, &m_samples);
  void *iccprof;
  uint32_t size;
  if (TIFFGetField (m_tif, TIFFTAG_ICCPROFILE, &size, &iccprof))
    {
      //m_img->icc_profile = iccprof;
      //m_img->icc_profile_size = size;
      m_img->icc_profile = malloc (size);
      memcpy (m_img->icc_profile, iccprof, size);
      m_img->icc_profile_size = size;
    }
  if (m_samples != 1 && m_samples != 3 && m_samples != 4)
    {
      if (debug)
	printf("Samples:%i\n", m_samples);
      *error = "only 1 sample per pixel (grayscale), 3 samples per pixel (RGB) or 4 samples per pixel (RGBa) are supported";
      return false;
    }
  if (TIFFGetField(m_tif, TIFFTAG_PHOTOMETRIC, &photometric))
    {
      if (photometric == PHOTOMETRIC_MINISBLACK && m_samples == 1)
	;
      else if (photometric == PHOTOMETRIC_RGB && (m_samples == 4 || m_samples == 3))
        ;
      else 
	{
	  *error = "only RGB, RGBa or grayscale images are suppored";
	  return false;
	}
    }
  if (debug)
    printf("Getting scanlinel\n");
  m_buf = _TIFFmalloc(TIFFScanlineSize(m_tif));
  if (!m_buf)
    {
      *error = "out of memory allocating tiff scanline";
      return false;
    }
  m_img->width = w;
  m_img->height = h;
  grayscale = m_samples == 4 || m_samples == 1;
  rgb = m_samples != 1;
  if (m_bitspersample == 8)
    m_img->maxval = 255;
  else if (m_bitspersample == 16)
    m_img->maxval = 65535;
  else
    {
      *error = "Only bith depths 8 and 16 are supported";
      return false;
    }
  m_row = 0;
  return true;
}

bool
tiff_image_data_loader::load_part (int *permille, const char **error)
{
  if ((int)m_row < m_img->height)
    {
      uint32_t row = m_row;
      uint32_t w = m_img->width;
      image_data::gray **data = m_img->data;
      image_data::pixel **rgbdata = m_img->rgbdata;

      if (debug)
	printf("Decoding scanline %i\n", row);
      if (!TIFFReadScanline(m_tif, m_buf, row))
	{
	  *error = "scanline decoding failed";
	  return false;
	}
      if (m_bitspersample == 8 && m_samples == 1)
	{
	  uint8_t *buf2 = (uint8_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    data[row][x] = buf2[x];
	}
      else if (m_bitspersample == 8 && m_samples == 3)
	{
	  uint8_t *buf2 = (uint8_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[3 * x+0];
	      rgbdata[row][x].g = buf2[3 * x+1];
	      rgbdata[row][x].b = buf2[3 * x+2];
	    }
	}
      else if (m_bitspersample == 8 && m_samples == 4)
	{
	  uint8_t *buf2 = (uint8_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[4 * x+0];
	      rgbdata[row][x].g = buf2[4 * x+1];
	      rgbdata[row][x].b = buf2[4 * x+2];
	      data[row][x] = buf2[4 * x+3];
	    }
	}
      else if (m_bitspersample == 16 && m_samples == 1)
	{
	  uint16_t *buf2 = (uint16_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    data[row][x] = buf2[x];
	}
      else if (m_bitspersample == 16 && m_samples == 3)
	{
	  uint16_t *buf2 = (uint16_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[3 * x+0];
	      rgbdata[row][x].g = buf2[3 * x+1];
	      rgbdata[row][x].b = buf2[3 * x+2];
	    }
	}
      else if (m_bitspersample == 16 && m_samples == 4)
	{
	  uint16_t *buf2 = (uint16_t *)m_buf;
	  for (uint32_t x = 0; x < w; x++)
	    {
	      rgbdata[row][x].r = buf2[4*x+0];
	      rgbdata[row][x].g = buf2[4*x+1];
	      rgbdata[row][x].b = buf2[4*x+2];
	      data[row][x] = buf2[4*x+3];
	    }
	}
      else
	{
	  /* We should have given up earlier.  */
	  fprintf (stderr, "Wrong combinations of bitspersample %i and samples %i\n",
		   m_bitspersample, m_samples);
	  abort ();
	}
      *permille = (999 * m_row + m_img->height / 2) / m_img->height;
      m_row++;
    }
  else
    {
      if (debug)
	printf("done\n");
      *permille = 1000;
    }
  return true;
}

bool
jpg_image_data_loader::init_loader (const char *name, const char **error)
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
  if ((m_jpeg_buf = (unsigned char *)tjAlloc(jpegSize)) == NULL)
    {
      *error = "input file is empty";
      fclose (jpegFile);
      return false;
    }
  if (fread(m_jpeg_buf, jpegSize, 1, jpegFile) < 1)
    {
      *error = "can not read file";
      fclose (jpegFile);
      return false;
    }
  fclose(jpegFile);  
  m_tj_instance = tjInitDecompress ();
  if (!m_tj_instance)
    {
      *error = "can not initialize jpeg decompressor";
      return false;
    }
  int inSubsamp, inColorspace;
  if (tjDecompressHeader3(m_tj_instance, m_jpeg_buf, jpegSize, &m_img->width, &m_img->height,
			  &inSubsamp, &inColorspace) < 0)
    {
      *error = "can not read header";
      return false;
    }
  /* RGB is 1 and gray is 2.  */
  if (inColorspace != 1 && inColorspace != 2)
    {
      *error = "only grayscale and rgb jpeg files are supported";
      return false;
    }
  rgb = inColorspace == 1;
  int pixelFormat = rgb ? TJPF_RGB : TJPF_GRAY;
  m_img_buf = (unsigned char *)tjAlloc(m_img->width * m_img->height * tjPixelSize[pixelFormat]);
  if (!m_img_buf)
    {
      *error = "can not allocate decompressed image buffer";
      return false;
    }
  if (tjDecompress2(m_tj_instance, m_jpeg_buf, jpegSize, m_img_buf, m_img->width, 0, m_img->height,
		    pixelFormat, TJFLAG_ACCURATEDCT) < 0)
    {
      *error = "can not allocate decompressed image buffer";
      return false;
    }
  free (m_jpeg_buf);
  m_jpeg_buf = NULL;
  tjDestroy(m_tj_instance);
  m_tj_instance = NULL;
  grayscale = !rgb;
  m_img->maxval = 255;
  return true;
}

bool
jpg_image_data_loader::load_part (int *permille, const char **error)
{
  int width = m_img->width;
  int height = m_img->height;
  image_data::gray **data = m_img->data;
  image_data::pixel **rgbdata = m_img->rgbdata;

  if (!rgb)
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++)
	data[y][x] = m_img_buf[y * width + x];
  else
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++)
	{
	  rgbdata[y][x].r = m_img_buf[y * width *3 + x * 3 + 0];
	  rgbdata[y][x].g = m_img_buf[y * width *3 + x * 3 + 1];
	  rgbdata[y][x].b = m_img_buf[y * width *3 + x * 3 + 2];
	}
  *permille = 1000;
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
image_data::init_loader (const char *name, const char **error)
{
  assert (!loader);
  if (has_suffix (name, ".tif") || has_suffix (name, ".tiff"))
    loader = new tiff_image_data_loader (this);
  else if (has_suffix (name, ".jpg") || has_suffix (name, ".jpeg"))
    loader = new jpg_image_data_loader (this);
  if (!loader)
    {
      *error = "Unknown file extension";
      return false;
    }
  bool ret = loader->init_loader (name, error);
  if (!ret)
    {
      delete loader;
      loader = NULL;
    }
  return ret;
}

bool
image_data::load_part (int *permille, const char **error)
{
  assert (loader);
  bool ret = loader->load_part (permille, error);
  if (!ret || *permille == 1000)
    {
      delete loader;
      loader = NULL;
    }
  return ret;
}

bool
image_data::load (const char *name, const char **error, progress_info *progress)
{
  int permille;
  if (progress)
    progress->set_task ("loading image header",1);
  if (!init_loader (name, error))
    return false;

  if (progress)
    progress->set_task ("allocating memory",1);
  if (!allocate ())
    {
      *error = "out of memory allocating image";
      delete loader;
      loader = NULL;
      return false;
    }

  if (progress)
    progress->set_task ("loading",1000);
  while (load_part (&permille, error))
    {
      if (permille == 1000)
	return true;
      if (progress)
	progress->set_progress (permille);
      if (progress && progress->cancel_requested ())
	{
	  *error = "cancelled";
	  return false;
	}
    }
  return false;
}
