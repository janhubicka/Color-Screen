#ifdef _OPENMP
#include <omp.h>
#endif
#include <algorithm>
#include <climits>
#include <limits>
#include <sstream>
#include <vector>
#include "finetune-int.h"
#include "include/analyze-scanner-blur.h"
#include "include/scr-to-img.h"
namespace colorscreen
{
namespace
{
/* Return the fitted correction represented by RES in MODE.  */
coord_t
get_correction (scanner_blur_correction_parameters::correction_mode mode,
                const finetune_result &res)
{
  switch (mode)
    {
    case scanner_blur_correction_parameters::blur_radius:
      return res.screen_blur_radius;
    case scanner_blur_correction_parameters::mtf_defocus:
      return res.scanner_mtf_defocus;
    case scanner_blur_correction_parameters::mtf_blur_diameter:
      return res.scanner_mtf_blur_diameter;
    case scanner_blur_correction_parameters::max_correction:
      abort ();
    }
  abort ();
}

/* Diagnostic counts for one adaptive-analysis pass or correction-table cell.
   These categories deliberately separate weak image information from solver
   failure and invalid numerical output.  */
struct fit_diagnostics
{
  size_t usable = 0;
  size_t solver_failures = 0;
  size_t invalid_contrasts = 0;
  size_t low_contrasts = 0;
  size_t invalid_fit_scores = 0;
  size_t finite_contrasts = 0;
  luminosity_t contrast_min = std::numeric_limits<luminosity_t>::max ();
  luminosity_t contrast_max = std::numeric_limits<luminosity_t>::lowest ();
  long double contrast_sum = 0;
};

/* Account RESULT in DIAGNOSTICS using MIN_CONTRAST.  */
void
account_fit_diagnostic (fit_diagnostics *diagnostics,
                        const finetune_result &result,
                        luminosity_t min_contrast)
{
  if (result.success && my_isfinite (result.contrast)
      && result.contrast >= 0)
    {
      diagnostics->finite_contrasts++;
      diagnostics->contrast_min
          = std::min (diagnostics->contrast_min, result.contrast);
      diagnostics->contrast_max
          = std::max (diagnostics->contrast_max, result.contrast);
      diagnostics->contrast_sum += result.contrast;
    }

  switch (finetune_classify_result (result, min_contrast))
    {
    case finetune_result_quality::usable:
      diagnostics->usable++;
      break;
    case finetune_result_quality::solver_failure:
      diagnostics->solver_failures++;
      break;
    case finetune_result_quality::invalid_contrast:
      diagnostics->invalid_contrasts++;
      break;
    case finetune_result_quality::low_contrast:
      diagnostics->low_contrasts++;
      break;
    case finetune_result_quality::invalid_fit_score:
      diagnostics->invalid_fit_scores++;
      break;
    }
}

/* Collect diagnostic counts for RESULTS.  */
fit_diagnostics
collect_fit_diagnostics (const std::vector<finetune_result *> &results,
                         luminosity_t min_contrast)
{
  fit_diagnostics diagnostics;
  for (const finetune_result *result : results)
    account_fit_diagnostic (&diagnostics, *result, min_contrast);
  return diagnostics;
}

/* Collect diagnostic counts for a complete worker pass.  */
fit_diagnostics
collect_fit_diagnostics (const std::vector<finetune_result> &results,
                         luminosity_t min_contrast)
{
  fit_diagnostics diagnostics;
  for (const finetune_result &result : results)
    account_fit_diagnostic (&diagnostics, result, min_contrast);
  return diagnostics;
}

/* Return true when RES is an identifiable fit with a usable historical
   fit-quality score.  The public field is named "uncertainty" for
   compatibility, but stores objective divided by registration contrast
   rather than a statistical error estimate.  */
bool
identifiable_result_p (const finetune_result &res,
                       luminosity_t min_contrast)
{
  return finetune_classify_result (res, min_contrast)
         == finetune_result_quality::usable;
}

/* Return true for a scalar correction representable by the current table
   format.  All supported fit coordinates are non-negative.  */
bool
valid_correction_p (coord_t correction)
{
  return my_isfinite (correction) && correction >= 0 && correction <= 1024;
}

/* Compute the fit-score cutoff for RESULTS after discarding SKIPMAX percent
   of the least reliable successful fits.  Store the cutoff in THRESHOLD.  */
bool
find_fit_score_threshold (const std::vector<finetune_result *> &results,
                          coord_t skipmax, luminosity_t min_contrast,
                          coord_t *threshold)
{
  histogram hist;
  int nvalid = 0;
  for (finetune_result *res : results)
    if (identifiable_result_p (*res, min_contrast))
      {
        hist.pre_account (res->uncertainty);
        nvalid++;
      }
  if (!nvalid)
    return false;
  hist.finalize_range (65536);
  for (finetune_result *res : results)
    if (identifiable_result_p (*res, min_contrast))
      hist.account (res->uncertainty);
  hist.finalize ();
  *threshold = hist.find_max (skipmax / (coord_t)100);
  return my_isfinite (*threshold);
}

/* Return true when RES passes the fit-score filter THRESHOLD.  */
bool
accepted_result_p (const finetune_result &res, coord_t threshold,
                   luminosity_t min_contrast)
{
  return identifiable_result_p (res, min_contrast)
         && res.uncertainty <= threshold;
}

/* Pause progress output through PROGRESS when it is available.  */
void
pause_stdout (progress_info *progress)
{
  if (progress)
    progress->pause_stdout ();
}

/* Resume progress output through PROGRESS when it is available.  */
void
resume_stdout (progress_info *progress)
{
  if (progress)
    progress->resume_stdout ();
}

/* Describe why no identifiable fits remain in PHASE.  */
std::string
no_identifiable_fits_message (const char *phase,
                              const fit_diagnostics &diagnostics,
                              luminosity_t min_contrast)
{
  std::ostringstream out;
  out << "Analysis failed";
  if (phase && *phase)
    out << " for " << phase;
  out << ": no identifiable fits at minimum fitted contrast "
      << min_contrast * 100 << "% (" << diagnostics.low_contrasts
      << " low contrast, " << diagnostics.invalid_contrasts
      << " invalid contrast, " << diagnostics.invalid_fit_scores
      << " invalid fit score, " << diagnostics.solver_failures
      << " solver failure";
  if (diagnostics.solver_failures != 1)
    out << 's';
  out << ')';
  return out.str ();
}

/* Print one compact identifiability summary.  The caller controls progress
   output around a group of profile lines.  */
void
print_fit_diagnostics (const char *phase,
                       const fit_diagnostics &diagnostics)
{
  printf ("  %s identifiability: %zu usable, %zu low contrast, %zu invalid "
          "contrast, %zu invalid fit score, %zu solver failure%s",
          phase, diagnostics.usable, diagnostics.low_contrasts,
          diagnostics.invalid_contrasts, diagnostics.invalid_fit_scores,
          diagnostics.solver_failures,
          diagnostics.solver_failures == 1 ? "" : "s");
  if (diagnostics.finite_contrasts)
    printf ("; fitted contrast %.9g%%...%.9g%% (mean %.9g%%)",
            (double)(diagnostics.contrast_min * 100),
            (double)(diagnostics.contrast_max * 100),
            (double)(diagnostics.contrast_sum * 100
                     / diagnostics.finite_contrasts));
  printf ("\n");
}

/* Return true when WIDTH*HEIGHT fits the worker's int-coordinate indexing and
   store the corresponding allocation size in SIZE.  */
bool
valid_grid_size_p (int width, int height, size_t *size)
{
  if (width <= 0 || height <= 0)
    return false;
  const int64_t n = (int64_t)width * height;
  if (n <= 0 || n > INT_MAX)
    return false;
  *size = (size_t)n;
  return true;
}

/* Return true when RPARAM contains an explicit pair of usable strip widths.
   Zero denotes the process default in render parameters, so in that case
   FINETUNE should keep its normal per-process initialization.  */
static bool
explicit_strip_widths_p (const render_parameters &rparam)
{
  return my_isfinite (rparam.red_strip_width)
         && my_isfinite (rparam.green_strip_width)
         && rparam.red_strip_width > 0 && rparam.red_strip_width < 1
         && rparam.green_strip_width > 0 && rparam.green_strip_width < 1;
}
} // namespace

/* Record and print one failure detected by a sequential worker stage.  Local
   FINETUNE calls run in parallel and therefore never update LAST_ERROR.  */
void
analyze_scanner_blur_worker::set_error (const std::string &message)
{
  last_error = message;
  pause_stdout (progress);
  fprintf (stderr, "%s\n", last_error.c_str ());
  resume_stdout (progress);
}

/* Prepare dimensions and storage for the scanner-blur analysis.  */
bool
analyze_scanner_blur_worker::step1 ()
{
  last_error.clear ();
  reduction_profile = {};
  if (const char *flag_error = finetune_flag_error (flags))
    {
      set_error (std::string ("Invalid adaptive finetune settings: ")
                 + flag_error);
      return false;
    }
  if (!my_isfinite (skipmin) || !my_isfinite (skipmax) || skipmin < 0
      || skipmax < 0 || skipmin > 50 || skipmax > 50
      || skipmin + skipmax >= 100 || !my_isfinite (tolerance)
      || !my_isfinite (min_contrast) || min_contrast < 0
      || scan.width <= 0 || scan.height <= 0)
    {
      set_error ("Invalid adaptive finetune grid or reduction settings");
      return false;
    }

  if (interpolate_focus)
    {
      const uint64_t incompatible
          = finetune_scanner_mtf_sigma
            | finetune_scanner_mtf_channel_defocus | finetune_screen_blur
            | finetune_screen_channel_blurs | finetune_emulsion_blur;
      if (!(flags & finetune_scanner_mtf_defocus) || (flags & incompatible)
          || reoptimize_strip_widths
          || !rparam.sharpen.scanner_mtf.simulate_diffraction_p ())
        {
          set_error ("Focus interpolation requires scalar physical defocus "
                     "as the sole varying screen-filter parameter");
          return false;
        }
      if (!my_isfinite (focus_mtf_threshold)
          || focus_mtf_threshold <= 0 || focus_mtf_threshold >= 1)
        {
          set_error ("Focus interpolation MTF threshold must be between 0 "
                     "and 100 percent");
          return false;
        }
      if (focus_interpolation_nodes < 2
          || focus_interpolation_nodes > 64)
        {
          set_error ("Focus interpolation node count must be in [2,64]");
          return false;
        }
    }

  /* The correction table stores one kind of scalar correction.  Scanner MTF
     sigma may be fitted together with defocus, but it is not itself the
     spatially varying correction recorded by this worker.  Combining legacy
     screen blur with the MTF model is rejected by FINETUNE_FLAG_ERROR.  */
  const bool legacy_blur
      = flags & (finetune_screen_blur | finetune_screen_channel_blurs);
  const bool mtf_focus
      = flags
        & (finetune_scanner_mtf_defocus
           | finetune_scanner_mtf_channel_defocus);
  /* Exactly one correction kind must be representable in the output table.
     Sigma may be optimized jointly with focus, but sigma alone has no table
     mode.  */
  if (legacy_blur == mtf_focus)
    {
      set_error ("Adaptive analysis requires exactly one stored blur or "
                 "focus correction family");
      return false;
    }

  if (!xsteps && !ysteps)
    xsteps = 10;
  if (!xsteps)
    xsteps = (int)(((int64_t)ysteps * scan.width + scan.height / 2)
                   / scan.height);
  if (!ysteps)
    ysteps = (int)(((int64_t)xsteps * scan.height + scan.width / 2)
                   / scan.width);
  if (xsteps <= 1)
    xsteps = 2;
  if (ysteps <= 1)
    ysteps = 2;
  if (!ysubsteps)
    ysubsteps = xsubsteps;
  if (!xsubsteps)
    xsubsteps = ysubsteps;
  if (!xsubsteps)
    xsubsteps = ysubsteps = 5;
  if (xsubsteps <= 0 || ysubsteps <= 0)
    {
      set_error ("Adaptive analysis sub-sample dimensions must be positive");
      return false;
    }

  if (!strip_xsteps && !strip_ysteps)
    strip_xsteps = 10;
  if (!strip_xsteps)
    strip_xsteps = (int)(((int64_t)strip_ysteps * scan.width
                          + scan.height / 2)
                         / scan.height);
  if (!strip_ysteps)
    strip_ysteps = (int)(((int64_t)strip_xsteps * scan.height
                          + scan.width / 2)
                         / scan.width);
  if (strip_xsteps <= 0)
    strip_xsteps = 1;
  if (strip_ysteps <= 0)
    strip_ysteps = 1;

  const int64_t sample_width = (int64_t)xsteps * xsubsteps;
  const int64_t sample_height = (int64_t)ysteps * ysubsteps;
  size_t prepass_size;
  size_t mainpass_size;
  if (sample_width > INT_MAX || sample_height > INT_MAX
      || !valid_grid_size_p (strip_xsteps, strip_ysteps, &prepass_size)
      || !valid_grid_size_p ((int)sample_width, (int)sample_height,
                             &mainpass_size))
    {
      set_error ("Adaptive analysis grid is too large");
      return false;
    }
  (void)mainpass_size;

  if (rparam.scanner_blur_correction)
    rparam.scanner_blur_correction.reset ();
#ifdef _OPENMP
  omp_set_nested (1);
#endif
  mode = scanner_blur_correction_parameters::blur_radius;
  if (flags
      & (finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus))
    mode = rparam.sharpen.scanner_mtf.simulate_diffraction_p ()
               ? scanner_blur_correction_parameters::mtf_defocus
               : scanner_blur_correction_parameters::mtf_blur_diameter;

  prepass.assign (prepass_size, finetune_result ());
  if (verbose)
    {
      pause_stdout (progress);
      if (screen_with_varying_strips_p (param.type) && optimize_strip_widths)
        printf ("Analyzing %ix%i areas to determine strip widths and "
                "blur (overall %i solutions to be computed)\n",
                strip_xsteps, strip_ysteps, strip_xsteps * strip_ysteps);
      else
        printf ("Analyzing %ix%i areas to determine blur (overall %i "
                "solutions to be computed)\n",
                strip_xsteps, strip_ysteps, strip_xsteps * strip_ysteps);
      resume_stdout (progress);
    }
  if (progress)
    {
      if (progress->cancel_requested ())
        return false;
      progress->set_task (screen_with_varying_strips_p (param.type)
                                  && optimize_strip_widths
                              ? "analyzing screen strip sizes and blur"
                              : "analyzing screen blur",
                          strip_xsteps * strip_ysteps);
    }
  return true;
}

/* Evaluate coarse prepass sample X,Y.  */
bool
analyze_scanner_blur_worker::analyze_strips (int x, int y,
                                             coord_t *red_strip_width,
                                             coord_t *green_strip_width)
{
  if (x < 0 || x >= strip_xsteps || y < 0 || y >= strip_ysteps)
    return false;
  if (progress && progress->cancel_requested ())
    return false;

  finetune_parameters fparam;
  fparam.flags = flags;
  if (screen_with_varying_strips_p (param.type))
    {
      if (optimize_strip_widths)
        fparam.flags |= finetune_strips;
      /* Start strip optimization from explicitly supplied rendering values,
         or keep those values fixed when optimization is disabled.  With zero
         widths leave FINETUNE_USE_STRIP_WIDTHS clear so process defaults are
         used instead of literal zero-width strips.  */
      if (explicit_strip_widths_p (rparam))
        fparam.flags |= finetune_use_strip_widths;
    }
  fparam.multitile = 1;
  fparam.collect_profile = report_profile;
  finetune_result &res = prepass_result (x, y);
  res = finetune (rparam, param, scan,
                  { { (coord_t)(x + 0.5) * scan.width / strip_xsteps,
                      (coord_t)(y + 0.5) * scan.height / strip_ysteps } },
                  NULL, fparam, progress);
  if (progress)
    progress->inc_progress ();
  const bool identifiable = identifiable_result_p (res, min_contrast);
  if (identifiable)
    {
      if (red_strip_width)
        *red_strip_width = res.red_strip_width;
      if (green_strip_width)
        *green_strip_width = res.green_strip_width;
    }
  return identifiable;
}

/* Reduce the prepass and prepare the dense focus-analysis pass.  */
bool
analyze_scanner_blur_worker::step2 ()
{
  last_error.clear ();
  if (progress && progress->cancel_requested ())
    {
      last_error = "Analysis cancelled.";
      return false;
    }

  if (!prepass.empty ())
    {
      std::vector<finetune_result *> results;
      results.reserve (prepass.size ());
      for (finetune_result &res : prepass)
        results.push_back (&res);

      coord_t fit_score_threshold;
      if (!find_fit_score_threshold (results, skipmax, min_contrast,
                                     &fit_score_threshold))
        {
          set_error (no_identifiable_fits_message (
              "prepass", collect_fit_diagnostics (results, min_contrast),
              min_contrast));
          return false;
        }

      int nok = 0;
      for (finetune_result *res : results)
        {
          if (!accepted_result_p (*res, fit_score_threshold, min_contrast))
            continue;
          const coord_t correction = get_correction (mode, *res);
          if (!valid_correction_p (correction))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && optimize_strip_widths
              && (!my_isfinite (res->red_strip_width)
                  || !my_isfinite (res->green_strip_width)))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && optimize_strip_widths)
            {
              red_hist.pre_account (res->red_strip_width);
              green_hist.pre_account (res->green_strip_width);
            }
          blur_hist.pre_account (correction);
          nok++;
        }
      if (!nok)
        {
          set_error ("Analysis failed: all identifiable prepass fits had "
                     "invalid correction or strip-width values");
          return false;
        }

