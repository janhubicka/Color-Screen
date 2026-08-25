from pathlib import Path

ROOT = Path('.')

def replace(path, old, new, count=1):
    p = ROOT / path
    text = p.read_text()
    found = text.count(old)
    if found != count:
        raise RuntimeError(f'{path}: expected {count} occurrences, found {found}: {old[:80]!r}')
    p.write_text(text.replace(old, new, count))

def replace_all(path, old, new, count):
    replace(path, old, new, count)

def insert_before(path, marker, addition, count=1):
    replace(path, marker, addition + marker, count)

tracking = r'''# MTF channel-domain tracking

Color-Screen currently handles three different kinds of three-component data
which must not be confused merely because all of them are indexed 0, 1 and 2:

1. **Native capture channels** are scanner/camera red, green and blue samples.
   Their MTFs and wavelengths belong to the capture device.
2. **Process-screen primaries** are the red, green and blue transmission masks
   of the historical additive screen.  Their component index is a process
   colour index, not a scanner-channel index.
3. **The image layer** is one scalar capture signal.  It can be a real visible
   monochrome scan, a real infrared companion plane, or a scalar signal mixed
   from native RGB.

This distinction is especially important for the physical MTF model.  A
scanner-channel wavelength may select the transfer of a native R/G/B capture
channel, but it must never be selected merely because the same numeric index is
being used for a red/green/blue process-screen primary.

## Invariants

- Native RGB sharpening happens before RGB mixing and uses three capture-channel
  transfer functions.  `render.C` is the reference implementation of this
  invariant.
- A real scalar capture has one transfer.  With RGB plus a fourth plane the
  fourth plane is interpreted as infrared and defaults to 750 nm.  Standalone
  grayscale defaults to visible light (550 nm).
- An RGB-derived image layer is not one of the native scanner channels.  Until
  the exact spectral/spatial model below is implemented, its analytical screen
  simulation uses a documented representative wavelength rather than stealing
  a native measured R/G/B curve.
- A measured image-layer MTF belongs to the scalar image layer.  It must not be
  used automatically as a native R/G/B MTF.  Conversely, a selected native
  red/green/blue curve must not silently become the scalar image-layer curve.
- Arrays passed to `screen::initialize_with_sharpen_parameters()` index the
  three **process-screen primary planes**.  They are not scanner-channel arrays.

## Audit and work items

### MTF-DOM-001 — Native RGB render sharpening

**Status:** correct on current main.

`render::precompute_all()` specializes scanner sharpening independently for
native R/G/B and mixes an RGB-derived image layer only after those channels have
been sharpened.  Keep this as the semantic reference for full-image capture
processing.

### MTF-DOM-002 — Explicit image-layer measurement domain

**Status:** fixed by the first domain-correctness patch.

Slanted-edge measurements made from the image layer are marked explicitly.
Native-channel specialization must reject such a curve; scalar image-layer
specialization may select it.  Legacy unlabelled measurements remain distinct
from explicit image-layer measurements.

### MTF-DOM-003 — Grayscale slanted-edge setup

**Status:** fixed by the first GUI patch.

When a scan has no native RGB there is no source choice, so the measurement
source selector is hidden.  The dialog starts from the effective scalar capture
wavelength (explicit slot-3 wavelength or the visible 550 nm fallback) instead
of displaying an unexplained unknown wavelength.

### MTF-DOM-004 — Scalar screen/render/finetune specialization

**Status:** fixed by the first domain-correctness patch.

Screen simulation and BW finetune used to copy the generic sharpening object and
change only `scanner_mtf.wavelength`.  This could retain a selected native
measured curve, and the dynamic BW focus path could even enter the physical
model without setting the scalar wavelength.  Scalar users now request one
complete image-layer sharpening specialization.

### MTF-DOM-005 — RGB-derived scalar image-layer capture model

**Status:** open; corresponds to SIM-011.

For scanner channel `c`, the physically correct periodic signal is

```
C_c(f) = M_cR S_R(f) + M_cG S_G(f) + M_cB S_B(f)
C'_c(f) = H_c(f) C_c(f)
```

where `S_R/S_G/S_B` are process-primary spectra, `M` is the scanner response to
those primaries and `H_c` is the capture transfer of scanner channel `c`.
Only after applying the three transfers may the channels be mixed into a scalar
image layer.

Implement an exact Fourier-domain reference and benchmark it against the current
representative-transfer shortcut.  The prepared finetune source already stores
the three process-primary spectra, so the per-frequency 3x3 mixing is cheap and
requires only three inverse FFTs for RGB output.  For a final scalar RGB-derived
image layer, the scalar mix may be combined in Fourier space before the inverse
FFT and may need only one inverse transform.

### MTF-DOM-006 — Color finetune process-primary/scanner-channel crossing

**Status:** open, high priority after MTF-DOM-004.

`finetune_solver::capture_sharpen_parameters()` currently supplies three
scanner-channel wavelengths to an API whose three entries filter the three
process-primary planes.  The numeric indices happen to be red/green/blue in
both domains but the operation is not physically valid.  Do not "fix" this by
renaming the indices or by further specializing those three process-primary
planes.  Replace it with the MTF-DOM-005 spectral-mixing model and benchmark the
focus fit before changing production defaults.

### MTF-DOM-007 — Precise-RGB screen collection and superposition

**Status:** open.

`render_interpolate` precise-RGB collection and color `render_superpose_img`
consume native scanner RGB while their periodic screen model is still scalar.
They must share the same exact scanner-channel-after-spectral-mixing model as
MTF-DOM-005 rather than assigning capture wavelengths to process-primary
indices.

## Regression policy

Tests should make domain mistakes obvious by using deliberately very different
wavelengths/curves for R, G, B and scalar image-layer data.  At minimum cover:

- an explicit image-layer measured curve is not selected for native R/G/B;
- a native R/G/B measured curve is not selected for an RGB-derived scalar layer;
- a native scalar/IR curve is usable for a real fourth channel;
- standalone grayscale receives the visible scalar fallback;
- BW finetune and scalar periodic-screen paths carry the same specialized MTF
  state as direct scalar rendering;
- the eventual exact RGB screen model is compared against a slow reference with
  scanner response matrices that have strong off-diagonal terms.
'''
Path('doc/mtf-channel-domain-tracking.md').write_text(tracking)

