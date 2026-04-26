#include "config.h"
#include "backlight-correction.h"
#include "include/histogram.h"
#include "include/imagedata.h"
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "mapalloc.h"
#include <array>
#include <assert.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <lcms2.h>
#include <tiffio.h>
#include <turbojpeg.h>
#include <zip.h>
#ifdef HAVE_OPENJPEG
#include <openjpeg.h>
#endif
#ifdef HAVE_LIBPNG
#include <png.h>
#endif
#include <exiv2/exiv2.hpp>


#define HAVE_LIBRAW
#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif
namespace colorscreen
{
extern void prune_render_caches ();
extern void prune_render_scr_detect_caches ();

const property_t image_data::demosaic_names[(int)demosaic_max]
     = {
  { "default", "Default", "Automatically choose demosaicing algorithm" },
  { "half", "Reduce image size to half and avoid demosaicing", "" },
  { "monochromatic", "Monochromatic", "Assume that every pixel in the RAW file represents intensity of a monochromatic capture (disable deosaicing completely)." },
  { "monochromatic_bayer_corrected", "Monochromatic with Bayer filter compensated", "Many monochromatic captures are still done through Bayer filter or with camera that sitll do in-camera processing assuming the Bayer filter.  In this case Monochromataic demosaicing leads to regular checkerboard pattern that is eliminated by this mode" },
  { "linear", "Linear interpolation", "Easiest demosaicing algorithm" },
  { "VNG", "Variable number of gradients interpolation (VNG)", "Variable number of gradients (VNG)[6] interpolation computes gradients near the pixel of interest and uses the lower gradients (representing smoother and more similar parts of the image) to make an estimate. It is used in first versions of dcraw, and suffers from color artifacts." },
  { "PPG", "Pixel grouping (PPG)", "Pixel grouping (PPG)[7] uses assumptions about natural scenery in making estimates. It has fewer color artifacts on natural images than the Variable Number of Gradients method." },
  { "AHD", "Adaptive homogeneity-directed (AHD)", "Adaptive homogeneity-directed (AHD) is widely used in the industry. It selects the direction of interpolation so as to maximize a homogeneity metric, thus typically minimizing color artifacts." },
  { "DCB", "Directional Conditional Based (DCB)", "DCB (Directional Conditional Based) is a high-quality demosaicing algorithm that reconstructs full-color images by intelligently analyzing edge directions to prevent blurring. It utilizes a directional interpolation strategy to detect local gradients, followed by a conditional refinement stage that filters out \"zipper\" artifacts and false color fringing. This dual-pass approach makes it exceptionally effective at preserving sharp details and color accuracy in fine textures, offering a cleaner output than standard interpolation methods" },
  { "DHT", "Directional Homogeneity and Thresholding (DHT)", "DHT (Directional Homogeneity and Thresholding) is an edge-aware demosaicing algorithm that minimizes image artifacts by reconstructing the green channel through competing horizontal and vertical interpolations. It evaluates directional homogeneity to determine which orientation best preserves local textures, using mathematical thresholds to suppress \"zipper\" effects and false color fringing. This approach makes DHT particularly effective at producing sharp, crisp edges in images with strong geometric patterns or fine architectural details." },
  { "AAHD", "Adaptive Homogeneity-Directed (AAHD)", "AAHD (Adaptive Homogeneity-Directed) is a sophisticated demosaicing algorithm that improves upon standard directional methods by evaluating the \"homogeneity\"—or uniformity—of color and luminance in local pixel neighborhoods. It performs separate interpolations along horizontal and vertical directions and then selects the result that maintains the highest level of local consistency, which significantly reduces \"zipper\" artifacts and aliasing. Frequently paired with a Luminance/Chrominance refinement pass, AAHD is highly regarded for its ability to produce sharp, natural-looking edges while effectively suppressing the \"rainbow\" color moiré often found in high-frequency textures." },
  { "none", "No demosaicing", "" },
};

class image_data_loader
{
public:
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *progress,
                            image_data::demosaicing_t demosaic)
      = 0;
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress)
      = 0;
  virtual ~image_data_loader () {}
  bool grayscale = false;
  bool rgb = false;
};

namespace
{
class jpg_image_data_loader : public image_data_loader
{
public:
  jpg_image_data_loader (image_data *img)
      : m_img (img), m_jpeg_buf (NULL), m_tj_instance (NULL), m_img_buf (NULL)
  {
  }
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *,
                            image_data::demosaicing_t demosaic);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~jpg_image_data_loader ()
  {
    if (m_jpeg_buf)
      tjFree (m_jpeg_buf);
    if (m_img_buf)
      MapAlloc::Free (m_img_buf);
    if (m_tj_instance)
      tjDestroy (m_tj_instance);
  }

private:
  image_data *m_img;
  unsigned char *m_jpeg_buf;
  tjhandle m_tj_instance;
  unsigned char *m_img_buf;
};

class tiff_image_data_loader : public image_data_loader
{
public:
  tiff_image_data_loader (image_data *img)
      : m_tif (NULL), m_img (img), m_buf (NULL)
  {
  }
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *,
                            image_data::demosaicing_t demosaic);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~tiff_image_data_loader ()
  {
    if (m_tif)
      TIFFClose (m_tif);
    if (m_buf)
      _TIFFfree (m_buf);
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

class raw_image_data_loader : public image_data_loader
{
public:
  raw_image_data_loader (image_data *img) : m_backlight_corr (nullptr), m_img (img), m_buffer (nullptr), m_processor (std::make_unique<LibRaw> ()) {}
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *, image_data::demosaicing_t);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~raw_image_data_loader ()
  {
    /*if (lcc)
      delete lcc;*/
    if (m_buffer)
      free (m_buffer);
  }

private:
  std::shared_ptr<backlight_correction_parameters> m_backlight_corr;
  image_data *m_img;
  void *m_buffer;
  /* Do not put it on the stack since it is rather large.  */
  std::unique_ptr<LibRaw> m_processor;
  bool monochromatic = false;
  bool bayer_correction = false;
};

class stitch_image_data_loader : public image_data_loader
{
public:
  stitch_image_data_loader (image_data *img, bool preload)
      : m_img (img), m_preload_all (preload)
  {
  }
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *,
                            image_data::demosaicing_t demosaic);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~stitch_image_data_loader () {}

private:
  image_data *m_img;
  bool m_preload_all;
  int m_curr_img = 0;
  int m_max_img = 0;
  image_data::demosaicing_t m_demosaic;
};

#ifdef HAVE_OPENJPEG
static void
opj_error_callback (const char *msg, void *)
{
  (void)msg;
}
static void
opj_warning_callback (const char *msg, void *)
{
  (void)msg;
}
static void
opj_info_callback (const char *msg, void *)
{
  (void)msg;
}

class jp2_image_data_loader : public image_data_loader
{
public:
  jp2_image_data_loader (image_data *img) : m_img (img) {}
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *,
                            image_data::demosaicing_t demosaic);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~jp2_image_data_loader () {}

private:
  image_data *m_img;
  std::string m_filename;
};
#endif

#ifdef HAVE_LIBPNG
class png_image_data_loader : public image_data_loader
{
public:
  png_image_data_loader (image_data *img) : m_img (img), m_fp (nullptr) {}
  virtual bool init_loader (const char *name, const char **error,
                            progress_info *,
                            image_data::demosaicing_t demosaic);
  virtual bool load_part (int *permille, const char **error,
                          progress_info *progress);
  virtual ~png_image_data_loader ()
  {
    if (m_fp)
      fclose (m_fp);
  }

private:
  image_data *m_img;
  FILE *m_fp;
  std::string m_filename;
};
#endif
}

image_data::image_data ()
    : id (lru_caches::get ())
{
}

image_data::~image_data ()
{
  if (stitch)
    delete stitch;
  if (icc_profile)
    free (icc_profile);
  if (!own)
    return;
  if (m_data)
    {
      MapAlloc::Free (m_data);
    }
  if (m_rgbdata)
    {
      MapAlloc::Free (m_rgbdata);
    }
  /* if (backlight_corr)
    delete backlight_corr; */
  prune_render_caches ();
  prune_render_scr_detect_caches ();
}

/* Return true if grayscale allocation is needed.  */
bool
image_data::allocate_grayscale ()
{
  if (stitch)
    return false;
  assert (loader != NULL);
  return loader->grayscale;
}

/* Return true if RGB allocation is needed.  */
bool
image_data::allocate_rgb ()
{
  if (stitch)
    return false;
  assert (loader != NULL);
  return loader->rgb;
}

