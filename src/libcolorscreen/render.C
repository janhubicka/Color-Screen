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

render::render (scr_to_img_parameters &param, image_data &img, render_parameters &params, int dst_maxval)
{
  m_img = img;
  m_scr_to_img.set_parameters (param);
  m_dst_maxval = dst_maxval;
  m_params = params;
}

void
render::precompute_all ()
{
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
      luminosity_t gamma = std::min (std::max (m_params.gamma, 0.0001), 10.0);
      luminosity_t min = pow (m_params.gray_min / (luminosity_t)m_img.maxval, gamma);
      luminosity_t max = pow (m_params.gray_max / (luminosity_t)m_img.maxval, gamma);
      if (min >= max)
	max += 0.0001;
      for (int i = 0; i <= m_img.maxval; i++)
	lookup_table [i] = (pow (i / (luminosity_t)m_img.maxval, gamma) - min) * (1 / (max-min));
    }
  if (m_dst_maxval != out_lookup_table_maxval)
    {
      assert (!out_lookup_table_uses);
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

render::~render ()
{
  lookup_table_uses --;
  out_lookup_table_uses --;
}
