#include "config.h"
#include "include/colorscreen.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "render-diff.h"
#include "render-tile.h"
#include "render-to-file.h"
#ifdef RENDER_EXTRA
#include "render-extra/render-extra.h"
#endif
#include "render-fast.h"
#include "render-scr-detect.h"

static void
sanitize_render_parameters (render_type_parameters &rtparam, scr_to_img_parameters &param, image_data &img)
{
  if (rtparam.color && !img.has_rgb ())
    rtparam.color = false;

  /* These rendering types requires RGB.  */
  if (!img.has_rgb ()
      && (rtparam.type == render_type_interpolated_original || rtparam.type == render_type_interpolated_profiled_original
	  || rtparam.type == render_type_interpolated_diff || rtparam.type == render_type_profiled_original))
    rtparam.type = render_type_original;

  /* only original and profiled original rendering can be performed on Random screen.  */
  if (param.type == Random
      && rtparam.type != render_type_original
      && rtparam.type != render_type_profiled_original)
    rtparam.type = render_type_original;

  if (rtparam.type == render_type_fast
      || rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original
      || rtparam.type == render_type_interpolated)
    rtparam.antialias = false;
  if (rtparam.type == render_type_profiled_original)
    rtparam.color = true;
  if (rtparam.type == render_type_realistic)
    rtparam.color = false;
}

bool
render_to_scr::render_tile (render_type_parameters rtparam,
			    scr_to_img_parameters &param, image_data &img,
			    render_parameters &rparam,
			    unsigned char *pixels, int pixelbytes, int rowstride,
			    int width, int height,
			    double xoffset, double yoffset,
			    double step,
			    progress_info *progress)
{
  if (width <= 0 || height <= 0)
    return true;

  /* Avoid rendering outside of image area.  This saves some time but also ugly artifacts.  */
  if ((int)xoffset < 0)
    {
      int border = -xoffset;
      if (border > width)
	border = width;
      for (int y = 0; y < height; y++)
	for (int x = 0; x < border; x++)
	  putpixel (pixels, pixelbytes, rowstride, x, y, 128, 128, 128);
      pixels += border * pixelbytes;
      xoffset += border;
      width -= border;
      if (!width)
	return true;
    }
  if ((int)yoffset < 0)
    {
      int border = -yoffset;
      if (border > height)
	border = height;
      for (int y = 0; y < border; y++)
	for (int x = 0; x < width; x++)
	  putpixel (pixels, pixelbytes, rowstride, x, y, 128, 128, 128);
      pixels += border * rowstride;
      yoffset += border;
      height -= border;
      if (!height)
	return true;
    }
  if ((int)((xoffset + width) - img.width / step) > 0)
    {
      int border = (int)((xoffset + width) - img.width / step);
      if (border > width)
	border = width;
      for (int y = 0; y < height; y++)
	for (int x = width - border; x < width; x++)
	  putpixel (pixels, pixelbytes, rowstride, x, y, 128, 128, 128);
      width -= border;
      if (!width)
	return true;
    }
  if ((int)((yoffset + height) - img.height / step) > 0)
    {
      int border = (int)((yoffset + height) - img.height / step);
      if (border > height)
	border = height;
      for (int y = height - border; y < height; y++)
	for (int x = 0; x < width; x++)
	  putpixel (pixels, pixelbytes, rowstride, x, y, 128, 128, 128);
      height -= border;
      if (!height)
	return true;
    }

  /* Do not render out of scan area; it is slow.  */
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  bool ok = true;
  const bool lock_p = false;
  if (lock_p)
    global_rendering_lock.lock ();

  sanitize_render_parameters (rtparam, param, img);

  render_parameters my_rparam;
  my_rparam.adjust_for (rtparam, rparam);

  if (progress)
    progress->set_task ("precomputing", 1);
  switch (rtparam.type)
    {
    case render_type_original:
    case render_type_profiled_original:
      ok = do_render_tile_with_gray<render_img> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_preview_grid:
    case render_type_realistic:
      ok = do_render_tile<render_superpose_img> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_interpolated_original:
    case render_type_interpolated_profiled_original:
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      ok = do_render_tile<render_interpolate> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_interpolated_diff:
      ok = do_render_tile<render_diff> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    case render_type_extra:
#ifdef RENDER_EXTRA
      ok = do_render_tile<render_extra> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
#endif
    case render_type_fast:
      ok = do_render_tile<render_fast> (rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width, height, xoffset, yoffset, step, progress);
      break;
    default:
      abort ();
    }
  if (stats)
    {
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;

      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time.tv_sec - start_time.tv_usec/1000000.0;
      printf ("\nRender type:%i resolution:%ix%i time:%.3fs fps:%.3f", rtparam.type, width, height, time, 1/time);
      if (prev_time_set)
	{
	  double time2 = end_time.tv_sec + end_time.tv_usec/1000000.0 - prev_time.tv_sec - prev_time.tv_usec/1000000.0;
          printf (" last run %.3f ago overall fps:%.3f\n", time2, 1/(time2+time));
	}
      else
        printf ("\n");
      prev_time = end_time;
      prev_time_set = true;
      fflush (stdout);
    }
  if (lock_p)
    global_rendering_lock.unlock ();
  return (!progress || !progress->cancelled ()) && ok;
}
const char *
render_to_scr::render_to_file (render_to_file_params &rfparams, render_type_parameters rtparam, scr_to_img_parameters param, render_parameters rparam, image_data &img, int black, progress_info *progress)
{
  switch (rtparam.type)
    {
    case render_type_original:
    case render_type_profiled_original:
      return produce_file<render_img,supports_img> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_preview_grid:
    case render_type_realistic:
      return produce_file<render_superpose_img,supports_img> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_interpolated_original:
    case render_type_interpolated_profiled_original:
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      /* TODO: supports_final needs a place to compute final range.  */
      return produce_file<render_interpolate,/*supports_final*/supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_interpolated_diff:
      return produce_file<render_diff,supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_extra:
#ifdef RENDER_EXTRA
      return produce_file<render_extra,/*supports_final*/supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
#endif
    case render_type_fast:
      return produce_file<render_fast,supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    default:
      abort ();
    }
}
DLL_PUBLIC bool
render_tile(image_data &scan, scr_to_img_parameters &param, scr_detect_parameters &dparam, render_parameters &rparam,
	    render_type_parameters &rtparam, tile_parameters &tile, progress_info *progress)
{
  if ((int)rtparam.type < (int)render_type_first_scr_detect)
    return render_to_scr::render_tile (rtparam, param, scan, rparam,
				       tile.pixels, tile.pixelbytes, tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y, tile.step, progress);
  else
    return render_scr_detect::render_tile (rtparam, dparam, scan, rparam,
					   tile.pixels, tile.pixelbytes, tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y, tile.step, progress);

}
