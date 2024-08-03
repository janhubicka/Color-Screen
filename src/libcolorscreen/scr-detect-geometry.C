/* This file implement logic to detect regular screen in RGB scan.  */
#include <memory>
#include <limits.h>
#include "include/colorscreen.h"
#include "include/screen-map.h"
#include "solver.h"
#include "render-to-scr.h"
#include "bitmap.h"
#include "analyze-paget.h"
#include "render-scr-detect.h"
namespace colorscreen
{
extern void prune_render_scr_detect_caches ();
namespace
{
const bool verbose = false;
const int verbose_confirm = 0;
struct patch_entry
{
  int x, y;
};


/* Lookup patch of a given color C, coordinates X, Y and maximal size MAX_PATCH_SIZE.
   Return number of pixels in patch.
   ENTRIES will be initialized to list all pixels in the patch.  VISITED bitmap is used
   to mark pixels that belings to already known patches.  If PERMANENT then bitmap is updated
   to mark all pixels of the patch as visited.  */
int
find_patch (color_class_map &color_map, scr_detect::color_class c, int x, int y, int max_patch_size, patch_entry *entries, bitmap_2d *visited, bool permanent)
{
  if (x < 0 || y < 0 || x >= color_map.width || y >= color_map.height)
    return 0;
  scr_detect::color_class t = color_map.get_class (x, y);
  if (t != c)
     return 0;
  if (visited->set_bit (x, y))
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
	      if (visited->set_bit (xx, yy))
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
  if (!permanent)
    for (int i = 0; i < end; i++)
      visited->clear_bit (entries[i].x, entries[i].y);
  return end;
}

/* Find center of patch specified by ENTRIES of SIZE.
   This is a weighted everage of all entries.  Return true if
   this center is inside of the patch.  */

bool
patch_center (patch_entry *entries, int size, coord_t *x, coord_t *y)
{
  int xsum = 0;
  int ysum = 0;
  for (int i = 0; i < size; i++)
    {
      xsum += entries[i].x;
      ysum += entries[i].y;
    }
  *x = (2 * xsum + size) / (coord_t)(2 * size);
  *y = (2 * ysum + size) / (coord_t)(2 * size);
  /* Confirm that the center is inside of the patch.  */
  for (int i = 0; i < size; i++)
    if ((int)(*x+0.5) == entries[i].x && (int)(*y+0.5) == entries[i].y)
      return true;
  return false;
}

/* Return true if there is a strip on coordinates X, Y of color C.
   This is done by checking that there is patch of maximal size.  */

bool
confirm_strip (color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, int *priority, bitmap_2d *visited)
{
  patch_entry entries[min_patch_size + 1];
  /* Since strips are not isolated do not mark them as visited so we do not block walk from other spot.  */
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), min_patch_size + 1, entries, visited, false);
  if (size < min_patch_size)
    return false;
  *priority = 7;
  return true;
}

const int npatches = 5;
struct patch_info
{
  coord_t x, y;
};

/* Try to guess Dufaycolor screen on coordinates X and Y.  Return true if sucessful
   This function only detect grid of NPATCHESxNPATCHES*2 color patches which is strong enough evidence that
   the scan seems to contain Dufay color screen but this is not enough patches to
   accurately determine geometry.  Once approximate geometry is detected in next step
   flood_fill will find remaining patches.  
   
   Screen organization is

   GBGBGB
   RRRRRR
   GBGBGB
   RRRRRR
   
   */

bool
try_guess_screen (FILE *report_file, color_class_map &color_map, solver_parameters &sparam, int x, int y, bitmap_2d *visited, progress_info *progress)
{
  const int max_size = 200;
  struct patch_info rbpatches[npatches][npatches*2];
  patch_entry entries[max_size];

  /* First try to find a green patch.  */
  int size = find_patch (color_map, scr_detect::green, x, y, max_size, entries, visited, true);
  if (size == 0 || size == max_size)
    return false;
  if (!patch_center (entries, size, &rbpatches[0][0].x, &rbpatches[0][0].y))
    return false;
  if (report_file && verbose)
    fprintf (report_file, "Dufay: Trying to start search at %i %i with initial green patch of size %i and center %f %f\n", x, y, size, rbpatches[0][0].x, rbpatches[0][0].y);

  bool patch_found = false;

  /* Now find adjacent blue patch.  */
  for (int i = 0; i < size && !patch_found; i++)
    {
      int x = entries[i].x;
      for (y = entries[i].y + 1; y < color_map.height && !patch_found; y++)
	{
	  scr_detect::color_class t = color_map.get_class (x, y);
	  if (t == scr_detect::blue)
	    {
	      /* Do not mark as visited so we can revisit.  */
	      int size = find_patch (color_map, scr_detect::blue, x, y, max_size, entries, visited, false);
	      patch_found = patch_center (entries, size, &rbpatches[0][1].x, &rbpatches[0][1].y);
	    }
	  else if (t != scr_detect::unknown)
	    break;
	}
    }

  if (!patch_found)
    {
      if (report_file && verbose)
	fprintf (report_file, "Dufay: Blue patch not found\n");
      return false;
    }

  /* Now find row of alternating green and blue patches;  keep updating screen geometry
     (patch_stepx, patch_stepy).  */
  coord_t patch_stepx = rbpatches[0][1].x - rbpatches[0][0].x;
  coord_t patch_stepy = rbpatches[0][1].y - rbpatches[0][0].y;
  if (report_file && verbose)
    fprintf (report_file, "Dufay: found blue patch of size %i and center %f %f guessing patch distance %f %f\n", size, rbpatches[0][0].x, rbpatches[0][0].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches * 2; p++)
    {
      int nx = rbpatches[0][p - 1].x + patch_stepx;
      int ny = rbpatches[0][p - 1].y + patch_stepy;
      size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, visited, false);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Dufay: Failed to guess patch 0, %i with steps %f %f\n", p, patch_stepx, patch_stepy);
	  return false;
	}
      if (!patch_center (entries, size, &rbpatches[0][p].x, &rbpatches[0][p].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Dufay: Center of patch 0, %i is not inside\n", p);
	  return false;
	}
      patch_stepx = (rbpatches[0][p].x - rbpatches[0][0].x) / p;
      patch_stepy = (rbpatches[0][p].y - rbpatches[0][0].y) / p;
    }
  if (report_file && verbose)
   fprintf (report_file, "Dufay: Confirmed %i patches in alternating direction with distances %f %f\n", npatches, patch_stepx, patch_stepy);

  /* Now once row is found, extend each entry to an orthogonal row.  */
  for (int r = 1; r < npatches; r++)
    {
      int rx = rbpatches[r - 1][0].x - patch_stepy;
      int ry = rbpatches[r - 1][0].y + patch_stepx;
      int nx = rbpatches[r - 1][0].x - 2*patch_stepy;
      int ny = rbpatches[r - 1][0].y + 2*patch_stepx;
      int priority;
      if (!confirm_strip (&color_map, rx, ry, scr_detect::red, 1, &priority, visited))
	 {
	  if (report_file && verbose)
	    fprintf (report_file, "Dufay: Failed to confirm red strip on way to %i,%i with steps %f %f\n", r, 0, patch_stepx, patch_stepy);
	  return false;
	 }
      size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries, visited, false);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Dufay: Failed to guess patch %i,%i with steps %f %f\n", r, 0, patch_stepx, patch_stepy);
	  return false;
	}
      if (!patch_center (entries, size, &rbpatches[r][0].x, &rbpatches[r][0].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Dufay: Center of patch %i,%i is not inside\n", r, 0);
	  return false;
	}
      for (int p = 1; p < npatches * 2; p++)
	{
	  int nx = rbpatches[r][p - 1].x + patch_stepx;
	  int ny = rbpatches[r][p - 1].y + patch_stepy;
	  size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, visited, false);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Dufay: Failed to guess patch %i,%i with steps %f %f\n", r, p, patch_stepx, patch_stepy);
	      return false;
	    }
	  if (!patch_center (entries, size, &rbpatches[r][p].x, &rbpatches[r][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Dufay: Center of patch %i,%i is not inside\n", r, p);
	      return false;
	    }
	}
    }

  /* Be sure that every point is unique.  */
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      for (int r1 = 0; r1 < r; r1++)
        for (int p1 = 0; p1 < p; p1++)
	  if (rbpatches[r][p].x == rbpatches[r1][p1].x && rbpatches[r][p].y == rbpatches[r1][p1].y)
	    return false;

  /* Add points to solver; this is mostly useful for debugging.  */
  sparam.remove_points ();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      sparam.add_point ({rbpatches[r][p].x, rbpatches[r][p].y}, {p / (coord_t)2.0, (coord_t)r}, (p & 1) ? solver_parameters::blue : solver_parameters::green);
  return true;
}

/* Same as try_guess_screen but for Paget/Finlay screens.  Screen is organized as follows
  
   G   R   G
 B   B   B
   R   G   R
 B   B   B

 Detection works in diagonal coordinates, so the grid is rorated 45 degrees.

   G B G B
   B R B R
   G B G B
   */
