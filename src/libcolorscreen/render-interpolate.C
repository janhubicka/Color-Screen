#include <assert.h>
#include <memory>
#include <limits>
#include "include/tiff-writer.h"
#include "lru-cache.h"
#include "include/dufaycolor.h"
#include "nmsimplex.h"
#include "include/stitch.h"
#include "include/finetune.h"
#include "render-interpolate.h"
namespace colorscreen
{
namespace
{

struct analyzer_params
{
  uint64_t img_id;
  uint64_t graydata_id;
  uint64_t screen_id;
  luminosity_t gamma;
  // int width, height, xshift, yshift;
  enum analyze_base::mode mode;
  luminosity_t collection_threshold;
  uint64_t mesh_trans_id;
  scr_to_img_parameters params;

  const image_data *img;
  const screen *scr;
  render_to_scr *render;
  scr_to_img *scr_to_img_map;

  bool
  operator== (analyzer_params &o)
  {
    if (mode != o.mode || mesh_trans_id != o.mesh_trans_id
        || (!mesh_trans_id && params != o.params)
	|| params.type != o.params.type)
      return false;
    if (mode == analyze_base::color || mode == analyze_base::precise_rgb)
      {
        if (img_id != o.img_id || gamma != o.gamma)
          return false;
      }
    else if (graydata_id != o.graydata_id)
      return false;
    if (mode == analyze_base::fast || mode == analyze_base::color)
      return true;
    return screen_id == o.screen_id
           && collection_threshold == o.collection_threshold;
  };
};

static analyze_dufay *
get_new_dufay_analysis (struct analyzer_params &p, int xshift, int yshift,
                        int width, int height, progress_info *progress)
{
  analyze_dufay *ret = new analyze_dufay ();
  {
    const screen *s = p.scr;
    screen adapted;
    if (!p.scr)
      ;
    else if (p.params.type == DioptichromeB)
      {
	s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
	    {
	      adapted.mult[y][x][0] = p.scr->mult[y][x][1];
	      adapted.mult[y][x][1] = p.scr->mult[y][x][0];
	      adapted.mult[y][x][2] = p.scr->mult[y][x][2];
	    }
      }
    else if (p.params.type == ImprovedDioptichromeB)
      {
	s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
	    {
	      adapted.mult[y][x][0] = p.scr->mult[y][x][2];
	      adapted.mult[y][x][1] = p.scr->mult[y][x][1];
	      adapted.mult[y][x][2] = p.scr->mult[y][x][0];
	    }
      }
    if (ret->analyze (p.render, p.img, p.scr_to_img_map, s, width, height,
		      xshift, yshift, p.mode, p.collection_threshold, progress))
      return ret;
  }
  delete ret;
  return NULL;
}

static analyze_paget *
get_new_paget_analysis (struct analyzer_params &p, int xshift, int yshift,
                        int width, int height, progress_info *progress)
{
  analyze_paget *ret = new analyze_paget ();
  if (ret->analyze (p.render, p.img, p.scr_to_img_map, p.scr, width, height,
                    xshift, yshift, p.mode, p.collection_threshold, progress))
    return ret;
  delete ret;
  return NULL;
}
static analyze_strips *
get_new_strips_analysis (struct analyzer_params &p, int xshift, int yshift,
                        int width, int height, progress_info *progress)
{
  analyze_strips *ret = new analyze_strips ();
  {
    const screen *s = p.scr;
    screen adapted;
    if (!p.scr)
      ;
    else if (p.params.type == Joly)
      {
	s = &adapted;
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
	    {
	      adapted.mult[y][x][0] = p.scr->mult[y][x][2];
	      adapted.mult[y][x][1] = p.scr->mult[y][x][1];
	      adapted.mult[y][x][2] = p.scr->mult[y][x][0];
	    }
      }
    if (ret->analyze (p.render, p.img, p.scr_to_img_map, s, width, height,
		      xshift, yshift, p.mode, p.collection_threshold, progress))
      return ret;
  }
  delete ret;
  return NULL;
}

static lru_tile_cache<analyzer_params, analyze_dufay, analyze_dufay *,
                      get_new_dufay_analysis, 1>
    dufay_analyzer_cache ("dufay analyzer");
static lru_tile_cache<analyzer_params, analyze_paget, analyze_paget *,
                      get_new_paget_analysis, 1>
    paget_analyzer_cache ("Paget analyzer");
static lru_tile_cache<analyzer_params, analyze_strips, analyze_strips *,
                      get_new_strips_analysis, 1>
    strips_analyzer_cache ("Strips analyzer");

}

render_interpolate::render_interpolate (scr_to_img_parameters &param,
                                        image_data &img,
                                        render_parameters &rparam,
                                        int dst_maxval)
    : render_to_scr (param, img, rparam, dst_maxval), m_screen (NULL),
      m_screen_compensation (false), m_adjust_luminosity (false),
      m_original_color (false), m_unadjusted (false), m_profiled (false),
      m_precise_rgb (false), m_dufay (NULL), m_paget (NULL), m_strips (NULL)
{
}
void
render_interpolate::set_render_type (render_type_parameters rtparam)
{
  m_adjust_luminosity = (rtparam.type == render_type_combined);
  m_screen_compensation = (rtparam.type == render_type_predictive);
  if (rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original)
    original_color (rtparam.type
                    == render_type_interpolated_profiled_original);
}

rgbdata
render_interpolate::compensate_saturation_loss_scr (point_t p, rgbdata c) const
{
  if (!m_saturation_loss_table)
    {
      m_saturation_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red,
                                        &c.green, &c.blue);
      return c;
    }
  return m_saturation_loss_table->compensate_saturation_loss_img (
      m_scr_to_img.to_img (p), c);
}

