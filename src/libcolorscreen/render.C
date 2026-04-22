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
#include "include/histogram.h"
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
  /* Initialize sharpened data with given WIDTH and HEIGHT.  */
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
  m_data = nullptr;
}

namespace
{
/* Parameters for backlight correction cache.  */
struct backlight_correction_cache_params
{
  class backlight_correction_parameters *backlight_correction_params;
  uint64_t backlight_correction_id;
  int width;
  int height;
  luminosity_t backlight_correction_black;
  bool grayscale_needed;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const backlight_correction_cache_params &o) const
  {
    return backlight_correction_id == o.backlight_correction_id
           && width == o.width && height == o.height
           && backlight_correction_black == o.backlight_correction_black
           && grayscale_needed == o.grayscale_needed;
  }
};

/* Parameters for input lookup table cache.  */
struct lookup_table_params
{
  int maxval;
  luminosity_t gamma;
  std::vector<luminosity_t> gamma_table;
  luminosity_t dark_point, scan_exposure;

  lookup_table_params ()
      : maxval (0), gamma (1), dark_point (0), scan_exposure (1)
  {
  }

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const lookup_table_params &o) const
  {
    return maxval == o.maxval && gamma == o.gamma
           && (gamma != 0 || gamma_table == o.gamma_table)
           && dark_point == o.dark_point && scan_exposure == o.scan_exposure;
  }
};

/* Parameters for grayscale data generation.  */
struct graydata_params
{
  uint64_t image_id;
  const class image_data *img;
  luminosity_t gamma;
  std::vector<luminosity_t> gamma_table[3];
  rgbdata dark;
  luminosity_t red, green, blue;
  class backlight_correction *backlight;
  uint64_t backlight_correction_id;
  bool ignore_infrared;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const graydata_params &o) const
  {
    return image_id == o.image_id && gamma == o.gamma
           && (gamma != 0
               || (gamma_table[0] == o.gamma_table[0]
                   && gamma_table[1] == o.gamma_table[1]
                   && gamma_table[2] == o.gamma_table[2]))
           && dark == o.dark && red == o.red
           && green == o.green && blue == o.blue
           && backlight_correction_id == o.backlight_correction_id
           && ignore_infrared == o.ignore_infrared;
  }
};

/* Parameters for grayscale and sharpened data cache.  */
struct gray_and_sharpen_params
{
  graydata_params gp;
  class sharpen_parameters sp;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const gray_and_sharpen_params &o) const
  {
    return gp == o.gp && sp == o.sp;
  }
};

/* Parameters for image layer histogram cache.  */
struct image_layer_histogram_params
{
  uint64_t graydata_id;
  int_image_area crop;
  render *r;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const image_layer_histogram_params &o) const
  {
    return graydata_id == o.graydata_id && crop == o.crop;
  }
};

/* Create new backlight correction instance using parameters P.  */
std::unique_ptr<backlight_correction>
get_new_backlight_correction (backlight_correction_cache_params &p,
                              progress_info *progress)
{
  auto c = std::make_unique<backlight_correction> (
      *p.backlight_correction_params, p.width, p.height,
      p.backlight_correction_black, p.grayscale_needed, progress);
  if (!c->initialized_p ())
    return nullptr;
  return c;
}

/* Prototype for histogram generation.  */
std::unique_ptr<histogram>
get_new_image_layer_histogram (image_layer_histogram_params &p, progress_info *);

/* Create new image layer histogram using parameters P.  */
std::unique_ptr<histogram>
get_new_image_layer_histogram (image_layer_histogram_params &p,
                               progress_info *)
{
  histogram hist;

  /* First determine the global range of values.  */
#pragma omp parallel for reduction(histogram_range : hist)
  for (int y = p.crop.y; y < p.crop.y + p.crop.height; y++)
    for (int x = p.crop.x; x < p.crop.x + p.crop.width; x++)
      hist.pre_account (p.r->get_unadjusted_data (x, y));

  hist.finalize_range (65536);

  /* Now account values into the finalized range.  */
#pragma omp parallel for reduction(histogram_entries : hist)
  for (int y = p.crop.y; y < p.crop.y + p.crop.height; y++)
    for (int x = p.crop.x; x < p.crop.x + p.crop.width; x++)
      hist.account (p.r->get_unadjusted_data (x, y));

  hist.finalize ();
  return std::make_unique<histogram> (std::move (hist));
}

