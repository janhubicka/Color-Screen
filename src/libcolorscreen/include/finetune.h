#ifndef FINETUNE_H
#define FINETUNE_H
#include "base.h"
#include "color.h"
#include "colorscreen.h"
#include <memory>
#include <string>
#include <vector>
namespace colorscreen
{
class screen;
struct render_parameters;
struct scr_to_img_parameters;
struct scr_detect_parameters;
class image_data;

/* Default minimum fitted screen contrast accepted by automatic and adaptive
   finetune reductions.  A caller-supplied zero explicitly disables the
   positive contrast floor while retaining numerical validity checks.  */
constexpr luminosity_t finetune_default_min_contrast
    = (luminosity_t)1 / (luminosity_t)1024;
/* Nonlinear and auxiliary operations enabled for FINETUNE.  Legacy scalar
   and per-channel screen blur are mutually exclusive.  Scanner-MTF sigma may
   be combined with either scalar or per-channel defocus, but scanner-MTF and
   legacy screen-blur optimization must not be mixed.  */
enum finetune_flags : uint64_t
{
  /* Refine the screen phase/translation inside every input tile.  */
  finetune_position = 1 << 0,
  /* Fit one legacy Gaussian screen-blur radius in scan pixels.  */
  finetune_screen_blur = 1 << 1,
  /* Fit independent legacy Gaussian screen-blur radii for RGB.  */
  finetune_screen_channel_blurs = 1 << 2,
  /* Fit the residual Gaussian sigma of the scanner/camera MTF, in pixels.  */
  finetune_scanner_mtf_sigma = 1 << 3,
  /* Fit physical defocus in millimetres, or compact blur-disk diameter in
     pixels when the selected MTF model lacks physical capture metadata.  */
  finetune_scanner_mtf_defocus = 1 << 4,
  /* Fit screen strip widths where the process permits varying widths.  */
  finetune_strips = 1 << 5,
  /* Fit RGB fog/dark offset.  */
  finetune_fog = 1 << 6,
  /* Force use of the grayscale/IR channel instead of RGB.  */
  finetune_bw = 1 << 7,
  /* Disable the fast patch-color data-collection estimate.  */
  finetune_no_data_collection = 1 << 8,
  /* Disable variable projection of linear screen colors by least squares.  */
  finetune_no_least_squares = 1 << 9,
  /* Suppress the inner simplex progress task; cancellation is still polled.  */
  finetune_no_progress_report = 1 << 10,
  /* Do not normalize RGB pixels to remove the approximately neutral image
     layer before registration.  */
  finetune_no_normalize = 1 << 11,
  /* Fit blur in the photographic emulsion, in screen-period coordinates.  */
  finetune_emulsion_blur = 1 << 12,
  /* Print fitted values and tile information.  */
  finetune_verbose = 1 << 13,
  /* Use caller-provided strip widths as the initial/fixed values.  */
  finetune_use_strip_widths = 1 << 14,
  /* Use caller-provided legacy screen blur instead of the small-blur start.  */
  finetune_use_screen_blur = 1 << 15,
  /* Experimental RGB objective that fits mixing weights and a scalar dark
     term while enforcing a neutral simulated-IR response.  This is distinct
     from FINETUNE_BW, whose renderer automatically derives grayscale from RGB
     when no measured IR channel is available.  */
  finetune_simulate_infrared = 1 << 16,
  /* Fit sharpening radius and amount applied to the measured tile.  */
  finetune_sharpening = 1 << 17,
  /* Fit scanner/camera defocus or compact blur independently for RGB.  */
  finetune_scanner_mtf_channel_defocus = 1 << 18,
  /* Refine scale and rotation around the supplied geometry.  */
  finetune_coordinates = 1 << 19,
  /* Search broadly for scale and rotation rather than refining a known map.  */
  finetune_guess_coordinates = 1 << 20,
  /* Retain diagnostic images in FINETUNE_RESULT.  */
  finetune_produce_images = 1 << 21,
  /* Model every explicitly supplied RGB tile as a locally uniform image
     layer.  The historical screen-primary responses and capture transfer are
     shared by all tiles, while each tile gets three scalar intensities that
     dim the red, green and blue screen primaries before capture blur.  This
     is intended for joint focus analysis of several differently coloured
     solid areas.  */
  finetune_uniform_image_layer = 1 << 22
};

/* Lightweight counters collected by FINETUNE.  Times use steady-clock
   nanoseconds and are intended for relative performance diagnosis, not for
   benchmarking across different machines or builds.  Cache hits include a
   thread that waited for another thread to finish constructing the same exact
   entry; the screen-filter time is charged only to the thread that performed
   the construction.  */
struct finetune_profile
{
  uint64_t simplex_runs = 0;
  uint64_t simplex_iterations = 0;
  uint64_t simplex_evaluations = 0;
  uint64_t objective_evaluations = 0;

