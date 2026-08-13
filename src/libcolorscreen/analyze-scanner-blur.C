#ifdef _OPENMP
#include <omp.h>
#endif
#include <climits>
#include <limits>
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

/* Return true when RES has a usable historical fit-quality score.  The public
   field is named "uncertainty" for compatibility, but stores objective divided
   by registration contrast rather than a statistical error estimate.  */
bool
valid_fit_score_p (const finetune_result &res)
{
  return res.success && my_isfinite (res.uncertainty)
         && res.uncertainty >= 0
         && res.uncertainty < std::numeric_limits<coord_t>::max ();
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
                            coord_t skipmax, coord_t *threshold)
{
  histogram hist;
  int nvalid = 0;
  for (finetune_result *res : results)
    if (valid_fit_score_p (*res))
      {
        hist.pre_account (res->uncertainty);
        nvalid++;
      }
  if (!nvalid)
    return false;
  hist.finalize_range (65536);
  for (finetune_result *res : results)
    if (valid_fit_score_p (*res))
      hist.account (res->uncertainty);
  hist.finalize ();
  *threshold = hist.find_max (skipmax / (coord_t)100);
  return my_isfinite (*threshold);
}

/* Return true when RES passes the fit-score filter THRESHOLD.  */
bool
accepted_result_p (const finetune_result &res, coord_t threshold)
{
  return valid_fit_score_p (res) && res.uncertainty <= threshold;
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
} // namespace

/* Prepare dimensions and storage for the scanner-blur analysis.  */
bool
analyze_scanner_blur_worker::step1 ()
{
  if (finetune_flag_error (flags))
    return false;
  if (!my_isfinite (skipmin) || !my_isfinite (skipmax) || skipmin < 0
      || skipmax < 0 || skipmin > 50 || skipmax > 50
      || skipmin + skipmax >= 100 || !my_isfinite (tolerance)
      || scan.width <= 0 || scan.height <= 0)
    return false;

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
    return false;

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
    return false;

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
    return false;
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
      if (screen_with_varying_strips_p (param.type))
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
  fparam.flags
      = flags
        | (screen_with_varying_strips_p (param.type)
               ? (uint64_t)finetune_strips
               : (uint64_t)0);
  fparam.multitile = 1;
  finetune_result &res = prepass_result (x, y);
  res = finetune (rparam, param, scan,
                  { { (coord_t)(x + 0.5) * scan.width / strip_xsteps,
                      (coord_t)(y + 0.5) * scan.height / strip_ysteps } },
                  NULL, fparam, progress);
  if (progress)
    progress->inc_progress ();
  if (res.success)
    {
      if (red_strip_width)
        *red_strip_width = res.red_strip_width;
      if (green_strip_width)
        *green_strip_width = res.green_strip_width;
    }
  return res.success;
}

