#include "include/scr-detect.h"
#include "include/solver.h"
#include "include/render-scr-detect.h"
#include "screen-map.h"
#include "include/bitmap.h"
#include "include/render-to-scr.h"
#include "include/solver.h"
namespace
{
  const bool verbose = false;

struct patch_entry
{
	int x, y;
};


/* Lookup patch of a given color, coordinates and maximal size.  Return number of vertices in patch.  */
int
find_patch (color_class_map &color_map, scr_detect::color_class c, int x, int y, int max_patch_size, patch_entry *entries, bitmap_2d *visited)
{
  if (x < 0 || y < 0 || x >= color_map.width || y >= color_map.height)
    return 0;
  if (visited && visited->set_bit (x, y))
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
		  if (visited->set_bit (xx, yy))
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
    {
      if ((int)*x == entries[i].x && (int)*y == entries[i].y)
	return true;
    }
  return false;
}

bool
confirm_strip (color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, bitmap_2d *visited)
{
  patch_entry entries[min_patch_size + 1];
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), min_patch_size + 1, entries, visited);
  /* Since strips are not isolated do not mark them as visited so we do not block walk from other spot.  */
  if (visited)
    for (int i = 0; i < size; i++)
      visited->clear_bit (entries[i].x, entries[i].y);
  //if (verbose)
    //printf ("size: %i coord: %f %f color %i\n", size,x,y, (int)c);
  if (size < min_patch_size)
    return false;
  return true;
}

bool
try_guess_screen (FILE *report_file, color_class_map &color_map, solver_parameters &sparam, int x, int y, bitmap_2d *visited, progress_info *progress)
{
  const int max_size = 200;
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
  if (!patch_center (entries, size, &rbpatches[0][0].x, &rbpatches[0][0].y))
    return false;
  if (report_file && verbose)
    fprintf (report_file, "Trying to start search at %i %i with initial green patch of size %i and center %f %f\n", x, y, size, rbpatches[0][0].x, rbpatches[0][0].y);

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
	    patch_found = patch_center (entries, size, &rbpatches[0][1].x, &rbpatches[0][1].y);
	  }
	else if (t != scr_detect::unknown)
	  break;
      }
  }

  if (!patch_found)
  {
    if (report_file && verbose)
      fprintf (report_file, "Blue patch not found\n");
    return false;
  }
  coord_t patch_stepx = rbpatches[0][1].x - rbpatches[0][0].x;
  coord_t patch_stepy = rbpatches[0][1].y - rbpatches[0][0].y;
  if (report_file && verbose)
    fprintf (report_file, "found blue patch of size %i and center %f %f guessing patch distance %f %f\n", size, rbpatches[0][0].x, rbpatches[0][0].y, patch_stepx, patch_stepy);
  for (int p = 2; p < npatches * 2; p++)
    {
      int nx = rbpatches[0][p - 1].x + patch_stepx;
      int ny = rbpatches[0][p - 1].y + patch_stepy;
      size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, NULL);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Failed to guess patch 0, %i with steps %f %f\n", p, patch_stepx, patch_stepy);
	  return 0;
	}
      if (!patch_center (entries, size, &rbpatches[0][p].x, &rbpatches[0][p].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Center of patch 0, %i is not inside\n", p);
	  return 0;
	}
      patch_stepx = (rbpatches[0][p].x - rbpatches[0][0].x) / p;
      patch_stepy = (rbpatches[0][p].y - rbpatches[0][0].y) / p;
    }
  if (report_file && verbose)
   fprintf (report_file, "Confirmed %i patches in alternating direction with distances %f %f\n", npatches, patch_stepx, patch_stepy);
  for (int r = 1; r < npatches; r++)
    {
      int rx = rbpatches[r - 1][0].x - patch_stepy;
      int ry = rbpatches[r - 1][0].y + patch_stepx;
      int nx = rbpatches[r - 1][0].x - 2*patch_stepy;
      int ny = rbpatches[r - 1][0].y + 2*patch_stepx;
      if (!confirm_strip (&color_map, rx, ry, scr_detect::red, 1, NULL))
	 {
	  if (report_file && verbose)
	    fprintf (report_file, "Failed to confirm red on way to %i,%i with steps %f %f\n", r, 0, patch_stepx, patch_stepy);
	  return 0;
	 }
      size = find_patch (color_map, scr_detect::green, nx, ny, max_size, entries, NULL);
      if (size == 0 || size == max_size)
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Failed to guess patch %i,%i with steps %f %f\n", r, 0, patch_stepx, patch_stepy);
	  return 0;
	}
      if (!patch_center (entries, size, &rbpatches[r][0].x, &rbpatches[r][0].y))
	{
	  if (report_file && verbose)
	    fprintf (report_file, "Center of patch %i,%i is not inside\n", r, 0);
	  return 0;
	}
      for (int p = 1; p < npatches * 2; p++)
	{
	  int nx = rbpatches[r][p - 1].x + patch_stepx;
	  int ny = rbpatches[r][p - 1].y + patch_stepy;
	  size = find_patch (color_map, (p & 1) ? scr_detect::blue : scr_detect::green, nx, ny, max_size, entries, NULL);
	  if (size == 0 || size == max_size)
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Failed to guess patch %i,%i with steps %f %f\n", r, p, patch_stepx, patch_stepy);
	      return 0;
	    }
	  if (!patch_center (entries, size, &rbpatches[r][p].x, &rbpatches[r][p].y))
	    {
	      if (report_file && verbose)
		fprintf (report_file, "Center of patch %i,%i is not inside\n", r, p);
	      return 0;
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
  sparam.remove_points ();
  for (int r = 0; r < npatches; r++)
    for (int p = 0; p < npatches * 2; p++)
      sparam.add_point (rbpatches[r][p].x, rbpatches[r][p].y, p / 2.0, r, (p & 1) ? solver_parameters::blue : solver_parameters::green);
  return true;
}

