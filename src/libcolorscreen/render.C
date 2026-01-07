#include "deconvolve.h"
#include "gaussian-blur.h"
#include "include/colorscreen.h"
#include "include/sensitivity.h"
#include "include/spectrum-to-xyz.h"
#include "include/stitch.h"
#include "lru-cache.h"
#include "mapalloc.h"
#include "render.h"
#include "sharpen.h"
#include <cassert>
namespace colorscreen
{
class lru_caches lru_caches;
std::atomic_uint64_t lru_caches::time;

/* A wrapper class around m_sharpened_data which handles allocation and
   dealocation. This is needed for the cache.  */
class sharpened_data
{
public:
  mem_luminosity_t *m_data;
  sharpened_data (int width, int height);
  ~sharpened_data ();
};
sharpened_data::sharpened_data (int width, int height)
{
  m_data = (mem_luminosity_t *)MapAlloc::Alloc (
      width * height * sizeof (mem_luminosity_t), "HDR data");
}
sharpened_data::~sharpened_data ()
{
  if (m_data)
    MapAlloc::Free (m_data);
  m_data = NULL;
}

namespace
{
/*****************************************************************************/
/*                         Backlight correction cache.                       */
/*****************************************************************************/

struct backlight_correction_cache_params
{
  backlight_correction_parameters *backlight_correction_params;
  uint64_t backlight_correction_id;
  int width;
  int height;
  luminosity_t backlight_correction_black;
  bool grayscale_needed;
  bool
  operator== (backlight_correction_cache_params &o)
  {
    return backlight_correction_id == o.backlight_correction_id
           && width == o.width && height == o.height
           && backlight_correction_black == o.backlight_correction_black
           && grayscale_needed == o.grayscale_needed;
  }
};

backlight_correction *
get_new_backlight_correction (struct backlight_correction_cache_params &p,
                              progress_info *progress)
{
  backlight_correction *c = new backlight_correction (
      *p.backlight_correction_params, p.width, p.height,
      p.backlight_correction_black, p.grayscale_needed, progress);
  if (!c->initialized_p ())
    {
      delete c;
      return NULL;
    }
  return c;
}
static lru_cache<backlight_correction_cache_params, backlight_correction,
                 backlight_correction *, get_new_backlight_correction, 10>
    backlight_correction_cache ("backlight corrections");

/*****************************************************************************/
/*    In lookup table (translating scan values to linear values) cache       */
/*****************************************************************************/

/* Lookup table translates raw input data into linear values.  */
struct lookup_table_params
{
  /* image_data are in range 0...img_maxval.  */
  int maxval;
  /* Input data are assumed to have gamma.  Inverse of gamma is applied to
     get linear data.
     if gamma is set to 0, then gamma_table is expected to be a vector
     converting raw data to linear gamma  */
  luminosity_t gamma;
  std::vector<luminosity_t> gamma_table;
  /* Dark point is subtracted from linear data and then the result is
     multiplied by scan_exposure.  */
  luminosity_t dark_point, scan_exposure;
  /* True if we should invert positive to negative.  */
  bool invert;
  /* Characteristic curve.  TODO: Not really implemented  */
  hd_curve *film_characteristic_curve;

  /* True if curve should be inverted.  */
  bool restore_original_luminosity;

  lookup_table_params ()
      : maxval (0), gamma (1), dark_point (0), scan_exposure (1), invert (0),
        film_characteristic_curve (NULL), restore_original_luminosity (0)
  {
  }