/* Allocate memory for image data.  Return true on success.  */
bool
image_data::allocate ()
{
  if (allocate_grayscale ())
    {
      m_data = (gray *)MapAlloc::Alloc (width * (uint64_t) height * sizeof (*m_data),
                                          "grayscale data");
    }
  if (allocate_rgb ())
    {
      assert (!m_rgbdata);
      m_rgbdata = (pixel *)MapAlloc::Alloc (
          width * (uint64_t) height * sizeof (*m_rgbdata), "RGB data");
      if (!m_rgbdata && m_data)
        {
          MapAlloc::Free (m_data);
          m_data = NULL;
        }
    }
  own = true;
  return true;
}

/* Silence warnings.
   They happen commonly while loading scans.  */
static void
warning_handler (const char *module, const char *fmt, va_list ap)
{
}

/* Initialize TIFF loader for file NAME.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use (ignored for TIFF).  */
bool
tiff_image_data_loader::init_loader (const char *name, const char **error,
                                     progress_info *progress,
                                     image_data::demosaicing_t demosaic)
{
  if (debug)
    printf ("TIFFopen\n");
  TIFFSetWarningHandler (warning_handler);
  m_tif = TIFFOpen (name, "r");
  if (!m_tif)
    {
      *error = "can not open file";
      return false;
    }
  uint32_t w, h;
  uint16_t photometric;
  float dpi;
  if (debug)
    printf ("checking width/height\n");
  TIFFGetField (m_tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField (m_tif, TIFFTAG_IMAGELENGTH, &h);
  if (TIFFGetField (m_tif, TIFFTAG_XRESOLUTION, &dpi))
    m_img->xdpi = dpi;
  if (TIFFGetField (m_tif, TIFFTAG_YRESOLUTION, &dpi))
    m_img->ydpi = dpi;
  if (m_img->xdpi && !m_img->ydpi)
    m_img->ydpi = m_img->xdpi;
  else if (!m_img->xdpi && m_img->ydpi)
    m_img->xdpi = m_img->ydpi;
  if (debug)
    printf ("checking bits per sample\n");
  TIFFGetFieldDefaulted (m_tif, TIFFTAG_BITSPERSAMPLE, &m_bitspersample);
  if (m_bitspersample != 8 && m_bitspersample != 16)
    {
      *error = "bit depth should be 8 or 16";
      return false;
    }
  if (debug)
    printf ("checking smaples per pixel\n");
  TIFFGetFieldDefaulted (m_tif, TIFFTAG_SAMPLESPERPIXEL, &m_samples);
  void *iccprof;
  uint32_t size;
  if (TIFFGetField (m_tif, TIFFTAG_ICCPROFILE, &size, &iccprof))
    {
      // m_img->icc_profile = iccprof;
      // m_img->icc_profile_size = size;
      m_img->icc_profile = malloc (size);
      memcpy (m_img->icc_profile, iccprof, size);
      m_img->icc_profile_size = size;
    }
  if (m_samples != 1 && m_samples != 3 && m_samples != 4)
    {
      if (debug)
        printf ("Samples:%i\n", m_samples);
      *error = "only 1 sample per pixel (grayscale), 3 samples per pixel "
               "(RGB) or 4 samples per pixel (RGBa) are supported";
      return false;
    }
  if (TIFFGetField (m_tif, TIFFTAG_PHOTOMETRIC, &photometric))
    {
      if (photometric == PHOTOMETRIC_MINISBLACK && m_samples == 1)
        ;
      else if (photometric == PHOTOMETRIC_RGB
               && (m_samples == 4 || m_samples == 3))
        ;
      else
        {
          *error = "only RGB, RGBa or grayscale images are suppored";
          return false;
        }
    }
  if (debug)
    printf ("Getting scanlinel\n");
  m_buf = _TIFFmalloc (TIFFScanlineSize (m_tif));
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
  m_img->load_exif (name);
  return true;
}

/* Load part of the TIFF image.
   Update PERMILLE with progress (0-1000).
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.  */
bool
tiff_image_data_loader::load_part (int *permille, const char **error,
                                   progress_info *progress)
{
  if ((int)m_row < m_img->height)
    {
      uint32_t row = m_row;
      uint32_t w = m_img->width;
      image_data::gray *data = m_img->get_row (row);
      image_data::pixel *rgbdata = m_img->get_rgb_row (row);

      if (debug)
        printf ("Decoding scanline %i\n", row);
      if (!TIFFReadScanline (m_tif, m_buf, row))
        {
          *error = "scanline decoding failed";
          return false;
        }
      if (m_bitspersample == 8 && m_samples == 1)
        {
          uint8_t *buf2 = (uint8_t *)m_buf;
          if (data)
            for (uint32_t x = 0; x < w; x++)
              data[x] = buf2[x];
        }
      else if (m_bitspersample == 8 && m_samples == 3)
        {
          uint8_t *buf2 = (uint8_t *)m_buf;
          if (rgbdata)
            for (uint32_t x = 0; x < w; x++)
              {
                rgbdata[x].r = buf2[3 * x + 0];
                rgbdata[x].g = buf2[3 * x + 1];
                rgbdata[x].b = buf2[3 * x + 2];
              }
        }
      else if (m_bitspersample == 8 && m_samples == 4)
        {
          uint8_t *buf2 = (uint8_t *)m_buf;
          for (uint32_t x = 0; x < w; x++)
            {
              if (rgbdata)
                {
                  rgbdata[x].r = buf2[4 * x + 0];
                  rgbdata[x].g = buf2[4 * x + 1];
                  rgbdata[x].b = buf2[4 * x + 2];
                }
              if (data)
                data[x] = buf2[4 * x + 3];
            }
        }
      else if (m_bitspersample == 16 && m_samples == 1)
        {
          uint16_t *buf2 = (uint16_t *)m_buf;
          if (data)
            for (uint32_t x = 0; x < w; x++)
              data[x] = buf2[x];
        }
      else if (m_bitspersample == 16 && m_samples == 3)
        {
          uint16_t *buf2 = (uint16_t *)m_buf;
          if (rgbdata)
            for (uint32_t x = 0; x < w; x++)
              {
                rgbdata[x].r = buf2[3 * x + 0];
                rgbdata[x].g = buf2[3 * x + 1];
                rgbdata[x].b = buf2[3 * x + 2];
              }
        }
      else if (m_bitspersample == 16 && m_samples == 4)
        {
          uint16_t *buf2 = (uint16_t *)m_buf;
          for (uint32_t x = 0; x < w; x++)
            {
              if (rgbdata)
                {
                  rgbdata[x].r = buf2[4 * x + 0];
                  rgbdata[x].g = buf2[4 * x + 1];
                  rgbdata[x].b = buf2[4 * x + 2];
                }
              if (data)
                data[x] = buf2[4 * x + 3];
            }
        }
      else
        {
          /* We should have given up earlier.  */
          fprintf (stderr,
                   "Wrong combinations of bitspersample %i and samples %i\n",
                   m_bitspersample, m_samples);
          abort ();
        }
      *permille = (999 * m_row + m_img->height / 2) / m_img->height;
      m_row++;
    }
  else
    {
      if (debug)
        printf ("done\n");
      *permille = 1000;
    }
  return true;
}

/* Initialize JPEG loader for file NAME.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use (ignored for JPEG).  */
bool
jpg_image_data_loader::init_loader (const char *name, const char **error,
                                    progress_info *progress, image_data::demosaicing_t demosaic)
{
  FILE *jpegFile;
  if ((jpegFile = fopen (name, "rb")) == NULL)
    {
      *error = "can not open file";
      return false;
    }
  size_t size;
  if (fseek (jpegFile, 0, SEEK_END) < 0 || ((size = ftell (jpegFile)) < 0)
      || fseek (jpegFile, 0, SEEK_SET) < 0)
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
  if ((m_jpeg_buf = (unsigned char *)tjAlloc (jpegSize)) == NULL)
    {
      *error = "input file is empty";
      fclose (jpegFile);
      return false;
    }
  if (fread (m_jpeg_buf, jpegSize, 1, jpegFile) < 1)
    {
      *error = "can not read file";
      fclose (jpegFile);
      return false;
    }
  fclose (jpegFile);
  m_tj_instance = tjInitDecompress ();
  if (!m_tj_instance)
    {
      *error = "can not initialize jpeg decompressor";
      return false;
    }
  int inSubsamp, inColorspace;
  /* TODO: use Exif to determine DPI.  */
  if (tjDecompressHeader3 (m_tj_instance, m_jpeg_buf, jpegSize, &m_img->width,
                           &m_img->height, &inSubsamp, &inColorspace)
      < 0)
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
  // m_img_buf = (unsigned char *)tjAlloc(m_img->width * (size_t) m_img->height
  // * tjPixelSize[pixelFormat]);
  m_img_buf = (unsigned char *)MapAlloc::Alloc (
      m_img->width * (size_t)m_img->height * tjPixelSize[pixelFormat],
      "Decompressed jpeg image");
  if (!m_img_buf)
    {
      *error = "can not allocate decompressed image buffer";
      return false;
    }
  if (tjDecompress2 (m_tj_instance, m_jpeg_buf, jpegSize, m_img_buf,
                     m_img->width, 0, m_img->height, pixelFormat,
                     TJFLAG_ACCURATEDCT)
      < 0)
    {
      *error = "jpeg decompression failed";
      return false;
    }
  free (m_jpeg_buf);
  m_jpeg_buf = NULL;
  tjDestroy (m_tj_instance);
  m_tj_instance = NULL;
  grayscale = !rgb;
  m_img->maxval = 255;
  m_img->load_exif (name);
  return true;
}

/* Load part of the JPEG image.
   Update PERMILLE with progress (0-1000).
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.  */
bool
jpg_image_data_loader::load_part (int *permille, const char **error,
                                  progress_info *progress)
{
  int width = m_img->width;
  int height = m_img->height;
  if (!rgb)
    for (int y = 0; y < height; y++)
      {
	image_data::gray *row = m_img->get_row (y);
	if (row)
	  for (int x = 0; x < width; x++)
	    row[x] = m_img_buf[y * (uint64_t) width + x];
      }
  else
    for (int y = 0; y < height; y++)
      {
	image_data::pixel *row = m_img->get_rgb_row (y);
	if (row)
	  for (int x = 0; x < width; x++)
	    {
	      row[x].r = m_img_buf[y * (uint64_t) width * 3 + x * 3 + 0];
	      row[x].g = m_img_buf[y * (uint64_t) width * 3 + x * 3 + 1];
	      row[x].b = m_img_buf[y * (uint64_t) width * 3 + x * 3 + 2];
	    }
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

/* Initialize RAW loader for file NAME.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use.  */
bool
raw_image_data_loader::init_loader (const char *name, const char **error,
                                    progress_info *progress,
                                    image_data::demosaicing_t demosaic)
{
  size_t buffer_size;
  m_buffer = NULL;
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
      name = "0.iiq";
      zip_file = zip_fopen (zip, name, 0);
      if (!zip_file)
        {
          name = "0.IIQ";
          zip_file = zip_fopen (zip, name, 0);
        }
      if (!zip_file)
        {
          *error = "can not find 0.iiq in the eip zip archive";
          return false;
        }
      zip_stat_t stat;
      if (zip_stat (zip, name, 0, &stat))
        {
          *error = "can not determine length of 0.iiq in the eip zip archive";
          return false;
        }
      buffer_size = stat.size;
      m_buffer = malloc (buffer_size);
      if (!m_buffer)
        {
          *error = "can not allocate buffer to decompress RAW file";
          return false;
        }
      if (buffer_size != (size_t)zip_fread (zip_file, m_buffer, buffer_size))
        {
          *error = "can not decompress the RAW file";
          free (m_buffer);
          m_buffer = NULL;
          return false;
        }
      zip_fclose (zip_file);
      int nentries = zip_get_num_entries (zip, 0);
      for (int i = 0; i < nentries; i++)
        {
          const char *name = zip_get_name (zip, i, 0);
          int len = strlen (name);
          if (name[len - 4] == '.' && name[len - 3] == 'l'
              && name[len - 2] == 'c' && name[len - 1] == 'c')
            {
              if (zip_stat (zip, name, 0, &stat))
                {
                  *error = "can not determine length of LLC file in the eip "
                           "zip archive";
                  free (m_buffer);
                  m_buffer = NULL;
                  return false;
                }
              zip_file = zip_fopen (zip, name, 0);
              if (!zip_file)
                {
                  *error = "can not find LLC file in the eip zip archive";
                  return false;
                }
              memory_buffer mbuffer = { NULL, 0, 0 };
              mbuffer.len = stat.size;
              mbuffer.data = malloc (mbuffer.len);
              if (!mbuffer.data)
                {
                  *error = "can not allocate buffer to decompress LCC file";
                  free (m_buffer);
                  m_buffer = NULL;
                  free (mbuffer.data);
                  return false;
                }
              if ((size_t)mbuffer.len
                  != (size_t)zip_fread (zip_file, mbuffer.data, mbuffer.len))
                {
                  *error = "can not allocate buffer to decompress LCC file";
                  free (m_buffer);
                  m_buffer = NULL;
                  free (mbuffer.data);
                  return false;
                }
#if 0
	      lcc = backlight_correction_parameters::load_captureone_lcc (&mbuffer, false);
	      if (!lcc)
		{
		  *error = "can not read LCC file";
		  free (m_buffer);
                  m_buffer = NULL;
		  free (mbuffer.data);
		  return false;
		}
#endif
              m_img->backlight_corr = m_backlight_corr;
              free (mbuffer.data);
              zip_fclose (zip_file);
              break;
            }
        }
      zip_close (zip);
    }
  m_processor->imgdata.params.gamm[0] = m_processor->imgdata.params.gamm[1]
      = m_processor->imgdata.params.no_auto_bright = 1;
  m_processor->imgdata.params.use_camera_matrix = 0;
  m_processor->imgdata.params.output_color = 0;
  m_processor->imgdata.params.highlight = 0;
  switch (demosaic)
    {
    case image_data::demosaic_linear:
    case image_data::demosaic_half:
      m_processor->imgdata.params.user_qual = 0;
      break;

    /* The following use no demosaicing; any value is good.  */
    case image_data::demosaic_monochromatic:
    case image_data::demosaic_monochromatic_bayer_corrected:
    case image_data::demosaic_none: 
      m_processor->imgdata.params.user_qual = 0;
      break;
    case image_data::demosaic_VNG:
      m_processor->imgdata.params.user_qual = 1;
      m_img->demosaiced_by = image_data::demosaic_VNG;
      break;
    case image_data::demosaic_PPG:
      m_processor->imgdata.params.user_qual = 2;
      m_img->demosaiced_by = image_data::demosaic_PPG;
      break;
    /* AHD seems to go well on demosaicing photo of Paget screen.  */
    case image_data::demosaic_default:
    case image_data::demosaic_AHD:
      m_processor->imgdata.params.user_qual = 3;
      m_img->demosaiced_by = image_data::demosaic_AHD;
      break;
    case image_data::demosaic_DCB:
      m_processor->imgdata.params.user_qual = 4;
      m_img->demosaiced_by = image_data::demosaic_DCB;
      break;
    case image_data::demosaic_DHT:
      m_processor->imgdata.params.user_qual = 11;
      m_img->demosaiced_by = image_data::demosaic_DHT;
      break;
    case image_data::demosaic_AAHD:
      m_processor->imgdata.params.user_qual = 12;
      m_img->demosaiced_by = image_data::demosaic_AAHD;
      break;
    case image_data::demosaic_max:
      abort ();
    }
  m_processor->imgdata.params.use_auto_wb = 0;
  m_processor->imgdata.params.use_camera_wb = 0;
  m_processor->imgdata.params.use_camera_matrix = 0;
  m_processor->imgdata.rawparams.max_raw_memory_mb = 10000;
  if (demosaic == image_data::demosaic_half)
    {
      m_processor->imgdata.params.half_size = 1;
      m_img->demosaiced_by = image_data::demosaic_half;
    }
  m_processor->imgdata.params.no_auto_bright = 1;
  m_processor->imgdata.params.fbdd_noiserd = 0;

  monochromatic
      = (demosaic == image_data::demosaic_monochromatic
         || demosaic == image_data::demosaic_monochromatic_bayer_corrected);
  bayer_correction
      = demosaic == image_data::demosaic_monochromatic_bayer_corrected;
  if (monochromatic)
    m_img->demosaiced_by = demosaic;
  /* TODO figure out threshold.  */
  m_processor->imgdata.params.threshold = 0;
  if (demosaic == image_data::demosaic_none || monochromatic)
    m_processor->imgdata.params.no_interpolation = 1;
  int ret;
  if (m_buffer)
    ret = m_processor->open_buffer (m_buffer, buffer_size);
  else
    ret = m_processor->open_file (name);
  if (ret != LIBRAW_SUCCESS)
    {
      if (m_buffer)
        {
          free (m_buffer);
          m_buffer = NULL;
        }
      *error = libraw_strerror (ret);
      return false;
    }
  if (!m_processor->imgdata.idata.filters)
    m_img->demosaiced_by = image_data::demosaic_max;
  m_img->f_stop = m_processor->imgdata.other.aperture;
  m_img->focal_length = m_processor->imgdata.other.focal_len;
  m_img->camera_model = m_processor->imgdata.idata.model;
  m_img->lens = m_processor->imgdata.lens.Lens;
  if (m_processor->imgdata.lens.FocalLengthIn35mmFormat > 0)
    m_img->focal_length_in_35mm = m_processor->imgdata.lens.FocalLengthIn35mmFormat;
  if (m_processor->imgdata.idata.colors != 1
      && m_processor->imgdata.idata.colors != 3)
    {
      *error
          = "number of colors in RAW file should be 3 (RGB) or 1 (achromatic)";
      return false;
    }
  if (progress)
    progress->set_task ("unpacking RAW data", 1);
  if ((ret = m_processor->unpack ()) != LIBRAW_SUCCESS)
    {
      if (m_buffer)
        {
          free (m_buffer);
          m_buffer = NULL;
        }
      *error = libraw_strerror (ret);
      return false;
    }
  if (progress)
    progress->set_task ("demosaicing", 1);
  if ((ret = m_processor->dcraw_process ()) != LIBRAW_SUCCESS)
    {
      if (m_buffer)
        {
          free (m_buffer);
          m_buffer = NULL;
        }
      *error = libraw_strerror (ret);
      return false;
    }
  grayscale = false;
  m_img->gamma = 1;
  if (monochromatic && m_processor->imgdata.idata.colors != 3)
    monochromatic = false;
  rgb = m_processor->imgdata.idata.colors == 3 && !monochromatic;
  grayscale = m_processor->imgdata.idata.colors == 1 || monochromatic;
  m_img->width = m_processor->imgdata.sizes.width;
  m_img->height = m_processor->imgdata.sizes.height;
  m_img->maxval = 65535;

  /* For achromatic back we need no camera matrix.  */
  if (m_processor->imgdata.idata.colors == 3)
    {
      bool nonzero = false;
      /* Some RAW files have empty camera matrix.  */
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
          if (m_processor->imgdata.color.cam_xyz[i][j])
            nonzero = true;
      if (nonzero)
        {
          color_matrix m (m_processor->imgdata.color.cam_xyz[0][0],
                          m_processor->imgdata.color.cam_xyz[1][0],
                          m_processor->imgdata.color.cam_xyz[2][0], 0,
                          m_processor->imgdata.color.cam_xyz[0][1],
                          m_processor->imgdata.color.cam_xyz[1][1],
                          m_processor->imgdata.color.cam_xyz[2][1], 0,
                          m_processor->imgdata.color.cam_xyz[0][2],
                          m_processor->imgdata.color.cam_xyz[1][2],
                          m_processor->imgdata.color.cam_xyz[2][2], 0, 0, 0, 0,
                          1);
          // m = m.invert ();
#if 0
	  const double b = 512;
	  color_matrix premult (b/m_processor->imgdata.color.cam_mul[0],0, 0, 0,
				0, b/m_processor->imgdata.color.cam_mul[1], 0, 0,
				0, 0, b/m_processor->imgdata.color.cam_mul[2], 0,
				0, 0, 0, 1);
#endif
          color_matrix premult (
              1 / m_processor->imgdata.color.pre_mul[0], 0, 0, 0, 0,
              1 / m_processor->imgdata.color.pre_mul[1], 0, 0, 0, 0,
              1 / m_processor->imgdata.color.pre_mul[2], 0, 0, 0, 0, 1);
          m = premult * m.invert ();
          xyz_to_xyY (m(0, 0), m(0, 1),
                      m(0, 2), &m_img->primary_red.x,
                      &m_img->primary_red.y, &m_img->primary_red.Y);
          // printf ("red %f %f\n",  m_img->primary_red.x,
          // m_img->primary_red.y);
          xyz_to_xyY (m(1, 0), m(1, 1),
                      m(1, 2), &m_img->primary_green.x,
                      &m_img->primary_green.y, &m_img->primary_green.Y);
          // printf ("green %f %f\n",  m_img->primary_green.x,
          // m_img->primary_green.y);
          xyz_to_xyY (m(2, 0), m(2, 1),
                      m(2, 2), &m_img->primary_blue.x,
                      &m_img->primary_blue.y, &m_img->primary_blue.Y);
          // printf ("blue %f %f\n",  m_img->primary_blue.x,
          // m_img->primary_blue.y); m_img->primary_red.Y /=
          // m_processor->imgdata.color.pre_mul[0]; m_img->primary_green.Y /=
          // m_processor->imgdata.color.pre_mul[1]; m_img->primary_blue.Y /=
          // m_processor->imgdata.color.pre_mul[2];
        }
    }
  m_img->load_exif (name);

  return true;
}

/* Load part of the RAW image.
   Update PERMILLE with progress (0-1000).
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.  */
bool
raw_image_data_loader::load_part (int *permille, const char **error,
                                  progress_info *progress)
{
  histogram rhistogram, bhistogram;
  const luminosity_t range = 5;

  /* Suppress mosaic pattern.  We only want to find good scaling factor.  */
  if (monochromatic)
    {
      float bscale = 1, rscale = 1;

      if (bayer_correction)
        {
	  luminosity_t grsum = 0, rsum = 0, gbsum = 0, bsum = 0;
	  /* Pass 1: find approximate ratio */
          for (int y = 0; y < m_img->height; y++)
            for (int x = 0; x < m_img->width - 1; x++)
	      {
		int i = y * m_img->width + x;
		int g = m_processor->imgdata.image[i][1];
		if (g > 0 && g < 65535 - 256)
		{
		  int r = m_processor->imgdata.image[i + 1][0];
		  if (r > 0 && r < 65535 - 256)
		    grsum += g, rsum += r;
		  int b = m_processor->imgdata.image[i + 1][2];
		  if (b > 0 && b < 65535 - 256)
		    gbsum += g, bsum += b;
		}
	      }
          if (!grsum || !gbsum)
            {
              *error = "image is too overexposed or completely dark in green channel";
              return false;
            }
          if (!bsum)
            {
              *error = "image has no data in blue channel";
              return false;
            }
          if (!rsum)
            {
              *error = "image has no data in red channel";
              return false;
            }
	  luminosity_t rratio = rsum / grsum;
	  luminosity_t bratio = bsum / gbsum;
	  //fprintf (stderr, "rratio %f bratio %f\n", rratio, bratio);
          rhistogram.set_range (1 - range, 1 + range, 65535 * 4);
          bhistogram.set_range (1 - range, 1 + range, 65535 * 4);
          for (int y = 0; y < m_img->height; y++)
            for (int x = 0; x < m_img->width - 1; x++)
              {
                int i = y * m_img->width + x;
                int g = m_processor->imgdata.image[i][1];

                if (g > 256 && g < 65535 - 256)
                  {
                    assert (!m_processor->imgdata.image[i][0]
                            && !m_processor->imgdata.image[i][2]);
                    int r = m_processor->imgdata.image[i + 1][0];
                    if (r > 256 && r < 65535 - 256)
                      {
                        luminosity_t ratio = g * rratio / (luminosity_t)r;
                        if (ratio > 1 - range && ratio < 1 + range)
                          rhistogram.account (ratio);
                      }
                    int b = m_processor->imgdata.image[i + 1][2];
                    if (b > 256 && b < 65535 - 256)
                      {
                        luminosity_t ratio = g * bratio / (luminosity_t)b;
                        if (ratio > 1 - range && ratio < 1 + range)
                          bhistogram.account (ratio);
                      }
                  }
              }
          rhistogram.finalize ();
          bhistogram.finalize ();
          if (rhistogram.num_samples () < 1024
              || bhistogram.num_samples () < 1024)
            {
              *error = "not enough samples to remove mosaic (image is too dark or overexposed)";
              return false;
            }
          bscale = bhistogram.find_avg (0.2, 0.2) / bratio;
          rscale = rhistogram.find_avg (0.2, 0.2) / rratio;
	  //fprintf (stderr, "rscale %f bscale %f\n", rscale, bscale);
        }
#pragma omp parallel for default(none)                                        \
    shared(m_img, m_processor, bscale, rscale)
      for (int y = 0; y < m_img->height; y++)
	{
	  image_data::gray *row = m_img->get_row (y);
	  if (row)
	    for (int x = 0; x < m_img->width; x++)
	      {
		int i = y * m_img->width + x;
		row[x] = std::clamp (
		    m_processor->imgdata.image[i][0] * rscale
			+ m_processor->imgdata.image[i][1]
			+ m_processor->imgdata.image[i][2] * bscale + (float)0.5,
		    (float)0, (float)65535);
	      }
	}
    }
  else if (m_img->has_rgb ())
    {
#pragma omp parallel for default(none) shared(m_img, m_processor)
      for (int y = 0; y < m_img->height; y++)
	{
	  image_data::pixel *row = m_img->get_rgb_row (y);
	  if (row)
	    for (int x = 0; x < m_img->width; x++)
	      {
		int i = y * m_img->width + x;
		row[x].r = m_processor->imgdata.image[i][0];
		row[x].g = m_processor->imgdata.image[i][1];
		row[x].b = m_processor->imgdata.image[i][2];
	      }
	}
    }
  else
    {
#pragma omp parallel for default(none) shared(m_img, m_processor)
      for (int y = 0; y < m_img->height; y++)
	{
	  image_data::gray *row = m_img->get_row (y);
	  for (int x = 0; x < m_img->width; x++)
	    {
	      int i = y * m_img->width + x;
	      row[x] = m_processor->imgdata.image[i][0];
	    }
	}
    }
  *permille = 1000;
  return true;
}

/* Initialize stitch project loader for file NAME.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use for component images.  */
bool
stitch_image_data_loader::init_loader (const char *name, const char **error,
                                       progress_info *progress,
                                       image_data::demosaicing_t demosaic)
{
  m_demosaic = demosaic;
  if (progress)
    progress->set_task ("opening stitch project", 1);
  FILE *f = fopen (name, "rt");
  if (!f)
    {
      *error = "Can not open file";
      return false;
    }
  if (progress)
    progress->set_task ("loading stitch project", 1);
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
  /* TODO: pass demosaic  */
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
      increase_lru_cache_sizes_for_stitch_projects (
          m_img->stitch->params.width * m_img->stitch->params.height);
    }
  m_img->width = xmax - m_img->xmin;
  m_img->height = ymax - m_img->ymin;
  m_curr_img = 0;
  m_max_img
      = m_preload_all
            ? m_img->stitch->params.width * m_img->stitch->params.height - 1
            : 0;

  return true;
}

/* Load part of the stitch project.
   Update PERMILLE with progress (0-1000).
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.  */
bool
stitch_image_data_loader::load_part (int *permille, const char **error,
                                     progress_info *progress)
{
  int y = m_curr_img / m_img->stitch->params.width;
  int x = m_curr_img % m_img->stitch->params.width;
  stitch_image &simg = m_img->stitch->images[y][x];
  if (!simg.img)
    {
      sub_task task (progress);
      if (progress)
        progress->set_task ("loading image header", 1);
      if (!simg.init_loader (error, progress))
        return false;
      *permille = 1000 * m_curr_img / (m_max_img + 1);
      if (!simg.img->allocate ())
        {
          *error = "out of memory";
          simg.img = NULL;
          return false;
        }
      return true;
    }
  int permille2;
  {
    sub_task task (progress);
    if (!simg.load_part (&permille2, error, progress))
      {
	simg.img = NULL;
	return false;
      }
  }
  *permille
      = 1000 * m_curr_img / (m_max_img + 1) + permille2 / (m_max_img + 1);
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
              memcpy (m_img->icc_profile, simg.img->icc_profile,
                      m_img->icc_profile_size);
            }
          m_img->xdpi = simg.img->xdpi;
          m_img->ydpi = simg.img->ydpi;
        }
      else
        {
          if (m_img->maxval != simg.img->maxval)
            {
              *error = "images in stitch project must all have the same bit "
                       "depth";
              return false;
            }
          if (m_img->xdpi != simg.img->xdpi || m_img->ydpi != simg.img->ydpi)
            {
              *error = "images in stitch project must all have the same DPI";
              return false;
            }
#if 1
          if (m_img->icc_profile_size != simg.img->icc_profile_size
              || memcmp (m_img->icc_profile, simg.img->icc_profile,
                         m_img->icc_profile_size))
            {
              *error = "images in stitch project must all have the same color "
                       "profile";
              return false;
            }
#endif
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

/* Initialize image loader for file NAME.
   PRELOAD_ALL is true if all images in a project should be preloaded.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use.  */
bool
image_data::init_loader (const char *name, bool preload_all,
                         const char **error, progress_info *progress,
                         demosaicing_t demosaic)
{
  assert (!loader);
  m_preload_all = preload_all;
  if (has_suffix (name, ".tif") || has_suffix (name, ".tiff"))
    loader = std::make_unique<tiff_image_data_loader> (this);
  else if (has_suffix (name, ".jpg") || has_suffix (name, ".jpeg"))
    loader = std::make_unique<jpg_image_data_loader> (this);
  else if (has_suffix (name, ".raw") || has_suffix (name, ".dng")
           || has_suffix (name, ".iiq") || has_suffix (name, ".nef")
           || has_suffix (name, ".cr2") || has_suffix (name, ".eip")
           || has_suffix (name, ".arw") || has_suffix (name, ".raf")
           || has_suffix (name, ".arq"))
    loader = std::make_unique<raw_image_data_loader> (this);
#ifdef HAVE_OPENJPEG
  else if (has_suffix (name, ".jp2") || has_suffix (name, ".j2k")
           || has_suffix (name, ".jpc") || has_suffix (name, ".jpf")
           || has_suffix (name, ".jpx"))
    loader = std::make_unique<jp2_image_data_loader> (this);
#endif
#ifdef HAVE_LIBPNG
  else if (has_suffix (name, ".png"))
    loader = std::make_unique<png_image_data_loader> (this);
#endif
  else if (has_suffix (name, ".csprj"))
    loader = std::make_unique<stitch_image_data_loader> (this, preload_all);
  if (!loader)
    {
      *error = "Unknown file extension";
      return false;
    }
  bool ret = loader->init_loader (name, error, progress, demosaic);
  if (!ret)
    loader = NULL;
  return ret;
}

/* Load part of the image.
   Update PERMILLE with progress (0-1000).
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.  */
bool
image_data::load_part (int *permille, const char **error,
                       progress_info *progress)
{
  assert (loader);
  bool ret = loader->load_part (permille, error, progress);
  if (!ret || *permille == 1000)
    {
      loader = NULL;
      /* If color profile is available, parse it.  */
      if (icc_profile)
        parse_icc_profile (progress);
      //printf ("%s %i %f %f\n", camera_model.c_str (), camera_model == "LS-9000", xdpi, ydpi);
      /* Set sensor size of Nikon camera. see if DPI looks sane.
         https://www.closeuphotography.com/scanner-nikkor-ed-lens
	 Scanner-Nikkor ED 100mm f/2.8 lens
		Part number: 9000ED: TB100-078, 8000ED: TB100-032
		Type: multi format film reproduction scanner lens
		Magnification range: 0.85x - 0.9x / 1.1x - 1.15x reverse, performs best in the range of 1.1 to 1.15x.
		Wavelength range: chromatic aberration strictly controlled from 435.84 nm (blue) to 852.11 nm (infrared)
		Reference wavelength: 546.07nm (e-line, green)
		Distortion: 0%  
		Focal length: 100mm
		Lens configuration: 14 elements in 6 groups + 2 protective elements including 6 low dispersion glass elements
		Fixed aperture: f/2.8 measured f/3.1 forward and f/3 in reverse on the 9000 type and f/3.0 and f/2.9 on the 8000 lens.
		Reference magnification: 0.8664x (Nikon spec)
		Working distance: 140mm at 1X
		Coverage: 56mm ⌀ image circle 
		Lens mount: none
		Accessory thread: none
		Source: Lens made in Japan
		Design includes sensor cover glass: Yes, CCD sensor coverglass
	  Models: Super COOLSCAN 8000 ED, LS-8000 ED, 9000 ED, LS-9000 ED 
		Type: Multi-format, 35mm, 16mm, 120/220 film scanner 
		System: Fixed optical system, movable media plane single-pass optical scanning system 
		Light type: R / G / B / IR four-color LED light source (LED is the cool in Coolscan)
		Planned production volume: 1,000 units per month
		CCD sensor: Sony linear CCD ILX133A 28 pin CerDIP
		CCD type: Tri-linear 3 x 10,000 pixel monochrome 
		CCD width: 61.2mm (58mm active)
		Maximum scan area: 56.9 x 83.7 max area for the 9000, 63.5mm x 88mm for the 8000.
		Manufacturers optical resolution: 4000 dpi 
		CCD maximum resolution: TBC
		Maximum optical resolution: TBC
		Production 8000 ED: 2001-2003
		Production 9000 ED: 2003-2010 
		Scanner street price 8000 ED: $2900 USD
		Full retail price 8000 ED: $2900
		Full retail price 9000 ED:$1900
		Country of origin: made in Japan
		Manufacturer: Nikon Japan
	       	*/
      if ((camera_model == "LS-9000" || camera_model == "LS-8000") && xdpi > 200 && xdpi <= 4000 && xdpi == ydpi)
	{
	  pixel_pitch = 5.800000 * 4000 / xdpi;
	  /* It seems that fill factor at 4000 DPI is approximately 2 at least in vertical resolution.
	     Lower resolutions can be either downscaled (which would make fill factor to converge to 1)
	     or sampled which makes it smaller.  Assume sampling.  */
	  sensor_fill_factor = 4 * ((xdpi * xdpi) / (4000.0 * 4000.0));
	  f_stop = 2.8;
	  wavelengths[2] =  466;
	  wavelengths[1] =  526;
	  wavelengths[0] =  653;
	  //printf ("Nikon scanner detected\n");
	}
      /* TODO: Support also 4800DPI lens. */
      if (camera_model == "PerfectionV700" || camera_model == "PerfectionV750"
          || camera_model == "PerfectionV800" || camera_model == "PerfectionV850"
          || camera_model == "Perfection V700/V750"
          || camera_model == "Perfection V800/V850")
	{
	  /* Total array length at about 56.8 mm. 6 line 122,400 pixel array,
	     the length of a line should be 122,400/6 or 20,400 pixels
	     f the pixels were of the 2.7um x 5.4 um variety that
	     seems in general use at NEC, then the active line length would be
	     55.080 mm and extra dark pixels and room for amplifiers can easily
	     account for the additional measured ~1.8mm.  */
	  pixel_pitch = 2.7 * 6400 / xdpi;
	  sensor_fill_factor = 8 * ((xdpi * xdpi) / (6400.0*6400));
	}
    }
  return ret;
}

/* Parse ICC profile for the image.
   PROGRESS is used for progress reporting.  */
bool
image_data::parse_icc_profile (progress_info *progress)
{
  if (!icc_profile)
    return true;
  cmsHPROFILE hInProfile
      = cmsOpenProfileFromMem (icc_profile, icc_profile_size);
  if (!hInProfile)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Failed to parse profile\n");
      if (progress)
        progress->resume_stdout ();
      return false;
    }
  if (!cmsIsMatrixShaper (hInProfile))
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Not a matrix ICC profile (do not use LUT profiles with mosaiced photos)!\n");
      if (progress)
        progress->resume_stdout ();
      cmsCloseProfile (hInProfile);
      return false;
    }
  if (cmsGetColorSpace (hInProfile) != cmsSigRgbData)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Non-RGB profiles are not supported by ColorScreen!\n");
      if (progress)
        progress->resume_stdout ();
      return false;
    }

  cmsProfileClassSignature cl = cmsGetDeviceClass (hInProfile);
  if (cl != cmsSigInputClass && cl != cmsSigDisplayClass
      && cl != cmsSigOutputClass && cl != cmsSigColorSpaceClass)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Only input, output, display and color space ICC "
                       "profiles are supported by ColorScreen!\n");
      if (progress)
        progress->resume_stdout ();
      return false;
    }

  cmsHTRANSFORM hTransform
      = cmsCreateTransform (hInProfile, TYPE_RGB_FLT, NULL, TYPE_XYZ_FLT,
                            INTENT_ABSOLUTE_COLORIMETRIC, 0);
  if (!hTransform)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Failed to do icc profile transform\n");
      if (progress)
        progress->resume_stdout ();
      return false;
    }
  float rgb_buffer[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  float xyz_buffer[4 * 3];
  cmsDoTransform (hTransform, rgb_buffer, xyz_buffer, 4);
  xyz_to_xyY (xyz_buffer[3] - xyz_buffer[0], xyz_buffer[4] - xyz_buffer[1],
              xyz_buffer[5] - xyz_buffer[2], &primary_red.x, &primary_red.y,
              &primary_red.Y);
  xyz_to_xyY (xyz_buffer[6] - xyz_buffer[0], xyz_buffer[7] - xyz_buffer[1],
              xyz_buffer[8] - xyz_buffer[2], &primary_green.x,
              &primary_green.y, &primary_green.Y);
  xyz_to_xyY (xyz_buffer[9] - xyz_buffer[0], xyz_buffer[10] - xyz_buffer[1],
              xyz_buffer[11] - xyz_buffer[2], &primary_blue.x, &primary_blue.y,
              &primary_blue.Y);
#if 0
  xyz r = xyY_to_xyz (primary_red.x, primary_red.y, primary_red.Y);
  printf ("%f %f %f   %f %f %f\n", primary_red.x, primary_red.y, primary_red.Y, r.x, r.y, r.z);
  r = xyY_to_xyz (primary_green.x, primary_green.y, primary_green.Y);
  printf ("%f %f %f   %f %f %f\n", primary_green.x, primary_green.y, primary_green.Y, r.x, r.y, r.z);
  r = xyY_to_xyz (primary_blue.x, primary_blue.y, primary_blue.Y);
  printf ("%f %f %f   %f %f %f\n", primary_blue.x, primary_blue.y, primary_blue.Y, r.x, r.y, r.z);
#endif

  cmsDeleteTransform (hTransform);
  double this_gamma = cmsDetectRGBProfileGamma (hInProfile, 0.001);
  if (this_gamma > 0)
    {
      // fprintf (stderr, "Gamma of ICC file %f\n", this_gamma);
      gamma = this_gamma;
    }
  else
    {
      // fprintf (stderr, "No gamma estimate found\n");
      cmsContext ContextID = cmsGetProfileContextID (hInProfile);
      cmsHPROFILE hXYZ = cmsCreateXYZProfileTHR (ContextID);
      if (!hXYZ)
        {
          if (progress)
            progress->pause_stdout ();
          fprintf (stderr, "Failed to create XYZ Profile HR\n");
          cmsCloseProfile (hInProfile);
          if (progress)
            progress->resume_stdout ();
          return false;
        }
      cmsHTRANSFORM xform = cmsCreateTransformTHR (
          ContextID, hInProfile, TYPE_RGB_DBL, hXYZ, TYPE_XYZ_DBL,
          INTENT_ABSOLUTE_COLORIMETRIC,
          cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC);
      if (!xform)
        {
          if (progress)
            progress->pause_stdout ();
          fprintf (stderr, "Failed to create profile transform\n");
          if (progress)
            progress->resume_stdout ();
          cmsCloseProfile (hInProfile);
          return false;
        }
      for (int channel = 0; channel < 3; channel++)
        {
          std::vector<std::array<cmsFloat64Number, 3>> rgb (maxval + 1);
          for (int i = 0; i <= maxval; i++)
            {
              rgb[i][0] = rgb[i][1] = rgb[i][2] = 0;
              rgb[i][channel] = (i + 0.5) / (maxval + 1);
            }
          std::vector<cmsCIEXYZ> XYZ (maxval + 1);
          cmsDoTransform (xform, rgb.data (), XYZ.data (), maxval + 1);
          luminosity_t max = (luminosity_t)XYZ[0].Y;
          for (int i = 1; i <= maxval; i++)
            if ((luminosity_t)XYZ[i].Y > max)
              max = (luminosity_t)XYZ[i].Y;
          to_linear[channel].reserve (maxval + 1);
          for (int i = 0; i <= maxval; i++)
            {
              to_linear[channel].push_back (XYZ[i].Y / max);
            }
        }
      cmsCloseProfile (hXYZ);
      cmsDeleteTransform (xform);
      gamma = 0;
    }
  cmsCloseProfile (hInProfile);
  return true;
}

