#include "include/render-fast.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "render-diff.h"
#include "render-tile.h"
#include "render-to-file.h"


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
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
  bool ok = true;
  const bool lock_p = false;
  if (lock_p)
    global_rendering_lock.lock ();

  if (rtparam.color && !img.has_rgb ())
    rtparam.color = false;

  /* These rendering types requires RGB.  */
  if (!img.has_rgb ()
      && (rtparam.type == render_type_interpolated_original || rtparam.type == render_type_interpolated_profiled_original
	  || rtparam.type == render_type_interpolated_diff))
    rtparam.type = render_type_original;

  if (rtparam.type == render_type_fast
      || rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original
      || rtparam.type == render_type_interpolated)
    rtparam.antialias = false;
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
      return produce_file<render_interpolate,supports_final> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_interpolated_diff:
      return produce_file<render_diff,supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_fast:
      return produce_file<render_fast,supports_scr> (rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    default:
      abort ();
    }
}
