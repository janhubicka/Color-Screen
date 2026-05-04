/* Rendering logic for screen detection.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef RENDER_SCR_DETECT_H
#define RENDER_SCR_DETECT_H
#include "include/progress-info.h"
#include "scr-detect.h"
#include "render.h"
#include "patches.h"
#include "lru-cache.h"

namespace colorscreen
{
/* Parameters for color classification cache.  */
struct color_class_params
{
  /* ID of input image.  */
  uint64_t image_id = 0;
  /* Input image data.  */
  const image_data *img = nullptr;
  /* Precomputed RGB data (optional).  */
  rgbdata *precomputed_rgbdata = nullptr;
  /* Screen detection parameters.  */
  scr_detect_parameters p;
  /* Screen detection logic.  */
  class scr_detect *d = nullptr;
  /* Gamma value for linearization.  */
  luminosity_t gamma = 0;

  bool
  operator==(const color_class_params &o) const
  {
    return image_id == o.image_id
	   && gamma == o.gamma
	   && p == o.p;
  }
};
std::unique_ptr<color_class_map> get_color_class_map(color_class_params &, progress_info *);

/* Parameters for precomputed RGB data cache.  */
struct precomputed_rgbdata_params
{
  /* ID of input image.  */
  uint64_t image_id = 0;
  /* Screen detection parameters.  */
  scr_detect_parameters p;
  /* Gamma value for linearization.  */
  luminosity_t gamma = 0;

  /* Input image data.  */
  const image_data *img = nullptr;
  /* Screen detection logic.  */
  class scr_detect *d = nullptr;
  /* Renderer used for precomputation.  */
  class render_scr_detect *r = nullptr;

  bool
  operator==(const precomputed_rgbdata_params &o) const
  {
    return image_id == o.image_id
	   && p.red == o.p.red
	   && p.green == o.p.green
	   && p.blue == o.p.blue
	   && p.black == o.p.black
	   && p.sharpen_radius == o.p.sharpen_radius
	   && p.sharpen_amount == o.p.sharpen_amount
	   && gamma == o.gamma;
  }
};
struct precomputed_rgbdata;
std::unique_ptr<precomputed_rgbdata> get_precomputed_rgbdata(precomputed_rgbdata_params &, progress_info *);

/* Parameters for patches cache.  */
struct patches_cache_params
{
  /* ID of color class map.  */
  uint64_t scr_map_id = 0;
  /* ID of grayscale data.  */
  uint64_t gray_data_id = 0;
  /* Color class map.  */
  class color_class_map *map = nullptr;
  /* Input image data.  */
  const image_data *img = nullptr;
  /* Renderer used for patch detection.  */
  class render *r = nullptr;

  /* TODO: render parameters affects luminosity.  */
  bool
  operator==(const patches_cache_params &o) const
  {
    return scr_map_id == o.scr_map_id
	   && gray_data_id == o.gray_data_id;
  }
};
std::unique_ptr<patches> get_patches(patches_cache_params &, progress_info *);

/* Parameters for color data cache.  */
struct color_data_params
{
  /* ID of color class map.  */
  uint64_t color_class_map_id = 0;
  /* ID of grayscale data.  */
  uint64_t graydata_id = 0;
  /* Input image data.  */
  const image_data *img = nullptr;
  /* Color class map.  */
  class color_class_map *map = nullptr;
  /* Renderer used for color data computation.  */
  class render *r = nullptr;


  bool
  operator==(const color_data_params &o) const
  {
    return color_class_map_id == o.color_class_map_id
	   && graydata_id == o.graydata_id;
  }
};
struct color_data;
std::unique_ptr<color_data> get_new_color_data(struct color_data_params &, progress_info *);
typedef lru_cache<color_class_params, color_class_map, get_color_class_map, 4> color_class_cache_t;
typedef lru_cache<precomputed_rgbdata_params, precomputed_rgbdata, get_precomputed_rgbdata, 4> precomputed_rgbdata_cache_t;
typedef lru_cache<patches_cache_params, patches, get_patches, 4> patches_cache_t;
typedef lru_cache<color_data_params, struct color_data, get_new_color_data, 10> color_data_cache_t;

