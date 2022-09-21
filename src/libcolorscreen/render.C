#include <cassert>
#include "include/render.h"
#include "lru-cache.h"

const char * render_parameters::color_model_names [] = {
  "none",
  "red",
  "green",
  "blue",
  "paget",
  "dufaycolor_NSMM_Bradford_11948",
  "duffay2",
  "duffay3",
  "duffay4",
  "duffay5",
  "autochrome-film",
  "autochrome-Casella-Tsukada",
};
const char * render_parameters::dye_balance_names [] = {
  "none",
  "neutral",
  "whitepoint"
};

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
get_new_lookup_table (struct lookup_table_params &p)
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
get_new_out_lookup_table (struct out_lookup_table_params &p)
{
  luminosity_t *lookup_table = new luminosity_t[65536];
  printf ("Output table for %i\n", p.maxval);
  for (int i = 0; i < 65536; i++)
    lookup_table[i] = linear_to_srgb ((i+ 0.5) / 65535) * p.maxval;
  return lookup_table;
}

/* To improve interactive response we cache conversion tables.  */
static lru_cache <lookup_table_params, luminosity_t, get_new_lookup_table, 4> lookup_table_cache;
static lru_cache <out_lookup_table_params, luminosity_t, get_new_out_lookup_table, 4> out_lookup_table_cache;


#if 0
struct graydata_params
{
  /* Pointers in image_data may become stale if image is freed. Use ID
     to check cache entries.  */
  int image_id;
  image_data img;
  luminosity_t gamma, red, blue, green;
};
static char *
get_new_graydata (struct graydata_params &p)
{
  if (p.img.data)
    return false;

  double red = p.red;
  double green = p.green;
  double blue = p.blue;

  /* TODO move out.  */
  if (p.gamma < 0.001)
    p.gamma = 0.001;
  if (p.gamma > 1000)
    p.gamma = 1000;

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


  cached_data = (unsigned short **)malloc (sizeof (*cached_data) * img.height);
  if (!cached_data)
    {
      cached_data = NULL;
      fprintf (stderr, "Out of memory\n");
      abort ();
    }
  cached_data[0] = (unsigned short *)malloc (img.width * img.height * sizeof (**cached_data));
  if (!cached_data [0])
    {
      free (cached_data);
      cached_data = NULL;
      fprintf (stderr, "Out of memory\n");
      abort ();
    }
  for (int i = 1; i < img.height; i++)
    cached_data[i] = cached_data[0] + i * img.width;

  cached_red = red;
  cached_green = green;
  cached_blue = blue;
  cached_gamma = gamma;

  luminosity_t *rtable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  luminosity_t *gtable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  luminosity_t *btable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  unsigned short *out_table = (unsigned short *)malloc (sizeof (luminosity_t) * 65536);

  for (int i = 0; i <= img.maxval; i++)
    {
      luminosity_t l = pow (i / (luminosity_t)img.maxval, gamma);
      if (l < 0 || l > 1)
	abort ();
      rtable[i] = l * red;
      gtable[i] = l * green;
      btable[i] = l * blue;
      //fprintf (stderr, "%f %f %f\n", rtable[i], gtable[i], btable[i]);
    }
  for (int i = 0; i < 65536; i++)
    {
      out_table[i] = pow (i / 65535.0, 1 / gamma) * 65535;
      //fprintf (stderr, "%i\n", out_table[i]);
    }
#pragma omp parallel shared(cached_data,rtable,gtable,btable,out_table,img) default(none)
  for (int y = 0; y < img.height; y++)
    for (int x = 0; x < img.width; x++)
     {
       luminosity_t val = rtable[img.rgbdata[y][x].r]
			  + gtable[img.rgbdata[y][x].g]
			  + btable[img.rgbdata[y][x].b];
       val = std::max (std::min (val, (luminosity_t)1.0), (luminosity_t)0.0);
       cached_data[y][x] = out_table[(int)(val * 65535 + (luminosity_t)0.5)];
     }

  free (rtable);
  free (gtable);
  free (btable);
  free (out_table);
  return true;
}
#endif

/* TODO: turn this also into cache.  */
static int cached_image_id = -1;
static luminosity_t cached_gamma, cached_red, cached_blue, cached_green;
static unsigned short **cached_data;
static pthread_mutex_t lock;

__attribute__ ((constructor))
void
render_init ()
{
  if (pthread_mutex_init(&lock, NULL) != 0)
    abort ();
}