/* Create new input lookup table using parameters P.  */
std::unique_ptr<luminosity_t[]>
get_new_lookup_table (lookup_table_params &p, progress_info *)
{
  bool use_table = (p.gamma == 0) && !p.gamma_table.empty ();
  /* Use some sane data if table is missing.  */
  if (p.gamma == 0 && !use_table)
    p.gamma = 1;
  auto lookup_table = std::make_unique<luminosity_t[]> (p.maxval + 1);
  luminosity_t gamma = p.gamma;
  if (gamma != -1)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  luminosity_t mul = 1 / (luminosity_t)(p.maxval);

  luminosity_t dark_point = p.dark_point;
  luminosity_t scan_exposure = p.scan_exposure;

  if (!use_table)
    for (int i = 0; i <= p.maxval; i++)
      lookup_table[i] = (apply_gamma (i * mul, gamma) - dark_point)
			* scan_exposure;
  else
    for (int i = 0; i <= p.maxval; i++)
      lookup_table[i] = (p.gamma_table[i] - dark_point) * scan_exposure;
  return lookup_table;
}

/* Prototypes for data generation.  */
std::unique_ptr<sharpened_data>
get_new_gray_sharpened_data (gray_and_sharpen_params &p, progress_info *progress);

/* Static cache instances.  */
static lru_cache<backlight_correction_cache_params, backlight_correction,
                 get_new_backlight_correction, 10>
    backlight_correction_cache ("backlight corrections");

static lru_cache<image_layer_histogram_params, histogram,
                 get_new_image_layer_histogram, 10>
    image_layer_histogram_cache ("image layer histograms");

static lru_cache<lookup_table_params, luminosity_t[], get_new_lookup_table, 4>
    lookup_table_cache ("in lookup tables");

static lru_cache<gray_and_sharpen_params, sharpened_data,
                 get_new_gray_sharpened_data, 2>
    gray_and_sharpened_data_cache ("gray and sharpened data");

/* Tables used during gray data computation.  */
struct gray_data_tables
{
  std::shared_ptr<luminosity_t[]> rtable;
  std::shared_ptr<luminosity_t[]> gtable;
  std::shared_ptr<luminosity_t[]> btable;
  rgbdata dark;
  luminosity_t red, green, blue;
  backlight_correction *correction;
};

/* Compute lookup tables for grayscale conversion using parameters P.  */
inline gray_data_tables
compute_gray_data_tables (graydata_params &p, bool correction,
                          progress_info *progress)
{
  gray_data_tables ret = {nullptr, nullptr, nullptr, {0,0,0}, 0, 0, 0, nullptr};
  luminosity_t red = p.red;
  luminosity_t green = p.green;
  luminosity_t blue = p.blue;
  rgbdata dark = p.dark;

  ret.red = red;
  ret.green = green;
  ret.blue = blue;
  ret.dark = dark;

  lookup_table_params par;
  par.gamma = p.gamma;
  par.maxval = p.img->maxval;
  par.scan_exposure = correction ? 1 : red;
  par.dark_point = correction ? 0 : dark.red;
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
      ret.rtable = nullptr;
      return ret;
    }
  par.scan_exposure = correction ? 1 : blue;
  par.dark_point = correction ? 0 : dark.blue;
  par.gamma_table = p.gamma_table[2];
  ret.btable = lookup_table_cache.get (par, progress);
  if (!ret.btable)
    {
      ret.rtable = nullptr;
      ret.gtable = nullptr;
      return ret;
    }
  return ret;
}