  uint64_t screen_init_calls = 0;
  uint64_t screen_state_reuses = 0;
  uint64_t fixed_screen_cache_hits = 0;
  uint64_t fixed_screen_cache_misses = 0;
  uint64_t focus_screen_cache_hits = 0;
  uint64_t focus_screen_cache_misses = 0;
  uint64_t focus_screen_local_node_hits = 0;
  uint64_t focus_screen_local_node_misses = 0;
  uint64_t focus_source_cache_hits = 0;
  uint64_t focus_source_cache_misses = 0;
  uint64_t focus_screen_interpolations = 0;
  uint64_t focus_screen_exact_node_uses = 0;
  uint64_t focus_screen_final_exact_builds = 0;
  uint64_t exact_screen_builds = 0;

  uint64_t mtf_precompute_calls = 0;
  uint64_t mtf_psf_precompute_calls = 0;
  uint64_t physical_focus_cache_hits = 0;
  uint64_t physical_focus_cache_misses = 0;
  uint64_t physical_focus_transfer_builds = 0;
  uint64_t empirical_focus_transfer_builds = 0;
  uint64_t direct_transfer_builds = 0;
  uint64_t wrapped_psf_builds = 0;
  uint64_t kernel_forward_ffts = 0;
  uint64_t screen_forward_ffts = 0;
  uint64_t screen_inverse_ffts = 0;

  uint64_t objective_nanoseconds = 0;
  uint64_t screen_filter_nanoseconds = 0;
  uint64_t screen_cache_nanoseconds = 0;
  uint64_t screen_interpolation_nanoseconds = 0;
  uint64_t screen_simulation_nanoseconds = 0;
  uint64_t color_estimation_nanoseconds = 0;
  uint64_t residual_nanoseconds = 0;

