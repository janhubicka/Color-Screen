#include "include/scr-detect.h"
#include "include/solver.h"
#include "include/render-scr-detect.h"
#include "screen-map.h"
namespace
{

const bool verbose = false;

struct patch_entry
{
	int x, y;
};

bool
set_bit (uint8_t *visited, int p)
{
  int pos = p / 8;
  int bit = p & 7;
  bool ret = visited[pos] & (1U << bit);
  visited[pos] |= (1U << bit);
  return ret;
}
bool
clear_bit (uint8_t *visited, int p)
{
  int pos = p / 8;
  int bit = p & 7;
  bool ret = visited[pos] & (1U << bit);
  visited[pos] &= ~(1U << bit);
  return ret;
}

/* Lookup patch of a given color, coordinates and maximal size.  Return number of vertices in patch.  */
int
find_patch (color_class_map &color_map, scr_detect::color_class c, int x, int y, int max_patch_size, patch_entry *entries, uint8_t *visited)
{
  if (x < 0 || y < 0 || x >= color_map.width || y >= color_map.height)
    return 0;
  if (visited && set_bit (visited, y * color_map.width + x))
    return 0;
  scr_detect::color_class t = color_map.get_class (x, y);
  if (t != c)
     return 0;
  int start = 0, end = 1;
  entries[0].x = x;
  entries[0].y = y;

  while (start < end)
    {
      int cx = entries[start].x;
      int cy = entries[start].y;
      for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, color_map.height); yy++)
	for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, color_map.width); xx++)
	  if ((xx != cx || yy != cy) && color_map.get_class (xx, yy) == t)
	    {
	      if (visited)
		{
		  if (set_bit (visited, yy * color_map.width + xx))
		    continue;
		}
	      else
		{
		  int q;
		  for (q = 0; q < end; q++)
		    if (entries[q].x == xx && entries[q].y == yy)
		      break;
		  if (q != end)
		    continue;
		}
	      entries[end].x = xx;
	      entries[end].y = yy;
	      end++;
	      if (end == max_patch_size)
		goto done;
	    }
      start++;
    }
done:
  return end;
}

void
patch_center (patch_entry *entries, int size, coord_t *x, coord_t *y)
{
  int xsum = 0;
  int ysum = 0;
  for (int i = 0; i < size; i++)
    {
      xsum += entries[i].x;
      ysum += entries[i].y;
    }
  *x = xsum / (coord_t)size;
  *y = ysum / (coord_t)size;
}