# mtf_measurement: make image-layer measurements an explicit domain rather
# than overloading channel == -1.
replace('src/libcolorscreen/include/mtf-parameters.h',
'''  mtf_measurement ()
      : channel (-1), wavelength (0), same_capture (false),
        name ("Measured MTF")
''',
'''  mtf_measurement ()
      : channel (-1), image_layer (false), wavelength (0), same_capture (false),
        name ("Measured MTF")
''')
replace('src/libcolorscreen/include/mtf-parameters.h',
'''  /* Channel: -1 unknown, 0 red, 1 green, 2 blue, 3 IR  */
  int channel;
  /* Wavelength in nanometers.  A positive value is authoritative for this
''',
'''  /* Channel: -1 unknown, 0 red, 1 green, 2 blue, 3 IR.  */
  int channel;
  /* True when this curve measures the scalar image layer rather than one
     native capture channel.  Keep this independent from CHANNEL: -1 still
     represents a genuinely unlabelled/generic curve for compatibility.  */
  bool image_layer;
  /* Wavelength in nanometers.  A positive value is authoritative for this
''')
replace('src/libcolorscreen/include/mtf-parameters.h',
'''    return channel == o.channel && wavelength == o.wavelength
           && same_capture == o.same_capture && name == o.name
''',
'''    return channel == o.channel && image_layer == o.image_layer
           && wavelength == o.wavelength && same_capture == o.same_capture
           && name == o.name
''')

# Public scalar specialization API.
replace('src/libcolorscreen/include/render-parameters.h',
'''  DLL_PUBLIC sharpen_parameters
  get_sharpen_parameters_for_channel (int channel, bool has_rgb = true) const;

  /***** Tile Adjustment (used to adjust parameters of individual tiles) *****/
''',
'''  DLL_PUBLIC sharpen_parameters
  get_sharpen_parameters_for_channel (int channel, bool has_rgb = true) const;

  /* Return capture sharpening specialized for the scalar image layer.  This
     selects scalar/image-layer measured data and the effective visible/IR
     wavelength without treating process-primary or scanner RGB indexes as the
     same domain.  */
  DLL_PUBLIC sharpen_parameters
  get_image_layer_sharpen_parameters (const image_data *img) const;

  /***** Tile Adjustment (used to adjust parameters of individual tiles) *****/
''')

