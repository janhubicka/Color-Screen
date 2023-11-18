#ifndef RENDER_H
#define RENDER_H
#include <math.h>
#include <assert.h>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "base.h"
#include "imagedata.h"
#include "color.h"
#include "spectrum-to-xyz.h"
#include "progress-info.h"
#include "sensitivity.h"
#include "backlight-correction.h"


/* Parameters of rendering algorithms.  */
struct DLL_PUBLIC render_parameters
{
  render_parameters()
  : gamma (2.2), output_gamma (-1), sharpen_radius (0), sharpen_amount (0), presaturation (1), saturation (1),
    brightness (1), collection_threshold (0.8), white_balance (1, 1, 1),
    mix_red (0.3), mix_green (0.1), mix_blue (1), backlight_temperature (6500),
    age(0),
    dye_balance (dye_balance_neutral),
    screen_blur_radius (0.5),
    color_model (color_model_none), output_profile (output_profile_sRGB), dark_point (0), scan_exposure (1),
    film_characteristics_curve (&film_sensitivity::linear_sensitivity), output_curve (NULL),
    backlight_correction (NULL), backlight_correction_black (0), invert (false),
    restore_original_luminosity (true), precise (true), tile_adjustments_width (0), tile_adjustments_height (0), tile_adjustments ()
  {
  }
#if 0
  render_parameters(render_parameters &from)
  : gamma (from.gamma), output_gamma (from.output_gamma), sharpen_radius (from.sharpen_radius), sharpen_amount (from.sharpen_amount), presaturation (from.presaturation), saturation (from.saturation),
    brightness (from.brightness), collection_threshold (from.collection_threshold), white_balance (from.white_balance),
    mix_red (from.mix_red), mix_green (from.mix_green), mix_blue (from.mix_blue), backlight_temperature (from.backlight_temperature),
    age(from.age),
    dye_balance (from.dye_balance),
    screen_blur_radius (from.screen_blur_radius),
    color_model (from.color_model), output_profile (from.output_profile), gray_min (from.gray_min), gray_max (from.gray_max),
    film_characteristics_curve (from.film_characteristics_curve), output_curve (from.output_curve),
    restore_original_luminosity (from.restore_original_luminosity), precise (from.precise)
  {
  }
  /* TODO: hack.  */
  render_parameters &operator=(const render_parameters &from)
  {
    memcpy (this, &from, sizeof (*this));
  }
#endif
  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).
     Only positive values makes sense; meaningful range is approx 0.01 to 10.  */
  luminosity_t gamma;
  /* Output gamma.  -1 means sRGB transfer curve.  */
  luminosity_t output_gamma;
  /* Radious (in pixels) and amount for unsharp-mask filter.  */
  luminosity_t sharpen_radius, sharpen_amount;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of sharpness).
     Only positive values makes sense; meaningful range is approx 0.1 to 10.  */
  luminosity_t presaturation;
  /* Saturation increase.  */
  luminosity_t saturation;
  /* Brightness adjustments.  */
  luminosity_t brightness;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;
  /* White balance adjustment in dye coordinates.  */
  color_t white_balance;
  /* Parameters used to turn RGB data to grayscale:
     mix_red,green and blue are relative weights.  */
  luminosity_t mix_red, mix_green, mix_blue;
  /* Temperature in K of backlight.  */
  luminosity_t backlight_temperature;
  static const int temperature_min = 2500;
  static const int temperature_max = 25000;

  /* Aging simulation (0 new dyes, 1 aged dyes).  */
  luminosity_t age;
  enum dye_balance_t
  {
    dye_balance_none,
    dye_balance_neutral,
    dye_balance_whitepoint,
    dye_balance_max
  };
  DLL_PUBLIC static const char *dye_balance_names [(int)dye_balance_max];
  /* How to balance dye colors.  */
  enum dye_balance_t dye_balance;
  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  enum color_model_t
    {
      color_model_none,
      color_model_scan,
      color_model_red,
      color_model_green,
      color_model_blue,
      color_model_max_separation,
      color_model_paget,
      color_model_miethe_goerz_reconstructed_wager,
      color_model_miethe_goerz_original_wager,
      color_model_dufay_manual,
      color_model_dufay_color_cinematography_xyY,
      color_model_dufay_color_cinematography_spectra,
      color_model_dufay1,
      color_model_dufay2,
      color_model_dufay3,
      color_model_dufay4,
      color_model_dufay5,
      color_model_autochrome,
      color_model_autochrome2,
      color_model_max
    };
  DLL_PUBLIC static const char *color_model_names [(int)color_model_max];
  /* If true apply color model of Finlay taking plate.  */
  enum color_model_t color_model;
  enum output_profile_t
    {
      output_profile_sRGB,
      output_profile_original,
      output_profile_max
    };
  output_profile_t output_profile;
  DLL_PUBLIC static const char *output_profile_names [(int)output_profile_max];
  /* After linearizing we apply (val - dark_point) * scan_exposure  */
  luminosity_t dark_point, scan_exposure;


  hd_curve *film_characteristics_curve;
  hd_curve *output_curve;
  class backlight_correction_parameters *backlight_correction;
  luminosity_t backlight_correction_black;

  /* True if negatuve should be inverted to positive.  */
  bool invert;

  /* Use characteristics curves to restore original luminosity.  */
  bool restore_original_luminosity;

  /* The following is used by interpolated rendering only.  */
  /* If true use precise data collection.  */
  bool precise;

  struct tile_adjustment
  {
    /* Multiplicative correction to stitch-project global scan_exposure.  */
    luminosity_t exposure;
    /* Additive correction to stitch-project dark point.  */
    luminosity_t dark_point;
    bool enabled;
    unsigned char x, y;
    constexpr tile_adjustment()
    : exposure (1), dark_point (0), enabled (true), x(0), y(0)
    {}
    bool operator== (tile_adjustment &other) const
    {
      return enabled == other.enabled
	     && dark_point == other.dark_point
	     && exposure == other.exposure;
    }
    bool operator!= (tile_adjustment &other) const
    {
      return !(*this == other);
    }
    void apply (render_parameters *p) const
    {
      p->dark_point = dark_point + exposure * p->dark_point;
      p->scan_exposure *= exposure;
    }
  };

  int tile_adjustments_width, tile_adjustments_height;
  std::vector<tile_adjustment> tile_adjustments;
  color_matrix get_dyes_matrix (bool *is_srgb, bool *spectrum_based, image_data *img);
  size_t get_icc_profile (void **buf, image_data *img);
  const tile_adjustment&
  get_tile_adjustment (stitch_project *stitch, int x, int y) const;
  tile_adjustment&
  get_tile_adjustment_ref (stitch_project *stitch, int x, int y);
  tile_adjustment&
  get_tile_adjustment (int x, int y);

  bool operator== (render_parameters &other) const
  {
    if (tile_adjustments.size () != other.tile_adjustments.size ()
	|| tile_adjustments_width != other.tile_adjustments_width
	|| tile_adjustments_height != other.tile_adjustments_height)
      return false;
    for (unsigned int i = 0; i < tile_adjustments.size (); i++)
      if (tile_adjustments[i] != other.tile_adjustments[i])
        return false;
    return gamma == other.gamma
	   && output_gamma == other.output_gamma
	   && sharpen_radius == other.sharpen_radius
	   && sharpen_amount == other.sharpen_amount
	   && presaturation == other.presaturation
	   && saturation == other.saturation
	   && brightness == other.brightness
	   && collection_threshold == other.collection_threshold
	   && mix_red == other.mix_red
	   && mix_green == other.mix_green
	   && mix_blue == other.mix_blue
	   && color_model == other.color_model
	   && age == other.age
	   && backlight_temperature == backlight_temperature
	   && dark_point == other.dark_point
	   && scan_exposure == other.scan_exposure
	   && screen_blur_radius == other.screen_blur_radius
    	   && dye_balance == other.dye_balance
	   && precise == other.precise
 	   && film_characteristics_curve == other.film_characteristics_curve
	   && restore_original_luminosity == other.restore_original_luminosity
 	   && output_curve == other.output_curve
	   && backlight_correction == other.backlight_correction
	   && backlight_correction_black == other.backlight_correction_black;
  }
  bool operator!= (render_parameters &other) const
  {
    return !(*this == other);
  }
  /* Set invert, exposure and dark_point for a given range of values
     in input scan.  Used to interpret old gray_range parameter
     and can be removed eventually.  */
  void set_gray_range (int gray_min, int gray_max, int maxval)
  {
    if (gray_min < gray_max)
      {
	luminosity_t min2 = apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
	luminosity_t max2 = apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
	scan_exposure = 1 / (max2 - min2);
	dark_point = min2;
	invert = false;
      }
    else if (gray_min > gray_max)
      {
	luminosity_t min2 = 1-apply_gamma ((gray_min + 0.5) / (luminosity_t)maxval, gamma);
	luminosity_t max2 = 1-apply_gamma ((gray_max + 0.5) / (luminosity_t)maxval, gamma);
	scan_exposure = 1 / (max2 - min2);
	dark_point = min2;
	invert = true;
      }
    printf ("gray_range %i %i exp %f dark %f\n", gray_min, gray_max, dark_point, scan_exposure);
  }
  void get_gray_range (int *min, int *max, int maxval)
  {
    if (!invert)
      {
       *min = invert_gamma (dark_point, gamma) * maxval + 0.5;
       *max = invert_gamma ((dark_point + 1) / scan_exposure, gamma) * maxval + 0.5;
      }
    else
      {
       *min = invert_gamma (1-dark_point, gamma) * maxval + 0.5;
       *max = invert_gamma (1-((dark_point + 1) / scan_exposure), gamma) * maxval + 0.5;
      }
    printf ("gray_range2 %i %i exp %f dark %f\n", *min, *max, dark_point, scan_exposure);
  }
  void set_tile_adjustments_dimensions (int w, int h);
