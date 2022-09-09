#ifndef RENDER_SCR_DETECT_H
#define RENDER_SCR_DETECT_H
#include "render.h"
#include "scr-detect.h"
#include "patches.h"
class render_scr_detect : public render
{
public:
  render_scr_detect (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval)
  {
    m_scr_detect.set_parameters (param, m_img.maxval);
  }
  ~render_scr_detect ();
  scr_detect::color_class classify_pixel (int x, int y)
  {
    if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
      return scr_detect::unknown;
    scr_detect::color_class t = m_color_class_map->get_class (x, y);
    if (t == scr_detect::unknown)
      return scr_detect::unknown;
    for (int yy = std::max (y - 1, 0); yy < std::min (y + 1, m_img.height); yy++)
      for (int xx = std::max (x - 1, 0); xx < std::min (x + 1, m_img.width); xx++)
	if (xx != x || yy != y)
	  {
	    scr_detect::color_class q = m_color_class_map->get_class (xx, yy);
	    if (q != scr_detect::unknown && q != t)
	      return scr_detect::unknown;
	  }
    return t;
  }
  inline 
  void get_screen_color (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
  {
    /* Center of pixel [0,0] is [0.5,0.5].  */
    xp -= (coord_t)0.5;
    yp -= (coord_t)0.5;
    int sx, sy;
    coord_t rx = my_modf (xp, &sx);
    coord_t ry = my_modf (yp, &sy);

    if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
      {
	*r = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_red ( sx-1, sy-1), m_color_class_map->get_color_red (sx-1, sy), m_color_class_map->get_color_red (sx-1, sy+1), m_color_class_map->get_color_red (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_red ( sx-0, sy-1), m_color_class_map->get_color_red (sx-0, sy), m_color_class_map->get_color_red (sx-0, sy+1), m_color_class_map->get_color_red (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_red ( sx+1, sy-1), m_color_class_map->get_color_red (sx+1, sy), m_color_class_map->get_color_red (sx+1, sy+1), m_color_class_map->get_color_red (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_red ( sx+2, sy-1), m_color_class_map->get_color_red (sx+2, sy), m_color_class_map->get_color_red (sx+2, sy+1), m_color_class_map->get_color_red (sx+2, sy+2), ry),
				rx);
	*g = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_green ( sx-1, sy-1), m_color_class_map->get_color_green (sx-1, sy), m_color_class_map->get_color_green (sx-1, sy+1), m_color_class_map->get_color_green (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_green ( sx-0, sy-1), m_color_class_map->get_color_green (sx-0, sy), m_color_class_map->get_color_green (sx-0, sy+1), m_color_class_map->get_color_green (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_green ( sx+1, sy-1), m_color_class_map->get_color_green (sx+1, sy), m_color_class_map->get_color_green (sx+1, sy+1), m_color_class_map->get_color_green (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_green ( sx+2, sy-1), m_color_class_map->get_color_green (sx+2, sy), m_color_class_map->get_color_green (sx+2, sy+1), m_color_class_map->get_color_green (sx+2, sy+2), ry),
				rx);
	*b = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_blue ( sx-1, sy-1), m_color_class_map->get_color_blue (sx-1, sy), m_color_class_map->get_color_blue (sx-1, sy+1), m_color_class_map->get_color_blue (sx-1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_blue ( sx-0, sy-1), m_color_class_map->get_color_blue (sx-0, sy), m_color_class_map->get_color_blue (sx-0, sy+1), m_color_class_map->get_color_blue (sx-0, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_blue ( sx+1, sy-1), m_color_class_map->get_color_blue (sx+1, sy), m_color_class_map->get_color_blue (sx+1, sy+1), m_color_class_map->get_color_blue (sx+1, sy+2), ry),
				cubic_interpolate (m_color_class_map->get_color_blue ( sx+2, sy-1), m_color_class_map->get_color_blue (sx+2, sy), m_color_class_map->get_color_blue (sx+2, sy+1), m_color_class_map->get_color_blue (sx+2, sy+2), ry),
				rx);
	 *r = std::min (std::max (*r, (luminosity_t)0), (luminosity_t)1);
	 *g = std::min (std::max (*g, (luminosity_t)0), (luminosity_t)1);
	 *b = std::min (std::max (*b, (luminosity_t)0), (luminosity_t)1);
     }
    else
      {
	*r = 0;
	*g = 0;
	*b = 0;
	return;
      }
  }
  void precompute_all ();
  enum render_scr_detect_type_t
  {
    render_type_original,
    render_type_adjusted_color,
    render_type_pixel_colors,
    render_type_realistic_scr,
    render_type_scr_nearest,
    render_type_scr_nearest_scaled
  };
  rgbdata fast_get_adjusted_pixel (int x, int y)
  {
    rgbdata d;
    m_scr_detect.adjust_color (m_img.rgbdata[y][x].r, m_img.rgbdata[y][x].g, m_img.rgbdata[y][x].b, &d.red, &d.green, &d.blue);
    return d;
  }
  void inline render_adjusted_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int xx = x;
    int yy = y;
    if (xx < 0 || xx >= m_img.width || yy < 0 || yy >= m_img.height)
      {
	set_color (0, 0, 0, r,g,b);
	return;
      }
    rgbdata d = fast_get_adjusted_pixel (xx, yy);
    set_color (d.red,d.green,d.blue, r, g, b);
  }
  luminosity_t
  get_patch_density (int x, int y, scr_detect::color_class c)
  {
    const int max_patch_size = 16;
    struct queue {int x, y;} queue [max_patch_size];
    int start = 0, end = 1;
    queue[0].x = x;
    queue[0].y = y;
    //assert (m_color_class_map.get_class (x, y) == c);
    //assert (!visited[y * m_img.width + x]);
    //visited[y * m_img.width + x] = 1;

    while (start < end)
      {
	int cx = queue[start].x;
	int cy = queue[start].y;
	for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, m_img.height); yy++)
	  for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, m_img.width); xx++)
	    if ((xx != cx || yy != cy) /*&& !visited[yy * m_img.width + xx]*/ && m_color_class_map->get_class (xx, yy) == c)
	      {
		int i;
		for (i = 0; i < end; i++)
		  if (queue[i].x == xx && queue[i].y == yy)
		    break;
		if (i != end)
		  continue;
		queue[end].x = xx;
		queue[end].y = yy;
		//visited[yy * m_img.width + xx] = 1;
		end++;
		if (end == max_patch_size)
		  goto done;
	      }
	start++;
      }
