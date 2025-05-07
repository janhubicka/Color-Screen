#include "analyze-base-worker.h"
#include "analyze-dufay.h"
#include "screen.h"
namespace colorscreen
{
bool
analyze_dufay::analyze_contrast (render_to_scr *render, const image_data *img, scr_to_img *scr_to_img, progress_info *progress)
{
  m_contrast = (contrast_info *)malloc (m_width * m_height * sizeof (contrast_info));
  if (!m_contrast)
    return 0;
  if (progress)
    progress->set_task ("collecting contrast info", img->height);
#pragma omp parallel for default (none) 
  for (int y = 0; y < m_height; y++)
    for (int x = 0; x < m_width; x++)
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
	    scr += {(coord_t)m_xshift, (coord_t)m_yshift};
	    int ix = floor (scr.x);
	    int iy = floor (scr.y);
	    ix += m_xshift;
	    iy += m_yshift;
	    if (ix >= 0 && ix < m_width && iy >= 0 && iy < m_height)
	      {
		luminosity_t d = render->get_data_red (x, y);
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
analyze_dufay::compare_contrast (analyze_dufay &other, int xpos, int ypos, int *x1, int *y1, int *x2, int *y2, scr_to_img &map, scr_to_img &other_map, progress_info *progress)
{
  const int tile_size = 10;
  luminosity_t max_ratio = 0;
  int maxx = 0, maxy = 0;
  if (progress)
    progress->set_task ("comparing contrast", m_height);
  for (int y = 0; y < m_height - tile_size; y++)
    {
      int y2 = y - m_yshift - ypos + other.m_yshift;
      if (y2 >= 0 && y2 < other.m_height - tile_size)
        for (int x = 0; x < m_width - tile_size; x++)
	  {
	    int x2 = x - m_xshift - xpos + other.m_xshift;
	    if (x2 >= 0 && x2 < other.m_width - tile_size)
	      {
		bool skip = false;
#if 0
		luminosity_t wsum = 0;
#endif
		luminosity_t ratsum1 = 0, ratsum2 = 0;
		//luminosity_t diffsum1 = 0, diffsum2 = 0, rsum1 = 0, rsum2 = 0;
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
			  //diffsum1 += get_contrast (x + xx, y + yy).max - get_contrast (x + xx, y + yy).min;
			  //diffsum2 += other.get_contrast (x2 + xx, y2 + yy).max - other.get_contrast (x2 + xx, y2 + yy).min;
			  //rsum1 += red ((x + xx)*2, y + yy);
			  //rsum2 += other.red ((x2 + xx)*2, y2 + yy);
			  if (get_contrast (x + xx, y + yy).max > threshold && other.get_contrast (x2 + xx, y2 + yy).max > threshold
			      && get_contrast (x + xx, y + yy).min > minthreshold && other.get_contrast (x2 + xx, y2 + yy).min > minthreshold)
			    {
				n++;
				luminosity_t w =  /*= get_contrast (x + xx, y + yy).max*/ 1;
				//luminosity_t ratio = (get_contrast (x + xx, y + yy).max - get_contrast (x + xx, y + yy).min) / (other.get_contrast (x2 + xx, y2 + yy).max - other.get_contrast (x2 + xx, y2 + yy).min);
				luminosity_t ratio1 = (get_contrast (x + xx, y + yy).max / get_contrast (x + xx, y + yy).min);
				luminosity_t ratio2 = (other.get_contrast (x2 + xx, y2 + yy).max / other.get_contrast (x2 + xx, y2 + yy).min);
#if 0
				wsum += w;
#endif
				ratsum1 += ratio1 * w;
				ratsum2 += ratio2 * w;
			    }
			}
		    }
		if (!skip && n > tile_size * tile_size / 10)
		  {
		    //luminosity_t ratio = rsum / wsum;
		    luminosity_t ratio = ratsum1 / ratsum2;
		    //luminosity_t ratio = (diffsum1/rsum1) / (diffsum2/rsum2);
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
  point_t imgp = map.to_img ({maxx + tile_size / (coord_t)2 - m_xshift, maxy + tile_size / (coord_t)2 - m_yshift});
  *x1 = imgp.x;
  *y1 = imgp.y;
  imgp = other_map.to_img ({maxx + tile_size / (coord_t)2 - m_xshift -xpos, maxy + tile_size / (coord_t)2 - m_yshift - ypos});
  *x2 = imgp.x;
  *y2 = imgp.y;
  return max_ratio;
}

bool
analyze_dufay::dump_patch_density (FILE *out)
{
  fprintf (out, "Paget dimenstion: %i %i\n", m_width, m_height);
  fprintf (out, "LeftDot %i %i\n", m_width , m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", green (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "RightDot %i %i\n", m_width , m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", blue (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Strip %i %i\n", m_width * 2, m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width * 2; x++)
	fprintf (out, "  %f", red (x, y));
      fprintf (out, "\n");
    }
  return true;
}
}
