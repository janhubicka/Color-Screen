#include <stdlib.h>
#include <sys/time.h>
#include "include/render-scr-detect.h"
#include "include/render-to-scr.h"
#include "lru-cache.h"
#include "sharpen.h"
struct color_data
{
  luminosity_t *m_data[3];
  int width;
  color_data (int width, int height);
  ~color_data ();
  luminosity_t get_luminosity (int color, int x, int y)
    {
      return m_data[color][y * width + x];
    }
};

color_data::color_data(int width, int height)
  : width(width)
{
  for (int color = 0; color < 3; color++)
    m_data[color] = (luminosity_t *)calloc (width * height, sizeof (luminosity_t));
}
color_data::~color_data()
{
  for (int i = 0; i < 3; i++)
    if (m_data[i])
      free (m_data[i]);
}

namespace
{
static int stats = -1;
/* Lookup table translates raw input data into linear values.  */
struct color_class_params
{
  uint64_t image_id;
  image_data *img;
  render_scr_detect::my_mem_rgbdata *precomputed_rgbdata;
  scr_detect_parameters p;
  scr_detect *d;
  luminosity_t gamma;

  bool
  operator==(color_class_params &o)
  {
    return image_id == o.image_id
	   && gamma == o.gamma
	   && p == o.p;
  }
};

color_class_map *get_color_class_map(color_class_params &p, progress_info *progress)
{
  image_data &img = *p.img;
  color_class_map *map = new color_class_map;
  map->allocate (img.width, img.height);
  //printf ("New color map\n");
  if (progress)
    progress->set_task ("computing screen", p.img->height);
#pragma omp parallel for default(none) shared(progress,img,map,p)
  for (int y = 0; y < img.height; y++)
    {
      if (!progress || !progress->cancel_requested ())
	{
	  if (p.precomputed_rgbdata)
	    for (int x = 0; x < img.width; x++)
	      map->set_class (x, y,
			      p.d->classify_adjusted_color (
				p.precomputed_rgbdata[y * img.width + x].red,
				p.precomputed_rgbdata[y * img.width + x].green,
				p.precomputed_rgbdata[y * img.width + x].blue));
	  else
	    for (int x = 0; x < img.width; x++)
	      map->set_class (x, y,
			      p.d->classify_color (img.rgbdata[y][x].r,
						   img.rgbdata[y][x].g,
						   img.rgbdata[y][x].b));
	}
       if (progress)
	 progress->inc_progress ();
    }
  if (progress && progress->cancelled ())
    {
      delete map;
      return NULL;
    }
  return map;
}
static lru_cache <color_class_params, color_class_map, get_color_class_map, 1> color_class_cache ("color tables");

/* Lookup table translates raw input data into linear values.  */
struct precomputed_rgbdata_params
{
  uint64_t image_id;
  scr_detect_parameters p;
  luminosity_t gamma;

  image_data *img;
  scr_detect *d;
  render_scr_detect *r;

  bool
  operator==(precomputed_rgbdata_params &o)
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

rgbdata
getdata_helper (render_scr_detect &r, int x, int y, int, int)
{
  return r.fast_get_adjusted_pixel (x, y);
}


render_scr_detect::my_mem_rgbdata *
get_precomputed_rgbdata(precomputed_rgbdata_params &p, progress_info *progress)
{
  render_scr_detect::my_mem_rgbdata *precomputed_rgbdata = (render_scr_detect::my_mem_rgbdata *)malloc (p.img->width * p.img->height * sizeof (render_scr_detect::my_mem_rgbdata));
  bool ok = true;
  if (!precomputed_rgbdata)
    return NULL;
  if (p.p.sharpen_radius > 0 && p.p.sharpen_amount > 0)
    ok = sharpen<rgbdata, render_scr_detect::my_mem_rgbdata, render_scr_detect &,int, getdata_helper> (precomputed_rgbdata, *p.r, 0, p.img->width, p.img->height, p.p.sharpen_radius, p.p.sharpen_amount, progress);
  else
    {
      if (progress)
	progress->set_task ("determining adjusted colors for screen detection", p.img->height);
#pragma omp parallel for default(none) shared(p,precomputed_rgbdata,progress)
      for (int y = 0; y < p.img->height; y++)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int x = 0; x < p.img->width; x++)
	      precomputed_rgbdata[y * p.img->width + x] = p.r->fast_nonprecomputed_get_adjusted_pixel (x, y);
	   if (progress)
	     progress->inc_progress ();
	}
    }
  if (!ok || (progress && progress->cancelled ()))
    {
      free (precomputed_rgbdata);
      return NULL;
    }
  return precomputed_rgbdata;
}
static lru_cache <precomputed_rgbdata_params, render_scr_detect::my_mem_rgbdata, get_precomputed_rgbdata, 1> precomputed_rgbdata_cache ("precomputed data");