  bool
  operator== (lookup_table_params &o)
  {
    return maxval == o.maxval && gamma == o.gamma
           && (gamma || gamma_table == o.gamma_table)
           && dark_point == o.dark_point && scan_exposure == o.scan_exposure
           && invert == o.invert
           /* TODO: Invent cache IDs for curves!
              Pointer compare may not be safe if curve is released.  */
           && film_characteristic_curve == o.film_characteristic_curve
           && restore_original_luminosity == o.restore_original_luminosity;
  }
};

/* Simulate linear photographic negative.  Given luminosity (exposure time)
   return luminosity of negative.  */
static luminosity_t
simulate_linear_negative (luminosity_t t)
{
  /* Denisty at full exposure. 1.6 is max denisty in Dufaycolor manual.
     We simulate linear gamma negative process  where

     log10(Dp) = maxd * log10(Ts)

     Or

     Dp = Ts^maxd

     Ts is the transmitance is linearized value of the scan (of a negative)
     and Dp is the optical density of the resulting positive.  We have

     D = -log10 (T)

     Inverse is

     T = 10^{-D}

     To translate transminace to density. To get trasmitance of positive we
     want

     Tp = 10^{-Ts^maxd} */

  if (t < 0.3 / 65535)
    return 1;

  /* x coordinate of DH curve is logexp.  */
  luminosity_t logexp = 1 / (luminosity_t)65536 + std::log10 (t);
  /* Fully linear negative.  */
  luminosity_t density = logexp;
  /* Compute density.  */
  // luminosity_t density = std::pow (10, logdensity);
  /* Translate density to transmitance.  */
  return std::pow (10, -density);
}

luminosity_t *
get_new_lookup_table (struct lookup_table_params &p, progress_info *)
{
  bool use_table = !p.gamma && p.gamma_table.size ();
  /* Use some sane data if table is missing.  */
  if (!p.gamma && !use_table)
    p.gamma = 1;
  luminosity_t *lookup_table = new luminosity_t[p.maxval + 1];
  luminosity_t gamma = p.gamma;
  if (gamma != -1)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  luminosity_t mul = 1 / (luminosity_t)(p.maxval);

  luminosity_t dark_point = p.dark_point;
  luminosity_t scan_exposure = p.scan_exposure;

  if (!p.invert)
    {
      if (!use_table)
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i] = (apply_gamma (i * mul, gamma) - dark_point)
                            * scan_exposure;
      else
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i] = (p.gamma_table[i] - dark_point) * scan_exposure;
    }
  else if (!p.film_characteristic_curve)
    {
      // luminosity_t v = simulate_linear_negative (apply_gamma
      // (((luminosity_t)0.5 * p.maxval / 256) * mul, gamma)); scan_exposure *=
      // 1/v;
      /* i+3 is hack so scans of negatives with 0 do not get very large values.
         We probably should add preflash parameter.  */
      if (!use_table)
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i] = simulate_linear_negative (
              (apply_gamma (i * mul, gamma) - dark_point)
              * scan_exposure);
      else
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i] = simulate_linear_negative (
              (p.gamma_table[i] - dark_point) * scan_exposure);
      // lookup_table[i] = std::pow((luminosity_t)10, -(p.gamma_table[i]) -
      // dark_point * maxd) * scan_exposure;
    }
  else if (p.restore_original_luminosity)
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();

      // TODO: For stitching exposure should be really inside
      if (!use_table)
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i]
              = s.unapply (1
                           - apply_gamma ((i + (luminosity_t)0.5) * mul, gamma)
                           - dark_point)
                * scan_exposure;
      else
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i]
              = s.unapply (1 - p.gamma_table[i] - dark_point) * scan_exposure;
    }
  else
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();

      if (!use_table)
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i]
              = s.apply (1 - apply_gamma ((i + 0.5) * mul, gamma) - dark_point)
                * scan_exposure;
      else
        for (int i = 0; i <= p.maxval; i++)
          lookup_table[i]
              = s.apply (1 - p.gamma_table[i] - dark_point) * scan_exposure;
    }
  return lookup_table;
}

/*****************************************************************************/
/*    Out lookup table (translating linear values to output gamma) cache     */
/*****************************************************************************/

/* Output lookup table takes linear r,g,b values in range 0...65536
   and outputs r,g,b values in sRGB gamma curve in range 0...maxval.  */
struct out_lookup_table_params
{
  int maxval;
  luminosity_t output_gamma;
  luminosity_t target_film_gamma;
  bool
  operator== (out_lookup_table_params &o)
  {
    return maxval == o.maxval && output_gamma == o.output_gamma
           && target_film_gamma == o.target_film_gamma;
  }
};