private:
  static const bool debug = false;
};

/* Datastructure used to store information about dye luminosities.  */
struct rgbdata
{
  luminosity_t red, green, blue;
  rgbdata &operator+=(const luminosity_t other)
  {
    red += other;
    green += other;
    blue += other;
    return *this;
  }
  rgbdata &operator-=(const luminosity_t other)
  {
    red -= other;
    green -= other;
    blue -= other;
    return *this;
  }
  rgbdata &operator*=(const luminosity_t other)
  {
    red *= other;
    green *= other;
    blue *= other;
    return *this;
  }
  rgbdata &operator+=(const rgbdata other)
  {
    red += other.red;
    green += other.green;
    blue += other.blue;
    return *this;
  }
  rgbdata &operator-=(const rgbdata other)
  {
    red -= other.red;
    green -= other.green;
    blue -= other.blue;
    return *this;
  }
  rgbdata &operator*=(const rgbdata other)
  {
    red *= other.red;
    green *= other.green;
    blue *= other.blue;
    return *this;
  }
  luminosity_t &operator[](const int index)
  {
    switch (index)
    {
      case 0: return red;
      case 1: return green;
      case 2: return blue;
      default: __builtin_unreachable ();
    }
  }
};
inline rgbdata operator+(rgbdata lhs, luminosity_t rhs)
{
  lhs += rhs;
  return lhs;
}
inline rgbdata operator-(rgbdata lhs, luminosity_t rhs)
{
  lhs -= rhs;
  return lhs;
}
inline rgbdata operator*(rgbdata lhs, luminosity_t rhs)
{
  lhs *= rhs;
  return lhs;
}
inline rgbdata operator+(rgbdata lhs, rgbdata rhs)
{
  lhs += rhs;
  return lhs;
}
inline rgbdata operator-(rgbdata lhs, rgbdata rhs)
{
  lhs -= rhs;
  return lhs;
}
inline rgbdata operator*(rgbdata lhs, rgbdata rhs)
{
  lhs *= rhs;
  return lhs;
}