rgbdata
render_interpolate::compensate_saturation_loss_img (point_t p, rgbdata c) const
{
  if (!m_saturation_loss_table)
    {
      m_saturation_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red,
                                        &c.green, &c.blue);
      return c;
    }
  return m_saturation_loss_table->compensate_saturation_loss_img (p, c);
}

bool
render_interpolate::precompute (coord_t xmin, coord_t ymin, coord_t xmax,
                                coord_t ymax, progress_info *progress)
{
  uint64_t screen_id = 0;
  if (m_scr_to_img_param.type == Random)
    return false;
  // printf ("Precomputing %f %f %f %f\n",xmin,ymin,xmax,ymax);
  /* When doing profiled matrix, we need to pre-scale the profile so black
     point corretion goes right. Without doing so, for exmaple black from red
     pixels would be subtracted too agressively, since we account for every
     pixel in image, not only red patch portion.  */
  if (!render_to_scr::precompute (!m_original_color && !m_precise_rgb,
                                  !m_original_color || m_profiled, xmin, ymin,
                                  xmax, ymax, progress))
    return false;
  if (m_screen_compensation || m_params.precise || m_precise_rgb)
    {
      coord_t radius = m_params.screen_blur_radius * pixel_size ();
      m_screen = get_screen (m_scr_to_img.get_type (), false, radius,
                             m_params.red_strip_width,
                             m_params.green_strip_width, progress,
                             &screen_id);
      if (!m_screen)
        return false;
      if (!m_original_color && !m_precise_rgb)
        {
          if (m_params.scanner_blur_correction)
            compute_saturation_loss_table (
                m_screen, screen_id, m_params.collection_threshold, m_params.sharpen_radius, m_params.sharpen_amount, progress);
          else
            {
              rgbdata cred, cgreen, cblue;
              if (determine_color_loss (
                      &cred, &cgreen, &cblue, *m_screen, *m_screen,
                      m_params.collection_threshold, m_params.sharpen_radius, m_params.sharpen_amount, m_scr_to_img,
                      m_img.width / 2 - 100, m_img.height / 2 - 100,
                      m_img.width / 2 + 100, m_img.height / 2 + 100))
                {
                  color_matrix sat (cred.red, cgreen.red, cblue.red,
                                    0, //
                                    cred.green, cgreen.green, cblue.green,
                                    0, //
                                    cred.blue, cgreen.blue, cblue.blue,
                                    0, //
                                    0, 0, 0, 1);
                  m_saturation_matrix = sat.invert ();
                }
            }
        }
    }
  int xshift = -xmin;
  int yshift = -ymin;
  int width = xmax - xmin;
  int height = ymax - ymin;
  int xshift2, yshift2, width2, height2;
  m_scr_to_img.get_range (0, 0, m_img.width, m_img.height, &xshift2, &yshift2,
                          &width2, &height2);
  if (xshift > xshift2)
    width -= xshift - xshift2, xshift = xshift2;
  if (yshift > yshift2)
    height -= yshift - yshift2, yshift = yshift2;
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
  xshift += 5;
  yshift += 5;
  width += 9;
  height += 9;
  struct analyzer_params p
  {
    m_img.id, m_gray_data_id, screen_id, m_params.gamma,
        m_original_color
            ? analyze_base::/*color*/ precise_rgb
            : (m_precise_rgb ? analyze_base::precise_rgb
                             : (!m_params.precise ? analyze_base::fast
                                                  : analyze_base::precise)),
        m_params.collection_threshold,
        m_scr_to_img.get_param ().mesh_trans
            ? m_scr_to_img.get_param ().mesh_trans->id
            : 0,
        m_scr_to_img.get_param (), &m_img, m_screen, this, &m_scr_to_img,
  };
  if (paget_like_screen_p (m_scr_to_img.get_type ()))
    {
      m_paget = paget_analyzer_cache.get (p, xshift, yshift, width, height,
                                          progress);
      if (!m_paget)
        return false;
    }
  else if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      m_dufay = dufay_analyzer_cache.get (p, xshift, yshift, width, height,
                                          progress);
      if (!m_dufay)
        return false;
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      m_strips = strips_analyzer_cache.get (p, xshift, yshift, width, height,
                                           progress);
      if (!m_strips)
        return false;
    }
  else
    return false;
  m_interpolation_proportions = m_scr_to_img.patch_proportions (&m_params);
  if (!m_original_color)
    {
      if (m_scr_to_img.get_type () == DioptichromeB)
        std::swap (m_interpolation_proportions.red, m_interpolation_proportions.green);
      else if (m_scr_to_img.get_type () == ImprovedDioptichromeB)
        std::swap (m_interpolation_proportions.red, m_interpolation_proportions.blue);
      else if (m_scr_to_img.get_type () == Joly)
        std::swap (m_interpolation_proportions.red, m_interpolation_proportions.blue);
    }
  return !progress || !progress->cancelled ();
}