/* Load image from file NAME.
   PRELOAD_ALL is true if all images in a project should be preloaded.
   On failure, set ERROR to the error message.
   PROGRESS is used for progress reporting.
   DEMOSAIC is the demosaicing algorithm to use.  */
bool
image_data::load (const char *name, bool preload_all, const char **error,
                  progress_info *progress, demosaicing_t demosaic)
{
  int permille;
  if (progress)
    progress->set_task ("loading image header", 1);
  if (!init_loader (name, preload_all, error, progress, demosaic))
    return false;

  if (progress)
    progress->set_task ("allocating memory", 1);
  if (!allocate ())
    {
      *error = "out of memory allocating image";
      loader = NULL;
      return false;
    }

  if (progress)
    progress->set_task ("loading", 1000);
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

/* Set DPI of the image to NEW_XDPI and NEW_YDPI.  */
void
image_data::set_dpi (coord_t new_xdpi, coord_t new_ydpi)
{
  xdpi = new_xdpi;
  ydpi = new_ydpi;
  if (stitch)
    stitch->set_dpi (new_xdpi, new_ydpi);
}

/* Return true if the image has RGB data.  */
bool
image_data::has_rgb () const
{
  if (stitch)
    return stitch->images[0][0].img->has_rgb ();
  return m_rgbdata != NULL;
}

/* Return true if the image has grayscale or infrared data.  */
bool
image_data::has_grayscale_or_ir () const
{
  if (stitch)
    return stitch->images[0][0].img->has_grayscale_or_ir ();
  return m_data != NULL;
}

/* Set dimensions of the image to W x H.
   If ALLOCATE_RGB is true, allocate RGB data.
   If ALLOCATE_GRAYSCALE is true, allocate grayscale data.  */
bool
image_data::set_dimensions (int w, int h, bool allocate_rgb,
                             bool allocate_grayscale)
{
  width = w;
  height = h;
  if (allocate_grayscale)
    {
      assert (!m_data);
      m_data = (gray *)MapAlloc::Alloc (width * height * sizeof (*m_data),
                                         "grayscale data");
      if (!m_data)
        {
          free (m_data);
          m_data = NULL;
          return false;
        }
    }
  if (allocate_rgb)
    {
      assert (!m_rgbdata);
      m_rgbdata = (pixel *)MapAlloc::Alloc (
          width * height * sizeof (*m_rgbdata), "RGB data");
      if (!m_rgbdata && m_data)
        {
          MapAlloc::Free (m_data);
          m_data = NULL;
	  return false;
        }
    }
  own = true;
  maxval = 65535;
  return true;
}

/* Save image to TIFF file FILENAME.  Intended mostly for debugging.
   PROGRESS is used for progress reporting.
   Return true on success.  */
bool
image_data::save_tiff (const char *filename, progress_info *progress)
{
  tiff_writer_params tp;
  const char *error = NULL;
  tp.filename = filename;
  tp.width = width;
  tp.height = height;
  tp.depth = 16;
  tp.icc_profile = icc_profile;
  tp.icc_profile_len = icc_profile_size;
  tp.xdpi = xdpi;
  tp.ydpi = ydpi;
  if (progress)
    progress->set_task ("Opening tiff file", 1);
  tiff_writer out (tp, &error);
  if (error)
    return false;
  if (progress)
    progress->set_task ("Writing tiff file", height);
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
          image_data::pixel p = get_rgb_pixel (x, y);
          out.put_pixel (x, p.r, p.g, p.b);
        }
      if (!out.write_rows (progress))
        return false;
      if (progress && progress->cancel_requested ())
        return false;
      if (progress)
        progress->inc_progress ();
    }
  return true;
}