bool
try_guess_paget_screen (FILE *report_file, color_class_map &color_map, solver_parameters &sparam, int x, int y, bitmap_2d *visited, progress_info *progress)
{
  const int max_size = 1000;
  struct patch_info rpatches[npatches][npatches];
  struct patch_info gpatches[npatches][npatches];
  struct patch_info bpatches[npatches*2][npatches];
  patch_entry entries[max_size];

  /* Find initial green patch.  */
  int size = find_patch (color_map, scr_detect::green, x, y, max_size, entries, visited, true);
  if (size == 0 || size == max_size)
    return false;
  if (!patch_center (entries, size, &gpatches[0][0].x, &gpatches[0][0].y))
    return false;
  if (report_file && verbose)
    fprintf (report_file, "Paget: Trying to start search at %i %i with initial green patch of size %i and center %f %f\n", x, y, size, gpatches[0][0].x, gpatches[0][0].y);

  bool patch_found = false;
#if 0
  bool verbose = 1;
  report_file = stdout;
#endif

  /* Find adjacent blue patch below the green one.  
     G
       B
    
     */
  for (int i = 0; i < size && !patch_found; i++)
    {
      int x = entries[i].x;
      for (y = entries[i].y + 1; y < color_map.height && !patch_found; y++)
	{
	  scr_detect::color_class t = color_map.get_class (x, y);
	  if (t == scr_detect::blue)
	    {
	      /* Do not mark as visited so we can revisit.  */
	      int size = find_patch (color_map, scr_detect::blue, x, y, max_size, entries, visited, false);
	      patch_found = patch_center (entries, size, &bpatches[0][0].x, &bpatches[0][0].y);
	    }
	  else if (t != scr_detect::unknown)
	    break;
	}
    }
  if (!patch_found)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Blue patch not found\n");
      return false;
    }

  /* Below green patch should be two blue patches.
    
        G
      B   B
    
     Guess coordinates of second one and try to find it.  */
  coord_t b1patch_stepx = bpatches[0][0].x - gpatches[0][0].x;
  coord_t b1patch_stepy = bpatches[0][0].y - gpatches[0][0].y;
  coord_t b2patch_stepx = b1patch_stepy;
  coord_t b2patch_stepy = -b1patch_stepx;
  coord_t bx, by;
  size = find_patch (color_map, scr_detect::blue, gpatches[0][0].x + b2patch_stepx, gpatches[0][0].y + b2patch_stepy, max_size, entries, visited, false);
  if (!size || size == max_size)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Second blue patch not found with step %f %f\n", b2patch_stepx, b2patch_stepy);
    }
  patch_found = patch_center (entries, size, &bx, &by);
  if (!patch_found || (bx == bpatches[0][0].x && by == bpatches[0][0].y))
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Center of second Blue patch not found\n");
      return false;
    }

  /* Order the blue patches to the step is positive.  */
  if (b1patch_stepx < 0)
    {
      std::swap (b1patch_stepx, b2patch_stepx);
      std::swap (b1patch_stepy, b2patch_stepy);
      std::swap (bpatches[0][0].x, bx);
      std::swap (bpatches[0][0].y, by);
    }

  /* Now determine the dsitance between two green patches
   
       G
     B   B
           G

     And try to confirm second green patch.  */
  coord_t gpatch_stepx = (bpatches[0][0].x - gpatches[0][0].x) * 2;
  coord_t gpatch_stepy = (bpatches[0][0].y - gpatches[0][0].y) * 2;

  size = find_patch (color_map, scr_detect::green, gpatches[0][0].x + gpatch_stepx, gpatches[0][0].y + gpatch_stepy, max_size, entries, visited, false);
  if (!size || size == max_size)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Second green patch not found\n");
      return false;
    }
  patch_found = patch_center (entries, size, &gpatches[0][1].x, &gpatches[0][1].y);
  if (!patch_found)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Center of second green patch not found\n");
      return false;
    }

  /* See if we can predict third blue patch 
       G
     B   B
           G  
             B  */
  coord_t patch_stepx = (gpatches[0][1].x - gpatches[0][0].x) / 2;
  coord_t patch_stepy = (gpatches[0][1].y - gpatches[0][0].y) / 2;
  coord_t opatch_stepx = patch_stepy;
  coord_t opatch_stepy = -patch_stepx;
  size = find_patch (color_map, scr_detect::blue, gpatches[0][1].x + patch_stepx, gpatches[0][1].y + patch_stepy, max_size, entries, visited, false);
  if (!size || size == max_size)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Third blue patch not found\n");
      return false;
    }
  patch_found = patch_center (entries, size, &bpatches[0][1].x, &bpatches[0][1].y);
  if (!patch_found)
    {
      if (report_file && verbose)
	fprintf (report_file, "Paget: Center of third blue patch not found\n");
      return false;
    }

  if (report_file && verbose)
    fprintf (report_file, "Paget: found green patch of size %i and center %f %f guessing patch distance %f %f\n", size, gpatches[0][1].x, gpatches[0][1].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches; p++)
    {
      int nx = bpatches[0][p - 1].x + patch_stepx;
      int ny = bpatches[0][p - 1].y + patch_stepy;
      size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries, visited, false);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Paget: Failed to guess green patch 0, %i with steps %f %f\n", p, patch_stepx, patch_stepy);
	  return 0;
	}
      if (!patch_center (entries, size, &gpatches[0][p].x, &gpatches[0][p].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Paget: Center of patch 0, %i is not inside\n", p);
	  return 0;
	}

      nx = gpatches[0][p].x + patch_stepx;
      ny = gpatches[0][p].y + patch_stepy;
      size = find_patch (color_map, scr_detect::blue, nx, ny, max_size, entries, visited, false);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Paget: Failed to guess blue patch 0, %i with steps %f %f\n", p, patch_stepx, patch_stepy);
	  return 0;
	}
      if (!patch_center (entries, size, &bpatches[0][p].x, &bpatches[0][p].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Paget: Center of patch 0, %i is not inside\n", p);
	  return 0;
	}

      patch_stepx = (gpatches[0][p].x - gpatches[0][0].x) / p / 2;
      patch_stepy = (gpatches[0][p].y - gpatches[0][0].y) / p / 2;
    }
  for (int p = 0; p < npatches; p++)
  {
    for (int q = 1; q < npatches * 2; q++)
      if (q & 1)
	{
	  int nx = gpatches[(q-1)/2][p].x + opatch_stepx;
	  int ny = gpatches[(q-1)/2][p].y + opatch_stepy;
	  size = find_patch (color_map, scr_detect::blue, nx, ny, max_size, entries, visited, false);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Failed to guess blue patch %i, %i with steps %f %f\n", q, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  if (!patch_center (entries, size, &bpatches[q][p].x, &bpatches[q][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Center of patch %i, %i is not inside\n", q, p);
	      return 0;
	    }
	  nx = bpatches[q-1][p].x + opatch_stepx;
	  ny = bpatches[q-1][p].y + opatch_stepy;
	  size = find_patch (color_map, scr_detect::red, nx, ny, max_size, entries, visited, false);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Failed to guess red patch %i, %i with steps %f %f\n", q, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  if (!patch_center (entries, size, &rpatches[q/2][p].x, &rpatches[q/2][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Center of patch %i, %i is not inside\n", q, p);
	      return 0;
	    }
	}
      else
	{
	  int nx = bpatches[q-1][p].x + opatch_stepx;
	  int ny = bpatches[q-1][p].y + opatch_stepy;
	  size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries, visited, false);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Failed to guess green patch %i, %i with steps %f %f\n", q, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  if (!patch_center (entries, size, &gpatches[q/2][p].x, &gpatches[q/2][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Center of patch %i, %i is not inside\n", q, p);
	      return 0;
	    }
	  nx = rpatches[(q-1)/2][p].x + opatch_stepx;
	  ny = rpatches[(q-1)/2][p].y + opatch_stepy;
	  size = find_patch (color_map, scr_detect::blue, nx, ny, max_size, entries, visited, false);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Failed to guess blue patch %i, %i with steps %f %f\n", q, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  if (!patch_center (entries, size, &bpatches[q][p].x, &bpatches[q][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Paget: Center of patch %i, %i is not inside\n", q, p);
	      return 0;
	    }
      }
  }

  sparam.remove_points ();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches; p++)
    {
      sparam.add_point ({gpatches[r][p].x, gpatches[r][p].y}, {(r+p)/2.0, (p-r)/2.0}, solver_parameters::green);
      sparam.add_point ({bpatches[r*2][p].x, bpatches[r*2][p].y}, {(r+p+0.5)/2.0, (p-r+0.5)/2.0}, solver_parameters::blue);
    }
  if (verbose)
    printf ("Paget: Initial screen found\n");

  return true;
}

/* Verify that there is patch on coordinates X and Y of color C
   of size in range of MIN_PATCH_SIZE and MAX_PATCH_SIZE.  */