# Native channels must not consume explicit image-layer measurements.
replace('src/libcolorscreen/render-parameters.C',
'''      if (selected_measurement.channel < 0
          || selected_measurement.channel == channel)
        measurement_index = selected;
''',
'''      if (selected_measurement.image_layer)
        measurement_index = -1;
      else if (selected_measurement.channel < 0
               || selected_measurement.channel == channel)
        measurement_index = selected;
''')
replace('src/libcolorscreen/render-parameters.C',
'''      /* An image-layer measurement intentionally applies to every channel.
         For native measurements, never deconvolve one channel using another
         channel's measured transfer function.  If this capture group does not
         contain CHANNEL, disabling the direct curve lets the configured
         analytical/fallback model take over.  */
''',
'''      /* Explicit image-layer measurements never apply to native channels.
         For native measurements, never deconvolve one channel using another
         channel's measured transfer function.  Legacy unlabelled measurements
         retain their historical generic behavior.  If this capture group does
         not contain CHANNEL, disabling the direct curve lets the configured
         analytical/fallback model take over.  */
''')
insert_before('src/libcolorscreen/render-parameters.C',
'''/* Return portion of screen occupied by red, green and blue patches for
''',
'''/* Return sharpening specialized for the scalar image layer of IMG.  A
   selected explicit image-layer measurement is authoritative.  A real native
   scalar plane may otherwise use its channel-3 measurement.  An RGB-derived
   scalar layer must not steal a native RGB measured curve; until the exact
   spectral/spatial model is implemented it uses the configured analytical
   representative wavelength instead.  */
sharpen_parameters
render_parameters::get_image_layer_sharpen_parameters (const image_data *img) const
{
  const int channel = get_image_layer_channel (img);
  const bool has_rgb = !img || img->has_rgb ();
  const mtf_parameters &base_mtf = sharpen.scanner_mtf;
  const bool selected_image_layer
      = base_mtf.use_measured_mtf ()
        && base_mtf.measurements[base_mtf.measured_mtf_idx].image_layer;

  sharpen_parameters result;
  if (selected_image_layer)
    result = sharpen;
  else if (channel == 3)
    result = get_sharpen_parameters_for_channel (3, has_rgb);
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
}

''')

# Persist the explicit measurement domain.
replace('src/libcolorscreen/loadsave.C',
'''                || fprintf (f,
                            "scanner_mtf_measurement_wavelength_nm: %.17g\\n",
                            measurement.wavelength)
                       < 0
                || fprintf (f, "scanner_mtf_measurement_same_capture: %s\\n",
                            bool_names[(int)measurement.same_capture])
''',
'''                || fprintf (f,
                            "scanner_mtf_measurement_wavelength_nm: %.17g\\n",
                            measurement.wavelength)
                       < 0
                || fprintf (f, "scanner_mtf_measurement_image_layer: %s\\n",
                            bool_names[(int)measurement.image_layer])
                       < 0
                || fprintf (f, "scanner_mtf_measurement_same_capture: %s\\n",
                            bool_names[(int)measurement.same_capture])
''')
insert_before('src/libcolorscreen/loadsave.C',
'''      else if (!strcmp (buf, "scanner_mtf_measurement_same_capture"))
''',
'''      else if (!strcmp (buf, "scanner_mtf_measurement_image_layer"))
        {
          if (measurement < 0)
            {
              *error
                  = "scanner_mtf_measurement_image_layer specified without "
                    "scanner_mtf_measurement";
              return false;
            }
          bool *image_layer
              = rparam
                    ? &rparam->sharpen.scanner_mtf.measurements[measurement]
                           .image_layer
                    : nullptr;
          if (!parse_bool (f, image_layer))
            {
              *error = "error parsing scanner_mtf_measurement_image_layer";
              return false;
            }
        }
''')

# Slanted-edge channel -1 from this API is explicitly the image layer.
replace('src/libcolorscreen/slanted-edge.C',
'''  measurement.channel = params.channel;
  measurement.wavelength = params.wavelength;
''',
'''  measurement.channel = params.channel;
  measurement.image_layer = params.channel < 0;
  measurement.wavelength = params.wavelength;
''')

