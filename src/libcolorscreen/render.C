#include <cassert>
#include "include/render.h"
#include "lru-cache.h"

class lru_caches lru_caches;
std::atomic_ulong lru_caches::time;

const char * render_parameters::color_model_names [] = {
  "none",
  "red",
  "green",
  "blue",
  "paget",
  "Miethe_Goerz_reconstructed_by_Wagner",
  "Miethe_Goerz_mesured_by_Wagner",
  "dufaycolor_NSMM_Bradford_11948",
  "dufaycolor_NSMM_Bradford_11951",
  "dufaycolor_NSMM_Bradford_11960",
  "dufaycolor_NSMM_Bradford_11967",
  "spicer_dufay_NSMM_Bradford_12075",
  "cinecolor_koshofer",
  "autochrome_Casella_Tsukada",
};
const char * render_parameters::dye_balance_names [] = {
  "none",
  "neutral",
  "whitepoint"
};

/* A wrapper class around m_gray_data which handles allocation and dealocation.
   This is needed for the cache.  */
class gray_data
{
public:
  unsigned short **m_gray_data;
  gray_data (int width, int height);
  ~gray_data();
};

gray_data::gray_data (int width, int height)
{
  m_gray_data = (unsigned short **)malloc (sizeof (*m_gray_data) * height);
  if (!m_gray_data)
    {
      m_gray_data = NULL;
      return;
    }
  m_gray_data[0] = (unsigned short *)malloc (width * height * sizeof (**m_gray_data));
  if (!m_gray_data [0])
    {
      free (m_gray_data);
      m_gray_data = NULL;
      return;
    }
  for (int i = 1; i < height; i++)
    m_gray_data[i] = m_gray_data[0] + i * width;
}
gray_data::~gray_data()
{
  if (m_gray_data)
    {
      free (m_gray_data[0]);
      free (m_gray_data);
    }
}

namespace
{

/* Lookup table translates raw input data into linear values.  */
struct lookup_table_params
{
  /* image_data are in range 0...img_maxval.  */
  int img_maxval;
  /* precomputed gray data (created from mixing r,g,b channles of image_data)
     have range 0...maxval.  */
  int maxval;
  /* Input data are assumed to have gamma.  Inverse of gamma is applied to
     get linear data.  */
  luminosity_t gamma;
  /* gray_min becomes 0, while gray_max becomes 1.  */
  int gray_min, gray_max;

