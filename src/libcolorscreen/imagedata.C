#include <cstring>
#include <assert.h>
#include <cstdlib>
#include <cmath>
#include <tiffio.h>
#include <turbojpeg.h>
#include <zip.h>
#include <lcms2.h>
#include "lru-cache.h"
#include "include/imagedata.h"
#include "include/backlight-correction.h"
#include "include/stitch.h"
#include "mapalloc.h"

#define HAVE_LIBRAW

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

extern void prune_render_caches ();

class image_data_loader
{
public:
  virtual bool init_loader (const char *name, const char **error, progress_info *progress) = 0;
  virtual bool load_part (int *permille, const char **error, progress_info *progress) = 0;
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
  virtual bool init_loader (const char *name, const char **error, progress_info *);
  virtual bool load_part (int *permille, const char **error, progress_info *progress);
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
  virtual bool init_loader (const char *name, const char **error, progress_info *);
  virtual bool load_part (int *permille, const char **error, progress_info *progress);
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

class raw_image_data_loader: public image_data_loader
{
public:
  raw_image_data_loader (image_data *img)
  : lcc(NULL),m_img (img)
  { }
  virtual bool init_loader (const char *name, const char **error, progress_info *);
  virtual bool load_part (int *permille, const char **error, progress_info *progress);
  virtual ~raw_image_data_loader ()
  {
    /*if (lcc)
      delete lcc;*/
  }
private:
  backlight_correction_parameters *lcc;
  image_data *m_img;
  LibRaw RawProcessor;
};

class stitch_image_data_loader: public image_data_loader
{
public:
  stitch_image_data_loader (image_data *img, bool preload)
  : m_img (img), m_preload_all (preload)
  { }
  virtual bool init_loader (const char *name, const char **error, progress_info *);
  virtual bool load_part (int *permille, const char **error, progress_info *progress);
  virtual ~stitch_image_data_loader ()
  {
  }
private:
  image_data *m_img;
  bool m_preload_all;
  int m_curr_img;
  int m_max_img;
};


image_data::image_data ()
: data (NULL), rgbdata (NULL), icc_profile (NULL), width (0), height (0), maxval (0), icc_profile_size (0), id (lru_caches::get ()), xdpi(0), ydpi(0), stitch (NULL), primary_red {0.6400, 0.3300, 1.0}, primary_green {0.3000, 0.6000, 1.0}, primary_blue {0.1500, 0.0600, 1.0}, whitepoint {0.312700492, 0.329000939, 1.0}, lcc (NULL), gamma (-2), loader (NULL), own (false)
{ 
}

image_data::~image_data ()
{
  if (loader)
    delete loader;
  if (stitch)
    delete stitch;
  if (icc_profile)
    free (icc_profile);
  if (!own)
    return;
  if (data)
    {
      MapAlloc::Free (*data);
      free (data);
    }
  if (rgbdata)
    {
      MapAlloc::Free (*rgbdata);
      free (rgbdata);
    }
  if (lcc)
    delete lcc;
  prune_render_caches ();
}

bool
image_data::allocate_grayscale ()
{
  if (stitch)
    return false;
  assert (loader != NULL);
  return loader->grayscale;
}

bool
image_data::allocate_rgb ()
{
  if (stitch)
    return false;
  assert (loader != NULL);
  return loader->rgb;
}

bool
image_data::allocate ()
{
  if (allocate_grayscale ())
    {
      assert (!data);
      data = (gray **)malloc (sizeof (*data) * height);
      if (!data)
	return false;
      data[0] = (gray *)MapAlloc::Alloc (width * height * sizeof (**data),"grayscale data");
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
      assert (!rgbdata);
      rgbdata = (pixel **)malloc (sizeof (*rgbdata) * height);
      if (!rgbdata)
	{
	  free (*data);
	  if (data)
	    free (data);
	  data = NULL;
	  return false;
	}
      rgbdata[0] = (pixel *)MapAlloc::Alloc (width * height * sizeof (**rgbdata), "RGB data");
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
tiff_image_data_loader::init_loader (const char *name, const char **error, progress_info *)
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
  float dpi;
  if (debug)
    printf("checking width/height\n");
  TIFFGetField(m_tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(m_tif, TIFFTAG_IMAGELENGTH, &h);
  if (TIFFGetField (m_tif, TIFFTAG_XRESOLUTION, &dpi))
    m_img->xdpi = dpi;
  if (TIFFGetField (m_tif, TIFFTAG_YRESOLUTION, &dpi))
    m_img->ydpi = dpi;
  if (m_img->xdpi && !m_img->ydpi)
    m_img->ydpi = m_img->xdpi;
  else if (!m_img->xdpi && m_img->ydpi)
    m_img->xdpi = m_img->ydpi;
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
tiff_image_data_loader::load_part (int *permille, const char **error, progress_info *)
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
jpg_image_data_loader::init_loader (const char *name, const char **error, progress_info *)
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
  /* TODO: use Exif to determine DPI.  */
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
  //m_img_buf = (unsigned char *)tjAlloc(m_img->width * (size_t) m_img->height * tjPixelSize[pixelFormat]);
  m_img_buf = (unsigned char *)malloc(m_img->width * (size_t) m_img->height * tjPixelSize[pixelFormat]);
  if (!m_img_buf)
    {
      *error = "can not allocate decompressed image buffer";
      return false;
    }
  if (tjDecompress2(m_tj_instance, m_jpeg_buf, jpegSize, m_img_buf, m_img->width, 0, m_img->height,
		    pixelFormat, TJFLAG_ACCURATEDCT) < 0)
    {
      *error = "jpeg decompression failed";
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
jpg_image_data_loader::load_part (int *permille, const char **error, progress_info *)
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
  return !strcasecmp (suffix, name + l1 - l2);
}

bool
raw_image_data_loader::init_loader (const char *name, const char **error, progress_info *progress)
{
  size_t buffer_size;
  void *buffer = NULL;
  if (has_suffix (name, ".eip"))
    {
      int errcode;
      zip_t *zip = NULL;
      zip_file_t *zip_file = NULL;
      zip = zip_open (name, ZIP_RDONLY, &errcode);
      if (!zip)
	{
	  *error = "can not open eip zip archive";
	  return false;
	}
      zip_file = zip_fopen (zip, "0.iiq", 0);
      if (!zip_file)
	{
	  *error = "can not find 0.iiq in the eip zip archive";
	  return false;
	}
      zip_stat_t stat;
      if (zip_stat (zip, "0.iiq", 0, &stat))
        {
	  *error = "can not determine length of 0.iiq in the eip zip archive";
	  return false;
        }
      buffer_size = stat.size;
      buffer = malloc (buffer_size);
      if (!buffer)
        {
	  *error = "can not allocate buffer to decompress RAW file";
	  return false;
        }
      if (buffer_size != (size_t)zip_fread (zip_file, buffer, buffer_size))
	{
	  *error = "can not decompress the RAW file";
	  free (buffer);
	  return false;
	}
      zip_fclose (zip_file);
      int nentries = zip_get_num_entries (zip, 0);
      for (int i = 0; i < nentries; i++)
	{
	  const char *name = zip_get_name (zip, i, 0);
	  int len = strlen (name);
	  if (name[len-4]=='.' && name[len-3]=='l' && name[len-2]=='c' && name[len-1]=='c')
	    {
	      if (zip_stat (zip, name, 0, &stat))
		{
		  *error = "can not determine length of 0.iiq in the eip zip archive";
		  free (buffer);
		  return false;
		}
	      zip_file = zip_fopen (zip, name, 0);
	      if (!zip_file)
		{
		  *error = "can not find 0.iiq in the eip zip archive";
		  return false;
		}
	      memory_buffer mbuffer = {NULL, 0, 0};
	      mbuffer.len = stat.size;
	      mbuffer.data = malloc (mbuffer.len);
	      if (!mbuffer.data)
		{
		  *error = "can not allocate buffer to decompress LCC file";
		  free (buffer);
		  free (mbuffer.data);
		  return false;
		}
	      if ((size_t)mbuffer.len != (size_t)zip_fread (zip_file, mbuffer.data, mbuffer.len))
		{
		  *error = "can not allocate buffer to decompress LCC file";
		  free (buffer);
		  free (mbuffer.data);
		  return false;
		}
	      lcc = backlight_correction_parameters::load_captureone_lcc (&mbuffer, false);
	      if (!lcc)
		{
		  *error = "can not read LCC file";
		  free (buffer);
		  free (mbuffer.data);
		  return false;
		}
	      m_img->lcc = lcc;
	      free (mbuffer.data);
	      zip_fclose (zip_file);
	      break;
	    }
	}
      zip_close (zip);
    }
  RawProcessor.imgdata.params.gamm[0] = RawProcessor.imgdata.params.gamm[1] = RawProcessor.imgdata.params.no_auto_bright = 1;
  RawProcessor.imgdata.params.use_camera_matrix = 0;
  RawProcessor.imgdata.params.output_color = 0;
  RawProcessor.imgdata.params.user_qual = 0;
  RawProcessor.imgdata.params.use_auto_wb = 0;
  RawProcessor.imgdata.params.use_camera_wb = 0;
  RawProcessor.imgdata.params.use_camera_matrix = 0;
  RawProcessor.imgdata.params.no_auto_bright = 1;
  RawProcessor.imgdata.params.fbdd_noiserd = 0;
  /* TODO figure out threshold.  */
  RawProcessor.imgdata.params.threshold = 0;
  int ret;
  if (buffer)
    ret = RawProcessor.open_buffer (buffer, buffer_size);
  else
    ret = RawProcessor.open_file(name);
  if (ret != LIBRAW_SUCCESS)
    {
      if (buffer)
	free (buffer);
      *error = libraw_strerror(ret);
      return false;
    }
  if (progress)
    progress->set_task ("unpacking RAW data",1);
  if ((ret = RawProcessor.unpack()) != LIBRAW_SUCCESS)
    {
      if (buffer)
	free (buffer);
      *error = libraw_strerror(ret);
      return false;
    }
  if (progress)
    progress->set_task ("demosaicing",1);
  if ((ret = RawProcessor.dcraw_process()) != LIBRAW_SUCCESS)
    {
      if (buffer)
	free (buffer);
      *error = libraw_strerror(ret);
      return false;
    }
  grayscale = false;
  m_img->gamma = 1;
  rgb = true;
  m_img->width = RawProcessor.imgdata.sizes.width;
  m_img->height = RawProcessor.imgdata.sizes.height;
  m_img->maxval = 65535;
  color_matrix m(RawProcessor.imgdata.color.cam_xyz[0][0], RawProcessor.imgdata.color.cam_xyz[1][0], RawProcessor.imgdata.color.cam_xyz[2][0], 0,
		 RawProcessor.imgdata.color.cam_xyz[0][1], RawProcessor.imgdata.color.cam_xyz[1][1], RawProcessor.imgdata.color.cam_xyz[2][1], 0,
		 RawProcessor.imgdata.color.cam_xyz[0][2], RawProcessor.imgdata.color.cam_xyz[1][2], RawProcessor.imgdata.color.cam_xyz[2][2], 0,
		 0, 0, 0, 1);
  //m = m.invert ();
#if 0
  const double b = 512;
  color_matrix premult (b/RawProcessor.imgdata.color.cam_mul[0],0, 0, 0,
			0, b/RawProcessor.imgdata.color.cam_mul[1], 0, 0,
			0, 0, b/RawProcessor.imgdata.color.cam_mul[2], 0,
			0, 0, 0, 1);
#endif
  color_matrix premult (1/RawProcessor.imgdata.color.pre_mul[0],0, 0, 0,
			0, 1/RawProcessor.imgdata.color.pre_mul[1], 0, 0,
			0, 0, 1/RawProcessor.imgdata.color.pre_mul[2], 0,
			0, 0, 0, 1);
  m = premult * m.invert ();
  xyz_to_xyY (m.m_elements[0][0], m.m_elements[1][0], m.m_elements[2][0], &m_img->primary_red.x, &m_img->primary_red.y, &m_img->primary_red.Y);
  //printf ("red %f %f\n",  m_img->primary_red.x, m_img->primary_red.y);
  xyz_to_xyY (m.m_elements[0][1], m.m_elements[1][1], m.m_elements[2][1], &m_img->primary_green.x, &m_img->primary_green.y, &m_img->primary_green.Y);
  //printf ("green %f %f\n",  m_img->primary_green.x, m_img->primary_green.y);
  xyz_to_xyY (m.m_elements[0][2], m.m_elements[1][2], m.m_elements[2][2], &m_img->primary_blue.x, &m_img->primary_blue.y, &m_img->primary_blue.Y);
  //printf ("blue %f %f\n",  m_img->primary_blue.x, m_img->primary_blue.y);
  //m_img->primary_red.Y /= RawProcessor.imgdata.color.pre_mul[0];
  //m_img->primary_green.Y /= RawProcessor.imgdata.color.pre_mul[1];
  //m_img->primary_blue.Y /= RawProcessor.imgdata.color.pre_mul[2];
  if (buffer)
    free (buffer);
  return true;
}

bool
raw_image_data_loader::load_part (int *permille, const char **error, progress_info *)
{
#pragma omp parallel for default(none) shared(m_img,RawProcessor)
  for (int y = 0; y < m_img->height; y++)
    for (int x = 0; x < m_img->width; x++)
      {
	int i = y * m_img->width + x;
	m_img->rgbdata[y][x].r = RawProcessor.imgdata.image[i][0];
	m_img->rgbdata[y][x].g = RawProcessor.imgdata.image[i][1];
	m_img->rgbdata[y][x].b = RawProcessor.imgdata.image[i][2];
#if 0
	if (!lcc)
	  {
	    m_img->rgbdata[y][x].r = RawProcessor.imgdata.image[i][0];
	    m_img->rgbdata[y][x].g = RawProcessor.imgdata.image[i][1];
	    m_img->rgbdata[y][x].b = RawProcessor.imgdata.image[i][2];
	  }
	else
	  {
	    m_img->rgbdata[y][x].r = lcc->apply (RawProcessor.imgdata.image[i][0], m_img->width, m_img->height, x, y, backlight_correction::red);
	    m_img->rgbdata[y][x].g = lcc->apply (RawProcessor.imgdata.image[i][1], m_img->width, m_img->height, x, y, backlight_correction::green);
	    m_img->rgbdata[y][x].b = lcc->apply (RawProcessor.imgdata.image[i][2], m_img->width, m_img->height, x, y, backlight_correction::blue);
	  }
#endif
      }
  *permille = 1000;
  RawProcessor.recycle ();
  return true;
}

bool
stitch_image_data_loader::init_loader (const char *name, const char **error, progress_info *progress)
{
  if (progress)
    progress->set_task ("opening stich project",1);
  FILE *f = fopen (name, "rt");
  if (!f)
    {
      *error = "Can not open file";
      return false;
    }
  if (progress)
    progress->set_task ("loading stich project",1);
  m_img->stitch = new stitch_project ();
  m_img->stitch->set_path_by_filename (name);
  if (!m_img->stitch->load (f, error))
    {
      fclose (f);
      delete m_img->stitch;
      m_img->stitch = NULL;
      return false;
    }
  fclose (f);
  if (!m_img->stitch->initialize ())
    {
      *error = "Can not initialize stitch project";
      delete m_img->stitch;
      m_img->stitch = NULL;
      return false;
    }
  m_img->stitch->determine_angle ();
  int xmax, ymax;
  m_img->stitch->determine_viewport (m_img->xmin, xmax, m_img->ymin, ymax);
  if (m_preload_all)
    {
      m_img->stitch->keep_all_images ();
      increase_lru_cache_sizes_for_stitch_projects (m_img->stitch->params.width * m_img->stitch->params.height);
    }
  m_img->width = xmax - m_img->xmin;
  m_img->height = ymax - m_img->ymin;
  m_curr_img = 0;
  m_max_img = m_preload_all ? m_img->stitch->params.width * m_img->stitch->params.height - 1 : 0;
  
  return true;
}

bool
stitch_image_data_loader::load_part (int *permille, const char **error, progress_info *progress)
{
  int y = m_curr_img / m_img->stitch->params.width;
  int x = m_curr_img % m_img->stitch->params.width;
  stitch_image &simg = m_img->stitch->images[y][x];
  if (!simg.img)
    {
      if (progress)
	progress->push();
      if (progress)
	progress->set_task ("loading image header",1);
      if (!simg.init_loader (error, progress))
	return false;
      if (progress)
	progress->pop();
      *permille = 1000 * m_curr_img / (m_max_img + 1);
      if (!simg.img->allocate ())
	{
	  *error = "out of memory";
	  delete simg.img;
	  simg.img = NULL;
	  return false;
	}
      return true;
    }
  int permille2;
  if (progress)
    progress->push();
  if (!simg.load_part (&permille2, error, progress))
    {
      delete simg.img;
      simg.img = NULL;
      return false;
    }
  if (progress)
    progress->pop();
  *permille = 1000 * m_curr_img / (m_max_img + 1) + permille2 / (m_max_img + 1);
  if (permille2 == 1000)
    {
      if (!x && !y)
	{
	  m_img->maxval = simg.img->maxval;
	  m_img->icc_profile_size = simg.img->icc_profile_size;
	  if (simg.img->icc_profile)
	    {
	      m_img->icc_profile = malloc (simg.img->icc_profile_size);
	      m_img->icc_profile_size = simg.img->icc_profile_size;
	      memcpy (m_img->icc_profile, simg.img->icc_profile, m_img->icc_profile_size);
	    }
	  m_img->xdpi = simg.img->xdpi;
	  m_img->ydpi =simg .img->ydpi;
	}
      else
	{
	  if (m_img->maxval != simg.img->maxval)
	    {
	      *error = "images in stitch project must all have the same bit depth";
	      return false;
	    }
	  if (m_img->xdpi != simg.img->xdpi || m_img->ydpi != simg.img->ydpi)
	    {
	      *error = "images in stitch project must all have the same DPI";
	      return false;
	    }
	  if (m_img->icc_profile_size != simg.img->icc_profile_size
	      || memcmp (m_img->icc_profile, simg.img->icc_profile, m_img->icc_profile_size))
	    {
	      *error = "images in stitch project must all have the same color profile";
	      return false;
	    }
	}
      if (m_curr_img == m_max_img)
	{
	  *permille = 1000;
	  return true;
	}
      else
	m_curr_img++;
      return true;
    }
  return true;
}

bool
image_data::init_loader (const char *name, bool preload_all, const char **error, progress_info *progress)
{
  assert (!loader);
  m_preload_all = preload_all;
  if (has_suffix (name, ".tif") || has_suffix (name, ".tiff"))
    loader = new tiff_image_data_loader (this);
  else if (has_suffix (name, ".jpg") || has_suffix (name, ".jpeg"))
    loader = new jpg_image_data_loader (this);
  else if (has_suffix (name, ".raw") || has_suffix (name, ".dng") || has_suffix (name, "iiq") || has_suffix (name, "NEF") || has_suffix (name, "cr2") || has_suffix (name, "CR2"))
    loader = new raw_image_data_loader (this);
  else if (has_suffix (name, ".eip"))
    loader = new raw_image_data_loader (this);
  else if (has_suffix (name, ".csprj"))
    loader = new stitch_image_data_loader (this, preload_all);
  if (!loader)
    {
      *error = "Unknown file extension";
      return false;
    }
  bool ret = loader->init_loader (name, error, progress);
  if (!ret)
    {
      delete loader;
      loader = NULL;
    }
  return ret;
}

bool
image_data::load_part (int *permille, const char **error, progress_info *progress)
{
  assert (loader);
  bool ret = loader->load_part (permille, error, progress);
  if (!ret || *permille == 1000)
    {
      delete loader;
      loader = NULL;
      /* If color profile is available, parse it.  */
      if (icc_profile)
	parse_icc_profile ();
    }
  return ret;
}

bool
image_data::parse_icc_profile ()
{
  if (!icc_profile)
    return true;
  cmsHPROFILE hInProfile = cmsOpenProfileFromMem (icc_profile, icc_profile_size);
  if (!hInProfile)
    {
      fprintf (stderr, "Failed to parse profile\n");
      return false;
    }
  cmsHTRANSFORM hTransform = cmsCreateTransform(hInProfile, TYPE_RGB_FLT, NULL, TYPE_XYZ_FLT, INTENT_ABSOLUTE_COLORIMETRIC, 0);
  if (!hTransform)
    {
      fprintf (stderr, "Failed to do icc profile transform\n");
      return false;
    }
  float rgb_buffer[] = {0,0,0,
			1,0,0,
			0,1,0,
			0,0,1};
  float xyz_buffer[4*3];
  cmsDoTransform (hTransform, rgb_buffer, xyz_buffer, 4);
  xyz_to_xyY (xyz_buffer[3]-xyz_buffer[0], xyz_buffer[4]-xyz_buffer[1], xyz_buffer[5]-xyz_buffer[2],
	      &primary_red.x, &primary_red.y, &primary_red.Y);
  xyz_to_xyY (xyz_buffer[6]-xyz_buffer[0], xyz_buffer[7]-xyz_buffer[1], xyz_buffer[8]-xyz_buffer[2],
	      &primary_green.x, &primary_green.y, &primary_green.Y);
  xyz_to_xyY (xyz_buffer[9]-xyz_buffer[0], xyz_buffer[10]-xyz_buffer[1], xyz_buffer[11]-xyz_buffer[2],
	      &primary_blue.x, &primary_blue.y, &primary_blue.Y);
#if 0
  xyz r = xyY_to_xyz (primary_red.x, primary_red.y, primary_red.Y);
  printf ("%f %f %f   %f %f %f\n", primary_red.x, primary_red.y, primary_red.Y, r.x, r.y, r.z);
  r = xyY_to_xyz (primary_green.x, primary_green.y, primary_green.Y);
  printf ("%f %f %f   %f %f %f\n", primary_green.x, primary_green.y, primary_green.Y, r.x, r.y, r.z);
  r = xyY_to_xyz (primary_blue.x, primary_blue.y, primary_blue.Y);
  printf ("%f %f %f   %f %f %f\n", primary_blue.x, primary_blue.y, primary_blue.Y, r.x, r.y, r.z);
#endif

  cmsDeleteTransform (hTransform);
  cmsCloseProfile (hInProfile);
  return false;
}

bool
image_data::load (const char *name, bool preload_all, const char **error, progress_info *progress)
{
  int permille;
  if (progress)
    progress->set_task ("loading image header",1);
  if (!init_loader (name, preload_all, error, progress))
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
  while (load_part (&permille, error, progress))
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

void
image_data::set_dpi (coord_t new_xdpi, coord_t new_ydpi)
{
  xdpi = new_xdpi;
  ydpi = new_ydpi;
  if (stitch)
    stitch->set_dpi (new_xdpi, new_ydpi);
}

bool
image_data::has_rgb ()
{
  if (stitch)
    return stitch->images[0][0].img->has_rgb ();
  return rgbdata != NULL;
}

bool
image_data::has_grayscale_or_ir ()
{
  if (stitch)
    return stitch->images[0][0].img->has_grayscale_or_ir ();
  return data != NULL;
}