struct render_to_file_params;
/* Renderer using screen detection logic.  */
class render_scr_detect : public render
{
public:
  /* Initialize screen detection renderer.
     PARAM specifies screen detection parameters.
     IMG is the input scan data.
     RPARAM specifies rendering parameters.
     DSTMAXVAL is the maximum destination pixel value.  */
  render_scr_detect (const scr_detect_parameters &param, const image_data &img, const render_parameters &rparam, int dstmaxval)
    : render (img, rparam, dstmaxval),
      m_precomputed_rgbdata (nullptr), m_precomputed_rgbdata_holder (),
      m_color_class_map ()
  {
    m_scr_detect.m_param = param;
  }
  ~render_scr_detect ();
  void set_render_type (render_type_parameters)
  {
  }
  /* Return the color class of the pixel or unknown if it is not a screen pixel at position P.  */
  pure_attr scr_detect::color_class classify_pixel (int_point_t p) const noexcept
  {
    int_image_area area (0, 0, m_img.width, m_img.height);
    if (!area.contains_p (p))
      return scr_detect::unknown;
    scr_detect::color_class t = m_color_class_map->get_class (p.x, p.y);
    if (t == scr_detect::unknown)
      return scr_detect::unknown;
    for (int yy = std::max (p.y - 1, (int64_t)0); yy <= std::min (p.y + 1, (int64_t)m_img.height - 1); yy++)
      for (int xx = std::max (p.x - 1, (int64_t)0); xx <= std::min (p.x + 1, (int64_t)m_img.width - 1); xx++)
	if (xx != p.x || yy != p.y)
	  {
	    scr_detect::color_class q = m_color_class_map->get_class (xx, yy);
	    if (q != scr_detect::unknown && q != t)
	      return scr_detect::unknown;
	  }
    return t;
  }
  /* Get the color of the screen at position P.  */
  pure_attr inline rgbdata
  get_screen_color (point_t p) const noexcept
  {
    /* Center of pixel [0,0] is [0.5,0.5].  */
    coord_t xp = p.x - (coord_t)0.5;
    coord_t yp = p.y - (coord_t)0.5;
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
	return {std::clamp (r, (luminosity_t)0, (luminosity_t)1),
                std::clamp (g, (luminosity_t)0, (luminosity_t)1),
                std::clamp (b, (luminosity_t)0, (luminosity_t)1)};
     }
    else
     return {0, 0, 0};
  }
  bool precompute_all (bool grayscale_needed, bool normalized_patches, rgbdata patch_proportions, progress_info *progress)
  {
    abort ();
  }
  /* Precompute all data needed for rendering.
     GRAYSCALE_NEEDED specifies if grayscale data is needed.
     NORMALIZED_PATCHES specifies if patches should be normalized.
     PROGRESS is used to report progress and check for cancellation.  */
  /* Precompute all data needed for rendering.
     GRAYSCALE_NEEDED is true if grayscale data is needed.
     NORMALIZED_PATCHES is true if patch proportions should be normalized.
     PROGRESS is used to report progress and check for cancellation.  */
  nodiscard_attr bool precompute_all (bool grayscale_needed, bool normalized_patches, progress_info *progress);
  /* Precompute adjusted RGB data.
     PROGRESS is used to report progress and check for cancellation.  */
  bool precompute_rgbdata (progress_info *progress);
  /* Get the adjusted pixel value at position P.  */
  pure_attr inline rgbdata get_adjusted_pixel (point_t p) const
  {
    coord_t xp = p.x - (coord_t)0.5;
    coord_t yp = p.y - (coord_t)0.5;
    int sx, sy;
    coord_t rx = my_modf (xp, &sx);
    coord_t ry = my_modf (yp, &sy);

    if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
      {
	rgbdata d[4][4];
	if (m_precomputed_rgbdata)
	  for (int yy = -1; yy <= 2; yy++)
	    for (int xx = -1; xx <= 2; xx++)
	      d[yy+1][xx+1] = fast_precomputed_get_adjusted_pixel ({xx + sx, yy + sy});
	else
	  for (int yy = -1; yy <= 2; yy++)
	    for (int xx = -1; xx <= 2; xx++)
	      d[yy+1][xx+1] =  fast_nonprecomputed_get_adjusted_pixel ({xx + sx, yy + sy});
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
  /* Normalize color C so that the sum of its components is 1.  */
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
  /* Get precomputed adjusted pixel at integer position P.  */
  pure_attr inline my_mem_rgbdata fast_precomputed_get_adjusted_pixel (int_point_t p) const
  {
    return m_precomputed_rgbdata[p.y * m_img.width + p.x];
  }
  /* Get precomputed normalized pixel at integer position P.  */
  pure_attr inline my_mem_rgbdata fast_precomputed_get_normalized_pixel (int_point_t p) const
  {
    return normalize_color (m_precomputed_rgbdata[p.y * m_img.width + p.x]);
  }
  /* Adjust linearized color C.  */
  pure_attr inline rgbdata adjust_linearized_color (rgbdata c) const
  {
    rgbdata d;
    m_scr_detect.adjust_linearized_color (c.red, c.green, c.blue, &d.red, &d.green, &d.blue);
    return d;
  }
  /* Get non-precomputed adjusted pixel at integer position P.  */
  pure_attr inline rgbdata fast_nonprecomputed_get_adjusted_pixel (int_point_t p) const
  {
    rgbdata d;
    image_data::pixel p_pixel = m_img.get_rgb_pixel (p.x, p.y);
    m_scr_detect.adjust_color (p_pixel.r, p_pixel.g, p_pixel.b, &d.red, &d.green, &d.blue);
    return d;
  }
  /* Get non-precomputed normalized pixel at integer position P.  */
  pure_attr inline rgbdata fast_nonprecomputed_get_normalized_pixel (int_point_t p) const
  {
    rgbdata d;
    image_data::pixel p_pixel = m_img.get_rgb_pixel (p.x, p.y);
    m_scr_detect.adjust_color (p_pixel.r, p_pixel.g, p_pixel.b, &d.red, &d.green, &d.blue);
    return normalize_color (d);
  }
  /* Get adjusted pixel at integer position P.  */
  pure_attr inline rgbdata fast_get_adjusted_pixel (int_point_t p) const
  {
    if (m_precomputed_rgbdata)
      return fast_precomputed_get_adjusted_pixel (p);
    else
      return fast_nonprecomputed_get_adjusted_pixel (p);
  }
  /* Get normalized pixel at integer position P.  */
  pure_attr inline rgbdata fast_get_normalized_pixel (int_point_t p) const
  {
    rgbdata c = normalize_color (fast_get_adjusted_pixel (p));
    return c;
  }
  /* Get screen color at integer position P.  */
  pure_attr inline rgbdata fast_get_screen_pixel (int_point_t p) const
  {
    rgbdata d;
    d.red = m_color_class_map->get_color_red (p.x, p.y);
    d.green = m_color_class_map->get_color_green (p.x, p.y);
    d.blue = m_color_class_map->get_color_blue (p.x, p.y);
    return d;
  }
#if 0
  void inline render_adjusted_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    int xx = x;
    int yy = y;
    if (xx < 0 || xx >= m_img.width || yy < 0 || yy >= m_img.height)
      {
	out_color.final_color (0, 0, 0, r,g,b);
	return;
      }
    //rgbdata d = fast_get_adjusted_pixel (xx, yy);
    rgbdata d = fast_get_adjusted_pixel (xx, yy);
    out_color.final_color (d.red,d.green,d.blue, r, g, b);
  }
#endif
  pure_attr inline rgbdata get_normalized_pixel_img (point_t p) const
  {
    int xx = p.x;
    int yy = p.y;
    if (xx < 0 || xx >= m_img.width || yy < 0 || yy >= m_img.height)
      return {0,0,0};
    //rgbdata d = fast_get_normalized_pixel (xx, yy);
    return  fast_get_normalized_pixel ({xx, yy});
  }
  /* Get the density of a patch of color C at position (X, Y).  */
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
	val += get_data ({cx, cy});
      }
     return val / end;
  }
  /* Render a tile of the image.
     RTPARAM specifies the type of rendering.
     PARAM specifies screen detection parameters.
     IMG is the input scan.
     RPARAM specifies rendering parameters.
     PIXELS is the destination buffer.
     ROWSTRIDE is the number of bytes per row in the destination buffer.
     PIXELBYTES is the number of bytes per pixel in the destination buffer.
     WIDTH, HEIGHT specifies the size of the tile.
     XOFFSET, YOFFSET specifies the offset of the tile in scan coordinates.
     STEP specifies the size of a single pixel in scan coordinates.
     P is used to report progress and check for cancellation.  */
  static bool render_tile (render_type_parameters &rtparam, scr_detect_parameters &param, image_data &img, render_parameters &rparam,
			   unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
			   double xoffset, double yoffset, double step, progress_info *p = nullptr);
  /* Render the image to a file.
     RFPARAMS specifies the output file parameters.
     RTPARAM specifies the type of rendering.
     PARAM specifies screen geometry mapping.
     DPARAM specifies screen detection parameters.
     RPARAM specifies rendering parameters.
     IMG is the input scan data.
     BLACK is the black level.
     PROGRESS is used to report progress and check for cancellation.  */
  static const char *render_to_file (render_to_file_params &rfparams, render_type_parameters rtparam, scr_to_img_parameters &param, scr_detect_parameters &dparam, render_parameters rparam, image_data &img, int black, progress_info *progress);
  /* Analyze color proportions in a given range.
     PARAM specifies screen geometry mapping.
     AREA specifies the range in scan coordinates.
     P is used to report progress and check for cancellation.  */
  rgbdata analyze_color_proportions (scr_to_img_parameters *param, int_image_area area, progress_info *p);
  /* Return color class map used by this renderer.  */
  color_class_map *get_color_class_map ()
  {
    return m_color_class_map.get ();
  }
