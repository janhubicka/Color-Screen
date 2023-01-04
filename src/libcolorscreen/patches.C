#include <include/patches.h>
#include <include/render-scr-detect.h>

patches::patches (image_data &img, render &render, color_class_map &color_map, int max_patch_size, progress_info *progress)
 : m_width (img.width), m_height (img.height)
{
  int num_pixels = 0, num_overall_pixels;
  struct {
    int x,y;
  } queue[max_patch_size];
  m_map = (patch_index_t *)calloc (img.width * img.height, sizeof (patch_index_t));
  if (progress)
    progress->set_task ("analyzing patches", m_height);
  for (int y = 0; y < m_height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < m_width; x++)
	  {
	    scr_detect::color_class t = color_map.get_class (x, y);
	    if (t == scr_detect::unknown || get_patch_index (x, y))
	      continue;
	    struct patch p = {(unsigned short)x, (unsigned short)y, 1, 0, static_cast<unsigned short>(t), 0};
	    int start = 0, end = 1;
	    int id = m_vec.size () + 1;
	    queue[0].x = x;
	    queue[0].y = y;
	    set_patch_index (x, y, id, (int)t);
	    while (start < end)
	      {
		int cx = queue[start].x;
		int cy = queue[start].y;
		for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, m_height); yy++)
		  for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, m_width); xx++)
		    if ((xx != cx || yy != cy) && !get_patch_index (xx,yy) && color_map.get_class (xx, yy) == t)
		      {
			queue[end].x = xx;
			queue[end].y = yy;
			set_patch_index (xx, yy, id, (int)t);
			end++;
			p.pixels++;
			p.luminosity_sum += render.fast_get_img_pixel (xx,yy);
			num_pixels++;
			if (end == max_patch_size)
			  goto done;
		      }
		start++;
	      }
done:
	    if (end > 4)
	      {
		//p.overall_pixels = p.pixels;
		m_vec.push_back (p);
	      }
	    /* Take back too small patches.  */
	    else
	      {
		for (int i = 0; i < end; i++)
		  set_patch_index (queue[i].x, queue[i].y, 0, 0);
	      }
	  }
      if (progress)
        progress->inc_progress ();
    }
  if (debug)
    printf ("Detected %i patches %f known pixels per patch\n", num_patches (), num_pixels / (double)num_patches ());
  num_overall_pixels = num_pixels;
  if (progress)
    progress->set_task ("producing vornoi diagram", m_height);
#pragma omp parallel for default(none) shared(progress,num_overall_pixels)
  for (int y = 0; y < m_height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = 0; x < m_width; x++)
	  {
	    int rx[3], ry[3];
	    patch_index_t rp[3];
	    if (!fast_nearest_patches (x, y, rx, ry, rp))
	      continue;
#pragma omp critical
	    for (int i = 0; i < 3; i++)
	      /*if (rx[i] != -1 && (rx[i] != x || ry[i] != y) && get_patch_index (x,y) != rp[i])*/
		{
		  {
		    patch &p = get_patch (rp[i]);
		    p.overall_pixels++;
		    num_overall_pixels++;
		  }
		}
	  }
	if (progress)
	progress->inc_progress ();
    }
  if (debug)
    printf ("%f overall pixels per patch\n", num_overall_pixels / (double)num_patches ());
}
bool
patches::nearest_patches (coord_t x, coord_t y, int *rx, int *ry, patch_index_t *index)
{
   /* Search for nearest pixels of each known color.  */
   const coord_t inf = distance_list::max_distance + 1;
   coord_t cdist[3] = {inf, inf, inf};
   coord_t biggest = inf;

       //printf ("Searching %f %f\n",x,y);
   rx[0]=rx[1]=rx[2] = -1;
   ry[0]=ry[1]=ry[2] = -1;
   index[0]=index[1]=index[2] = 0;
   for (int i = 0; i < distance_list.num && distance_list.list[i].fdist < biggest + 2; i++)
     {
       int xx = (int)x + distance_list.list[i].x;
       int yy = (int)y + distance_list.list[i].y;

       //printf ("%f %f %i %i\n",x,y,xx,yy);
       if (xx < 0 || yy < 0 || xx >= m_width || yy >= m_height)
	 continue;
       patch_index_t id = get_patch_index (xx, yy);
       if (!id)
	 continue;
       int t = get_patch_color (xx, yy);
       if (id == index[(int)t] || distance_list.list[i].fdist > cdist[(int)t] + 2)
	 continue;
       double dist = scr_to_img::my_sqrt ((xx + (coord_t)0.5 - x) * (xx + (coord_t)0.5 - x) + (yy + (coord_t)0.5 - y) * (yy + (coord_t)0.5 - y));
       if (dist < cdist[(int)t])
	 {
	   cdist[(int)t] = dist;
	   rx[(int)t] = xx;
	   ry[(int)t] = yy;
	   index[(int)t] = id;
	   biggest = std::max (std::max (cdist[0], cdist[1]), cdist[2]);
       //printf ("Found %i %f %f %f\n",t, cdist[0], cdist[1], cdist[2]);
	 }
     }
   //if (biggest != inf)
     //printf ("Found %f %f r: %i %i %f g: %i %i %f b: %i %i %f\n", x,y,rx[0],ry[0],cdist[0],rx[1],ry[1],cdist[1],rx[2],ry[2],cdist[2]);
   return biggest != inf;
}
bool
patches::fast_nearest_patches (int x, int y, patch_index_t *rx, patch_index_t *ry, patch_index_t *index)
{
   /* Search for nearest pixels of each known color.  */
   const int inf = distance_list::max_distance + 1;
   int cdist[3] = {inf, inf, inf};
   int biggest = inf;

   rx[0]=rx[1]=rx[2] = -1;
   ry[0]=ry[1]=ry[2] = -1;
   index[0]=index[1]=index[2] = 0;
   for (int i = 0; i < distance_list.num && distance_list.list[i].dist < biggest; i++)
     {
       int xx = (int)x + distance_list.list[i].x;
       int yy = (int)y + distance_list.list[i].y;

       if (xx < 0 || yy < 0 || xx >= m_width || yy >= m_height)
	 continue;
       patch_index_t id = get_patch_index (xx, yy);
       if (!id)
	 continue;
       int t = get_patch_color (xx, yy);
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
patches::~patches ()
{
  free (m_map);
}