/* Load EXIF metadata from file NAME.  */
void
image_data::load_exif (const char *name)
{
  try
    {
#if EXIV2_TEST_VERSION(0,27,99)
      Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open (name);
#else
      Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open (name);
#endif
      if (image.get () == 0)
        return;
      image->readMetadata ();

      Exiv2::ExifData &exifData = image->exifData ();
      if (exifData.empty ())
        return;

      Exiv2::ExifData::const_iterator it;

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Photo.FNumber"));
      if (it != exifData.end () && it->count ())
        f_stop = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (
          Exiv2::ExifKey ("Exif.Photo.FocalPlaneXResolution"));
      if (it != exifData.end () && it->count ())
        focal_plane_x_resolution
            = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (
          Exiv2::ExifKey ("Exif.Photo.FocalPlaneYResolution"));
      if (it != exifData.end () && it->count ())
        focal_plane_y_resolution
            = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Photo.FocalLength"));
      if (it != exifData.end () && it->count ())
        focal_length
            = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (
          Exiv2::ExifKey ("Exif.Photo.FocalLengthIn35mmFilm"));
      if (it != exifData.end () && it->count ())
#if EXIV2_TEST_VERSION(0,27,99)
        focal_length_in_35mm = it->toInt64 ();
#else
        focal_length_in_35mm = it->toLong ();
#endif

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.Model"));
      if (it != exifData.end () && it->count ())
        camera_model = it->value ().toString ();

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Photo.LensModel"));
      if (it != exifData.end () && it->count ())
        lens = it->value ().toString ();

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.XResolution"));
      if (it != exifData.end () && it->count ())
        exif_xdpi = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.YResolution"));
      if (it != exifData.end () && it->count ())
        exif_ydpi = it->toRational ().first / (double)it->toRational ().second;

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.ResolutionUnit"));
      if (it != exifData.end () && it->count ())
        {
#if EXIV2_TEST_VERSION(0,27,99)
          long unit = it->toInt64 ();
#else
          long unit = it->toLong ();
#endif
          if (unit == 3) // Centimeter
            {
              exif_xdpi *= 2.54;
              exif_ydpi *= 2.54;
            }
        }

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.Software"));
      if (it != exifData.end () && it->count ())
        software = it->value ().toString ();

      if (software.find ("VueScan") != std::string::npos && maxval == 65535)
        gamma = 1.0;

      it = exifData.findKey (Exiv2::ExifKey ("Exif.Image.Orientation"));
      if (it != exifData.end () && it->count ())
        {
#if EXIV2_TEST_VERSION(0,27,99)
          long orientation = it->toInt64 ();
#else
          long orientation = it->toLong ();
#endif
          switch (orientation)
            {
            case 1:
              rotation = 0;
              mirror = 0;
              break;
            case 2:
              rotation = 0;
              mirror = 1;
              break;
            case 3:
              rotation = 2;
              mirror = 0;
              break;
            case 4:
              rotation = 2;
              mirror = 1;
              break;
            case 5:
              rotation = 3;
              mirror = 1;
              break;
            case 6:
              rotation = 1;
              mirror = 0;
              break;
            case 7:
              rotation = 1;
              mirror = 1;
              break;
            case 8:
              rotation = 3;
              mirror = 0;
              break;
            }
        }
    }
  catch (Exiv2::Error &e)
    {
      // No metadata or file not found...
    }
}