/* Compute grayscale value for source pixel R, G, B at index X, Y.  */
inline luminosity_t
compute_gray_data (gray_data_tables &t, int x, int y, int r, int g, int b)
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
  return l1 + l2 + l3;
}

/* Helper parameters for grayscale data fetching.  */
struct getdata_params
{
  std::shared_ptr<luminosity_t[]> table;
  backlight_correction *correction;
  int width, height;
};

/* Helper for sharpening template for images with gray data with no correction.  */
inline luminosity_t
getdata_helper_no_correction (unsigned short **graydata, int x, int y, int,
                              getdata_params &d)
{
  if (colorscreen_checking)
    assert (x >= 0 && x < d.width && y >= 0 && y < d.height);
  return d.table[graydata[y][x]];
}

/* Helper for sharpening template for images with gray data with correction.  */
inline luminosity_t
getdata_helper_correction (unsigned short **graydata, int x, int y, int,
                           getdata_params &d)
{
  if (colorscreen_checking)
    assert (x >= 0 && x < d.width && y >= 0 && y < d.height);
  luminosity_t v = d.table[graydata[y][x]];
  v = d.correction->apply (v, x, y, backlight_correction_parameters::ir);
  return v;
}

/* Helper for sharpening template for images with RGB data only.  */
inline luminosity_t
getdata_helper2 (const image_data *img, int x, int y, int, gray_data_tables &t)
{
  return compute_gray_data (
      t, x, y, img->rgbdata[y][x].r,
      img->rgbdata[y][x].g, img->rgbdata[y][x].b);
}