# Scalar periodic-screen users request a complete image-layer specialization.
replace('src/libcolorscreen/render-superposeimg.h',
'''\tsharpen = m_params.sharpen;
\tcoord_t psize = pixel_size ();
\tsharpen.usm_radius = m_params.screen_blur_radius * psize;
\tsharpen.scanner_mtf_scale *= psize;
\tsharpen.scanner_mtf.wavelength
\t    = m_params.get_image_layer_wavelength (&m_img);
''',
'''\tsharpen = m_params.get_image_layer_sharpen_parameters (&m_img);
\tcoord_t psize = pixel_size ();
\tsharpen.usm_radius = m_params.screen_blur_radius * psize;
\tsharpen.scanner_mtf_scale *= psize;
''')

replace_all('src/libcolorscreen/render-interpolate.C',
'''      sharpen_parameters sharpen = m_params.sharpen;
      sharpen.usm_radius = m_params.screen_blur_radius * psize;
      sharpen.scanner_mtf_scale *= psize;
      sharpen.scanner_mtf.wavelength
          = m_params.get_image_layer_wavelength (&m_img);
''',
'''      sharpen_parameters sharpen
          = m_params.get_image_layer_sharpen_parameters (&m_img);
      sharpen.usm_radius = m_params.screen_blur_radius * psize;
      sharpen.scanner_mtf_scale *= psize;
''', 2)

replace('src/libcolorscreen/render-to-scr.C',
'''      p.sharpen = m_params.sharpen;
      p.sharpen.scanner_mtf_scale *= pixel_size ();
      p.sharpen.scanner_mtf.wavelength
          = m_params.get_image_layer_wavelength (&m_img);
''',
'''      p.sharpen = m_params.get_image_layer_sharpen_parameters (&m_img);
      p.sharpen.scanner_mtf_scale *= pixel_size ();
''')
replace('src/libcolorscreen/render-to-scr.C',
'''  sharpen_parameters sharpen = m_params.sharpen;
  sharpen.usm_radius = m_params.screen_blur_radius * psize;
  sharpen.scanner_mtf_scale *= psize;
  sharpen.scanner_mtf.wavelength
      = m_params.get_image_layer_wavelength (&m_img);
''',
'''  sharpen_parameters sharpen
      = m_params.get_image_layer_sharpen_parameters (&m_img);
  sharpen.usm_radius = m_params.screen_blur_radius * psize;
  sharpen.scanner_mtf_scale *= psize;
''')

replace('src/libcolorscreen/stitch-image.C',
'''  sharpen_parameters sharpen = my_rparam.sharpen;
  sharpen.usm_radius = m_prj->pixel_size * my_rparam.screen_blur_radius;
  sharpen.scanner_mtf_scale *= m_prj->pixel_size;
  sharpen.scanner_mtf.wavelength
      = my_rparam.get_image_layer_wavelength (img.get ());
''',
'''  sharpen_parameters sharpen
      = my_rparam.get_image_layer_sharpen_parameters (img.get ());
  sharpen.usm_radius = m_prj->pixel_size * my_rparam.screen_blur_radius;
  sharpen.scanner_mtf_scale *= m_prj->pixel_size;
''')

replace('src/libcolorscreen/render-simulate.C',
'''  struct simulation_params p = {m_img.id, &m_img, this, m_params.gamma, m_scr_to_img.get_param (), m_params.sharpen};
  p.sharpen.mode = sharpen_parameters::blur_deconvolution;
''',
'''  sharpen_parameters image_layer_sharpen
      = m_params.get_image_layer_sharpen_parameters (&m_img);
  struct simulation_params p = {m_img.id, &m_img, this, m_params.gamma,
                                m_scr_to_img.get_param (),
                                image_layer_sharpen};
  p.sharpen.mode = sharpen_parameters::blur_deconvolution;
''')