bool
confirm_patch (FILE *report_file, color_class_map *color_map,
	       coord_t x, coord_t y, scr_detect::color_class c,
	       int min_patch_size, int max_patch_size, coord_t max_distance,
	       coord_t *cx, coord_t *cy, bitmap_2d *visited)
{
  patch_entry entries[max_patch_size + 1];
  const char *fail = NULL;
  int size = find_patch (*color_map, c, (int)(x + 0.5), (int)(y + 0.5), max_patch_size + 1, entries, visited);
  if (size < min_patch_size || size > max_patch_size)
    fail = "rejected: bad size";
  else if (!patch_center (entries, size, cx, cy))
    fail = "rejected: center not in patch";
  else if ((*cx - x) * (*cx - x) + (*cy - y) * (*cy - y) > max_distance * max_distance)
    fail = "rejected: distance out of tolerance";
  if (report_file && fail && verbose)
    fprintf (report_file, "size: %i (expecting %i...%i) coord: %f %f center %f %f color %i%s\n", size, min_patch_size, max_patch_size,x,y, *cx, *cy, (int)c, fail ? fail : "");
  //printf ("center %f %f\n", *cx, *cy);
  return !fail;
}

#define N_PRIORITIES 4

bool
confirm (render_scr_detect *render,
	 coord_t coordinate1_x, coord_t coordinate1_y,
	 coord_t coordinate2_x, coord_t coordinate2_y,
	 coord_t x, coord_t y, scr_detect::color_class t,
	 int width, int height,
	 coord_t max_distance,
	 coord_t *rcx, coord_t *rcy, int *priority, bool strip)
{
  coord_t bestcy = x, bestcx = y, minsum = 0, bestinner = 0, bestouter = 0;
  const int sample_steps = 2;
  const coord_t pixel_step = 0.1;
  bool found = false;
  int xmin = ceil (std::min (std::min (coordinate1_x / 2, coordinate2_x / 2), std::min (-coordinate1_x / 2, -coordinate2_x / 2)));
  int xmax = ceil (std::max (std::max (coordinate1_x / 2, coordinate2_x / 2), std::max (-coordinate1_x / 2, -coordinate2_x / 2)));
  int ymin = ceil (std::min (std::min (coordinate1_y / 2, coordinate2_y / 2), std::min (-coordinate1_y / 2, -coordinate2_y / 2)));
  int ymax = ceil (std::max (std::max (coordinate1_y / 2, coordinate2_y / 2), std::max (-coordinate1_y / 2, -coordinate2_y / 2)));

  /* Do not try to search towards end of screen since it gives wrong resutls.
     broder 4x4 is necessary for interpolation.  */
  if (y - max_distance /4 + ymin - 4 < 0
      || y + max_distance /4 + ymax + 4 >= height
      || x - max_distance /4 + xmin - 4 < 0
      || x + max_distance /4 + xmax + 3 >= width)
    return false;

  if (!strip)
#if 0
    {
      coord_t cx = x;
      coord_t cy = y;
      for (int i = 0; i < 5; i++)
	{
	  coord_t xsum = 0, xavg = 0;
	  coord_t ysum = 0, yavg =0;
	  switch ((int) t)
	    {
	      case 0:
	      for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		  {
		    luminosity_t color[3];
		    render->fast_precomputed_get_adjusted_pixel (xx, yy, &color[0], &color[1], &color[2]);
		    //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		    //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		    xsum += color[t];
		    ysum += color[t];
		    xavg += color[t] * (xx + 0.5 - cx);
		    yavg += color[t] * (yy + 0.5 - cy);
		  }
	      break;
	      case 1:
	      for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		  {
		    luminosity_t color[3];
		    render->fast_precomputed_get_adjusted_pixel (xx, yy, &color[0], &color[1], &color[2]);
		    //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		    //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		    xsum += color[t];
		    ysum += color[t];
		    xavg += color[t] * (xx + 0.5 - cx);
		    yavg += color[t] * (yy + 0.5 - cy);
		  }
	      break;
	      case 2:
	      for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		  {
		    luminosity_t color[3];
		    render->fast_precomputed_get_adjusted_pixel (xx, yy, &color[0], &color[1], &color[2]);
		    //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		    //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		    xsum += color[t];
		    ysum += color[t];
		    xavg += color[t] * (xx + 0.5 - cx);
		    yavg += color[t] * (yy + 0.5 - cy);
		  }
	      break;
	      default:
	      abort ();
	    }
	  coord_t xadj = xavg / xsum / 2;
	  coord_t yadj = yavg / ysum / 2;
	  coord_t sum = xadj * xadj + yadj * yadj;
	  printf ("%i:sum %f %f %f %f %f %f\n",i,sum,xavg,yavg,xsum,ysum,xavg/xsum, yavg/ysum);
	  cx += xavg / xsum / 2;
	  cy += yavg / ysum / 2;
	  if (sum < 0.1)
	    break;
	}
      bestcx = cx;
      bestcy = cy;
      if ((cx - x) * (cx -x) + (cy - y) * (cy - y) > max_distance / 2)
	return false;
    }
#else
    {
      //printf ("%i %i %i %i\n",xmin,xmax,ymin,ymax);
      for (coord_t cy = std::max (y - max_distance / 4, (coord_t)-ymin); cy <= std::min (y + max_distance / 4, (coord_t)height - ymax); cy+= pixel_step)
	for (coord_t cx = std::max (x - max_distance / 4, (coord_t)-xmin); cx <= std::min (x + max_distance / 4, (coord_t)width - xmax); cx+= pixel_step)
	  {
	    coord_t xsum = 0;
	    coord_t ysum = 0;
	    switch ((int) t)
	      {
		case 0:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx);
		      ysum += color[t] * (yy + 0.5 - cy);
		    }
		break;
		case 1:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx);
		      ysum += color[t] * (yy + 0.5 - cy);
		    }
		break;
		case 2:
		for (int yy = floor (cy + ymin) ; yy < ceil (cy + ymax); yy++)
		  for (int xx = floor (cx + xmin) ; xx < ceil (cx + xmax); xx++)
		    {
		      rgbdata color = render->fast_precomputed_get_adjusted_pixel (xx, yy);
		      //xsum += std::max (color[t], (luminosity_t)0) * (xx + 0.5 - cx);
		      //ysum += std::max (color[t], (luminosity_t)0) * (yy + 0.5 - cy);
		      xsum += color[t] * (xx + 0.5 - cx);
		      ysum += color[t] * (yy + 0.5 - cy);
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
    }
