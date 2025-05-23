#ifndef RENDER_SCR_DETECT_H
#define RENDER_SCR_DETECT_H
#include "include/progress-info.h"
#include "scr-detect.h"
#include "render.h"
#include "patches.h"
namespace colorscreen
{
struct render_to_file_params;
class render_scr_detect : public render
{
public:
  render_scr_detect (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval), m_precomputed_rgbdata (NULL), m_color_class_map (NULL)
  {
    /* TODO: Move to precomputation and also check return value, pass progress.  */
    m_scr_detect.set_parameters (param, rparam.gamma, &m_img);
  }
  ~render_scr_detect ();
  scr_detect::color_class classify_pixel (int x, int y)
  {
    if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
      return scr_detect::unknown;
    scr_detect::color_class t = m_color_class_map->get_class (x, y);
    if (t == scr_detect::unknown)
      return scr_detect::unknown;
    for (int yy = std::max (y - 1, 0); yy <= std::min (y + 1, m_img.height); yy++)
      for (int xx = std::max (x - 1, 0); xx <= std::min (x + 1, m_img.width); xx++)
	if (xx != x || yy != y)
	  {
	    scr_detect::color_class q = m_color_class_map->get_class (xx, yy);
	    if (q != scr_detect::unknown && q != t)
	      return scr_detect::unknown;
	  }
    return t;
  }
  pure_attr inline rgbdata
  get_screen_color (coord_t xp, coord_t yp) const
  {
    /* Center of pixel [0,0] is [0.5,0.5].  */
    xp -= (coord_t)0.5;
    yp -= (coord_t)0.5;
    int sx, sy;
    coord_t rx = my_modf (xp, &sx);
    coord_t ry = my_modf (yp, &sy);

    if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
      {
	luminosity_t r,g,b;
	r = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_red ( sx-1, sy-1), m_color_class_map->get_color_red (sx-1, sy), m_color_class_map->get_color_red (sx-1, sy+1), m_color_class_map->get_color_red (sx-1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_red ( sx-0, sy-1), m_color_class_map->get_color_red (sx-0, sy), m_color_class_map->get_color_red (sx-0, sy+1), m_color_class_map->get_color_red (sx-0, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_red ( sx+1, sy-1), m_color_class_map->get_color_red (sx+1, sy), m_color_class_map->get_color_red (sx+1, sy+1), m_color_class_map->get_color_red (sx+1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_red ( sx+2, sy-1), m_color_class_map->get_color_red (sx+2, sy), m_color_class_map->get_color_red (sx+2, sy+1), m_color_class_map->get_color_red (sx+2, sy+2), ry),
			       rx);
	g = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_green ( sx-1, sy-1), m_color_class_map->get_color_green (sx-1, sy), m_color_class_map->get_color_green (sx-1, sy+1), m_color_class_map->get_color_green (sx-1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_green ( sx-0, sy-1), m_color_class_map->get_color_green (sx-0, sy), m_color_class_map->get_color_green (sx-0, sy+1), m_color_class_map->get_color_green (sx-0, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_green ( sx+1, sy-1), m_color_class_map->get_color_green (sx+1, sy), m_color_class_map->get_color_green (sx+1, sy+1), m_color_class_map->get_color_green (sx+1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_green ( sx+2, sy-1), m_color_class_map->get_color_green (sx+2, sy), m_color_class_map->get_color_green (sx+2, sy+1), m_color_class_map->get_color_green (sx+2, sy+2), ry),
			       rx);
	b = cubic_interpolate (cubic_interpolate (m_color_class_map->get_color_blue ( sx-1, sy-1), m_color_class_map->get_color_blue (sx-1, sy), m_color_class_map->get_color_blue (sx-1, sy+1), m_color_class_map->get_color_blue (sx-1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_blue ( sx-0, sy-1), m_color_class_map->get_color_blue (sx-0, sy), m_color_class_map->get_color_blue (sx-0, sy+1), m_color_class_map->get_color_blue (sx-0, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_blue ( sx+1, sy-1), m_color_class_map->get_color_blue (sx+1, sy), m_color_class_map->get_color_blue (sx+1, sy+1), m_color_class_map->get_color_blue (sx+1, sy+2), ry),
			       cubic_interpolate (m_color_class_map->get_color_blue ( sx+2, sy-1), m_color_class_map->get_color_blue (sx+2, sy), m_color_class_map->get_color_blue (sx+2, sy+1), m_color_class_map->get_color_blue (sx+2, sy+2), ry),
			       rx);
	return {std::min (std::max (r, (luminosity_t)0), (luminosity_t)1), std::min (std::max (g, (luminosity_t)0), (luminosity_t)1), std::min (std::max (b, (luminosity_t)0), (luminosity_t)1)};
     }
    else
     return {0, 0, 0};
  }
  bool precompute_all (bool grayscale_needed, bool normalized_patches, progress_info *);
  bool precompute_rgbdata (progress_info *progress);
  pure_attr inline rgbdata get_adjusted_pixel (coord_t xp, coord_t yp) const
  {
    xp -= (coord_t)0.5;
    yp -= (coord_t)0.5;
    int sx, sy;
    coord_t rx = my_modf (xp, &sx);
    coord_t ry = my_modf (yp, &sy);

    if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
      {
	rgbdata d[4][4];
	if (m_precomputed_rgbdata)
	  for (int yy = -1; yy <= 2; yy++)
	    for (int xx = -1; xx <= 2; xx++)
	      d[yy+1][xx+1] = fast_precomputed_get_adjusted_pixel (xx + sx, yy + sy);
	else
	  for (int yy = -1; yy <= 2; yy++)
	    for (int xx = -1; xx <= 2; xx++)
	      d[yy+1][xx+1] =  fast_nonprecomputed_get_adjusted_pixel (xx + sx, yy + sy);
	luminosity_t rr,gg, bb;
	rr = cubic_interpolate (cubic_interpolate (d[0][0].red, d[1][0].red, d[2][0].red, d[3][0].red, ry),
				cubic_interpolate (d[0][1].red, d[1][1].red, d[2][1].red, d[3][1].red, ry),
				cubic_interpolate (d[0][2].red, d[1][2].red, d[2][2].red, d[3][2].red, ry),
				cubic_interpolate (d[0][3].red, d[1][3].red, d[2][3].red, d[3][3].red, ry),
				rx);
	gg = cubic_interpolate (cubic_interpolate (d[0][0].green, d[1][0].green, d[2][0].green, d[3][0].green, ry),
				cubic_interpolate (d[0][1].green, d[1][1].green, d[2][1].green, d[3][1].green, ry),
				cubic_interpolate (d[0][2].green, d[1][2].green, d[2][2].green, d[3][2].green, ry),
				cubic_interpolate (d[0][3].green, d[1][3].green, d[2][3].green, d[3][3].green, ry),
				rx);
	bb = cubic_interpolate (cubic_interpolate (d[0][0].blue, d[1][0].blue, d[2][0].blue, d[3][0].blue, ry),
				cubic_interpolate (d[0][1].blue, d[1][1].blue, d[2][1].blue, d[3][1].blue, ry),
				cubic_interpolate (d[0][2].blue, d[1][2].blue, d[2][2].blue, d[3][2].blue, ry),
				cubic_interpolate (d[0][3].blue, d[1][3].blue, d[2][3].blue, d[3][3].blue, ry),
				rx);
	return {rr, gg, bb};
      }
    else
      return {0, 0, 0};
  }
  static rgbdata
  normalize_color (rgbdata c)
  {
    c.red = std::max (c.red, (luminosity_t)0);
    c.green = std::max (c.green, (luminosity_t)0);
    c.blue = std::max (c.blue, (luminosity_t)0);
    //luminosity_t min = /*std::max (*/std::min (std::min (c.red, c.green), c.blue)/*, (luminosity_t) 0*/);
    //c.red -= min;
    //c.green -= min;
    //c.blue -= min;
    luminosity_t sum = c.red + c.green + c.blue;
    if (!(sum > 0))
      return {0, 0, 0};
    luminosity_t adj = 1 / sum;
#if 1
    c.red *= adj;
    c.green *= adj;
    c.blue *= adj;
#else
    c.red = (2 * c.red - sum) * adj;
    c.green = (2 * c.green - sum) * adj;
    c.blue = (2 * c.blue - sum) * adj;
#endif
    return c;
  }
  /* TODO: 16bit floats seems to be too slow for screen detection.  */
  typedef rgbdata my_mem_rgbdata;
  pure_attr inline my_mem_rgbdata fast_precomputed_get_adjusted_pixel (int x, int y) const
  {
    return m_precomputed_rgbdata[y * m_img.width + x];
  }
  pure_attr inline my_mem_rgbdata fast_precomputed_get_normalized_pixel (int x, int y) const
  {
    return normalize_color (m_precomputed_rgbdata[y * m_img.width + x]);
  }
  pure_attr inline rgbdata adjust_linearized_color (rgbdata c) const
  {
    rgbdata d;
    m_scr_detect.adjust_linearized_color (c.red, c.green, c.blue, &d.red, &d.green, &d.blue);
    return d;
  }
  pure_attr inline rgbdata fast_nonprecomputed_get_adjusted_pixel (int x, int y) const
  {
    rgbdata d;
    m_scr_detect.adjust_color (m_img.rgbdata[y][x].r, m_img.rgbdata[y][x].g, m_img.rgbdata[y][x].b, &d.red, &d.green, &d.blue);
    return d;
  }
  pure_attr inline rgbdata fast_nonprecomputed_get_normalized_pixel (int x, int y) const
  {
    rgbdata d;
    m_scr_detect.adjust_color (m_img.rgbdata[y][x].r, m_img.rgbdata[y][x].g, m_img.rgbdata[y][x].b, &d.red, &d.green, &d.blue);
    return normalize_color (d);
  }
  pure_attr inline rgbdata fast_get_adjusted_pixel (int x, int y) const
  {
    if (m_precomputed_rgbdata)
      return fast_precomputed_get_adjusted_pixel (x, y);
    else
      return fast_nonprecomputed_get_adjusted_pixel (x, y);
  }
  pure_attr inline rgbdata fast_get_normalized_pixel (int x, int y) const
  {
    rgbdata c = normalize_color (fast_get_adjusted_pixel (x, y));
    return c;
  }
  pure_attr inline rgbdata fast_get_screen_pixel (int x, int y) const
  {
    rgbdata d;
    d.red = m_color_class_map->get_color_red (x, y);
    d.green = m_color_class_map->get_color_green (x, y);
    d.blue = m_color_class_map->get_color_blue (x, y);
    return d;
  }
#if 0
  void inline render_adjusted_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int xx = x;
    int yy = y;
    if (xx < 0 || xx >= m_img.width || yy < 0 || yy >= m_img.height)
      {
	set_color (0, 0, 0, r,g,b);
	return;
      }
    //rgbdata d = fast_get_adjusted_pixel (xx, yy);
    rgbdata d = fast_get_adjusted_pixel (xx, yy);
    set_color (d.red,d.green,d.blue, r, g, b);
  }
#endif
  pure_attr inline rgbdata get_normalized_pixel_img (coord_t x, coord_t y) const
  {
    int xx = x;
    int yy = y;
    if (xx < 0 || xx >= m_img.width || yy < 0 || yy >= m_img.height)
      return {0,0,0};
    //rgbdata d = fast_get_normalized_pixel (xx, yy);
    return  fast_get_normalized_pixel (xx, yy);
  }
  inline pure_attr luminosity_t
  get_patch_density (int x, int y, scr_detect::color_class c) const
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
  static bool render_tile (render_type_parameters &rtparam, scr_detect_parameters &param, image_data &img, render_parameters &rparam,
			   unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
			   double xoffset, double yoffset, double step, progress_info *p = NULL);
  static const char *render_to_file (render_to_file_params &rfparams, render_type_parameters rtparam, scr_to_img_parameters &param, scr_detect_parameters &dparam, render_parameters rparam, image_data &img, int black, progress_info *progress);
  color_class_map *get_color_class_map ()
  {
    return m_color_class_map;
  }
  rgbdata analyze_color_proportions (scr_to_img_parameters *param, int xmin, int ymin, int xmax, int ymax, progress_info *p);
protected:
  my_mem_rgbdata *m_precomputed_rgbdata;
  class precomputed_rgbdata *m_precomputed_rgbdata_holder;
  color_class_map *m_color_class_map;
  scr_detect m_scr_detect;
  uint64_t m_color_class_map_id;
  uint64_t m_precomputed_rgbdata_id;
};

/* Simple wrapper to be used by rendering templates.  */
class render_scr_detect_adjusted : public render_scr_detect
{
public:
  render_scr_detect_adjusted (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (coord_t xx, coord_t yy) const
  {
    return get_adjusted_pixel (xx, yy);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);
  inline void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b) const
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue, r,g,b);
  }
};

/* Simple wrapper to be used by rendering templates.  */
class render_scr_detect_normalized : public render_scr_detect
{
public:
  render_scr_detect_normalized (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (coord_t xx, coord_t yy) const
  {
    return get_normalized_pixel_img (xx, yy);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);
};
/* Simple wrapper to be used by rendering templates.  */
class render_scr_detect_pixel_color : public render_scr_detect
{
public:
  render_scr_detect_pixel_color (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (coord_t xx, coord_t yy) const
  {
    return get_screen_color (xx, yy);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  void get_color_data (rgbdata *graydata, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress);
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b) const
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue, r,g,b);
  }
};
class render_scr_detect_superpose_img : public render_scr_detect
{
public:
  inline render_scr_detect_superpose_img (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b);
  void get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *);
  pure_attr inline rgbdata fast_sample_pixel_img (int x, int y) const;
  pure_attr inline rgbdata sample_pixel_img (coord_t x, coord_t y) const;
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
private:
};
pure_attr inline rgbdata
render_scr_detect_superpose_img::fast_sample_pixel_img (int x, int y) const
{
  return get_screen_color (x, y) * get_data (x, y);
}
pure_attr inline rgbdata
render_scr_detect_superpose_img::sample_pixel_img (coord_t x, coord_t y) const
{
  return get_screen_color (x, y) * get_img_pixel (x, y);
}
void
render_scr_detect_superpose_img::render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b) 
{
  rgbdata d = sample_pixel_img (x, y);
  set_color (d.red, d.green, d.blue, r,g,b);
}