done:
    luminosity_t val = 0;
    //printf ("%i\n",end);
    for (int i = 0; i < end; i++)
      {
	int cx = queue[i].x;
	int cy = queue[i].y;
	//visited[cy * m_img.width + cx] = 0;
        //assert (m_color_class_map.get_class (cx, cy) == c);
	val += get_data (cx, cy);
      }
     return val / end;
  }
  DLL_PUBLIC static void render_tile (enum render_scr_detect_type_t render_type, scr_detect_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step);
protected:
  scr_detect m_scr_detect;
  color_class_map *m_color_class_map;
  void get_adjusted_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize);
};
class render_scr_detect_superpose_img : public render_scr_detect
{
public:
  inline render_scr_detect_superpose_img (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b);
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize);
private:
  rgbdata fast_sample_pixel_img (int x, int y);
  void inline sample_pixel_img (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
};
flatten_attr inline rgbdata
render_scr_detect_superpose_img::fast_sample_pixel_img (int x, int y)
{
  luminosity_t rr, gg, bb;
  get_screen_color (x, y, &rr, &gg, &bb);
  luminosity_t graydata = get_data (x, y);
  rgbdata ret = {graydata * rr, graydata * gg, graydata * bb};
  return ret;
}
flatten_attr inline void
render_scr_detect_superpose_img::sample_pixel_img (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  luminosity_t rr, gg, bb;
  get_screen_color (x, y, &rr, &gg, &bb);
  luminosity_t graydata = get_img_pixel (x, y);
  *r = graydata * rr;
  *g = graydata * gg;
  *b = graydata * bb;
}
flatten_attr void
render_scr_detect_superpose_img::render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
{
  luminosity_t rr, gg, bb;
  sample_pixel_img (x, y, &rr, &gg, &bb);
  set_color (rr, gg, bb, r,g,b);
}

