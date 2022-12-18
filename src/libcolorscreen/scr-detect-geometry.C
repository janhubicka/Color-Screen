#include "include/scr-detect.h"
#include "include/solver.h"
#include "include/render-scr-detect.h"
namespace
{

const bool verbose = false;

struct patch_entry
{
	int x, y;
};

/* Lookup patch of a given color, coordinates and maximal size.  Return number of vertices in patch.  */
int
find_patch (color_class_map &color_map, scr_detect::color_class c, int x, int y, int max_patch_size, patch_entry *entries)
{
  if (x < 0 || y < 0 || x >= color_map.width || y >= color_map.height)
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
	      int q;
	      for (q = 0; q < end; q++)
		if (entries[q].x == xx && entries[q].y == yy)
		  break;
	      if (q != end)
		continue;
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
try_guess_screen (color_class_map &color_map, solver_parameters &sparam, int x, int y)
{
  const int max_size = 100;
  const int npatches = 5;
  struct patch_info
  {
    coord_t x, y;
  };
  struct patch_info rbpatches[npatches][npatches*2];
  patch_entry entries[max_size];
  int size = find_patch (color_map, scr_detect::green, x, y, max_size, entries);
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
	    int size = find_patch (color_map, scr_detect::blue, x, y, max_size, entries);
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
      size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries);
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
      size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries);
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
	  size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries);
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
}

void
detect_solver_points (image_data &img, scr_detect_parameters &dparam, solver_parameters &sparam, progress_info *progress)
{
  int max_diam = std::max (img.width, img.height);
  bool found = false;
  render_parameters empty;
  render_scr_detect render (dparam, img, empty, 256);
  render.precompute_all (progress);
  progress->set_task ("Looking for initial grid", max_diam);
  for (int d = 0; d < max_diam && !found; d++)
    {
      if (!progress || !progress->cancel_requested ())
	for (int i = -d; i < d && !found; i++)
	  {
	    if (try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 + d)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 - d)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 + d, img.height / 2 + i)
		|| try_guess_screen (*render.get_color_class_map (), sparam, img.width / 2 - d, img.height / 2 + i))
	      {
		found = true;
		break;
	      }
	  }
      if (progress)
	progress->inc_progress ();
    }
  if (!found)
    return;
  scr_to_img_parameters param;
  param.type = Dufay;
  simple_solver (&param, img, sparam, progress);
}