bool
try_guess_screen (color_class_map &color_map, solver_parameters &sparam, int x, int y, uint8_t *visited)
{
  const int max_size = 100;
  const int npatches = 5;
  struct patch_info
  {
    coord_t x, y;
  };
  struct patch_info rbpatches[npatches][npatches*2];
  patch_entry entries[max_size];
  int size = find_patch (color_map, scr_detect::green, x, y, max_size, entries, visited);
  if (size == 0 || size == max_size)
    return false;
  patch_center (entries, size, &rbpatches[0][0].x, &rbpatches[0][0].y);
  if (verbose)
    printf ("Trying to start search at %i %i with initial green patch of size %i and center %f %f\n", x, y, size, rbpatches[0][0].x, rbpatches[0][0].y);

  bool patch_found = false;

  for (int i = 0; i < size && !patch_found; i++)
  {
    int x = entries[i].x;
    for (y = entries[i].y + 1; y < color_map.height && !patch_found; y++)
      {
	scr_detect::color_class t = color_map.get_class (x, y);
	if (t == scr_detect::blue)
	  {
	    /* Do not mark as visited so we can revisit.  */
	    int size = find_patch (color_map, scr_detect::blue, x, y, max_size, entries, NULL);
	    patch_center (entries, size, &rbpatches[0][1].x, &rbpatches[0][1].y);
	    patch_found = true;
	  }
	else if (t != scr_detect::unknown)
	  break;
      }
  }

  if (!patch_found)
  {
    if (verbose)
      printf ("Blue patch not found\n");
    return false;
  }
  coord_t patch_stepx = rbpatches[0][1].x - rbpatches[0][0].x;
  coord_t patch_stepy = rbpatches[0][1].y - rbpatches[0][0].y;
  if (verbose)
    printf ("found blue patch of size %i and center %f %f guessing patch distance %f %f\n", size, rbpatches[0][0].x, rbpatches[0][0].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches * 2; p++)
    {
      int nx = rbpatches[0][p - 1].x + patch_stepx;
      int ny = rbpatches[0][p - 1].y + patch_stepy;
      size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, NULL);
      if (size == 0 || size == max_size)
	{
	  if (verbose)
	    printf ("Failed to guess patch 0, %i with steps %f %f\n", p, patch_stepx, patch_stepy);
	  return 0;
	}
      patch_center (entries, size, &rbpatches[0][p].x, &rbpatches[0][p].y);
      patch_stepx = (rbpatches[0][p].x - rbpatches[0][0].x) / p;
      patch_stepy = (rbpatches[0][p].y - rbpatches[0][0].y) / p;
    }
  if (verbose)
    printf ("Confirmed %i patches in alternating direction with distances %f %f\n", npatches, patch_stepx, patch_stepy);
  for (int r = 1; r < npatches; r++)
    {
      int nx = rbpatches[r - 1][0].x - 2*patch_stepy;
      int ny = rbpatches[r - 1][0].y + 2*patch_stepx;
      size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries, NULL);
      if (size == 0 || size == max_size)
	{
	  if (verbose)
	    printf ("Failed to guess patch %i,%i with steps %f %f\n", r, 0, patch_stepx, patch_stepy);
	  return 0;
	}
      patch_center (entries, size, &rbpatches[r][0].x, &rbpatches[r][0].y);
      for (int p = 1; p < npatches * 2; p++)
	{
	  int nx = rbpatches[r][p - 1].x + patch_stepx;
	  int ny = rbpatches[r][p - 1].y + patch_stepy;
	  size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, NULL);
	  if (size == 0 || size == max_size)
	    {
	      if (verbose)
		printf ("Failed to guess patch %i,%i with steps %f %f\n", r, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  patch_center (entries, size, &rbpatches[r][p].x, &rbpatches[r][p].y);
	}
    }
  sparam.remove_points ();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      sparam.add_point (rbpatches[r][p].x, rbpatches[r][p].y, p / 2.0, r, (p & 1) ? solver_parameters::blue : solver_parameters::green);
  return true;
}

bool
confirm_patch (color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, int max_patch_size, coord_t max_distance,
	       coord_t *cx, coord_t *cy, uint8_t *visited)
{
  patch_entry entries[max_patch_size + 1];
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), max_patch_size + 1, entries, visited);
  //if (verbose)
   //printf ("size: %i coord: %f %f color %i\n", size,x,y, (int)c);
  if (size < min_patch_size || size > max_patch_size)
    return false;
  patch_center (entries, size, cx, cy);
  if ((*cx - x) * (*cx - x) + (*cy - y) * (*cy - y) > max_distance * max_distance)
    return false;
  //printf ("center %f %f\n", *cx, *cy);
  return true;
}
bool
confirm_strip (color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, uint8_t *visited)
{
  patch_entry entries[min_patch_size + 1];
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), min_patch_size + 1, entries, visited);
  /* Since strips are not isolated do not mark them as visited so we do not block walk from other spot.  */
  for (int i = 0; i < size; i++)
    clear_bit (visited, entries[i].y * color_map->width + entries[i].x);
  //if (verbose)
    //printf ("size: %i coord: %f %f color %i\n", size,x,y, (int)c);
  if (size < min_patch_size)
    return false;
  return true;
}