bool
confirm_patch (FILE *report_file, color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, int max_patch_size, coord_t max_distance,
	       coord_t *cx, coord_t *cy, int *priority, bitmap_2d *visited)
{
  //if (x < 0 || y < 0 || x >= color_map->width || y >= color_map->height)
    //return false;
  patch_entry entries[max_patch_size + 1];
  const char *fail = NULL;
  bool set_p = 0;//= visited->test_bit ((int)(x + 0.5), (int)(y + 0.5));
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), max_patch_size + 1, entries, visited, true);
  if (size < min_patch_size)
    {
      if (set_p)
        fail = "rejected: already visited";
      else if (!size)
        fail = "rejected: zero size";
      else
        fail = "rejected: too small";
    }
  else if (size > max_patch_size)
    fail = "rejected: too large";
  else if (!patch_center (entries, size, cx, cy))
    fail = "rejected: center not in patch";
  else if ((*cx - x) * (*cx - x) + (*cy - y) * (*cy - y) > max_distance * max_distance)
    fail = "rejected: distance out of tolerance";
  if (report_file && fail && verbose)
    fprintf (report_file, "size: %i (expecting %i...%i) coord: %f %f center %f %f color %i%s\n", size, min_patch_size, max_patch_size,x,y, *cx, *cy, (int)c, fail ? fail : "");
  //printf ("center %f %f\n", *cx, *cy);
  if (fail)
    {
      for (int i = 0; i < size; i++)
	visited->clear_bit (entries[i].x, entries[i].y);
      //printf ("%s %i %i color %i %i size %i %i...%i\n", fail,(int)(x+0.5),(int)(y+0.5), (int)c, (int)color_map->get_class ((int)(x+0.5),(int)(y+0.5)), size, min_patch_size, max_patch_size);
      return false;
    }
  coord_t dist = (*cx - x) * (*cx - x) + (*cy - y) * (*cy - y);
  if (dist < max_distance * max_distance / 128)
    *priority = 7;
  else if (dist < max_distance * max_distance / 32)
    *priority = 6;
  else if (dist < max_distance * max_distance / 8)
    *priority = 5;
  else
    *priority = 4;
  return true;
}

#define N_PRIORITIES 8

bool
confirm (render_scr_detect *render,
	 point_t coordinate1,
	 point_t coordinate2,
	 coord_t x, coord_t y, scr_detect::color_class t,
	 int width, int height,
	 coord_t max_distance,
	 coord_t *rcx, coord_t *rcy, int *priority,
	 coord_t sum_range,
	 coord_t patch_xscale, coord_t patch_yscale,
	 bool strip, bool corners,
	 luminosity_t min_contrast)
{
  /*  bestcx, bestcy are adjusted locations of the patch.  */
  coord_t bestcy = x, bestcx = y;
  /*  bestouter_lr is intensity of pixels on left and right boundary the patch
      bestouter_ud is intensity of pixels on upwards and downwards boundary the patch
      bestouter_corners is intensity of pixels in corners of the patch
      bestinner is intensity of pixels inside of patch.  */
  luminosity_t minsum = 0, bestinner = 0, bestouter_lr = 0, bestouter_corners = 0, bestouter_ud = 0;

  /* Analyze (sample_steps * 2 + 1) x (sample_steps * 2 + 1) pixels.
     The inner square of (sample_steps * 2 - 1) x (sample_steps * 2 - 1) should be inside of
     the patch while the boundary outside.
     Separate boundary by outer_space to allow unsharp patches.  */
  const int sample_steps = 2;
  const int outer_space = 1;
  // TODO: Works for Dufay.  */
  //const coord_t pixel_step = 0.1;

  /* Search just part of max distance range.  */
  const int max_distance_scale = 16;
  bool found = false;
  /* We go to both directions from given X and Y coordinates.  */
  sum_range = 0.5 * sum_range;

  int xmin = ceil (std::min (std::min (coordinate1.x * sum_range, coordinate2.x * sum_range), std::min (-coordinate1.x * sum_range, -coordinate2.x * sum_range)));
  int xmax = ceil (std::max (std::max (coordinate1.x * sum_range, coordinate2.x * sum_range), std::max (-coordinate1.x * sum_range, -coordinate2.x * sum_range)));
  int ymin = ceil (std::min (std::min (coordinate1.y * sum_range, coordinate2.y * sum_range), std::min (-coordinate1.y * sum_range, -coordinate2.y * sum_range)));
  int ymax = ceil (std::max (std::max (coordinate1.y * sum_range, coordinate2.y * sum_range), std::max (-coordinate1.y * sum_range, -coordinate2.y * sum_range)));
  coord_t scaled_max_distance = max_distance / max_distance_scale;
  coord_t pixel_step = scaled_max_distance / 5;

  /* We want to sum symmetrically in each direction.  */
  ymin = xmin = std::min (std::min (std::min (xmin, ymin), -xmax), -ymax);
  ymax = xmax = /*std::min (xmax, ymax)*/ -ymin;

  if (verbose_confirm > 1)
    printf ("pixel step: %f ranges %i %i distance %f\n",pixel_step, xmax - xmin, ymax - ymin, scaled_max_distance);

  /* Do not try to search towards end of screen since it gives wrong resutls.
     broder 4x4 is necessary for interpolation.  */
  if (y - scaled_max_distance + ymin - 4 < 0
      || y + scaled_max_distance + ymax + 4 >= height
      || x - scaled_max_distance + xmin - 4 < 0
      || x + scaled_max_distance + xmax + 3 >= width)
    return false;

  if (!strip)
    {
      luminosity_t min = INT_MAX, max = INT_MIN;
      for (coord_t cy = std::max (y - scaled_max_distance, (coord_t)-ymin); cy <= std::min (y + scaled_max_distance, (coord_t)height - ymax); cy+= pixel_step)
	for (coord_t cx = std::max (x - scaled_max_distance, (coord_t)-xmin); cx <= std::min (x + scaled_max_distance, (coord_t)width - xmax); cx+= pixel_step)
	  {
	    int xstart = floor (cx + xmin);
	    int ystart = floor (cy + ymin);
	    coord_t xsum = 0;
	    coord_t ysum = 0;
#define account(xx, yy, wx, wy)									\
		    { rgbdata color = render->fast_precomputed_get_normalized_pixel (xx, yy);	\
		      luminosity_t c = color[t];					\
		      xsum += c * wx * wy * (xx + 0.5 - cx);				\
		      ysum += c * wx * wy * (yy + 0.5 - cy);				\
		      max = std::max (max, c);						\
		      min = std::min (min, c); /*printf("  %7.2f %7.2f", (coord_t)wx, (coord_t)wy)*/;}

		      //luminosity_t c = color[t];
		      //luminosity_t d = std::max (color[0] + color[1] + color[2], (luminosity_t)0.0001);
		      //c = c / d;							
	    for (int yy = ystart ; yy < ystart + ymax - ymin + 1; yy++)
	      {
		coord_t wy = 1;
		if (yy == ystart)
		  wy = 1 - (cy+ymin - ystart);
		if (yy == ystart + ymax - ymin)
		  wy = (cy+ymin - ystart);
	        coord_t wx = 1 - (cx + ymin - xstart);
		int xx = xstart;
		account (xx, yy, wx, wy);
		for (xx = xstart + 1; xx < xstart + xmax - ymin; xx++)
		  account (xx, yy, 1, wy);
		account (xx, yy, (1 - wx), wy);
		//printf ("\n");
	      }
#undef account

	   coord_t sum = xsum * xsum + ysum * ysum;
	    if (verbose_confirm > 1)
	      printf (" trying %f %f : %f %f xsum %f ysum %f sum %f %s\n  ", cx, cy, cx - x, cy - y, xsum, ysum, sum, minsum > sum ? "new best":"");
	   if (!found || minsum > sum)
	     {
	       bestcx = cx;
	       bestcy = cy;
	       minsum = sum;
	       found = true;
	     }
	  }
      if (verbose_confirm > 1)
	{
	   int xstart = floor (bestcx + xmin);
	   int ystart = floor (bestcy + ymin);
	   printf ("best %f %f : %f %f min %f max %f\n  ", bestcx, bestcy, bestcx - x, bestcy - y, min, max);
	   for (int xx = xstart; xx < floor (bestcx); xx++)
	     printf (" ");
	   printf ("|\n");

	   for (int yy = ystart ; yy < ystart + ymax - ymin + 1; yy++)
	     {
	       printf (yy == floor (bestcy) ? "->" : "  ");

#define account(xx, yy, wx, wy)\
		     { rgbdata color = render->fast_precomputed_get_normalized_pixel (xx, yy);	\
		       luminosity_t c = color[t];\
		       luminosity_t d = std::max (color[0] + color[1] + color[2], (luminosity_t)0.0001);\
		       c = c / d;							\
		       putc(".oO*"[(int)((c-min) * 3.9999 / (max - min))], stdout); }
		       //printf (" %7.4f", c); }
		       //printf (" %i %i %5.2f*%5.2f*%5.2f", xx, yy, c, (coord_t)wx, wy); }
	       int xx = xstart;
	       account (xx, yy, wx, wy);
	       for (xx = xstart + 1; xx < xstart + xmax - ymin; xx++)
		 account (xx, yy, 1, wy);
	       account (xx, yy, (1 - wx), wy);
#undef account
	       printf ("\n");
	     }
	}
#if 0
      //printf ("%i %i %i %i\n",xmin,xmax,ymin,ymax);
      for (coord_t cy = std::max (y - scaled_max_distance, (coord_t)-ymin); cy <= std::min (y + scaled_max_distance, (coord_t)height - ymax); cy+= pixel_step)
	for (coord_t cx = std::max (x - scaled_max_distance, (coord_t)-xmin); cx <= std::min (x + scaled_max_distance, (coord_t)width - xmax); cx+= pixel_step)
	  {
	    coord_t xsum = 0;
	    coord_t ysum = 0;
	    int maxdistsq = xmax * xmax + ymax * ymax;
	    switch ((int) t)
	      {
		case 0:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		case 1:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		case 2:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		      ysum += color[t] * (yy + 0.5 - cy) * (maxdistsq - (yy - cy) * (yy - cy) - (xx - cx) * (xx - cx) + 1);
		    }
		break;
		default:
		abort ();
	      }
	   coord_t sum = xsum * xsum + ysum * ysum;
	   //printf ("%f %f: %f %f %f\n", cx-x, cy-y, xsum, ysum, sum);
	   if (!found || minsum > sum)
	     {
	       bestcx = cx;
	       bestcy = cy;
	       minsum = sum;
	       found = true;
	   }
	 }
      if (!found)
	return false;
#endif
    }
  else
  {
    bestcx = x;
    bestcy = y;
  }
  //int nouter = 0, ninner = 0;
  luminosity_t min = 0;
  if (verbose_confirm > 1)
    {
      printf ("coordinate1 %f %f\n", coordinate1.x, coordinate1.y);
      printf ("coordinate2 %f %f\n", coordinate2.x, coordinate2.y);
      printf ("patch_xscale %f %f t %i strip %i corner %i\n", patch_xscale, patch_yscale, t, strip, corners);
    }
  for (int yy = -sample_steps - outer_space; yy <= sample_steps + outer_space; yy++)
    {
      /* Make bigger gap between the outer set of points and inner ones so image can be unsharp.  */
      if (yy == -sample_steps - outer_space + 1 || yy == sample_steps)
	yy+= outer_space;
      for (int xx = -sample_steps - outer_space; xx <= sample_steps + outer_space; xx++)
	{
	  if (xx == -sample_steps - outer_space + 1 || xx == sample_steps)
	    xx += outer_space;
	  coord_t ax = bestcx + (xx * ( 1 / ((coord_t)sample_steps + 2 * outer_space) * patch_xscale)) * coordinate1.x + (yy * (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_yscale)) * coordinate2.x;
	  coord_t ay = bestcy + (xx * ( 1 / ((coord_t)sample_steps + 2 * outer_space) * patch_xscale)) * coordinate1.y + (yy * (1 / ((coord_t)sample_steps + 2 * outer_space) * patch_yscale)) * coordinate2.y;

	  rgbdata d = render->get_adjusted_pixel (ax, ay);
	  luminosity_t color[3] = {d.red, d.green, d.blue};

	  luminosity_t sum = color[0]+color[1]+color[2];
	  color[0] = std::max (color[0], (luminosity_t)0);
	  color[1] = std::max (color[1], (luminosity_t)0);
	  color[2] = std::max (color[2], (luminosity_t)0);
	  sum = std::max (sum, (luminosity_t)0.0001);
	  //sum=1;
	  luminosity_t val = color[t] / sum;
	  min = std::min (val, min);
	  if (verbose_confirm > 2)
	    printf (" [% 6.2F % 6.2F]:", ax - bestcx, ay - bestcy);
	  if (verbose_confirm > 1)
	    printf ("   r% 8.3F g% 8.3F b% 8.3F *% 8.3F*", color[0]*100, color[1]*100, color[2]*100, val);
	  if (/*sum > 0 && color[t] > 0*/1)
	    {
	      bool lr = (xx == -sample_steps - outer_space || xx == sample_steps + outer_space);
	      bool ud = (yy == -sample_steps - outer_space || yy == sample_steps + outer_space);
	      if (lr && ud)
		bestouter_corners += val;
	      else if (lr)
		bestouter_lr += val;
	      else if (ud)
		bestouter_ud += val;
	      else 
		bestinner += val;
#if 0
	      if (corners && ((xx == -sample_steps - outer_space || xx == sample_steps + outer_space) && (yy == -sample_steps - outer_space || yy == sample_steps + outer_space)))
		{
		  if (verbose_confirm > 1)
		    printf ("X");
		}
	      else if ((!strip && (xx == -sample_steps - outer_space || xx == sample_steps + outer_space)) || yy == -sample_steps - outer_space || yy == sample_steps + outer_space)
		{
		  if (xx == -sample_steps - outer_space || xx == sample_steps + outer_space)
		    bestouter_lr += val;
		  if (yy == -sample_steps - outer_space || yy == sample_steps + outer_space)
		    bestouter_ud += val;
		  if (verbose_confirm > 1)
		    printf ("O");
		}
	      else
		{
		  bestinner += val;// ninner++;
		  if (verbose_confirm > 1)
		    printf ("I");
		}
#endif
	    }
	}
      if (verbose_confirm > 1)
	printf("\n");
    }
  //printf ("%f %f %f\n",min, bestinner, bestouter);

  *rcx = bestcx;
  *rcy = bestcy;
  /*  For sample_steps == 2:
      O O O O O
      O I I I O
      O I I I O
      O I I I O
      O O O O O  */
  if (!strip && !corners)
    {
      int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
      /* We count left/right and up/down separately. Corners are counted twice.  */
      int nouter = (2 * sample_steps - 1) * 2;
      bestinner -= min * ninner;
      bestouter_lr -= min * nouter;
      bestouter_ud -= min * nouter;
      bestouter_corners -= min * 4;
      bestinner *= (1 / (luminosity_t) ninner);
      bestouter_lr *= (1 / (luminosity_t) nouter);
      bestouter_ud *= (1 / (luminosity_t) nouter);
      bestouter_corners *= (1 / (luminosity_t) 4);
    }
  /*  For sample_steps == 2:
        O O O  
      O I I I O
      O I I I O
      O I I I O
        O O O    */
  else if (!strip)
    {
      int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
      int nouter = (2 * sample_steps - 1) * 2;

      bestinner -= min * ninner;
      bestouter_lr -= min * nouter;
      bestouter_ud -= min * nouter;
      bestinner *= (1 / (luminosity_t) ninner);
      bestouter_lr *= (1 / (luminosity_t) nouter);
      bestouter_ud *= (1 / (luminosity_t) nouter);
      bestouter_corners = 0;
    }
  /*  For sample_steps == 2:
      O O O O O
      I I I I I
      I I I I I
      I I I I I
      O O O O O  */
  else
    {
      int ninner = (2 * sample_steps + 1) * (2 * sample_steps - 1);
      int nouter = (2 * sample_steps + 1) * 2;
      bestinner += bestouter_lr;
      bestouter_ud += bestouter_corners;
      bestouter_lr = 0;
      bestouter_corners = 0;

      bestinner -= min * ninner;
      bestouter_ud -= min * nouter;
      bestinner *= (1 / (luminosity_t) ninner);
      bestouter_ud *= (1 / (luminosity_t) nouter);
    }
  luminosity_t bestouter = std::max (std::max (std::max (bestouter_ud, (luminosity_t) 0.00001), bestouter_lr), bestouter_corners);
  coord_t dist = (bestcx - x) * (bestcx - x) + (bestcy - y) * (bestcy - y);
  if (bestinner <= 0 || bestinner < bestouter_lr * min_contrast)
    {
      if (verbose_confirm)
        printf ("FAILED: given:%f %f best:%f %f inner:%f outer:%f %f ratio: %f color:%i min:%f\n", x, y, bestcx-x, bestcy-y, bestinner, bestouter_lr, bestouter_ud, bestinner/bestouter, (int) t, min);
      return false;
    }
  else if (bestinner > bestouter * 8 * min_contrast && dist < max_distance * max_distance / 128)
    *priority = 3;
  else if (bestinner > bestouter * 4 * min_contrast && dist < max_distance * max_distance / 32)
    *priority = 2;
  else if (bestinner > bestouter * 2 * min_contrast)
    *priority = 1;
  else
    *priority = 0;
  if (verbose_confirm > 1)
    printf ("given:%f %f best:%f %f inner:%f outer:%f %f ratio:%f priority:%i color:%i\n", x, y, bestcx-x, bestcy-y, bestinner, bestouter_lr, bestouter_ud, bestinner/bestouter, *priority, (int)t);
  return true;
}

