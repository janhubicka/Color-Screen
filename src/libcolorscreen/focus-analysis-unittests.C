#include "include/focus-analysis.h"
#include "finetune-int.h"
#include "include/scr-to-img.h"
#include "screen.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace colorscreen;

static bool
build_uniform_focus_fixture (
    image_data *image, scr_to_img_parameters *geometry,
    std::vector<finetune_focus_area_candidate> *candidates)
{
  constexpr int width = 384;
  constexpr int height = 96;
  constexpr int regions = 3;
  constexpr coord_t true_blur = (coord_t)0.82;
  const rgbdata truth[regions]
      = { { 0.50, 0.30, 0.20 }, { 0.18, 0.78, 0.35 },
          { 0.82, 0.12, 0.55 } };
  const point_t locations[regions]
      = { { 64, 48 }, { 192, 48 }, { 320, 48 } };

  if (!image->set_dimensions (width, height, true, false))
    return false;

  geometry->type = Paget;
  geometry->center = { 0, 0 };
  geometry->coordinate1 = { 8, 0 };
  geometry->coordinate2 = { 0, 8 };
  scr_to_img map;
  if (!map.set_parameters (*geometry, *image))
    return false;
  const coord_t pixel_size = map.pixel_size ({ 0, 0, width, height });
  if (!(pixel_size > 0))
    return false;

  screen source;
  source.initialize (geometry->type);
  std::array<std::unique_ptr<screen>, regions> blurred;
  for (int tileid = 0; tileid < regions; tileid++)
    {
      auto weighted = std::make_unique<screen> ();
      blurred[tileid] = std::make_unique<screen> ();
      finetune_apply_uniform_image_layer (*weighted, source, source,
                                          truth[tileid], { 0, 0 });
      blurred[tileid]->initialize_with_blur (*weighted,
                                             true_blur * pixel_size);
    }

  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const int tileid = std::min (x / (width / regions), regions - 1);
        rgbdata value = blurred[tileid]->interpolated_mult (
            map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 }));
        value.red = std::clamp (value.red, (luminosity_t)0, (luminosity_t)1);
        value.green
            = std::clamp (value.green, (luminosity_t)0, (luminosity_t)1);
        value.blue
            = std::clamp (value.blue, (luminosity_t)0, (luminosity_t)1);
        image->put_rgb_pixel (
            x, y,
            { (image_data::gray)(value.red * 65535 + (luminosity_t)0.5),
              (image_data::gray)(value.green * 65535 + (luminosity_t)0.5),
              (image_data::gray)(value.blue * 65535 + (luminosity_t)0.5) });
      }

  candidates->clear ();
  for (int tileid = 0; tileid < regions; tileid++)
    {
      finetune_focus_area_candidate candidate;
      candidate.center = locations[tileid];
      candidate.mean_color = truth[tileid];
      candidate.search_score = (coord_t)tileid * (coord_t)0.001;
      candidate.fit.success = true;
      candidate.fit.badness = (coord_t)0.01;
      candidate.fit.contrast = (coord_t)0.2;
      candidate.fit.screen_coord_adjust = { 0, 0 };
      candidate.fit.emulsion_coord_adjust = { 0, 0 };
      candidate.fit.screen_blur_radius = (coord_t)0.3;
      candidates->push_back (candidate);
    }
  return true;
}

