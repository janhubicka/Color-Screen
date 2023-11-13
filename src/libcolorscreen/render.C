#include <cassert>
#include "include/render.h"
#include "lru-cache.h"
#include "include/sensitivity.h"
#include "gaussian-blur.h"
#include "sharpen.h"
#include "mapalloc.h"

class lru_caches lru_caches;
std::atomic_ulong lru_caches::time;


/* A wrapper class around m_sharpened_data which handles allocation and dealocation.
   This is needed for the cache.  */
class sharpened_data
{
public:
  luminosity_t *m_data;
  sharpened_data (int width, int height);
  ~sharpened_data();
};
sharpened_data::sharpened_data (int width, int height)
{
   m_data = (luminosity_t *)MapAlloc::Alloc (width * height * sizeof (luminosity_t), "HDR data");
}
sharpened_data::~sharpened_data ()
{
  if (m_data)
    MapAlloc::Free (m_data);
  m_data = NULL;
}

namespace
{

/* Lookup table translates raw input data into linear values.  */
struct lookup_table_params
{
  /* image_data are in range 0...img_maxval.  */
  int maxval;
  /* Input data are assumed to have gamma.  Inverse of gamma is applied to
     get linear data.  */
  luminosity_t gamma;
  /* Dark point is subtracted from linear data and then the result is multiplied by scan_exposure.  */
  luminosity_t dark_point, scan_exposure;
  /* True if we should invert positive to negative.  */
  bool invert;
  /* Characteristic curve.  TODO: Not really implemented  */
  hd_curve *film_characteristic_curve;

  /* True if curve should be inverted.  */
  bool restore_original_luminosity;

  bool
  operator==(lookup_table_params &o)
  {
    return maxval == o.maxval
	   && gamma == o.gamma
	   && dark_point == o.dark_point
	   && scan_exposure == o.scan_exposure
	   && invert == o.invert
	   /* TODO: Invent cache IDs for curves!
	      Pointer compare may not be safe if curve is released.  */
	   && film_characteristic_curve == o.film_characteristic_curve
	   && restore_original_luminosity == o.restore_original_luminosity;
  }
};

luminosity_t *
get_new_lookup_table (struct lookup_table_params &p, progress_info *)
{
  luminosity_t *lookup_table = new luminosity_t[p.maxval + 1];
  luminosity_t gamma = std::min (std::max (p.gamma, (luminosity_t)0.0001), (luminosity_t)100.0);
  luminosity_t mul = (luminosity_t)1 / p.maxval;

  luminosity_t dark_point = p.dark_point;
  luminosity_t scan_exposure = p.scan_exposure;

  if (!p.invert)
    {
      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = (apply_gamma ((i + 0.5) * mul, gamma) - dark_point) * scan_exposure;
    }
  else if (p.restore_original_luminosity)
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();

      // TODO: For stitching exposure should be really inside
      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = s.unapply (apply_gamma ((i + 0.5) * mul, gamma) - dark_point) * scan_exposure;
    }
  else
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();

      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = s.apply (apply_gamma ((i + 0.5) * mul, gamma) - dark_point) * scan_exposure;
    }
  return lookup_table;
}


/* Output lookup table takes linear r,g,b values in range 0...65536
   and outputs r,g,b values in sRGB gamma curve in range 0...maxval.  */
struct out_lookup_table_params
{
  int maxval;
  luminosity_t output_gamma;
  bool
  operator==(out_lookup_table_params &o)
  {
    return maxval == o.maxval
	   && output_gamma == o.output_gamma;
  }
};

luminosity_t *
get_new_out_lookup_table (struct out_lookup_table_params &p, progress_info *)
{
  luminosity_t *lookup_table = new luminosity_t[65536];
  luminosity_t gamma = p.output_gamma;
  int maxval = p.maxval;

  for (int i = 0; i < 65536; i++)
    lookup_table[i] = invert_gamma ((i + (luminosity_t)0.5) / 65535, gamma) * maxval;

  return lookup_table;
}

/* To improve interactive response we cache conversion tables.  */
static lru_cache <lookup_table_params, luminosity_t, get_new_lookup_table, 4> lookup_table_cache ("in lookup tables");
static lru_cache <out_lookup_table_params, luminosity_t, get_new_out_lookup_table, 4> out_lookup_table_cache ("out lookup tables");


struct graydata_params
{
  /* Pointers in image_data may become stale if image is freed. Use ID
     to check cache entries.  */
  unsigned long image_id;
  image_data *img;
  /* Gamma and weights of individual channels.  */
  luminosity_t gamma, red, green, blue;
  bool
  operator==(graydata_params &o)
  {
    return image_id == o.image_id
	   && gamma == o.gamma
	   && red == o.red
	   && green == o.green
	   && blue == o.blue;
  }
};

struct gray_data_tables
{
  luminosity_t *rtable;
  luminosity_t *gtable;
  luminosity_t *btable;
  backlight_correction *correction;
};