      if (screen_with_varying_strips_p (param.type)
          && optimize_strip_widths)
        {
          red_hist.finalize_range (65536);
          green_hist.finalize_range (65536);
        }
      blur_hist.finalize_range (65536);
      for (finetune_result *res : results)
        {
          if (!accepted_result_p (*res, fit_score_threshold, min_contrast))
            continue;
          const coord_t correction = get_correction (mode, *res);
          if (!valid_correction_p (correction))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && optimize_strip_widths
              && (!my_isfinite (res->red_strip_width)
                  || !my_isfinite (res->green_strip_width)))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && optimize_strip_widths)
            {
              red_hist.account (res->red_strip_width);
              green_hist.account (res->green_strip_width);
            }
          blur_hist.account (correction);
        }

      if (screen_with_varying_strips_p (param.type)
          && optimize_strip_widths)
        {
          red_hist.finalize ();
          green_hist.finalize ();
        }
      blur_hist.finalize ();
      if (screen_with_varying_strips_p (param.type)
          && optimize_strip_widths)
        {
          rparam.red_strip_width
              = red_hist.find_avg (skipmin / 100, skipmax / 100);
          rparam.green_strip_width
              = green_hist.find_avg (skipmin / 100, skipmax / 100);
          if (!my_isfinite (rparam.red_strip_width)
              || !my_isfinite (rparam.green_strip_width))
            return false;
        }
      const coord_t robust_correction
          = blur_hist.find_avg (skipmin / 100, skipmax / 100);
      if (!valid_correction_p (robust_correction))
        return false;
      switch (mode)
        {
        case scanner_blur_correction_parameters::blur_radius:
          rparam.screen_blur_radius = robust_correction;
          break;
        case scanner_blur_correction_parameters::mtf_defocus:
          rparam.sharpen.scanner_mtf.defocus = robust_correction;
          break;
        case scanner_blur_correction_parameters::mtf_blur_diameter:
          rparam.sharpen.scanner_mtf.blur_diameter = robust_correction;
          break;
        case scanner_blur_correction_parameters::max_correction:
          abort ();
        }
      if (verbose)
        {
          pause_stdout (progress);
          if (screen_with_varying_strips_p (param.type)
              && optimize_strip_widths)
            {
              printf ("Red strip width %.2f%%\n",
                      rparam.red_strip_width * 100);
              printf ("Green strip width %.2f%%\n",
                      rparam.green_strip_width * 100);
            }
          switch (mode)
            {
            case scanner_blur_correction_parameters::blur_radius:
              printf ("Average screen blur %.2f pixels\n",
                      rparam.screen_blur_radius);
              break;
            case scanner_blur_correction_parameters::mtf_defocus:
              printf ("Average mtf defocus %.5f mm\n",
                      rparam.sharpen.scanner_mtf.defocus);
              break;
            case scanner_blur_correction_parameters::mtf_blur_diameter:
              printf ("Average mtf blur diameter %.2f pixels\n",
                      rparam.sharpen.scanner_mtf.blur_diameter);
              break;
            case scanner_blur_correction_parameters::max_correction:
              abort ();
            }
          resume_stdout (progress);
        }
    }

  if (interpolate_focus)
    {
      scr_to_img map;
      if (!map.set_parameters (param, scan))
        return false;
      const coord_t pixel_size
          = map.pixel_size ({ 0, 0, scan.width, scan.height });
      focus_screen_frequency = scr_names[param.type].frequency * pixel_size;
      mtf_parameters focus_mtf = rparam.sharpen.scanner_mtf;
      /* RGB finetuning currently applies one achromatic periodic-screen
         filter and uses 550 nm; mirror CAPTURE_SHARPEN_PARAMETERS exactly
         when deriving the useful focus range.  */
      if (!(flags & finetune_bw) && scan.has_rgb ())
        focus_mtf.wavelength = 550;
      const bool useful_range
          = my_isfinite (focus_screen_frequency)
            && focus_screen_frequency > 0
            && finetune_useful_defocus_limit (
                focus_mtf, focus_screen_frequency, focus_mtf_threshold,
                (coord_t)20, &focus_interpolation_max);
      if (!useful_range)
        {
          pause_stdout (progress);
          fprintf (stderr,
                   "Focus analysis failed: the in-focus MTF at the "
                   "process-screen frequency is at or below %.1f%%\n",
                   focus_mtf_threshold * 100);
          resume_stdout (progress);
          return false;
        }
      const coord_t robust_focus = rparam.sharpen.scanner_mtf.defocus;
      const coord_t tolerance
          = std::numeric_limits<coord_t>::epsilon ()
            * std::max ((coord_t)1, focus_interpolation_max) * 64;
      if (!my_isfinite (robust_focus)
          || robust_focus > focus_interpolation_max + tolerance)
        {
          pause_stdout (progress);
          fprintf (stderr,
                   "Focus analysis failed: coarse defocus %.5f mm is outside "
                   "the useful %.5f mm range (screen-frequency MTF %.1f%%)\n",
                   robust_focus, focus_interpolation_max,
                   focus_mtf_threshold * 100);
          resume_stdout (progress);
          return false;
        }
      if (verbose)
        {
          pause_stdout (progress);
          printf ("Dense focus interpolation: %.6f cycles/pixel, "
                  "0...%.5f mm at %.1f%% MTF, %i quadratic nodes\n",
                  focus_screen_frequency, focus_interpolation_max,
                  focus_mtf_threshold * 100, focus_interpolation_nodes);
          resume_stdout (progress);
        }
    }

  if (verbose)
    {
      pause_stdout (progress);
      printf ("Analyzing %ix%i areas each subsampled %ix%i (overall %i "
              "solutions to be computed)\n",
              xsteps, ysteps, xsubsteps, ysubsteps,
              xsteps * ysteps * xsubsteps * ysubsteps);
      resume_stdout (progress);
    }

  const int sample_width = xsteps * xsubsteps;
  const int sample_height = ysteps * ysubsteps;
  size_t mainpass_size;
  if (!valid_grid_size_p (sample_width, sample_height, &mainpass_size))
    return false;
  mainpass.assign (mainpass_size, finetune_result ());
  if (progress)
    progress->set_task ("analyzing samples", (int)mainpass_size);
  return true;
}