#endif
  else
  {
    bestcx = x;
    bestcy = y;
  }
  //int nouter = 0, ninner = 0;
  luminosity_t min = 0;
  for (int yy = -sample_steps; yy <= sample_steps; yy++)
    for (int xx = -sample_steps; xx <= sample_steps; xx++)
      {
	luminosity_t color[3];
	render->get_adjusted_pixel (bestcx + (xx * ( 1 / ((coord_t)sample_steps * 2))) * coordinate1_x + (yy * (1 / ((coord_t)sample_steps * 2))) * coordinate1_y,
				    bestcy + (xx * ( 1 / ((coord_t)sample_steps * 2))) * coordinate2_x + (yy * (1 / ((coord_t)sample_steps * 2))) * coordinate2_y,
				    &color[0], &color[1], &color[2]);
	luminosity_t sum = color[0]+color[1]+color[2];
	color[0] = std::max (color[0], (luminosity_t)0);
	color[1] = std::max (color[1], (luminosity_t)0);
	color[2] = std::max (color[2], (luminosity_t)0);
	sum = std::max (sum, (luminosity_t)0.0001);
	//sum=1;
	min = std::min (color[t] / sum, min);
	if (/*sum > 0 && color[t] > 0*/1)
	  {
	    if ((!strip && (xx == -sample_steps || xx == sample_steps)) || yy == -sample_steps || yy == sample_steps)
	      bestouter += color[t] / sum;// nouter++;
	    else
	      bestinner += color[t] / sum;// ninner++;
	  }
      }

  //if (bestinner <= 0)
    //return false;
