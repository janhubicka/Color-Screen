#!/usr/bin/env python3
from pathlib import Path

p = Path('src/libcolorscreen/render-parameters.C')
s = p.read_text()
old = '''  sharpen_parameters result;
  if (selected_image_layer)
    result = sharpen;
  else if (native_channel >= 0)
    result = get_sharpen_parameters_for_channel (native_channel, has_rgb);
  else
    {
      result = sharpen;
      if (result.scanner_mtf.use_measured_mtf ())
        result.scanner_mtf.measured_mtf_idx = -1;
    }

  double wavelength = get_image_layer_wavelength (img);
  if (result.scanner_mtf.use_measured_mtf ())
    {
      const mtf_measurement &measurement
          = result.scanner_mtf.measurements[result.scanner_mtf.measured_mtf_idx];
      if (my_isfinite (measurement.wavelength) && measurement.wavelength > 0)
        wavelength = measurement.wavelength;
    }
  if (my_isfinite (wavelength) && wavelength > 0)
    result.scanner_mtf.wavelength = wavelength;
  return result;
}'''
new = '''  if (!selected_image_layer && native_channel >= 0)
    return get_sharpen_parameters_for_channel (native_channel, has_rgb);

  sharpen_parameters result = sharpen;
  if (!selected_image_layer && result.scanner_mtf.use_measured_mtf ())
    result.scanner_mtf.measured_mtf_idx = -1;

  double wavelength = get_image_layer_wavelength (img);
  if (selected_image_layer && result.scanner_mtf.use_measured_mtf ())
    {
      const mtf_measurement &measurement
          = result.scanner_mtf.measurements[result.scanner_mtf.measured_mtf_idx];
      if (my_isfinite (measurement.wavelength) && measurement.wavelength > 0)
        wavelength = measurement.wavelength;
    }
  if (my_isfinite (wavelength) && wavelength > 0)
    result.scanner_mtf.wavelength = wavelength;
  return result;
}'''
if s.count(old) != 1:
    raise SystemExit(f'render-parameters.C specialization block count {s.count(old)}')
p.write_text(s.replace(old, new, 1))

p = Path('doc/mtf-channel-domain-tracking.md')
s = p.read_text()
old = '''The one-hot test is exact rather than epsilon-based: any second nonzero mix
weight means that two different capture transfers contribute and must use the
general model below.'''
new = '''The one-hot test is exact rather than epsilon-based: any second nonzero mix
weight means that two different capture transfers contribute and must use a
multi-channel model.  The one-channel simplification is valid regardless of
capture provenance, including a transparency scanned with its viewing filter:
for one selected scanner channel the same `H_c` filters the complete
process-primary mixture, so convolution distributes over that sum.'''
if s.count(old) != 1:
    raise SystemExit(f'tracking one-hot paragraph count {s.count(old)}')
p.write_text(s.replace(old, new, 1))