inline void
render_scr_detect_superpose_img::get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  downscale<render_scr_detect_superpose_img, rgbdata, &render_scr_detect_superpose_img::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
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
class render_scr_relax : public render_scr_detect
{
public:
  inline render_scr_relax (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval), m_color_data_handle (NULL)
  { 
  }
  bool precompute_all (progress_info *);
  inline pure_attr rgbdata
  fast_sample_pixel_img (int x, int y) const
  {
    if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
      return {0,0,0};
    else
      return {get_luminosity (0, x, y),
	      get_luminosity (1, x, y),
	      get_luminosity (2, x, y)};
  }
  rgbdata
  sample_pixel_img (coord_t xx, coord_t yy)
  {
    return fast_sample_pixel_img (xx + 0.5, yy + 0.5);
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue,r,g,b);
  }
  ~render_scr_relax();
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  inline void
  get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    downscale<render_scr_relax, rgbdata, &render_scr_relax::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
  }
private:
  luminosity_t *cdata[3];
  inline luminosity_t get_luminosity (int color, int x, int y) const
    {
      return cdata[color][y * m_img.width + x];
    }
  struct color_data *m_color_data_handle;
};
class render_scr_nearest : public render_scr_detect
{
public:
  inline render_scr_nearest (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  inline pure_attr rgbdata
  sample_pixel_img (coord_t x, coord_t y) const
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
	 double dist = my_sqrt ((xx + (coord_t)0.5 - x) * (xx + (coord_t)0.5 - x) + (yy + (coord_t)0.5 - y) * (yy + (coord_t)0.5 - y));
	 if (dist < cdist[(int)t])
	   {
	     cdist[(int)t] = dist;
	     rx[(int)t] = xx;
	     ry[(int)t] = yy;
	     biggest = std::max (std::max (cdist[0], cdist[1]), cdist[2]);
	   }
       }
     if (biggest == inf)
      return {0, 0, 0};
     else
      return {get_patch_density (rx[0], ry[0], scr_detect::red), get_patch_density (rx[1], ry[1], scr_detect::green), get_patch_density (rx[2], ry[2], scr_detect::blue)};
  }
  inline pure_attr rgbdata
  fast_sample_pixel_img (int x, int y) const
  {
    return sample_pixel_img (x + 0.5, y + 0.5);
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue, r, g, b);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, true, progress);
  }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  inline void
  get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    downscale<render_scr_nearest, rgbdata, &render_scr_nearest::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
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
  ~render_scr_nearest_scaled ();
  bool precompute_all (progress_info *);
  rgbdata
  sample_pixel_img (coord_t x, coord_t y)
  {
    int rx[3], ry[3];
    patches::patch_index_t ri[3];
    if (m_patches->nearest_patches (x,y, rx, ry, ri))
      {
#if 1
	patches::patch p = m_patches->get_patch (ri[0]);
	rgbdata ret;
	ret.red = p.luminosity_sum / (luminosity_t) p.overall_pixels;
	p = m_patches->get_patch (ri[1]);
	ret.green = p.luminosity_sum / (luminosity_t) p.overall_pixels;
	p = m_patches->get_patch (ri[2]);
	ret.blue = p.luminosity_sum / (luminosity_t) p.overall_pixels;
	return ret;
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
      return {0, 0, 0};
  }
  rgbdata
  fast_sample_pixel_img (int x, int y)
  {
    return sample_pixel_img (x + 0.5, y + 0.5);
  }
  void
  render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (x, y);
    set_color (d.red, d.green, d.blue,r,g,b);
  }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (coord_t, coord_t, coord_t, coord_t, progress_info *progress)
  {
    return precompute_all (progress);
  }
  inline void
  get_color_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    downscale<render_scr_relax, rgbdata, &render_scr_relax::fast_sample_pixel_img, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
  }
private:
  patches *m_patches;
};
}
#endif
