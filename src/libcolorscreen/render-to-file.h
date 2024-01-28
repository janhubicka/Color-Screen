
namespace {
/* Utilities to report time eneeded for a given operation.
  
   Time measurement started.
 
   TODO: This is leftover of colorscreen utility and should be integrated
   with progress_info and removed. */
static struct timeval start_time;

/* Start measurement.  */
static void
record_time ()
{
  gettimeofday (&start_time, NULL);
}

/* Finish measurement and output time.  */
static void
print_time ()
{
  struct timeval end_time;
  gettimeofday (&end_time, NULL);
  double time =
    end_time.tv_sec + end_time.tv_usec / 1000000.0 - start_time.tv_sec -
    start_time.tv_usec / 1000000.0;
  printf ("  ... done in %.3fs\n", time);
}

template<typename T, rgbdata (T::*sample_data)(coord_t x, coord_t y), rgbdata (T::*sample_scr_data)(coord_t x, coord_t y), bool support_tile>
const char *
produce_file (render_to_file_params p, T &render, int black, progress_info *progress)
{
  const char *error = NULL;
  tiff_writer_params tp;
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
      if (!support_tile)
         abort ();
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
	printf (", PPI %f", p.xdpi);
      else
	{
	  if (p.xdpi)
	    printf (", horisontal PPI %f", p.xdpi);
	  if (p.ydpi)
	    printf (", vertical PPI %f", p.ydpi);
	}
      fflush (stdout);
      record_time ();
      printf ("\n");
      if (progress)
        progress->resume_stdout ();
    }
  if (progress)
    progress->set_task ("Rendering and saving", p.height);
  for (int y = 0; y < p.height; y++)
    {
      if (p.antialias == 1)
	{
	  if (p.tile && support_tile)
#pragma omp parallel for default(none) shared(p,render,y,out)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		p.common_map->final_to_scr (xx, yy, &xx, &yy);
		if (!p.pixel_known_p (p.pixel_known_p_data, xx, yy))
		  {
		    if (!p.hdr)
		      out.kill_pixel (x);
		    else
		      out.kill_hdr_pixel (x);
		  }
		else
		  {
		    xx -= p.xpos;
		    yy -= p.ypos;
		    rgbdata d = (render.*sample_scr_data) (xx, yy);
		    if (!p.hdr)
		      {
			int rr, gg, bb;
			render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_pixel (x, rr, gg, bb);
		      }
		    else
		      {
			luminosity_t rr, gg, bb;
			render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_hdr_pixel (x, rr, gg, bb);
		      }
		  }
	      }
	  else
#pragma omp parallel for default(none) shared(p,render,y,out)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		rgbdata d = (render.*sample_data) (xx, yy);
		if (!p.hdr)
		  {
		    int rr, gg, bb;
		    render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_pixel (x, rr, gg, bb);
		  }
		else
		  {
		    luminosity_t rr, gg, bb;
		    render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_hdr_pixel (x, rr, gg, bb);
		  }
	      }
	}
      else
	{
	  coord_t asx = p.xstep / p.antialias;
	  coord_t asy = p.ystep / p.antialias;
	  luminosity_t sc = 1.0 / (p.antialias * p.antialias);
	  if (p.tile && support_tile)
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc)
	    for (int x = 0; x < p.width; x++)
	      {
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		coord_t xx2, yy2;
		p.common_map->final_to_scr (xx, yy, &xx2, &yy2);
		if (!p.pixel_known_p (p.pixel_known_p_data, xx2, yy2))
		  {
		    if (!p.hdr)
		      out.kill_pixel (x);
		    else
		      out.kill_hdr_pixel (x);
		  }
		else
		  {
		    rgbdata d = {0, 0, 0};
		    for (int ay = 0 ; ay < p.antialias; ay++)
		      for (int ax = 0 ; ax < p.antialias; ax++)
			{
			  p.common_map->final_to_scr (xx + ax * asx, yy + ay * asy, &xx2, &yy2);
			  xx2 -= p.xpos;
			  yy2 -= p.ypos;
			  d += (render.*sample_scr_data) (xx2 + ax * asx, yy2 + ay * asy);
			}
		    d.red *= sc;
		    d.green *= sc;
		    d.blue *= sc;
		    if (!p.hdr)
		      {
			int rr, gg, bb;
			render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_pixel (x, rr, gg, bb);
		      }
		    else
		      {
			luminosity_t rr, gg, bb;
			render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
			out.put_hdr_pixel (x, rr, gg, bb);
		      }
		  }
	      }
	  else
#pragma omp parallel for default(none) shared(p,render,y,out,asx,asy,sc)
	    for (int x = 0; x < p.width; x++)
	      {
		rgbdata d = {0, 0, 0};
		coord_t xx = x * p.xstep + p.xstart;
		coord_t yy = y * p.ystep + p.ystart;
		for (int ay = 0 ; ay < p.antialias; ay++)
		  for (int ax = 0 ; ax < p.antialias; ax++)
		    d += (render.*sample_data) (xx + ax * asx, yy + ay * asy);
		d.red *= sc;
		d.green *= sc;
		d.blue *= sc;
		if (!p.hdr)
		  {
		    int rr, gg, bb;
		    render.set_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_pixel (x, rr, gg, bb);
		  }
		else
		  {
		    luminosity_t rr, gg, bb;
		    render.set_hdr_color (d.red, d.green, d.blue, &rr, &gg, &bb);
		    out.put_hdr_pixel (x, rr, gg, bb);
		  }
	      }
	  }
      if (!out.write_row ())
	return "Write error";
      if (progress && progress->cancel_requested ())
	return "Cancelled";
      if (progress)
	progress->inc_progress ();
    }
  if (progress)
    progress->set_task ("Closing tiff file", 1);
  if (p.verbose)
    {
      if (progress)
	progress->pause_stdout ();
      print_time ();
      if (progress)
	progress->resume_stdout ();
    }
  return NULL;
}
template<typename T, typename P, rgbdata (T::*sample_data)(coord_t x, coord_t y), rgbdata (T::*sample_scr_data)(coord_t x, coord_t y), bool support_tile>
const char *
produce_file (render_to_file_params &rfparams, render::render_type_parameters rtparam, P param, render_parameters rparam, image_data &img, int black, progress_info *progress)
{
  T render (param, img, rparam, 65535);
  render.set_render_type (rtparam);
  if (progress)
    {
      progress->set_task ("precomputing", 1);
      progress->push ();
    }
  if (!render.precompute_all (progress))
    {
      return "Precomputation failed (out of memory)";
    }
  if (rfparams.verbose)
    {
      if (progress)
	progress->pause_stdout ();
      print_time ();
      if (progress)
	progress->resume_stdout ();
    }

  // TODO: For HDR output we want to linearize the ICC profile.
  return produce_file<render_img, &render_img::sample_pixel_final, &render_img::sample_pixel_scr, true> (rfparams, render, black, progress);
  return NULL;
}

}