#if 0
    for (coord_t cy = std::max (y - max_distance / 4, (coord_t)0); cy <= std::min (y + max_distance / 4, (coord_t)height); cy+= pixel_step)
      for (coord_t cx = std::max (x - max_distance / 4, (coord_t)0); cx <= std::min (x + max_distance / 4, (coord_t)width); cx+= pixel_step)
	{
	  coord_t xsum = 0;
	  coord_t ysum = 0;
	  coord_t inner = 0;
	  coord_t outer = 0;
	  for (int yy = -sample_steps; yy <= sample_steps; yy++)
	    for (int xx = -sample_steps; xx <= sample_steps; xx++)
	      {
		luminosity_t color[3];
		render->get_adjusted_pixel (cx + (xx * ( 1 / ((coord_t)sample_steps * 2))) * coordinate1_x + (yy * (1 / ((coord_t)sample_steps * 2))) * coordinate1_y,
					    cy + (xx * ( 1 / ((coord_t)sample_steps * 2))) * coordinate2_x + (yy * (1 / ((coord_t)sample_steps * 2))) * coordinate2_y,
					    &color[0], &color[1], &color[2]);
		xsum += std::max (color[t], (luminosity_t)0) * xx;
		ysum += std::max (color[t], (luminosity_t)0) * yy;
		luminosity_t sum = color[0]+color[1]+color[2];
		if (sum > 0 && color[t] > 0)
		  {
		    if ((!strip && (xx == -sample_steps || xx == sample_steps)) || yy == -sample_steps || yy == sample_steps)
		      outer += color[t] / sum;
		    else
		      inner += color[t] / sum;
		  }
	      }
	   coord_t sum = xsum * xsum + ysum * ysum;
	   //printf ("%f %f: %f %f %f\n", cx-x, cy-y, xsum, ysum, sum);
	   if (!found || minsum > sum)
	     {
	       bestcx = cx;
	     bestcy = cy;
	     bestinner = inner;
	     bestouter = outer;
	     minsum = sum;
	     found = true;
	   }
      }