inline void
render_scr_detect_superpose_img::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{ 
  downscale<render_scr_detect_superpose_img, rgbdata, &render_scr_detect_superpose_img::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize);
}

class distance_list
{
public:
  static const int max_distance = 20;
  struct entry {
	  int x, y;
	  coord_t fdist;
	  int dist;
  };
  struct entry list[max_distance*max_distance * 4];
  int num;
  distance_list ();
};

extern class distance_list distance_list;
class render_scr_nearest : public render_scr_detect
{
public:
  inline render_scr_nearest (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
     /* Search for nearest pixels of each known color.  */
     const coord_t inf = distance_list::max_distance + 1;
     int rx[3], ry[3];
     coord_t cdist[3] = {inf, inf, inf};
     coord_t biggest = inf;
     for (int i = 0; i < distance_list.num && distance_list.list[i].fdist < biggest + 2; i++)
       {
	 int xx = (int)x + distance_list.list[i].x;
	 int yy = (int)y + distance_list.list[i].y;
	 if (xx < 0 || yy < 0 || xx >= m_img.width || yy >= m_img.height)
	   continue;
	 scr_detect::color_class t = m_color_class_map->get_class (xx, yy);
	 if (t == scr_detect::unknown)
	   continue;
         //assert (t>=0 && t < 3);
	 if (distance_list.list[i].fdist > cdist[(int)t] + 2)
	   continue;
	 double dist = scr_to_img::my_sqrt ((xx + (coord_t)0.5 - x) * (xx + (coord_t)0.5 - x) + (yy + (coord_t)0.5 - y) * (yy + (coord_t)0.5 - y));
	 if (dist < cdist[(int)t])
	   {
	     cdist[(int)t] = dist;
	     rx[(int)t] = xx;
	     ry[(int)t] = yy;
	     biggest = std::max (std::max (cdist[0], cdist[1]), cdist[2]);
	   }
       }
     if (biggest == inf)
      set_color (0,0,0,r,g,b);
    else
      set_color (get_patch_density (rx[0], ry[0], scr_detect::red), get_patch_density (rx[1], ry[1], scr_detect::green), get_patch_density (rx[2], ry[2], scr_detect::blue), r, g, b);
  }
private:
};

extern class distance_list distance_list;
class render_scr_nearest_scaled : public render_scr_detect
{
public:
  inline render_scr_nearest_scaled (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval), m_patches (NULL)
  { 
  }
  inline ~render_scr_nearest_scaled ()
  {
    delete m_patches;
  }
  void precompute_all ()
  {
    render_scr_detect::precompute_all ();
    m_patches = new patches (m_img, *this, *m_color_class_map, 16);
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int rx[3], ry[3];
    patches::patch_index_t ri[3];
    if (m_patches->nearest_patches (x,y, rx, ry, ri))
      {
#if 1
	patches::patch p = m_patches->get_patch (ri[0]);
	luminosity_t rr = p.luminosity_sum / (luminosity_t) p.overall_pixels;
	p = m_patches->get_patch (ri[1]);
	luminosity_t gg = p.luminosity_sum / (luminosity_t) p.overall_pixels;
	p = m_patches->get_patch (ri[2]);
	luminosity_t bb = p.luminosity_sum / (luminosity_t) p.overall_pixels;
        set_color (rr,gg,bb,r,g,b);
#else
	luminosity_t rr;
	rr = ((ri[0] & 15) + 1) / 17.0;
        set_color (rr,rr,rr,r,g,b);
#endif
#if 0
	luminosity_t rr;
	if (!m_patches->get_patch_color (x,y))
	  rr = ((ri[0] & 15) + 1) / 17.0;
	else
	  rr = 0;
        set_color (rr,rr,rr,r,g,b);
#endif
      }
    else
      set_color (0,0,0,r,g,b);
  }
private:
  patches *m_patches;
};
#endif
