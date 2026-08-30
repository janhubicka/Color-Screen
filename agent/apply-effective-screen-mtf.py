#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one match, found {n}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/libcolorscreen/focus-analysis.C",
    '#include "render-interpolate.h"\n#include <algorithm>\n#include <cmath>\n',
    '#include "render-interpolate.h"\n#include "screen.h"\n#include <algorithm>\n#include <cmath>\n#include <complex>\n',
)

old = r'''/* Evaluate the scanner system MTF represented by FIT at FREQUENCY.  The RGB
   periodic-screen finetuner uses the same achromatic 550 nm convention.  */
static coord_t
fit_system_mtf (const render_parameters &rparam, const image_data &img,
                uint64_t flags, const finetune_result &fit,
                coord_t frequency)
{
  if (!fit.success || !(frequency > 0))
    return -1;
  mtf_parameters mtf
      = (flags & finetune_bw)
            ? rparam.get_image_layer_sharpen_parameters (&img).scanner_mtf
            : rparam.sharpen.scanner_mtf;
  if ((flags & finetune_scanner_mtf_sigma)
      && my_isfinite (fit.scanner_mtf_sigma))
    mtf.sigma = fit.scanner_mtf_sigma;
  if (flags & finetune_scanner_mtf_defocus)
    {
      if (mtf.simulate_diffraction_p ())
        {
          if (!my_isfinite (fit.scanner_mtf_defocus))
            return -1;
          mtf.defocus = fit.scanner_mtf_defocus;
        }
      else
        {
          if (!my_isfinite (fit.scanner_mtf_blur_diameter))
            return -1;
          mtf.blur_diameter = fit.scanner_mtf_blur_diameter;
        }
    }
  if (!(flags & finetune_bw) && img.has_rgb ())
    mtf.wavelength = 550;
  const coord_t value = mtf.system_mtf (frequency);
  return my_isfinite (value) ? value : (coord_t)-1;
}
'''
new = r'''struct process_screen_harmonic
{
  int x;
  int y;
};

/* Return ideal RGB Fourier power in one screen-space harmonic.  This is used
   only for a handful of low-order carrier harmonics, so a direct DFT is both
   simpler and cheaper than constructing an FFT plan for every focus report.  */
static double
ideal_screen_harmonic_power (const screen &scr, process_screen_harmonic k)
{
  std::complex<double> sum[3] = {};
  constexpr double two_pi = 2.0 * M_PI;
  for (int y = 0; y < screen::size; y++)
    for (int x = 0; x < screen::size; x++)
      {
        const double phase
            = -two_pi
              * (k.x * (x + 0.5) / screen::size
                 + k.y * (y + 0.5) / screen::size);
        const std::complex<double> e (std::cos (phase), std::sin (phase));
        for (int c = 0; c < 3; c++)
          sum[c] += (double)scr.mult[y][x][c] * e;
      }
  double power = 0;
  for (const std::complex<double> &v : sum)
    power += std::norm (v);
  return power;
}

/* Convert one integer screen-space harmonic to cycles per image pixel using
   the local screen-map Jacobian.  This also handles modest scale/rotation
   anisotropy instead of reducing every carrier to one scalar pixel size.  */
static coord_t
process_screen_harmonic_frequency (scr_to_img &map, point_t center,
                                   process_screen_harmonic k)
{
  const point_t s0 = map.to_scr (center);
  const point_t sx = map.to_scr ({ center.x + 1, center.y });
  const point_t sy = map.to_scr ({ center.x, center.y + 1 });
  const coord_t fx = k.x * (sx.x - s0.x) + k.y * (sx.y - s0.y);
  const coord_t fy = k.x * (sy.x - s0.x) + k.y * (sy.y - s0.y);
  const coord_t frequency = std::hypot (fx, fy);
  return my_isfinite (frequency) && frequency > 0 ? frequency : (coord_t)-1;
}

/* Evaluate the capture transfer represented by FIT.  SCREEN_FREQUENCY remains
   the nominal process carrier used by existing diagnostics and defocus-range
   seeding.  Paget/Finlay, however, has two comparably strong first carrier
   families: the diagonal (1,1) red/green family and the axial (2,0) family
   carrying substantial blue modulation.  Report the power-weighted RMS MTF of
   both families, which is the scalar attenuation of the dominant periodic
   screen modulation.  Other screen families retain the historical nominal-
   carrier value until their carrier sets are characterized explicitly.  */
static coord_t
fit_system_mtf (const render_parameters &rparam,
                const scr_to_img_parameters &param, const image_data &img,
                uint64_t flags, const finetune_result &fit,
                coord_t screen_frequency)
{
  if (!fit.success || !(screen_frequency > 0))
    return -1;
  mtf_parameters mtf
      = (flags & finetune_bw)
            ? rparam.get_image_layer_sharpen_parameters (&img).scanner_mtf
            : rparam.sharpen.scanner_mtf;
  if ((flags & finetune_scanner_mtf_sigma)
      && my_isfinite (fit.scanner_mtf_sigma))
    mtf.sigma = fit.scanner_mtf_sigma;
  if (flags & finetune_scanner_mtf_defocus)
    {
      if (mtf.simulate_diffraction_p ())
        {
          if (!my_isfinite (fit.scanner_mtf_defocus))
            return -1;
          mtf.defocus = fit.scanner_mtf_defocus;
        }
      else
        {
          if (!my_isfinite (fit.scanner_mtf_blur_diameter))
            return -1;
          mtf.blur_diameter = fit.scanner_mtf_blur_diameter;
        }
    }
  if (!(flags & finetune_bw) && img.has_rgb ())
    mtf.wavelength = 550;

  if (param.type == Paget || param.type == Finlay)
    {
      scr_to_img map;
      if (map.set_parameters (param, img))
        {
          screen ideal;
          ideal.initialize (param.type);
          static constexpr process_screen_harmonic harmonics[]
              = { { 1, 1 }, { 1, -1 }, { 2, 0 }, { 0, 2 } };
          const point_t center
              = { (coord_t)img.width * (coord_t)0.5,
                  (coord_t)img.height * (coord_t)0.5 };
          double power_sum = 0;
          double transferred_power = 0;
          for (process_screen_harmonic harmonic : harmonics)
            {
              const double power
                  = ideal_screen_harmonic_power (ideal, harmonic);
              const coord_t frequency
                  = process_screen_harmonic_frequency (map, center, harmonic);
              if (!(power > 0) || !(frequency > 0))
                continue;
              const coord_t value = mtf.system_mtf (frequency);
              if (!my_isfinite (value))
                continue;
              power_sum += power;
              transferred_power += power * value * value;
            }
          if (power_sum > 0 && transferred_power >= 0)
            {
              const coord_t value = std::sqrt (transferred_power / power_sum);
              if (my_isfinite (value))
                return value;
            }
        }
    }

  const coord_t value = mtf.system_mtf (screen_frequency);
  return my_isfinite (value) ? value : (coord_t)-1;
}
'''
replace_once("src/libcolorscreen/focus-analysis.C", old, new)