/* Helper for downscaling template for color rendering
   data += lum * scale.  */
inline void
account_rgb_pixel (rgbdata *data, rgbdata lum, luminosity_t scale)
{
  data->red += lum.red * scale;
  data->green += lum.green * scale;
  data->blue += lum.blue * scale;
}

/* Helper for downscaling template for grayscale rendering
   data += lum * scale.  */
inline void
account_pixel (luminosity_t *data, luminosity_t lum, luminosity_t scale)
{
  *data += lum * scale;
}

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class DLL_PUBLIC render
{
public:
  render (image_data &img, render_parameters &rparam, int dstmaxval)
  : m_img (img), m_params (rparam), m_gray_data_id (img.id), m_sharpened_data (NULL), m_sharpened_data_holder (NULL), m_maxval (img.data ? img.maxval : 65535), m_dst_maxval (dstmaxval),
    m_rgb_lookup_table (NULL), m_out_lookup_table (NULL), m_backlight_correction (NULL)
  {
    if (m_params.invert)
      {
	static synthetic_hd_curve c (10, safe_output_curve);
	m_params.output_curve = &c;
      }
    else
      m_params.output_curve = NULL;
  }
  ~render ();
  inline luminosity_t get_img_pixel (coord_t x, coord_t y);
  inline luminosity_t get_unadjusted_img_pixel (coord_t x, coord_t y);
  inline void get_img_rgb_pixel (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  inline luminosity_t sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2);
  inline luminosity_t fast_get_img_pixel (int x, int y);
    
  static const int num_color_models = render_parameters::color_model_max;
  enum render_type_t
  {
    render_type_original,
    render_type_preview_grid,
    render_type_realistic,
    render_type_interpolated,
    render_type_combined,
    render_type_predictive,
    render_type_fast
  };
  static luminosity_t *get_lookup_table (luminosity_t gamma, int maxval);
  static void release_lookup_table (luminosity_t *);
  inline void set_color (luminosity_t, luminosity_t, luminosity_t, int *, int *, int *);
  inline void set_linear_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *);
  inline void set_hdr_color (luminosity_t, luminosity_t, luminosity_t, luminosity_t *, luminosity_t *, luminosity_t *);
  inline luminosity_t get_data (int x, int y);
  inline luminosity_t get_unadjusted_data (int x, int y);
  inline luminosity_t adjust_luminosity_ir (luminosity_t);
  inline luminosity_t get_data_red (int x, int y);
  inline luminosity_t get_data_green (int x, int y);
  inline luminosity_t get_data_blue (int x, int y);
  inline luminosity_t get_unadjusted_data_red (int x, int y);
  inline luminosity_t get_unadjusted_data_green (int x, int y);
  inline luminosity_t get_unadjusted_data_blue (int x, int y);
  bool precompute_all (bool grayscale_needed, progress_info *progress);
  inline rgbdata
  get_rgb_pixel (int x, int y)
  {
    rgbdata d = {m_rgb_lookup_table [m_img.rgbdata[y][x].r],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].g],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].b]};
    if (m_backlight_correction)
      {
	d.red = m_backlight_correction->apply (d.red, x, y, backlight_correction_parameters::red);
	d.green = m_backlight_correction->apply (d.green, x, y, backlight_correction_parameters::green);
	d.blue = m_backlight_correction->apply (d.blue, x, y, backlight_correction_parameters::blue);
	/* TODO do inversion and film curves if requested.  */
      }
    d.red = (d.red - m_params.dark_point) * m_params.scan_exposure;
    d.green = (d.green - m_params.dark_point) * m_params.scan_exposure;
    d.blue = (d.blue - m_params.dark_point) * m_params.scan_exposure;
    return d;
  }