static bool
test_joint_focus_analysis ()
{
  image_data image;
  scr_to_img_parameters geometry;
  std::vector<finetune_focus_area_candidate> candidates;
  if (!build_uniform_focus_fixture (&image, &geometry, &candidates))
    return false;

  render_parameters rparam;
  rparam.gamma = 1;
  rparam.screen_blur_radius = (coord_t)0.3;
  rparam.sharpen.mode = sharpen_parameters::none;
  rparam.sharpen.scanner_mtf_scale = 0;

  finetune_parameters fparam;
  fparam.range = 2;
  fparam.ignore_outliers = 0;
  fparam.flags = finetune_screen_blur | finetune_uniform_image_layer
                 | finetune_no_normalize | finetune_no_data_collection;

  finetune_focus_analysis_parameters analysis;
  analysis.selection.min_areas = 3;
  analysis.selection.max_areas = 3;
  analysis.selection.minimum_color_volume = 0;

  finetune_focus_analysis_result result;
  if (!finetune_analyze_focus_areas (rparam, geometry, image, candidates,
                                     fparam, analysis, &result, nullptr))
    {
      fprintf (stderr, "Joint focus analysis failed: %s\n",
               result.err.c_str ());
      return false;
    }
  if (!result.success || result.selected.size () != 3
      || result.leave_one_out_fits.size () != 3
      || result.held_out_fits.size () != 3
      || result.held_out_relative_badness.size () != 3)
    {
      fprintf (stderr,
               "Joint focus analysis returned incomplete diagnostics\n");
      return false;
    }
  if (!(result.held_out_max_relative_badness >= 0)
      || !std::isfinite ((double)result.held_out_max_relative_badness))
    {
      fprintf (stderr, "Held-out focus residual is invalid: %.9g\n",
               (double)result.held_out_max_relative_badness);
      return false;
    }
  if (std::fabs (result.joint_fit.screen_blur_radius - (coord_t)0.82) > 0.12)
    {
      fprintf (stderr, "Joint focus blur mismatch: %.9g\n",
               (double)result.joint_fit.screen_blur_radius);
      return false;
    }
  if (!(result.leave_one_out_focus_span >= 0)
      || !(result.leave_one_out_focus_max_delta >= 0)
      || result.leave_one_out_focus_max_delta > 0.20)
    {
      fprintf (stderr,
               "Unexpected leave-one-out focus stability: span %.9g delta %.9g\n",
               (double)result.leave_one_out_focus_span,
               (double)result.leave_one_out_focus_max_delta);
      return false;
    }

  /* Candidate ordering must not materially change the common focus estimate.
     The selection routine is deterministic and the joint model is symmetric
     in its explicit tile list apart from harmless optimizer ordering noise.  */
  std::reverse (candidates.begin (), candidates.end ());
  finetune_focus_analysis_result reversed;
  if (!finetune_analyze_focus_areas (rparam, geometry, image, candidates,
                                     fparam, analysis, &reversed, nullptr))
    return false;
  if (std::fabs (reversed.joint_fit.screen_blur_radius
                 - result.joint_fit.screen_blur_radius)
      > 0.03)
    {
      fprintf (stderr, "Focus analysis depends on candidate ordering\n");
      return false;
    }

  analysis.leave_one_out = false;
  finetune_focus_analysis_result no_loo;
  if (!finetune_analyze_focus_areas (rparam, geometry, image, candidates,
                                     fparam, analysis, &no_loo, nullptr)
      || !no_loo.leave_one_out_fits.empty ()
      || !no_loo.held_out_fits.empty ()
      || no_loo.leave_one_out_focus_span >= 0
      || no_loo.leave_one_out_focus_max_delta >= 0)
    {
      fprintf (stderr, "Leave-one-out disable switch is not respected\n");
      return false;
    }

  fparam.flags |= finetune_coordinates;
  finetune_focus_analysis_result invalid;
  if (finetune_analyze_focus_areas (rparam, geometry, image, candidates,
                                    fparam, analysis, &invalid, nullptr)
      || invalid.err.empty ())
    {
      fprintf (stderr,
               "Coordinate-discovery mode was accepted by area analysis\n");
      return false;
    }
  return true;
}