/* Lookup table translates raw input data into linear values.  */
struct patches_cache_params
{
  uint64_t scr_map_id;
  uint64_t gray_data_id;
  color_class_map *map;
  image_data *img;
  render *r;

  /* TODO: render parameters affects luminosity.  */
  bool
  operator==(patches_cache_params &o)
  {
    return scr_map_id == o.scr_map_id
	   && gray_data_id == o.gray_data_id;
  }
};
/* TODO: progress info  */
patches *get_patches(patches_cache_params &p, progress_info *progress)
{
  patches *pat = new patches (*p.img, *p.r, *p.map, 16, progress);
  if (progress && progress->cancelled ())
    {
      delete pat;
      return NULL;
    }
  return pat;
}
static lru_cache <patches_cache_params, patches, get_patches, 1> patches_cache ("patches");

/* Lookup table translates raw input data into linear values.  */
struct color_data_params
{
  uint64_t color_class_map_id;
  uint64_t graydata_id;
  image_data *img;
  color_class_map *map;
  render *r;


  bool
  operator==(color_data_params &o)
  {
    return color_class_map_id == o.color_class_map_id
	   && graydata_id == o.graydata_id;
  }
};

/* Do relaxation and demosaik color data.  TODO:progress  */
static color_data *
get_new_color_data (struct color_data_params &p, progress_info *progress)
{
  color_data *data = new color_data (p.img->width, p.img->height);
  if (!data || !data->m_data[0] || !data->m_data[1] || !data->m_data[2])
    {
      if (data)
	delete data;
      return NULL;
    }
  const int max_patch_size = 8;
  const int min_patch_size = 1;
  if (progress)
    progress->set_task ("determining colors", p.img->height);
#pragma omp parallel for default(none) shared(p,data)
  for (int y = 0; y < p.img->height; y++)
    for (int x = 0; x < p.img->width; x++)
      {
	scr_detect::color_class t = p.map->get_class (x, y);
	if (t == scr_detect::unknown)
	  continue;
	struct queue {int x, y;} queue [max_patch_size];
	luminosity_t sum = p.r->get_data (x, y);
	int start = 0, end = 1;
	queue[0].x = x;
	queue[0].y = y;
	while (start < end)
	  {
	    int cx = queue[start].x;
	    int cy = queue[start].y;
	    for (int yy = std::max (cy - 1, 0); yy < std::min (cy + 2, p.img->height); yy++)
	      for (int xx = std::max (cx - 1, 0); xx < std::min (cx + 2, p.img->width); xx++)
		if ((xx != cx || yy != cy) /*&& !visited[yy * p.img->width + xx]*/ && p.map->get_class (xx, yy) == t)
		  {
		    int i;
		    for (i = 0; i < end; i++)
		      if (queue[i].x == xx && queue[i].y == yy)
			break;
		    if (i != end)
		      continue;
		    sum += p.r->get_data (xx, yy);
		    queue[end].x = xx;
		    queue[end].y = yy;
		    //visited[yy * p.img->width + xx] = 1;
		    end++;
		    if (end == max_patch_size)
		      goto done;
		  }
	    start++;
	  }
	if (end < min_patch_size)
	  continue;
	done:
	data->m_data[(int)t][y * p.img->width + x] = sum / end;
      }
  if (progress && progress->cancelled ())
    {
      delete data;
      return NULL;
    }
  luminosity_t *tmp = (luminosity_t *)malloc (p.img->width * p.img->height * sizeof (luminosity_t));
  if (progress)
    progress->set_task ("demosaiking", p.img->height * 100 * 3);
  for (int color = 0; color < 3; color++)
    {
      for (int iteration = 0; iteration < 100; iteration ++)
	{
#pragma omp parallel for default(none) shared(progress,color,tmp,data,p)
	  for (int y = 1; y < p.img->height - 1; y++)
	    {
	      if (!progress || !progress->cancel_requested ())
		for (int x = 1; x < p.img->width - 1; x++)
		  {
		    if (p.map->get_class (x, y) == color)
		      {
			tmp[y * p.img->width + x] = data->get_luminosity (color, x, y);
			continue;
		      }
		    luminosity_t lum = data->get_luminosity (color, x - 1, y) + data->get_luminosity (color, x + 1, y) + data->get_luminosity (color, x, y - 1) + data->get_luminosity (color, x, y + 1)
				       + (data->get_luminosity (color, x - 1, y - 1) + data->get_luminosity (color, x + 1, y - 1) + data->get_luminosity (color, x - 1, y + 1) + data->get_luminosity (color, x + 1, y + 1))
				       + data->get_luminosity (color, x, y);
		    tmp[y * p.img->width + x] = lum * ((luminosity_t)1 / 9);
		  }
	      if (progress)
		progress->inc_progress ();
	    }
	  std::swap (tmp, data->m_data[color]);
	  if (progress && progress->cancel_requested ())
	    break;
	}
      if (progress && progress->cancel_requested ())
	break;
    }
  free (tmp);
  if (progress && progress->cancelled ())
    {
      delete data;
      return NULL;
    }
  return data;
}
static lru_cache <color_data_params, color_data, get_new_color_data, 1> color_data_cache ("color data");
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
render_scr_detect::get_adjusted_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_adjusted_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
}

