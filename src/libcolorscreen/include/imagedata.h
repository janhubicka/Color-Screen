#ifndef IMAGEDATA_H
#define IMAGEDATA_H
#include <memory>
#include "dllpublic.h"
#include "base.h"
#include "color.h"
#include "progress-info.h"
#include "backlight-correction-parameters.h"
#include <string>
#include <vector>
#include <array>
namespace colorscreen
{

class image_data_loader;
class stitch_project;

/* Scanned image descriptor.  */
class image_data
{
public:
  /* Specify spectra or XYZ coordinates of color dyes used in the process.  */
  enum demosaicing_t
  {
    demosaic_default,
    demosaic_half,
    demosaic_monochromatic,
    demosaic_monochromatic_bayer_corrected,
    demosaic_linear,
    demosaic_VNG,
    demosaic_PPG,
    demosaic_AHD,
    demosaic_DCB,
    demosaic_DHT,
    demosaic_AAHD,
    demosaic_none,
    demosaic_max
  };
  demosaicing_t demosaic = demosaic_default;
  DLL_PUBLIC static const property_t demosaic_names[(int)demosaic_max];

  typedef uint16_t gray;
  struct pixel
  {
    gray r, g, b;
  };

  /* Grayscale scan API.  */
  inline gray
  get_pixel (uint32_t x, unsigned int y) const
  {
    if (colorscreen_checking)
      assert (x >= 0 && (int)x < width && y >= 0 && (int)y < height);
    return *(m_data + y * (uint64_t)width + x);
  }
  inline void
  put_pixel (uint32_t x, unsigned int y, gray val)
  {
    if (colorscreen_checking)
      assert ((int)x >= 0 && (int)x < width && y >= 0 && (int)y < height);
    *(m_data + y * (uint64_t)width + x) = val;
  }
  inline gray *
  get_row (uint32_t y)
  {
    if (colorscreen_checking)
      assert ((int)y >= 0 && (int)y < height);
    return m_data ? m_data + y * (uint64_t)width : nullptr;
  }
  inline const gray *
  get_row (uint32_t y) const
  {
    if (colorscreen_checking)
      assert ((int)y >= 0 && (int)y < height);
    return m_data ? m_data + y * (uint64_t)width : nullptr;
  }

  /* RGB scan API.  */
  inline pixel
  get_rgb_pixel (uint32_t x, unsigned int y) const
  {
    if (colorscreen_checking)
      assert ((int)x >= 0 && (int)x < width && (int)y >= 0 && (int)y < height);
    return *(m_rgbdata+ y * (uint64_t)width + x);
  }
  inline void
  put_rgb_pixel (uint32_t x, unsigned int y, pixel val)
  {
    if (colorscreen_checking)
      assert ((int)x >= 0 && (int)x < width && (int)y >= 0 && (int)y < height);
    *(m_rgbdata + y * (uint64_t)width + x) = val;
  }
  inline pixel *
  get_rgb_row (uint32_t y)
  {
    if (colorscreen_checking)
      assert ((int)y >= 0 && (int)y < height);
    return m_rgbdata ? m_rgbdata + y * (uint64_t)width : nullptr;
  }
  inline const pixel *
  get_rgb_row (uint32_t y) const
  {
    if (colorscreen_checking)
      assert ((int)y >= 0 && (int)y < height);
    return m_rgbdata ? m_rgbdata + y * (uint64_t)width : nullptr;
  }

  /* Raw data access (legacy/performance).  */
  inline gray *
  get_data_ptr ()
  {
    return m_data;
  }
  inline gray *
  get_data_ptr () const
  {
    return m_data;
  }
  inline pixel *
  get_rgb_data_ptr ()
  {
    return m_rgbdata;
  }
  inline pixel *
  get_rgb_data_ptr () const
  {
    return m_rgbdata;
  }

  void *icc_profile = nullptr;
  std::array<std::vector<luminosity_t>, 3> to_linear;

