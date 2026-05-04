/* Screen patches detection and manipulation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "patches.h"
#include "render-scr-detect.h"

namespace colorscreen
{

/* Initialize patches for given image IMG using RENDER and COLOR_MAP.
   MAX_PATCH_SIZE is the maximum size of a patch to be considered.
   PROGRESS is used to report progress and check for cancellation.  */
patches::patches (const image_data &img, render &render, color_class_map &color_map,
		  int max_patch_size, progress_info *progress)
  : m_width (img.width), m_height (img.height),
    m_map (std::make_unique<patch_index_t[]> ((uint64_t)img.width * img.height))
{
  int num_pixels = 0;
  uint64_t num_overall_pixels;
  std::vector<int_point_t> queue (max_patch_size);

  for (int i = 0; i < img.width * img.height; i++)
    m_map[i] = 0;

  if (progress)
    progress->set_task ("analyzing patches", m_height);
  for (int y = 0; y < m_height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < m_width; x++)
	  {
	    scr_detect::color_class t = color_map.get_class (x, y);
	    if (t == scr_detect::unknown || get_patch_index ({x, y}))
	      continue;
	    struct patch p = {(unsigned short)x, (unsigned short)y, 1, 0,
			      static_cast<unsigned short>(t), 0};
	    int start = 0, end = 1;
	    int id = m_vec.size () + 1;
	    queue[0] = {x, y};
	    set_patch_index ({x, y}, id, (int)t);
	    while (start < end)
	      {
		int cx = queue[start].x;
		int cy = queue[start].y;
		for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, m_height); yy++)
		  for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, m_width); xx++)
		    if ((xx != cx || yy != cy) && !get_patch_index ({xx, yy})
			&& color_map.get_class (xx, yy) == t)
		      {
			queue[end] = {xx, yy};
			set_patch_index ({xx, yy}, id, (int)t);
			end++;
			p.pixels++;
			p.luminosity_sum += render.fast_get_img_pixel ({xx, yy});
			num_pixels++;
			if (end == max_patch_size)
			  goto done;
		      }
		start++;
	      }
done:
	    if (end > 4)
	      {
		m_vec.push_back (p);
	      }
	    /* Take back too small patches.  */
	    else
	      {
		for (int i = 0; i < end; i++)
		  set_patch_index (queue[i], 0, 0);
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  if (debug)
    printf ("Detected %i patches %f known pixels per patch\n", num_patches (),
	    num_pixels / (double)num_patches ());
  num_overall_pixels = num_pixels;
  if (progress)
    progress->set_task ("producing voronoi diagram", m_height);
#pragma omp parallel for default(none) shared(progress) reduction(+:num_overall_pixels)
  for (int y = 0; y < m_height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < m_width; x++)
	  {
	    int rx[3], ry[3];
	    patch_index_t rp[3];
	    if (!fast_nearest_patches ({x, y}, rx, ry, rp))
	      continue;
#pragma omp critical
	    for (int i = 0; i < 3; i++)
	      {
		patch &p = get_patch (rp[i]);
		p.overall_pixels++;
		num_overall_pixels++;
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  if (debug)
    printf ("%f overall pixels per patch\n", num_overall_pixels / (double)num_patches ());
}

/* Find coordinates of nearest patch in each color for position P.
   Set RP to patch indices and RX, RY to coordinates.
   Return true if patches were found.  */
bool
patches::nearest_patches (point_t p, int *rx, int *ry, patch_index_t *index) const
{
  /* Search for nearest pixels of each known color.  */
  const coord_t inf = distance_list::max_distance + 1;
  coord_t cdist[3] = {inf, inf, inf};
  coord_t biggest = inf;

  rx[0] = rx[1] = rx[2] = -1;
  ry[0] = ry[1] = ry[2] = -1;
  index[0] = index[1] = index[2] = 0;
  for (int i = 0; i < distance_list.num && distance_list.list[i].fdist < biggest + 2; i++)
    {
      int xx = (int)p.x + distance_list.list[i].x;
      int yy = (int)p.y + distance_list.list[i].y;

      if (xx < 0 || yy < 0 || xx >= m_width || yy >= m_height)
	continue;
      patch_index_t id = get_patch_index ({xx, yy});
      if (!id)
	continue;
      int t = get_patch_color ({xx, yy});
      if (id == index[(int)t] || distance_list.list[i].fdist > cdist[(int)t] + 2)
	continue;
      double dist = my_sqrt ((xx + (coord_t)0.5 - p.x) * (xx + (coord_t)0.5 - p.x)
			     + (yy + (coord_t)0.5 - p.y) * (yy + (coord_t)0.5 - p.y));
      if (dist < cdist[(int)t])
	{
	  cdist[(int)t] = dist;
	  rx[(int)t] = xx;
	  ry[(int)t] = yy;
	  index[(int)t] = id;
	  biggest = std::max (std::max (cdist[0], cdist[1]), cdist[2]);
	}
    }
  return biggest != inf;
}

/* Fast version of nearest_patches for integer position P.  */
bool
patches::fast_nearest_patches (int_point_t p, int *rx, int *ry, patch_index_t *index) const
{
  /* Search for nearest pixels of each known color.  */
  const int inf = distance_list::max_distance + 1;
  int cdist[3] = {inf, inf, inf};
  int biggest = inf;

  rx[0] = rx[1] = rx[2] = -1;
  ry[0] = ry[1] = ry[2] = -1;
  index[0] = index[1] = index[2] = 0;
  for (int i = 0; i < distance_list.num && distance_list.list[i].dist < biggest; i++)
    {
      int xx = p.x + distance_list.list[i].x;
      int yy = p.y + distance_list.list[i].y;

      if (xx < 0 || yy < 0 || xx >= m_width || yy >= m_height)
	continue;
      patch_index_t id = get_patch_index ({xx, yy});
      if (!id)
	continue;
      int t = get_patch_color ({xx, yy});
      if (id == index[(int)t] || cdist[(int)t] != inf)
	continue;
      cdist[(int)t] = distance_list.list[i].dist;
      rx[(int)t] = xx;
      ry[(int)t] = yy;
      index[(int)t] = id;
      biggest = std::max (std::max (cdist[0], cdist[1]), cdist[2]);
    }
  return biggest != inf;
}

/* Free allocated memory.  */
patches::~patches ()
{
}

}