precomputed_function<luminosity_t> *
get_new_out_lookup_table (struct out_lookup_table_params &p, progress_info *)
{
  std::vector<luminosity_t> lookup_table(render::out_lookup_table_size);
  luminosity_t gamma = p.output_gamma;
  if (gamma != -1)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  luminosity_t target_film_gamma = p.target_film_gamma;
  int maxval = p.maxval;
  luminosity_t mul = 1 / (luminosity_t)(render::out_lookup_table_size - 1);

  for (int i = 0; i < (int)render::out_lookup_table_size; i++)
    lookup_table[i]
        = invert_gamma (apply_gamma (i * mul, target_film_gamma), gamma)
          * maxval + (luminosity_t) 0.5;

  return new precomputed_function<luminosity_t> (0, 1, lookup_table.data (), render::out_lookup_table_size);
}

/* To improve interactive response we cache conversion tables.  */
static lru_cache<lookup_table_params, luminosity_t[], luminosity_t *,
                 get_new_lookup_table, 4>
    lookup_table_cache ("in lookup tables");
static lru_cache<out_lookup_table_params, precomputed_function <luminosity_t>, precomputed_function <luminosity_t> *,
                 get_new_out_lookup_table, 4>
    out_lookup_table_cache ("out lookup tables");

/*****************************************************************************/
/*                     Gray and sharpened data cache                         */
/*        (in tables are applied, channels mixed and data sharpened          */
/*****************************************************************************/

struct graydata_params
{
  /* Pointers in image_data may become stale if image is freed. Use ID
     to check cache entries.  */
  uint64_t image_id;
  const image_data *img;
  luminosity_t gamma;
  std::vector<luminosity_t> gamma_table[3];
  /* Dark point for mixing. */
  rgbdata dark;
  /* Weights of individual channels.  */
  luminosity_t red, green, blue;
  bool invert;
  /* Backlight correction.  */
  backlight_correction *backlight;
  uint64_t backlight_correction_id;
  bool ignore_infrared;
  bool
  operator== (graydata_params &o)
  {
    return image_id == o.image_id && gamma == o.gamma
           && (gamma
               || (gamma_table[0] == o.gamma_table[0]
                   && gamma_table[1] == o.gamma_table[1]
                   && gamma_table[2] == o.gamma_table[2]))
           && invert == o.invert && dark == o.dark && red == o.red
           && green == o.green && blue == o.blue
           && backlight_correction_id == o.backlight_correction_id
           && ignore_infrared == o.ignore_infrared;
  }
};

struct gray_data_tables
{
  luminosity_t *rtable;
  luminosity_t *gtable;
  luminosity_t *btable;
  rgbdata dark;
  luminosity_t red, green, blue;
  backlight_correction *correction;
  bool invert;
};

inline gray_data_tables
compute_gray_data_tables (struct graydata_params &p, bool correction,
                          progress_info *progress)
{
  gray_data_tables ret;
  luminosity_t red = p.red;
  luminosity_t green = p.green;
  luminosity_t blue = p.blue;
  rgbdata dark = p.dark;
#if 0
  luminosity_t sum = (red < 0 ? 0 : red) + (green < 0 ? 0 : green) + (blue < 0 ? 0 : blue);

  if (!sum)
    {
      sum = 1;
      green = 1;
      blue = red = 0;
    }
  red /= sum / 3;
  green /= sum / 3;
  blue /= sum / 3;
#endif

  /* Normally the lookup tables contains red, green, blue weights.
     However with backlight correction we need to apply them only after
     correcting the input.  */
  ret.red = red;
  ret.green = green;
  ret.blue = blue;
  ret.dark = dark;

  lookup_table_params par;
  par.gamma = p.gamma;
  par.maxval = p.img->maxval;
  par.scan_exposure = correction ? 1 : red;
  par.dark_point = correction ? 0 : dark.red;
  par.invert = p.invert;
  par.gamma_table = p.gamma_table[0];
  ret.rtable = lookup_table_cache.get (par, progress);
  if (!ret.rtable)
    return ret;
  par.scan_exposure = correction ? 1 : green;
  par.dark_point = correction ? 0 : dark.green;
  par.gamma_table = p.gamma_table[1];
  ret.gtable = lookup_table_cache.get (par, progress);
  if (!ret.gtable)
    {
      lookup_table_cache.release (ret.rtable);
      ret.rtable = NULL;
      return ret;
    }
  par.scan_exposure = correction ? 1 : blue;
  par.dark_point = correction ? 0 : dark.blue;
  par.gamma_table = p.gamma_table[2];
  ret.btable = lookup_table_cache.get (par, progress);
  if (!ret.btable)
    {
      lookup_table_cache.release (ret.rtable);
      ret.rtable = NULL;
      lookup_table_cache.release (ret.gtable);
      ret.gtable = NULL;
      return ret;
    }
  return ret;
}