inline gray_data_tables
compute_gray_data_tables (struct graydata_params &p, luminosity_t *in_table, progress_info *progress)
{
  gray_data_tables ret;
  luminosity_t red = p.red;
  luminosity_t green = p.green;
  luminosity_t blue = p.blue;
  luminosity_t sum = (red < 0 ? 0 : red) + (green < 0 ? 0 : green) + (blue < 0 ? 0 : blue);
  luminosity_t gamma = p.gamma;
  luminosity_t maxval_inv = 1 / (luminosity_t)p.img->maxval;

  if (!sum)
    {
      sum = 1;
      green = 1;
      blue = red = 0;
    }
  red /= sum;
  green /= sum;
  blue /= sum;

  ret.rtable = (luminosity_t *)malloc (sizeof (luminosity_t) * (p.img->maxval + 1));
  ret.gtable = (luminosity_t *)malloc (sizeof (luminosity_t) * (p.img->maxval + 1));
  ret.btable = (luminosity_t *)malloc (sizeof (luminosity_t) * (p.img->maxval + 1));
  ret.correction = NULL;
  for (int i = 0; i <= p.img->maxval; i++)
    {
      luminosity_t l = apply_gamma (i * maxval_inv, gamma);
      if (l < 0 || l > 1)
	abort ();
      ret.rtable[i] = l * red;
      ret.gtable[i] = l * green;
      ret.btable[i] = l * blue;
    }
  return ret;
}

inline void
free_gray_data_tables (gray_data_tables &t)
{
  free (t.rtable);
  free (t.gtable);
  free (t.btable);
}

inline luminosity_t
compute_gray_data (gray_data_tables &t, int width, int height, int x, int y, int r, int g, int b)
{
  luminosity_t l1 = t.rtable[r];
  luminosity_t l2 = t.gtable[g];
  luminosity_t l3 = t.btable[b];
  /* TODO: We should make specific tables so addition works.  */
  if (t.correction)
    {
      l1 = t.correction->apply (l1, width, height, x, y, backlight_correction::red);
      l2 = t.correction->apply (l2, width, height, x, y, backlight_correction::green);
      l3 = t.correction->apply (l3, width, height, x, y, backlight_correction::blue);
    }
  luminosity_t val = l1 + l2 + l3;
  return /*std::max (std::min (val, (luminosity_t)1.0), (luminosity_t)0.0)*/ val;
}

struct sharpen_params
{
  luminosity_t radius;
  luminosity_t amount;
  unsigned long gray_data_id;
  /* TODO: Add comparator.  */
  backlight_correction *backlight;

  unsigned short **gray_data;
  luminosity_t *lookup_table;
  unsigned long lookup_table_id;
  int width;
  int height;
  bool
  operator==(sharpen_params &o)
  {
    return radius == o.radius
	   && amount == o.amount
	   && gray_data_id == o.gray_data_id
	   && lookup_table_id == o.lookup_table_id;
  }
};

struct gray_and_sharpen_params
{
  graydata_params gp;
  sharpen_params sp;
  bool
  operator==(gray_and_sharpen_params &o)
    {
      return gp == o.gp && sp == o.sp;
    }
};

/* Helper for sharpening template for images with gray data.  */
inline luminosity_t
getdata_helper (unsigned short **graydata, int x, int y, int, luminosity_t *table)
{
  luminosity_t v = table[graydata[y][x]];
  /* TODO: Implement correction for IR.  */
#if 0
  if (t.correction)
    {
      v = t.corretion->apply (v, img->width, img->height, x, y, backlight_correction::ir);
      v = v * m_params.scan_exposure - m_params.dark_point;
      /* TODO do inversion and film curves if requested.  */
    }
#endif
  return v;
}
/* Helper for sharpening template for images with RGB data only.  */
inline luminosity_t
getdata_helper2 (image_data *img, int x, int y, int, gray_data_tables *t)
{
  luminosity_t val = compute_gray_data (*t, img->width, img->height, x, y, img->rgbdata[y][x].r, img->rgbdata[y][x].g, img->rgbdata[y][x].b);
  return val;
}
sharpened_data *
get_new_gray_sharpened_data (struct gray_and_sharpen_params &p, progress_info *progress)
{
  sharpened_data *ret = new sharpened_data (p.sp.width, p.sp.height);
  if (!ret)
    return NULL;
  luminosity_t *out = ret->m_data;
  if (!out)
    {
      delete ret;
      return NULL;
    }

  bool ok;
  if (p.sp.gray_data)
    ok = sharpen<luminosity_t, unsigned short **, luminosity_t *, getdata_helper> (out, p.sp.gray_data, p.sp.lookup_table, p.sp.width, p.sp.height, p.sp.radius, p.sp.amount, progress);
  else
    {
      gray_data_tables t = compute_gray_data_tables (p.gp, p.sp.lookup_table, progress);
      t.correction = p.sp.backlight;
      ok = sharpen<luminosity_t, image_data *, gray_data_tables *, getdata_helper2> (out, p.gp.img, &t, p.sp.width, p.sp.height, p.sp.radius, p.sp.amount, progress);
      free_gray_data_tables (t);
    }
  if (!ok)
    {
      delete ret;
      return NULL;
    }
  return ret;
}
static lru_cache <gray_and_sharpen_params, sharpened_data, get_new_gray_sharpened_data, 1> gray_and_sharpened_data_cache ("gray and sharpened data");

}

