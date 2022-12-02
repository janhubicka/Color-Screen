#include <cassert>
#include "include/render.h"
#include "lru-cache.h"
#include "include/sensitivity.h"
#include "gaussian-blur.h"

class lru_caches lru_caches;
std::atomic_ulong lru_caches::time;

const char * render_parameters::color_model_names [] = {
  "none",
  "red",
  "green",
  "blue",
  "Wall_max_separation",
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
  /* Characcteristic curve.  */
  hd_curve *film_characteristic_curve;

  bool restore_original_luminosity;

  bool
  operator==(lookup_table_params &o)
  {
    return img_maxval == o.img_maxval
	   && maxval == o.maxval
	   && gamma == o.gamma
	   && gray_min == o.gray_min
	   && gray_max == o.gray_max
	   /* TODO: Invent IDs!
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
  bool invert = p.gray_min > p.gray_max;
  luminosity_t min = pow ((p.gray_min + 0.5) / (luminosity_t)p.img_maxval, gamma);
  luminosity_t max = pow ((p.gray_max + 0.5) / (luminosity_t)p.img_maxval, gamma);

  if (min == max)
    max += 0.0001;
  if (!invert)
    {
      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = (pow ((i + 0.5) / (luminosity_t)p.maxval, gamma) - min) * (1 / (max-min));
    }
  else if (p.restore_original_luminosity)
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();
      min = s.unapply (min);
      max = s.unapply (max);

      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = (s.unapply (pow ((i + 0.5) / (luminosity_t)p.maxval, gamma)) - min) * (1 / (max-min));
    }
  else
    {
      film_sensitivity s (p.film_characteristic_curve);
      s.precompute ();
      min = s.apply (min);
      max = s.apply (max);

      for (int i = 0; i <= p.maxval; i++)
	lookup_table[i] = (s.apply (pow ((i + 0.5) / (luminosity_t)p.maxval, gamma)) - min) * (1 / (max-min));
    }
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

struct sharpen_params
{
  luminosity_t radius;
  luminosity_t amount;
  unsigned long gray_data_id;

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

void
blur_horisontal (luminosity_t *out, luminosity_t *lookup_table, unsigned short *data, int width, int clen, luminosity_t *cmatrix)
{
  if (width < clen)
  {
    for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
    {
      luminosity_t sum = 0;
      for (int d = std::max (- clen / 2, -x); d < std::min (clen / 2, width - x); d++)
	sum += cmatrix[d + clen / 2] * lookup_table [data[x + d]];
      out[x] = sum;
    }
    return;
  }
      
   
  for (int x = 0; x < std::min (width - clen / 2, clen / 2); x++)
    {
      luminosity_t sum = 0;
      for (int d = -x; d < clen / 2; d++)
	sum += cmatrix[d + clen / 2] * lookup_table [data[x + d]];
      out[x] = sum;
    }
  for (int x = clen / 2; x < width - clen / 2; x++)
    {
      luminosity_t sum = 0;
      for (int d = - clen / 2; d < clen / 2; d++)
	sum += cmatrix[d + clen / 2] * lookup_table [data[x + d]];
      out[x] = sum;
    }
  for (int x = width - clen / 2; x < width; x++)
    {
      luminosity_t sum = 0;
      for (int d = - clen / 2; d < width - x; d++)
	sum += cmatrix[d + clen / 2] * lookup_table [data[x + d]];
      out[x] = sum;
    }
}

luminosity_t *
get_new_sharpened_data (struct sharpen_params &p, progress_info *progress)
{
  luminosity_t *cmatrix;
  int clen = fir_blur::gen_convolve_matrix (p.radius, &cmatrix);
  if (!clen)
    return NULL;
  if (progress)
    progress->set_task ("sharpening", p.height);
  luminosity_t *out = (luminosity_t *)calloc (p.width * p.height, sizeof (luminosity_t));
#pragma omp parallel shared(progress,out,clen,cmatrix,p) default(none)
    {
      luminosity_t *hblur = (luminosity_t *)calloc (p.width * clen, sizeof (luminosity_t));
      luminosity_t *rotated_cmatrix = (luminosity_t *)malloc (clen * sizeof (luminosity_t));
#ifdef _OPENMP
      int tn = omp_get_thread_num ();
      int threads = omp_get_max_threads ();
#else
      int tn = 0;
      int threads = 1;
#endif
      int ystart = tn * p.height / threads;
      int yend = (tn + 1) * p.height / threads - 1;

      for (int d = -clen/2; d < clen/2 - 1; d++)
	{
	  int yp = ystart + d;
	  int tp = (yp + clen) % clen;
	  if (yp < 0 || yp > p.height)
	    memset (hblur + tp * p.width, 0, sizeof (luminosity_t) * p.width);
	  else
	    blur_horisontal (hblur + tp * p.width, p.lookup_table, p.gray_data[yp], p.width, clen, cmatrix);
	}
      for (int y = ystart; y <= yend; y++)
	{
	  if (y + clen / 2 - 1 < p.height)
	    blur_horisontal (hblur + ((y + clen / 2 - 1 + clen) % clen) * p.width, p.lookup_table, p.gray_data[y + clen / 2 - 1], p.width, clen, cmatrix);
	  else
	    memset (hblur + ((y + clen / 2 - 1 + clen) % clen) * p.width, 0, sizeof (luminosity_t) * p.width);
	  for (int d = 0; d < clen; d++)
	    rotated_cmatrix[(y + d - clen / 2 + clen) % clen] = cmatrix[d];
	  for (int x = 0; x < p.width; x++)
	    {
	      luminosity_t sum = 0;
	      for (int d = 0; d < clen; d++)
		sum += rotated_cmatrix[d] * hblur[d * p.width + x];
	      luminosity_t orig = p.lookup_table [p.gray_data[y][x]];
	      out[y * p.width + x] = orig + (orig - sum) * p.amount;
	    }
	  if (progress)
	     progress->inc_progress ();
	}
      free (rotated_cmatrix);
      free (hblur);
    }

  free (cmatrix);
  return out;
}
static lru_cache <sharpen_params, luminosity_t, get_new_sharpened_data, 1> sharpened_data_cache;


}

bool
render::precompute_all (bool duffay, progress_info *progress)
{
  lookup_table_params par = {m_img.maxval, m_maxval, m_params.gamma, m_params.gray_min, m_params.gray_max, m_params.film_characteristics_curve, m_params.restore_original_luminosity};
  m_lookup_table = lookup_table_cache.get (par, progress, &m_lookup_table_id);
  if (m_img.rgbdata)
    {
      lookup_table_params rgb_par = {m_img.maxval, m_img.maxval, m_params.gamma, m_params.gray_min, m_params.gray_max, m_params.film_characteristics_curve, m_params.restore_original_luminosity};
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
  if (m_params.sharpen_radius && m_params.sharpen_amount)
    {
      sharpen_params p = {m_params.sharpen_radius, m_params.sharpen_amount, m_gray_data_id, m_gray_data, m_lookup_table, m_lookup_table_id, m_img.width, m_img.height};
      m_sharpened_data = sharpened_data_cache.get (p, progress);
    }

  color_matrix color;
  /* Matrix converting dyes either to XYZ (default) or sRGB.  */
  color_matrix dyes;
  bool is_srgb = false;
  switch (m_params.color_model)
    {
      /* No color adjustemnts: dyes are translated to sRGB.  */
      case render_parameters::color_model_none:
	is_srgb = true;
	break;
      case render_parameters::color_model_red:
	{
	  color_matrix m (1, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0.5, 0, 0, 0,
			  0, 0, 0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_green:
	{
	  color_matrix m (0, 0.5,  0, 0,
			  0, 1,0, 0,
			  0, 0.5,0, 0,
			  0, 0, 0,1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      case render_parameters::color_model_blue:
	{
	  color_matrix m (0, 0, 0.5,  0,
			  0, 0, 0.5,0,
			  0, 0, 1,0,
			  0, 0, 0, 1);
	  dyes = m;
	  is_srgb = true;
	}
	break;
      /* Color based on frequencies determined in Wall's Practical Color Photography
         as triggering best individual stimulus of an eye.  */
      case render_parameters::color_model_max_separation:
	{
	  dyes = matrix_by_dye_xy (0.7319933,0.2680067,  /*670nm */
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  break;
	}
      /* Colors found to be working for Finlays and Pagets pretty well.  */
      case render_parameters::color_model_paget:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325, 
				   0.059325533,0.829425776, /*518nm */
				   0.143960396, 0.02970297 /*460nm */);
	  break;
#if 0
	  adjusted_finlay_matrix m;
	  xyz_srgb_matrix m2;
	  color_matrix mm;
	  xyz white;
	  srgb_to_xyz (1, 1, 1, &white.x, &white.y, &white.z);
	  m.normalize_grayscale (white.x, white.y, white.z);
	  mm = m2 * m;
	  //mm.normalize_grayscale ();
	  color = mm * color;
	  break;
#endif
	}
      /* Colors derived from reconstructed filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_reconstructed_wager:
	{
	  dyes = matrix_by_dye_xy (0.674, 0.325,
				   0.182, 0.747,
				   0.151, 0.041);
	  break;
	}
      /* Colors derived from filters for Miethe-Goerz projector by Jens Wagner.  */
      case render_parameters::color_model_miethe_goerz_original_wager:
	{
	  dyes = matrix_by_dye_xy (0.620, 0.315,
				   0.304, 0.541,
				   0.182, 0.135);
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
  else if (is_srgb)
    {
      if (m_params.presaturation != 1)
	{
	  presaturation_matrix m (m_params.presaturation);
	  color = m * color;
	}
      color = dyes * color;
    }
  else
    {
      if (m_params.presaturation != 1)
	{
	  presaturation_matrix m (m_params.presaturation);
	  color = m * color;
	}
      color = dyes * color;
      if (m_params.backlight_temperature != 6500)
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
  if (m_sharpened_data)
    sharpened_data_cache.release (m_sharpened_data);
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