void
render_scr_detect::get_normalized_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_normalized_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
}

void
render_scr_detect::get_screen_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize, progress_info *progress)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_screen_pixel, &account_rgb_pixel> (data, x, y, width, height, pixelsize, progress);
}

bool
render_scr_detect::render_tile (enum render_scr_detect_type_t render_type,
			        scr_detect_parameters &param, image_data &img,
			        render_parameters &rparam, bool color,
			        unsigned char *pixels, int pixelbytes, int rowstride,
			        int width, int height,
			        double xoffset, double yoffset,
			        double step,
				progress_info *progress)
{
  if (width <= 0 || height <= 0)
    return true;
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  if (progress)
    progress->set_task ("precomputing", 1);
  switch (render_type)
    {
    case render_type_original:
      {
	render_parameters my_rparam;
	my_rparam.original_render_from (rparam, color);
	if (render_type == render_type_original && step > 1)
	  {
	    scr_to_img_parameters dummy;
	    return render_to_scr::render_tile (render::render_type_original, dummy, img, my_rparam, color, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step);
	  }
	scr_to_img_parameters dummy;
	render_img render (dummy, img, my_rparam, 255);
	if (color)
	  render.set_color_display ();
	if (!render.precompute_all (progress))
	  return false;

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.fast_render_pixel_img ((x + xoffset) * step, py, &r, &g,
						&b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_adjusted_color:
      {
	render_parameters my_rparam = rparam;
	my_rparam.color_model = render_parameters::color_model_none;
	my_rparam.presaturation = 1;
	my_rparam.saturation = 1;
	render_scr_detect render (param, img, my_rparam, 255);
	if (!render.precompute_all (false, false, progress))
	  return false;

	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_adjusted_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	    if (progress)
	      progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		if (!progress || !progress->cancel_requested ())
		  for (int x = 0; x < width; x++)
		    {
		      int r, g, b;
		      render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		      putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		    }
		if (progress)
		  progress->inc_progress ();
	      }
	    free (data);
	    break;
	  }

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_adjusted_pixel_img ((x + xoffset) * step, py, &r, &g,
						&b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_normalized_color:
      {
	render_parameters my_rparam = rparam;
	my_rparam.color_model = render_parameters::color_model_none;
	my_rparam.presaturation = 1;
	my_rparam.saturation = 1;
	my_rparam.brightness = 1;
	render_scr_detect render (param, img, my_rparam, 255);
	if (!render.precompute_all (false, false, progress))
	  return false;

	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_normalized_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	    if (progress)
	      progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		if (!progress || !progress->cancel_requested ())
		  for (int x = 0; x < width; x++)
		    {
		      int r, g, b;
		      render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		      putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		    }
		if (progress)
		  progress->inc_progress ();
	      }
	    free (data);
	    break;
	  }

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_normalized_pixel_img ((x + xoffset) * step, py, &r, &g,
						&b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_pixel_colors:
      {
	render_parameters my_rparam = rparam;
	my_rparam.color_model = render_parameters::color_model_none;
	my_rparam.presaturation = 1;
	my_rparam.saturation = 1;
	my_rparam.brightness = 1;
	render_scr_detect render (param, img, my_rparam, 255);
	if (!render.precompute_all (false, false, progress))
	  return false;
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_screen_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	    if (progress)
	      progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		if (!progress || !progress->cancel_requested ())
		  for (int x = 0; x < width; x++)
		    putpixel (pixels, pixelbytes, rowstride, x, y, data[x + width * y].red * 255 + 0.5, data[x + width * y].green * 255 + 0.5, data[x + width * y].blue * 255 + 0.5);
		if (progress)
		  progress->inc_progress ();
	      }
	    free (data);
	    break;
	  }
	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  coord_t px = (x + xoffset) * step;
		  luminosity_t r, g, b;
		  render.get_screen_color (px, py, &r, &g, &b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r * 255 + 0.5, g * 255 + 0.5, b * 255 + 0.5);
		}
	    if (progress)
	      progress->inc_progress ();
	   }
      }
      break;
    case render_type_realistic_scr:
      {
	render_scr_detect_superpose_img render (param, img, rparam, 255);
	if (!render.precompute_all (progress))
	  return false;
	if (step > 1)
	  {
	    rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
	    render.get_color_data (data, xoffset * step, yoffset * step, width, height, step, progress);
	    if (progress)
	      progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset,data)
	    for (int y = 0; y < height; y++)
	      {
		if (!progress || !progress->cancel_requested ())
		  for (int x = 0; x < width; x++)
		    {
		      int r, g, b;
		      render.set_color (data[x + width * y].red, data[x + width * y].green, data[x + width * y].blue, &r, &g, &b);
		      putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		    }
		if (progress)
		  progress->inc_progress ();
	      }
	    free (data);
	    break;
	  }

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_scr_nearest:
      {
	render_scr_nearest render (param, img, rparam, 255);
	if (!render.precompute_all (progress))
	  return false;

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_scr_nearest_scaled:
      {
	render_scr_nearest_scaled render (param, img, rparam, 255);
	if (!render.precompute_all (progress))
	  return false;

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    case render_type_scr_relax:
      {
	render_scr_relax render (param, img, rparam, 255);
	if (!render.precompute_all (progress))
	  return false;

	if (progress)
	  progress->set_task ("rendering", height);
#pragma omp parallel for default(none) shared(progress,pixels,render,pixelbytes,rowstride,height, width,step,yoffset,xoffset)
	for (int y = 0; y < height; y++)
	  {
	    coord_t py = (y + yoffset) * step;
	    if (!progress || !progress->cancel_requested ())
	      for (int x = 0; x < width; x++)
		{
		  int r, g, b;
		  render.render_pixel_img ((x + xoffset) * step, py, &r, &g, &b);
		  putpixel (pixels, pixelbytes, rowstride, x, y, r, g, b);
		}
	    if (progress)
	      progress->inc_progress ();
	  }
      }
      break;
    }
  return !progress || !progress->cancelled ();
}
bool
render_scr_detect::precompute_all (bool grayscale_needed, bool normalized_patches, progress_info *progress)
{
  if (m_scr_detect.m_param.sharpen_radius > 0 || m_scr_detect.m_param.sharpen_amount > 0)
    {
      if (!precompute_rgbdata (progress))
	return false;
    }
  color_class_params p = {m_precomputed_rgbdata ? m_precomputed_rgbdata_id : m_img.id, &m_img, m_precomputed_rgbdata, m_scr_detect.m_param, &m_scr_detect, m_params.gamma};
  m_color_class_map = color_class_cache.get (p, progress, &m_color_class_map_id);
  return render::precompute_all (grayscale_needed, normalized_patches, {1/3.0, 1/3.0, 1/3.0}, progress);
}