/* Flood fill is controlled withpriority queue with fixed
   number of priorities.  This reduces chances that misdetected
   patches will be used as seeds to identify large parts of screen.  */

template<int N, typename T>
class priority_queue
{
public:
  priority_queue ()
  : sum {}
  {}
  const int npriorities = N;
  std::vector<T> queue[N];
  int sum[N];

  void
  insert (T e, int priority)
  {
    sum[priority]++;
    queue[N - priority - 1].push_back (e);
  }

  bool
  extract_min (T &e)
  {
    for (int i = 0; i < npriorities; i++)
      if (queue[i].size ())
	{
	  e = queue[i].back ();
	  queue[i].pop_back ();
	  return true;
	}
    return false;
  }

  void
  print_sums (FILE *f)
  {
    fprintf (f, "Overall priority entries:");
    for (int n : sum)
      {
	fprintf (f, " %i", n);
      }
    fprintf (f, "\n");
  }
};

static solver_parameters::point_color
diagonal_coordinates_to_color (int x, int y)
{
  if (!(y&1))
    return (x&1) ? solver_parameters::blue : solver_parameters::green;
  return (x&1) ? solver_parameters::red : solver_parameters::blue;
}

std::unique_ptr <screen_map>
flood_fill (FILE *report_file, bool slow, bool fast, coord_t greenx, coord_t greeny, scr_to_img_parameters &param, image_data &img, render_scr_detect *render, color_class_map *color_map, solver_parameters *sparam, bitmap_2d *visited, int *npatches, detect_regular_screen_params *dsparams, progress_info *progress)
{
  double screen_xsize = sqrt (param.coordinate1.x * param.coordinate1.x + param.coordinate1.y * param.coordinate1.y);
  double screen_ysize = sqrt (param.coordinate2.x * param.coordinate2.x + param.coordinate2.y * param.coordinate2.y);

  /* If screen is estimated too small or too large give up.  */
  if (screen_xsize < 2 || screen_ysize < 2 || screen_xsize > 100 || screen_ysize > 100)
    return NULL;

  /* Do not flip the image.  */
  if (param.type == Dufay && param.coordinate1.y < 0)
    return NULL;

  scr_to_img scr_map;
  scr_map.set_parameters (param, img);

  int max_patch_size = floor (screen_xsize * screen_ysize / 1.5);
  int min_patch_size = (int)(screen_xsize * screen_ysize / 8);

  if (param.type != Dufay)
    {
      /* Dufay has 4 square patches and one strip per screen tile, while Paget/Finlay has 8 squares.  */
      //max_patch_size /= 2;
      //min_patch_size /= 2;
      /* Disable pixels on boundaries.  */
      min_patch_size = min_patch_size - 4 * sqrt (min_patch_size);
      //fprintf (stderr, "Computed min patch size %i\n", min_patch_size);
    }
  min_patch_size = std::max (min_patch_size, 1);
  max_patch_size = std::max (max_patch_size, min_patch_size + 1);
  coord_t max_distance = (screen_xsize + screen_ysize) * 0.1;
  int nfound = 1;

  int xshift, yshift, width, height;
  scr_map.get_range (img.width, img.height, &xshift, &yshift, &width, &height);
#if 0
  if (width <= xshift)
    width = xshift + 1;
  if (height <= yshift)
    height = yshift + 1;
#endif
  int nexpected = (param.type != Dufay ? 8 : 2) * img.width * img.height / (screen_xsize * screen_ysize);
  //printf ("Flood fill started with coordinates %f,%f and %f,%f\n", param.coordinate1.x, param.coordinate1.y, param.coordinate2.x, param.coordinate2.y);
  /* Be sure that coordinates 0,0 are on screen.
     Normally, this should always be the case since screen discovery always
     places point 0,0 on screen. But in case of large deformations we may hit this.   */
  if (width <= xshift || height <= yshift)
    return NULL;
  if (report_file)
    fprintf (report_file, "Flood fill started with coordinates %f,%f and %f,%f\n", param.coordinate1.x, param.coordinate1.y, param.coordinate2.x, param.coordinate2.y);
  if (progress)
    progress->set_task ("Flood fill", nexpected);
  if (param.type == Dufay)
    {
      xshift *= 2;
      width *= 2;
    }
  else
    {
      int xmin, ymin, xmax, ymax;
      analyze_base::data_entry p = paget_geometry::to_diagonal_coordinates ((analyze_base::data_entry){-xshift, -yshift});
      int ix = p.x / 2;
      int iy = p.y / 2;
      xmin = ix - 1;
      xmax = ix + 1;
      ymin = iy - 1;
      ymax = iy + 1;
      p = paget_geometry::to_diagonal_coordinates ((analyze_base::data_entry){-xshift + width, -yshift});
      ix = p.x / 2;
      iy = p.y / 2;
      xmin = std::min (xmin, ix - 1);
      xmax = std::max (xmax, ix + 1);
      ymin = std::min (ymin, iy - 1);
      ymax = std::max (ymax, iy + 1);
      p = paget_geometry::to_diagonal_coordinates ((analyze_base::data_entry){-xshift, -yshift + height});
      ix = p.x / 2;
      iy = p.y / 2;
      xmin = std::min (xmin, ix - 1);
      xmax = std::max (xmax, ix + 1);
      ymin = std::min (ymin, iy - 1);
      ymax = std::max (ymax, iy + 1);
      p = paget_geometry::to_diagonal_coordinates ((analyze_base::data_entry){-xshift + width, -yshift + height});
      ix = p.x / 2;
      iy = p.y / 2;
      xmin = std::min (xmin, ix - 1);
      xmax = std::max (xmax, ix + 1);
      ymin = std::min (ymin, iy - 1);
      ymax = std::max (ymax, iy + 1);
      xshift = -xmin * 2;
      yshift = -ymin * 2;
      width = (xmax - xmin) * 2;
      height = (ymax - ymin) * 2;
    }
  std::unique_ptr <screen_map> map (new screen_map(param.type, xshift, yshift, width, height));

  struct queue_entry
  {
    /* X and Y inscreen specific coordinates so all points appear at integer
       for Dufay X is multiplied by 2; for paget these are diagonal coordinates.  */
    int scr_x, scr_y;
    coord_t img_x, img_y;
  };
  priority_queue<N_PRIORITIES,queue_entry> queue;
  queue.insert ((struct queue_entry){0, 0, greenx, greeny}, 0);
  map->set_coord ({0, 0}, {greenx, greeny});
  if (sparam)
    sparam->remove_points ();
  //printf ("%i %i %f %f %f %f\n", queue.size (), map.in_range_p (0, 0), param.coordinate1.x, param.coordinate1.y, param.coordinate2.x, param.coordinate2.y);
  queue_entry e;
  while (queue.extract_min (e)
	 && (!progress || !progress->cancel_requested ()))
    {
      coord_t ix, iy;
      int priority = 0;
      int priority2 = 0;
      //if (verbose)
        //printf ("visiting %i %i %f %f %f %f\n", e.scr_x, e.scr_y, e.img_x, e.img_y, param.coordinate1.x, param.coordinate1.y);
      if (progress)
        progress->inc_progress ();
      if (param.type == Dufay)
	{
	  if (sparam)
	    sparam->add_point ({e.img_x, e.img_y}, {e.scr_x / 2.0, (coord_t)e.scr_y}, e.scr_y ? solver_parameters::blue : solver_parameters::green);

  // search range should be 1/2 but 1/3 seems to work better in practice. Maybe it is because we look into orthogonal bounding box of the area we really should compute.
#define cpatch(x,y,t, priority) ((fast && confirm_patch (report_file, color_map, x, y, t, min_patch_size, max_patch_size, max_distance, &ix, &iy, &priority, visited)) \
				 || (slow && confirm (render, param.coordinate1, param.coordinate2, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, 1.0 / 3, 0.5, 0.5, false, false, dsparams->min_patch_contrast)))
#define cstrip(x,y,t, priority) ((fast && confirm_strip (color_map, x, y, t, min_patch_size, &priority, visited)) \
				 || (slow && confirm (render, param.coordinate1, param.coordinate2, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, 1.0 / 3, 0.5, 0.5, true, false, dsparams->min_patch_contrast)))
	  if (!map->known_p ({e.scr_x - 1, e.scr_y})
	      && cpatch (e.img_x - param.coordinate1.x / 2, e.img_y - param.coordinate1.y / 2, ((e.scr_x - 1) & 1) ? scr_detect::blue : scr_detect::green, priority))
	    {
	      map->safe_set_coord ({e.scr_x - 1, e.scr_y}, {ix, iy});
	      queue.insert ((struct queue_entry){e.scr_x - 1, e.scr_y, ix, iy}, priority);
	      nfound++;
	    }
	  if (!map->known_p ({e.scr_x + 1, e.scr_y})
	      && cpatch (e.img_x + param.coordinate1.x / 2, e.img_y + param.coordinate1.y / 2, ((e.scr_x + 1) & 1) ? scr_detect::blue : scr_detect::green, priority))
	    {
	      map->safe_set_coord ({e.scr_x + 1, e.scr_y}, {ix, iy});
	      queue.insert ((struct queue_entry){e.scr_x + 1, e.scr_y, ix, iy}, priority);
	      nfound++;
	    }
	  if (!map->known_p ({e.scr_x, e.scr_y - 1})
	      && cstrip (e.img_x - param.coordinate2.x / 2, e.img_y - param.coordinate2.y / 2, scr_detect::red, priority)
	      && cpatch (e.img_x - param.coordinate2.x, e.img_y - param.coordinate2.y, (e.scr_x & 1) ? scr_detect::blue : scr_detect::green, priority2))
	    {
	      map->safe_set_coord ({e.scr_x, e.scr_y - 1}, {ix, iy});
	      queue.insert ((struct queue_entry){e.scr_x, e.scr_y - 1, ix, iy}, std::min (priority, priority2));
	      nfound++;
	    }
	  if (!map->known_p ({e.scr_x, e.scr_y + 1})
	      && cstrip (e.img_x + param.coordinate2.x / 2, e.img_y + param.coordinate2.y / 2, scr_detect::red, priority)
	      && cpatch (e.img_x + param.coordinate2.x, e.img_y + param.coordinate2.y, (e.scr_x & 1) ? scr_detect::blue : scr_detect::green, priority2))
	    {
	      map->safe_set_coord ({e.scr_x, e.scr_y + 1}, {ix, iy});
	      queue.insert ((struct queue_entry){e.scr_x, e.scr_y + 1, ix, iy}, std::min (priority, priority2));
	      nfound++;
	    }
#undef cstrip
#undef cpatch
	}
      else
	{
	  /* Blue patches are smaller.  */
	  int blue_min_patch_size = (min_patch_size + 1) / 2;
	  point_t c1 = param.coordinate2 - param.coordinate1;
	  point_t c2 = param.coordinate1 + param.coordinate2;
#define cpatch(x,y,t, priority) ((fast && confirm_patch (report_file, color_map, x, y, t, t == scr_detect::blue ? blue_min_patch_size : min_patch_size, max_patch_size, max_distance, &ix, &iy, &priority, visited)) \
				 || (slow && confirm (render, c1, c2, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, 1.0 / 3, 0.20, t == scr_detect::blue ? 0.18 : 0.25, false, t == scr_detect::blue, dsparams->min_patch_contrast)))
				 //|| (!fast && confirm (render, c1_x, c1_y, c2_x, c2_y, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, 1.0 / 6, 0.33 / 2, 0.33 / 2, false)))
	  if (sparam)
	    {
	      analyze_base::data_entry p = paget_geometry::from_diagonal_coordinates ((analyze_base::data_entry){e.scr_x, e.scr_y});
	      solver_parameters::point_color color = diagonal_coordinates_to_color (e.scr_x, e.scr_y);
	      if (sparam)
		sparam->add_point ({e.img_x, e.img_y}, {p.x / 2.0, p.y / 2.0}, color);
	    }
	  for (int xx = -1; xx <= 1; xx++)
	    for (int yy = -1; yy <= 1; yy++)
	      if ((xx || yy)// && ((xx != 0) + (yy != 0)) == 1
	          && !map->known_p ({e.scr_x + xx, e.scr_y + yy}))
		{
	          analyze_base::data_entry p = paget_geometry::from_diagonal_coordinates ((analyze_base::data_entry){xx, yy});
		  solver_parameters::point_color color = diagonal_coordinates_to_color (e.scr_x + xx, e.scr_y + yy);
		  if (cpatch (e.img_x + p.x * param.coordinate1.x / 4 + p.y * param.coordinate2.x / 4, e.img_y + p.x * param.coordinate1.y / 4 + p.y * param.coordinate2.y / 4, (scr_detect::color_class)color, priority))
		    {
		      map->safe_set_coord ({e.scr_x + xx, e.scr_y + yy}, {ix, iy});
		      queue.insert ((struct queue_entry){e.scr_x + xx, e.scr_y + yy, ix, iy}, priority);
		      nfound++;
		    }
		}