static bool
test_sky_candidate_budget ()
{
  constexpr int width = 96;
  constexpr int height = 24;
  std::vector<rgbdata> data ((size_t)width * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      data[(size_t)y * width + x]
          = x < 72 ? rgbdata{ 0.20, 0.45, 0.80 }
                   : rgbdata{ 0.75, 0.22, 0.12 };

  finetune_focus_area_search_parameters search;
  search.window_width = search.window_height = 8;
  search.scan_step = 4;
  search.max_candidates = 4;
  search.max_relative_rms = (coord_t)0.001;
  search.max_relative_gradient = (coord_t)0.05;
  search.minimum_separation = 8;

  std::vector<finetune_focus_area_candidate> candidates;
  if (!finetune_find_focus_area_candidates (data.data (), width, height,
                                             width, search, &candidates)
      || candidates.size () != 4)
    return false;
  for (const auto &candidate : candidates)
    if (candidate.mean_color.red > (luminosity_t)0.5)
      return true;
  fprintf (stderr, "Flat sky exhausted the focus candidate budget\n");
  return false;
}

static bool
test_bw_selection_uses_primary_intensities ()
{
  std::vector<finetune_focus_area_candidate> candidates (3);
  const rgbdata intensities[3]
      = { { 0.8, 0.1, 0.1 }, { 0.1, 0.8, 0.1 }, { 0.1, 0.1, 0.8 } };
  for (int i = 0; i < 3; i++)
    {
      candidates[i].mean_color = { 0.4, 0.4, 0.4 };
      candidates[i].fit.success = true;
      candidates[i].fit.badness = (coord_t)0.01;
      candidates[i].fit.contrast = (coord_t)0.2;
      candidates[i].fit.color = intensities[i];
      candidates[i].search_score = i;
    }

  finetune_focus_area_selection_parameters selection;
  selection.min_areas = selection.max_areas = 3;
  selection.minimum_color_volume = (coord_t)1e-4;
  std::vector<size_t> selected;
  coord_t volume = 0;
  if (!finetune_select_focus_areas (candidates, selection, &selected, &volume)
      || selected.size () != 3 || !(volume > selection.minimum_color_volume))
    {
      fprintf (stderr,
               "BW primary-intensity diversity was not used (volume %.9g)\n",
               (double)volume);
      return false;
    }
  return true;
}

static bool
build_bw_focus_fixture (image_data *image, scr_to_img_parameters *geometry,
                        std::vector<point_t> *locations)
{
  constexpr int width = 384;
  constexpr int height = 96;
  constexpr int regions = 3;
  constexpr coord_t true_blur = (coord_t)0.82;
  const rgbdata truth[regions]
      = { { 0.80, 0.12, 0.08 }, { 0.10, 0.75, 0.20 },
          { 0.10, 0.12, 0.80 } };
  const point_t loc[regions]
      = { { 64, 48 }, { 192, 48 }, { 320, 48 } };

  if (!image->set_dimensions (width, height, false, true))
    return false;
  geometry->type = Paget;
  geometry->center = { 0, 0 };
  geometry->coordinate1 = { 8, 0 };
  geometry->coordinate2 = { 0, 8 };
  scr_to_img map;
  if (!map.set_parameters (*geometry, *image))
    return false;
  const coord_t pixel_size = map.pixel_size ({ 0, 0, width, height });

  screen source;
  source.initialize (geometry->type);
  std::array<std::unique_ptr<screen>, regions> blurred;
  for (int tileid = 0; tileid < regions; tileid++)
    {
      blurred[tileid] = std::make_unique<screen> ();
      blurred[tileid]->initialize_with_blur (source, true_blur * pixel_size);
    }
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const int tileid = std::min (x / (width / regions), regions - 1);
        const rgbdata screen_value = blurred[tileid]->interpolated_mult (
            map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 }));
        luminosity_t value
            = screen_value.red * truth[tileid].red
              + screen_value.green * truth[tileid].green
              + screen_value.blue * truth[tileid].blue;
        value = std::clamp (value, (luminosity_t)0, (luminosity_t)1);
        image->put_pixel (
            x, y, (image_data::gray)(value * 65535 + (luminosity_t)0.5));
      }
  locations->assign (loc, loc + regions);
  return true;
}

static bool
test_bw_multitile_focus ()
{
  image_data image;
  scr_to_img_parameters geometry;
  std::vector<point_t> locations;
  if (!build_bw_focus_fixture (&image, &geometry, &locations))
    return false;

  render_parameters rparam;
  rparam.gamma = 1;
  rparam.screen_blur_radius = (coord_t)0.3;
  rparam.sharpen.mode = sharpen_parameters::none;
  rparam.sharpen.scanner_mtf_scale = 0;
  finetune_parameters fparam;
  fparam.range = 2;
  fparam.ignore_outliers = 0;
  fparam.flags = finetune_screen_blur | finetune_bw | finetune_no_normalize
                 | finetune_no_data_collection;

  finetune_result result
      = finetune (rparam, geometry, image, locations, nullptr, fparam, nullptr);
  if (!result.success)
    {
      fprintf (stderr, "BW multi-tile focus fit failed: %s\n",
               result.err.c_str ());
      return false;
    }
  if (std::fabs (result.screen_blur_radius - (coord_t)0.82) > 0.20)
    {
      fprintf (stderr, "BW multi-tile focus mismatch: %.9g\n",
               (double)result.screen_blur_radius);
      return false;
    }
  if (!(result.color.red > result.color.green
        && result.color.red > result.color.blue))
    {
      fprintf (stderr, "BW first-tile primary intensities were not exported\n");
      return false;
    }
  return true;
}

