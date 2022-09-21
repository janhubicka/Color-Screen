#ifndef RENDER_H
#define RENDER_H
#include <math.h>
#include <assert.h>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "imagedata.h"
#include "color.h"
#include "spectrum-to-xyz.h"

typedef float coord_t;

/* Parameters of rendering algorithms.  */
struct DLL_PUBLIC render_parameters
{
  render_parameters()
  : gamma (2.2), presaturation (1), saturation (1.5), brightness (1), collection_threshold (0.8),
    mix_gamma (2.2), mix_red (0.3), mix_green (0.1), mix_blue (1), backlight_temperature (6500),
    age(0),
    dye_balance (dye_balance_neutral),
    screen_blur_radius (1.3),
    color_model (color_model_none), gray_min (0), gray_max (255), precise (true),
    screen_compensation (true), adjust_luminosity (false)
  {}
  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).  */
  luminosity_t gamma;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of sharpness.  */
  luminosity_t presaturation;
  /* Saturation increase.  */
  luminosity_t saturation;
  /* Brightness adjustments.  */
  luminosity_t brightness;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;
  /* Parameters used to turn RGB data to grayscale:
     mix_gamma should be gamma of the scan, mix_red,green and blue
     are relative weights.  */
  luminosity_t mix_gamma, mix_red, mix_green, mix_blue;
  /* Temperature in K of backlight.  */
  luminosity_t backlight_temperature;
  /* Aging simulation (0 new dyes, 1 aged dyes).  */
  luminosity_t age;
  static const int temperature_min = 2500;
  static const int temperature_max = 25000;
  enum dye_balance_t
  {
    dye_balance_none,
    dye_balance_neutral,
    dye_balance_whitepoint,
    dye_balance_max
  };
  static const char *dye_balance_names [(int)dye_balance_max];
  /* How to balance dye colors.  */
  enum dye_balance_t dye_balance;
  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  enum color_model_t
    {
      color_model_none,
      color_model_red,
      color_model_green,
      color_model_blue,
      color_model_paget,
      color_model_duffay1,
      color_model_duffay2,
      color_model_duffay3,
      color_model_duffay4,
      color_model_duffay5,
      color_model_autochrome,
      color_model_autochrome2,
      color_model_max
    };
  static const char *color_model_names [(int)color_model_max];
  /* If true apply color model of Finlay taking plate.  */
  enum color_model_t color_model;
  /* Gray range to boot to full contrast.  */
  int gray_min, gray_max;

  /* The following is used by interpolated rendering only.  */
  /* If true use precise data collection.  */
  bool precise;
  /* If true try to compensate for screen.  */
  bool screen_compensation;
  /* If true use luminosity from scan.  */
  bool adjust_luminosity;

  bool operator== (render_parameters &other) const
  {
    return gamma == other.gamma
	   && presaturation == other.presaturation
	   && saturation == other.saturation
	   && brightness == other.brightness
	   && collection_threshold == other.collection_threshold
	   && mix_gamma == other.mix_gamma
	   && mix_red == other.mix_red
	   && mix_green == other.mix_green
	   && mix_blue == other.mix_blue
	   && color_model == other.color_model
	   && age == other.age
	   && backlight_temperature == backlight_temperature
	   && gray_min == other.gray_min
	   && gray_max == other.gray_max
	   && precise == other.precise
	   && screen_compensation == other.screen_compensation
	   && adjust_luminosity == other.adjust_luminosity;
  }
  bool operator!= (render_parameters &other) const
  {
    return !(*this == other);
  }
};

struct rgbdata
{
  luminosity_t red, green, blue;
};

inline void
account_rgb_pixel (rgbdata *data, rgbdata lum, luminosity_t scale)
{
  data->red += lum.red * scale;
  data->green += lum.green * scale;
  data->blue += lum.blue * scale;
}
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
  : m_img (img), m_params (rparam), m_spectrum_dyes_to_xyz (NULL), m_data (img.data), m_maxval (img.data ? img.maxval : 65535), m_dst_maxval (dstmaxval),
    m_lookup_table (NULL), m_rgb_lookup_table (NULL), m_out_lookup_table (NULL)
  {
  }
  ~render ();
  inline luminosity_t get_img_pixel (coord_t x, coord_t y);
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