#ifdef HAVE_OPENJPEG
bool
jp2_image_data_loader::init_loader (const char *name, const char **error,
                                    progress_info *, image_data::demosaicing_t)
{
  m_filename = name;

  opj_codec_t *l_codec = NULL;
  opj_image_t *l_image = NULL;
  opj_stream_t *l_stream = NULL;

  l_stream = opj_stream_create_default_file_stream (name, OPJ_TRUE);
  if (!l_stream)
    {
      *error = "failed to open JP2 stream";
      return false;
    }

  if (has_suffix (name, ".jp2"))
    l_codec = opj_create_decompress (OPJ_CODEC_JP2);
  else
    l_codec = opj_create_decompress (OPJ_CODEC_J2K);

  opj_set_error_handler (l_codec, opj_error_callback, NULL);
  opj_set_warning_handler (l_codec, opj_warning_callback, NULL);
  opj_set_info_handler (l_codec, opj_info_callback, NULL);

  opj_dparameters_t l_params;
  opj_set_default_decoder_parameters (&l_params);
  if (!opj_setup_decoder (l_codec, &l_params))
    {
      *error = "failed to setup JP2 decoder";
      opj_stream_destroy (l_stream);
      opj_destroy_codec (l_codec);
      return false;
    }

  if (!opj_read_header (l_stream, l_codec, &l_image))
    {
      *error = "failed to read JP2 header";
      opj_stream_destroy (l_stream);
      opj_destroy_codec (l_codec);
      return false;
    }

  m_img->width = l_image->x1 - l_image->x0;
  m_img->height = l_image->y1 - l_image->y0;
  m_img->maxval = (1 << l_image->comps[0].prec) - 1;

  if (l_image->numcomps == 1)
    {
      grayscale = true;
      rgb = false;
    }
  else if (l_image->numcomps == 3)
    {
      grayscale = false;
      rgb = true;
    }
  else if (l_image->numcomps == 4)
    {
      grayscale = true;
      rgb = true;
    }
  else
    {
      *error = "unsupported number of components in JP2 file";
      opj_image_destroy (l_image);
      opj_stream_destroy (l_stream);
      opj_destroy_codec (l_codec);
      return false;
    }

  opj_image_destroy (l_image);
  opj_stream_destroy (l_stream);
  opj_destroy_codec (l_codec);
  return true;
}