/* Create new grayscale and sharpened data using parameters P.  */
std::unique_ptr<sharpened_data>
get_new_gray_sharpened_data (gray_and_sharpen_params &p,
                             progress_info *progress)
{
  auto ret = std::make_unique<sharpened_data> (p.gp.img->width, p.gp.img->height);
  if (!ret || !ret->m_data)
      return nullptr;
  mem_luminosity_t *out = ret->m_data;

  bool ok;
  if (p.gp.img->data && !p.gp.ignore_infrared)
    {
      lookup_table_params par;
      getdata_params d;
      par.maxval = p.gp.img->maxval;
      par.gamma = p.gp.gamma;
      d.table = lookup_table_cache.get (par, progress);
      d.correction = p.gp.backlight;
      d.width = p.gp.img->width;
      d.height = p.gp.img->height;
      if (!d.table)
          return nullptr;
      if (d.correction)
        {
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                                unsigned short **, getdata_params &,
                                getdata_helper_correction> (
                  out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, unsigned short **,
                         getdata_params &, getdata_helper_correction> (
                out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
                p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
	       	p.sp.usm_amount, progress);
        }
      else if (p.sp.deconvolution_p ())
        {
          ok = deconvolve<luminosity_t, mem_luminosity_t, unsigned short **,
                           getdata_params &, getdata_helper_no_correction> (
              out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
	      p.sp, progress, true);
        }
      else
        ok = sharpen<luminosity_t, mem_luminosity_t, unsigned short **,
                     getdata_params &, getdata_helper_no_correction> (
            out, p.gp.img->data, d, p.gp.img->width, p.gp.img->height,
            p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
            p.sp.usm_amount, progress);
    }
  else
    {
      gray_data_tables t
          = compute_gray_data_tables (p.gp, p.gp.backlight != nullptr, progress);
      if (!t.rtable)
        ok = false;
      else
        {
          t.correction = p.gp.backlight;
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                                const image_data *, gray_data_tables &,
                                getdata_helper2> (
                  out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, const image_data *,
                         gray_data_tables &, getdata_helper2> (
                out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
                p.sp.usm_amount, progress);
        }
    }
  if (!ok)
      return nullptr;
  return ret;
}
} // anonymous namespace

/* Prune render cache.  We need to do this so destruction order of MapAlloc and
   the cache does not yield an segfault.  */
void
prune_render_caches ()
{
  gray_and_sharpened_data_cache.prune ();
  lookup_table_cache.prune ();
  backlight_correction_cache.prune ();
  image_layer_histogram_cache.prune ();
}

/*****************************************************************************/
/*                             render implementation                         */
/*****************************************************************************/

/* Precompute all data needed for rendering.  */
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
              true };
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
      if (par.gamma == 0)
        par.gamma_table = m_img.to_linear[0];
      m_rgb_lookup_table[0] = lookup_table_cache.get (par, progress);
      if (!m_rgb_lookup_table[0])
        return false;
      if (par.gamma == 0)
        {
          par.gamma_table = m_img.to_linear[1];
          m_rgb_lookup_table[1] = lookup_table_cache.get (par, progress);
          par.gamma_table = m_img.to_linear[2];
          m_rgb_lookup_table[2] = lookup_table_cache.get (par, progress);
        }
      else
        {
	  m_rgb_lookup_table[1] = lookup_table_cache.get (par, progress);
	  m_rgb_lookup_table[2] = lookup_table_cache.get (par, progress);
        }
    }

  if (grayscale_needed)
    {
      bool ir_simulation = !m_img.has_grayscale_or_ir ()
			   || (m_img.has_rgb () && m_params.ignore_infrared);
      gray_and_sharpen_params p
          = { { m_img.id,
                &m_img,
                m_params.gamma,
                { m_img.to_linear[0], m_img.to_linear[1], m_img.to_linear[2] },
                ir_simulation ? m_params.mix_dark : (rgbdata) {1.0f,1.0f,1.0f},
                ir_simulation ? m_params.mix_red : 1.0f,
                ir_simulation ? m_params.mix_green : 1.0f,
                ir_simulation ? m_params.mix_blue : 1.0f,
                m_backlight_correction.get (),
                m_backlight_correction_id,
                ir_simulation ? m_params.ignore_infrared : false }, m_params.sharpen };
      m_sharpened_data_holder
          = gray_and_sharpened_data_cache.get (p, progress, &m_gray_data_id);
      if (!m_sharpened_data_holder)
        return false;
      m_sharpened_data = m_sharpened_data_holder->m_data;
    }
  if (m_params.contact_copy.simulate)
    {
      m_sensitivity_hd_curve = std::make_unique <richards_hd_curve> (100, m_params.contact_copy.emulsion_characteristic_curve);
      m_sensitivity = std::make_unique <film_sensitivity> (m_sensitivity_hd_curve.get (), m_params.contact_copy.preflash, m_params.contact_copy.exposure, m_params.contact_copy.boost);
      m_sensitivity->precompute ();
    }
  if (m_sensitivity)
    {
      luminosity_t lmin=-0.1;
      luminosity_t lmax=2;
      const int steps = 65536;
      luminosity_t yvals[steps];
      for (int i = 0; i < steps; i++)
	yvals[i] = adjust_luminosity_ir (i * (lmax - lmin) / (steps - 1) + lmin);
      m_adjust_luminosity = std::make_unique <precomputed_function <luminosity_t>> (lmin, lmax, yvals, steps);
    }
  return out_color.precompute (m_params, &m_img, normalized_patches, patch_proportions, progress);
}

/* Compute lookup table converting image_data to range 0..1 with GAMMA.  */
bool
render::get_lookup_tables (std::shared_ptr<luminosity_t[]> *ret,
                           luminosity_t gamma, const image_data *img,
                           progress_info *progress)
{
  lookup_table_params par;
  par.gamma = gamma;
  par.maxval = img->maxval;

  if (par.gamma == 0)
    par.gamma_table = img->to_linear[0];
  ret[0] = lookup_table_cache.get (par, progress);
  if (!ret[0])
    return false;
  if (par.gamma != 0)
  {
    ret[1] = lookup_table_cache.get (par, progress);
    ret[2] = lookup_table_cache.get (par, progress);
  }
  else
    {
      par.gamma_table = img->to_linear[1];
      ret[1] = lookup_table_cache.get (par, progress);
      if (!ret[1])
        {
          ret[0] = nullptr;
          return false;
        }
      par.gamma_table = img->to_linear[2];
      ret[2] = lookup_table_cache.get (par, progress);
      if (!ret[2])
        {
          ret[0] = nullptr;
          ret[1] = nullptr;
          return false;
        }
    }
  return true;
}