inline void
free_gray_data_tables (gray_data_tables &t)
{
  lookup_table_cache.release (t.rtable);
  lookup_table_cache.release (t.gtable);
  lookup_table_cache.release (t.btable);
}

inline luminosity_t
compute_gray_data (gray_data_tables &t, int width, int height, int x, int y,
                   int r, int g, int b)
{
  luminosity_t l1 = t.rtable[r];
  luminosity_t l2 = t.gtable[g];
  luminosity_t l3 = t.btable[b];
  if (t.correction)
    {
      l1 = (t.correction->apply (l1, x, y,
                                 backlight_correction_parameters::red)
            - t.dark.red)
           * t.red;
      l2 = (t.correction->apply (l2, x, y,
                                 backlight_correction_parameters::green)
            - t.dark.green)
           * t.green;
      l3 = (t.correction->apply (l3, x, y,
                                 backlight_correction_parameters::blue)
            - t.dark.blue)
           * t.blue;
    }
  luminosity_t val = l1 + l2 + l3;
  return val;
}


struct gray_and_sharpen_params
{
  graydata_params gp;
  sharpen_parameters sp;
  bool
  operator== (gray_and_sharpen_params &o)
  {
    return gp == o.gp && sp == o.sp;
  }
};

struct getdata_params
{
  luminosity_t *table;
  backlight_correction *correction;
  int width, height;
};

/* Helper for sharpening template for images with gray data.  */
inline luminosity_t
getdata_helper_no_correction (unsigned short **graydata, int x, int y, int,
                              getdata_params d)
{
  if (colorscreen_checking)
    assert (x >= 0 && x < d.width && y >= 0 && y < d.height);
  return d.table[graydata[y][x]];
}

/* Helper for sharpening template for images with gray data.  */
inline luminosity_t
getdata_helper_correction (unsigned short **graydata, int x, int y, int,
                           getdata_params d)
{
  luminosity_t v = d.table[graydata[y][x]];
  if (colorscreen_checking)
    assert (x >= 0 && x < d.width && y >= 0 && y < d.height);
  v = d.correction->apply (v, x, y, backlight_correction_parameters::ir);
  return v;
}
/* Helper for sharpening template for images with RGB data only.  */
inline luminosity_t
getdata_helper2 (const image_data *img, int x, int y, int, gray_data_tables t)
{
  // assert (x >= 0 && x < t.width && y >= 0 && y < t.height);
  luminosity_t val = compute_gray_data (
      t, img->width, img->height, x, y, img->rgbdata[y][x].r,
      img->rgbdata[y][x].g, img->rgbdata[y][x].b);
  return val;
}

