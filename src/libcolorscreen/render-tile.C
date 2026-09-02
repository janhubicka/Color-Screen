/* Implementation of tile-based rendering and file output.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include "config.h"
#include "include/colorscreen.h"
#include "render-diff.h"
#include "render-interpolate.h"
#include "render-superposeimg.h"
#include "render-tile.h"
#include "render-to-file.h"
#ifdef RENDER_EXTRA
#include "render-extra/render-extra.h"
#endif
#include "render-fast.h"
#include "render-scr-detect.h"
#include "render-screen.h"
#include "render-simulate.h"

namespace colorscreen
{
/* Properties of individual rendering types.  */
const constexpr render_type_property render_type_properties[render_type_max] = {
  { "original", "Original digital capture",
    render_type_property::
            OUTPUTS_SCAN_PROFILE /*|
                                    render_type_property::SUPPORTS_IR_RGB_SWITCH*/
        | render_type_property::SCAN_RESOLUTION },
  { "interpolated", "Image layer + screen filter demosaiced",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::PATCH_RESOLUTION },
  { "interpolated-predictive",
    "Image layer + screen filter demosaiced with detail recovery",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "image-layer", "Image layer",
    render_type_property::OUTPUTS_SCAN_PROFILE
        | render_type_property::SCAN_RESOLUTION },
  { "screen", "Screen filter",
    render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS
        | render_type_property::OUTPUTS_SRGB_PROFILE },
  { "realistic", "Image layer + screen filter",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::SUPPORTS_IR_RGB_SWITCH
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "interpolated-combined", "Image layer + demosaiced colors",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "interpolated-original", "Original digital capture demosaiced",
    render_type_property::OUTPUTS_SCAN_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::NEEDS_RGB
        | render_type_property::PATCH_RESOLUTION },
  { "preview-grid", "Preview screen registration",
    render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::SUPPORTS_IR_RGB_SWITCH
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "simulate-process", "Simulate capture using screen filter",
    render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS
        | render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::NEEDS_RGB },
  { "fast", "Demosaiced using simple algorithm",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::SCREEN_RESOLUTION
        | render_type_property::HIDE_IN_GUI },
  { "extra", "extra",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::PATCH_RESOLUTION
        | render_type_property::HIDE_IN_GUI },
  { "detected-adjusted-color",
    "Color used for auto-detection of screen filter",
    render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "detected-normalized-color",
    "Normalized color used for auto-detection of screen filter",
    render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::RESET_BRIGHTNESS_ETC },
  { "detected-screen-color", "Auto-detected screen filter",
    render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::RESET_BRIGHTNESS_ETC },
  { "detected-realistic", "Image layer + auto-detected screen filter",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION
        | render_type_property::ANTIALIAS },
  { "detected-interpolated",
    "Image layer + auto-detected screen filter demosaiced",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION },
  { "detected-interpolated-scaled",
    "Image layer + auto-detected screen filter demosaiced assuming irregular "
    "element size",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION },
  { "detected-relaxation-scaled",
    "Image layer + auto-detected screen filter demosaiced assuming irregular "
    "element size and interpolated",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_DETECT
        | render_type_property::SCAN_RESOLUTION },
  { "profiled-original", "Original capture with correction profile",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::
            NEEDS_SCR_TO_IMG /* currently only to compute patch proportions,
                                but we have no profiling otherwise yet */
        | render_type_property::NEEDS_RGB
        | render_type_property::NEEDS_CORRECTION_PROFILE
        | render_type_property::SCAN_RESOLUTION },
  { "interpolated-profiled-original",
    "Original capture demosaiced with correction profile",
    render_type_property::OUTPUTS_PROCESS_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::NEEDS_RGB
        | render_type_property::PATCH_RESOLUTION
        | render_type_property::NEEDS_CORRECTION_PROFILE },
  { "interpolated-diff", "Difference between profile and image layer",
    render_type_property::OUTPUTS_SRGB_PROFILE
        | render_type_property::NEEDS_SCR_TO_IMG
        | render_type_property::NEEDS_RGB
        | render_type_property::PATCH_RESOLUTION
        | render_type_property::NEEDS_CORRECTION_PROFILE },
};