bool
render::precompute_all (bool grayscale_needed, progress_info *progress)
{
  /* We used to produce data with corrections applied, but we do that now later.  */
  bool cor = m_params.backlight_correction || 1;
  lookup_table_params par = {m_img.maxval, m_params.gamma,
			     !cor ? m_params.dark_point : 0, !cor ? m_params.scan_exposure : 1, !cor ? m_params.invert : false,
			     !cor ? m_params.film_characteristics_curve : 0, !cor ? m_params.restore_original_luminosity : false};
  unsigned long lookup_table_id;
  luminosity_t *lookup_table = lookup_table_cache.get (par, progress, &lookup_table_id);
  if (!lookup_table)
    return false;
  if (m_img.rgbdata)
    {
      /* TODO: check if the other maxval is correct: does 256 bit image display well?  */
      lookup_table_params rgb_par = {m_img.maxval, m_params.gamma,
				     !cor ? m_params.dark_point : 0, !cor ? m_params.scan_exposure : 1, !cor ? m_params.invert : false,
				     !cor ? m_params.film_characteristics_curve : 0, !cor ? m_params.restore_original_luminosity : false};
      m_rgb_lookup_table = lookup_table_cache.get (rgb_par, progress);
      if (!m_rgb_lookup_table)
	{
	  if (lookup_table)
	    lookup_table_cache.release (lookup_table);
	  return false;
	}
    }
  out_lookup_table_params out_par = {m_dst_maxval, m_params.output_gamma};
  m_out_lookup_table = out_lookup_table_cache.get (out_par, progress);

  if (grayscale_needed)
    {
      gray_and_sharpen_params p = {{m_img.id, &m_img, m_params.gamma, m_params.mix_red, m_params.mix_green, m_params.mix_blue},
				   {m_params.sharpen_radius, m_params.sharpen_amount, 0, m_params.backlight_correction, m_img.data, lookup_table, lookup_table_id, m_img.width, m_img.height}};
      m_sharpened_data_holder = gray_and_sharpened_data_cache.get (p, progress, &m_gray_data_id);
      if (!m_sharpened_data_holder)
	{
	  if (lookup_table)
	    lookup_table_cache.release (lookup_table);
	  return false;
	}
      m_sharpened_data = m_sharpened_data_holder->m_data;
    }
  if (lookup_table)
    lookup_table_cache.release (lookup_table);

  color_matrix color;
  if (m_params.presaturation != 1)
    {
      presaturation_matrix m (m_params.presaturation);
      color = m * color;
    }
  if (m_params.output_profile != render_parameters::output_profile_original)
    {
      /* Matrix converting dyes either to XYZ or sRGB.  */
      bool spectrum_based;
      bool is_srgb;
      color_matrix dyes = m_params.get_dyes_matrix (&is_srgb, &spectrum_based);
      if (is_srgb)
	color = dyes * color;
      else
	{
	  color = dyes * color;
	  if (m_params.backlight_temperature != 6500 && !spectrum_based)
	    {
	      xyz whitepoint = spectrum_dyes_to_xyz::temperature_xyz (m_params.backlight_temperature);
	      xyz white;
	      srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
	      for (int i = 0; i < 4; i++)
		{
		  color.m_elements[0][i] *= whitepoint.x / white.x;
		  color.m_elements[1][i] *= whitepoint.y / white.y;
		  color.m_elements[2][i] *= whitepoint.z / white.z;
		}
	    }
	  xyz_srgb_matrix m2;
	  color = m2 * color;
	}
      if (m_params.saturation != 1)
	{
	  saturation_matrix m (m_params.saturation);
	  color = m * color;
	}
    }
  color = color * m_params.brightness;
  m_color_matrix = color;
  return true;
}

render::~render ()
{
  if (m_out_lookup_table)
    out_lookup_table_cache.release (m_out_lookup_table);
  if (m_rgb_lookup_table)
    lookup_table_cache.release (m_rgb_lookup_table);
  if (m_sharpened_data)
    gray_and_sharpened_data_cache.release (m_sharpened_data_holder);
}

/* Compute lookup table converting image_data to range 0...1 with GAMMA.  */
luminosity_t *
render::get_lookup_table (luminosity_t gamma, int maxval)
{
  lookup_table_params par = {maxval, gamma, 0, 1, false};
  return lookup_table_cache.get (par, NULL);
}

/* Release lookup table.  */
void
render::release_lookup_table (luminosity_t *table)
{
  lookup_table_cache.release (table);
}

/* Compute graydata of downscaled image.  */
void
render::get_gray_data (luminosity_t *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, luminosity_t, &render::get_data, &account_pixel> (data, x, y, width, height, pixelsize, progress);
}

/* Compute RGB data of downscaled image.  */
void
render::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, rgbdata, &render::get_rgb_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
}
void
render_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  gray_and_sharpened_data_cache.increase_capacity (n);
}