protected:
  void get_gray_data (luminosity_t *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);


  template<typename T, typename D, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  void process_line (T *data, int *pixelpos, luminosity_t *weights,
		     int xstart, int xend,
		     int width, int height,
		     int py, int yy,
		     bool y0, bool y1,
		     luminosity_t scale, luminosity_t yweight);

  template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
  void process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T val, luminosity_t scale, luminosity_t xweight, luminosity_t yweight);

  template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  __attribute__ ((__flatten__))
  bool downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *);

  /* Scanned image.  */
  image_data &m_img;
  /* Rendering parameters.
     Make local copy for performance reasons and also because render-tile releases rparam
     after constructing renderer.  */
  render_parameters m_params;
  /* ID of graydata computed.  */
  unsigned long m_gray_data_id;
  /* Sharpened data we render from.  */
  luminosity_t *m_sharpened_data;
  /* Wrapping class to cause proper destruction.  */
  class sharpened_data *m_sharpened_data_holder;
  /* Maximal value in m_data.  */
  int m_maxval;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input rgb channel values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_rgb_lookup_table;
  /* Translates back to gamma 2.  */
  luminosity_t *m_out_lookup_table;
  /* Color matrix.  */
  color_matrix m_color_matrix;

  backlight_correction *m_backlight_correction;

private:
  const bool debug = false;
};

typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;

/* Cubic interpolation helper.  */