  finetune_profile &
  operator+= (const finetune_profile &o)
  {
    simplex_runs += o.simplex_runs;
    simplex_iterations += o.simplex_iterations;
    simplex_evaluations += o.simplex_evaluations;
    objective_evaluations += o.objective_evaluations;
    screen_init_calls += o.screen_init_calls;
    screen_state_reuses += o.screen_state_reuses;
    fixed_screen_cache_hits += o.fixed_screen_cache_hits;
    fixed_screen_cache_misses += o.fixed_screen_cache_misses;
    focus_screen_cache_hits += o.focus_screen_cache_hits;
    focus_screen_cache_misses += o.focus_screen_cache_misses;
    focus_screen_local_node_hits += o.focus_screen_local_node_hits;
    focus_screen_local_node_misses += o.focus_screen_local_node_misses;
    focus_source_cache_hits += o.focus_source_cache_hits;
    focus_source_cache_misses += o.focus_source_cache_misses;
    focus_screen_interpolations += o.focus_screen_interpolations;
    focus_screen_exact_node_uses += o.focus_screen_exact_node_uses;
    focus_screen_final_exact_builds += o.focus_screen_final_exact_builds;
    exact_screen_builds += o.exact_screen_builds;
    mtf_precompute_calls += o.mtf_precompute_calls;
    mtf_psf_precompute_calls += o.mtf_psf_precompute_calls;
    physical_focus_cache_hits += o.physical_focus_cache_hits;
    physical_focus_cache_misses += o.physical_focus_cache_misses;
    physical_focus_transfer_builds += o.physical_focus_transfer_builds;
    empirical_focus_transfer_builds += o.empirical_focus_transfer_builds;
    direct_transfer_builds += o.direct_transfer_builds;
    wrapped_psf_builds += o.wrapped_psf_builds;
    kernel_forward_ffts += o.kernel_forward_ffts;
    screen_forward_ffts += o.screen_forward_ffts;
    screen_inverse_ffts += o.screen_inverse_ffts;
    objective_nanoseconds += o.objective_nanoseconds;
    screen_filter_nanoseconds += o.screen_filter_nanoseconds;
    screen_cache_nanoseconds += o.screen_cache_nanoseconds;
    screen_interpolation_nanoseconds += o.screen_interpolation_nanoseconds;
    screen_simulation_nanoseconds += o.screen_simulation_nanoseconds;
    color_estimation_nanoseconds += o.color_estimation_nanoseconds;
    residual_nanoseconds += o.residual_nanoseconds;
    return *this;
  }
};

/* Configuration and optional diagnostic outputs for one FINETUNE call.  */
struct finetune_parameters
{
  /* Bitwise OR of FINETUNE_FLAGS.  */
  uint64_t flags = 0;
  /* Half-extent of the analyzed tile in periodic screen-cell coordinates.
     The corresponding image-pixel rectangle is derived from the local map.
     Zero selects a mode-dependent default.  */
  int range = 0;
  /* Search an odd MULTITILE by MULTITILE neighbourhood and keep the tile with
     the lowest contrast-scaled fit score.  */
  int multitile = 1;
  /* Fraction of largest residuals excluded before the final refinement.  */
  coord_t ignore_outliers = 0.1;
  /* Optional paths for diagnostic images.  Null disables an output.  */
  const char *simulated_file = nullptr;
  const char *orig_file = nullptr;
  const char *sharpened_file = nullptr;
  const char *diff_file = nullptr;
  const char *screen_file = nullptr;
  const char *screen_blur_file = nullptr;
  const char *emulsion_file = nullptr;
  const char *merged_file = nullptr;
  const char *collected_file = nullptr;
  const char *dot_spread_file = nullptr;
  /* Collect detailed cache, FFT and timing counters in FINETUNE_RESULT.
     Disabled by default so ordinary geometry/focus fitting pays no profiling
     clock or atomic-counter overhead.  */
  bool collect_profile = false;