sharpened_data *
get_new_gray_sharpened_data (struct gray_and_sharpen_params &p,
                             progress_info *progress)
{
  sharpened_data *ret = new sharpened_data (p.gp.img->width, p.gp.img->height);
  if (!ret)
    return NULL;
  mem_luminosity_t *out = ret->m_data;
  if (!out)
    {
      delete ret;
      return NULL;
    }

  bool ok;
  if (p.gp.img->data && !p.gp.ignore_infrared)
    {
      lookup_table_params par;
      getdata_params d;
      par.maxval = p.gp.img->maxval;
      par.gamma = p.gp.gamma;
      par.invert = p.gp.invert;
      /* Here we want to use table for infrared, but that is not present in ICC
         profile. Use blue instead, since usually blue dye fades first and thus
         blue channel is most representative for IR in the scan.
         ??? This does not work with the argyll profiles I made for Nikon.  */
      // if (!p.gp.gamma)
      // par.gamma_table = p.gp.img->to_linear[2];
      d.table = lookup_table_cache.get (par, progress);
      d.correction = p.gp.backlight;
      d.width = p.gp.img->width;
      d.height = p.gp.img->height;
      if (!d.table)
        {
          delete ret;
          return NULL;
        }
      if (d.correction)
        {
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                               unsigned short **, getdata_params,
                               getdata_helper_correction> (
                  out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, unsigned short **,
                         getdata_params, getdata_helper_correction> (
                out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
                p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
	       	p.sp.usm_amount, progress);
        }
      else if (p.sp.deconvolution_p ())
        {
          ok = deconvolve<luminosity_t, mem_luminosity_t, unsigned short **,
                           getdata_params, getdata_helper_no_correction> (
              out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
	      p.sp, progress, true);
        }
      else
        ok = sharpen<luminosity_t, mem_luminosity_t, unsigned short **,
                     getdata_params, getdata_helper_no_correction> (
            out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
            p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
            p.sp.usm_amount, progress);
      lookup_table_cache.release (d.table);
    }
  else
    {
      gray_data_tables t
          = compute_gray_data_tables (p.gp, p.gp.backlight != NULL, progress);
      if (!t.rtable)
        ok = false;
      else
        {
          t.correction = p.gp.backlight;
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                               const image_data *, gray_data_tables,
                               getdata_helper2> (
                  out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, const image_data *,
                         gray_data_tables, getdata_helper2> (
                out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
                p.sp.usm_amount, progress);
          free_gray_data_tables (t);
        }
    }
  if (!ok)
    {
      delete ret;
      return NULL;
    }
  return ret;
}
static lru_cache<gray_and_sharpen_params, sharpened_data, sharpened_data *,
                 get_new_gray_sharpened_data, 1>
    gray_and_sharpened_data_cache ("gray and sharpened data");

}
/* Prune render cache.  We need to do this so destruction order of MapAlloc and
   the cache does not yield an segfault.  */

void
prune_render_caches ()
{
  gray_and_sharpened_data_cache.prune ();
}
/*****************************************************************************/
/*                             render implementation                         */
/*****************************************************************************/

bool
render::precompute_all (bool grayscale_needed, bool normalized_patches,
                        rgbdata patch_proportions, progress_info *progress)
{
  if (m_params.backlight_correction)
    {
      backlight_correction_cache_params p
          = { m_params.backlight_correction.get (),
              m_params.backlight_correction->id,
              m_img.width,
              m_img.height,
              m_params.backlight_correction_black,
              /*!grayscale_needed*/ true };
      m_backlight_correction = backlight_correction_cache.get (
          p, progress, &m_backlight_correction_id);
      if (!m_backlight_correction)
        return false;
    }
  if (m_img.rgbdata)
    {
      lookup_table_params par;
      par.maxval = m_img.maxval;
      par.gamma = m_params.gamma;
      if (!par.gamma)
        par.gamma_table = m_img.to_linear[0];
      par.invert = m_params.invert;
      m_rgb_lookup_table[0] = lookup_table_cache.get (par, progress);
      if (!m_rgb_lookup_table[0])
        return false;
      if (!par.gamma)
        {
          par.gamma_table = m_img.to_linear[1];
          m_rgb_lookup_table[1] = lookup_table_cache.get (par, progress);
          if (m_rgb_lookup_table[1] == m_rgb_lookup_table[0])
            lookup_table_cache.release (m_rgb_lookup_table[1]);
          par.gamma_table = m_img.to_linear[2];
          m_rgb_lookup_table[2] = lookup_table_cache.get (par, progress);
          if (m_rgb_lookup_table[2] == m_rgb_lookup_table[0])
            lookup_table_cache.release (m_rgb_lookup_table[2]);
        }
      else
        m_rgb_lookup_table[1] = m_rgb_lookup_table[2] = m_rgb_lookup_table[0];
    }
  out_lookup_table_params out_par
      = { m_dst_maxval, m_params.output_gamma, m_params.target_film_gamma };
  m_out_lookup_table = out_lookup_table_cache.get (out_par, progress);

  if (grayscale_needed)
    {
      gray_and_sharpen_params p
          = { { m_img.id,
                &m_img,
                m_params.gamma,
                { m_img.to_linear[0], m_img.to_linear[1], m_img.to_linear[2] },
                m_params.mix_dark,
                m_params.mix_red,
                m_params.mix_green,
                m_params.mix_blue,
                m_params.invert,
                m_backlight_correction,
                m_backlight_correction_id,
                m_params.ignore_infrared }, m_params.sharpen };
      m_sharpened_data_holder
          = gray_and_sharpened_data_cache.get (p, progress, &m_gray_data_id);
      if (!m_sharpened_data_holder)
        return false;
      m_sharpened_data = m_sharpened_data_holder->m_data;
    }

  color_matrix color;

  if (m_params.output_profile != render_parameters::output_profile_original)
    {
      /* See if we want to do some output adjustments in pro photo RGB space.
         These should closely follow what DNG reference recommends.  */
      bool do_pro_photo
          = m_params.output_tone_curve != tone_curve::tone_curve_linear;
      // printf ("Prophoto %i\n", do_pro_photo);
      /* Matrix converting dyes to XYZ.  */
      color = m_params.get_rgb_to_xyz_matrix (
          &m_img, normalized_patches, patch_proportions,
          do_pro_photo ? d50_white : d65_white);

      // printf ("To xyz\n");
      // color.print (stdout);

      /* For subtractive processes we do post-processing in separate matrix
       * after spectrum dyes to xyz are applied.  */
      if (m_params.color_model == render_parameters::color_model_kodachrome25)
        {
          m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
          m_spectrum_dyes_to_xyz->set_film_response (
              spectrum_dyes_to_xyz::response_even);
          m_spectrum_dyes_to_xyz->set_dyes (
              spectrum_dyes_to_xyz::kodachrome_25_sensitivity);
          m_spectrum_dyes_to_xyz->set_backlight (
              spectrum_dyes_to_xyz::il_D, m_params.backlight_temperature);

          spectrum_dyes_to_xyz dufay;
          dufay.set_film_response (
              spectrum_dyes_to_xyz::dufaycolor_harrison_horner_emulsion_cut);
          dufay.set_dyes (
              spectrum_dyes_to_xyz::
                  dufaycolor_harrison_horner /*dufaycolor_color_cinematography*/);
          dufay.set_backlight (spectrum_dyes_to_xyz::il_D,
                               m_params.backlight_temperature);
          // dufay.set_characteristic_curve
          // (spectrum_dyes_to_xyz::linear_reversal_curve);

          color = color
                  * m_spectrum_dyes_to_xyz->process_transformation_matrix (
                      &dufay);
          m_spectrum_dyes_to_xyz->set_characteristic_curve (
              spectrum_dyes_to_xyz::kodachrome25_curve);

          saturation_matrix m (m_params.saturation);
          m_color_matrix2 = (bradford_whitepoint_adaptation_matrix (
                                 m_spectrum_dyes_to_xyz->whitepoint_xyz (),
                                 do_pro_photo ? d50_white : d65_white)
                             * m)
                            * 1.5;
          if (m_params.output_profile == render_parameters::output_profile_xyz)
            ;
          else if (do_pro_photo)
            {
              xyz_pro_photo_rgb_matrix m;
              m_color_matrix2 = m * m_color_matrix2;
              m_tone_curve
                  = std::make_unique<tone_curve> (m_params.output_tone_curve);
              assert (!m_tone_curve->is_linear ());
            }
          else
            {
              xyz_srgb_matrix m;
              m_color_matrix2 = m * m_color_matrix2;
            }
        }
      else
        {
          if (m_params.output_profile == render_parameters::output_profile_xyz)
            ;
          else if (do_pro_photo)
            {
              xyz_pro_photo_rgb_matrix m;
              color = m * color;
              m_tone_curve
                  = std::make_unique<tone_curve> (m_params.output_tone_curve);
              assert (!m_tone_curve->is_linear ());
            }
          else
            {
              xyz_srgb_matrix m;
              color = m * color;
            }
        }
    }
  else
    color = m_params.get_rgb_adjustment_matrix (normalized_patches,
                                                patch_proportions);
  m_color_matrix = color;
  // printf ("Final\n");
  // color.print (stdout);
  return true;
}

render::~render ()
{
  if (m_out_lookup_table)
    out_lookup_table_cache.release (m_out_lookup_table);
  if (m_rgb_lookup_table[0])
    {
      lookup_table_cache.release (m_rgb_lookup_table[0]);
      if (m_rgb_lookup_table[0] != m_rgb_lookup_table[1]
          && m_rgb_lookup_table[1])
        lookup_table_cache.release (m_rgb_lookup_table[1]);
      if (m_rgb_lookup_table[0] != m_rgb_lookup_table[2]
          && m_rgb_lookup_table[2])
        lookup_table_cache.release (m_rgb_lookup_table[2]);
    }
  if (m_sharpened_data)
    gray_and_sharpened_data_cache.release (m_sharpened_data_holder);
  if (m_backlight_correction)
    backlight_correction_cache.release (m_backlight_correction);
  if (m_spectrum_dyes_to_xyz)
    delete m_spectrum_dyes_to_xyz;
}

/* Compute lookup table converting image_data to range 0...1 with GAMMA.  */
bool
render::get_lookup_tables (luminosity_t **ret, luminosity_t gamma,
                           const image_data *img, progress_info *progress)
{
  lookup_table_params par;
  par.gamma = gamma;
  par.maxval = img->maxval;
  ret[0] = ret[1] = ret[2] = NULL;
  if (!par.gamma)
    par.gamma_table = img->to_linear[0];
  ret[0] = lookup_table_cache.get (par, progress);
  if (!ret[0])
    return false;
  if (par.gamma)
    ret[1] = ret[2] = ret[0];
  else
    {
      par.gamma_table = img->to_linear[1];
      ret[1] = lookup_table_cache.get (par, progress);
      if (!ret[1])
        {
          release_lookup_tables (ret);
          return false;
        }
      if (ret[1] == ret[0])
        lookup_table_cache.release (ret[1]);
      par.gamma_table = img->to_linear[2];
      ret[2] = lookup_table_cache.get (par, progress);
      if (!ret[2])
        {
          release_lookup_tables (ret);
          return false;
        }
      if (ret[2] == ret[0])
        lookup_table_cache.release (ret[2]);
    }
  return true;
}

/* Release lookup table.  */
void
render::release_lookup_tables (luminosity_t **table)
{
  lookup_table_cache.release (table[0]);
  if (table[1] != table[0] && table[1])
    lookup_table_cache.release (table[1]);
  if (table[2] != table[0] && table[2])
    lookup_table_cache.release (table[2]);
  table[0] = table[1] = table[2] = NULL;
}

/* Compute graydata of downscaled image.  */
void
render::get_gray_data (luminosity_t *data, coord_t x, coord_t y, int width,
                       int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, luminosity_t, &render::get_data, &account_pixel> (
      data, x, y, width, height, pixelsize, progress);
}

/* Compute RGB data of downscaled image.  */
void
render::get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
                        int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, rgbdata, &render::get_rgb_pixel, &account_rgb_pixel> (
      data, x, y, width, height, pixelsize, progress);
}
void
render_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  gray_and_sharpened_data_cache.increase_capacity (n);
}

