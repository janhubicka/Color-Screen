#include <stdlib.h>
#include <sys/time.h>
#include "include/render-scr-detect.h"
#include "include/render-to-scr.h"
class distance_list distance_list;
static int stats = -1;

static inline void
putpixel (unsigned char *pixels, int pixelbytes, int rowstride, int x, int y,
       	  int r, int g, int b)
{
  *(pixels + y * rowstride + x * pixelbytes) = r;
  *(pixels + y * rowstride + x * pixelbytes + 1) = g;
  *(pixels + y * rowstride + x * pixelbytes + 2) = b;
  *(pixels + y * rowstride + x * pixelbytes + 3) = 255;
}

void
render_scr_detect::get_adjusted_data (rgbdata *data, coord_t x, coord_t y, int width, int height, coord_t pixelsize)
{ 
  downscale<render_scr_detect, rgbdata, &render_scr_detect::fast_get_adjusted_pixel, &render::account_rgb_pixel> (data, x, y, width, height, pixelsize);
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
	    render::rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
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
	    render::rgbdata *data = (rgbdata *)malloc (sizeof (rgbdata) * width * height);
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
    }
}
void
render_scr_detect::precompute_all ()
{
  m_color_class_map.allocate (m_img.width, m_img.height);
#pragma omp parallel for default(none) 
  for (int y = 0; y < m_img.height; y++)
    for (int x = 0; x < m_img.width; x++)
      m_color_class_map.set_class (x, y,
				   m_scr_detect.classify_color (m_img.rgbdata[y][x].r / (luminosity_t)m_img.maxval,
								m_img.rgbdata[y][x].g / (luminosity_t)m_img.maxval,
								m_img.rgbdata[y][x].b / (luminosity_t)m_img.maxval));
  render::precompute_all (false);
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
  //for (int i = 0; i < num; i++)
   //fprintf (stderr, "%i %i %f\n",list[i].x, list[i].y, list[i].fdist);
}