/* Reduce the prepass and prepare the dense focus-analysis pass.  */
bool
analyze_scanner_blur_worker::step2 ()
{
  if (progress && progress->cancel_requested ())
    return false;

  if (!prepass.empty ())
    {
      std::vector<finetune_result *> results;
      results.reserve (prepass.size ());
      for (finetune_result &res : prepass)
        results.push_back (&res);

      coord_t fit_score_threshold;
      if (!find_fit_score_threshold (results, skipmax,
                                       &fit_score_threshold))
        {
          pause_stdout (progress);
          fprintf (stderr, "Analysis failed: no successful prepass fits\n");
          resume_stdout (progress);
          return false;
        }

      int nok = 0;
      for (finetune_result *res : results)
        {
          if (!accepted_result_p (*res, fit_score_threshold))
            continue;
          const coord_t correction = get_correction (mode, *res);
          if (!valid_correction_p (correction))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && (!my_isfinite (res->red_strip_width)
                  || !my_isfinite (res->green_strip_width)))
            continue;
          if (screen_with_varying_strips_p (param.type))
            {
              red_hist.pre_account (res->red_strip_width);
              green_hist.pre_account (res->green_strip_width);
            }
          blur_hist.pre_account (correction);
          nok++;
        }
      if (!nok)
        {
          pause_stdout (progress);
          fprintf (stderr,
                   "Analysis failed: all successful prepass fits were "
                   "rejected\n");
          resume_stdout (progress);
          return false;
        }

      if (screen_with_varying_strips_p (param.type))
        {
          red_hist.finalize_range (65536);
          green_hist.finalize_range (65536);
        }
      blur_hist.finalize_range (65536);
      for (finetune_result *res : results)
        {
          if (!accepted_result_p (*res, fit_score_threshold))
            continue;
          const coord_t correction = get_correction (mode, *res);
          if (!valid_correction_p (correction))
            continue;
          if (screen_with_varying_strips_p (param.type)
              && (!my_isfinite (res->red_strip_width)
                  || !my_isfinite (res->green_strip_width)))
            continue;
          if (screen_with_varying_strips_p (param.type))
            {
              red_hist.account (res->red_strip_width);
              green_hist.account (res->green_strip_width);
            }
          blur_hist.account (correction);
        }

      if (screen_with_varying_strips_p (param.type))
        {
          red_hist.finalize ();
          green_hist.finalize ();
        }
      blur_hist.finalize ();
      if (screen_with_varying_strips_p (param.type))
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
          if (screen_with_varying_strips_p (param.type))
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
  fparam.flags
      = flags
        | (reoptimize_strip_widths ? (uint64_t)finetune_strips
                                   : (uint64_t)0);
  /* STEP2 stores the robust prepass blur in RPARAM.  Preserve it as the dense
     pass starting point just as the MTF path starts from the updated scanner
     model.  */
  if (mode == scanner_blur_correction_parameters::blur_radius)
    fparam.flags |= finetune_use_screen_blur;
  fparam.multitile = 1;
  finetune_result &res = mainpass_result (x, y);
  res = finetune (
      rparam, param, scan,
      { { (coord_t)(x + 0.5) * scan.width / width,
          (coord_t)(y + 0.5) * scan.height / height } },
      NULL, fparam, progress);
  if (res.success && displacements)
    {
      coord_t correction = get_correction (mode, res);
      *displacements = { (luminosity_t)correction,
                         (luminosity_t)correction,
                         (luminosity_t)correction };
    }
  if (progress)
    progress->inc_progress ();
  return res.success;
}

/* Reduce dense fits into the final adaptive correction table.  */
std::unique_ptr<scanner_blur_correction_parameters>
analyze_scanner_blur_worker::step3 ()
{
  if (progress && progress->cancel_requested ())
    return NULL;
  if (mainpass.empty ())
    return NULL;

  std::unique_ptr<scanner_blur_correction_parameters> scanner_blur_correction
      = std::make_unique<scanner_blur_correction_parameters> ();
  if (!scanner_blur_correction->alloc (xsteps, ysteps, mode))
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
        if (!find_fit_score_threshold (samples, skipmax,
                                         &fit_score_threshold))
          {
            pause_stdout (progress);
            fprintf (stderr,
                     "Analysis failed for sample %i,%i: no successful fits\n",
                     x, y);
            resume_stdout (progress);
            return NULL;
          }

        int nok = 0;
        histogram hist;
        for (finetune_result *res : samples)
          {
            if (!accepted_result_p (*res, fit_score_threshold))
              continue;
            const coord_t correction = get_correction (mode, *res);
            if (!valid_correction_p (correction))
              continue;
            hist.pre_account (correction);
            nok++;
          }
        if (!nok)
          {
            pause_stdout (progress);
            fprintf (stderr,
                     "Analysis failed for sample %i,%i: all fits were "
                     "rejected\n",
                     x, y);
            resume_stdout (progress);
            return NULL;
          }

        hist.finalize_range (65536);
        for (finetune_result *res : samples)
          if (accepted_result_p (*res, fit_score_threshold))
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
        if (mode == scanner_blur_correction_parameters::blur_radius)
          correction *= pixel_size;
        scanner_blur_correction->set_correction (x, y, correction);
      }
  if (progress)
    progress->inc_progress ();
  if (fail)
    return NULL;
  return scanner_blur_correction;
}

} // namespace colorscreen