bool
render_scr_detect::precompute_rgbdata (progress_info *progress)
{
  if (m_precomputed_rgbdata)
    return true;
  struct precomputed_rgbdata_params p = {m_img.id, m_scr_detect.m_param, m_params.gamma, &m_img, &m_scr_detect, this};
  m_precomputed_rgbdata = precomputed_rgbdata_cache.get (p, progress, &m_precomputed_rgbdata_id);
  return m_precomputed_rgbdata;
}


render_scr_detect::~render_scr_detect ()
{
  color_class_cache.release (m_color_class_map);
  precomputed_rgbdata_cache.release (m_precomputed_rgbdata);
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

bool
render_scr_nearest_scaled::precompute_all (progress_info *progress)
{
  if (!render_scr_detect::precompute_all (true, false, progress))
    return false;
  patches_cache_params p = {m_color_class_map_id, m_gray_data_id, m_color_class_map, &m_img, this};
  m_patches = patches_cache.get (p, progress);
  return m_patches;
}

bool
render_scr_relax::precompute_all (progress_info *progress)
{
	/* TODO: Perhaps non-scaled relaxation makes sense. */
  if (!render_scr_detect::precompute_all (true, false, progress))
    return false;
  color_data_params p = {m_color_class_map_id, m_gray_data_id, &m_img, m_color_class_map, this};
  m_color_data_handle = color_data_cache.get (p, progress);
  if (!m_color_data_handle)
    return false;
  for (int color = 0; color < 3; color++)
    cdata[color] = m_color_data_handle->m_data[color];
  return true;
}
render_scr_relax::~render_scr_relax()
{
  color_data_cache.release (m_color_data_handle);
}