rgbdata
get_linearized_pixel (const image_data &img, render_parameters &rparam, int xx,
                      int yy, int range, progress_info *progress)
{
  rgbdata color = { 0, 0, 0 };
  int n = 0;
  const image_data *imgp = &img;
  if (img.stitch)
    {
      int tx, ty;
      point_t scr = img.stitch->common_scr_to_img.final_to_scr (
          { (coord_t)(xx + img.xmin), (coord_t)(yy + img.ymin) });
      if (!img.stitch->tile_for_scr (&rparam, scr.x, scr.y, &tx, &ty, true))
        return color;
      point_t p = img.stitch->images[ty][tx].common_scr_to_img (scr);
      xx = nearest_int (p.x);
      yy = nearest_int (p.y);
      imgp = img.stitch->images[ty][tx].img.get ();
    }
  render r (*imgp, rparam, 255);
  r.precompute_all (img.rgbdata ? false : true, false,
                    { 1 / 3.0, 1 / 3.0, 1 / 3.0 }, progress);
  for (int y = yy - range; y < yy + range; y++)
    for (int x = xx - range; x < xx + range; x++)
      if (x >= 0 && x < img.width && y >= 0 && y < img.height)
        {
          if (img.rgbdata)
            color += r.get_linearized_rgb_pixel (x, y);
          else
            {
              rgbdata color2 = { 1, 1, 1 };
              color += color2 * r.get_unadjusted_data (x, y);
            }
          n++;
        }
  return n ? color / n : color;
}
}