#undef cpatch
	}
    }
  if (progress && progress->cancel_requested ())
    return NULL;
  /* Dufay screen has two points per screen repetetion.  */
  *npatches = nfound;
  if (nfound < 100)
    return NULL;

  /* Determine better screen dimension so the overall statistics are meaningfull.  */
  solver_parameters sparam2;
  scr_to_img_parameters param2;
  if (dsparams->do_mesh)
    sparam2.optimize_lens = sparam2.optimize_tilt = false;
  map->determine_solver_points (*npatches, &sparam2);
  param2 = param;
  simple_solver (&param2, img, sparam2, progress);
  if (progress && progress->cancel_requested ())
    return NULL;
  screen_xsize = sqrt (param2.coordinate1.x * param2.coordinate1.x + param2.coordinate1.y * param2.coordinate1.y);
  screen_ysize = sqrt (param2.coordinate2.x * param2.coordinate2.x + param2.coordinate2.y * param2.coordinate2.y);
  nexpected = (param.type != Dufay ? 8 : 2) * img.width * img.height / (screen_xsize * screen_ysize);

  /* Check for large unanalyzed areas.  */
  scr_to_img map2;
  map2.set_parameters (param2, img);
  int xmin, ymin, xmax, ymax;
  map->get_known_range (&xmin, &ymin, &xmax, &ymax);
  int snexpected = (param.type != Dufay ? 8 : 2) * (xmax - xmin) * (ymax - ymin) / (screen_xsize * screen_ysize);
  if (snexpected > 0 && nfound > 1000)
    {
# if 0
      progress->pause_stdout ();
      printf ("Analyzed %2.2f%% of scan and %2.2f%% of the screen area", nfound * 100.0 / nexpected, nfound * 100.0 / snexpected);
      printf ("; left border: %2.2f%%", xmin * 100.0 / img.width);
      printf ("; top border: %2.2f%%", ymin * 100.0 / img.height);
      printf ("; right border: %2.2f%%", 100 - xmax * 100.0 / img.width);
      printf ("; bottom border: %2.2f%%", 100 - ymax * 100.0 / img.height);
      printf ("\n");
      progress->resume_stdout ();
#endif
      if (report_file)
	fprintf (report_file, "Analyzed %2.2f%% of scan and %2.2f%%  of the screen area", nfound * 100.0 / nexpected, nfound * 100.0 / snexpected);
      if (report_file)
	fprintf (report_file, "; left border: %2.2f%%", xmin * 100.0 / img.width);
      if (report_file)
	fprintf (report_file, "; top border: %2.2f%%", ymin * 100.0 / img.height);
      if (report_file)
	fprintf (report_file, "; right border: %2.2f%%", 100 - xmax * 100.0 / img.width);
      if (report_file)
	fprintf (report_file, "; bottom border: %2.2f%%", 100 - ymax * 100.0 / img.height);
      if (report_file)
	{
	  fprintf (report_file, "\n");
	  queue.print_sums (report_file);
	}
    }
  if (!dsparams->do_mesh && nfound > 100000)
    return map;

  if (progress)
    progress->set_task ("Checking known range", map->height);
  for (int y = -map->yshift; y < map->height - map->yshift; y++)
    {
      int last_seen = INT_MAX / 2;
      if (progress && progress->cancel_requested ())
	return NULL;
      for (int x = -map->xshift; x < map->width - map->xshift; x++, last_seen++)
	if (!map->known_p ({x, y}))
	  {
	    point_t scr = map->get_screen_coord ({x, y});
	    point_t img = map2.to_img (scr);
	    if (img.x < xmin || img.x > xmax || img.y < ymin || img.y > ymax)
	      continue;
	    int xrmul = 2;
	    int yrmul = param.type != Dufay ? 2 : 1;
	    bool found = last_seen < dsparams->max_unknown_screen_range * xrmul;
	    for (int yy = std::max (y - dsparams->max_unknown_screen_range * yrmul, -map->yshift); yy < std::min (map->height - map->yshift, y + dsparams->max_unknown_screen_range * yrmul) && !found; yy++)
	      for (int xx = std::max (x - dsparams->max_unknown_screen_range * xrmul, -map->xshift); xx < std::min (map->width - map->xshift, x + dsparams->max_unknown_screen_range * xrmul) && !found; xx++)
		if (map->known_p ({xx, yy}))
		  {
		    last_seen = -xx;
		    found = true;
		  }
	    if (!found)
	      {
	        progress->pause_stdout ();
	        printf ("Too large unanalyzed unknown screen area around image coordinates %f %f\n", img.x, img.y);
		progress->resume_stdout ();
		if (report_file)
		  {
		    fprintf (report_file, "Too large unanalyzed unknown screen area around image coordinates %f %f\n", img.x, img.y);
		  }
		return NULL;
	      }
	    //else
		//printf ("found %i %i\n",x,y);
	  }
	else
	  last_seen = 0;
      if (progress)
	progress->inc_progress ();
    }
  if (snexpected * dsparams->min_screen_percentage > nfound * 100)
    {
      if (report_file)
	{
	  fprintf (report_file, "Detected screen patches covers only %2.2f%% of the screen\n", nfound * 100.0 / snexpected);
	  //fprintf (report_file, "Reducing --min-screen-percentage would bypass this error\n");
	}
      return NULL;
    }
  if (xmin > std::max (dsparams->border_left, (coord_t)2) * img.width / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach left border of the image (limit %f)\n", dsparams->border_left);
      return NULL;
    }
  if (ymin > std::max (dsparams->border_top, (coord_t)2) * img.height / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach top border of the image (limit %f)\n", dsparams->border_top);
      return NULL;
    }
  if (xmax < std::min (100 - dsparams->border_right, (coord_t)98) * img.width / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach right border of the image (limit %f)\n", dsparams->border_right);
      return NULL;
    }
  if (ymax < std::min (100 - dsparams->border_bottom, (coord_t)98) * img.height / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach bottom border of the image (limit %f)\n", dsparams->border_bottom);
      return NULL;
    }
  return map;
}