/* Evaluate dense-grid sample X,Y.  */
bool
analyze_scanner_blur_worker::analyze_blur (int x, int y,
                                           rgbdata *displacements)
{
  const int width = xsteps * xsubsteps;
  const int height = ysteps * ysubsteps;
  if (x < 0 || x >= width || y < 0 || y >= height)
    return false;
  if (progress && progress->cancel_requested ())
    return false;

  finetune_parameters fparam;
  fparam.flags = flags;
  if (screen_with_varying_strips_p (param.type))
    {
      if (reoptimize_strip_widths)
        fparam.flags |= finetune_strips;
      /* STEP2 either installed robust prepass widths or retained the caller's
         explicit widths.  Preserve them in the dense pass, including as the
         starting point when widths are reoptimized.  */
      if (explicit_strip_widths_p (rparam))
        fparam.flags |= finetune_use_strip_widths;
    }
  /* STEP2 stores the robust prepass blur in RPARAM.  Preserve it as the dense
     pass starting point just as the MTF path starts from the updated scanner
     model.  */
  if (mode == scanner_blur_correction_parameters::blur_radius)
    fparam.flags |= finetune_use_screen_blur;
  fparam.multitile = 1;
  fparam.collect_profile = report_profile;
  if (interpolate_focus)
    {
      fparam.interpolate_scanner_mtf_defocus = true;
      fparam.scanner_mtf_defocus_interpolation_max
          = focus_interpolation_max;
      fparam.scanner_mtf_defocus_interpolation_nodes
          = focus_interpolation_nodes;
    }
  finetune_result &res = mainpass_result (x, y);
  res = finetune (
      rparam, param, scan,
      { { (coord_t)(x + 0.5) * scan.width / width,
          (coord_t)(y + 0.5) * scan.height / height } },
      NULL, fparam, progress);
  bool identifiable = identifiable_result_p (res, min_contrast);
  coord_t correction = -1;
  if (identifiable)
    {
      correction = get_correction (mode, res);
      identifiable = valid_correction_p (correction);
    }
  if (identifiable && displacements)
    {
      *displacements = { (luminosity_t)correction,
                         (luminosity_t)correction,
                         (luminosity_t)correction };
    }
  if (progress)
    progress->inc_progress ();
  return identifiable;
}