#endif
  *rcx = bestcx;
  *rcy = bestcy;
  /*  For sample_steps == 2:
      O O O O O
      O I I I O
      O I I I O
      O I I I O
      O O O O O  */
  if (!strip)
    {
      int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
      int nouter = 2 * (2 * sample_steps + 1) + 2*(2*sample_steps-1);
      bestinner -= min * ninner;
      bestouter -= min * nouter;
      bestinner *= (1 / (luminosity_t) ninner);
      bestouter *= (1 / (luminosity_t) nouter);
    }
  /*  For sample_steps == 2:
      O O O O O
      I I I I I
      I I I I I
      I I I I I
      O O O O O  */
  else
    {
      int ninner = (2 * sample_steps - 1) * (2 * sample_steps - 1);
      int nouter = 2 * (2 * sample_steps + 1) + 2*(2*sample_steps-1);
      bestinner -= min * ninner;
      bestouter -= min * nouter;
      bestinner *= (1 / (luminosity_t) ninner);
      bestouter *= (1 / (luminosity_t) nouter);
    }
  coord_t dist = (bestcx - x) * (bestcx - x) + (bestcy - y) * (bestcy - y);
  if (bestinner <= 0 || bestinner < bestouter * 2)
    {
      //printf ("FAILED: given:%f %f best:%f %f inner:%f outer:%f color:%i min:%i\n", x, y, bestcx-x, bestcy-y, bestinner, bestouter, (int) t, min);
      return false;
    }
  else if (bestinner > bestouter * 8 && dist < max_distance * max_distance / 128)
    *priority = 3;
  else if (bestinner > bestouter * 4 && dist < max_distance * max_distance / 32)
    *priority = 2;
  else if (bestinner > bestouter * 3)
    *priority = 1;
  else
    *priority = 0;
  //printf ("given:%f %f best:%f %f inner:%f outer:%f priority:%i color:%i\n", x, y, bestcx-x, bestcy-y, bestinner, bestouter, (int) t, *priority);
  return true;
}