  DLL_PUBLIC image_data ();
  DLL_PUBLIC_EXP ~image_data ();
  /* Dimensions of image data.  */
  int width = 0, height = 0;
  /* Maximal value of the image data.  */
  int maxval = 0;
  uint32_t icc_profile_size = 0;
  /* Unique id of the image (used for caching).  */
  uint64_t id = 0;
  coord_t xdpi = 0, ydpi = 0;
  coord_t exif_xdpi = 0, exif_ydpi = 0;
  stitch_project *stitch = nullptr;

  /* Beginning of the viewport of stitched object.  */
  int xmin = 0, ymin = 0;

  /* Initialize loader for NAME.  Return true on success.
     If false is returned ERROR is initialized to error
     message.  */
  nodiscard_attr DLL_PUBLIC bool init_loader (const char *name, bool preload_all,
					      const char **error,
					      progress_info *progress = NULL,
					      demosaicing_t demosaic = demosaic_default);
  /* True if grayscale allocation is needed
     (used after init_loader and before load_part).  */
  nodiscard_attr DLL_PUBLIC bool allocate_grayscale ();
  /* True if rgb allocation is needed
     (used after init_loader and before load_part).  */
  nodiscard_attr DLL_PUBLIC bool allocate_rgb ();
  /* Load part of image. Initialize PERMILLE to status.
     If PERMILLE==1000 loading is finished.
     If false is returned ERROR is initialized.  */
  nodiscard_attr DLL_PUBLIC bool load_part (int *permille, const char **error,
					    progress_info *progress = NULL);

  /* Allocate memory.  */
  nodiscard_attr DLL_PUBLIC bool allocate ();
  /* Load image data from file with auto-detection.  */
  nodiscard_attr DLL_PUBLIC bool load (const char *name, bool preload_all, const char **error,
				       progress_info *progress = NULL,
				       demosaicing_t demosaic = demosaic_default);
  /* Set dimensions of the image.  This can be used to produce image_data
   * without loading it.  */
  nodiscard_attr DLL_PUBLIC void set_dimensions (int w, int h, bool allocate_rgb = false,
						 bool allocate_grayscale = false);
  DLL_PUBLIC bool save_tiff (const char *name, progress_info *progress = NULL);

  pure_attr DLL_PUBLIC bool has_rgb () const;
  pure_attr DLL_PUBLIC bool has_grayscale_or_ir () const;
  pure_attr inline int_image_area
  get_area () const
  {
    return int_image_area (0, 0, width, height);
  }

  xyY primary_red = { 0.6400, 0.3300, 0.2126 };
  xyY primary_green = { 0.3000, 0.6000, 0.7152 };
  xyY primary_blue = { 0.1500, 0.0600, 0.0722 };
  xyz whitepoint = { 0.95047, 1.0, 1.08883 };
  std::shared_ptr<backlight_correction_parameters> backlight_corr = nullptr;
  DLL_PUBLIC void set_dpi (coord_t xdpi, coord_t ydpi);
  /* Gamma, -2 if unknown.  */
  luminosity_t gamma = -2;
  /* Data about camera setup, all -2 if unknown.  */
  luminosity_t f_stop = -2;
  luminosity_t focal_plane_x_resolution = -2;
  luminosity_t focal_plane_y_resolution = -2;
  luminosity_t focal_length = -2;
  luminosity_t focal_length_in_35mm = -2;
  luminosity_t pixel_pitch = -2;
  luminosity_t sensor_fill_factor = -2;
  std::array<luminosity_t, 4> wavelengths = { -2, -2, -2, -2 };
  int rotation = -1;
  int mirror = -1;
  demosaicing_t demosaiced_by = demosaic_max;
  std::string camera_model;
  std::string lens;
  std::string software;
  DLL_PUBLIC void load_exif (const char *name);

private:
  std::unique_ptr<image_data_loader> loader;
  /* True if the data is owned by the structure.  */
  bool own = false;
  bool m_preload_all = false;

  bool parse_icc_profile (progress_info *);

  /* Grayscale scan.  */
  gray *m_data = nullptr;
  /* Optional color scan.  */
  pixel *m_rgbdata = nullptr;
};
}
#endif