finetune_profile
analyze_scanner_blur_worker::get_profile () const
{
  finetune_profile ret;
  for (const finetune_result &res : prepass)
    ret += res.profile;
  for (const finetune_result &res : mainpass)
    ret += res.profile;
  return ret;
}

void
analyze_scanner_blur_worker::print_profile () const
{
  const finetune_profile p = get_profile ();
  const fit_diagnostics prepass_diagnostics
      = collect_fit_diagnostics (prepass, min_contrast);
  const fit_diagnostics mainpass_diagnostics
      = collect_fit_diagnostics (mainpass, min_contrast);
  uint64_t successful = 0;
  for (const finetune_result &res : prepass)
    successful += res.success;
  for (const finetune_result &res : mainpass)
    successful += res.success;
  const uint64_t fits = prepass.size () + mainpass.size ();
  const uint64_t focus_lookups
      = p.focus_screen_cache_hits + p.focus_screen_cache_misses;
  const double focus_hit_rate
      = focus_lookups
            ? 100.0 * p.focus_screen_cache_hits / focus_lookups
            : 0.0;
  const uint64_t local_focus_lookups
      = p.focus_screen_local_node_hits + p.focus_screen_local_node_misses;
  const double local_focus_hit_rate
      = local_focus_lookups
            ? 100.0 * p.focus_screen_local_node_hits / local_focus_lookups
            : 0.0;
  const uint64_t source_lookups
      = p.focus_source_cache_hits + p.focus_source_cache_misses;
  const double source_hit_rate
      = source_lookups
            ? 100.0 * p.focus_source_cache_hits / source_lookups
            : 0.0;
  const auto milliseconds = [] (uint64_t nanoseconds) {
    return nanoseconds / 1000000.0;
  };

  pause_stdout (progress);
  printf ("Finetune profile: %llu/%llu fits solver-successful; %llu simplex "
          "runs, "
          "%llu iterations, %llu evaluations; %llu objective calls\n",
          (unsigned long long)successful, (unsigned long long)fits,
          (unsigned long long)p.simplex_runs,
          (unsigned long long)p.simplex_iterations,
          (unsigned long long)p.simplex_evaluations,
          (unsigned long long)p.objective_evaluations);
  printf ("  adaptive minimum fitted contrast: %.9g%%\n",
          (double)(min_contrast * 100));
  if (!prepass.empty ())
    print_fit_diagnostics ("prepass", prepass_diagnostics);
  if (!mainpass.empty ())
    print_fit_diagnostics ("dense pass", mainpass_diagnostics);
  if (reduction_profile.cells)
    {
      const double mean_accepted
          = (double)reduction_profile.accepted_samples
            / reduction_profile.cells;
      const double mean_total
          = (double)reduction_profile.total_samples / reduction_profile.cells;
      const double accepted_fraction
          = reduction_profile.total_samples
                ? (double)reduction_profile.accepted_samples * 100
                      / reduction_profile.total_samples
                : 0;
      printf ("  correction cells: %zu; accepted samples %d...%d "
              "(mean %.2f of %.2f, %.1f%%); robust %s spread "
              "%.9g...%.9g (mean %.9g); accepted fitted contrast "
              "%.9g%%...%.9g%% (mean %.9g%%)\n",
              reduction_profile.cells, reduction_profile.accepted_min,
              reduction_profile.accepted_max, mean_accepted, mean_total,
              accepted_fraction,
              scanner_blur_correction_parameters::pretty_correction_names
                  [(int)mode],
              (double)reduction_profile.spread_min,
              (double)reduction_profile.spread_max,
              (double)(reduction_profile.spread_sum
                       / reduction_profile.cells),
              (double)(reduction_profile.contrast_min * 100),
              (double)(reduction_profile.contrast_max * 100),
              (double)(reduction_profile.contrast_sum * 100
                       / reduction_profile.accepted_samples));
    }
  printf ("  screen states: %llu init calls, %llu local reuses; "
          "focus cache %llu hits, %llu misses (%.1f%% hit); fixed cache "
          "%llu hits, %llu misses\n",
          (unsigned long long)p.screen_init_calls,
          (unsigned long long)p.screen_state_reuses,
          (unsigned long long)p.focus_screen_cache_hits,
          (unsigned long long)p.focus_screen_cache_misses, focus_hit_rate,
          (unsigned long long)p.fixed_screen_cache_hits,
          (unsigned long long)p.fixed_screen_cache_misses);
  printf ("  focus source spectra: %llu hits, %llu misses (%.1f%% hit)\n",
          (unsigned long long)p.focus_source_cache_hits,
          (unsigned long long)p.focus_source_cache_misses, source_hit_rate);
  printf ("  solver-local focus nodes: %llu hits, %llu misses (%.1f%% hit)\n",
          (unsigned long long)p.focus_screen_local_node_hits,
          (unsigned long long)p.focus_screen_local_node_misses,
          local_focus_hit_rate);
  printf ("  focus approximation: %llu interpolations, %llu exact node "
          "uses, %llu exact final builds\n",
          (unsigned long long)p.focus_screen_interpolations,
          (unsigned long long)p.focus_screen_exact_node_uses,
          (unsigned long long)p.focus_screen_final_exact_builds);
  printf ("  exact screen builds: %llu; general MTF precomputes %llu, PSF "
          "precomputes %llu; physical focus state %llu hits, %llu misses, "
          "%llu transfer tables; empirical fallback %llu transfer tables\n",
          (unsigned long long)p.exact_screen_builds,
          (unsigned long long)p.mtf_precompute_calls,
          (unsigned long long)p.mtf_psf_precompute_calls,
          (unsigned long long)p.physical_focus_cache_hits,
          (unsigned long long)p.physical_focus_cache_misses,
          (unsigned long long)p.physical_focus_transfer_builds,
          (unsigned long long)p.empirical_focus_transfer_builds);
  printf ("  periodic filters: %llu direct transfers, %llu wrapped PSFs\n",
          (unsigned long long)p.direct_transfer_builds,
          (unsigned long long)p.wrapped_psf_builds);
  printf ("  FFTs: %llu kernel forward, %llu screen forward, %llu screen "
          "inverse\n",
          (unsigned long long)p.kernel_forward_ffts,
          (unsigned long long)p.screen_forward_ffts,
          (unsigned long long)p.screen_inverse_ffts);
  printf ("  accumulated time: objective %.1f ms; filtering %.1f ms; "
          "cache lookup/wait %.1f ms; interpolation %.1f ms; simulation "
          "%.1f ms; colors %.1f ms; residual %.1f ms\n",
          milliseconds (p.objective_nanoseconds),
          milliseconds (p.screen_filter_nanoseconds),
          milliseconds (p.screen_cache_nanoseconds),
          milliseconds (p.screen_interpolation_nanoseconds),
          milliseconds (p.screen_simulation_nanoseconds),
          milliseconds (p.color_estimation_nanoseconds),
          milliseconds (p.residual_nanoseconds));
  resume_stdout (progress);
}