template<int N, typename T>
class priority_queue
{
public:
  const int npriorities = N;
  std::vector<T> queue[N];
  void
  insert (T e, int priority)
  {
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
};

screen_map *
flood_fill (FILE *report_file, bool slow, coord_t greenx, coord_t greeny, scr_to_img_parameters &param, image_data &img, render_scr_detect *render, color_class_map *color_map, solver_parameters *sparam, bitmap_2d *visited, int *npatches, detect_regular_screen_params *dsparams, progress_info *progress)
{
  double screen_xsize = sqrt (param.coordinate1_x * param.coordinate1_x + param.coordinate1_y * param.coordinate1_y);
  double screen_ysize = sqrt (param.coordinate2_x * param.coordinate2_x + param.coordinate2_y * param.coordinate2_y);

  /* If screen is estimated too small or too large give up.  */
  if (screen_xsize < 2 || screen_ysize < 2 || screen_xsize > 100 || screen_ysize > 100)
    return NULL;

  /* Do not flip the image.  */
  if (param.coordinate1_y < 0)
    return NULL;

  scr_to_img scr_map;
  scr_map.set_parameters (param, img);

  int max_patch_size = floor (screen_xsize * screen_ysize / 1.5);
  int min_patch_size = std::max ((int)(screen_xsize * screen_ysize / 5), 1);
  coord_t max_distance = (screen_xsize + screen_ysize) * 0.1;
  int nfound = 1;

  int xshift, yshift, width, height;
  scr_map.get_range (img.width, img.height, &xshift, &yshift, &width, &height);
  int nexpected = 2 * img.width * img.height / (screen_xsize * screen_ysize);
  if (report_file)
    fprintf (report_file, "Flood fill started with coordinates %f,%f and %f,%f\n", param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y);
  if (progress)
    progress->set_task ("Flood fill", nexpected);
  screen_map *map = new screen_map(xshift * 2, yshift, width * 2, height);

  struct queue_entry
  {
    int scr_xm2, scr_y;
    coord_t img_x, img_y;
  };
  priority_queue<N_PRIORITIES,queue_entry> queue;
  queue.insert ((struct queue_entry){0, 0, greenx, greeny}, 0);
  map->set_coord (0, 0, greenx, greeny);
  if (sparam)
    sparam->remove_points ();
  //printf ("%i %i %f %f %f %f\n", queue.size (), map.in_range_p (0, 0), param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y);
  queue_entry e;
  while (queue.extract_min (e) /*&& nfound < 500*/)
    {
      coord_t ix, iy;
      int priority = 0;
      int priority2 = 0;
      //if (verbose)
        //printf ("visiting %i %i %f %f %f %f\n", e.scr_xm2, e.scr_y, e.img_x, e.img_y, param.coordinate1_x, param.coordinate1_y);
      if (progress)
        progress->inc_progress ();
      if (sparam)
	sparam->add_point (e.img_x, e.img_y, e.scr_xm2 / 2.0, e.scr_y, e.scr_y ? solver_parameters::blue : solver_parameters::green);


#define cpatch(x,y,t, priority) (!slow ? confirm_patch (report_file, color_map, x, y, t, min_patch_size, max_patch_size, max_distance, &ix, &iy, visited) \
				 : confirm (render, param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, false))
#define cstrip(x,y,t, priority) (!slow ? confirm_strip (color_map, x, y, t, min_patch_size, visited) \
				 : confirm (render, param.coordinate1_x, param.coordinate1_y, param.coordinate2_x, param.coordinate2_y, x, y, t, color_map->width, color_map->height, max_distance, &ix, &iy, &priority, true))
      if (!map->known_p (e.scr_xm2 - 1, e.scr_y)
	  && cpatch (e.img_x - param.coordinate1_x / 2, e.img_y - param.coordinate1_y / 2, ((e.scr_xm2 - 1) & 1) ? scr_detect::blue : scr_detect::green, priority))
	{
	  map->safe_set_coord (e.scr_xm2 - 1, e.scr_y, ix, iy);
	  queue.insert ((struct queue_entry){e.scr_xm2 - 1, e.scr_y, ix, iy}, priority);
	  nfound++;
	}
      if (!map->known_p (e.scr_xm2 + 1, e.scr_y)
	  && cpatch (e.img_x + param.coordinate1_x / 2, e.img_y + param.coordinate1_y / 2, ((e.scr_xm2 + 1) & 1) ? scr_detect::blue : scr_detect::green, priority))
	{
	  map->safe_set_coord (e.scr_xm2 + 1, e.scr_y, ix, iy);
	  queue.insert ((struct queue_entry){e.scr_xm2 + 1, e.scr_y, ix, iy}, priority);
	  nfound++;
	}
      if (!map->known_p (e.scr_xm2, e.scr_y - 1)
	  && cstrip (e.img_x - param.coordinate2_x / 2, e.img_y - param.coordinate2_y / 2, scr_detect::red, priority)
	  && cpatch (e.img_x - param.coordinate2_x, e.img_y - param.coordinate2_y, (e.scr_xm2 & 1) ? scr_detect::blue : scr_detect::green, priority2))
	{
	  map->safe_set_coord (e.scr_xm2, e.scr_y - 1, ix, iy);
	  queue.insert ((struct queue_entry){e.scr_xm2, e.scr_y - 1, ix, iy}, std::min (priority, priority2));
	  nfound++;
	}
      if (!map->known_p (e.scr_xm2, e.scr_y + 1)
	  && cstrip (e.img_x + param.coordinate2_x / 2, e.img_y + param.coordinate2_y / 2, scr_detect::red, priority)
	  && cpatch (e.img_x + param.coordinate2_x, e.img_y + param.coordinate2_y, (e.scr_xm2 & 1) ? scr_detect::blue : scr_detect::green, priority2))
	{
	  map->safe_set_coord (e.scr_xm2, e.scr_y + 1, ix, iy);
	  queue.insert ((struct queue_entry){e.scr_xm2, e.scr_y + 1, ix, iy}, std::min (priority, priority2));
	  nfound++;
	}
#undef cpatch
#undef cstrip
    }
  /* Dufay screen has two points per screen repetetion.  */
  *npatches = nfound;
  int xmin, ymin, xmax, ymax;
  map->get_known_range (&xmin, &ymin, &xmax, &ymax);
  nexpected = 2 * (xmax - xmin) * (ymax - ymin) / (screen_xsize * screen_ysize);
  if (nexpected * dsparams->min_screen_percentage > nfound * 100)
    {
      if (report_file)
	{
	  fprintf (report_file, "Detected screen patches covers only %2.2f%% of the screen\n", nfound * 100.0 / nexpected);
	  //fprintf (report_file, "Reducing --min-screen-percentage would bypass this error\n");
	}
      delete map;
      return NULL;
    }
  progress->pause_stdout ();
  printf ("Analyzed %2.2f%% of the screen area", nfound * 100.0 / nexpected);
  if (report_file)
    fprintf (report_file, "Analyzed %2.2f%% of the screen area", nfound * 100.0 / nexpected);
  printf ("; left border: %2.2f%%", xmin * 100.0 / img.width);
  if (report_file)
    fprintf (report_file, "; left border: %2.2f%%", xmin * 100.0 / img.width);
  printf ("; top border: %2.2f%%", ymin * 100.0 / img.height);
  if (report_file)
    fprintf (report_file, "; top border: %2.2f%%", ymin * 100.0 / img.height);
  printf ("; right border: %2.2f%%", 100 - xmax * 100.0 / img.width);
  if (report_file)
    fprintf (report_file, "; right border: %2.2f%%", 100 - xmax * 100.0 / img.width);
  printf ("; bottom border: %2.2f%%", 100 - ymax * 100.0 / img.height);
  if (report_file)
    fprintf (report_file, "; bottom border: %2.2f%%", 100 - ymax * 100.0 / img.height);
  printf ("\n");
  if (report_file)
    fprintf (report_file, "\n");
  progress->resume_stdout ();
  if (xmin > std::max (dsparams->border_left, (coord_t)2) * img.width / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach left border of the image\n");
      delete map;
      return NULL;
    }
  if (ymin > std::max (dsparams->border_top, (coord_t)2) * img.height / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach top border of the image\n");
      delete map;
      return NULL;
    }
  if (xmax < std::min (100 - dsparams->border_right, (coord_t)98) * img.width / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach right border of the image\n");
      delete map;
      return NULL;
    }
  if (ymax < std::min (100 - dsparams->border_bottom, (coord_t)98) * img.height / 100)
    {
      if (report_file)
	fprintf (report_file, "Detected screen failed to reach bottom border of the image\n");
      delete map;
      return NULL;
    }
  return map;
}
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

