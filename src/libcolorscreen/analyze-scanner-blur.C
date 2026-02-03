#ifdef _OPENMP
#include <omp.h>
#endif
#include "include/analyze-scanner-blur.h"
#include "include/scr-to-img.h"
namespace colorscreen
{
namespace {
coord_t
get_correction (scanner_blur_correction_parameters::correction_mode mode, finetune_result &res)
{
  switch (mode)
    {
    case scanner_blur_correction_parameters::blur_radius:
      return res.screen_blur_radius;
      break;
    case scanner_blur_correction_parameters::mtf_defocus:
      return res.scanner_mtf_defocus;
      break;
    case scanner_blur_correction_parameters::mtf_blur_diameter:
      return res.scanner_mtf_blur_diameter;
      break;
    case scanner_blur_correction_parameters::max_correction:
      abort ();
    }
  abort ();
}
}

bool
analyze_scanner_blur_worker::step1()
{
  if (!xsteps && !ysteps)
    xsteps = 10;
  if (!xsteps)
    xsteps = (ysteps * scan.width + scan.height / 2) / scan.height;
  if (!ysteps)
    ysteps = (xsteps * scan.height + scan.width / 2) / scan.width;
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
  if (!strip_xsteps && !strip_ysteps)
    strip_xsteps = 10;
  if (!strip_xsteps)
    strip_xsteps = (strip_ysteps * scan.width + scan.height / 2) / scan.height;
  if (!strip_ysteps)
    strip_ysteps = (strip_xsteps * scan.height + scan.width / 2) / scan.width;
  if (!strip_xsteps)
    strip_xsteps = 1;
  if (!strip_ysteps)
    strip_ysteps = 1;
  if (rparam.scanner_blur_correction)
    rparam.scanner_blur_correction = NULL;
#ifdef _OPENMP
  omp_set_nested (1);
#endif
  mode = scanner_blur_correction_parameters::blur_radius;
  if (flags & (finetune_scanner_mtf_defocus | finetune_scanner_mtf_channel_defocus))
    mode = rparam.sharpen.scanner_mtf.simulate_difraction_p ()
	   ? scanner_blur_correction_parameters::mtf_defocus : scanner_blur_correction_parameters::mtf_blur_diameter;
  {
    prepass.resize (strip_xsteps * strip_ysteps);
    if (verbose)
      {
	progress->pause_stdout ();
	if (screen_with_varying_strips_p (param.type))
	  printf ("Analyzing %ix%i areas to determine strip widths and "
		  "blur (overall %i solutions to be computed)\n",
		  strip_xsteps, strip_ysteps, strip_xsteps * strip_ysteps);
	else
	  printf ("Analyzing %ix%i areas to determine blur (overall %i "
		  "solutions to be computed)\n",
		  strip_xsteps, strip_ysteps, strip_xsteps * strip_ysteps);
	progress->resume_stdout ();
      }
    progress->set_task (screen_with_varying_strips_p (param.type) ? "analyzing screen strip sizes and blur"
			      : "analyzing screen blur",
		       strip_xsteps * strip_ysteps);
    return true;
#if 0
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
      shared(strip_xsteps, strip_ysteps, rparam, scan, progress, param, prepass, \
		 flags)
    for (int y = 0; y < strip_ysteps; y++)
      for (int x = 0; x < strip_xsteps; x++)
	{
	}
#endif
  }
}

bool
analyze_scanner_blur_worker::analyze_strips (int x, int y, coord_t *red_strip_width, coord_t *green_strip_width)
{
  finetune_parameters fparam;
  fparam.flags = flags | (screen_with_varying_strips_p (param.type) ? finetune_strips : 0);
  fparam.multitile = 1;
  prepass [y * strip_xsteps + x] = finetune (
      rparam, param, scan,
      { { (coord_t)(x + 0.5) * scan.width / strip_xsteps,
	  (coord_t)(y + 0.5) * scan.height / strip_ysteps } },
      NULL, fparam, progress);
  progress->inc_progress ();
  if (prepass [y * strip_xsteps + x].success && red_strip_width)
    {
      *red_strip_width = prepass [y * strip_xsteps + x].red_strip_width;
      *green_strip_width = prepass [y * strip_xsteps + x].green_strip_width;
    }
  return prepass [y * strip_xsteps + x].success;
}
bool
analyze_scanner_blur_worker::step2()
{
  if (prepass.size ())
  {
    int nok = 0;
    for (int y = 0; y < strip_ysteps; y++)
      for (int x = 0; x < strip_xsteps; x++)
	{
	  finetune_result &res = prepass[y * strip_xsteps + x];
	  if (res.success)
	    uncertainity_hist.pre_account (res.uncertainity);
	}
    uncertainity_hist.finalize_range (65536);
    for (int y = 0; y < strip_ysteps; y++)
      for (int x = 0; x < strip_xsteps; x++)
	{
	  finetune_result &res = prepass[y * strip_xsteps + x];
	  if (res.success)
	    uncertainity_hist.account (res.uncertainity);
	}
    uncertainity_hist.finalize ();
    coord_t uncertainity_threshold = uncertainity_hist.find_max (skipmax / 100.0);
    for (int y = 0; y < strip_ysteps; y++)
      for (int x = 0; x < strip_xsteps; x++)
	{
	  finetune_result &res = prepass[y * strip_xsteps + x];
	  if (!res.success || res.uncertainity > uncertainity_threshold)
	    continue;
	  if (screen_with_varying_strips_p (param.type))
	    {
	      red_hist.pre_account (res.red_strip_width);
	      green_hist.pre_account (res.green_strip_width);
	    }
	  blur_hist.pre_account (get_correction (mode, res));
	  nok++;
	}
    if (!nok)
      {
	progress->pause_stdout ();
	fprintf (stderr, "Analysis failed\n");
	return false;
      }
    if (screen_with_varying_strips_p (param.type))
      {
	red_hist.finalize_range (65536);
	green_hist.finalize_range (65536);
      }
    blur_hist.finalize_range (65536);
    for (int y = 0; y < strip_ysteps; y++)
      for (int x = 0; x < strip_xsteps; x++)
	{
	  finetune_result &res = prepass[y * strip_xsteps + x];
	  if (!res.success || res.uncertainity > uncertainity_threshold)
	    continue;
	  if (screen_with_varying_strips_p (param.type))
	    {
	      red_hist.account (res.red_strip_width);
	      green_hist.account (res.green_strip_width);
	    }
	  blur_hist.account (get_correction (mode, res));
	  nok++;
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
      }
    switch (mode)
      {
      case scanner_blur_correction_parameters::blur_radius:
	rparam.screen_blur_radius
	    = blur_hist.find_avg (skipmin / 100, skipmax / 100);
	break;
      case scanner_blur_correction_parameters::mtf_defocus:
	rparam.sharpen.scanner_mtf.defocus
	    = blur_hist.find_avg (skipmin / 100, skipmax / 100);
	break;
      case scanner_blur_correction_parameters::mtf_blur_diameter:
	rparam.sharpen.scanner_mtf.blur_diameter
	    = blur_hist.find_avg (skipmin / 100, skipmax / 100);
	break;
      case scanner_blur_correction_parameters::max_correction:
	abort ();
      }
    if (verbose)
      {
	progress->pause_stdout ();
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
	    printf ("Average screen blur %.2f pixels\n", rparam.screen_blur_radius);
	    break;
	  case scanner_blur_correction_parameters::mtf_defocus:
	    printf ("Average mtf defocus %.5f mm\n", rparam.sharpen.scanner_mtf.defocus);
	    break;
	  case scanner_blur_correction_parameters::mtf_blur_diameter:
	    printf ("Average mtf blur diameter %.2f pixels\n", rparam.sharpen.scanner_mtf.blur_diameter);
	    break;
	  case scanner_blur_correction_parameters::max_correction:
	    abort ();
	  }
	progress->resume_stdout ();
      }
  }
  if (verbose)
    {
      progress->pause_stdout ();
      printf ("Analyzing %ix%i areas each subsampled %ix%i (overall %i "
              "solutions to be computed)\n",
              xsteps, ysteps, xsubsteps, ysubsteps,
              xsteps * ysteps * xsubsteps * ysubsteps);
      progress->resume_stdout ();
    }
  mainpass.resize (xsteps * xsubsteps * ysteps * ysubsteps);
  progress->set_task ("analyzing samples",
                      ysteps * xsteps * xsubsteps * ysubsteps);
  return true;
#if 0
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(xsteps, ysteps, xsubsteps, ysubsteps, rparam, scan, progress,      \
               param, mainpass, reoptimize_strip_widths, flags)
  for (int y = 0; y < ysteps * ysubsteps; y++)
    for (int x = 0; x < xsteps * xsubsteps; x++)
      {
        finetune_parameters fparam;
        fparam.flags = flags | (reoptimize_strip_widths ? finetune_strips : 0);
        fparam.multitile = 1;
        mainpass[y * xsteps * xsubsteps + x] = finetune (
            rparam, param, scan,
            { { (coord_t)(x + 0.5) * scan.width / (xsteps * xsubsteps),
                (coord_t)(y + 0.5) * scan.height / (ysteps * ysubsteps) } },
            NULL, fparam, progress);
        progress->inc_progress ();
      }
#endif
}
bool
analyze_scanner_blur_worker::analyze_blur (int x, int y, rgbdata *displacements)
{
  finetune_parameters fparam;
  fparam.flags = flags | (reoptimize_strip_widths ? finetune_strips : 0);
  fparam.multitile = 1;
  mainpass[y * xsteps * xsubsteps + x] = finetune (
      rparam, param, scan,
      { { (coord_t)(x + 0.5) * scan.width / (xsteps * xsubsteps),
	  (coord_t)(y + 0.5) * scan.height / (ysteps * ysubsteps) } },
      NULL, fparam, progress);
  if (mainpass [y * strip_xsteps + x].success && displacements)
    {
      coord_t cor = get_correction (mode, mainpass[y * xsteps * xsubsteps + x]);
      *displacements = {cor, cor, cor};
    }
  progress->inc_progress ();
  return mainpass [y * strip_xsteps + x].success;
} 
std::unique_ptr <scanner_blur_correction_parameters>
analyze_scanner_blur_worker::step3()
{
  std::unique_ptr <scanner_blur_correction_parameters> scanner_blur_correction = std::make_unique<scanner_blur_correction_parameters> ();
  scanner_blur_correction->alloc (xsteps, ysteps, mode);
  coord_t pixel_size;
  scr_to_img map;
  map.set_parameters (param, scan);
  pixel_size = map.pixel_size (scan.width, scan.height);
  progress->set_task ("summarizing results", 1);
  bool fail = false;
  for (int y = 0; y < ysteps; y++)
    for (int x = 0; x < xsteps; x++)
      {
        int nok = 0;
        histogram uncertainity_hist;
        for (int yy = 0; yy < ysubsteps; yy++)
          for (int xx = 0; xx < xsubsteps; xx++)
	    {
	      finetune_result &res = mainpass[(y * ysubsteps + yy) * xsteps * xsubsteps + x * xsubsteps + xx];
	      if (res.success)
		uncertainity_hist.pre_account (res.uncertainity);
	    }
	uncertainity_hist.finalize_range (65536);
        for (int yy = 0; yy < ysubsteps; yy++)
          for (int xx = 0; xx < xsubsteps; xx++)
	    {
	      finetune_result &res = mainpass[(y * ysubsteps + yy) * xsteps * xsubsteps + x * xsubsteps + xx];
	      if (res.success)
		uncertainity_hist.account (res.uncertainity);
	    }
        uncertainity_hist.finalize ();
        coord_t uncertainity_threshold = uncertainity_hist.find_max (skipmax / 100.0);
        histogram hist;
        for (int yy = 0; yy < ysubsteps; yy++)
          for (int xx = 0; xx < xsubsteps; xx++)
            {
	      finetune_result &res = mainpass[(y * ysubsteps + yy) * xsteps * xsubsteps + x * xsubsteps + xx];
	      if (!res.success || res.uncertainity > uncertainity_threshold)
		continue;
	      nok++;
	      hist.pre_account (get_correction (mode, res));
            }
        if (!nok)
          {
            progress->pause_stdout ();
            fprintf (stderr, "Analysis failed for sample %i,%i\n", x, y);
            return NULL;
          }
        hist.finalize_range (65536);
        for (int yy = 0; yy < ysubsteps; yy++)
          for (int xx = 0; xx < xsubsteps; xx++)
            {
	      finetune_result &res = mainpass[(y * ysubsteps + yy) * xsteps * xsubsteps + x * xsubsteps + xx];
              hist.account (get_correction (mode, res));
            }
        hist.finalize ();
        if (tolerance >= 0
            && hist.find_max (skipmax / 100.0) - hist.find_min (skipmin / 100)
                   > tolerance)
          {
	    progress->pause_stdout ();
            printf ("Tolerance threshold %f exceeded for entry %i,%i: %s "
                    " range is %f...%f (diff %f)\n",
                    tolerance, x, y, 
		    scanner_blur_correction_parameters::pretty_correction_names[(int)mode],
		    hist.find_min (skipmin / 100),
                    hist.find_max (skipmax / 100.0),
                    hist.find_max (skipmax / 100.0)
                        - hist.find_min (skipmin / 100));
            fail = true;
	    progress->resume_stdout ();
          }
        luminosity_t b = hist.find_avg (skipmin / 100, skipmax / 100);
        assert (b >= 0 && b <= 1024);
	if (mode == scanner_blur_correction_parameters::blur_radius)
	  b *= pixel_size;
        scanner_blur_correction->set_correction (x, y, b);
      }
  if (fail)
    return NULL;
  return scanner_blur_correction;
}

}
