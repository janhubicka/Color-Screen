#include "analyze-base-worker.h"
#include "analyze-dufay.h"
#include "screen.h"
namespace colorscreen
{
bool
analyze_dufay::analyze_contrast (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, progress_info *progress)
{
  m_contrast.reset (new contrast_info [m_area.width * m_area.height]);
  if (!m_contrast)
    return false;
  if (progress)
    progress->set_task ("collecting contrast info", img->height);
#pragma omp parallel for default (none) 
  for (int y = 0; y < m_area.height; y++)
    for (int x = 0; x < m_area.width; x++)
      {
	get_contrast (x,y).min = 10000;
	get_contrast (x,y).max = -10000;
      }
//#pragma omp parallel for default (none) shared (render,img,scr_to_img,progress)
  for (int y = 0 ; y < img->height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < img->width; x++)
	  {
            point_t scr = scr_to_img->to_scr ({x + (coord_t)0.5, y + (coord_t)0.5});
            int ix = my_floor (scr.x) + m_area.xshift ();
            int iy = my_floor (scr.y) + m_area.yshift ();
	    if (ix >= 0 && ix < m_area.width && iy >= 0 && iy < m_area.height)
	      {
		luminosity_t d = render->get_data_red ({x, y});
//#pragma omp critical
		{
		  get_contrast (ix,iy).min = std::min (get_contrast (ix,iy).min, d);
		  get_contrast (ix,iy).max = std::max (get_contrast (ix,iy).max, d);
		}
	      }
	  }
       if (progress)
	 progress->inc_progress ();
    }
  return !progress || !progress->cancelled ();
}

luminosity_t
analyze_dufay::compare_contrast (const analyze_dufay &other, int_point_t pos, int_point_t &pt1, int_point_t &pt2, const scr_to_img &map, const scr_to_img &other_map, progress_info *progress)
{
  const int tile_size = 10;
  luminosity_t max_ratio = 0;
  int maxx = 0, maxy = 0;
  if (progress)
    progress->set_task ("comparing contrast", m_area.height);
  for (int y = 0; y < m_area.height - tile_size; y++)
    {
      int y2 = y - m_area.yshift () - pos.y + other.m_area.yshift ();
      if (y2 >= 0 && y2 < other.m_area.height - tile_size)
        for (int x = 0; x < m_area.width - tile_size; x++)
	  {
	    int x2 = x - m_area.xshift () - pos.x + other.m_area.xshift ();
	    if (x2 >= 0 && x2 < other.m_area.width - tile_size)
	      {
		bool skip = false;
		luminosity_t ratsum1 = 0, ratsum2 = 0;
		const luminosity_t threshold = 0.1;
		const luminosity_t minthreshold = 0.01;
		int n = 0;
		for (int yy = 0; yy < tile_size && !skip; yy++)
		  for (int xx = 0; xx < tile_size && !skip; xx++)
		    {
		      if (!m_known_pixels->test_bit (x+xx, y+yy)
			  || !other.m_known_pixels->test_bit (x2+xx, y2+yy))
			skip = true;
		      else
			{
			  if (get_contrast (x + xx, y + yy).max > threshold && other.get_contrast (x2 + xx, y2 + yy).max > threshold
			      && get_contrast (x + xx, y + yy).min > minthreshold && other.get_contrast (x2 + xx, y2 + yy).min > minthreshold)
			    {
				n++;
				luminosity_t w = 1;
				luminosity_t ratio1 = (get_contrast (x + xx, y + yy).max / get_contrast (x + xx, y + yy).min);
				luminosity_t ratio2 = (other.get_contrast (x2 + xx, y2 + yy).max / other.get_contrast (x2 + xx, y2 + yy).min);
				ratsum1 += ratio1 * w;
				ratsum2 += ratio2 * w;
			    }
			}
		    }
		if (!skip && n > tile_size * tile_size / 10)
		  {
		    luminosity_t ratio = ratsum1 / ratsum2;
		    if (ratio < 1)
		      ratio = 1 / ratio;
		    if (ratio > max_ratio)
		      {
			max_ratio = ratio;
			maxx = x;
			maxy = y;
		      }
		  }
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  if (!max_ratio)
    return -1;
  point_t imgp = map.to_img ({maxx + tile_size / (coord_t)2 - m_area.xshift (), maxy + tile_size / (coord_t)2 - m_area.yshift ()});
  pt1.x = imgp.x;
  pt1.y = imgp.y;
  imgp = other_map.to_img ({maxx + tile_size / (coord_t)2 - m_area.xshift () - pos.x, maxy + tile_size / (coord_t)2 - m_area.yshift () - pos.y});
  pt2.x = imgp.x;
  pt2.y = imgp.y;
  return max_ratio;
}

bool
analyze_dufay::dump_patch_density (FILE *out)
{
  fprintf (out, "Dufay dimension: %i %i\n", m_area.width, m_area.height);
  fprintf (out, "LeftDot %i %i\n", m_area.width , m_area.height);
  for (int y = 0; y < m_area.height; y++)
    {
      for (int x = 0; x < m_area.width; x++)
	fprintf (out, "  %f", green (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "RightDot %i %i\n", m_area.width , m_area.height);
  for (int y = 0; y < m_area.height; y++)
    {
      for (int x = 0; x < m_area.width; x++)
	fprintf (out, "  %f", blue (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Strip %i %i\n", m_area.width * 2, m_area.height);
  for (int y = 0; y < m_area.height; y++)
    {
      for (int x = 0; x < m_area.width * 2; x++)
	fprintf (out, "  %f", red (x, y));
      fprintf (out, "\n");
    }
  return true;
}
}