/* Sanitize rendering parameters RTPARAM according to screen-to-image mapping
   parameters PARAM and image data IMG.  This ensures that requested rendering
   type is compatible with available data.  */
static void
sanitize_render_parameters (render_type_parameters &rtparam,
                            scr_to_img_parameters &param, image_data &img)
{
  if (rtparam.color && !img.has_rgb ())
    rtparam.color = false;

  if (!img.has_rgb ()
      && (render_type_properties[(int)rtparam.type].flags
          & render_type_property::NEEDS_RGB))
    rtparam.type = render_type_original;

  if (!screen_has_regular_geometry_p (param.type)
      && (render_type_properties[(int)rtparam.type].flags
          & render_type_property::NEEDS_SCR_TO_IMG))
    rtparam.type = render_type_original;

  if (rtparam.type == render_type_fast
      || rtparam.type == render_type_interpolated_original
      || rtparam.type == render_type_interpolated_profiled_original
      || rtparam.type == render_type_interpolated)
    rtparam.antialias = false;
  if (rtparam.type == render_type_original && img.has_rgb ())
    rtparam.color = true;
  if (rtparam.type == render_type_image_layer)
    rtparam.color = false;
  if (rtparam.type == render_type_profiled_original)
    rtparam.color = true;
#if 0
  if (rtparam.type == render_type_realistic)
    rtparam.color = false;
#endif
}

/* Render tile of image to screen buffer PIXELS.  RTPARAM specifies rendering
   type, PARAM is the screen-to-image mapping parameters, IMG is the image
   data, RPARAM is the rendering parameters, PIXELBYTES is the bytes per pixel,
   ROWSTRIDE is the row stride, WIDTH and HEIGHT are tile dimensions,
   XOFFSET and YOFFSET are coordinates in the output image, STEP is the
   sampling step, and PROGRESS is used for progress reporting.  */
bool
render_to_scr::render_tile (render_type_parameters rtparam,
                            scr_to_img_parameters &param, image_data &img,
                            render_parameters &rparam, unsigned char *pixels,
                            int pixelbytes, int rowstride, int width,
                            int height, double xoffset, double yoffset,
                            double step, progress_info *progress)
{
  if (width <= 0 || height <= 0)
    return true;

  // A capture with no historical screen may still use the ordinary image
  // layer (including sharpening and capture corrections). Only screen-colour
  // detection itself is nonsensical without a physical screen.
  if (param.type == NoScreen
      && (render_type_properties[(int)rtparam.type].flags
          & render_type_property::USES_SCR_DETECT))
    rtparam.type = render_type_original;

  /* Avoid rendering outside of image area.  This saves some time and prevents
   * ugly artifacts.  */
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

  /* Do not render out of scan area; it is slow.  Function-local static
     initialization is synchronized by C++, so concurrent GUI render tasks do
     not race while discovering whether statistics are enabled.  */
  static const bool stats_enabled = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats_enabled)
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
    case render_type_image_layer:
      ok = do_render_tile_with_gray<render_img> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
    case render_type_preview_grid:
    case render_type_realistic:
      ok = do_render_tile<render_superpose_img> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
    case render_type_screen:
      my_rparam.brightness = 1;
      ok = do_render_tile<render_screen> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
    case render_type_simulate_process:
      ok = do_render_tile<render_simulate_process> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
    case render_type_interpolated_original:
    case render_type_interpolated_profiled_original:
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      ok = do_render_tile<render_interpolate> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
    case render_type_interpolated_diff:
      ok = do_render_tile<render_diff> (rtparam, param, img, my_rparam, pixels,
                                        pixelbytes, rowstride, width, height,
                                        xoffset, yoffset, step, progress);
      break;
    case render_type_extra:
