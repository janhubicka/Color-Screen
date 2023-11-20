#include <assert.h>
#include "lru-cache.h"
#include "render-interpolate.h"

namespace {

struct analyzer_params
{
  uint64_t graydata_id;
  uint64_t screen_id;
  //int width, height, xshift, yshift;
  bool precise;
  luminosity_t collection_threshold;
  uint64_t mesh_trans_id;
  scr_to_img_parameters params;
  /* TODO: We can also do this on computed data.  */
  luminosity_t dark_point, exposure;

  image_data *img;
  screen *scr;
  render_to_scr *render;
  scr_to_img *scr_to_img_map;

  bool
  operator==(analyzer_params &o)
  {
    return graydata_id == o.graydata_id
	   && precise == o.precise
	   && (!precise || screen_id == o.screen_id)
	   /* TODO: Can be more fine grained.  */
	   && mesh_trans_id == o.mesh_trans_id
	   && dark_point == o.dark_point
	   && exposure == o.exposure
	   && (mesh_trans_id || params == o.params)
	   && (!precise || collection_threshold == o.collection_threshold);
  };
};

static analyze_dufay *
get_new_dufay_analysis (struct analyzer_params &p, int xshift, int yshift, int width, int height, progress_info *progress)
{
  analyze_dufay *ret = new analyze_dufay();
  if (ret->analyze (p.render, p.img, p.scr_to_img_map, p.scr, width, height, xshift, yshift, p.precise, p.collection_threshold, progress))
    return ret;
  delete ret;
  return NULL;
}

static analyze_paget *
get_new_paget_analysis (struct analyzer_params &p, int xshift, int yshift, int width, int height, progress_info *progress)
{
  analyze_paget *ret = new analyze_paget();
  if (ret->analyze (p.render, p.img, p.scr_to_img_map, p.scr, width, height, xshift, yshift, p.precise, p.collection_threshold, progress))
    return ret;
  delete ret;
  return NULL;
}
static lru_tile_cache <analyzer_params, analyze_dufay, get_new_dufay_analysis, 1> dufay_analyzer_cache ("dufay analyzer");
static lru_tile_cache <analyzer_params, analyze_paget, get_new_paget_analysis, 1> paget_analyzer_cache ("Paget analyzer");

}

render_interpolate::render_interpolate (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dst_maxval, bool screen_compensation, bool adjust_luminosity)
   : render_to_scr (param, img, rparam, dst_maxval), m_screen (NULL), m_screen_compensation (screen_compensation), m_adjust_luminosity (adjust_luminosity), m_dufay (NULL), m_paget (NULL)
{
}

bool
render_interpolate::precompute (coord_t xmin, coord_t ymin, coord_t xmax, coord_t ymax, progress_info *progress)
{
  uint64_t screen_id = 0;
  if (!render_to_scr::precompute (true, xmin, ymin, xmax, ymax, progress))
    return false;
  if (m_screen_compensation || m_params.precise)
    {
      coord_t radius = m_params.screen_blur_radius * pixel_size ();
      m_screen = get_screen (m_scr_to_img.get_type (), false, radius, progress, &screen_id);
      if (!m_screen)
	return false;
    }
  int xshift = -xmin;
  int yshift = -ymin;
  int width = xmax - xmin;
  int height = ymax - ymin;
  int xshift2, yshift2, width2, height2;
  m_scr_to_img.get_range (0, 0, m_img.width, m_img.height, &xshift2, &yshift2, &width2, &height2);
  if (xshift > xshift2)
     width -= xshift-xshift2, xshift = xshift2;
  if (yshift > yshift2)
     height -= yshift-yshift2, yshift = yshift2;
  if (width - xshift > width2 - xshift2)
     width = width2 - xshift2 + xshift;
  if (height - yshift > height2 - yshift2)
     height = height2 - yshift2 + yshift;
  /* For UI response, 
     it is better to compute whole image then significant portion of it.  */
  if (width * height > width2 * height2 / 2)
     {
       xshift = xshift2;
       yshift = yshift2;
       width = width2;
       height = height2;
     }
  /* We need to compute bit more to get interpolation right.
     TODO: figure out how much.  */
  xshift += 4;
  yshift += 4;
  width += 8;
  height += 8;
  struct analyzer_params p
    {
      m_gray_data_id,
      screen_id,
      //width, height, xshift, yshift,
      m_params.precise,
      m_params.collection_threshold,
      m_scr_to_img.get_param ().mesh_trans ? m_scr_to_img.get_param ().mesh_trans->id : 0,
      m_scr_to_img.get_param (),
      /* We use unadjusted data.  TODO: Implement also in fast.  */
      /*m_params.dark_point*/0,
      /*m_params.scan_exposure*/1,
      &m_img,
      m_screen,
      this,
      &m_scr_to_img,
    };
  if (m_scr_to_img.get_type () != Dufay)
    {
      m_paget = paget_analyzer_cache.get (p, xshift, yshift, width, height, progress);
      if (!m_paget)
	return false;
    }
  else
    {
      m_dufay = dufay_analyzer_cache.get (p, xshift, yshift, width, height, progress);
      if (!m_dufay)
	return false;
    }
  return !progress || !progress->cancelled ();
}

