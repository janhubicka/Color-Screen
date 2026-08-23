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

int
main ()
{
  if (!test_joint_focus_analysis ())
    return 1;
  printf ("focus-analysis-unittests: PASS\n");
  return 0;
}