screen_map *
flood_fill (coord_t greenx, coord_t greeny, scr_to_img_parameters &param, image_data &img, color_class_map *color_map, solver_parameters *sparam, uint8_t *visited, progress_info *progress)
{
  scr_to_img scr_map;
  scr_map.set_parameters (param, img);

  double screen_xsize = sqrt (param.coordinate1_x * param.coordinate1_x + param.coordinate1_y * param.coordinate1_y);
  double screen_ysize = sqrt (param.coordinate2_x * param.coordinate2_x + param.coordinate2_y * param.coordinate2_y);
  int max_patch_size = floor (screen_xsize * screen_ysize / 3);
  int min_patch_size = std::min ((int)(screen_xsize * screen_ysize / 5), 1);
  coord_t max_distance = (screen_xsize + screen_ysize) * 0.1;
  int nfound = 1;

  int xshift, yshift, width, height;
  scr_map.get_range (img.width, img.height, &xshift, &yshift, &width, &height);
  int nexpected = 2 * img.width * img.height / (screen_xsize * screen_ysize);
  if (verbose || 1)
    printf ("Flood fill started with coordinates %f,%f and %f,%f\n", param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y);
  if (progress)
    progress->set_task ("Flood fill", nexpected);
  screen_map *map = new screen_map(xshift * 2, yshift, width * 2, height);

  struct queue_entry
  {
    int scr_xm2, scr_y;
    coord_t img_x, img_y;
  };
  std::vector<queue_entry> queue;
  queue.push_back ((struct queue_entry){0, 0, greenx, greeny});
  map->set_coord (0, 0, greenx, greeny);
  if (sparam)
    sparam->remove_points ();
  //printf ("%i %i %f %f %f %f\n", queue.size (), map.in_range_p (0, 0), param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y);
  while (queue.size () /*&& nfound < 500*/)
    {
      coord_t ix, iy;
      queue_entry e = queue.back ();
      queue.pop_back ();
      //if (verbose)
        //printf ("visiting %i %i %f %f %f %f\n", e.scr_xm2, e.scr_y, e.img_x, e.img_y, param.coordinate1_x, param.coordinate1_y);
      if (progress)
        progress->inc_progress ();
      if (sparam)
	sparam->add_point (e.img_x, e.img_y, e.scr_xm2 / 2.0, e.scr_y, e.scr_y ? solver_parameters::blue : solver_parameters::green);


      if (map->in_range_p (e.scr_xm2 - 1, e.scr_y)
	  && !map->known_p (e.scr_xm2 - 1, e.scr_y)
	  && confirm_patch (color_map, e.img_x - param.coordinate1_x / 2, e.img_y - param.coordinate1_y / 2, ((e.scr_xm2 - 1) & 1) ? scr_detect::blue : scr_detect::green, min_patch_size, max_patch_size, max_distance, &ix, &iy, visited))
	{
	  map->set_coord (e.scr_xm2 - 1, e.scr_y, ix, iy);
	  queue.push_back ((struct queue_entry){e.scr_xm2 - 1, e.scr_y, ix, iy});
	  nfound++;
	}
      if (map->in_range_p (e.scr_xm2 + 1, e.scr_y)
	  && !map->known_p (e.scr_xm2 + 1, e.scr_y)
	  && confirm_patch (color_map, e.img_x + param.coordinate1_x / 2, e.img_y + param.coordinate1_y / 2, ((e.scr_xm2 + 1) & 1) ? scr_detect::blue : scr_detect::green, min_patch_size, max_patch_size, max_distance, &ix, &iy, visited))
	{
	  map->set_coord (e.scr_xm2 + 1, e.scr_y, ix, iy);
	  queue.push_back ((struct queue_entry){e.scr_xm2 + 1, e.scr_y, ix, iy});
	  nfound++;
	}
      if (map->in_range_p (e.scr_xm2, e.scr_y - 1)
	  && !map->known_p (e.scr_xm2, e.scr_y - 1)
	  && confirm_strip (color_map, e.img_x - param.coordinate2_x / 2, e.img_y - param.coordinate2_y / 2, scr_detect::red, min_patch_size, visited)
	  && confirm_patch (color_map, e.img_x - param.coordinate2_x, e.img_y - param.coordinate2_y, (e.scr_xm2 & 1) ? scr_detect::blue : scr_detect::green, min_patch_size, max_patch_size, max_distance, &ix, &iy, visited))
	{
	  map->set_coord (e.scr_xm2, e.scr_y - 1, ix, iy);
	  queue.push_back ((struct queue_entry){e.scr_xm2, e.scr_y - 1, ix, iy});
	  nfound++;
	}
      if (map->in_range_p (e.scr_xm2, e.scr_y + 1)
	  && !map->known_p (e.scr_xm2, e.scr_y + 1)
	  && confirm_strip (color_map, e.img_x + param.coordinate2_x / 2, e.img_y + param.coordinate2_y / 2, scr_detect::red, min_patch_size, visited)
	  && confirm_patch (color_map, e.img_x + param.coordinate2_x, e.img_y + param.coordinate2_y, (e.scr_xm2 & 1) ? scr_detect::blue : scr_detect::green, min_patch_size, max_patch_size, max_distance, &ix, &iy, visited))
	{
	  map->set_coord (e.scr_xm2, e.scr_y + 1, ix, iy);
	  queue.push_back ((struct queue_entry){e.scr_xm2, e.scr_y + 1, ix, iy});
	  nfound++;
	}
    }
  if (nfound < nexpected / 3)
    {
      delete map;
      return NULL;
    }
  printf ("Found %i points (%i expected, %f%%)\n", nfound, nexpected, nfound * 100.0 / nexpected);
  return map;
}
}

