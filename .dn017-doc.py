from pathlib import Path

p = Path('doc/denoise-tracking.md')
s = p.read_text()
old = '| DN-017 | Open | Medium | Decide whether filtering is best in linear intensity, density/log, or variance-stabilized domain for scanner/film noise. |'
new = '| DN-017 | Resolved | Medium | Multi-scale diagnostics compared linear intensity, density/log and a variance-stabilized transform on synthetic data plus real Hurley/Paget and Dufay scans. Density improves the Paget scan but consistently degrades Dufay; the fitted VST is neutral or worse. No universal transform is justified, so reconstruction denoising remains in linear intensity. |'
assert s.count(old) == 1
s = s.replace(old, new)
old_order = '''## Recommended next implementation order

1. Use the same multi-scale diagnostics to compare linear intensity with
   density/log or a variance-stabilized domain (DN-017).  Prefer a domain only
   if it improves scale invariance and cross-channel consistency on both real
   scans rather than merely improving one fitted coefficient.
2. Revisit automatic DN-013 coefficient estimation only if one domain gives
   stable scale behaviour; otherwise keep the explicit variance model opt-in.
3. Add IR-guided similarity for registered RGB+IR scans and compare it with
   the best calibrated RGB guide (DN-014).
4. Grow the quality corpus with stable real-scan crops and quantitative
   texture/edge/chromaticity/confidence checks (DN-016).
5. Only after quality is established, optimize the geometry-aware
   implementation (DN-018/DN-019).
'''
section = '''## DN-017 domain diagnostics

The three-scale estimator was also evaluated after transforming each admitted
sample into photographic density (`-log(x)`) and into a generalized
variance-stabilized coordinate derived from the fitted linear-domain model
`variance = a + b*x`.  The stabilizing transform is evaluated in the
numerically stable form

```
T(x) = 2*x / (sqrt(a + b*x) + sqrt(a)).
```

Density measurements reject non-positive samples rather than clipping them.
All three variants remain read-only diagnostics: they do not alter rendering
parameters or the domain used by NLM.  The table gives spacing-2/spacing-1,
spacing-3/spacing-1 robust variance ratios, followed by the relative error of
the spacing-1 floor-plus-slope fit.  Ratios closer to one and a smaller fit
error indicate a better noise-domain description.

| Scan / separation | linear | density/log | variance stabilized |
|---|---|---|---|
| Hurley/Paget red | `2.68 / 4.72 / 0.738` | `2.08 / 3.30 / 0.732` | `2.69 / 4.83 / 0.740` |
| Hurley/Paget green | `2.64 / 4.81 / 0.734` | `2.08 / 3.44 / 0.685` | `2.66 / 4.93 / 0.732` |
| Hurley/Paget blue | `1.58 / 2.82 / 0.729` | `1.49 / 2.26 / 0.572` | `1.53 / 2.68 / 0.736` |
| Dufay red | `1.14 / 1.27 / 0.776` | `1.14 / 1.28 / 0.878` | `1.14 / 1.27 / 0.776` |
| Dufay green | `1.20 / 1.33 / 0.340` | `1.24 / 1.42 / 0.842` | `1.20 / 1.33 / 0.426` |
| Dufay blue | `1.15 / 1.24 / 0.799` | `1.20 / 1.30 / 0.838` | `1.15 / 1.24 / 0.799` |

Density/log clearly reduces the structural scale dependence of the
Hurley/Paget scan, most strongly in blue, but it worsens every Dufay
separation.  The fitted VST gives no consistent improvement: constant-variance
linear fits reduce to a harmless rescaling, while the signal-dependent Dufay
green case becomes less well fitted.  Consequently there is no process- and
channel-independent reason to move reconstruction denoising away from linear
intensity.  DN-017 is therefore resolved as an evaluated alternative, not as a
new user-selectable parameter.

## Recommended next implementation order

1. Keep automatic DN-013 variance calibration deferred; the real-scan
   diagnostics do not support one process-independent noise model or domain.
2. Add IR-guided similarity for registered RGB+IR scans and compare it against
   the existing linear RGB guide (DN-014).
3. Grow the quality corpus with stable real-scan crops and quantitative
   texture/edge/chromaticity/confidence checks (DN-016).
4. Only after quality is established, optimize the geometry-aware
   implementation (DN-018/DN-019).
'''
assert s.count(old_order) == 1
s = s.replace(old_order, section)
p.write_text(s)
