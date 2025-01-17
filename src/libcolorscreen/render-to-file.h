#include <atomic>
#include "include/tiff-writer.h"
namespace colorscreen
{
namespace
{


template<typename T>
inline rgbdata
sample_data_final_by_img (T &render, scr_to_img &map,coord_t x, coord_t y, int final_xshift, int final_yshift)
{
  point_t p = map.final_to_img ({x - final_xshift, y - final_yshift});
  return render.sample_pixel_img (p.x, p.y);
}
template<typename T>
inline rgbdata
sample_data_final_by_scr (T &render, scr_to_img &map,coord_t x, coord_t y, int final_xshift, int final_yshift)
{
  point_t p = map.final_to_scr ({x - final_xshift, y - final_yshift});
  return render.sample_pixel_scr (p.x, p.y);
}
template<typename T>
inline rgbdata
sample_data_final_by_final (T &render, scr_to_img &,coord_t x, coord_t y, int final_xshift, int final_yshift)
{
  return render.sample_pixel_final (x, y);
}
template<typename T>
inline rgbdata
sample_data_scr_by_scr (T &render, scr_to_img &map,coord_t x, coord_t y)
{
  return render.sample_pixel_scr (x, y);
}
template<typename T>
inline rgbdata
sample_data_scr_by_img (T &render, scr_to_img &map,coord_t x, coord_t y)
{
  point_t p = map.to_img ({x, y});
  return render.sample_pixel_img (p.x, p.y);
}

#define supports_final sample_data_final_by_final, sample_data_scr_by_scr
#define supports_scr sample_data_final_by_scr, sample_data_scr_by_scr
#define supports_img sample_data_final_by_img, sample_data_scr_by_img

template<typename T, rgbdata (sample_data_final)(T &render, scr_to_img &map, coord_t x, coord_t y, int, int), rgbdata (sample_data_scr)(T &render, scr_to_img &map, coord_t x, coord_t y)>
const char *
produce_file (render_to_file_params &p, scr_to_img_parameters &param, image_data &img, T &render, int black, progress_info *progress)
{
  const char *error = NULL;
  scr_to_img map;
  map.set_parameters (param, img);
  int final_xshift, final_yshift, final_width, final_height;
  map.get_final_range (img.width, img.height, &final_xshift, &final_yshift, &final_width, &final_height);
  tiff_writer_params tp;
  tp.parallel = true;
  tp.filename = p.filename;
  tp.width = p.width;
  tp.height = p.height;
  if (p.dng)
    {
      p.depth=16;
      p.hdr = false;
      tp.dng = true; 
      // TODO: Handle normalized patches and screen proportions correctly.
      tp.dye_to_xyz = render.get_rgb_to_xyz_matrix (true, {1/3.0,1/3.0,1/3.0});
      tp.black= black;
    }
  tp.hdr = p.hdr;
  tp.depth = p.depth;
  tp.icc_profile = p.icc_profile;
  tp.icc_profile_len = p.icc_profile_len;
  tp.tile = p.tile;
  tp.xdpi = p.xdpi;
  tp.ydpi = p.ydpi;
  if (p.tile)
    {
      tp.xoffset = p.xoffset;
      tp.yoffset = p.yoffset;
      tp.alpha = true;
    }
  if (progress)
    progress->set_task ("Opening tiff file", 1);
  tiff_writer out(tp, &error);
  if (error)
    return error;
  if (p.verbose)
    {
      if (progress)
        progress->pause_stdout ();
      printf ("Rendering %s in resolution %ix%i and depth %i", p.filename, p.width,
	      p.height, p.depth);
      if (p.hdr)
	printf (", HDR");
      if (p.xdpi && p.xdpi == p.ydpi)
	printf (", PPI %.2f", p.xdpi);
      else
	{
	  if (p.xdpi)
	    printf (", horisontal PPI %.2f", p.xdpi);
	  if (p.ydpi)
	    printf (", vertical PPI %.2f", p.ydpi);
	}
      fflush (stdout);
      printf ("\n");
      if (progress)
        progress->resume_stdout ();
    }
  if (progress)
    progress->set_task ("Rendering and saving", p.height * 2);
  for (int y = 0; y < p.height;)
    {
      if (p.antialias == 1)
	{
	  if (p.tile)
#pragma omp parallel for default(none) shared(p,render,y,out,map,progress) collapse (2)
	    for (int row = 0; row < out.get_n_rows (); row++)
	      for (int x = 0; x < p.width; x++)
		{
		  point_t scr = p.common_map->final_to_scr ({x * p.xstep + p.xstart, (y + row) * p.ystep + p.ystart});
		  if (!p.pixel_known_p (p.pixel_known_p_data, scr.x, scr.y))
		    {
		      if (!p.hdr)
			out.kill_pixel (x, row);
		      else
			out.kill_hdr_pixel (x, row);
		    }
		  else
		    {
		      scr -= {p.xpos, p.ypos};
		      rgbdata d = sample_data_scr (render, map, scr.x, scr.y);
		      if (!p.hdr)
			{
			  int rr, gg, bb;
			  render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			  out.put_pixel (x, row, rr, gg, bb);
			}
		      else
			{
			  luminosity_t rr, gg, bb;
			  render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			  out.put_hdr_pixel (x, row, rr, gg, bb);
			}
		    }
		  if (x == p.width - 1 && progress)
		    progress->inc_progress ();
		}
	  else
#pragma omp parallel for default(none) shared(p,render,y,out,map,final_xshift, final_yshift,progress) collapse (2)
	    for (int row = 0; row < out.get_n_rows (); row++)
	      for (int x = 0; x < p.width; x++)
		{
		  coord_t xx = x * p.xstep + p.xstart;
		  coord_t yy = (y + row) * p.ystep + p.ystart;
		  rgbdata d = sample_data_final (render, map, xx, yy, final_xshift, final_yshift);
		  if (!p.hdr)
		    {
		      int rr, gg, bb;
		      render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		      out.put_pixel (x, row, rr, gg, bb);
		    }
		  else
		    {
		      luminosity_t rr, gg, bb;
		      render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		      out.put_hdr_pixel (x, row, rr, gg, bb);
		    }
		  if (x == p.width - 1 && progress)
		    progress->inc_progress ();
		}
	}
      else
	{
	  coord_t asx = p.xstep / p.antialias;
	  coord_t asy = p.ystep / p.antialias;
	  luminosity_t sc = 1.0 / (p.antialias * p.antialias);
	  if (p.tile)
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc,map,final_xshift, final_yshift,progress) collapse (2)
	    for (int row = 0; row < out.get_n_rows (); row++)
	      for (int x = 0; x < p.width; x++)
		{
		  point_t finalp = {x * p.xstep + p.xstart, (y + row) * p.ystep + p.ystart};
		  point_t scr = p.common_map->final_to_scr (finalp);
		  if (!p.pixel_known_p (p.pixel_known_p_data, scr.x, scr.y))
		    {
		      if (!p.hdr)
			out.kill_pixel (x, row);
		      else
			out.kill_hdr_pixel (x, row);
		    }
		  else
		    {
		      rgbdata d = {0, 0, 0};
		      for (int ay = 0 ; ay < p.antialias; ay++)
			for (int ax = 0 ; ax < p.antialias; ax++)
			  {
			    scr = p.common_map->final_to_scr ({finalp.x + ax * asx, finalp.y + ay * asy}) - (point_t){p.xpos, p.ypos};
			    d += sample_data_scr (render, map, scr.x + ax * asx, scr.y + ay * asy);
			  }
		      d.red *= sc;
		      d.green *= sc;
		      d.blue *= sc;
		      if (!p.hdr)
			{
			  int rr, gg, bb;
			  render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			  out.put_pixel (x, row, rr, gg, bb);
			}
		      else
			{
			  luminosity_t rr, gg, bb;
			  render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			  out.put_hdr_pixel (x, row, rr, gg, bb);
			}
		    }
		  if (x == p.width - 1 && progress)
		    progress->inc_progress ();
		}
	  else
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc,map,final_xshift,final_yshift,progress) collapse (2)
	    for (int row = 0; row < out.get_n_rows (); row++)
	      for (int x = 0; x < p.width; x++)
		{
		  rgbdata d = {0, 0, 0};
		  coord_t xx = x * p.xstep + p.xstart;
		  coord_t yy = (y + row) * p.ystep + p.ystart;
		  for (int ay = 0 ; ay < p.antialias; ay++)
		    for (int ax = 0 ; ax < p.antialias; ax++)
		      d += sample_data_final (render, map, xx + ax * asx, yy + ay * asy, final_xshift, final_yshift);
		  d.red *= sc;
		  d.green *= sc;
		  d.blue *= sc;
		  if (!p.hdr)
		    {
		      int rr, gg, bb;
		      render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		      out.put_pixel (x, row, rr, gg, bb);
		    }
		  else
		    {
		      luminosity_t rr, gg, bb;
		      render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		      out.put_hdr_pixel (x, row, rr, gg, bb);
		    }
		  if (x == p.width - 1 && progress)
		    progress->inc_progress ();
		}
	    }
      y += out.get_n_rows ();
      bool ret;
      ret = out.write_rows (progress);
      if (progress && progress->cancel_requested ())
	return "Cancelled";
      if (!ret)
	return "Write error";
    }
  if (progress)
    progress->set_task ("Closing tiff file", 1);
  return NULL;
}
template<typename T, rgbdata (sample_data_final)(T &render, scr_to_img &map, coord_t x, coord_t y, int, int), rgbdata (sample_data_scr)(T &render, scr_to_img &map, coord_t x, coord_t y), typename P>
const char *
produce_file (render_to_file_params &rfparams,
	      render_type_parameters &rtparam, scr_to_img_parameters sparam, P &param, render_parameters &rparam, image_data &img, int black, progress_info *progress)
{
  T render (param, img, rparam, 65535);
  render.compute_final_range ();
  render.set_render_type (rtparam);
  if (progress)
    {
      progress->set_task ("precomputing", 1);
      progress->push ();
    }
  if (!render.precompute_all (progress))
    return "Precomputation failed (out of memory)";
  if (progress)
    progress->pop ();

  // TODO: For HDR output we want to linearize the ICC profile.
  return produce_file<T, sample_data_final, sample_data_scr> (rfparams, sparam, img, render, black, progress);
}
}
}