pure_attr flatten_attr rgbdata
render_interpolate::sample_pixel_scr (coord_t x, coord_t y)
{
  luminosity_t red, green, blue;
  int xshift, yshift;

  if (m_scr_to_img.get_type () != Dufay)
    {
      xshift = m_paget->get_xshift ();
      yshift = m_paget->get_yshift ();
      x += xshift;
      y += yshift;
      coord_t xx = 2*(x-0.25);
      coord_t yy = 2*(y-0.25);
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_blue(xx, yy) adjust_luminosity_ir (m_paget->blue (xp + (xx), yp + (yy)))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue

      coord_t xd, yd;
      analyze_paget::to_diagonal_coordinates (x, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);

#define get_green(xx, yy) adjust_luminosity_ir (m_paget->diag_green (xp + (xx), yp + (yy)))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      analyze_paget::to_diagonal_coordinates (x + 0.5, y, &xd, &yd);
      xo = my_modf (xd, &xp);
      yo = my_modf (yd, &yp);
#define get_red(xx, yy) adjust_luminosity_ir (m_paget->diag_red (xp + (xx), yp + (yy)))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
    }
  else
    {
      xshift = m_dufay->get_xshift ();
      yshift = m_dufay->get_yshift ();
      x += xshift;
      y += yshift;
      coord_t xx = 2*(x - 0.25);
      coord_t yy = y-0.5;
      int xp, yp;
      coord_t xo = my_modf (xx, &xp);
      coord_t yo = my_modf (yy, &yp);
#define get_red(xx, yy) adjust_luminosity_ir (m_dufay->red (xp + (xx), yp + (yy)))
      red = cubic_interpolate (cubic_interpolate (get_red (-1, -1), get_red (-1, 0), get_red (-1, 1), get_red (-1, 2), yo),
			       cubic_interpolate (get_red ( 0, -1), get_red ( 0, 0), get_red ( 0, 1), get_red ( 0, 2), yo),
			       cubic_interpolate (get_red ( 1, -1), get_red ( 1, 0), get_red ( 1, 1), get_red ( 1, 2), yo),
			       cubic_interpolate (get_red ( 2, -1), get_red ( 2, 0), get_red ( 2, 1), get_red ( 2, 2), yo), xo);
#undef get_red
      xx = x;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_green(xx, yy) adjust_luminosity_ir (m_dufay->green (xp + (xx), yp + (yy)))
      green = cubic_interpolate (cubic_interpolate (get_green (-1, -1), get_green (-1, 0), get_green (-1, 1), get_green (-1, 2), yo),
				 cubic_interpolate (get_green ( 0, -1), get_green ( 0, 0), get_green ( 0, 1), get_green ( 0, 2), yo),
				 cubic_interpolate (get_green ( 1, -1), get_green ( 1, 0), get_green ( 1, 1), get_green ( 1, 2), yo),
				 cubic_interpolate (get_green ( 2, -1), get_green ( 2, 0), get_green ( 2, 1), get_green ( 2, 2), yo), xo);
#undef get_green
      xx = x-0.5;
      yy = y;
      xo = my_modf (xx, &xp);
      yo = my_modf (yy, &yp);
#define get_blue(xx, yy) adjust_luminosity_ir (m_dufay->blue (xp + (xx), yp + (yy)))
      blue = cubic_interpolate (cubic_interpolate (get_blue (-1, -1), get_blue (-1, 0), get_blue (-1, 1), get_blue (-1, 2), yo),
				cubic_interpolate (get_blue ( 0, -1), get_blue ( 0, 0), get_blue ( 0, 1), get_blue ( 0, 2), yo),
				cubic_interpolate (get_blue ( 1, -1), get_blue ( 1, 0), get_blue ( 1, 1), get_blue ( 1, 2), yo),
				cubic_interpolate (get_blue ( 2, -1), get_blue ( 2, 0), get_blue ( 2, 1), get_blue ( 2, 2), yo), xo);
#undef get_blue
    }
  if (m_screen_compensation)
    {
      coord_t lum = get_img_pixel_scr (x - xshift, y - yshift);
      int ix = (uint64_t) nearest_int ((x - xshift) * screen::size) & (unsigned)(screen::size - 1);
      int iy = (uint64_t) nearest_int ((y - yshift) * screen::size) & (unsigned)(screen::size - 1);
      luminosity_t sr = m_screen->mult[iy][ix][0];
      luminosity_t sg = m_screen->mult[iy][ix][1];
      luminosity_t sb = m_screen->mult[iy][ix][2];

      red = std::max (red, (luminosity_t)0);
      green = std::max (green, (luminosity_t)0);
      blue = std::max (blue, (luminosity_t)0);

      luminosity_t llum = red * sr + green * sg + blue * sb;
      luminosity_t correction = llum ? lum / llum : lum * 100;

#if 1
      luminosity_t redmin = lum - (1 - sr);
      luminosity_t redmax = lum + (1 - sr);
      if (red * correction < redmin)
	correction = redmin / red;
      else if (red * correction > redmax)
	correction = redmax / red;

      luminosity_t greenmin = lum - (1 - sg);
      luminosity_t greenmax = lum + (1 - sg);
      if (green * correction < greenmin)
	correction = greenmin / green;
      else if (green * correction > greenmax)
	correction = greenmax / green;

      luminosity_t bluemin = lum - (1 - sb);
      luminosity_t bluemax = lum + (1 - sb);
      if (blue * correction < bluemin)
	correction = bluemin / blue;
      else if (blue * correction > bluemax)
	correction = bluemax / blue;
#endif
      correction = std::max (std::min (correction, (luminosity_t)5.0), (luminosity_t)0.0);

      return {red * correction, green * correction, blue * correction};
#if 0
      red = std::min (1.0, std::max (0.0, red));
      green = std::min (1.0, std::max (0.0, green));
      blue = std::min (1.0, std::max (0.0, blue));
      if (llum < 0.0001)
	llum = 0.0001;
      if (llum > 1)
	llum = 1;
      if (lum / llum > 2)
	lum = 2 * llum;
      //set_color_luminosity (red, green, blue, lum / llum * (red * rwght + green * gwght + blue * bwght), r, g, b);
      //set_color_luminosity (red, green, blue, lum / llum * (red + green + blue)*0.333, r, g, b);
#endif
    }
  else if (m_adjust_luminosity)
    {
      luminosity_t l = get_img_pixel_scr (x - xshift, y - yshift);
      luminosity_t red2, green2, blue2;
      m_color_matrix.apply_to_rgb (red, green, blue, &red2, &green2, &blue2);
      // TODO: We really should convert to XYZ and determine just Y.
      luminosity_t gr = (red2 * rwght + green2 * gwght + blue2 * bwght);
      if (gr <= 0.00001 || l <= 0.00001)
	red = green = blue = l;
      else
	{
	  gr = l / gr;
	  red2 *= gr;
	  green2 *= gr;
	  blue2 *= gr;
	}
      // TODO: Inverse color matrix can be stored.
      m_color_matrix.invert ().apply_to_rgb (red2, green2, blue2, &red, &green, &blue);
      return {red, green, blue};
    }
  else
    return {red, green, blue};
}

render_interpolate::~render_interpolate ()
{
  if (m_screen)
    release_screen (m_screen);
  if (m_dufay)
    dufay_analyzer_cache.release (m_dufay);
  if (m_paget)
    paget_analyzer_cache.release (m_paget);
}
void
render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  dufay_analyzer_cache.increase_capacity (n);
  paget_analyzer_cache.increase_capacity (n);
}
