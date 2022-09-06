#include <cassert>
#include "include/render.h"

const char * render_parameters::color_model_names [] = {
  "none",
  "paget",
  "duffay1",
  "duffay2",
  "autochrome"
};
const char * render_parameters::dye_balance_names [] = {
  "none",
  "neutral",
  "whitepoint"
};

static int lookup_table_uses;
static int out_lookup_table_uses;
static int lookup_table_maxval;
static int lookup_table_rgbmaxval;
static int out_lookup_table_maxval;
static int lookup_table_gray_min, lookup_table_gray_max;
static luminosity_t lookup_table_gamma;
static luminosity_t lookup_table[65536];
static luminosity_t rgb_lookup_table[65536];
static luminosity_t out_lookup_table[65536];

static int cached_image_id = -1;
static luminosity_t cached_gamma, cached_red, cached_blue, cached_green;
static unsigned short **cached_data;


static bool
compute_grayscale (image_data &img,
		   luminosity_t gamma, luminosity_t red, luminosity_t green, luminosity_t blue)
{
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
    return false;

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
  return true;
}

void
render::precompute_all (bool duffay)
{
  bool recompute = false;
  if (!m_data)
    {
      compute_grayscale (m_img, m_params.mix_gamma, m_params.mix_red, m_params.mix_green, m_params.mix_blue);
      m_data = cached_data;
    }
  luminosity_t gamma = std::min (std::max (m_params.gamma, (luminosity_t)0.0001), (luminosity_t)100.0);
  luminosity_t min = pow (m_params.gray_min / (luminosity_t)m_img.maxval, gamma);
  luminosity_t max = pow (m_params.gray_max / (luminosity_t)m_img.maxval, gamma);

  m_lookup_table = lookup_table;
  m_out_lookup_table = out_lookup_table;
  if (lookup_table_maxval != m_maxval || lookup_table_gamma != gamma
      || lookup_table_gray_min != min || lookup_table_gray_max != max)
    {
      assert (!lookup_table_uses);
      assert (m_maxval < 65536);
      lookup_table_gamma = m_params.gamma; 
      lookup_table_maxval = m_maxval;
      lookup_table_gray_min = min; 
      lookup_table_gray_max = max;
      recompute = true;

      if (min == max)
	max += 0.0001;
      for (int i = 0; i <= m_maxval; i++)
	lookup_table [i] = (pow (i / (luminosity_t)m_maxval, gamma) - min) * (1 / (max-min));
    }
  if (m_img.rgbdata)
    {
      m_rgb_lookup_table = rgb_lookup_table;
      if (recompute || lookup_table_rgbmaxval != m_img.maxval)
	{
	  assert (m_img.maxval < 65536);
	  if (min == max)
	    max += 0.0001;
	  for (int i = 0; i <= m_img.maxval; i++)
	    rgb_lookup_table [i] = (pow (i / (luminosity_t)m_img.maxval, gamma) - min) * (1 / (max-min));
	}
    }
  else
    {
      m_rgb_lookup_table = NULL;
      lookup_table_rgbmaxval = -1;
    }
  if (m_dst_maxval != out_lookup_table_maxval)
    {
      assert (!out_lookup_table_uses);
      out_lookup_table_maxval = m_dst_maxval;
      for (int i = 0; i < 65536; i++)
	out_lookup_table[i] = linear_to_srgb ((i+ 0.5) / 65535) * m_dst_maxval;
    }
  lookup_table_uses ++;
  out_lookup_table_uses ++;

  color_matrix color;
  if (m_params.presaturation != 1)
    {
      presaturation_matrix m (m_params.presaturation);
      color = m * color;
    }
  switch (m_params.color_model)
    {
      /* No color adjustemnts: dyes are translated to sRGB.  */
      case render_parameters::color_model_none:
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
      case render_parameters::color_model_duffay1:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_duffay (0);
	  break;
	}
      case render_parameters::color_model_duffay2:
	{
	  m_spectrum_dyes_to_xyz = new (spectrum_dyes_to_xyz);
	  m_spectrum_dyes_to_xyz->set_dyes_to_duffay (1);
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
    {
      lookup_table_uses --;
      out_lookup_table_uses --;
    }
  if (m_spectrum_dyes_to_xyz)
    delete m_spectrum_dyes_to_xyz;
}
void
render::get_gray_data (luminosity_t *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{
  downscale<render, luminosity_t, &render::get_data, &render::account_pixel> (data, x, y, width, height, pixelsize);
}
void
render::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{
  downscale<render, rgbdata, &render::get_rgb_pixel, &render::account_rgb_pixel> (data, x, y, width, height, pixelsize);
}
