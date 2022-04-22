#include <cassert>
#include "include/render.h"
static int lookup_table_uses;
static int out_lookup_table_uses;
static int lookup_table_maxval;
static int out_lookup_table_maxval;
static int lookup_table_gray_min, lookup_table_gray_max;
static luminosity_t lookup_table_gamma;
static luminosity_t lookup_table[65536];
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
  double sum = red + green + blue;
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
  luminosity_t *out_table = (luminosity_t *)malloc (sizeof (luminosity_t) * 65536);

  for (int i = 0; i <= img.maxval; i++)
   {
     luminosity_t l = pow (i / (luminosity_t)img.maxval, gamma);
     if (l < 0 || l > 1)
       abort ();
     rtable[i] = l * red;
     gtable[i] = l * green;
     btable[i] = l * blue;
   }
  for (int i = 0; i < 65536; i++)
   out_table[i] = pow (i / 65535.0, 1 / gamma) * img.maxval;
#pragma omp parallel shared(cached_data,rtable,gtable,btable,out_table,img) default(none)
  for (int y = 0; y < img.height; y++)
    for (int x = 0; x < img.width; x++)
     {
       cached_data[y][x] = out_table[(int)((rtable[img.rgbdata[y][x].r]
					    + gtable[img.rgbdata[y][x].g]
					    + btable[img.rgbdata[y][x].b]) * 65535)];
     }

  free (rtable);
  free (gtable);
  free (btable);
  free (out_table);
  return true;
}

void
render::precompute_all ()
{
  if (!m_data)
    {
      compute_grayscale (m_img, m_params.mix_gamma, m_params.mix_red, m_params.mix_green, m_params.mix_blue);
      m_data = cached_data;
    }
  m_lookup_table = lookup_table;
  m_out_lookup_table = out_lookup_table;
  if (lookup_table_maxval != m_img.maxval || lookup_table_gamma != m_params.gamma
      || lookup_table_gray_min != m_params.gray_min || lookup_table_gray_max != m_params.gray_max)
    {
      assert (!lookup_table_uses);
      assert (m_img.maxval < 65536);
      lookup_table_gamma = m_params.gamma; 
      lookup_table_maxval = m_img.maxval;
      lookup_table_gray_min = m_params.gray_min; 
      lookup_table_gray_max = m_params.gray_max;
      luminosity_t gamma = std::min (std::max (m_params.gamma, (luminosity_t)0.0001), (luminosity_t)100.0);
      luminosity_t min = pow (m_params.gray_min / (luminosity_t)m_img.maxval, gamma);
      luminosity_t max = pow (m_params.gray_max / (luminosity_t)m_img.maxval, gamma);
      if (min == max)
	max += 0.0001;
      for (int i = 0; i <= m_img.maxval; i++)
	lookup_table [i] = (pow (i / (luminosity_t)m_img.maxval, gamma) - min) * (1 / (max-min));
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
  if (m_params.color_model == 1 || m_params.color_model == 2)
    {
      if (m_scr_to_img.get_type () != Dufay)
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
      if (m_scr_to_img.get_type () != Dufay)
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
          color = m * color;
	}
    }
  if (m_params.saturation != 1)
    {
      saturation_matrix m (m_params.saturation);
      color = m * color;
    }
  color = color * m_params.brightness;
  m_color_matrix = color;
}

/* Return approximate size of an scan pixel in screen corrdinates.  */
coord_t
render::pixel_size ()
{
  coord_t x,x2, y, y2;
  m_scr_to_img.to_scr (0, 0, &x, &y);
  m_scr_to_img.to_scr (1, 0, &x2, &y2);
  return sqrt ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y));
}

render::~render ()
{
  if (m_lookup_table)
    {
      lookup_table_uses --;
      out_lookup_table_uses --;
    }
}