pure_attr rgbdata
render_interpolate::sample_pixel_scr (coord_t x, coord_t y) const
{
  rgbdata c;

  if (paget_like_screen_p (m_scr_to_img.get_type ()))
    c = m_paget->bicubic_interpolate (
        { x, y }, m_interpolation_proportions);
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      c = m_strips->bicubic_interpolate (
          { x, y }, m_interpolation_proportions);
      if (m_original_color)
	;
      else if (m_scr_to_img.get_type () == Joly)
        std::swap (c.red, c.blue);
    }
  else
    {
      c = m_dufay->bicubic_interpolate (
          { x, y }, m_interpolation_proportions);
      if (m_original_color)
	;
      else if (m_scr_to_img.get_type () == DioptichromeB)
        std::swap (c.red, c.green);
      else if (m_scr_to_img.get_type () == ImprovedDioptichromeB)
        std::swap (c.red, c.blue);
    }
  if (!m_original_color)
    c = compensate_saturation_loss_scr ({ x, y }, c);
  if (m_unadjusted)
    ;
  else if (!m_original_color)
    {
      c.red = adjust_luminosity_ir (c.red);
      c.green = adjust_luminosity_ir (c.green);
      c.blue = adjust_luminosity_ir (c.blue);
    }
  else if (m_profiled)
    {
      profile_matrix.apply_to_rgb (c.red, c.green, c.blue, &c.red, &c.green,
                                   &c.blue);
      c.red = adjust_luminosity_ir (c.red);
      c.green = adjust_luminosity_ir (c.green);
      c.blue = adjust_luminosity_ir (c.blue);
    }
  else
    c = adjust_rgb (c);
  if (m_screen_compensation)
    {
      coord_t lum = get_img_pixel_scr (x, y);
      int ix = (uint64_t)nearest_int ((x)*screen::size)
               & (unsigned)(screen::size - 1);
      int iy = (uint64_t)nearest_int ((y)*screen::size)
               & (unsigned)(screen::size - 1);
      luminosity_t sr = m_screen->mult[iy][ix][0];
      luminosity_t sg = m_screen->mult[iy][ix][1];
      luminosity_t sb = m_screen->mult[iy][ix][2];

      c.red = std::max (c.red, (luminosity_t)0);
      c.green = std::max (c.green, (luminosity_t)0);
      c.blue = std::max (c.blue, (luminosity_t)0);

      luminosity_t llum = c.red * sr + c.green * sg + c.blue * sb;
      luminosity_t correction = llum ? lum / llum : lum * 100;

      luminosity_t redmin = lum - (1 - sr);
      luminosity_t redmax = lum + (1 - sr);
      if (c.red * correction < redmin)
        correction = redmin / c.red;
      else if (c.red * correction > redmax)
        correction = redmax / c.red;

      luminosity_t greenmin = lum - (1 - sg);
      luminosity_t greenmax = lum + (1 - sg);
      if (c.green * correction < greenmin)
        correction = greenmin / c.green;
      else if (c.green * correction > greenmax)
        correction = greenmax / c.green;

      luminosity_t bluemin = lum - (1 - sb);
      luminosity_t bluemax = lum + (1 - sb);
      if (c.blue * correction < bluemin)
        correction = bluemin / c.blue;
      else if (c.blue * correction > bluemax)
        correction = bluemax / c.blue;
      correction = std::max (std::min (correction, (luminosity_t)5.0),
                             (luminosity_t)0.0);

      return c * correction;
    }
  else if (m_adjust_luminosity)
    {
      luminosity_t l = get_img_pixel_scr (x, y);
      luminosity_t red2, green2, blue2;
      m_color_matrix.apply_to_rgb (c.red, c.green, c.blue, &red2, &green2,
                                   &blue2);
      // TODO: We really should convert to XYZ and determine just Y.
      luminosity_t gr = (red2 * rwght + green2 * gwght + blue2 * bwght);
      if (gr <= 0.00001 || l <= 0.00001)
        red2 = green2 = blue2 = l;
      else
        {
          gr = l / gr;
          red2 *= gr;
          green2 *= gr;
          blue2 *= gr;
        }
      // TODO: Inverse color matrix can be stored.
      m_color_matrix.invert ().apply_to_rgb (red2, green2, blue2, &c.red,
                                             &c.green, &c.blue);
      return c;
    }
  else
    return c;
}