detected_screen
detect_regular_screen (image_data &img, scr_detect_parameters &dparam, luminosity_t gamma, solver_parameters &sparam, detect_regular_screen_params *dsparams, progress_info *progress, FILE *report_file)
{
  detected_screen ret;
  render_parameters empty;

  empty.gamma = gamma;
  ret.mesh_trans = NULL;
  ret.known_patches = NULL;
  ret.smap = NULL;
  render_scr_detect *render = NULL;
  bitmap_2d visited (img.width, img.height);
  scr_to_img_parameters param;
  screen_map *smap = NULL;
  param.type = Dufay;

  const int search_xsteps = 6;
  const int search_ysteps = 6;
  if (progress)
    progress->set_task ("Looking for initial grid", search_xsteps * search_ysteps);
  auto points = check_points (search_xsteps, search_ysteps);
  for (int s = 0; s < (int)points.size () && !smap; s++)
    {
      int xmin = points[s].x * img.width / search_xsteps;
      int ymin = points[s].y * img.height / search_ysteps;
      int xmax = (points[s].x + 1) * img.width / search_xsteps;
      int ymax = (points[s].y + 1) * img.height / search_ysteps;
      int nattempts = 0;
      const int  maxattempts = 10;
      if (dsparams->optimize_colors)
	{
	  if (!optimize_screen_colors (&dparam, &img, gamma, xmin, ymin, std::min (1000, xmax - xmin), std::min (1000, ymax - ymin), progress, report_file))
	    {
	      if (progress)
		progress->pause_stdout ();
	      printf ("Failed to analyze colors on start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
	      if (report_file)
	      {
	        fprintf (report_file, "Failed to analyze colors on start coordinates %i,%i (translated %i,%i) failed (%i out of %i attempts)\n", points[s].x, points[s].y, xmax, ymax, s + 1, (int)points.size ());
		fflush (report_file);
	      }
	      if (progress)
		progress->resume_stdout ();
	      continue;
	    }
	  /* Re-detect screen.  */
	  delete render;
	  render = NULL;
	}
      if (!render)
	{
	  render = new render_scr_detect (dparam, img, empty, 256);
	  render->precompute_all (false, progress);
	  render->precompute_rgbdata (progress);
	}
      if (!progress || !progress->cancel_requested ())
	for (int y = ymin; y < ymax && !smap && nattempts < maxattempts; y++)
	  for (int x = xmin; x < xmax && !smap && nattempts < maxattempts; x++)
	    {
	      if (try_guess_screen (report_file, *render->get_color_class_map (), sparam, x, y, &visited, progress))
		{
		  nattempts++;
		  if (verbose)
		    {
		      if (report_file && verbose)
			fprintf (report_file, "Initial grid found at:\n");
		      sparam.dump (report_file);
		    }
		  visited.clear ();
		  simple_solver (&param, img, sparam, progress);
		  smap = flood_fill (report_file, dsparams->slow_floodfill, sparam.point[0].img_x, sparam.point[0].img_y, param, img, render, render->get_color_class_map (), NULL /*sparam*/, &visited, &ret.patches_found, dsparams, progress);
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
		    break;
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
  if (!smap)
    {
      delete render;
      return ret;
    }
  /* Obtain more realistic solution so the range chosen for final mesh is likely right.  */
  sparam.remove_points ();

  for (int y = -smap->yshift, nf = 0, next =0, step = ret.patches_found / 1000; y < smap->height - smap->yshift; y ++)
    for (int x = -smap->xshift; x < smap->width - smap->xshift; x ++)
      if (smap->known_p (x,y) && nf++ > next)
	{
	  next += step;
	  coord_t ix, iy;
	  smap->get_coord (x, y, &ix, &iy);
	  sparam.add_point (ix, iy, x / 2.0, y, x & 1 ? solver_parameters::blue : solver_parameters::green);
	}

  ret.param.type = Dufay;
  simple_solver (&ret.param, img, sparam, progress);
  if (report_file)
    {
      fprintf (report_file, "Detected geometry\n");
      save_csp (report_file, &param, NULL, NULL, NULL);
    }
  {
    render_to_scr render (ret.param, img, empty, 256);
    ret.pixel_size = render.pixel_size ();
    if (report_file)
      fprintf (report_file, "pixel size: %f\n",ret.pixel_size);
  }
  smap->get_known_range (&ret.xmin, &ret.ymin, &ret.xmax, &ret.ymax);
#if 1
  {
    scr_to_img map;
    map.set_parameters (ret.param, img);
    for (int y = -smap->yshift; y < smap->height - smap->yshift; y ++)
      for (int x = -smap->xshift; x < smap->width - smap->xshift; x ++)
        if (!smap->known_p (x, y))
	  {
	    bool found = false;
	    for (int yy = std::max (y - 10, -smap->yshift); yy < std::min (smap->height - smap->yshift, y + 10) && !found; yy++)
	      for (int xx = std::max (x - 10, -smap->xshift); xx < std::min (smap->width - smap->xshift, x + 10) && !found; xx++)
		if (smap->known_p (xx, yy))
		  found = true;
	    if (!found)
	      {
		coord_t ix, iy;
		map.to_img (x / 2.0, y, &ix, &iy);
		smap->set_coord (x, y, ix, iy);
	      }
	    //else
		//printf ("found %i %i\n",x,y);
	  }
  }
#endif
  int errs = smap->check_consistency (report_file, ret.param.coordinate1_x, ret.param.coordinate1_y, ret.param.coordinate2_x, ret.param.coordinate2_y,
				      sqrt (ret.param.coordinate1_x * ret.param.coordinate1_x + ret.param.coordinate1_y * ret.param.coordinate1_y) / 2);
  if (errs)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("%i inconsistent screen coordinates!\n", errs);
      if (progress)
	progress->resume_stdout ();
    }
  mesh *m = solver_mesh (&ret.param, img, sparam, *smap, progress);

  const int xsteps = 50, ysteps = 50;
  m->precompute_inverse ();
  if (progress)
    progress->set_task ("Determinig solver points", 1);
  sparam.remove_points ();
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


  if (dsparams->return_known_patches)
    {
      ret.xshift = smap->xshift / 2;
      ret.yshift = smap->yshift;
      ret.known_patches = new bitmap_2d (smap->width / 2, smap->height);
      for (int y = 0; y < smap->height; y ++)
	for (int x = 0; x < smap->width / 2; x ++)
	  if (smap->known_p (x * 2 - ret.xshift * 2, y - ret.yshift)
	      && smap->known_p (x * 2 - ret.xshift * 2 + 1, y - ret.yshift))
	    ret.known_patches->set_bit (x, y);
    }
  if (progress)
    progress->pause_stdout ();
  if (report_file)
    fprintf (report_file, "Unalanyzed border left: %f%%, right %f%%, top %f%%, bottom %f%%\n", ret.xmin * 100.0 / img.width, 100 - ret.xmax * 100.0 / img.width, ret.ymin * 100.0 / img.height, 100 - ret.ymax * 100.0 / img.height);
  if (progress)
    progress->resume_stdout ();
  
  delete render;
  delete smap;
  ret.mesh_trans = m;
  return ret;
}