mesh *
detect_solver_points (image_data &img, scr_detect_parameters &dparam, solver_parameters &sparam, progress_info *progress)
{
  int max_diam = std::max (img.width, img.height);
  render_parameters empty;
  render_scr_detect render (dparam, img, empty, 256);
  render.precompute_all (progress);
  if (progress)
    progress->set_task ("Looking for initial grid", max_diam);
  uint8_t *visited = (uint8_t *)calloc ((img.width * img.height + 7) / 8, 1);
  scr_to_img_parameters param;
  screen_map *smap = NULL;
  param.type = Dufay;
  for (int d = 0; d < max_diam && !smap; d++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int i = -d; i < d && !smap; i++)
	  {
	    if (try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 + d, visited)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 - d, visited)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + d, img.height / 2 + i, visited)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 - d, img.height / 2 + i, visited))
	      {
		if (verbose)
		  {
		    printf ("Initial grid found at:\n");
		    sparam.dump (stdout);
		  }
		memset (visited, 0, (img.width * img.height + 7) / 8);
		simple_solver (&param, img, sparam, progress);
		smap = flood_fill (sparam.point[0].img_x, sparam.point[0].img_y, param, img, render.get_color_class_map (), /*&sparam*/ NULL, visited, progress);
		if (!smap)
		  memset (visited, 0, (img.width * img.height + 7) / 8);
		break;
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  free (visited);
  if (!smap)
    return NULL;
  smap->check_consistency (param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y,
			   sqrt (param.coordinate1_x * param.coordinate1_x + param.coordinate1_y * param.coordinate1_y) / 2);
  //smap->get_solver_points_nearby (0, 0, 200, sparam);
  //sparam.dump (stdout);
  //return NULL;
  mesh *m = solver_mesh (&param, img, sparam, *smap, progress);
  delete smap;

#if 0
  const int xsteps = 50, ysteps = 50;
  sparam.remove_points ();
  m->precompute_inverse ();
  for (int y = 0; y < img.height; y += img.height / ysteps)
    for (int x = 0; x < img.width; x += img.width / xsteps)
      {
	coord_t sx, sy;
	coord_t ix, iy;
	m->invert (x, y, &sx, &sy);
	sx = (int)sx;
	sy = (int)sy;
	m->apply (sx, sy, &ix, &iy);
        sparam.add_point (ix, iy, sx, sy, solver_parameters::green);
      }
#endif
  return m;
}