render_interpolate::~render_interpolate ()
{
  if (m_screen)
    release_screen (m_screen);
  if (m_dufay)
    dufay_analyzer_cache.release (m_dufay);
  if (m_paget)
    paget_analyzer_cache.release (m_paget);
  if (m_strips)
    strips_analyzer_cache.release (m_strips);
}
void
render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  /* Triple size, since we have 3 modes.  */
  dufay_analyzer_cache.increase_capacity (3 * n);
  paget_analyzer_cache.increase_capacity (3 * n);
  strips_analyzer_cache.increase_capacity (3 * n);
}

/* Compute RGB data of downscaled image.  */
void
render_interpolate::get_color_data (rgbdata *data, coord_t x, coord_t y,
                                    int width, int height, coord_t pixelsize,
                                    progress_info *progress)
{
  downscale<render_interpolate, rgbdata, &render_interpolate::sample_pixel_img,
            &account_rgb_pixel> (data, x, y, width, height, pixelsize,
                                 progress);
}

/* Run ANALYZE on every screen point in the given (image) range, pass infared
   value of tile color. Rendering must be initialized in precise mode from
   infrared channel.  */
bool
render_interpolate::analyze_patches (analyzer analyze, const char *task,
                                     bool screen, int xmin, int xmax, int ymin,
                                     int ymax, progress_info *progress)
{
  assert (!m_precise_rgb && !m_original_color);
  if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_dufay->get_height ());
      for (int y = 0; y < m_dufay->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_dufay->get_width (); x++)
              {
                coord_t xs = x - m_dufay->get_xshift (),
                        ys = y - m_dufay->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata c = m_dufay->screen_tile_color (x, y);
		if (m_scr_to_img.get_type () == DioptichromeB)
		  std::swap (c.red, c.green);
		else if (m_scr_to_img.get_type () == ImprovedDioptichromeB)
		  std::swap (c.red, c.blue);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_strips->get_height ());
      for (int y = 0; y < m_strips->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_strips->get_width (); x++)
              {
                coord_t xs = x - m_strips->get_xshift (),
                        ys = y - m_strips->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata c = m_strips->screen_tile_color (x, y);
		if (m_scr_to_img.get_type () == Joly)
		  std::swap (c.blue, c.red);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else
    {
      if (progress)
        progress->set_task (task, m_paget->get_height ());
      for (int y = 0; y < m_paget->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_paget->get_width (); x++)
              {
                coord_t xs = x - m_paget->get_xshift (),
                        ys = y - m_paget->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata c = m_paget->screen_tile_color (x, y);
                c = compensate_saturation_loss_scr ({ xs, ys }, c);
                if (!analyze (xs, ys, c))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  return !progress || !progress->cancelled ();
}
/* Run ANALYZE on every screen point in the given (image) range, pass RGB value
   of every tile color. Rendering must be initialized in precise_rgb mode from
   infrared channel.  */
bool
render_interpolate::analyze_rgb_patches (rgb_analyzer analyze,
                                         const char *task, bool screen,
                                         int xmin, int xmax, int ymin,
                                         int ymax, progress_info *progress)
{
  assert (m_precise_rgb && !m_original_color);
  if (dufay_like_screen_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_dufay->get_height ());
      for (int y = 0; y < m_dufay->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_dufay->get_width (); x++)
              {
                coord_t xs = x - m_dufay->get_xshift (),
                        ys = y - m_dufay->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata r, g, b;
                m_dufay->screen_tile_rgb_color (r, g, b, x, y);
		if (m_scr_to_img.get_type () == DioptichromeB)
		  std::swap (r, g);
		else if (m_scr_to_img.get_type () == ImprovedDioptichromeB)
		  std::swap (r, b);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else if (screen_with_vertical_strips_p (m_scr_to_img.get_type ()))
    {
      if (progress)
        progress->set_task (task, m_strips->get_height ());
      for (int y = 0; y < m_strips->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_strips->get_width (); x++)
              {
                coord_t xs = x - m_strips->get_xshift (),
                        ys = y - m_strips->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata r, g, b;
		if (m_scr_to_img.get_type () == Joly)
		  std::swap (b, r);
                m_strips->screen_tile_rgb_color (r, g, b, x, y);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  else
    {
      if (progress)
        progress->set_task (task, m_paget->get_height ());
      for (int y = 0; y < m_paget->get_height (); y++)
        {
          if (!progress || !progress->cancel_requested ())
            for (int x = 0; x < m_paget->get_width (); x++)
              {
                coord_t xs = x - m_paget->get_xshift (),
                        ys = y - m_paget->get_yshift ();
                if (screen
                    && (xs < xmin || ys < ymin || xs > xmax || ys > ymax))
                  continue;
                if (!screen)
                  {
                    point_t imgp = m_scr_to_img.to_img ({ xs, ys });
                    if (!screen
                        && (imgp.x < xmin || imgp.y < ymin || imgp.x > xmax
                            || imgp.y > ymax))
                      continue;
                  }
                rgbdata r, g, b;
                m_paget->screen_tile_rgb_color (r, g, b, x, y);
                if (!analyze (xs, ys, r, g, b))
                  return false;
              }
          if (progress)
            progress->inc_progress ();
        }
    }
  return !progress || !progress->cancelled ();
}

bool
render_interpolate::dump_patch_density (FILE *out)
{
  if (!m_strips)
    return m_strips->dump_patch_density (out);
  if (!m_paget)
    return m_paget->dump_patch_density (out);

  fprintf (stderr, "Unsuported screen format\n");
  return false;
}

/* Cool ANALYZE with unadjusted luminosity of every screen tile in range
   xmin,ymin,xmax,ymmax. For normal images range can be either in image or
   screen coordinates (specified for screen). For stitch project image is
   always in final coordinates.  */
bool
analyze_patches (analyzer analyze, const char *task, image_data &img,
                 render_parameters &rparam, scr_to_img_parameters &param,
                 bool screen, int xmin, int xmax, int ymin, int ymax,
                 progress_info *progress)
{
  if (img.stitch)
    {
      stitch_project &stitch = *img.stitch;
      xmin += img.xmin;
      ymin += img.ymin;
      xmax += img.xmin;
      ymax += img.ymin;
      if (progress)
        progress->set_task ("searching for tiles", 1);
      /* It is easy to add support for screen coordinates if needed.  */
      assert (!screen);
      std::vector<stitch_project::tile_range> ranges
          = stitch.find_ranges (xmin, xmax, ymin, ymax, true, true);
      if (progress)
        progress->set_task (task, ranges.size ());
      for (auto r : ranges)
        {
          int tx = r.tile_x;
          int ty = r.tile_y;
          image_data &tile = *stitch.images[ty][tx].img;
	  render_parameters my_rparam = rparam;
	  rparam.get_tile_adjustment (&stitch, tx, ty).apply (&my_rparam);
          int stack = 0;
          if (progress)
            stack = progress->push ();
          if (!analyze_patches (
                  [&] (coord_t tsx, coord_t tsy, rgbdata c) {
                    int ttx, tty;
                    point_t src = stitch.images[ty][tx].img_scr_to_common_scr (
                        { tsx, tsy });
                    point_t pfin = stitch.common_scr_to_img.scr_to_final (src);
                    if (pfin.x < xmin || pfin.y < ymin || pfin.x > xmax
                        || pfin.y > ymax
                        || !stitch.tile_for_scr (&my_rparam, src.x, src.y, &ttx,
                                                 &tty, true)
                        || ttx != tx || tty != ty)
                      return true;
                    return analyze (pfin.x - img.xmin, pfin.y - img.ymin, c);
                  },
                  "analyzing tile", tile, rparam, stitch.images[ty][tx].param,
                  true, r.xmin, r.xmax, r.ymin, r.ymax, progress))
            {
              if (progress)
                progress->pop (stack);
              return false;
            }
          if (progress)
            progress->pop (stack);
          if (progress)
            progress->inc_progress ();
        }
      return true;
    }
  render_interpolate render (param, img, rparam, 256);
  render.set_unadjusted ();
  if (!screen)
    {
      if (!render.precompute_img_range (xmin, ymin, xmax, ymax, progress))
        return false;
    }
  else
    {
      if (!render.precompute (xmin, ymin, xmax, ymax, progress))
        // if (!render.precompute_img_range (0, 0, img.width, img.height,
        // progress))
        return false;
    }
  if (progress && progress->cancel_requested ())
    return false;
  return render.analyze_patches (analyze, task, screen, xmin, xmax, ymin, ymax,
                                 progress);
}

/* Cool ANALYZE with unadjusted RGB value of every screen tile in range
   xmin,ymin,xmax,ymmax. For normal images range can be either in image or
   screen coordinates (specified for screen). For stitch project image is
   always in final coordinates.  */
bool
analyze_rgb_patches (rgb_analyzer analyze, const char *task, image_data &img,
                     render_parameters &rparam, scr_to_img_parameters &param,
                     bool screen, int xmin, int xmax, int ymin, int ymax,
                     progress_info *progress)
{
  if (img.stitch)
    {
      stitch_project &stitch = *img.stitch;
      xmin += img.xmin;
      ymin += img.ymin;
      xmax += img.xmin;
      ymax += img.ymin;
      /* It is easy to add support for screen coordinates if needed.  */
      assert (!screen);
      if (progress)
        progress->set_task ("searching for tiles", 1);
      std::vector<stitch_project::tile_range> ranges
          = stitch.find_ranges (xmin, xmax, ymin, ymax, true, true);
      if (progress)
        progress->set_task (task, ranges.size ());
      for (auto r : ranges)
        {
          int tx = r.tile_x;
          int ty = r.tile_y;
          image_data &tile = *stitch.images[ty][tx].img;
	  render_parameters my_rparam = rparam;
	  rparam.get_tile_adjustment (&stitch, tx, ty).apply (&my_rparam);
          int stack = 0;
          if (progress)
            stack = progress->push ();
          if (!analyze_rgb_patches (
                  [&] (coord_t tsx, coord_t tsy, rgbdata r, rgbdata g,
                       rgbdata b) {
                    int ttx, tty;
                    point_t src = stitch.images[ty][tx].img_scr_to_common_scr (
                        { tsx, tsy });
                    point_t pfin = stitch.common_scr_to_img.scr_to_final (src);
                    // printf ("tile %i %i tilescreen %f %f screen %f %f final
                    // %f %f range %i:%i %i:%i\n",tx,ty,
                    // tsx,tsy,src.x,src.y,fx,fy,xmin,xmax,ymin,ymax);
                    if (pfin.x < xmin || pfin.y < ymin || pfin.x > xmax
                        || pfin.y > ymax
                        || !stitch.tile_for_scr (&my_rparam, src.x, src.y, &ttx,
                                                 &tty, true)
                        || ttx != tx || tty != ty)
                      return true;
                    return analyze (pfin.x - img.xmin, pfin.y - img.ymin, r, g,
                                    b);
                  },
                  "analyzing tile", tile, rparam, stitch.images[ty][tx].param,
                  true, r.xmin, r.xmax, r.ymin, r.ymax, progress))
            {
              if (progress)
                progress->pop (stack);
              return false;
            }
          if (progress)
            progress->pop (stack);
          if (progress)
            progress->inc_progress ();
        }
      return true;
    }
  render_interpolate render (param, img, rparam, 256);
  render.set_precise_rgb ();
  render.set_unadjusted ();
  // printf ("Screen %i\n",screen);
  if (!screen)
    {
      if (!render.precompute_img_range (xmin, ymin, xmax, ymax, progress))
        return false;
    }
  else
    {
      // if (!render.precompute_img_range (0, 0, img.width, img.height,
      // progress))
      if (!render.precompute (xmin, ymin, xmax, ymax, progress))
        return false;
    }
  if (progress && progress->cancel_requested ())
    return false;
  return render.analyze_rgb_patches (analyze, task, screen, xmin, xmax, ymin,
                                     ymax, progress);
}

bool
dump_patch_density (FILE *out, image_data &scan, scr_to_img_parameters &param,
                    render_parameters &rparam, progress_info *progress)
{
  render_interpolate render (param, scan, rparam, 256);
  if (!render.precompute_all (progress))
    return false;
  return render.dump_patch_density (out);
}

static double
get_deltae (xyz fc1, xyz fc2, long *cln = NULL, xyz *mins = NULL, xyz *maxs = NULL)
{
  xyz bfc1 = fc1, bfc2 = fc2;
  if (mins)
    {
      if (fc1.x < mins->x)
	mins->x = fc1.x;
      if (fc1.y < mins->y)
	mins->y = fc1.y;
      if (fc1.z < mins->z)
	mins->z = fc1.z;
      if (fc2.x < mins->x)
	mins->x = fc2.x;
      if (fc2.y < mins->y)
	mins->y = fc2.y;
      if (fc2.z < mins->z)
	mins->z = fc2.z;
      if (fc1.x < maxs->x)
	maxs->x = fc1.x;
      if (fc1.y < maxs->y)
	maxs->y = fc1.y;
      if (fc1.z < maxs->z)
	maxs->z = fc1.z;
      if (fc2.x < maxs->x)
	maxs->x = fc2.x;
      if (fc2.y < maxs->y)
	maxs->y = fc2.y;
      if (fc2.z < maxs->z)
	maxs->z = fc2.z;
    }
  /* DeltaE is meaningfully defined only in the range of xyz.  */
  bool cl = false;
  if (fc1.x < 0)
    fc1.x = 0, cl = true;
  if (fc1.x > 1)
    fc1.x = 1, cl = true;
  if (fc1.y < 0)
    fc1.y = 0, cl = true;
  if (fc1.y > 1)
    fc1.y = 1, cl = true;
  if (fc1.z < 0)
    fc1.z = 0, cl = true;
  if (fc1.z > 1)
    fc1.z = 1, cl = true;
  if (fc2.x < 0)
    fc2.x = 0, cl = true;
  if (fc2.x > 1)
    fc2.x = 1, cl = true;
  if (fc2.y < 0)
    fc2.y = 0, cl = true;
  if (fc2.y > 1)
    fc2.y = 1, cl = true;
  if (fc2.z < 0)
    fc2.z = 0, cl = true;
  if (fc2.z > 1)
    fc2.z = 1, cl = true;
  if (cl && cln)
    {
#if 0
      printf ("Clamping\n");
      bfc1.print (stdout);
      fc1.print (stdout);
      bfc2.print (stdout);
      fc2.print (stdout);
#endif
      (*cln)++;
    }
  cie_lab cc1(fc1, srgb_white);
  cie_lab cc2(fc2, srgb_white);
  luminosity_t delta = deltaE2000(cc1, cc2);
  if (!(delta >= 0))
    abort ();
  return delta;
}

bool
compare_deltae (image_data &img, scr_to_img_parameters &param1, render_parameters &rparam1, scr_to_img_parameters &param2, render_parameters &rparam2, const char *cmpname, double *ret_avg, double *ret_max, progress_info *progress)
{
  rparam1.output_profile = render_parameters::output_profile_xyz;
  rparam2.output_profile = render_parameters::output_profile_xyz;
  rparam1.observer_whitepoint = srgb_white;
  rparam2.observer_whitepoint = srgb_white;
  render_interpolate render1 (param1, img, rparam1, 256);
  render_interpolate render2 (param2, img, rparam2, 256);
  if (!render1.precompute_all (progress)
      || !render2.precompute_all (progress))
    return false;
  int border = 100;
  xyz mins = {1,1,1};
  xyz maxs = {0,0,0};
  long n = 0;
  long cln = 0;
  xyz fc1, fc2;

  render1.set_linear_hdr_color (1, 0, 0, &fc1.x, &fc1.y, &fc1.z);
  render2.set_linear_hdr_color (1, 0, 0, &fc2.x, &fc2.y, &fc2.z);
  if (progress)
    progress->pause_stdout ();
  printf ("Red primary 1: ");
  fc1.print (stdout);
  printf ("Red primary 2: ");
  fc2.print (stdout);
  printf ("Red primary deltaE 2000: %f\n", get_deltae (fc1, fc2));

  render1.set_linear_hdr_color (0, 1, 0, &fc1.x, &fc1.y, &fc1.z);
  render2.set_linear_hdr_color (0, 1, 0, &fc2.x, &fc2.y, &fc2.z);
  if (progress)
    progress->pause_stdout ();
  printf ("Green primary 1: ");
  fc1.print (stdout);
  printf ("Green primary 2: ");
  fc2.print (stdout);
  printf ("Green primary deltaE 2000: %f\n", get_deltae (fc1, fc2));

  render1.set_linear_hdr_color (0, 0, 1, &fc1.x, &fc1.y, &fc1.z);
  render2.set_linear_hdr_color (0, 0, 1, &fc2.x, &fc2.y, &fc2.z);
  if (progress)
    progress->pause_stdout ();
  printf ("Blue primary 1: ");
  fc1.print (stdout);
  printf ("Blue primary 2: ");
  fc2.print (stdout);
  printf ("Blue primary deltaE 2000: %f\n", get_deltae (fc1, fc2));
  if (progress)
    progress->resume_stdout ();

  int step = 4;
  if (progress)
    progress->set_task ("comparing", (img.height - 2 * border) / step * 2);
  histogram deltaE;
  luminosity_t sum = 0;
//#pragma omp parallel for default(none) shared(progress,img,render1, redner2,mins,maxs,sum) 
  for (int y = border; y < img.height - border; y+=step)
    {
      if (!progress || !progress->cancel_requested ())
	for (int x = border; x < img.width - border; x+=step)
	  {
	    rgbdata c1 = render1.sample_pixel_img (x, y);
	    rgbdata c2 = render2.sample_pixel_img (x, y);
	    render1.set_linear_hdr_color (c1.red, c1.green, c1.blue, &fc1.x, &fc1.y, &fc1.z);
	    render2.set_linear_hdr_color (c2.red, c2.green, c2.blue, &fc2.x, &fc2.y, &fc2.z);
	    double delta = get_deltae (fc1, fc2);
	    deltaE.pre_account (delta);
#if 0
	    if (delta > 0.1)
	      {
		printf ("Delta %f\n",delta);
		fc1.print (stdout);
		fc2.print (stdout);
	      }
#endif
	    //sum += delta;
	    //maxd = std::max (maxd, delta);
	    sum += delta;
	  }
      if (progress)
	progress->inc_progress ();
    }
  deltaE.finalize_range (65535);

  if (!cmpname)
    {
      for (int y = border; y < img.height - border; y+=step)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int x = border; x < img.width - border; x+=step)
	      {
		rgbdata c1 = render1.sample_pixel_img (x, y);
		rgbdata c2 = render2.sample_pixel_img (x, y);
		render1.set_linear_hdr_color (c1.red, c1.green, c1.blue, &fc1.x, &fc1.y, &fc1.z);
		render2.set_linear_hdr_color (c2.red, c2.green, c2.blue, &fc2.x, &fc2.y, &fc2.z);
		double delta = get_deltae (fc1, fc2, &cln, &mins, &maxs);
		deltaE.account (delta);
		n++;
	      }
	  if (progress)
	    progress->inc_progress ();
	}
    }
  else
    {
      tiff_writer_params par;
      const char *error;
      par.filename = cmpname;
      par.width = (img.width - 2 * border) / step;
      par.height = (img.height - 2 * border) / step;
      par.depth = 32;
      par.hdr = true;
      tiff_writer tiff (par, &error);
      if (error)
	return false;
      for (int y = border; y < img.height - border; y+=step)
	{
	  if (!progress || !progress->cancel_requested ())
	    for (int x = border; x < img.width - border; x+=step)
	      {
		rgbdata c1 = render1.sample_pixel_img (x, y);
		rgbdata c2 = render2.sample_pixel_img (x, y);
		render1.set_linear_hdr_color (c1.red, c1.green, c1.blue, &fc1.x, &fc1.y, &fc1.z);
		render2.set_linear_hdr_color (c2.red, c2.green, c2.blue, &fc2.x, &fc2.y, &fc2.z);
		double delta = get_deltae (fc1, fc2, &cln, &mins, &maxs);
		deltaE.account (delta);
		n++;
		tiff.put_hdr_pixel ((x - border) / step, fc1.x, fc1.y, fc1.z);
	      }
	  if (progress)
	    progress->inc_progress ();
	  if (!tiff.write_row ())
	    return false;
	}
    }
  deltaE.finalize ();
  if (progress)
    progress->pause_stdout ();
  if (mins.x < 0)
    fprintf (stderr, "Warning: minimal x is %f; clamping\n", mins.x);
  if (mins.y < 0)
    fprintf (stderr, "Warning: minimal y is %f; clamping\n", mins.y);
  if (mins.z < 0)
    fprintf (stderr, "Warning: minimal z is %f; clamping\n", mins.z);
  if (maxs.x > 1)
    fprintf (stderr, "Warning: maximal x is %f; clamping\n", maxs.x);
  if (maxs.y > 1)
    fprintf (stderr, "Warning: maximal y is %f; clamping\n", maxs.y);
  if (maxs.z > 1)
    fprintf (stderr, "Warning: maximal z is %f; clamping\n", maxs.z);
  if (cln)
    fprintf (stderr, "Clamped %li pixels out of %li (%f%%)\n", cln, n, cln * 100.0 / n);
  printf ("Average %f\n", sum / n);
  if (progress)
    progress->resume_stdout ();

  *ret_avg = deltaE.find_avg (0, 0.01);
  *ret_max = deltaE.find_max (0.01);
  //*ret_avg = sum / n;
  //*ret_max = maxd;
  return true;
}

}