/* List points in range 0...(xstep-1) and 0....(ystep - 1) starting from middle.  */
struct int_point {int x,y;};
std::vector<struct int_point>check_points(int xsteps, int ysteps)
{
  std::vector<struct int_point>ret;
  ret.push_back((struct int_point){xsteps /2, ysteps /2});
  for (int d = 1; d < std::max (xsteps, ysteps); d++)
    {
      for (int i = -d; i <= d; i++)
	{
	  int xx = xsteps / 2 + i;
	  if (xx < 0 || xx >= xsteps)
	    continue;
	  int yy = ysteps / 2 - d;
	  if (yy >= 0 && yy < ysteps)
	    ret.push_back((struct int_point){xx, yy});
	  yy = ysteps / 2 + d;
	  if (yy >= 0 && yy < ysteps)
	    ret.push_back((struct int_point){xx, yy});
	}
      for (int i = -d + 1; i < d; i++)
	{
	  int yy = ysteps / 2 + i;
	  if (yy < 0 || yy >= ysteps)
	    continue;
	  int xx = xsteps / 2 - d;
	  if (xx >= 0 && xx < xsteps)
	    ret.push_back((struct int_point){xx, yy});
	  xx = xsteps / 2 + d;
	  if (xx >= 0 && xx < xsteps)
	    ret.push_back((struct int_point){xx, yy});
	}
    }
  return ret;
}

