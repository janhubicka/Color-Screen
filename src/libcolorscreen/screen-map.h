#ifndef SCREEN_MAP_H
#define SCREEN_MAP_H
#include <limits>
#include "include/solver.h"
class
screen_map
{
public:
  screen_map (int xshift1, int yshift1, int width1, int height1)
  : width (width1), height (height1), xshift (xshift1), yshift (yshift1)
  {
    map = (coord_entry *)calloc (width * height, sizeof (coord_entry));
  }
  ~screen_map ()
  {
    free (map);
  }
  bool in_range_p (int x, int y)
  {
    x += xshift;
    y += yshift;
    return (x >= 0 && x < width && y >= 0 && y < height);
  }
  bool known_p (int x, int y)
  {
    if (!in_range_p (x, y))
      return false;
    x += xshift;
    y += yshift;
    //printf ("Check %i %i: %i\n",x,y, map[y * width + x].x != 0);
    return (map[y * width + x].x != 0);
  }
  void set_coord (int xx, int yy, coord_t img_x, coord_t img_y)
  {
    int x = xx + xshift;
    int y = yy + yshift;
    if (img_x == 0)
      img_x = 0.00001;
    if (img_y == 0)
      img_y = 0.00001;
    map[y * width + x].x = img_x;
    map[y * width + x].y = img_y;
  }
  void safe_set_coord (int xx, int yy, coord_t img_x, coord_t img_y)
  {
    int x = xx + xshift;
    int y = yy + yshift;
    if (x < 0 || x >= width || y < 0 || y >= height)
      {
	grow (x < 0, x >= width, y < 0, y >= height);
	x = xx + xshift;
	y = yy + yshift;
	assert (x >= 0 && x < width && y >= 0 && y < height);
      }
    if (img_x == 0)
      img_x = 0.00001;
    if (img_y == 0)
      img_y = 0.00001;
    map[y * width + x].x = img_x;
    map[y * width + x].y = img_y;
  }
  void get_coord (int x, int y, coord_t *img_x, coord_t *img_y)
  {
    if (!known_p (x, y))
      abort ();
    x += xshift;
    y += yshift;
    *img_x = map[y * width + x].x;
    *img_y = map[y * width + x].y;
  }
  void get_solver_points_nearby (coord_t sx, coord_t sy, int n, solver_parameters &sparams)
  {
    int npoints = 0;
    // TODO: Round properly
    int x = sx * 2;
    int y = sy;
    //printf ("pre: %i %i\n", x, y);
    x += xshift;
    y += yshift;
    x = std::max (std::min (x, width), 0);
    y = std::max (std::min (y, height), 0);
    x -= xshift;
    y -= yshift;
    //printf ("post: %i %i\n", x, y);
    sparams.remove_points ();
    // TODO: Take into account that x is multiplied by 2. 
    for (int d = 0; d < std::max (width, height); d++)
      {
	int lnpoints = npoints;
	//printf ("d:%i\n",d);
	for (int i = 0; i <= 2 * d; i++)
	  {
	    if (known_p (x - d + i, y - d))
	      {
		coord_t img_x, img_y;
		get_coord (x - d + i, y - d, &img_x, &img_y);
		if (img_x < 0 || img_y < 0)
		  {
		    printf ("%i %i %i %i %p\n", x - d + i, y - d, xshift, yshift, this);
		    abort ();
		  }
		sparams.add_point (img_x, img_y, (x - d + i) / 2.0, y - d, (x - d + i) & 1 ? solver_parameters::blue : solver_parameters::green);
		npoints++;
	      }
	    if (d && known_p (x - d + i, y + d))
	      {
		coord_t img_x, img_y;
		get_coord (x - d + i, y + d, &img_x, &img_y);
		if (img_x < 0 || img_y < 0)
		  {
		    printf ("%i %i %i %i %p\n", x - d + i, y + d, xshift, yshift, this);
		    abort ();
		  }
		sparams.add_point (img_x, img_y, (x - d + i) / 2.0, y + d, (x - d + i) & 1 ? solver_parameters::blue : solver_parameters::green);
		npoints++;
	      }
	  }
	for (int i = 1; i < 2 * d; i++)
	  {
	    if (known_p (x-d, y - d + i))
	      {
		coord_t img_x, img_y;
		get_coord (x - d, y - d + i, &img_x, &img_y);
		if (img_x < 0 || img_y < 0)
		  {
		    printf ("%i %i %i %i %p\n", x - d, y - d + i, xshift, yshift, this);
		    abort ();
		  }
		sparams.add_point (img_x, img_y, (x - d) / 2.0, y - d + i, (x - d) & 1 ? solver_parameters::blue : solver_parameters::green);
		npoints++;
	      }
	    if (known_p (x+d, y - d + i))
	      {
		coord_t img_x, img_y;
		get_coord (x + d, y - d + i, &img_x, &img_y);
		if (img_x < 0 || img_y < 0)
		  {
		    printf ("%i %i %i %i %p\n", x + d, y - d + i, xshift, yshift, this);
		    abort ();
		  }
		sparams.add_point (img_x, img_y, (x + d) / 2.0, y - d + i, (x + d) & 1 ? solver_parameters::blue : solver_parameters::green);
		npoints++;
	      }
	  }
	if (lnpoints > n)
	  return;
      }
    //sparams.dump (stdout);
    //abort ();
  }
  int
  check_consistency (coord_t coordinate1_x, coord_t coordinate1_y, coord_t coordinate2_x, coord_t coordinate2_y, coord_t tolerance)
  {
    int n = 0;
    for (int y = 0; y < height - 1; y++)
      for (int x = 0; x < width - 1; x++)
      {
	if (map[y * width + x].x != 0)
	  {
	    if (!map[y * width + x].y)
	    {
	      printf ("%i %i\n",x,y);
	      abort ();
	    }
	    if (map[y * width + x + 1].x != 0)
	      {
		coord_t rx = map[y * width + x + 1].x;
		coord_t ry = map[y * width + x + 1].y;
		coord_t ex = map[y * width + x].x + coordinate1_x / 2;
		coord_t ey = map[y * width + x].y + coordinate1_y / 2;
		coord_t dist = (ex - rx) * (ex - rx) + (ey - ry) * (ey - ry);
		if (dist > tolerance * tolerance)
		  {
		    printf ("Out of tolerance points %i,%i (%f,%f) and %i,%i (%f,%f) distance:%f tolerance:%f\n",x,y, map[y * width + x].x, map[y * width + x].y, x+1, y, rx, ry, sqrt(dist), tolerance);
		    n++;
		  }
	      }
	    if (map[(y + 1) * width + x].x != 0)
	      {
		coord_t rx = map[(y + 1) * width + x].x;
		coord_t ry = map[(y + 1) * width + x].y;
		coord_t ex = map[y * width + x].x + coordinate2_x;
		coord_t ey = map[y * width + x].y + coordinate2_y;
		coord_t dist = (ex - rx) * (ex - rx) + (ey - ry) * (ey - ry);
		if (dist > tolerance * tolerance)
		  {
		    printf ("Out of tolerance points %i,%i (%f,%f) and %i,%i (%f,%f) distance:%f tolerance:%f\n",x,y, map[y * width + x].x, map[y * width + x].y, x, y+1, rx, ry, sqrt(dist), tolerance);
		    n++;
		  }
	      }
	  }
      }
    return n;
  }
  bool
  grow (bool left, bool right, bool top, bool bottom)
  {
    int new_xshift = xshift;
    int new_yshift = yshift;
    int new_width = width;
    int new_height = height;
    int xgrow = width / 8 + 1;
    int ygrow = height / 8 + 1;
    if (left)
      new_xshift += xgrow, new_width += xgrow;
    if (right)
      new_width += xgrow;
    if (top)
      new_yshift += ygrow, new_height += ygrow;
    if (bottom)
      new_height += ygrow;
    coord_entry *new_map = (coord_entry *)calloc (new_width * new_height, sizeof (coord_entry));
    if (!new_map)
      return false;
    for (int y = 0; y < height; y++)
      memcpy (new_map + (new_width * (y + new_yshift - yshift) + new_xshift - xshift), map + width * y, width * sizeof (coord_entry));
    free (map);
    map = new_map;
    width = new_width;
    height = new_height;
    xshift = new_xshift;
    yshift = new_yshift;
    return true;
  }
  void
  add_solver_points (solver_parameters *sparam, int xgrid, int ygrid)
  {
    int xstep = width / xgrid;
    int ystep = height / ygrid;
    sparam->remove_points ();
    for (int x = 0; x + xstep - 1 < width; x += xstep)
      for (int y = 0; y + ystep - 1 < height; y += ystep)
	{
	  bool found = false;
	  for (int yy = y ; yy < y+ystep && !found; yy++)
	    for (int xx = x ; xx < x+xstep && !found; xx++)
	      if (known_p (xx - xshift, yy - yshift))
		{
		  coord_t ix, iy;
		  found = true;
		  get_coord (xx -xshift, yy - yshift, &ix, &iy);
		  sparam->add_point (ix, iy, (xx - xshift) / 2.0, yy - yshift, (xx - xshift ? solver_parameters::blue : solver_parameters::green));
		}
	}
  }
  void
  get_known_range (int *xminr, int *yminr, int *xmaxr, int *ymaxr)
  {
    int xmin = std::numeric_limits<int>::max (), ymin = std::numeric_limits<int>::max (), xmax = std::numeric_limits<int>::min (), ymax = std::numeric_limits<int>::min ();
    for (int y = 0 ; y < height; y++)
      for (int x = 0 ; x < width * 2; x++)
	if (known_p (x - xshift, y - yshift))
	  {
	    coord_t ix, iy;
	    get_coord (x - xshift, y - yshift, &ix, &iy);
	    xmin = std::min (xmin, (int)floor (ix));
	    ymin = std::min (ymin, (int)floor (iy));
	    xmax = std::max (xmax, (int)ceil (ix));
	    ymax = std::max (ymax, (int)ceil (iy));
	  }
    *xminr = xmin;
    *yminr = ymin;
    *xmaxr = xmax;
    *ymaxr = ymax;
  }
  int width, height, xshift, yshift;
private:
  struct coord_entry {coord_t x, y;};
  coord_entry *map;
};
mesh *
solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam2, screen_map &smap, progress_info *progress);
#endif