protected:
  inline luminosity_t get_data (int x, int y);
  inline luminosity_t get_data_red (int x, int y);
  inline luminosity_t get_data_green (int x, int y);
  inline luminosity_t get_data_blue (int x, int y);
  inline void set_color_luminosity (luminosity_t, luminosity_t, luminosity_t, luminosity_t, int *, int *, int *);
  void precompute_all (bool duffay);
  void get_gray_data (luminosity_t *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize);
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize);


  template<typename T, typename D, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  void
  process_line (T *data, int *pixelpos, luminosity_t *weights,
		int xstart, int xend,
		int width, int height,
		int py, int yy,
		bool y0, bool y1,
		luminosity_t scale, luminosity_t yweight);

  template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
  void
  process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T val, luminosity_t scale, luminosity_t xweight, luminosity_t yweight);

  template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
  __attribute__ ((__flatten__))
  void downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize);

  /* Scanned image.  */
  image_data &m_img;
  /* Rendering parameters.  */
  render_parameters &m_params;
  /* If non-NULL used to turn spectrum dyes to XYZ.  */
  spectrum_dyes_to_xyz *m_spectrum_dyes_to_xyz;
  /* Grayscale we render from.  */
  unsigned short **m_data;
  /* Maximal value in m_data.  */
  int m_maxval;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input gray values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_lookup_table;
  /* Translates input rgb channel values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_rgb_lookup_table;
  /* Translates back to gamma 2.  */
  luminosity_t *m_out_lookup_table;
  /* Color matrix.  */
  color_matrix m_color_matrix;

private:
  inline rgbdata
  get_rgb_pixel (int x, int y)
  {
    rgbdata d = {m_rgb_lookup_table [m_img.rgbdata[y][x].r],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].g],
		 m_rgb_lookup_table [m_img.rgbdata[y][x].b]};
    return d;
  }
};

typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;

/* Cubic interpolation helper.  */

static inline luminosity_t
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * x * (p2 - p0 +
			 x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}
static inline vec_luminosity_t
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * x * (p2 - p0 +
			 x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_data (int x, int y)
{
  return m_lookup_table [m_data[y][x]];
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_data_red (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].r];
}

inline luminosity_t
render::get_data_green (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].g];
}

inline luminosity_t
render::get_data_blue (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].b];
}

#if 0
static inline luminosity_t
cap_color (luminosity_t val, luminosity_t weight, luminosity_t *diff, luminosity_t *cnt_neg, luminosity_t *cnt_pos)
{
  if (isnan (val))
    return 1;
  if (val < 0)
    {
      *cnt_neg += weight;
      *diff += val * weight;
      val = 0;
    }
  if (val > 1)
    {
      *cnt_pos += weight;
      *diff += (val - 1) * weight;
      val = 1;
    }
  return val;
}
#endif

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval.  */

inline void
render::set_color (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb)
{
  if (m_spectrum_dyes_to_xyz)
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
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  /* Fancy capping seems to look weird.  */
#if 0
  luminosity_t r2 =r, g2= g, b2 = b;
  luminosity_t r1 =r, g1= g, b1 = b;
  luminosity_t diff = 0;
  luminosity_t cnt_neg = 0;
  luminosity_t cnt_pos = 0;
  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
  if (fabs (diff) > 0.0001)
    {
      luminosity_t lum = r * rwght + g * gwght + b * bwght;
      if (lum + diff < 0.00001)
	r = g = b = 0;
      else if (lum + diff > 0.99999)
	r = g = b = 1;
      else while (fabs (diff) > 0.0001)
	{
	  if (diff > 0)
	    {
	      luminosity_t add = diff / (3 - cnt_pos);
	      if (r < 1)
		r += add;
	      if (g < 1)
		g += add;
	      if (b < 1)
		b += add;
	    }
	  if (diff < 0)
	    {
	      luminosity_t add = diff / (3 - cnt_neg);
	      if (r > 0)
		r += add;
	      if (g > 0)
		g += add;
	      if (b > 0)
		b += add;
	    }
	  diff = 0;
	  cnt_neg = 0;
	  cnt_pos = 0;
	  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
	  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
	  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
	}
    }
#endif
  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}

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

/* Determine grayscale value at a given position in the image.  */