void
summarise_quality (image_data &img, screen_map *smap, scr_to_img_parameters &param, const char *type, FILE *report_file, progress_info *progress)
{
  coord_t max_distance[3] = {0,0,0};
  coord_t distance_sum[3] = {0,0,0};
  int distance_num[3] = {0,0,0};
  int one_num[3] = {0,0,0};
  int four_num[3] = {0,0,0};
  scr_to_img map;
  map.set_parameters (param, img);
  for (int y = -smap->yshift; y < smap->height - smap->yshift; y++)
    for (int x = -smap->xshift; x < smap->width - smap->xshift; x++)
      if (smap->known_and_not_fake_p ({x, y}))
	{
	  solver_parameters::point_color color;
	  point_t scrp = smap->get_screen_coord ({x, y}, &color);
	  point_t imgp = map.to_img (scrp);
	  point_t imgp2 = smap->get_coord ({x, y});
	  coord_t dist = imgp.dist_from (imgp2);
	  int t = (int)color;
	  max_distance [t] = std::max (max_distance[t], dist);
	  distance_sum [t] += dist;
	  distance_num [t] ++;
	  if (dist >= 4)
	    four_num[t]++;
	  else if (dist >= 1)
	    one_num[t]++;
	}
  if (progress)
    progress->pause_stdout ();
  for (int c = 0; c < 3; c++)
    if (distance_num[c])
      {
	const char *channel[3]={"Red", "Green", "Blue"};
#if 0
        printf ("%s patches %i. Avg distance to %s solution %f; max distance %f; %2.2f%% with distance over 1 and %2.2f%% with distance over 4\n", channel[c], distance_num[c], type, distance_sum[c] / distance_num[c], max_distance[c], (one_num[c] + four_num[c]) * 100.0 / distance_num[c], four_num[c] * 100.0 / distance_num[c]);
#endif
        if (report_file)
	  fprintf (report_file, "%s patches %i. Avg distance to %s solution %f; max distance %f; %2.2f%% with distance over 1 and %2.2f%% with distance over 4\n", channel[c], distance_num[c], type, distance_sum[c] / distance_num[c], max_distance[c], one_num[c] * 100.0 / distance_num[c], four_num[c] * 100.0 / distance_num[c]);
      }
  if (progress)
    progress->resume_stdout ();
}

