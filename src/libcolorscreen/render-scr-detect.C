#include <stdlib.h>
#include <sys/time.h>
#include "include/render-scr-detect.h"
#include "include/render-to-scr.h"
#include "lru-cache.h"
namespace
{
static int stats = -1;
/* Lookup table translates raw input data into linear values.  */
struct color_class_params
{
  unsigned long image_id;
  image_data *img;
  scr_detect_parameters p;
  scr_detect *d;

  bool
  operator==(color_class_params &o)
  {
    return image_id == o.image_id
	   && p == o.p;
  }
};
color_class_map *get_color_class_map(color_class_params &p)
{
  image_data &img = *p.img;
  color_class_map *map = new color_class_map;
  map->allocate (img.width, img.height);
  //printf ("New color map\n");
#pragma omp parallel for default(none) shared(img,map,p)
  for (int y = 0; y < img.height; y++)
    for (int x = 0; x < img.width; x++)
      map->set_class (x, y,
		      p.d->classify_color (img.rgbdata[y][x].r,
					   img.rgbdata[y][x].g,
					   img.rgbdata[y][x].b));
  return map;
}
static lru_cache <color_class_params, color_class_map, get_color_class_map, 1> color_class_cache;

/* Lookup table translates raw input data into linear values.  */
struct patches_cache_params
{
  int scr_map_id;
  color_class_map *map;
  image_data *img;
  render *r;

  /* TODO: render parameters affects luminosity.  */
  bool
  operator==(patches_cache_params &o)
  {
    return scr_map_id == o.scr_map_id;
  }
};
patches *get_patches(patches_cache_params &p)
{
  return new patches (*p.img, *p.r, *p.map, 16);
}
static lru_cache <patches_cache_params, patches, get_patches, 1> patches_cache;
}
class distance_list distance_list;

static inline void
putpixel (unsigned char *pixels, int pixelbytes, int rowstride, int x, int y,
       	  int r, int g, int b)
{
  *(pixels + y * rowstride + x * pixelbytes) = r;
  *(pixels + y * rowstride + x * pixelbytes + 1) = g;
  *(pixels + y * rowstride + x * pixelbytes + 2) = b;
  if (pixelbytes > 3)
    *(pixels + y * rowstride + x * pixelbytes + 3) = 255;
}

void
render_scr_detect::get_adjusted_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_adjusted_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize);
}

void
render_scr_detect::get_screen_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_screen_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize);
}

