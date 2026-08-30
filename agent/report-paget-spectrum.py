#!/usr/bin/env python3
from pathlib import Path

p = Path("src/libcolorscreen/focus-analysis-unittests.C")
text = p.read_text()
old = '#include <string>\n'
new = '#include <string>\n#include <complex>\n'
if text.count(old) != 1:
    raise SystemExit("include anchor mismatch")
text = text.replace(old, new, 1)

anchor = """\n\nint\nmain ()\n{\n"""
func = r'''

static void
report_paget_spectrum ()
{
  screen source;
  source.initialize (Paget);
  fprintf (stderr, "PAGET_PROPERTY_FREQUENCY %.12g\n",
           (double)scr_names[Paget].frequency);
  const double nrm = 1.0 / (screen::size * screen::size);
  for (int ky = 0; ky <= 4; ky++)
    for (int kx = -4; kx <= 4; kx++)
      {
        if (!kx && !ky)
          continue;
        /* Report one representative from conjugate pairs.  */
        if (!ky && kx < 0)
          continue;
        std::complex<double> coeff[3] = {};
        for (int y = 0; y < screen::size; y++)
          for (int x = 0; x < screen::size; x++)
            {
              const double phase
                  = -2.0 * M_PI
                    * ((double)kx * x + (double)ky * y) / screen::size;
              const std::complex<double> e = std::polar (1.0, phase);
              for (int c = 0; c < 3; c++)
                coeff[c] += (double)source.mult[y][x][c] * e;
            }
        double power = 0;
        for (int c = 0; c < 3; c++)
          {
            coeff[c] *= nrm;
            power += std::norm (coeff[c]);
          }
        const double amplitude = std::sqrt (power);
        if (amplitude > 1e-4)
          fprintf (stderr,
                   "PAGET_HARMONIC k=(%d,%d) radius=%.12g amp=%.12g "
                   "rgb=(%.9g,%.9g,%.9g)\n",
                   kx, ky, std::hypot ((double)kx, (double)ky), amplitude,
                   std::abs (coeff[0]), std::abs (coeff[1]),
                   std::abs (coeff[2]));
      }
}
'''
if text.count(anchor) != 1:
    raise SystemExit("main anchor mismatch")
text = text.replace(anchor, func + anchor, 1)
oldmain = """main ()\n{\n  if (!test_joint_focus_analysis ()\n"""
newmain = """main ()\n{\n  report_paget_spectrum ();\n  if (!test_joint_focus_analysis ()\n"""
if text.count(oldmain) != 1:
    raise SystemExit("main body anchor mismatch")
text = text.replace(oldmain, newmain, 1)
p.write_text(text)