protected:
  /* Precomputed RGB data.  */
  my_mem_rgbdata *m_precomputed_rgbdata = nullptr;
  /* Holder for precomputed RGB data.  */
  std::shared_ptr<precomputed_rgbdata> m_precomputed_rgbdata_holder;
  /* Map of color classes for each pixel.  */
  std::shared_ptr<color_class_map> m_color_class_map;
  /* Screen detection parameters and state.  */
  scr_detect m_scr_detect;
  /* ID of the color class map.  */
  uint64_t m_color_class_map_id = 0;
  /* ID of the precomputed RGB data.  */
  uint64_t m_precomputed_rgbdata_id = 0;
};

/* Simple wrapper to be used by rendering templates to render adjusted colors.  */
class render_scr_detect_adjusted : public render_scr_detect
{
public:
  /* Initialize adjusted color renderer.
     PARAM specifies screen detection parameters.
     IMG is the input scan data.
     RPARAM specifies rendering parameters.
     DSTMAXVAL is the maximum destination pixel value.  */
  render_scr_detect_adjusted (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (point_t p) const
  {
    return get_adjusted_pixel (p);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    return fast_get_adjusted_pixel (p);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress);
  inline void
  render_pixel_img (point_t p, int *r, int *g, int *b) const
  {
    rgbdata d = sample_pixel_img (p);
    int_rgbdata out_c = out_color.final_color (d);
    *r = out_c.red; *g = out_c.green; *b = out_c.blue;
  }
};

/* Simple wrapper to be used by rendering templates to render normalized colors.  */
class render_scr_detect_normalized : public render_scr_detect
{
public:
  /* Initialize normalized color renderer.
     PARAM specifies screen detection parameters.
     IMG is the input scan data.
     RPARAM specifies rendering parameters.
     DSTMAXVAL is the maximum destination pixel value.  */
  render_scr_detect_normalized (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (point_t p) const
  {
    return get_normalized_pixel_img (p);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    return fast_get_normalized_pixel (p);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress);
};
/* Simple wrapper to be used by rendering templates to render pixel colors.  */
class render_scr_detect_pixel_color : public render_scr_detect
{
public:
  /* Initialize pixel color renderer.
     PARAM specifies screen detection parameters.
     IMG is the input scan data.
     RPARAM specifies rendering parameters.
     DSTMAXVAL is the maximum destination pixel value.  */
  render_scr_detect_pixel_color (scr_detect_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : render_scr_detect (param, img, rparam, dstmaxval)
  { }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  pure_attr inline rgbdata
  sample_pixel_img (point_t p) const
  {
    return get_screen_color (p);
  }
  pure_attr inline rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    return fast_get_screen_pixel (p);
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress);
  void
  render_pixel_img (point_t p, int *r, int *g, int *b) const
  {
    rgbdata d = sample_pixel_img (p);
    int_rgbdata out_c = out_color.final_color (d);
    *r = out_c.red; *g = out_c.green; *b = out_c.blue;
  }
};
class render_scr_detect_superpose_img : public render_scr_detect
{
public:
  inline render_scr_detect_superpose_img (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  void set_render_type (render_type_parameters)
  {
  }
  void inline render_pixel_img (point_t p, int *r, int *g, int *b);
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *);
  pure_attr inline rgbdata fast_sample_pixel_img (int_point_t p) const;
  pure_attr inline rgbdata sample_pixel_img (point_t p) const;
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, false, progress);
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
private:
};
pure_attr inline rgbdata
render_scr_detect_superpose_img::fast_sample_pixel_img (int_point_t p) const
{
  return get_screen_color ({(coord_t)(p.x + 0.5), (coord_t)(p.y + 0.5)}) * get_data (p);
}
pure_attr inline rgbdata
render_scr_detect_superpose_img::sample_pixel_img (point_t p) const
{
  return get_screen_color (p) * get_img_pixel (p);
}
void
render_scr_detect_superpose_img::render_pixel_img (point_t p, int *r, int *g, int *b) 
{
  rgbdata d = sample_pixel_img (p);
  int_rgbdata out_c = out_color.final_color (d);
  *r = out_c.red; *g = out_c.green; *b = out_c.blue;
}

inline bool
render_scr_detect_superpose_img::get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  return downscale<render_scr_detect_superpose_img, rgbdata,
                   &render_scr_detect_superpose_img::fast_sample_pixel_img> (
      data, p, width, height, pixelsize, progress);
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
   : render_scr_detect (param, data, rparam, dst_maxval), m_color_data_handle (nullptr)
  { 
  }
  bool precompute_all (progress_info *progress);
  inline pure_attr rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    if (p.x < 0 || p.x >= m_img.width || p.y < 0 || p.y >= m_img.height)
      return {0,0,0};
    else
      return {get_luminosity (0, p.x, p.y),
	      get_luminosity (1, p.x, p.y),
	      get_luminosity (2, p.x, p.y)};
  }
  rgbdata
  sample_pixel_img (point_t p)
  {
    return fast_sample_pixel_img ({(int)(p.x + 0.5), (int)(p.y + 0.5)});
  }
  void
  render_pixel_img (point_t p, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (p);
    int_rgbdata out_c = out_color.final_color (d);
    *r = out_c.red; *g = out_c.green; *b = out_c.blue;
  }
  ~render_scr_relax();
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    return downscale<render_scr_relax, rgbdata,
                     &render_scr_relax::fast_sample_pixel_img> (data, p, width, height, pixelsize,
                                                 progress);
  }