# BW finetune must retain scalar measured-curve selection as well as wavelength.
replace('src/libcolorscreen/finetune.C',
'''  sharpen_parameters render_sharpen_params;
  double img_layer_wavelength = 530;
''',
'''  sharpen_parameters render_sharpen_params;
  sharpen_parameters image_layer_sharpen_params;
''')
replace('src/libcolorscreen/finetune.C',
'''    std::array<sharpen_parameters, 3> sp
        = { render_sharpen_params, render_sharpen_params,
            render_sharpen_params };
''',
'''    const sharpen_parameters &base_sharpen
        = tiles[0].color.empty () ? image_layer_sharpen_params
                                  : render_sharpen_params;
    std::array<sharpen_parameters, 3> sp
        = { base_sharpen, base_sharpen, base_sharpen };
''')
replace('src/libcolorscreen/finetune.C',
'''            sharpen_parameters sp = render_sharpen_params;
            sp.scanner_mtf_scale *= pixel_size;
            sp.scanner_mtf.wavelength = img_layer_wavelength;
''',
'''            sharpen_parameters sp
                = tiles[0].color.empty () ? image_layer_sharpen_params
                                          : render_sharpen_params;
            sp.scanner_mtf_scale *= pixel_size;
''')
replace_all('src/libcolorscreen/finetune.C',
'''            solver.render_sharpen_params = rparam.sharpen;
            solver.img_layer_wavelength
                = rparam.get_image_layer_wavelength (imgp[0]);
''',
'''            solver.render_sharpen_params = rparam.sharpen;
            solver.image_layer_sharpen_params
                = rparam.get_image_layer_sharpen_parameters (imgp[0]);
''', 2)
replace('src/libcolorscreen/finetune.C',
'''      best_solver.render_sharpen_params = rparam.sharpen;
      best_solver.img_layer_wavelength
          = rparam.get_image_layer_wavelength (imgp[0]);
''',
'''      best_solver.render_sharpen_params = rparam.sharpen;
      best_solver.image_layer_sharpen_params
          = rparam.get_image_layer_sharpen_parameters (imgp[0]);
''')
replace('src/libcolorscreen/finetune.C',
'''  sharpen_parameters sharpen = rparam.sharpen;
  sharpen.usm_radius = rparam.screen_blur_radius * pixel_size;
  sharpen.scanner_mtf_scale *= pixel_size;
  sharpen.scanner_mtf.wavelength
      = rparam.get_image_layer_wavelength (&img);
''',
'''  sharpen_parameters sharpen
      = rparam.get_image_layer_sharpen_parameters (&img);
  sharpen.usm_radius = rparam.screen_blur_radius * pixel_size;
  sharpen.scanner_mtf_scale *= pixel_size;
''')

# Unit coverage for domain specialization and explicit image-layer metadata.
insert_before('src/libcolorscreen/unittests.C',
'''  const double expected_magnification = 0.27933543307086614;
''',
'''  /* Image-layer measurements and native-channel measurements are distinct
     domains even though both live in the same MTF collection.  */
  render_parameters domain_render;
  mtf_measurement scalar_measurement;
  scalar_measurement.channel = -1;
  scalar_measurement.image_layer = true;
  scalar_measurement.wavelength = 575;
  scalar_measurement.add_value (0, 100);
  scalar_measurement.add_value (0.1, 60);
  scalar_measurement.add_value (0.2, 20);
  domain_render.sharpen.scanner_mtf.measurements.push_back (
      scalar_measurement);
  domain_render.sharpen.scanner_mtf.measured_mtf_idx = 0;
  if (domain_render.get_sharpen_parameters_for_channel (0)
              .scanner_mtf.measured_mtf_idx
          != -1
      || domain_render.get_image_layer_sharpen_parameters (nullptr)
                 .scanner_mtf.measured_mtf_idx
             != 0
      || domain_render.get_image_layer_sharpen_parameters (nullptr)
                 .scanner_mtf.wavelength
             != 575)
    {
      fprintf (stderr, "Image-layer measured MTF leaked into native RGB\\n");
      ok = false;
    }

  render_parameters native_domain_render;
  mtf_measurement red_measurement;
  red_measurement.channel = 0;
  red_measurement.wavelength = 640;
  red_measurement.add_value (0, 100);
  red_measurement.add_value (0.1, 70);
  red_measurement.add_value (0.2, 30);
  native_domain_render.sharpen.scanner_mtf.measurements.push_back (
      red_measurement);
  native_domain_render.sharpen.scanner_mtf.measured_mtf_idx = 0;
  if (native_domain_render.get_sharpen_parameters_for_channel (0)
              .scanner_mtf.measured_mtf_idx
          != 0
      || native_domain_render.get_image_layer_sharpen_parameters (nullptr)
                 .scanner_mtf.measured_mtf_idx
             != -1)
    {
      fprintf (stderr, "Native RGB measured MTF leaked into image layer\\n");
      ok = false;
    }

''')