  /* Approximate scalar physical image-plane defocus during a fit by linearly
     interpolating periodic screens cached at a fixed nonlinear grid of exact
     nodes.  This is intended for the dense displacement-analysis pass after
     an exact coarse prepass and is accepted only when physical defocus is the
     sole varying capture-transfer parameter.  Metadata-free empirical fallback
     blur deliberately remains exact because its multimodal blur/color
     objective is not interpolation-stable.

     SCANNER_MTF_DEFOCUS_INTERPOLATION_MAX is the useful nonnegative defocus
     range in millimetres.  SCANNER_MTF_DEFOCUS_INTERPOLATION_NODES includes
     both endpoints; nodes are quadratically spaced to provide finer resolution
     near best focus.  The final fitted point is always evaluated with an exact
     screen and is not inserted into the node cache.  */
  bool interpolate_scanner_mtf_defocus = false;
  coord_t scanner_mtf_defocus_interpolation_max = 0;
  int scanner_mtf_defocus_interpolation_nodes = 49;
  finetune_parameters () {}
};

/* Result of matching one or more scan tiles to the simulated additive-screen
   capture.  Numerical fields are valid only when SUCCESS is true.  A failed
   solver may have filled intermediate fields before detecting the failure, so
   callers must ignore the result except for SUCCESS and ERR.  */
struct finetune_result
{
  bool success = false;
  /* Centre of the selected input tile in image pixels.  */
  point_t tile_pos = { -1, -1 };
  /* Final robust objective.  This includes the small blur-growth penalty and
     is therefore not a pure image residual.  */
  coord_t badness = 12345;
  /* Minimum objective divided by positional color contrast.  This historical
     field name is retained for compatibility; the value is a heuristic
     fit-quality score, not a statistical uncertainty or simplex spread.
     Lower is better.  */
  coord_t uncertainty = 12345;
  /* Legacy Gaussian blur radius in scan pixels.  */
  coord_t screen_blur_radius = -1;
  rgbdata screen_channel_blur_radius = { -1, -1, -1 };
  /* Scanner/camera residual Gaussian sigma in scan pixels.  */
  luminosity_t scanner_mtf_sigma = -1;
  /* Compact blur-disk diameter in scan pixels.  On a successful MTF focus
     fit, the active focus field is updated and the inactive field preserves
     the corresponding value supplied in render_parameters.  This lets
     callers copy both fields back without corrupting a later model switch.  */
  luminosity_t scanner_mtf_blur_diameter = -1;
  /* Physical image-plane defocus in millimetres; see the contract above.  */
  luminosity_t scanner_mtf_defocus = -1;
  /* Registration contrast derived from fitted screen colors.  This is also
     the adaptive blur/focus identifiability signal: a finite optimizer result
     with too little fitted screen modulation is not a reliable measurement.  */
  luminosity_t contrast = 0;
  /* Per-channel physical defocus or compact blur diameter; interpretation is
     selected by the active scanner MTF model.  */
  rgbdata scanner_mtf_channel_defocus_or_blur = { -1, -1, -1 };
  /* Emulsion blur in screen-period coordinates.  */
  coord_t emulsion_blur_radius = -1;
  /* Fractions of one periodic screen cell.  */
  coord_t red_strip_width = -1;
  coord_t green_strip_width = -1;
  /* Registration and emulsion offsets in screen-period coordinates.  */
  point_t screen_coord_adjust = { -1, -1 };
  point_t emulsion_coord_adjust = { -1, -1 };
  /* Fitted BW patch intensities or legacy color summary.  */
  rgbdata color = { -1, -1, -1 };
  /* For FINETUNE_UNIFORM_IMAGE_LAYER, per-tile scalar image-layer
     intensities multiplying the shared red, green and blue screen primaries.
     Entries follow LOCS order.  Empty for ordinary RGB/BW fits and failure.  */
  std::vector<rgbdata> tile_primary_intensities;
  /* Scanner RGB response to ideal red, green and blue screen primaries.  */
  rgbdata screen_red = { -1, -1, -1 }, screen_green = { -1, -1, -1 },
          screen_blue = { -1, -1, -1 };
  rgbdata fog = { 0, 0, 0 };
  /* Experimental RGB-to-simulated-IR parameters.  When that mode fits a
     scalar post-mixing dark term, it is returned as an equivalent neutral
     pre-mixing RGB dark value.  Other modes preserve the input RGB dark.  */
  rgbdata mix_weights = { -1, -1, -1 };
  rgbdata mix_dark = { -1, -1, -1 };
  std::string err;

  /* Work performed by this FINETUNE call when COLLECT_PROFILE was enabled.
     The snapshot remains useful on failure and is independent of the validity
     of the numerical result fields.  */
  finetune_profile profile;

  /* Solver point data.  */
  point_t solver_point_img_location = { -1, -1 };
  point_t solver_point_screen_location = { -1, -1 };
  enum solver_parameters::point_color solver_point_color
      = solver_parameters::max_point_color;

  point_t center = { 0, 0 }, coordinate1 = { 0, 0 }, coordinate2 = { 0, 0 };

  /* Solver images  */
  std::shared_ptr<simple_image> diff;
  std::shared_ptr<simple_image> simulated;
  std::shared_ptr<simple_image> sharpened;
  std::shared_ptr<simple_image> orig;
  std::shared_ptr<simple_image> screen;
  std::shared_ptr<simple_image> blurred_screen;
  std::shared_ptr<simple_image> emulsion_screen;
  std::shared_ptr<simple_image> merged_screen;
  std::shared_ptr<simple_image> collected_screen;
  std::shared_ptr<simple_image> dot_spread;

