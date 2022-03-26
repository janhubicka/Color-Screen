#include <cassert>
#include "render.h"
static double lookup_table_uses;
static int lookup_table_maxval;
static double lookup_table_gamma;
static double lookup_table[65536];

render::render (scr_to_img_parameters param, image_data &img, int dst_maxval)
{
  m_img = img;
  m_scr_to_img.set_parameters (param);
  m_dst_maxval = dst_maxval;
  m_lookup_table = lookup_table;
  if (lookup_table_maxval != img.maxval || lookup_table_gamma != 2.2)
    {
      assert (!lookup_table_uses);
      assert (img.maxval < 65536);
      lookup_table_gamma = 2.2;
      lookup_table_maxval = img.maxval;
      for (int i = 0; i <= img.maxval; i++)
	lookup_table [i] = pow (i / (double)img.maxval, 1 / 2.2);
    }
  lookup_table_uses ++;
}

render::~render ()
{
  lookup_table_uses --;
}