  bool
  operator==(lookup_table_params &o)
  {
    return img_maxval == o.img_maxval
	   && maxval == o.maxval
	   && gamma == o.gamma
	   && gray_min == o.gray_min
	   && gray_max == o.gray_max;
  }
};

luminosity_t *
get_new_lookup_table (struct lookup_table_params &p, progress_info *)
{
  luminosity_t *lookup_table = new luminosity_t[p.maxval];
  luminosity_t gamma = std::min (std::max (p.gamma, (luminosity_t)0.0001), (luminosity_t)100.0);
  luminosity_t min = pow (p.gray_min / (luminosity_t)p.img_maxval, gamma);
  luminosity_t max = pow (p.gray_max / (luminosity_t)p.img_maxval, gamma);

  //printf ("Lookup table for %i %i %f %i %i\n", p.img_maxval,p.maxval,p.gamma,p.gray_min,p.gray_max);

  if (min == max)
    max += 0.0001;
  for (int i = 0; i <= p.maxval; i++)
    lookup_table[i] = (pow (i / (luminosity_t)p.maxval, gamma) - min) * (1 / (max-min));
  return lookup_table;
}


/* Output lookup table takes linear r,g,b values in range 0...65536
   and outputs r,g,b values in sRGB gamma curve in range 0...maxval.  */
struct out_lookup_table_params
{
  int maxval;
  bool
  operator==(out_lookup_table_params &o)
  {
    return maxval == o.maxval;
  }
};

luminosity_t *
get_new_out_lookup_table (struct out_lookup_table_params &p, progress_info *)
{
  luminosity_t *lookup_table = new luminosity_t[65536];
  //printf ("Output table for %i\n", p.maxval);
  for (int i = 0; i < 65536; i++)
    lookup_table[i] = linear_to_srgb ((i+ 0.5) / 65535) * p.maxval;
  return lookup_table;
}

/* To improve interactive response we cache conversion tables.  */
static lru_cache <lookup_table_params, luminosity_t, get_new_lookup_table, 4> lookup_table_cache;
static lru_cache <out_lookup_table_params, luminosity_t, get_new_out_lookup_table, 4> out_lookup_table_cache;


struct graydata_params
{
  /* Pointers in image_data may become stale if image is freed. Use ID
     to check cache entries.  */
  unsigned long image_id;
  image_data *img;
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

/* Mix RGB channels into grayscale.  */
static gray_data *
get_new_graydata (struct graydata_params &p, progress_info *progress)
{
  double red = p.red;
  double green = p.green;
  double blue = p.blue;
  double sum = (red < 0 ? 0 : red) + (green < 0 ? 0 : green) + (blue < 0 ? 0 : blue);

  if (!sum)
    {
      sum = 1;
      green = 1;
      blue = red = 0;
    }
  red /= sum;
  green /= sum;
  blue /= sum;

  if (progress)
    progress->set_task ("computing grayscale", p.img->height);

  gray_data *data = new gray_data (p.img->width, p.img->height);
  if (!data || !data->m_gray_data)
    {
      if (data)
	delete data;
      return NULL;
    }


  luminosity_t *rtable = (luminosity_t *)malloc (sizeof (luminosity_t) * p.img->maxval);
  luminosity_t *gtable = (luminosity_t *)malloc (sizeof (luminosity_t) * p.img->maxval);
  luminosity_t *btable = (luminosity_t *)malloc (sizeof (luminosity_t) * p.img->maxval);
  unsigned short *out_table = (unsigned short *)malloc (sizeof (luminosity_t) * 65536);

  for (int i = 0; i <= p.img->maxval; i++)
    {
      luminosity_t l = pow (i / (double)p.img->maxval, p.gamma);
      if (l < 0 || l > 1)
	abort ();
      rtable[i] = l * red;
      gtable[i] = l * green;
      btable[i] = l * blue;
    }
  for (int i = 0; i < 65536; i++)
    {
      out_table[i] = pow (i / 65535.0, 1 / p.gamma) * 65535;
    }
#pragma omp parallel shared(data,rtable,gtable,btable,out_table,p,progress) default(none)
  for (int y = 0; y < p.img->height; y++)
    {
      for (int x = 0; x < p.img->width; x++)
	{
	  luminosity_t val = rtable[p.img->rgbdata[y][x].r]
			     + gtable[p.img->rgbdata[y][x].g]
			     + btable[p.img->rgbdata[y][x].b];
	  val = std::max (std::min (val, (luminosity_t)1.0), (luminosity_t)0.0);
	  data->m_gray_data[y][x] = out_table[(int)(val * 65535 + (luminosity_t)0.5)];
	}
      if (progress)
	 progress->inc_progress ();
    }

  free (rtable);
  free (gtable);
  free (btable);
  free (out_table);
  return data;
}
static lru_cache <graydata_params, gray_data, get_new_graydata, 1> gray_data_cache;
}

bool
render::precompute_all (bool duffay, progress_info *progress)
{
  lookup_table_params par = {m_img.maxval, m_maxval, m_params.gamma, m_params.gray_min, m_params.gray_max};
  m_lookup_table = lookup_table_cache.get (par, progress);
  if (m_img.rgbdata)
    {
      lookup_table_params rgb_par = {m_img.maxval, m_img.maxval, m_params.gamma, m_params.gray_min, m_params.gray_max};
      m_rgb_lookup_table = lookup_table_cache.get (rgb_par, progress);
    }
  out_lookup_table_params out_par = {m_dst_maxval};
  m_out_lookup_table = out_lookup_table_cache.get (out_par, progress);

  if (!m_gray_data)
    {
      graydata_params p = {m_img.id, &m_img, m_params.gamma, m_params.mix_red, m_params.mix_green, m_params.mix_blue};
      if (p.gamma < 0.001)
	p.gamma = 0.001;
      if (p.gamma > 1000)
	p.gamma = 1000;
      m_gray_data_holder = gray_data_cache.get (p, progress, &m_gray_data_id);
      m_gray_data = m_gray_data_holder->m_gray_data;
    }

  color_matrix color;
  /* We can combine presaturation to the matrix for simple matrix
     transformations.  For non-linear transformations it needs to be done
     separately since we apply the matrix only after the dye to XYZ conversion.  */
  if (m_params.presaturation != 1
      && (m_params.color_model == render_parameters::color_model_none
	  || m_params.color_model == render_parameters::color_model_paget
	  || m_params.color_model == render_parameters::color_model_miethe_goerz_reconstructed_wager
	  || m_params.color_model == render_parameters::color_model_miethe_goerz_original_wager))
    {
      presaturation_matrix m (m_params.presaturation);
      color = m * color;
    }
  switch (m_params.color_model)
    {
      /* No color adjustemnts: dyes are translated to sRGB.  */
      case render_parameters::color_model_none:
	break;
      case render_parameters::color_model_red:
	{
	  color_matrix m (1, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0, 0, 0, 1);
	  color = m * color;
	}
	break;
      case render_parameters::color_model_green:
	{
	  color_matrix m (0, 0.5,  0, 0,
			  0, 1,0, 0,
			  0, 0.5,0, 0,
			  0, 0, 0,1);
	  color = m * color;
	}
	break;
      case render_parameters::color_model_blue:
	{
	  color_matrix m (0, 0, 0.5,  0,
			  0, 0, 0.5,0,
			  0, 0, 1,0,
			  0, 0, 0, 1);
	  color = m * color;
	}
	break;
      /* Colors found to be working for Finlays and Pagets pretty well.  */
      case render_parameters::color_model_paget:
	{
	  adjusted_finlay_matrix m;
	  xyz_srgb_matrix m2;
	  color_matrix mm;
	  mm = m2 * m;
	  mm.normalize_grayscale ();
	  color = mm * color;
	  break;
	}
      /* Colors derived from reconstructed filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_reconstructed_wager:
	{
	  color_matrix m = matrix_by_dye_xy (0.674, 0.325,
					     0.182, 0.747,
					     0.151, 0.041), mm;
#if 0
	  xyz r = xyY_to_xyz (0.674, 0.325, 1);
	  xyz g = xyY_to_xyz (0.182, 0.747, 1);
	  xyz b = xyY_to_xyz (0.151, 0.041, 1);
	  color_matrix m (r.x, g.x, b.x, 0,
			  r.y, g.y, b.y, 0,
			  r.z, g.z, b.z, 0,
			  0,   0,   0,   1), mm;
	  xyz white;
	  srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
	  m.normalize_grayscale (white.x, white.y, white.z);
#endif
	  xyz_srgb_matrix m2;
	  mm = m2 * m;
	  //mm.normalize_grayscale ();
	  color = mm * color;
	  break;
	}
      /* Colors derived from filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_original_wager:
	{
#if 0
	  xyz r = xyY_to_xyz (0.620, 0.315, 1);
	  xyz g = xyY_to_xyz (0.304, 0.541, 1);
	  xyz b = xyY_to_xyz (0.182, 0.135, 1);
	  color_matrix m (r.x, g.x, b.x, 0,
			  r.y, g.y, b.y, 0,
			  r.z, g.z, b.z, 0,
			  0,   0,   0,   1), mm;
	  xyz white;
	  srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
	  m.normalize_grayscale (white.x, white.y, white.z);
#endif
	  color_matrix m = matrix_by_dye_xy (0.620, 0.315,
					     0.304, 0.541,
					     0.182, 0.135), mm;
	  xyz_srgb_matrix m2;
	  mm = m2 * m;
	  //mm.normalize_grayscale ();
	  color = mm * color;
	  break;
	}
      case render_parameters::color_model_autochrome:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_autochrome ();
	  break;
	}
      case render_parameters::color_model_autochrome2:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_autochrome2 (1, 1, 19.7 / (20.35),
							   1, 21 / (20.35),
							   1,1,m_params.age);
	  break;
	}
      case render_parameters::color_model_duffay1:
      case render_parameters::color_model_duffay2:
      case render_parameters::color_model_duffay3:
      case render_parameters::color_model_duffay4:
      case render_parameters::color_model_duffay5:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_duffay ((int)m_params.color_model - (int)render_parameters::color_model_duffay1);
	  break;
	}
      case render_parameters::color_model_max:
	abort ();
    }
  if (m_spectrum_dyes_to_xyz)
    {

      m_spectrum_dyes_to_xyz->set_daylight_backlight (m_params.backlight_temperature);
      switch (m_params.dye_balance)
	{
	  case render_parameters::dye_balance_none:
	    m_spectrum_dyes_to_xyz->normalize_brightness ();
	    break;
	  case render_parameters::dye_balance_neutral:
	    m_spectrum_dyes_to_xyz->normalize_dyes (6500);
	    break;
	  case render_parameters::dye_balance_whitepoint:
	    m_spectrum_dyes_to_xyz->normalize_xyz_to_backlight_whitepoint ();
	    break;
	  default:
	    abort ();
	}
      /* At the moment all conversion we do are linear conversions.  In that case
         we can build XYZ matrix and proceed with that.  */
      if (debug && !m_spectrum_dyes_to_xyz->is_linear ())
	{
	  xyz_srgb_matrix m2;
	  color = m2 * color;
	  /* There is disabled code in render.h to optimize codegen.  */
	  abort ();
	}
      else
	{
	  if (m_params.presaturation != 1)
	    {
	      presaturation_matrix m (m_params.presaturation);
	      color = m * color;
	    }
	  color_matrix mm, m = m_spectrum_dyes_to_xyz->xyz_matrix ();
	  xyz_srgb_matrix m2;
	  mm = m2 * m;
	  color = mm * color;
	  delete (m_spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz = NULL;
	}
    }
  if (m_params.saturation != 1)
    {
      saturation_matrix m (m_params.saturation);
      color = m * color;
    }
  color = color * m_params.brightness;
  m_color_matrix = color;
  return true;
}

render::~render ()
{
  if (m_lookup_table)
    lookup_table_cache.release (m_lookup_table);
  if (m_out_lookup_table)
    out_lookup_table_cache.release (m_out_lookup_table);
  if (m_rgb_lookup_table)
    lookup_table_cache.release (m_rgb_lookup_table);
  if (m_spectrum_dyes_to_xyz)
    delete m_spectrum_dyes_to_xyz;
  if (m_gray_data_holder)
    gray_data_cache.release (m_gray_data_holder);
}

/* Compute lookup table converting image_data to range 0...1 with GAMMA.  */
luminosity_t *
render::get_lookup_table (luminosity_t gamma, int maxval)
{
  lookup_table_params par = {maxval, maxval, gamma, 0, maxval};
  return lookup_table_cache.get (par, NULL);
}

/* Release lookup table.  */
void
render::release_lookup_table (luminosity_t *table)
{
  out_lookup_table_cache.release (table);
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