  finetune_result () {}

  // finetune_result(finetune_result&&) = default;
  // finetune_result& operator=(finetune_result&&) = default;
};

/* Match scan tiles to a simulated additive-screen capture.

   For normal refinement LOCS contains between one and eight image-pixel
   centres.  With FINETUNE_COORDINATES or FINETUNE_GUESS_COORDINATES, LOCS
   must be empty and one geometry-discovery tile is generated around
   PARAM.CENTER.  RESULTS, when nonnull, provides starting offsets for every
   selected tile and must contain at least the same number of entries.
   PROGRESS may be null.

   Result offsets are fractions of a periodic screen cell.  Legacy blur,
   residual scanner sigma and compact blur diameter are in scan pixels;
   physical defocus is in millimetres.  On failure SUCCESS is false, ERR
   describes the first detected problem when available, and all numerical
   fields must be ignored.  */
DLL_PUBLIC finetune_result
finetune (const render_parameters &rparam, const scr_to_img_parameters &param,
          const image_data &img, const std::vector<point_t> &locs,
          const std::vector<finetune_result> *results,
          const finetune_parameters &fparams, progress_info *progress);

struct finetune_area_parameters
{
  /* Requested grid width.  Zero lets get_grid_dimensions() choose it.  */
  int grid_width = 0;
  /* Requested grid height.  Zero lets get_grid_dimensions() choose it.  */
  int grid_height = 0;
  /* Minimum positional colour contrast accepted during automatic detection.
     A meaningful range is approximately 0 to 1/16.  */
  luminosity_t min_contrast = finetune_default_min_contrast;
  /* Fraction of the most reliable successful fits to retain.  */
  luminosity_t uncertainty_ratio = 0.8;
  /* Maximum accepted registration displacement in screen-period units.  A
     meaningful range is approximately 0 to 0.2.  */
  luminosity_t max_displacement = 0.05;

  /* Determine grid WIDTH and HEIGHT for CROP and PARAM.  */
  void
  get_grid_dimensions (const int_image_area &crop, const scr_to_img_parameters &param, int *width, int *height) const
  {
    int grid_w = grid_width;
    int grid_h = grid_height;
    int scalex = 1, scaley = 1;
    if (param.scanner_type == lens_move_horizontally
	|| param.scanner_type == fixed_lens_sensor_move_horizontally)
      scalex *= 3;
    else if (param.scanner_type == lens_move_vertically
	     || param.scanner_type == fixed_lens_sensor_move_vertically)
      scaley *= 3;
    if (!grid_w && !grid_h)
      {
        int n = 100;
	/* Dufaycolor has a flexible base that is prone to deformation.  */
	if (param.type == Dufay)
	  n = 300;
        if (crop.width > crop.height)
	  grid_w = n * scalex;
	else
	  grid_h = n * scaley;
      }
    if (!grid_w)
      grid_w = nearest_int (grid_h * (luminosity_t)crop.width / crop.height * scalex / scaley);
    if (!grid_h)
      grid_h = nearest_int (grid_w * (luminosity_t)crop.height / crop.width * scaley / scalex);
    *width = grid_w;
    *height = grid_h;
  }
};
nodiscard_attr DLL_PUBLIC bool
finetune_area (solver_parameters *sparam, render_parameters &rparam,
               const scr_to_img_parameters &param, const image_data &img,
               const int_image_area &area, const finetune_area_parameters &fparam, progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
finetune_misregistered_area (solver_parameters *solver, render_parameters &rparam,
			     const scr_to_img_parameters &param, const image_data &img,
			     const int_image_area &area, const struct finetune_area_parameters &fparam, progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
render_screen (image_data &img, const scr_to_img_parameters &param,
               const render_parameters &rparam, const scr_detect_parameters &dparam,
	       int width, int height);
nodiscard_attr DLL_PUBLIC bool
autodetect_coordinates (const image_data &img, scr_to_img_parameters &param,
		        const render_parameters &rparam, progress_info *progress);
} // namespace colorscreen
#endif