static inline luminosity_t
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}
static inline vec_luminosity_t
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * (luminosity_t)x * (p2 - p0 +
			 (luminosity_t)x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      (luminosity_t)x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_unadjusted_data (int x, int y)
{
  /* TODO do inversion and film curves if requested.  */
  return m_sharpened_data [y * m_img.width + x];
}

inline luminosity_t
render::adjust_luminosity_ir (luminosity_t lum)
{
  return (lum - m_params.dark_point) * m_params.scan_exposure;
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_data (int x, int y)
{
  return adjust_luminosity_ir (get_unadjusted_data (x, y));
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_unadjusted_data_red (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].r];
  /* TODO do inversion and film curves if requested.  */
}

inline luminosity_t
render::get_unadjusted_data_green (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].g];
  /* TODO do inversion and film curves if requested.  */
}
inline luminosity_t
render::get_unadjusted_data_blue (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].b];
  /* TODO do inversion and film curves if requested.  */
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_data_red (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].r];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::red);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

inline luminosity_t
render::get_data_green (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].g];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::green);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

inline luminosity_t
render::get_data_blue (int x, int y)
{
  luminosity_t v = m_rgb_lookup_table [m_img.rgbdata[y][x].b];
  if (m_backlight_correction)
    {
      v = m_backlight_correction->apply (v, x, y, backlight_correction_parameters::blue);
    }
  v = (v - m_params.dark_point) * m_params.scan_exposure;
  /* TODO do inversion and film curves if requested.  */
  return v;
}

/* Compute color in linear HDR image.  */
inline void
render::set_linear_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
  r *= m_params.white_balance.red;
  g *= m_params.white_balance.green;
  b *= m_params.white_balance.blue;
#if 0
  if (m_spectrum_dyes_to_xyz)
    {
      /* At the moment all conversions are linear.
         Simplify the codegen here.  */
      if (1)
	abort ();
      else
	{
	  if (m_params.presaturation != 1)
	    {
	      presaturation_matrix m (m_params.presaturation);
	      m.apply_to_rgb (r, g, b, &r, &g, &b);
	    }
	  struct xyz c = m_spectrum_dyes_to_xyz->dyes_rgb_to_xyz (r, g, b);
	  r = c.x;
	  g = c.y;
	  b = c.z;
	}
    }
#endif
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  if (m_params.output_curve)
    {
      luminosity_t lum = r * rwght + g * gwght + b * bwght;
      luminosity_t lum2;
      lum2 = m_params.output_curve->apply (lum);
      if (lum != lum2)
	{
	  r *= lum2 / lum;
	  g *= lum2 / lum;
	  b *= lum2 / lum;
	}
    }

  *rr = r;
  *gg = g;
  *bb = b;
}
inline void
render::set_hdr_color (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
  luminosity_t r1, g1, b1;
  render::set_linear_hdr_color (r, g, b, &r1, &g1, &b1);
  *rr = invert_gamma (r1, m_params.output_gamma);
  *gg = invert_gamma (g1, m_params.output_gamma);
  *bb = invert_gamma (b1, m_params.output_gamma);
}

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval.  */
inline void
render::set_color (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb)
{
  set_linear_hdr_color (r, g, b, &r, &g, &b);
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}

#if 0
/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval
   combining color and luminosity information.  */

inline void
render::set_color_luminosity (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t l, int *rr, int *gg, int *bb)
{
  luminosity_t r1, g1, b1;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  m_color_matrix.apply_to_rgb (l, l, l, &r1, &g1, &b1);
  l = r1 * rwght + g1 * gwght + b1 * bwght;
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  l = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, l));
  luminosity_t gr = (r * rwght + g * gwght + b * bwght);
  if (gr <= 0.00001 || l <= 0.00001)
    r = g = b = l;
  else
    {
      gr = l / gr;
      r *= gr;
      g *= gr;
      b *= gr;
    }
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));

  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}
#endif

/* Determine grayscale value at a given position in the image.  */