/* Coupled physical fitting does not need a unique sigma/defocus split, but
   it must recover the effective transfer at the process-screen carrier from a
   cold start.  */
static bool
test_coupled_physical_focus_recovers_screen_mtf ()
{
  constexpr int width = 384;
  constexpr int height = 96;
  constexpr int regions = 3;
  const point_t locations[regions]
      = { { 64, 48 }, { 192, 48 }, { 320, 48 } };
  const rgbdata truth[regions]
      = { { 0.80, 0.12, 0.08 }, { 0.10, 0.75, 0.20 },
          { 0.10, 0.12, 0.80 } };

  image_data image;
  if (!image.set_dimensions (width, height, false, true))
    return false;
  scr_to_img_parameters geometry;
  geometry.type = Paget;
  geometry.center = { 0, 0 };
  geometry.coordinate1 = { 8, 0 };
  geometry.coordinate2 = { 0, 8 };
  scr_to_img map;
  if (!map.set_parameters (geometry, image))
    return false;
  const coord_t pixel_size = map.pixel_size ({ 0, 0, width, height });
  sharpen_parameters capture;
  capture.mode = sharpen_parameters::none;
  capture.scanner_mtf_scale = pixel_size;
  capture.scanner_mtf.model = mtf_model::physical_diffraction;
  capture.scanner_mtf.pixel_pitch = 3.76;
  capture.scanner_mtf.f_stop = 8;
  capture.scanner_mtf.scan_dpi = 2000;
  capture.scanner_mtf.wavelength = 750;
  capture.scanner_mtf.sigma = (coord_t)0.70;
  capture.scanner_mtf.defocus = (coord_t)0.16;
  screen source, filtered;
  source.initialize (Paget);
  sharpen_parameters *sp[3] = { &capture, &capture, &capture };
  if (!filtered.initialize_with_sharpen_parameters (source, sp, false, false))
    return false;

  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const int tileid = std::min (x / (width / regions), regions - 1);
        const rgbdata screen_value = filtered.interpolated_mult (
            map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 }));
        luminosity_t value
            = screen_value.red * truth[tileid].red
              + screen_value.green * truth[tileid].green
              + screen_value.blue * truth[tileid].blue;
        value = std::clamp (value, (luminosity_t)0, (luminosity_t)1);
        image.put_pixel (
            x, y, (image_data::gray)(value * 65535 + (luminosity_t)0.5));
      }

  std::vector<finetune_focus_area_candidate> candidates;
  for (int i = 0; i < regions; i++)
    {
      finetune_focus_area_candidate candidate;
      candidate.center = locations[i];
      candidate.mean_color = truth[i];
      candidate.search_score = (coord_t)i * (coord_t)0.001;
      candidate.fit.success = true;
      candidate.fit.badness = (coord_t)0.01;
      candidate.fit.contrast = (coord_t)0.2;
      candidate.fit.color = truth[i];
      candidate.fit.screen_coord_adjust = { 0, 0 };
      candidate.fit.emulsion_coord_adjust = { 0, 0 };
      candidates.push_back (candidate);
    }

  render_parameters rparam;
  rparam.gamma = 1;
  rparam.sharpen.mode = sharpen_parameters::none;
  rparam.sharpen.scanner_mtf_scale = 1;
  rparam.sharpen.scanner_mtf = capture.scanner_mtf;
  rparam.sharpen.scanner_mtf.sigma = 0;
  rparam.sharpen.scanner_mtf.defocus = 0;

  finetune_parameters fparam;
  fparam.range = 2;
  fparam.ignore_outliers = 0;
  fparam.flags = finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus
                 | finetune_bw | finetune_no_normalize
                 | finetune_no_data_collection;
  finetune_focus_analysis_parameters analysis;
  analysis.selection.min_areas = 3;
  analysis.selection.max_areas = 3;
  analysis.selection.minimum_color_volume = 0;
  analysis.leave_one_out = false;
  analysis.held_out = false;

  finetune_focus_analysis_result cold;
  if (!finetune_analyze_focus_areas (rparam, geometry, image, candidates,
                                     fparam, analysis, &cold, nullptr))
    {
      fprintf (stderr, "Cold coupled physical focus fit failed: %s\n",
               cold.err.c_str ());
      return false;
    }

  /* Use the exact forward-model capture parameters as a good warm basin, then
     require the cold multi-start fit to recover the same effective carrier
     transfer.  This tests the quantity needed for colour recovery without
     depending on the non-exported low-level MTF evaluator in this executable. */
  render_parameters warm_rparam = rparam;
  warm_rparam.sharpen.scanner_mtf.sigma = capture.scanner_mtf.sigma;
  warm_rparam.sharpen.scanner_mtf.defocus = capture.scanner_mtf.defocus;
  finetune_focus_analysis_result warm;
  if (!finetune_analyze_focus_areas (warm_rparam, geometry, image, candidates,
                                     fparam, analysis, &warm, nullptr))
    {
      fprintf (stderr, "Warm coupled physical focus fit failed: %s\n",
               warm.err.c_str ());
      return false;
    }
  if (!(cold.joint_screen_mtf > 0) || !(warm.joint_screen_mtf > 0)
      || std::fabs (cold.joint_screen_mtf - warm.joint_screen_mtf)
             > (coord_t)0.04)
    {
      fprintf (stderr,
               "Cold/warm carrier MTF mismatch: cold %.6f warm %.6f "
               "(cold badness %.9g sigma %.6f defocus %.6f seed %d; "
               "warm badness %.9g sigma %.6f defocus %.6f seed %d)\n",
               (double)cold.joint_screen_mtf, (double)warm.joint_screen_mtf,
               (double)cold.joint_fit.badness,
               (double)cold.joint_fit.scanner_mtf_sigma,
               (double)cold.joint_fit.scanner_mtf_defocus,
               cold.joint_seed_kind,
               (double)warm.joint_fit.badness,
               (double)warm.joint_fit.scanner_mtf_sigma,
               (double)warm.joint_fit.scanner_mtf_defocus,
               warm.joint_seed_kind);
      return false;
    }
  return true;
}