static bool
compute_grayscale (image_data &img,
		   luminosity_t gamma, luminosity_t red, luminosity_t green, luminosity_t blue)
{
  pthread_mutex_lock (&lock);
  if (img.data)
    return false;
  if (gamma < 0.001)
    gamma = 0.001;
  if (gamma > 1000)
    gamma = 1000;
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
  if (cached_gamma == gamma && cached_red == red && cached_blue == blue && cached_green == green
      && cached_image_id == img.id)
  {
    pthread_mutex_unlock (&lock);
    return false;
  }

  cached_gamma = gamma;
  cached_red = red;
  cached_green = green;
  cached_blue = blue;
  cached_image_id = img.id;

  if (cached_data)
    {
      free (*cached_data);
      free (cached_data);
    }

  cached_data = (unsigned short **)malloc (sizeof (*cached_data) * img.height);
  if (!cached_data)
    {
      cached_data = NULL;
      fprintf (stderr, "Out of memory\n");
      abort ();
    }
  cached_data[0] = (unsigned short *)malloc (img.width * img.height * sizeof (**cached_data));
  if (!cached_data [0])
    {
      free (cached_data);
      cached_data = NULL;
      fprintf (stderr, "Out of memory\n");
      abort ();
    }
  for (int i = 1; i < img.height; i++)
    cached_data[i] = cached_data[0] + i * img.width;

  cached_red = red;
  cached_green = green;
  cached_blue = blue;
  cached_gamma = gamma;

  luminosity_t *rtable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  luminosity_t *gtable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  luminosity_t *btable = (luminosity_t *)malloc (sizeof (luminosity_t) * img.maxval);
  unsigned short *out_table = (unsigned short *)malloc (sizeof (luminosity_t) * 65536);

  for (int i = 0; i <= img.maxval; i++)
    {
      luminosity_t l = pow (i / (luminosity_t)img.maxval, gamma);
      if (l < 0 || l > 1)
	abort ();
      rtable[i] = l * red;
      gtable[i] = l * green;
      btable[i] = l * blue;
      //fprintf (stderr, "%f %f %f\n", rtable[i], gtable[i], btable[i]);
    }
  for (int i = 0; i < 65536; i++)
    {
      out_table[i] = pow (i / 65535.0, 1 / gamma) * 65535;
      //fprintf (stderr, "%i\n", out_table[i]);
    }
#pragma omp parallel shared(cached_data,rtable,gtable,btable,out_table,img) default(none)
  for (int y = 0; y < img.height; y++)
    for (int x = 0; x < img.width; x++)
     {
       luminosity_t val = rtable[img.rgbdata[y][x].r]
			  + gtable[img.rgbdata[y][x].g]
			  + btable[img.rgbdata[y][x].b];
       val = std::max (std::min (val, (luminosity_t)1.0), (luminosity_t)0.0);
       cached_data[y][x] = out_table[(int)(val * 65535 + (luminosity_t)0.5)];
     }

  free (rtable);
  free (gtable);
  free (btable);
  free (out_table);
  pthread_mutex_unlock (&lock);
  return true;
}
}

void
render::precompute_all (bool duffay)
{
  lookup_table_params par = {m_img.maxval, m_maxval, m_params.gamma, m_params.gray_min, m_params.gray_max};
  m_lookup_table = lookup_table_cache.get (par);
  if (m_img.rgbdata)
    {
      lookup_table_params rgb_par = {m_img.maxval, m_img.maxval, m_params.gamma, m_params.gray_min, m_params.gray_max};
      m_rgb_lookup_table = lookup_table_cache.get (rgb_par);
    }
  out_lookup_table_params out_par = {m_dst_maxval};
  m_out_lookup_table = out_lookup_table_cache.get (out_par);


  if (!m_data)
    {
      compute_grayscale (m_img, m_params.mix_gamma, m_params.mix_red, m_params.mix_green, m_params.mix_blue);
      m_data = cached_data;
    }

  color_matrix color;
  /* We can combine presaturation to the matrix for simple matrix
     transformations.  For non-linear transformations it needs to be done
     separately.  */
  if (m_params.presaturation != 1
      && (m_params.color_model == render_parameters::color_model_none
	  || m_params.color_model == render_parameters::color_model_paget))
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
      xyz_srgb_matrix m2;

      color = m2 * color;
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
    }
#if 0
  if (m_params.color_model == 1 || m_params.color_model == 2)
    {
      if (!duffay)
	{
	  finlay_matrix m;
	  xyz_srgb_matrix m2;
	  color_matrix mm;
	  mm = m2 * m;
	  if (m_params.color_model != 2)
	    mm.normalize_grayscale ();
	  color = mm * color;
	}
      else
	{
	  dufay_matrix m;
	  xyz_srgb_matrix m2;
	  color_matrix mm;
	  mm = m2 * m;
	  if (m_params.color_model != 2)
	    mm.normalize_grayscale (1.02, 1.05, 1);
	  color = mm * color;
	}
    }
  if (m_params.color_model == 3)
    {
      if (!duffay)
	{
	  adjusted_finlay_matrix m;
	  xyz_srgb_matrix m2;
	  color_matrix mm;
	  mm = m2 * m;
	  mm.normalize_grayscale ();
	  color = mm * color;
	}
      else
        {
          grading_matrix m;
          autochrome_matrix m;
	  m.normalize_grayscale ();
          color = m * color;
	}
    }
#endif
  if (m_params.saturation != 1)
    {
      saturation_matrix m (m_params.saturation);
      color = m * color;
    }
  color = color * m_params.brightness;
  m_color_matrix = color;
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
}

/* Compute lookup table converting image_data to range 0...1 with GAMMA.  */
luminosity_t *
render::get_lookup_table (luminosity_t gamma, int maxval)
{
  lookup_table_params par = {maxval, maxval, gamma, 0, maxval};
  return lookup_table_cache.get (par);
}

/* Release lookup table.  */
void
render::release_lookup_table (luminosity_t *table)
{
  out_lookup_table_cache.release (table);
}

/* Compute graydata of downscaled image.  */
void
render::get_gray_data (luminosity_t *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{
  downscale<render, luminosity_t, &render::get_data, &account_pixel> (data, x, y, width, height, pixelsize);
}

/* Compute RGB data of downscaled image.  */
void
render::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{
  downscale<render, rgbdata, &render::get_rgb_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize);
}