# GUI: hide the meaningless source selector when only one scalar source exists.
replace('src/qtgui/SlantedEdgeDialog.cpp',
'''  form->addRow(tr("Measure:"), m_sourceCombo);
''',
'''  auto *sourceLabel = new QLabel(tr("Measure:"), this);
  form->addRow(sourceLabel, m_sourceCombo);
  sourceLabel->setVisible(hasRgb);
  m_sourceCombo->setVisible(hasRgb);
''')
replace('src/qtgui/SlantedEdgeDialog.cpp',
'''  auto *description = new QLabel(
      tr("For RGB scans the default is to measure each native scanner channel "
         "from the selected edge. The image layer can still be measured "
         "directly when its RGB mixture is itself the quantity of interest."),
      this);
''',
'''  auto *description = new QLabel(
      hasRgb
          ? tr("For RGB scans the default is to measure each native scanner "
               "channel from the selected edge. The image layer can still be "
               "measured directly when its RGB mixture is itself the quantity "
               "of interest.")
          : tr("This scan has one monochrome capture channel. The image layer "
               "is measured directly."),
      this);
''')

# Both document and external-reference measurement entry points use the scalar
# default for grayscale instead of reusing an unrelated/unknown prior value.
old_defaults = '''    colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
    if (defaults.wavelength <= 0) {
      if (!currentMtf.measurements.empty()
          && currentMtf.measurements.back().wavelength > 0)
        defaults.wavelength = currentMtf.measurements.back().wavelength;
      else if (currentMtf.wavelength > 0)
        defaults.wavelength = currentMtf.wavelength;
    }

    const bool hasRgb = m_scan && m_scan->has_rgb();
    const bool hasInfrared = m_scan && m_scan->has_grayscale_or_ir();
'''
new_defaults = '''    colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
    const bool hasRgb = m_scan && m_scan->has_rgb();
    const bool hasInfrared = m_scan && m_scan->has_grayscale_or_ir();
    if (!hasRgb && m_scan) {
      defaults.wavelength
          = currentState.rparams.get_image_layer_wavelength(m_scan.get());
    } else if (defaults.wavelength <= 0) {
      if (!currentMtf.measurements.empty()
          && currentMtf.measurements.back().wavelength > 0)
        defaults.wavelength = currentMtf.measurements.back().wavelength;
      else if (currentMtf.wavelength > 0)
        defaults.wavelength = currentMtf.wavelength;
    }

'''
replace('src/qtgui/MainWindow.cpp', old_defaults, new_defaults)

old_reference_defaults = '''  colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
  if (defaults.wavelength <= 0) {
    if (!currentMtf.measurements.empty() &&
        currentMtf.measurements.back().wavelength > 0)
      defaults.wavelength = currentMtf.measurements.back().wavelength;
    else if (currentMtf.wavelength > 0)
      defaults.wavelength = currentMtf.wavelength;
  }

  const bool hasRgb = m_scan->has_rgb();
  const bool hasInfrared = m_scan->has_grayscale_or_ir();
'''
new_reference_defaults = '''  colorscreen::slanted_edge_parameters defaults = m_slantedEdgeParameters;
  const bool hasRgb = m_scan->has_rgb();
  const bool hasInfrared = m_scan->has_grayscale_or_ir();
  if (!hasRgb) {
    defaults.wavelength
        = currentState.rparams.get_image_layer_wavelength(m_scan.get());
  } else if (defaults.wavelength <= 0) {
    if (!currentMtf.measurements.empty() &&
        currentMtf.measurements.back().wavelength > 0)
      defaults.wavelength = currentMtf.measurements.back().wavelength;
    else if (currentMtf.wavelength > 0)
      defaults.wavelength = currentMtf.wavelength;
  }

'''
replace('src/qtgui/ImageViewWindow.cpp', old_reference_defaults,
        new_reference_defaults)

print('MTF domain patch applied')