static bool
test_grayscale_image_search ()
{
  image_data image;
  scr_to_img_parameters geometry;
  std::vector<point_t> locations;
  if (!build_bw_focus_fixture (&image, &geometry, &locations))
    return false;
  render_parameters rparam;
  rparam.gamma = 1;
  rparam.sharpen.mode = sharpen_parameters::none;
  rparam.sharpen.scanner_mtf_scale = 0;
  finetune_focus_area_image_search_parameters search;
  search.max_analysis_dimension = 384;
  search.search.max_candidates = 12;
  search.search.max_relative_rms = (coord_t)0.08;
  search.search.max_relative_gradient = (coord_t)0.25;
  std::vector<finetune_focus_area_candidate> candidates;
  std::string error;
  if (!finetune_find_focus_area_candidates_in_image (
          rparam, geometry, image, search, &candidates, nullptr, &error))
    {
      fprintf (stderr, "Grayscale image focus search failed: %s\n",
               error.c_str ());
      return false;
    }
  if (candidates.empty ())
    {
      fprintf (stderr, "Grayscale image focus search found no areas\n");
      return false;
    }
  return true;
}


int
main ()
{
  if (!test_joint_focus_analysis ()
      || !test_sky_candidate_budget ()
      || !test_bw_selection_uses_primary_intensities ()
      || !test_bw_multitile_focus ()
      || !test_coupled_physical_focus_recovers_screen_mtf ()
      || !test_grayscale_image_search ())
    return 1;
  printf ("focus-analysis-unittests: PASS\n");
  return 0;
}