/* Reduce dense fits into the final adaptive correction table.  */
std::unique_ptr<scanner_blur_correction_parameters>
analyze_scanner_blur_worker::step3 ()
{
  last_error.clear ();
  reduction_profile = {};
  if (progress && progress->cancel_requested ())
    {
      last_error = "Analysis cancelled.";
      return NULL;
    }
  if (mainpass.empty ())
    return NULL;

  std::unique_ptr<scanner_blur_correction_parameters> scanner_blur_correction
      = std::make_unique<scanner_blur_correction_parameters> ();
  if (!scanner_blur_correction->alloc (xsteps, ysteps, mode)
      || !scanner_blur_correction->alloc_diagnostics ())
    return NULL;
  scr_to_img map;
  if (!map.set_parameters (param, scan))
    return NULL;
  /* The correction table currently uses one representative pixel scale for
     the whole scan.  Spatially varying scale is tracked separately.  */
  coord_t pixel_size = map.pixel_size ({0, 0, scan.width, scan.height});
  if (!my_isfinite (pixel_size) || pixel_size <= 0)
    return NULL;
  if (progress)
    progress->set_task ("summarizing results", 1);

  bool fail = false;
  std::vector<finetune_result *> samples;
  samples.reserve ((size_t)xsubsteps * ysubsteps);
  for (int y = 0; y < ysteps; y++)
    for (int x = 0; x < xsteps; x++)
      {
        samples.clear ();
        for (int yy = 0; yy < ysubsteps; yy++)
          for (int xx = 0; xx < xsubsteps; xx++)
            samples.push_back (&mainpass_result (x * xsubsteps + xx,
                                                 y * ysubsteps + yy));

        coord_t fit_score_threshold;
        if (!find_fit_score_threshold (samples, skipmax, min_contrast,
                                       &fit_score_threshold))
          {
            char phase[64];
            snprintf (phase, sizeof (phase), "sample %i,%i", x, y);
            set_error (no_identifiable_fits_message (
                phase, collect_fit_diagnostics (samples, min_contrast),
                min_contrast));
            return NULL;
          }

        int nok = 0;
        long double accepted_contrast_sum = 0;
        histogram hist;
        for (finetune_result *res : samples)
          {
            if (!accepted_result_p (*res, fit_score_threshold, min_contrast))
              continue;
            const coord_t correction = get_correction (mode, *res);
            if (!valid_correction_p (correction))
              continue;
            hist.pre_account (correction);
            accepted_contrast_sum += res->contrast;
            nok++;
          }
        if (!nok)
          {
            char message[160];
            snprintf (message, sizeof (message),
                      "Analysis failed for sample %i,%i: all identifiable "
                      "fits had invalid correction values",
                      x, y);
            set_error (message);
            return NULL;
          }

        hist.finalize_range (65536);
        for (finetune_result *res : samples)
          if (accepted_result_p (*res, fit_score_threshold, min_contrast))
            {
              const coord_t correction = get_correction (mode, *res);
              if (valid_correction_p (correction))
                hist.account (correction);
            }
        hist.finalize ();

        const coord_t low = hist.find_min (skipmin / 100);
        const coord_t high = hist.find_max (skipmax / 100.0);
        if (tolerance >= 0 && high - low > tolerance)
          {
            pause_stdout (progress);
            printf ("Tolerance threshold %f exceeded for entry %i,%i: %s "
                    "range is %f...%f (diff %f)\n",
                    tolerance, x, y,
                    scanner_blur_correction_parameters::pretty_correction_names
                        [(int)mode],
                    low, high, high - low);
            fail = true;
            resume_stdout (progress);
          }
        luminosity_t correction
            = hist.find_avg (skipmin / 100, skipmax / 100);
        if (!valid_correction_p (correction))
          {
            pause_stdout (progress);
            fprintf (stderr,
                     "Analysis failed for sample %i,%i: invalid correction "
                     "%f\n",
                     x, y, correction);
            resume_stdout (progress);
            return NULL;
          }
        luminosity_t spread = high - low;
        if (mode == scanner_blur_correction_parameters::blur_radius)
          {
            correction *= pixel_size;
            spread *= pixel_size;
          }
        if (!my_isfinite (spread) || spread < 0)
          {
            set_error ("Analysis produced an invalid robust correction "
                       "spread");
            return NULL;
          }
        scanner_blur_correction->set_correction (x, y, correction);
        scanner_blur_correction_parameters::cell_diagnostics diagnostics = {
          spread,
          (luminosity_t)(accepted_contrast_sum / nok),
          nok,
          (int)samples.size ()
        };
        scanner_blur_correction->set_diagnostics (x, y, diagnostics);

        if (!reduction_profile.cells)
          {
            reduction_profile.accepted_min
                = reduction_profile.accepted_max = nok;
            reduction_profile.spread_min
                = reduction_profile.spread_max = spread;
            reduction_profile.contrast_min
                = reduction_profile.contrast_max = diagnostics.mean_contrast;
          }
        else
          {
            reduction_profile.accepted_min
                = std::min (reduction_profile.accepted_min, nok);
            reduction_profile.accepted_max
                = std::max (reduction_profile.accepted_max, nok);
            reduction_profile.spread_min
                = std::min (reduction_profile.spread_min, (coord_t)spread);
            reduction_profile.spread_max
                = std::max (reduction_profile.spread_max, (coord_t)spread);
            reduction_profile.contrast_min
                = std::min (reduction_profile.contrast_min,
                            diagnostics.mean_contrast);
            reduction_profile.contrast_max
                = std::max (reduction_profile.contrast_max,
                            diagnostics.mean_contrast);
          }
        reduction_profile.cells++;
        reduction_profile.accepted_samples += nok;
        reduction_profile.total_samples += samples.size ();
        reduction_profile.spread_sum += spread;
        reduction_profile.contrast_sum += accepted_contrast_sum;
      }
  if (progress)
    progress->inc_progress ();
  if (fail)
    return NULL;
  return scanner_blur_correction;
}

} // namespace colorscreen