#ifdef RENDER_EXTRA
      ok = do_render_tile<render_extra> (
          rtparam, param, img, my_rparam, pixels, pixelbytes, rowstride, width,
          height, xoffset, yoffset, step, progress);
      break;
#endif
    case render_type_fast:
      ok = do_render_tile<render_fast> (rtparam, param, img, my_rparam, pixels,
                                        pixelbytes, rowstride, width, height,
                                        xoffset, yoffset, step, progress);
      break;
    default:
      abort ();
    }
  if (stats_enabled)
    {
      /* CSSTATS is diagnostic output only.  Serialize its shared timing state,
         not rendering itself.  */
      static std::mutex stats_lock;
      std::lock_guard<std::mutex> stats_guard (stats_lock);
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;

      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec / 1000000.0
                    - start_time.tv_sec - start_time.tv_usec / 1000000.0;
      printf ("\nRender type:%i resolution:%ix%i time:%.3fs fps:%.3f",
              rtparam.type, width, height, time, 1 / time);
      if (prev_time_set)
        {
          double time2 = end_time.tv_sec + end_time.tv_usec / 1000000.0
                         - prev_time.tv_sec - prev_time.tv_usec / 1000000.0;
          printf (" last run %.3f s ago overall FPS:%.3f\n", time2,
                  1 / (time2 + time));
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

/* Render whole image to a file.  RFPARAMS specifies file output parameters,
   RTPARAM specifies rendering type, PARAM is the screen-to-image mapping
   parameters, RPARAM is the rendering parameters, IMG is the image data,
   BLACK specifies if black border should be rendered, and PROGRESS is used
   for progress reporting.  */
const char *
render_to_scr::render_to_file (render_to_file_params &rfparams,
                               render_type_parameters rtparam,
                               scr_to_img_parameters param,
                               render_parameters rparam, image_data &img,
                               int black, progress_info *progress)
{
  switch (rtparam.type)
    {
    case render_type_original:
    case render_type_profiled_original:
    case render_type_image_layer:
      return produce_file<render_img, supports_img> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_preview_grid:
    case render_type_realistic:
      return produce_file<render_superpose_img, supports_img> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_screen:
      rparam.brightness = 1;
      return produce_file<render_screen, supports_scr> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_simulate_process:
      return produce_file<render_simulate_process, supports_img> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_interpolated_original:
    case render_type_interpolated_profiled_original:
    case render_type_interpolated:
    case render_type_combined:
    case render_type_predictive:
      return produce_file<render_interpolate, supports_final> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_interpolated_diff:
      return produce_file<render_diff, supports_scr> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    case render_type_extra:
#ifdef RENDER_EXTRA
      return produce_file<render_extra, supports_final> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
#endif
    case render_type_fast:
      return produce_file<render_fast, supports_scr> (
          rfparams, rtparam, param, param, rparam, img, black, progress);
      break;
    default:
      abort ();
    }
}

/* Render a tile on the zero-based final-coordinate canvas used by
   screen-geometry file output.  SAMPLE_DATA_FINAL is the same sampling policy
   used by render_to_file: renderers may natively sample final coordinates, or
   final coordinates are mapped to screen/image coordinates first.  */
template<typename T,
         rgbdata (sample_data_final)(T &render, scr_to_img &map, coord_t x,
                                     coord_t y, int final_xshift,
                                     int final_yshift),
         typename P>
static bool
render_tile_final_impl (render_type_parameters rtparam,
                        scr_to_img_parameters &map_param, P &engine_param,
                        image_data &img, render_parameters rparam,
                        tile_parameters &tile, progress_info *progress)
{
  if (tile.width <= 0 || tile.height <= 0)
    return true;

  scr_to_img map;
  if (!map.set_parameters (map_param, img))
    return false;

  T render (engine_param, img, rparam, 255);
  render.compute_final_range ();
  render.set_render_type (rtparam);
  if (progress)
    progress->set_task ("precomputing", 1);
  {
    sub_task task (progress);
    if (!render.precompute_all (progress))
      return false;
  }

  const int_image_area final_range (map.get_final_range (img.width, img.height));
  const int final_xshift = -final_range.x;
  const int final_yshift = -final_range.y;

  int antialias = 1;
  if (rtparam.antialias && tile.step > 1)
    antialias = std::min (4, std::max (1, (int)ceil (tile.step)));
  const coord_t substep = tile.step / antialias;
  const luminosity_t sample_scale
      = 1 / (luminosity_t)(antialias * antialias);

  if (progress)
    progress->set_task ("rendering", tile.height);
#pragma omp parallel for default(none) shared(tile,render,map,progress,final_xshift,final_yshift,antialias,substep,sample_scale) if (tile.width * (size_t)tile.height > render.openmp_size ())
  for (int y = 0; y < tile.height; y++)
    {
      if (!progress || !progress->cancel_requested ())
        for (int x = 0; x < tile.width; x++)
          {
            rgbdata d = {0, 0, 0};
            coord_t xx = (x + tile.pos.x) * tile.step;
            coord_t yy = (y + tile.pos.y) * tile.step;
            if (antialias == 1)
              d = sample_data_final (render, map, xx, yy, final_xshift,
                                     final_yshift);
            else
              {
                for (int ay = 0; ay < antialias; ay++)
                  for (int ax = 0; ax < antialias; ax++)
                    d += sample_data_final (
                        render, map, xx + (ax + (coord_t)0.5) * substep,
                        yy + (ay + (coord_t)0.5) * substep, final_xshift,
                        final_yshift);
                d.red *= sample_scale;
                d.green *= sample_scale;
                d.blue *= sample_scale;
              }
            int_rgbdata out_c = render.out_color.final_color (d);
            putpixel (tile.pixels, tile.pixelbytes, tile.rowstride, x, y,
                      out_c.red, out_c.green, out_c.blue);
          }
      if (progress)
        progress->inc_progress ();
    }
  return (!progress || !progress->cancelled ());
}

/* Dispatch final-coordinate tile rendering using the same renderer-specific
   sampling policies as render_to_file.  */
static bool
render_tile_final (image_data &scan, scr_to_img_parameters &param,
                   scr_detect_parameters &dparam, render_parameters &rparam,
                   render_type_parameters rtparam, tile_parameters &tile,
                   progress_info *progress)
{
  if (scan.stitch)
    {
      /* The existing stitched tile renderer already consumes zero-based final
         viewport coordinates; stitched projects have no useful scan canvas.  */
      if ((int)rtparam.type < (int)render_type_first_scr_detect
          && rtparam.type != render_type_interpolated_diff)
        return render_to_scr::render_tile (
            rtparam, param, scan, rparam, tile.pixels, tile.pixelbytes,
            tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y,
            tile.step, progress);
      return render_scr_detect::render_tile (
          rtparam, dparam, scan, rparam, tile.pixels, tile.pixelbytes,
          tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y,
          tile.step, progress);
    }

  if ((int)rtparam.type < (int)render_type_first_scr_detect
      && rtparam.type != render_type_interpolated_diff)
    {
      sanitize_render_parameters (rtparam, param, scan);
      render_parameters my_rparam;
      my_rparam.adjust_for (rtparam, rparam);
      switch (rtparam.type)
        {
        case render_type_original:
        case render_type_profiled_original:
        case render_type_image_layer:
          return render_tile_final_impl<render_img, sample_data_final_by_img> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        case render_type_preview_grid:
        case render_type_realistic:
          return render_tile_final_impl<render_superpose_img,
                                        sample_data_final_by_img> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        case render_type_screen:
          my_rparam.brightness = 1;
          return render_tile_final_impl<render_screen, sample_data_final_by_scr> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        case render_type_simulate_process:
          return render_tile_final_impl<render_simulate_process,
                                        sample_data_final_by_img> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        case render_type_interpolated_original:
        case render_type_interpolated_profiled_original:
        case render_type_interpolated:
        case render_type_combined:
        case render_type_predictive:
          return render_tile_final_impl<render_interpolate,
                                        sample_data_final_by_final> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        case render_type_extra:
#ifdef RENDER_EXTRA
          return render_tile_final_impl<render_extra, sample_data_final_by_final> (
              rtparam, param, param, scan, my_rparam, tile, progress);
#endif
        case render_type_fast:
          return render_tile_final_impl<render_fast, sample_data_final_by_scr> (
              rtparam, param, param, scan, my_rparam, tile, progress);
        default:
          abort ();
        }
    }

  if (rtparam.type == render_type_scr_nearest
      || rtparam.type == render_type_scr_nearest_scaled
      || rtparam.type == render_type_scr_relax)
    rtparam.antialias = false;
  render_parameters my_rparam;
  my_rparam.adjust_for (rtparam, rparam);
  switch (rtparam.type)
    {
    case render_type_interpolated_diff:
      return render_tile_final_impl<render_diff, sample_data_final_by_scr> (
          rtparam, param, param, scan, my_rparam, tile, progress);
    case render_type_adjusted_color:
      return render_tile_final_impl<render_scr_detect_adjusted,
                                    sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_normalized_color:
      return render_tile_final_impl<render_scr_detect_normalized,
                                    sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_pixel_colors:
      return render_tile_final_impl<render_scr_detect_pixel_color,
                                    sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_realistic_scr:
      return render_tile_final_impl<render_scr_detect_superpose_img,
                                    sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_scr_nearest:
      return render_tile_final_impl<render_scr_nearest, sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_scr_nearest_scaled:
      return render_tile_final_impl<render_scr_nearest_scaled,
                                    sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    case render_type_scr_relax:
      return render_tile_final_impl<render_scr_relax, sample_data_final_by_img> (
          rtparam, param, dparam, scan, my_rparam, tile, progress);
    default:
      abort ();
    }
}

/* Global entry point for rendering a tile.  SCAN is the scanned image data,
   PARAM is the screen-to-image mapping parameters, DPARAM is the screen
   detection parameters, RPARAM is the rendering parameters, RTPARAM specifies
   rendering type, TILE is the tile parameters, and PROGRESS is used for
   progress reporting.  */
DLL_PUBLIC bool
render_tile (image_data &scan, scr_to_img_parameters &param,
             scr_detect_parameters &dparam, render_parameters &rparam,
             render_type_parameters &rtparam, tile_parameters &tile,
             progress_info *progress)
{
  return render_tile (scan, param, dparam, rparam, rtparam, tile,
                      render_scan_coordinates, progress);
}

DLL_PUBLIC bool
render_tile (image_data &scan, scr_to_img_parameters &param,
             scr_detect_parameters &dparam, render_parameters &rparam,
             render_type_parameters &rtparam, tile_parameters &tile,
             render_coordinate_space coordinates, progress_info *progress)
{
  if (coordinates == render_final_coordinates)
    return render_tile_final (scan, param, dparam, rparam, rtparam, tile,
                              progress);

  if ((int)rtparam.type < (int)render_type_first_scr_detect
      && rtparam.type != render_type_interpolated_diff)
    return render_to_scr::render_tile (
        rtparam, param, scan, rparam, tile.pixels, tile.pixelbytes,
        tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y,
        tile.step, progress);
  else
    return render_scr_detect::render_tile (
        rtparam, dparam, scan, rparam, tile.pixels, tile.pixelbytes,
        tile.rowstride, tile.width, tile.height, tile.pos.x, tile.pos.y,
        tile.step, progress);
}
} // namespace colorscreen