bool
jp2_image_data_loader::load_part (int *permille, const char **error,
                                  progress_info *)
{
  opj_dparameters_t l_params;
  opj_set_default_decoder_parameters (&l_params);

  opj_codec_t *l_codec = NULL;
  if (has_suffix (m_filename.c_str (), ".jp2"))
    l_codec = opj_create_decompress (OPJ_CODEC_JP2);
  else
    l_codec = opj_create_decompress (OPJ_CODEC_J2K);

  opj_stream_t *l_stream
      = opj_stream_create_default_file_stream (m_filename.c_str (), OPJ_TRUE);
  opj_image_t *l_image = NULL;

  if (!l_stream)
    {
      *error = "failed to open JP2 stream";
      if (l_codec)
        opj_destroy_codec (l_codec);
      return false;
    }

  if (!opj_setup_decoder (l_codec, &l_params)
      || !opj_read_header (l_stream, l_codec, &l_image)
      || !opj_decode (l_codec, l_stream, l_image)
      || !opj_end_decompress (l_codec, l_stream))
    {
      *error = "JP2 decoding failed";
      if (l_image)
        opj_image_destroy (l_image);
      if (l_stream)
        opj_stream_destroy (l_stream);
      if (l_codec)
        opj_destroy_codec (l_codec);
      return false;
    }

  int width = m_img->width;
  int height = m_img->height;

  if (l_image->numcomps == 1)
    {
      for (int y = 0; y < height; y++)
	{
	  image_data::gray *row = m_img->get_row (y);
	  if (row)
	    for (int x = 0; x < width; x++)
	      row[x] = l_image->comps[0].data[y * (uint64_t) width + x];
	}
    }
  else if (l_image->numcomps == 3)
    {
      for (int y = 0; y < height; y++)
	{
	  image_data::pixel *row = m_img->get_rgb_row (y);
	  if (row)
	    for (int x = 0; x < width; x++)
	      {
		row[x].r = l_image->comps[0].data[y * (uint64_t) width + x];
		row[x].g = l_image->comps[1].data[y * (uint64_t) width + x];
		row[x].b = l_image->comps[2].data[y * (uint64_t) width + x];
	      }
	}
    }
  else if (l_image->numcomps == 4)
    {
      for (int y = 0; y < height; y++)
	{
	  image_data::pixel *rgbrow = m_img->get_rgb_row (y);
	  image_data::gray *row = m_img->get_row (y);
	  for (int x = 0; x < width; x++)
	    {
	      if (rgbrow)
		{
		  rgbrow[x].r = l_image->comps[0].data[y * (uint64_t) width + x];
		  rgbrow[x].g = l_image->comps[1].data[y * (uint64_t) width + x];
		  rgbrow[x].b = l_image->comps[2].data[y * (uint64_t) width + x];
		}
	      if (row)
		row[x] = l_image->comps[3].data[y * (uint64_t) width + x];
	    }
	}
    }

  opj_image_destroy (l_image);
  opj_stream_destroy (l_stream);
  opj_destroy_codec (l_codec);

  *permille = 1000;
  return true;
}
#endif
#ifdef HAVE_LIBPNG
bool
png_image_data_loader::init_loader (const char *name, const char **error,
                                    progress_info *, image_data::demosaicing_t)
{
  m_filename = name;
  m_fp = fopen (name, "rb");
  if (!m_fp)
    {
      *error = "failed to open PNG file";
      return false;
    }

  unsigned char header[8];
  if (fread (header, 1, 8, m_fp) != 8 || png_sig_cmp (header, 0, 8))
    {
      *error = "not a PNG file";
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }

  png_structp png_ptr
      = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
    {
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }
  png_infop info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr)
    {
      png_destroy_read_struct (&png_ptr, NULL, NULL);
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }

  if (setjmp (png_jmpbuf (png_ptr)))
    {
      png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }

  png_init_io (png_ptr, m_fp);
  png_set_sig_bytes (png_ptr, 8);
  png_read_info (png_ptr, info_ptr);

  m_img->width = png_get_image_width (png_ptr, info_ptr);
  m_img->height = png_get_image_height (png_ptr, info_ptr);
  int color_type = png_get_color_type (png_ptr, info_ptr);
  int bit_depth = png_get_bit_depth (png_ptr, info_ptr);

  m_img->maxval = (1 << bit_depth) - 1;

  if (color_type == PNG_COLOR_TYPE_GRAY)
    {
      grayscale = true;
      rgb = false;
    }
  else if (color_type == PNG_COLOR_TYPE_RGB
           || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    {
      grayscale = false;
      rgb = true;
    }
  else if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
      grayscale = true;
      rgb = false;
    }
  else
    {
      *error = "unsupported PNG color type";
      png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }

  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
  fclose (m_fp);
  m_fp = NULL;
  return true;
}