void
render_scr_detect::render_tile (enum render_scr_detect_type_t render_type,
			        scr_detect_parameters &param, image_data &img,
			        render_parameters &rparam, bool color,
			        unsigned char *pixels, int pixelbytes, int rowstride,
			        int width, int height,
			        double xoffset, double yoffset,
			        double step)
{
  if (width <= 0 || height <= 0)
    return;
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  switch (render_type)
    {
    case render_type_original:
      {
	if (render_type == render_type_original && step > 1)
	  {
	    scr_to_img_parameters dummy;
	    render_to_scr::render_tile (render::render_type_original, dummy, img, rparam, color, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step);
	    return;
	  }
	scr_to_img_parameters dummy;
	render_img render (dummy, img, rparam, 255);
	if (color)
	  render.set_color_display ();
	render.precompute_all ();

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.fast_render_pixel_img ((x + xoffset) * step, py, &r, &g,
					      &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_adjusted_color:
      {
	render_scr_detect render (param, img, rparam, 255);
	render.precompute_all ();

	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_adjusted_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_adjusted_pixel_img ((x + xoffset) * step, py, &r, &g,
					      &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_pixel_colors:
      {
	render_scr_detect render (param, img, rparam, 255);
	render.precompute_all ();
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_screen_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      for (int x = 0; x < width; x++)
		putpixel (pixels, pixelbytes, rowstride, x, y, data[x + width * y].red * 255 + 0.5, data[x + width * y].green * 255 + 0.5, data[x + width * y].blue * 255 + 0.5);
	    free (data);
	    break;
	  }
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		coord_t px = (x + xoffset) * step;
		luminosity_t r, g, b;
		render.get_screen_color (px, py, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r * 255 + 0.5, g * 255 + 0.5, b * 255 + 0.5);
	      }
	   }
      }
      break;
    case render_type_realistic_scr:
      {
	render_scr_detect_superpose_img render (param, img, rparam, 255);
	render.precompute_all ();
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_color_data (data, xoffset * step, yoffset * step, width, height, step);
#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		for (int x = 0; x < width; x++)
		  {
		    int r, g, b;
		    render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		    putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		  }
	      }
	    free (data);
	    break;
	  }

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_scr_nearest:
      {
	render_scr_nearest render (param, img, rparam, 255);
	render.precompute_all ();

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    case render_type_scr_nearest_scaled:
      {
	render_scr_nearest_scaled render (param, img, rparam, 255);
	render.precompute_all ();

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
    case render_type_scr_relax:
      {
	render_scr_relax render (param, img, rparam, 255);
	render.precompute_all ();

#pragma omp parallel for default(none) shared(pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    for (int x = 0; x < width; x++)
	      {
		int r, g, b;
		render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
	      }
	  }
      }
      break;
    }
}
void
render_scr_detect::precompute_all ()
{
  color_class_params p = {m_img.id, &m_img, m_scr_detect.m_param, &m_scr_detect};
  m_color_class_map = color_class_cache.get (p);
  render::precompute_all (false);
}


render_scr_detect::~render_scr_detect ()
{
  color_class_cache.release (m_color_class_map);
}

int cmp_entry(const void *p1, const void *p2)
{
  distance_list::entry *e1 = (distance_list::entry *)p1;
  distance_list::entry *e2 = (distance_list::entry *)p2;
  if (e1->fdist < e2->fdist)
    return -1;
  if (e1->fdist > e2->fdist)
    return 1;
   return (e1->y * 2 * distance_list::max_distance + e1->x) -  (e2->y * 2 * distance_list::max_distance + e2->x);
}

distance_list::distance_list ()
{
  num = 0;
  for (int x = -max_distance; x <= max_distance; x++)
    for (int y = -max_distance; y <= max_distance; y++)
      {
	coord_t dist = sqrt ((x*x) + (y*y));
	if (dist > max_distance)
	  continue;
	list[num].x = x;
	list[num].y = y;
	list[num].fdist = dist;
	list[num].dist = dist;
	num++;
      }
  qsort (list, num, sizeof (struct entry), cmp_entry);
}

render_scr_nearest_scaled::~render_scr_nearest_scaled ()
{
  patches_cache.release (m_patches);
}

void 
render_scr_nearest_scaled::precompute_all ()
{
  render_scr_detect::precompute_all ();
  patches_cache_params p = {m_color_class_map->id, m_color_class_map, &m_img, this};
  m_patches =  patches_cache.get (p);
}

void
render_scr_relax::precompute_all ()
{
  const int max_patch_size = 8;
  const int min_patch_size = 1;
  render_scr_detect::precompute_all ();
  for (int color = 0; color < 3; color++)
    cdata[color] = (luminosity_t *)calloc (m_img.width * m_img.height, sizeof (luminosity_t));
  luminosity_t *tmp = (luminosity_t *)malloc (m_img.width * m_img.height * sizeof (luminosity_t));
#pragma omp parallel for default(none) 
  for (int y = 0; y < m_img.height; y++)
    for (int x = 0; x < m_img.width; x++)
      {
	scr_detect::color_class t = m_color_class_map->get_class (x, y);
	if (t == scr_detect::unknown)
	  continue;
	struct queue {int x, y;} queue [max_patch_size];
	luminosity_t sum = get_data (x, y);
	int start = 0, end = 1;
	queue[0].x = x;
	queue[0].y = y;
	while (start < end)
	  {
	    int cx = queue[start].x;
	    int cy = queue[start].y;
	    for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, m_img.height); yy++)
	      for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, m_img.width); xx++)
		if ((xx != cx || yy != cy) /*&& !visited[yy * m_img.width + xx]*/ && m_color_class_map->get_class (xx, yy) == t)
		  {
		    int i;
		    for (i = 0; i < end; i++)
		      if (queue[i].x == xx && queue[i].y == yy)
			break;
		    if (i != end)
		      continue;
		    sum += get_data (xx,yy);
		    queue[end].x = xx;
		    queue[end].y = yy;
		    //visited[yy * m_img.width + xx] = 1;
		    end++;
		    if (end == max_patch_size)
		      goto done;
		  }
	    start++;
	  }
	if (end < min_patch_size)
	  continue;
	done:
	cdata[(int)t][y * m_img.width + x] = sum / end;
      }
  for (int color = 0; color < 3; color++)
    {
      for (int iteration = 0; iteration < 100; iteration ++)
	{
#pragma omp parallel for default(none) shared(color,tmp)
	  for (int y = 1; y < m_img.height - 1; y++)
	    for (int x = 1; x < m_img.width - 1; x++)
	      {
		if (m_color_class_map->get_class (x, y) == color)
		  {
		    tmp[y * m_img.width + x] = get_luminosity (color, x, y);
		    continue;
		  }
		luminosity_t lum = get_luminosity (color, x - 1, y) + get_luminosity (color, x + 1, y) + get_luminosity (color, x, y - 1) + get_luminosity (color, x, y + 1)
		  		   + (get_luminosity (color, x - 1, y - 1) + get_luminosity (color, x + 1, y - 1) + get_luminosity (color, x - 1, y + 1) + get_luminosity (color, x + 1, y + 1))
				   + get_luminosity (color, x, y);
		tmp[y * m_img.width + x] = lum * ((luminosity_t)1 / 9);
	      }
	  std::swap (tmp, cdata[color]);
	}
    }
  free (tmp);
}
render_scr_relax::~render_scr_relax()
{
  free (cdata[0]);
  free (cdata[1]);
  free (cdata[2]);
}