inline luminosity_t
render::fast_get_img_pixel (int x, int y)
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Like modf but always round down.  */
static inline float
my_modf (float x, int *ptr)
{
  float f = floorf (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}
static inline double
my_modf (double x, int *ptr)
{
  float f = floorf (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}

static inline long long
nearest_int (float x)
{
  return roundf (x);
}
static inline long long
nearest_int (double x)
{
  return round (x);
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp)
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
      vec_luminosity_t v1 = {get_data (sx-1, sy-1), get_data (sx, sy-1), get_data (sx+1, sy-1), get_data (sx+2, sy-1)};
      vec_luminosity_t v2 = {get_data (sx-1, sy-0), get_data (sx, sy-0), get_data (sx+1, sy-0), get_data (sx+2, sy-0)};
      vec_luminosity_t v3 = {get_data (sx-1, sy+1), get_data (sx, sy+1), get_data (sx+1, sy+1), get_data (sx+2, sy+1)};
      vec_luminosity_t v4 = {get_data (sx-1, sy+2), get_data (sx, sy+2), get_data (sx+1, sy+2), get_data (sx+2, sy+2)};
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      val = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
#if 0
  val = cubic_interpolate (cubic_interpolate (get_data ( sx-1, sy-1), get_data (sx-1, sy), get_data (sx-1, sy+1), get_data (sx-1, sy+2), ry),
			   cubic_interpolate (get_data ( sx-0, sy-1), get_data (sx-0, sy), get_data (sx-0, sy+1), get_data (sx-0, sy+2), ry),
			   cubic_interpolate (get_data ( sx+1, sy-1), get_data (sx+1, sy), get_data (sx+1, sy+1), get_data (sx+1, sy+2), ry),
			   cubic_interpolate (get_data ( sx+2, sy-1), get_data (sx+2, sy), get_data (sx+2, sy+1), get_data (sx+2, sy+2), ry),
			   rx);
#endif
      return val;
    }
    return 0;
  return val;
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline void
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
      *r = cubic_interpolate (cubic_interpolate (get_data_red ( sx-1, sy-1), get_data_red (sx-1, sy), get_data_red (sx-1, sy+1), get_data_red (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx-0, sy-1), get_data_red (sx-0, sy), get_data_red (sx-0, sy+1), get_data_red (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx+1, sy-1), get_data_red (sx+1, sy), get_data_red (sx+1, sy+1), get_data_red (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx+2, sy-1), get_data_red (sx+2, sy), get_data_red (sx+2, sy+1), get_data_red (sx+2, sy+2), ry),
			      rx);
      *g = cubic_interpolate (cubic_interpolate (get_data_green ( sx-1, sy-1), get_data_green (sx-1, sy), get_data_green (sx-1, sy+1), get_data_green (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx-0, sy-1), get_data_green (sx-0, sy), get_data_green (sx-0, sy+1), get_data_green (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx+1, sy-1), get_data_green (sx+1, sy), get_data_green (sx+1, sy+1), get_data_green (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx+2, sy-1), get_data_green (sx+2, sy), get_data_green (sx+2, sy+1), get_data_green (sx+2, sy+2), ry),
			      rx);
      *b = cubic_interpolate (cubic_interpolate (get_data_blue ( sx-1, sy-1), get_data_blue (sx-1, sy), get_data_blue (sx-1, sy+1), get_data_blue (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx-0, sy-1), get_data_blue (sx-0, sy), get_data_blue (sx-0, sy+1), get_data_blue (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx+1, sy-1), get_data_blue (sx+1, sy), get_data_blue (sx+1, sy+1), get_data_blue (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx+2, sy-1), get_data_blue (sx+2, sy), get_data_blue (sx+2, sy+1), get_data_blue (sx+2, sy+2), ry),
			      rx);
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

template<typename T, void (*account_pixel) (T *, T, luminosity_t)>
void
render::process_pixel (T *data, int width, int height, int px, int py, bool x0, bool x1, bool y0, bool y1, T pixel, luminosity_t scale, luminosity_t xweight, luminosity_t yweight)
{
  // assert (px >= (x0?0:-1) && px < (x1 ? width - 1 : width));
  // assert (py >= (y0?0:-1) && py < (y1 ? height - 1: height));
  
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

template<typename D, typename T, T (D::*get_pixel) (int x, int y), void (*account_pixel) (T *, T, luminosity_t)>
void
render::downscale (T *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{
  int pxstart = std::max (0, (int)(-x / pixelsize));
  int pxend = std::min (width - 1, (int)((m_img.width - x) / pixelsize));

  memset (data, 0, sizeof (T) * width * height);

  if (pxstart > pxend)
    return;

  int *pixelpos = (int *)malloc (sizeof (int) * width + 1);
  luminosity_t *weights = (luminosity_t *)malloc (sizeof (luminosity_t) * width + 1);

  for (int px = pxstart; px <= pxend + 1; px++)
    {
      coord_t ix = x + pixelsize * px;
      int xx = floor (ix);
      pixelpos[px] = std::min (xx, m_img.width);
      weights[px] = 1 - (ix - xx);
    }

#define ypixelpos(p) ((int)floor (y + pixelsize * (p)))
#define weight(p) (1 - (y + pixelsize * (p) - ypixelpos (p)))

#pragma omp parallel shared(data,pixelsize,width,height,pixelpos,x,y,pxstart,pxend,weights) default (none)
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
      process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
    py++;
    while (py <= yend)
      {
        process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, true, scale, weight (py));
	stop = std::min (ypixelpos(py + 1), m_img.height);
	yy++;
	for (; yy < stop; yy++)
	  process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py, yy, true, false, scale, 0);
	py++;
      }
     if (yy < m_img.height)
       process_line<T, D, get_pixel, account_pixel> (data, pixelpos, weights, pxstart, pxend, width, height, py - 1, yy, true, false, scale, weight (py));
     end:;
  }

#undef ypixelpos
#undef weight
  free (pixelpos);
  free (weights);
}
#endif