# Pass geometry to the effective carrier evaluation at both call sites.
p = Path("src/libcolorscreen/focus-analysis.C")
text = p.read_text()
text2 = text.replace(
    "fit_system_mtf (\n      rparam, img, fparams.flags, result->joint_fit, result->screen_frequency)",
    "fit_system_mtf (\n      rparam, param, img, fparams.flags, result->joint_fit,\n      result->screen_frequency)")
text2 = text2.replace(
    "fit_system_mtf (\n          rparam, img, fparams.flags, fit, result->screen_frequency)",
    "fit_system_mtf (\n          rparam, param, img, fparams.flags, fit, result->screen_frequency)")
if text2 == text:
    raise SystemExit("focus-analysis.C: no fit_system_mtf call updated")
if "fit_system_mtf (\n      rparam, img" in text2 or "fit_system_mtf (\n          rparam, img" in text2:
    raise SystemExit("focus-analysis.C: stale fit_system_mtf call")
p.write_text(text2)

replace_once(
    "src/libcolorscreen/include/focus-analysis.h",
    """  /* Process-screen carrier frequency and system MTF represented by JOINT_FIT.
     This is the transfer value directly relevant to restoring screen contrast. */
""",
    """  /* Nominal process-screen carrier frequency and effective system MTF
     represented by JOINT_FIT.  For Paget/Finlay the MTF is the ideal-screen-
     power-weighted RMS across the comparably strong diagonal and axial carrier
     families; other processes currently use the nominal carrier directly.  */
""",
)
replace_once(
    "src/colorscreen/colorscreen.C",
    'printf ("Joint process-screen MTF: %.6f%%\\n",',
    'printf ("Effective process-screen MTF: %.6f%%\\n",',
)
replace_once(
    "src/qtgui/MainWindow.cpp",
    'details << tr("Process-screen MTF at %1 cycles/pixel: %2%")',
    'details << tr("Effective process-screen MTF: %2% (nominal carrier %1 cycles/pixel)")',
)
replace_once(
    "src/qtgui/SharpnessPanel.cpp",
    '"leave-one-out and held-out validation. Coupled physical Sigma/"\n         "Defocus fits use robust scalar-prefit starts and report the "\n         "process-screen MTF relevant to colour recovery."));',
    '"leave-one-out and held-out validation. Coupled physical Sigma/"\n         "Defocus fits use robust scalar-prefit starts and report effective "\n         "process-screen modulation transfer relevant to colour recovery."));',
)