/* Compute graydata of downscaled image.  */
void
render::get_gray_data (luminosity_t *data, coord_t x, coord_t y, int width,
                       int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, luminosity_t, &render::get_data> (
      data, x, y, width, height, pixelsize, progress);
}

/* Compute RGB data of downscaled image.  */
void
render::get_color_data (rgbdata *data, coord_t x, coord_t y, int width,
                        int height, coord_t pixelsize, progress_info *progress)
{
  downscale<render, rgbdata, &render::get_rgb_pixel> (
      data, x, y, width, height, pixelsize, progress);
}

/* Increase capacity of stitch related caches to N.  */
void
render_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  gray_and_sharpened_data_cache.increase_capacity (n);
}

/* Get linearized pixel at XX, YY with given RANGE.  */
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
  if (!r.precompute_all (img.rgbdata ? false : true, false,
                    { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f }, progress))
    return {0,0,0};
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
  return n ? color / (float)n : color;
}

/* Convert HD Y values to RGB.  */
std::vector <rgbdata>
hd_y_to_rgb (render_parameters &rparam, int steps, luminosity_t miny, luminosity_t maxy, rgbdata patch_proportions, hd_axis_type axis_type)
{
  out_color_adjustments a (256);
  if (!a.precompute (rparam, nullptr, false, patch_proportions, nullptr))
    return {};
  std::vector <rgbdata> data (steps);
  for (int i = 0 ; i < steps; i++)
  {
    luminosity_t y = i * (maxy - miny) / (steps - 1) + miny;
    y = hd_axis_y_to_linear (y, rparam.contact_copy.boost, axis_type);
    int rr, gg, bb;
    a.final_color (y, y, y, &rr, &gg, &bb);
    data[i].red = rr;
    data[i].green = gg;
    data[i].blue = bb;
  }
  return data;
}

/* Fetch histogram for the current scan area.  */
std::shared_ptr<histogram>
render::get_image_layer_histogram (progress_info *progress)
{
  image_layer_histogram_params p = {m_gray_data_id, m_params.get_scan_crop (m_img.width, m_img.height), this};
  return image_layer_histogram_cache.get (p, progress);
}

/* Compute histogram for HD X axis.  */
std::vector<uint64_t>
hd_x_histogram (render_parameters &rparam, image_data &img, int steps, luminosity_t minx, luminosity_t maxx, hd_axis_type axis_type, progress_info *progress)
{
  /* TODO: Handle stitched projects.  */
  if (img.stitch)
    return {};
  render r (img, rparam, 256);
  if (!r.precompute_all (true, false, {1, 1, 1}, progress))
    return {};
  auto hist = r.get_image_layer_histogram (progress);
  if (!hist)
    return {};
  std::vector<uint64_t> data (steps);
  for (size_t i = 0 ; i < hist->n_entries (); i++)
    {
      luminosity_t xv = hist->index_to_val (i);
      xv = (xv - rparam.dark_point) * rparam.scan_exposure;
      luminosity_t x = hd_linear_to_axis_x (xv, axis_type, rparam.contact_copy.preflash, rparam.contact_copy.exposure);
      if (x < minx || x > maxx)
	continue;
      int idx = nearest_int ((x - minx) * (steps-1) / (maxx - minx));
      if (idx == steps)
	idx = steps-1;
      data[idx] += hist->entry (i);
    }
  return data;
}
}