private:
  luminosity_t *cdata[3];
  inline luminosity_t get_luminosity (int color, int x, int y) const
    {
      return cdata[color][y * m_img.width + x];
    }
  std::shared_ptr<color_data> m_color_data_handle;
};
/* Nearest neighbor screen color renderer.  */
class render_scr_nearest : public render_scr_detect
{
public:
  /* Initialize nearest neighbor renderer.
     PARAM specifies screen detection parameters.
     DATA is the input scan data.
     RPARAM specifies rendering parameters.
     DST_MAXVAL is the maximum destination pixel value.  */
  inline render_scr_nearest (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval)
  { 
  }
  inline pure_attr rgbdata
  sample_pixel_img (point_t p) const
  {
     /* Search for nearest pixels of each known color.  */
     const coord_t inf = distance_list::max_distance + 1;
     int rx[3], ry[3];
     coord_t cdist[3] = {inf, inf, inf};
     coord_t biggest = inf;
     for (int i = 0; i < distance_list.num && distance_list.list[i].fdist < biggest + 2; i++)
       {
	 int xx = (int)p.x + distance_list.list[i].x;
	 int yy = (int)p.y + distance_list.list[i].y;
	 if (xx < 0 || yy < 0 || xx >= m_img.width || yy >= m_img.height)
	   continue;
	 scr_detect::color_class t = m_color_class_map->get_class (xx, yy);
	 if (t == scr_detect::unknown)
	   continue;
         //assert (t>=0 && t < 3);
	 if (distance_list.list[i].fdist > cdist[(int)t] + 2)
	   continue;
	 double dist = my_sqrt ((xx + (coord_t)0.5 - p.x) * (xx + (coord_t)0.5 - p.x) + (yy + (coord_t)0.5 - p.y) * (yy + (coord_t)0.5 - p.y));
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
  fast_sample_pixel_img (int_point_t p) const
  {
    return sample_pixel_img ({p.x + (coord_t)0.5, p.y + (coord_t)0.5});
  }
  void
  render_pixel_img (point_t p, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (p);
    int_rgbdata out_c = out_color.final_color (d);
    *r = out_c.red; *g = out_c.green; *b = out_c.blue;
  }
  bool precompute_all (progress_info *progress)
  {
    return render_scr_detect::precompute_all (true, true, progress);
  }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  bool get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    return downscale<render_scr_nearest, rgbdata,
                     &render_scr_nearest::fast_sample_pixel_img> (data, p, width, height, pixelsize,
                                                 progress);
  }
private:
};

extern class distance_list distance_list;
/* Scaled nearest neighbor screen color renderer.  */
class render_scr_nearest_scaled : public render_scr_detect
{
public:
  /* Initialize scaled nearest neighbor renderer.
     PARAM specifies screen detection parameters.
     DATA is the input scan data.
     RPARAM specifies rendering parameters.
     DST_MAXVAL is the maximum destination pixel value.  */
  inline render_scr_nearest_scaled (scr_detect_parameters &param, image_data &data, render_parameters &rparam, int dst_maxval)
   : render_scr_detect (param, data, rparam, dst_maxval), m_patches ()
  { 
  }
  ~render_scr_nearest_scaled ();
  bool precompute_all (progress_info *progress);
  rgbdata
  sample_pixel_img (point_t p) const
  {
    int rx[3], ry[3];
    patches::patch_index_t ri[3];
    if (m_patches->nearest_patches (p, rx, ry, ri))
      {
#if 1
	const patches::patch &p1 = m_patches->get_patch (ri[0]);
	rgbdata ret;
	ret.red = p1.luminosity_sum / (luminosity_t) p1.overall_pixels;
	const patches::patch &p2 = m_patches->get_patch (ri[1]);
	ret.green = p2.luminosity_sum / (luminosity_t) p2.overall_pixels;
	const patches::patch &p3 = m_patches->get_patch (ri[2]);
	ret.blue = p3.luminosity_sum / (luminosity_t) p3.overall_pixels;
	return ret;
#else
	luminosity_t rr;
	rr = ((ri[0] & 15) + 1) / 17.0;
        out_color.final_color (rr,rr,rr,r,g,b);
#endif
#if 0
	luminosity_t rr;
	if (!m_patches->get_patch_color ({(int)p.x, (int)p.y}))
	  rr = ((ri[0] & 15) + 1) / 17.0;
	else
	  rr = 0;
        out_color.final_color (rr,rr,rr,r,g,b);
#endif
      }
    else
      return {0, 0, 0};
  }
  rgbdata
  fast_sample_pixel_img (int_point_t p) const
  {
    return sample_pixel_img ({p.x + (coord_t)0.5, p.y + (coord_t)0.5});
  }
  void
  render_pixel_img (point_t p, int *r, int *g, int *b)
  {
    rgbdata d = sample_pixel_img (p);
    int_rgbdata out_c = out_color.final_color (d);
    *r = out_c.red; *g = out_c.green; *b = out_c.blue;
  }
  void set_render_type (render_type_parameters rtparam)
  {
  }
  bool precompute_img_range (int_image_area area, progress_info *progress)
  {
    (void)area;
    return precompute_all (progress);
  }
  inline bool
  get_color_data (rgbdata *data, point_t p, int width, int height, coord_t pixelsize, progress_info *progress)
  { 
    return downscale<render_scr_nearest_scaled, rgbdata,
                     &render_scr_nearest_scaled::fast_sample_pixel_img> (
        data, p, width, height, pixelsize, progress);
  }
private:
  std::shared_ptr<patches> m_patches;
};
}
#endif