inline luminosity_t
render::fast_get_img_pixel (int x, int y)
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline luminosity_t
render::get_unadjusted_img_pixel (coord_t xp, coord_t yp)
{
  luminosity_t val;

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  //int sx = xp, sy = yp;
  //luminosity_t rx = xp - sx, ry = yp - sy;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      vec_luminosity_t v1 = {get_unadjusted_data (sx-1, sy-1), get_unadjusted_data (sx, sy-1), get_unadjusted_data (sx+1, sy-1), get_unadjusted_data (sx+2, sy-1)};
      vec_luminosity_t v2 = {get_unadjusted_data (sx-1, sy-0), get_unadjusted_data (sx, sy-0), get_unadjusted_data (sx+1, sy-0), get_unadjusted_data (sx+2, sy-0)};
      vec_luminosity_t v3 = {get_unadjusted_data (sx-1, sy+1), get_unadjusted_data (sx, sy+1), get_unadjusted_data (sx+1, sy+1), get_unadjusted_data (sx+2, sy+1)};
      vec_luminosity_t v4 = {get_unadjusted_data (sx-1, sy+2), get_unadjusted_data (sx, sy+2), get_unadjusted_data (sx+1, sy+2), get_unadjusted_data (sx+2, sy+2)};
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      val = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
      return val;
    }
    return 0;
  return val;
}

inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp)
{
  return adjust_luminosity_ir (get_unadjusted_img_pixel (xp, yp));
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline flatten_attr void
render::get_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      luminosity_t rr, gg, bb;
      rr = cubic_interpolate (cubic_interpolate (get_unadjusted_data_red ( sx-1, sy-1), get_unadjusted_data_red (sx-1, sy), get_unadjusted_data_red (sx-1, sy+1), get_unadjusted_data_red (sx-1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_red ( sx-0, sy-1), get_unadjusted_data_red (sx-0, sy), get_unadjusted_data_red (sx-0, sy+1), get_unadjusted_data_red (sx-0, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_red ( sx+1, sy-1), get_unadjusted_data_red (sx+1, sy), get_unadjusted_data_red (sx+1, sy+1), get_unadjusted_data_red (sx+1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_red ( sx+2, sy-1), get_unadjusted_data_red (sx+2, sy), get_unadjusted_data_red (sx+2, sy+1), get_unadjusted_data_red (sx+2, sy+2), ry),
			      rx);
      gg = cubic_interpolate (cubic_interpolate (get_unadjusted_data_green ( sx-1, sy-1), get_unadjusted_data_green (sx-1, sy), get_unadjusted_data_green (sx-1, sy+1), get_unadjusted_data_green (sx-1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_green ( sx-0, sy-1), get_unadjusted_data_green (sx-0, sy), get_unadjusted_data_green (sx-0, sy+1), get_unadjusted_data_green (sx-0, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_green ( sx+1, sy-1), get_unadjusted_data_green (sx+1, sy), get_unadjusted_data_green (sx+1, sy+1), get_unadjusted_data_green (sx+1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_green ( sx+2, sy-1), get_unadjusted_data_green (sx+2, sy), get_unadjusted_data_green (sx+2, sy+1), get_unadjusted_data_green (sx+2, sy+2), ry),
			      rx);
      bb = cubic_interpolate (cubic_interpolate (get_unadjusted_data_blue ( sx-1, sy-1), get_unadjusted_data_blue (sx-1, sy), get_unadjusted_data_blue (sx-1, sy+1), get_unadjusted_data_blue (sx-1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_blue ( sx-0, sy-1), get_unadjusted_data_blue (sx-0, sy), get_unadjusted_data_blue (sx-0, sy+1), get_unadjusted_data_blue (sx-0, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_blue ( sx+1, sy-1), get_unadjusted_data_blue (sx+1, sy), get_unadjusted_data_blue (sx+1, sy+1), get_unadjusted_data_blue (sx+1, sy+2), ry),
			      cubic_interpolate (get_unadjusted_data_blue ( sx+2, sy-1), get_unadjusted_data_blue (sx+2, sy), get_unadjusted_data_blue (sx+2, sy+1), get_unadjusted_data_blue (sx+2, sy+2), ry),
			      rx);
      rr = m_backlight_correction->apply (rr, xp, yp, backlight_correction_parameters::red);
      gg = m_backlight_correction->apply (gg, xp, yp, backlight_correction_parameters::green);
      bb = m_backlight_correction->apply (bb, xp, yp, backlight_correction_parameters::blue);
      *r = (rr - m_params.dark_point) * m_params.scan_exposure;
      *g = (gg - m_params.dark_point) * m_params.scan_exposure;
      *b = (bb - m_params.dark_point) * m_params.scan_exposure;
      /* TODO do inversion and film curves if requested.  */
    }
  else
    {
      *r = 0;
      *g = 0;
      *b = 0;
      return;
    }
}

/* Sample square patch with center xc and yc and x1/y1, x2/y2 determining a coordinates
   of top left and top right corner.  */

luminosity_t
render::sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2)
{
  luminosity_t acc = 0, weights = 0;
  int xmin = std::max ((int)(std::min (std::min (std::min (xc - x1, xc + x1), xc - x2), xc + x2) - 0.5), 0);
  int xmax = std::min ((int)ceil (std::max(std::max (std::max (xc - x1, xc + x1), xc - x2), xc + x2) + 0.5), m_img.width - 1);
  /* If the resolution is too small, just sample given point.  */
  if (xmax-xmin < 2)
    return get_img_pixel (xc, yc);
  /* For bigger resolution we can sample few points in the square.  */
  if (xmax-xmin < 6)
    {
      /* Maybe this will give more reproducible results, but it is very slow.  */
      int samples = (sqrt (x1 * x1 + y1 * y1) + 0.5) * 2;
      luminosity_t rec = 1.0 / samples;
      if (!samples)
	return get_img_pixel (xc, yc);
      for (int y = -samples ; y <= samples; y++)
	for (int x = -samples ; x <= samples; x++)
	  {
	    luminosity_t w = 1 + (samples - abs (x) - abs (y));
	    if (w < 0)
	      continue;
	    acc += w * get_img_pixel (xc + (x1 * x + x2 * y) * rec, yc + (y1 * x + y2 * y) * rec);
	    weights += w;
	  }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      int ymin = std::max ((int)(std::min (std::min (std::min (yc - y1, yc + y1), yc - y2), yc + y2) - 0.5), 0);
      int ymax = std::min ((int)ceil (std::max(std::max (std::max (yc - y1, yc + y1), yc - y2), yc + y2) + 0.5), m_img.height - 1);
      matrix2x2<coord_t> base (x1, x2,
			      y1, y2);
      matrix2x2<coord_t> inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
	{
	  for (int x = xmin ; x <= xmax; x++)
	    {
	      coord_t cx = x+0.5 -xc;
	      coord_t cy = y+0.5 -yc;
	      coord_t ccx, ccy;
	      inv.apply_to_vector (cx, cy, &ccx, &ccy);
	      luminosity_t w = fabs (ccx) + fabs (ccy);

	      //if (w < 1)
		//printf ("%.1f ",w);
	      //else
		//printf ("    ",w);
	      if (w < 1)
		{
		  w = (1 - w);
		  acc += w * get_data (x, y);
		  weights += w;
		}
	    }
	    //printf ("\n");
	 }
    }
  if (weights)
    return acc / weights;
  return 0;
}

/* Helper for downscaling template.
   PIXEL is a pixel obtained from source image.  Account PIXEL*SCALE
   to DATA at coordinates (px,py), (px,py+1), (py+1, px) and (px+1,py+1)
   and distribute its value according to XWEIGHT and YWEIHT (here 0,0 means
   that pixel is accounted only to px,py.

   x0,x1,y0,y1 is used to disable updating for certain rows and columns to void
   accessing out of range data. 
  
   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
void
render::process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T pixel, luminosity_t scale, luminosity_t xweight, luminosity_t yweight)
{
  if (0)
    {
      assert (px >= (x0?0:-1) && px < (x1 ? width - 1 : width));
      assert (py >= (y0?0:-1) && py < (y1 ? height - 1: height));
    }
  
  if (x0)
    {
      if (y0)
	account_pixel (data + px + py * width, pixel, scale * (1 - yweight) * (1 - xweight));
      if (y1)
	account_pixel (data + px + (py + 1) * width, pixel, scale * yweight * (1 - xweight));
    }
  if (x1)
    {
      if (y0)
        account_pixel (data + px + (py * width) + 1, pixel, scale * (1 - yweight) * xweight);
      if (y1)
	account_pixel (data + px + (py + 1) * width + 1, pixel, scale * yweight * xweight);
    }
}

/* Helper for downscaling template.  Process line (in range XSTART..XEND) if input image with
   coordinate YY and account it (scaled by SCALE) to line of DATA with coordinate PY and PY+1.
   PY gets 1-yweight of the data, while py+1 get yweight of data. 
   PIXELPOS and WEIGHTS are precoputed scaling data for for x coordinate.

   WIDTH and HEIGHT are dimension of DATA pixmap.  */

template<typename T, typename D, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
void
render::process_line (T *data, int *pixelpos, luminosity_t *weights,
		      int xstart, int xend,
		      int width, int height,
		      int py, int yy,
		      bool y0, bool y1,
		      luminosity_t scale, luminosity_t yweight)
{
  int px = xstart;
  int xx = pixelpos[px];
  int stop;
  if (yy < 0 || yy >= m_img.height || xx >= m_img.width)
    return;
  if (px >= 0 && xx >= 0)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, false, true, y0, y1, pixel, scale, weights[px], yweight);
    }
  xx++;
  if (xx < 0)
    xx = 0;
  stop = pixelpos[px + 1];
  for (; xx < stop; xx++)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
    }
  px++;
  while (px <= xend)
    {
      T pixel = (((D *)this)->*get_pixel) (xx, yy);
      process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, true, y0, y1, pixel, scale, weights[px], yweight);
      stop = pixelpos[px + 1];
      xx++;
      for (; xx < stop; xx++)
	{
	  T pixel = (((D *)this)->*get_pixel) (xx, yy);
	  process_pixel<T,account_pixel> (data, width, height, px, py, true, false, y0, y1, pixel, scale, 0, yweight);
	}
      px++;
    }
   if (xx < m_img.width)
     {
       T pixel = (((D *)this)->*get_pixel) (xx, yy);
       process_pixel<T,account_pixel> (data, width, height, px - 1, py, true, false, y0, y1, pixel, scale, weights[px], yweight);
     }
}

/* Template for paralelized downscaling of image.
   GET_PIXEL is used to access input image which is of type T and ACCOUNT_PIXEL is used to account
   pixels to given position of DATA.
 
   DATA is an output pixmap with dimensions WIDTH*HEIGHT.
   pixelsize if size of output pixel inside of input image.
   X,Y are coordinates of the top left corner of the output image in the input image.  */

template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
bool
render::downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  memset (data, 0, sizeof (T) * width * height);

  if (pxstart > pxend)
    return true;

  if (progress)
    {
      int pystart = std::max (0, (int)(-y / pixelsize));
      int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
      progress->set_task ("downscaling", pyend - pystart + 1);
    }

  /* Precompute to which column of output image given colon of input image shold be accounted to.  */
  int *pixelpos = (int *)malloc (sizeof (int) * (width + 1));
  luminosity_t *weights = (luminosity_t *)malloc (sizeof (luminosity_t) * (width + 1));

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

#pragma omp parallel shared(progress,data,pixelsize,width,height,pixelpos,x,y,pxstart,pxend,weights) default (none)
  {
    luminosity_t scale = 1 / (pixelsize * pixelsize);
    int pystart = std::max (0, (int)(-y / pixelsize));
    int pyend = std::min (height - 1, (int)((m_img.height - y) / pixelsize));
#ifdef _OPENMP
    int tn = omp_get_thread_num ();
    int threads = omp_get_max_threads ();
#else
    int tn = 0;
    int threads = 1;
#endif
    int ystart = pystart + (pyend + 1 - pystart) * tn / threads;
    int yend = pystart + (pyend + 1 - pystart) * (tn + 1) / threads - 1;

    int py = ystart;
    int yy = ypixelpos(py);
    int stop;

    if (ystart > yend)
      goto end;
    if (py >= 0 && yy >= 0)
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, false, true, scale, weight(py));
    yy++;
    stop = std::min (ypixelpos(py + 1), m_img.height);
    for (; yy < stop; yy++)
      {
	process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
      }
    py++;
    if (progress)
      progress->inc_progress ();
    while (py <= yend && (!progress || !progress->cancel_requested ()))
      {
        process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, true, scale, weight (py));
	stop = std::min (ypixelpos(py + 1), m_img.height);
	yy++;
	for (; yy < stop; yy++)
	  {
	    process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
	  }
	py++;
	if (progress)
	  progress->inc_progress ();
      }
     if (yy < m_img.height)
       process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, false, scale, weight (py));
     end:;
  }

#undef ypixelpos
#undef weight
  free (pixelpos);
  free (weights);
  return !progress || !progress->cancelled ();
}
#endif
