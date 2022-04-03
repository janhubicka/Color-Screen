#include <cassert>
#include "include/render.h"
static double lookup_table_uses;
static double out_lookup_table_uses;
static int lookup_table_maxval;
static int out_lookup_table_maxval;
static int lookup_table_gray_min, lookup_table_gray_max;
static double lookup_table_gamma;
static double lookup_table[65536];
static double out_lookup_table[65536];

render::render (scr_to_img_parameters param, image_data &img, int dst_maxval)
{
  m_img = img;
  m_scr_to_img.set_parameters (param);
  m_dst_maxval = dst_maxval;
  m_gray_min = 0;
  m_gray_max = img.maxval;
  m_lookup_table = NULL;
  m_out_lookup_table = NULL;
  m_saturate = 1;
  m_brightness = 1;
}

void
render::precompute_all ()
{
  m_lookup_table = lookup_table;
  m_out_lookup_table = out_lookup_table;
  if (lookup_table_maxval != m_img.maxval || lookup_table_gamma != m_img.gamma
      || lookup_table_gray_min != m_gray_min || lookup_table_gray_max != m_gray_max)
    {
      assert (!lookup_table_uses);
      assert (m_img.maxval < 65536);
      lookup_table_gamma = m_img.gamma; 
      lookup_table_maxval = m_img.maxval;
      lookup_table_gray_min = m_gray_min; 
      lookup_table_gray_max = m_gray_max;
      double gamma = std::min (std::max (m_img.gamma, 0.0001), 10.0);
      double min = pow (m_gray_min / (double)m_img.maxval, gamma);
      double max = pow (m_gray_max / (double)m_img.maxval, gamma);
      if (min >= max)
	max += 0.0001;
      for (int i = 0; i <= m_img.maxval; i++)
	lookup_table [i] = (pow (i / (double)m_img.maxval, gamma) - min) * (1 / (max-min));
    }
  if (m_dst_maxval != out_lookup_table_maxval)
    {
      assert (!out_lookup_table_uses);
      for (int i = 0; i < 65536; i++)
	out_lookup_table[i] = pow ((i+ 0.5) / 65535, 1/2.2) * m_dst_maxval;
    }
  lookup_table_uses ++;
  out_lookup_table_uses ++;

  matrix4x4 color;
  if (m_color_model == 1)
    {
      if (m_scr_to_img.get_type () != Dufay)
	{
	  finlay_matrix m;
	  xyz_srgb_matrix m2;
	  matrix4x4 mm;
	  mm = m * m2;
	  mm.normalize_grayscale ();
	  color = color * mm;
	}
      else
	{
	  dufay_matrix m;
	  xyz_srgb_matrix m2;
	  matrix4x4 mm;
	  mm = m * m2;
	  mm.normalize_grayscale (1.02, 1.05, 1);
	  color = color * mm;
	}
    }
  if (m_color_model == 2)
    {
      grading_matrix m;
      color = color * m;
    }
  if (m_saturate != 1)
    {
      saturation_matrix m (m_saturate);
      color = color * m;
    }
  color = color * m_brightness;
  m_color_matrix = color;
}

render::~render ()
{
  lookup_table_uses --;
  out_lookup_table_uses --;
}