bool
png_image_data_loader::load_part (int *permille, const char **error,
                                  progress_info *)
{
  m_fp = fopen (m_filename.c_str (), "rb");
  if (!m_fp)
    {
      *error = "failed to open PNG file";
      return false;
    }
  png_structp png_ptr
      = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info_ptr = png_create_info_struct (png_ptr);
  if (setjmp (png_jmpbuf (png_ptr)))
    {
      png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
      fclose (m_fp);
      m_fp = NULL;
      return false;
    }
  png_init_io (png_ptr, m_fp);
  png_read_info (png_ptr, info_ptr);

  int bit_depth = png_get_bit_depth (png_ptr, info_ptr);
  if (bit_depth < 8)
    png_set_packing (png_ptr);
  if (bit_depth == 16)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      png_set_swap (png_ptr);
#endif
    }

  png_read_update_info (png_ptr, info_ptr);

  int width = m_img->width;
  int height = m_img->height;
  int rowbytes = png_get_rowbytes (png_ptr, info_ptr);
  png_bytep *row_pointers = (png_bytep *)malloc (sizeof (png_bytep) * height);
  for (int y = 0; y < height; y++)
    row_pointers[y] = (png_byte *)malloc (rowbytes);

  png_read_image (png_ptr, row_pointers);

  int color_type = png_get_color_type (png_ptr, info_ptr);
  if (color_type == PNG_COLOR_TYPE_GRAY
      || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
      int channels = png_get_channels (png_ptr, info_ptr);
      for (int y = 0; y < height; y++)
        {
	  image_data::gray *dest_row = m_img->get_row (y);
          if (bit_depth == 16)
            {
              unsigned short *row = (unsigned short *)row_pointers[y];
	      if (dest_row)
		for (int x = 0; x < width; x++)
		  dest_row[x] = row[x * channels];
            }
          else
            {
              png_bytep row = row_pointers[y];
	      if (dest_row)
		for (int x = 0; x < width; x++)
		  dest_row[x] = row[x * channels];
            }
        }
    }
  else if (color_type == PNG_COLOR_TYPE_RGB
           || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    {
      int channels = png_get_channels (png_ptr, info_ptr);
      for (int y = 0; y < height; y++)
        {
	  image_data::pixel *dest_row = m_img->get_rgb_row (y);
          if (bit_depth == 16)
            {
              unsigned short *row = (unsigned short *)row_pointers[y];
	      if (dest_row)
		for (int x = 0; x < width; x++)
		  {
		    dest_row[x].r = row[x * channels];
		    dest_row[x].g = row[x * channels + 1];
		    dest_row[x].b = row[x * channels + 2];
		  }
            }
          else
            {
              png_bytep row = row_pointers[y];
	      if (dest_row)
		for (int x = 0; x < width; x++)
		  {
		    dest_row[x].r = row[x * channels];
		    dest_row[x].g = row[x * channels + 1];
		    dest_row[x].b = row[x * channels + 2];
		  }
            }
        }
    }

  for (int y = 0; y < height; y++)
    free (row_pointers[y]);
  free (row_pointers);
  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
  fclose (m_fp);
  m_fp = NULL;
  *permille = 1000;
  return true;
}
#endif
}
