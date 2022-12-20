#ifndef SCREEN_MAP_H
#define SCREEN_MAP_H
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
  void set_coord (int x, int y, coord_t img_x, coord_t img_y)
  {
    x += xshift;
    y += yshift;
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
private:
  struct coord_entry {coord_t x, y;};
  coord_entry *map;
  int width, height, xshift, yshift;
};
mesh *
solver_mesh (scr_to_img_parameters *param, image_data &img_data, solver_parameters &sparam2, screen_map &smap, progress_info *progress);
#endif