detected_screen
detect_regular_screen_1 (image_data &img, enum scr_type type, scr_detect_parameters &dparam, luminosity_t gamma, solver_parameters &sparam, detect_regular_screen_params *dsparams, progress_info *progress, FILE *report_file)
{
  /* Try both screen types; it is cheap to do so and seems to work quite reliable now.  */
  const bool try_dufay = true;
  const bool try_paget_finlay = true;

  detected_screen ret;
  render_parameters empty;
  std::unique_ptr <screen_map> smap = NULL;

  empty.gamma = gamma;
  ret.mesh_trans = NULL;
  ret.success = false;
  ret.known_patches = NULL;
  ret.smap = NULL;
  ret.param.type = type;
  scr_to_img_parameters param;
  param.type = type;

  {
    bitmap_2d visited (img.width, img.height);
    bitmap_2d visited_paget (img.width, img.height);
    std::unique_ptr <render_scr_detect> render = NULL;
    std::unique_ptr <color_class_map> cmap = NULL;
    const int search_xsteps = 6;
    const int search_ysteps = 6;

    /* We try to detect screen starting from various places in the scans organized from
       center to the border.  */
    auto points = check_points (search_xsteps, search_ysteps);

    if (progress)
      progress->set_task ("Looking for initial grid", search_xsteps * search_ysteps);
    for (int s = 0; s < (int)points.size () && !smap; s++)
      if (!progress || !progress->cancel_requested ())
	{
	  int xmin = points[s].x * img.width / search_xsteps;
	  int ymin = points[s].y * img.height / search_ysteps;
	  int xmax = (points[s].x + 1) * img.width / search_xsteps;
	  int ymax = (points[s].y + 1) * img.height / search_ysteps;
	  int nattempts = 0;
	  const int  maxattempts = 10;
	  if (report_file)
	    fflush (report_file);
	  if (progress)
	    progress->push ();
	  if (dsparams->optimize_colors)
	    {
	      if (!optimize_screen_colors (&dparam, &img, gamma, xmin, ymin, std::min (1000, xmax - xmin), std::min (1000, ymax - ymin), progress, report_file))
		{
		  if (progress)
		    progress->pause_stdout ();
		  printf ("Failed to analyze colors on start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
		  if (report_file)
		    fprintf (report_file, "Failed to analyze colors on start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
		  if (progress)
		    progress->resume_stdout ();
		  if (progress)
		    {
		      progress->pop ();
		      progress->inc_progress ();
		    }
		  continue;
		}
	      /* Re-detect screen.  */
	      cmap = NULL;
	      render = NULL;
	    }
	  if (!render)
	    {
	      prune_render_scr_detect_caches ();
	      std::unique_ptr<render_scr_detect> new_render (new render_scr_detect (dparam, img, empty, 256));
	      if (!new_render)
		{
		  if (progress)
		    progress->pop ();
		  return ret;
		}
	      render = std::move (new_render);
	      if (!render->precompute_all (false, false, progress)
		  || !render->precompute_rgbdata (progress))
		{
		  render = NULL;
		  if (progress)
		    {
		      progress->pop ();
		      progress->inc_progress ();
		    }
		  continue;
		}
	    }
	  /* In Finlay/Paget screen the blue patches touches by borders.
	     Enforce boundaries between patches so flood fill does not overflow.
	   
	     FIXME: This does not seem to work well since blue patches are too small
	     and may get eliminated completely.  So in the following code we
	     simply try both cmaps.  */
	  if (try_paget_finlay)
	    {
	      std::unique_ptr<color_class_map> new_cmap (new color_class_map);
	      cmap = std::move (new_cmap);
	      cmap->allocate (img.width, img.height);
	      if (progress)
		progress->set_task ("pruning screen", img.height);
#pragma omp parallel for default(none) shared(progress,img,cmap,render)
	      for (int y = 0; y < img.height; y++)
		{
		  if (!progress || !progress->cancel_requested ())
		    for (int x = 0; x < img.width; x++)
		      cmap->set_class (x, y, render->classify_pixel (x, y));
		  if (progress)
		    progress->inc_progress ();
		}
	      if (progress && progress->cancel_requested ())
		{
		  if (progress)
		    progress->pop ();
		  return ret;
		}
	    }
	  if (progress)
	    progress->set_task ("Looking for initial green patch", 1);
	  if (!progress || !progress->cancel_requested ())
	    for (int y = ymin; y < ymax && !smap && nattempts < maxattempts; y++)
	      for (int x = xmin; x < xmax && !smap && nattempts < maxattempts; x++)
		{
		  if (report_file)
		    fflush (report_file);

		  enum scr_type current_type = Random;
		  color_class_map *this_cmap;
		  /* Try to guess both screen types.  If we find Paget/Finlay screen, preserve original type
		     if it makes sense, otheriwse default to Paget.  */
		  if (try_dufay && try_guess_screen (report_file, *render->get_color_class_map (), sparam, x, y, &visited, progress))
		    {
		      current_type = Dufay;
		      this_cmap = render->get_color_class_map ();
		    }
		  else if (try_paget_finlay && try_guess_paget_screen (report_file, cmap ? *cmap : *render->get_color_class_map (), sparam, x, y, &visited_paget, progress))
		    {
		      current_type = type == Finlay ? Finlay : Paget;
		      this_cmap = cmap ? cmap.get () : render->get_color_class_map ();
		    }
		  else if (try_paget_finlay && cmap && try_guess_paget_screen (report_file, *render->get_color_class_map (), sparam, x, y, &visited_paget, progress))
		    {
		      current_type = type == Finlay ? Finlay : Paget;
		      this_cmap = render->get_color_class_map ();
		    }
		  if (progress && progress->cancel_requested ())
		    {
		      if (progress)
			progress->pop ();
		      return ret;
		    }

		  if (current_type != Random)
		    {
		      nattempts++;
		      if (report_file && verbose)
			{
			  fprintf (report_file, "Initial grid found at:\n");
			  sparam.dump (report_file);
			}
		      visited.clear ();
		      param.type = current_type;
		      simple_solver (&param, img, sparam, progress);
		      smap = flood_fill (report_file, dsparams->slow_floodfill, dsparams->fast_floodfill, sparam.points[0].img.x, sparam.points[0].img.y, param, img, render.get (),
				         this_cmap, NULL /*sparam*/, &visited, &ret.patches_found, dsparams, progress);
		      if (!smap)
			{
			  if (progress)
			    {
			      progress->set_task ("Looking for initial grid", search_xsteps * search_ysteps);
			      progress->set_progress (s);
			    }
			  visited.clear ();
			  x+= 10;
			}
		      else
		        {
			  type = current_type;
			  break;
			}
		    }
		}
	  if (!smap)
	    {
	      if (progress)
		progress->pause_stdout ();
	      printf ("Start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
	      if (report_file)
		fprintf (report_file, "Start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
	      if (progress)
		progress->resume_stdout ();
	    }
	  if (progress)
	    {
	      progress->pop ();
	      progress->inc_progress ();
	    }
	}
#if 0
    int max_diam = std::max (img.width, img.height);
    for (int d = 0; d < max_diam && !smap; d++)
      {
	if (!progress || !progress->cancel_requested ())
	  for (int i = -d; i < d && !smap; i++)
	    {
	      if (try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 + d, &visited, progress)
		  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + i, img.height / 2 - d, &visited, progress)
		  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 + d, img.height / 2 + i, &visited, progress)
		  || try_guess_screen (report_file, *render.get_color_class_map (), sparam, img.width / 2 - d, img.height / 2 + i, &visited, progress))
		{
		  if (verbose)
		    {
		      if (report_file && verbose)
			fprintf (report_file, "Initial grid found at:\n");
		      sparam.dump (report_file);
		    }
		  visited.clear ();
		  simple_solver (&param, img, sparam, progress);
		  smap = flood_fill (report_file, sparam.point[0].img_x, sparam.point[0].img_y, param, img, &render, render.get_color_class_map (), NULL /*sparam*/, &visited, &ret.patches_found, progress);
		  if (!smap)
		    {
		      if (progress)
			{
			  progress->set_task ("Looking for initial grid", max_diam);
			  progress->set_progress (d);
			}
		      visited.clear ();
		    }
		  else
		    break;
		}
	    }
	if (progress)
	  progress->inc_progress ();
      }
#endif
  }
  if (!smap || (progress && progress->cancel_requested ()))
    {
      return ret;
    }
  /* Obtain more realistic solution so the range chosen for final mesh is likely right.  */
  if (progress)
    progress->set_task ("Obtaininig intitial solver points", 1);
  smap->determine_solver_points (ret.patches_found, &sparam);

  /* Determine scr-to-img parameters.
     Do perspective correction this time since this will be the final parameter produced.  */
  ret.param.type = type;
  /*ret.param.lens_center_x = img.width / 2;
  ret.param.lens_center_y = img.width / 2;*/
  //ret.param.projection_distance = img.width;
  ret.param.lens_correction = dsparams->lens_correction;
  solver (&ret.param, img, sparam, progress);
  if (progress && progress->cancel_requested ())
    {
      return ret;
    }
  summarise_quality (img, smap.get (), ret.param, "homographic", report_file, progress);
  if (progress && progress->cancel_requested ())
    {
      return ret;
    }


  if (report_file)
    {
      fprintf (report_file, "Detected geometry\n");
      save_csp (report_file, &ret.param, NULL, NULL, NULL);
    }
  {
    render_to_scr render (ret.param, img, empty, 256);
    ret.pixel_size = render.pixel_size ();
    if (report_file)
      fprintf (report_file, "pixel size: %f\n",ret.pixel_size);
  }
  smap->get_known_range (&ret.xmin, &ret.ymin, &ret.xmax, &ret.ymax);
  if (progress)
    progress->set_task ("Checking screen consistency", 1);
  int errs;
  if (type == Dufay)
    errs = smap->check_consistency (report_file, ret.param.coordinate1.x / 2, ret.param.coordinate1.y / 2, ret.param.coordinate2.x, ret.param.coordinate2.y,
				    sqrt (ret.param.coordinate1.x * ret.param.coordinate1.x + ret.param.coordinate1.y * ret.param.coordinate1.y) / 2);
  else
    errs = smap->check_consistency (report_file, (ret.param.coordinate1.x - ret.param.coordinate2.x) / 4, (ret.param.coordinate1.y - ret.param.coordinate2.y) / 4,
				    (ret.param.coordinate1.x + ret.param.coordinate2.x) / 4, (ret.param.coordinate1.y + ret.param.coordinate2.y) / 4,
				    sqrt (ret.param.coordinate1.x * ret.param.coordinate1.x + ret.param.coordinate1.y * ret.param.coordinate1.y) / 3);
  /* If we do mesh, insert fake control points to the detected screen so the binding tapes are not curly.  */
  if (dsparams->do_mesh
      && (dsparams->left || dsparams->top || dsparams->right || dsparams->bottom))
    {
      scr_to_img map;
      map.set_parameters (ret.param, img);
      const int range = 10;
      if (progress)
        progress->set_task ("Straightening corners", smap->height);
      for (int y = -smap->yshift; y < smap->height - smap->yshift; y++)
	{
	  int last_seen = INT_MAX / 2;
	  for (int x = -smap->xshift; x < smap->width - smap->xshift; x++, last_seen++)
	    if (!smap->known_p ({x, y}))
	      {
		int xrmul = 2;
		int yrmul = type != Dufay ? 2 : 1;
		bool found = last_seen < range * xrmul;
		for (int yy = std::max (y - range * yrmul, -smap->yshift); yy < std::min (smap->height - smap->yshift, y + range * yrmul) && !found; yy++)
		  for (int xx = std::max (x - range * xrmul, -smap->xshift); xx < std::min (smap->width - smap->xshift, x + range * xrmul) && !found; xx++)
		    if (smap->known_p ({xx, yy}))
		      found = true;
		if (!found)
		  {
		    point_t scrp = smap->get_screen_coord ({x, y});
		    point_t imgp = map.to_img (scrp);
	            last_seen = 0;
		    if ((imgp.x <= ret.xmin && dsparams->left) || (imgp.y < ret.ymin && dsparams->top) || (imgp.x >= ret.xmax && dsparams->right) || (imgp.y >= ret.ymax && dsparams->bottom))
		      smap->set_coord ({x, y}, imgp);
		    
		  }
		//else
		    //printf ("found %i %i\n",x,y);
	      }
	    else
	      last_seen = 0;
	  if (progress)
	    progress->inc_progress ();
	}
    }
  if (errs)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("%i inconsistent screen coordinates!\n", errs);
      if (progress)
	progress->resume_stdout ();
    }
  if (dsparams->do_mesh)
    {
      mesh *m = solver_mesh (&ret.param, img, sparam, *smap, progress);
      if (!m || (progress && progress->cancel_requested ()))
        {
	  delete m;
	  return ret;
        }
      const int xsteps = 50, ysteps = 50;
      m->precompute_inverse ();
      /* Now produce output (regular) grid of solver points.
         This can be used to re-compute the mesh from GUI  */
      if (progress)
	progress->set_task ("Determinig solver points", 1);
      sparam.remove_points ();
      for (int y = 0; y < ysteps; y ++)
	for (int x = 0; x < xsteps; x ++)
	  {
	    int border = 1;
	    point_t p = m->invert ({(x + border) * img.width / (coord_t)(xsteps + border),
				    (y + border) * img.height / (coord_t)(ysteps + border)});
	    p.x = nearest_int (p.x);
	    p.y = nearest_int (p.y);
	    point_t imgp = m->apply (p);
	    if (sparam.find_img (imgp) < 0)
	      sparam.add_point (imgp, p, solver_parameters::green);
	  }
      ret.mesh_trans = m;
      ret.param.mesh_trans = m;
      summarise_quality (img, smap.get (), ret.param, "mesh", report_file, progress);
      ret.param.mesh_trans = NULL;
    }
  else
    ret.mesh_trans = NULL;


  /* Known patches is a bitmap in screen coordinates that is set of 1 if any patches
     belonging to a given screen coordinate was found.  */
  if (dsparams->return_known_patches)
    {
      if (progress)
	progress->set_task ("Computing known patches", 1);
      /* TODO: test that Dufay path can be replaced by generic one.  */
      if (type == Dufay)
	{
	  ret.xshift = smap->xshift / 2;
	  ret.yshift = smap->yshift;
	  ret.known_patches = new bitmap_2d (smap->width / 2, smap->height);
	  for (int y = 0; y < smap->height; y ++)
	    for (int x = 0; x < smap->width / 2; x ++)
	      if (smap->known_p ({x * 2 - ret.xshift * 2, y - ret.yshift})
		  && smap->known_p ({x * 2 - ret.xshift * 2 + 1, y - ret.yshift}))
		ret.known_patches->set_bit (x, y);
	}
      else
	{
	  int xmin = INT_MAX, xmax = INT_MIN, ymin = INT_MAX, ymax = INT_MIN;
	  for (int y = 0; y < smap->height; y ++)
	    for (int x = 0; x < smap->width; x ++)
	       if (smap->known_p ({x - smap->xshift, y - smap->yshift}))
		 {
		   point_t scrp = smap->get_screen_coord ({x - smap->xshift, y - smap->yshift});
		   xmin = std::min (xmin, (int)scrp.x);
		   xmax = std::max (xmax, (int)scrp.x);
		   ymin = std::min (ymin, (int)scrp.y);
		   ymax = std::max (ymax, (int)scrp.y);
		 }
	  ret.xshift = -xmin;
	  ret.yshift = -ymin;
	  ret.known_patches = new bitmap_2d (xmax - xmin + 1, ymax - ymin + 1);
	  for (int y = 0; y < smap->height; y ++)
	    for (int x = 0; x < smap->width; x ++)
	       if (smap->known_p ({x - smap->xshift, y - smap->yshift}))
		 {
		   point_t scr = smap->get_screen_coord ({x - smap->xshift, y - smap->yshift});
		   /* TODO: perhaps we should be conservative here and require all entries
		      to be set.  But most likely this makes no difference.  */
		   ret.known_patches->set_bit ((int)scr.x + ret.xshift, (int)scr.y + ret.yshift);
		 }
	}
    }
  if (progress)
    progress->pause_stdout ();
  if (report_file)
    fprintf (report_file, "Unalanyzed border left: %f%%, right %f%%, top %f%%, bottom %f%%\n", ret.xmin * 100.0 / img.width, 100 - ret.xmax * 100.0 / img.width, ret.ymin * 100.0 / img.height, 100 - ret.ymax * 100.0 / img.height);
  if (progress)
    progress->resume_stdout ();
  
  if (dsparams->return_screen_map)
    ret.smap = smap.release ();
  ret.success = true;
  return ret;
}
}


detected_screen
detect_regular_screen (image_data &img, enum scr_type type, scr_detect_parameters &dparam, luminosity_t gamma, solver_parameters &sparam, detect_regular_screen_params *dsparams, progress_info *progress, FILE *report_file)
{
  //dsparams->slow_floodfill = false;
  //dsparams->fast_floodfill = true;
  //dsparams->optimize_colors = false;
  detected_screen ret = detect_regular_screen_1 (img, type, dparam, gamma, sparam, dsparams, progress, report_file);
  prune_render_scr_detect_caches ();
  return ret;
}
}
