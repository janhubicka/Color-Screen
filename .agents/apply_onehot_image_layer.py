#!/usr/bin/env python3
import pathlib
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")


def read(path):
    return (root / path).read_text()


def write(path, text):
    (root / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}")
    write(path, text.replace(old, new, 1))


# 1. Add a non-generating lookup to the simple LRU cache.
path = "src/libcolorscreen/lru-cache.h"
old = '''  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL,
       bool *cache_hit = NULL)
  {
    return this->get_internal (
        p, progress, id, cache_hit,
        [&](Entry *e) { return p == e->params; },
        [](Entry *) {},
        [&](Entry *e) { return get_new (e->params, progress); });
  }
};
'''
new = '''  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL,
       bool *cache_hit = NULL)
  {
    return this->get_internal (
        p, progress, id, cache_hit,
        [&](Entry *e) { return p == e->params; },
        [](Entry *) {},
        [&](Entry *e) { return get_new (e->params, progress); });
  }

  /* Return a completed cached value for P without generating or waiting for
     one.  A matching entry which is still being computed is deliberately
     treated as a miss.  The returned shared pointer pins the cached value
     against eviction while the caller uses it.  */
  std::shared_ptr<T>
  peek (const P &p, uint64_t *id = NULL)
  {
    std::shared_lock<std::shared_timed_mutex> guard (this->lock);
    for (Entry *e = this->entries; e; e = e->next)
      if (!e->computing && e->val && p == e->params)
        {
          if (id)
            *id = e->id;
          return e->val;
        }
    return nullptr;
  }
};
'''
replace_once(path, old, new)

# 2. Expose exact native-channel provenance for scalar image-layer mixes.
path = "src/libcolorscreen/include/render-parameters.h"
old = '''  /* Return the scanner-channel slot used to model the scalar image layer.
     RGB-derived image layers use green as the current representative channel.
     A native scalar plane uses slot 3; whether its default wavelength is
     infrared or visible is decided from the presence of RGB data.  */
  int get_image_layer_channel (const image_data *img) const
  {
    if (!img)
      return 1;
    bool ir_sim = !img->has_grayscale_or_ir ()
                  || (img->has_rgb () && ignore_infrared);
    return ir_sim ? 1 : 3;
  }
  /* Return the wavelength used by the current scalar image-layer model.  */
  double get_image_layer_wavelength (const image_data *img) const
  {
    const int channel = get_image_layer_channel (img);
    return sharpen.scanner_mtf.get_channel_wavelength (
        channel, !img || img->has_rgb ());
  }
'''
new = '''  /* Return the native capture channel which exactly supplies the scalar
     image layer, or -1 when the layer is a genuine RGB mixture.  A real
     grayscale/IR plane is channel 3.  For an RGB-derived layer, infer a native
     channel only when exactly one finite mix weight is nonzero; the selected
     weight may have any magnitude because gain does not change normalized
     MTF.  Exact zero is intentional: even a tiny second-channel contribution
     introduces another capture transfer.  */
  int get_image_layer_native_channel (const image_data *img) const
  {
    if (!img)
      return -1;
    const bool ir_sim = !img->has_grayscale_or_ir ()
                        || (img->has_rgb () && ignore_infrared);
    if (!ir_sim)
      return 3;
    if (!img->has_rgb ())
      return -1;

    const luminosity_t weights[3] = { mix_red, mix_green, mix_blue };
    int channel = -1;
    for (int c = 0; c < 3; ++c)
      {
        if (!my_isfinite ((double)weights[c]))
          return -1;
        if (weights[c] != 0)
          {
            if (channel >= 0)
              return -1;
            channel = c;
          }
      }
    return channel;
  }

  /* Return the scanner-channel slot used to model the scalar image layer.
     Exact one-channel RGB mixes use that native channel.  Genuine RGB mixtures
     keep green as the current representative analytical channel.  A native
     scalar plane uses slot 3; whether its default wavelength is infrared or
     visible is decided from the presence of RGB data.  */
  int get_image_layer_channel (const image_data *img) const
  {
    const int native_channel = get_image_layer_native_channel (img);
    return native_channel >= 0 ? native_channel : 1;
  }
  /* Return the wavelength used by the current scalar image-layer model.  */
  double get_image_layer_wavelength (const image_data *img) const
  {
    const int channel = get_image_layer_channel (img);
    return sharpen.scanner_mtf.get_channel_wavelength (
        channel, !img || img->has_rgb ());
  }
'''
replace_once(path, old, new)

# 3. Let scalar specialization inherit a native MTF for an exact one-hot mix.
path = "src/libcolorscreen/render-parameters.C"
text = read(path)
start_anchor = "/* Return sharpening specialized for the scalar image layer of IMG."
end_anchor = "\n/* Return portion of screen occupied by red, green and blue patches for"
start = text.find(start_anchor)
end = text.find(end_anchor, start)
if start < 0 or end < 0:
    raise SystemExit("render-parameters.C: scalar specialization anchors missing")
new_block = '''/* Return sharpening specialized for the scalar image layer of IMG.  A
   selected explicit image-layer measurement is authoritative.  A real native
   scalar plane may otherwise use its channel-3 measurement.  An exact one-hot
   RGB mix inherits the selected native scanner channel, including its measured
   curve and wavelength.  A genuine RGB mixture is not any one scanner channel:
   RGB capture of a monochrome original needs a weighted combination of channel
   transfers, while a transparency captured with its viewing filter present
   requires process-primary spectral mixing before scanner-channel transfer.
   Until those mixed-channel forward models exist, do not steal one native
   measured RGB curve; use the configured representative analytical transfer.  */
sharpen_parameters
render_parameters::get_image_layer_sharpen_parameters (const image_data *img) const
{
  const int native_channel = get_image_layer_native_channel (img);
  const bool has_rgb = !img || img->has_rgb ();
  const mtf_parameters &base_mtf = sharpen.scanner_mtf;
  const bool selected_image_layer
      = base_mtf.use_measured_mtf ()
        && base_mtf.measurements[base_mtf.measured_mtf_idx].image_layer;

  sharpen_parameters result;
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
}
'''
write(path, text[:start] + new_block + text[end:])

# 4. Optimize one-hot image-layer rendering and reuse a completed RGB cache.
path = "src/libcolorscreen/render.C"
text = read(path)
start_anchor = '''  const bool ir_simulation
      = !m_img.has_grayscale_or_ir ()
        || (m_img.has_rgb () && m_params.ignore_infrared);
'''
end_anchor = "\n  if (m_params.contact_copy.simulate)"
start = text.find(start_anchor)
end = text.find(end_anchor, start)
if start < 0 or end < 0:
    raise SystemExit("render.C: image-layer precompute anchors missing")
new_block = '''  const bool ir_simulation
      = !m_img.has_grayscale_or_ir ()
        || (m_img.has_rgb () && m_params.ignore_infrared);
  const bool scanner_sharpening
      = m_img.has_rgb ()
        && m_params.sharpen.get_mode () != sharpen_parameters::none;
  const int image_layer_native_channel
      = image_layer_needed && ir_simulation
            ? m_params.get_image_layer_native_channel (&m_img)
            : -1;
  const bool one_channel_rgb_image_layer
      = image_layer_native_channel >= 0 && image_layer_native_channel < 3;

  /* Scanner sharpening belongs to native capture channels, before any RGB
     mixture is formed.  A genuine RGB mixture needs all three channels, but an
     exact one-channel image-layer mix can avoid that work unless RGB output is
     itself requested.  If the identical full RGB result already exists, reuse
     it without starting another computation.  */
  if (scanner_sharpening)
    {
      rgb_and_sharpen_params p
          = { { m_img.id,
                &m_img,
                m_params.gamma,
                { m_img.to_linear[0], m_img.to_linear[1],
                  m_img.to_linear[2] },
                rgbdata{0, 0, 0},
                1.0f,
                1.0f,
                1.0f,
                m_backlight_correction.get (),
                m_backlight_correction_id,
                true },
              { m_params.get_sharpen_parameters_for_channel (0),
                m_params.get_sharpen_parameters_for_channel (1),
                m_params.get_sharpen_parameters_for_channel (2) } };
      const bool require_full_rgb
          = rgb_image_needed
            || (image_layer_needed && ir_simulation
                && !one_channel_rgb_image_layer);
      if (require_full_rgb)
        m_rgb_image_holder = rgb_and_sharpened_data_cache.get (p, progress);
      else if (image_layer_needed && ir_simulation
               && one_channel_rgb_image_layer)
        m_rgb_image_holder = rgb_and_sharpened_data_cache.peek (p);

      if (m_rgb_image_holder)
        {
          m_rgb_image = m_rgb_image_holder->m_data;
          if (colorscreen_checking)
            assert (m_rgb_image);
        }
      else if (require_full_rgb)
        return false;
    }

  if (image_layer_needed)
    {
      if (ir_simulation && m_rgb_image)
        {
          /* RGB-derived image layers are mixed from already-sharpened native
             channels.  Mixing first and deconvolving afterwards would impose
             one transfer function on three spectrally different channels.  */
          m_image_layer_holder
              = std::make_shared<sharpened_data> (m_img.width, m_img.height);
          if (!m_image_layer_holder->m_data)
            return false;
          m_image_layer = m_image_layer_holder->m_data;
          const size_t size = (size_t)m_img.width * m_img.height;
#pragma omp parallel for
          for (size_t i = 0; i < size; ++i)
            {
              const rgbdata pixel = m_rgb_image[i];
              m_image_layer[i]
                  = (pixel.red - m_params.mix_dark.red) * m_params.mix_red
                    + (pixel.green - m_params.mix_dark.green)
                          * m_params.mix_green
                    + (pixel.blue - m_params.mix_dark.blue)
                          * m_params.mix_blue;
            }
          m_image_layer_id = lru_caches::get ();
        }
      else if (ir_simulation && scanner_sharpening
               && one_channel_rgb_image_layer)
        {
          /* No full RGB result was requested or cached.  Sharpen only the
             native channel which actually contributes to the image layer.
             Keep mix-dark and gain after sharpening so this is equivalent to
             the full RGB sharpen-then-mix path even for Wiener/RL filters.  */
          const int channel = image_layer_native_channel;
          gray_and_sharpen_params p
              = { { m_img.id,
                    &m_img,
                    m_params.gamma,
                    { m_img.to_linear[0], m_img.to_linear[1],
                      m_img.to_linear[2] },
                    rgbdata{0, 0, 0},
                    channel == 0 ? 1.0f : 0.0f,
                    channel == 1 ? 1.0f : 0.0f,
                    channel == 2 ? 1.0f : 0.0f,
                    m_backlight_correction.get (),
                    m_backlight_correction_id,
                    true },
                  m_params.get_image_layer_sharpen_parameters (&m_img) };
          uint64_t channel_id = 0;
          std::shared_ptr<sharpened_data> channel_holder
              = gray_and_sharpened_data_cache.get (p, progress, &channel_id);
          if (!channel_holder)
            return false;

          const luminosity_t weight
              = channel == 0 ? m_params.mix_red
                : channel == 1 ? m_params.mix_green
                               : m_params.mix_blue;
          const luminosity_t dark
              = channel == 0 ? m_params.mix_dark.red
                : channel == 1 ? m_params.mix_dark.green
                               : m_params.mix_dark.blue;
          if (weight == 1 && dark == 0)
            {
              m_image_layer_holder = channel_holder;
              m_image_layer = channel_holder->m_data;
              m_image_layer_id = channel_id;
            }
          else
            {
              m_image_layer_holder
                  = std::make_shared<sharpened_data> (m_img.width, m_img.height);
              if (!m_image_layer_holder->m_data)
                return false;
              m_image_layer = m_image_layer_holder->m_data;
              const size_t size = (size_t)m_img.width * m_img.height;
#pragma omp parallel for
              for (size_t i = 0; i < size; ++i)
                m_image_layer[i]
                    = (channel_holder->m_data[i] - dark) * weight;
              m_image_layer_id = lru_caches::get ();
            }
        }
      else
        {
          sharpen_parameters image_layer_sharpen = m_params.sharpen;
          /* A native scalar plane uses scanner slot 3.  With RGB present this
             is infrared; standalone grayscale instead uses the visible-light
             fallback wavelength.  */
          if (!ir_simulation)
            image_layer_sharpen = m_params.get_sharpen_parameters_for_channel (
                3, m_img.has_rgb ());

          gray_and_sharpen_params p
              = { { m_img.id,
                    &m_img,
                    m_params.gamma,
                    { m_img.to_linear[0], m_img.to_linear[1],
                      m_img.to_linear[2] },
                    ir_simulation ? m_params.mix_dark
                                  : rgbdata{1.0f, 1.0f, 1.0f},
                    ir_simulation ? m_params.mix_red : 1.0f,
                    ir_simulation ? m_params.mix_green : 1.0f,
                    ir_simulation ? m_params.mix_blue : 1.0f,
                    m_backlight_correction.get (),
                    m_backlight_correction_id,
                    ir_simulation ? m_params.ignore_infrared : false },
                  image_layer_sharpen };
          m_image_layer_holder
              = gray_and_sharpened_data_cache.get (p, progress,
                                                    &m_image_layer_id);
          if (!m_image_layer_holder)
            return false;
          m_image_layer = m_image_layer_holder->m_data;
        }
    }
'''
write(path, text[:start] + new_block + text[end:])

# 5. Extend focused tests for LRU peek and one-hot channel selection/reuse.
path = "src/libcolorscreen/unittests.C"
old = '''  /* Verify true least-recently-used eviction.  The former comparison selected
     the newest free entry and therefore behaved as an MRU cache.  */
  lru_cache<test_params, int, get_new_test_fast, 2> eviction_cache (
      "test_eviction_cache");
  get_new_fast_calls = 0;
'''
new = '''  /* A cache peek is a pure lookup: it must not generate a missing value, and
     a completed value must be returned without another generator call.  */
  lru_cache<test_params, int, get_new_test_fast, 2> peek_cache (
      "test_peek_cache");
  get_new_fast_calls = 0;
  test_params peek_key = { 7 }, missing_key = { 8 };
  uint64_t generated_id = 0, peeked_id = 0;
  if (peek_cache.peek (peek_key) || get_new_fast_calls != 0)
    {
      printf ("LRU peek test FAIL: miss generated a value\n");
      ok = false;
    }
  std::shared_ptr<int> peek_value
      = peek_cache.get (peek_key, nullptr, &generated_id);
  if (!peek_value || *peek_value != 14 || get_new_fast_calls != 1)
    ok = false;
  peek_value.reset ();
  peek_value = peek_cache.peek (peek_key, &peeked_id);
  if (!peek_value || *peek_value != 14 || peeked_id != generated_id
      || get_new_fast_calls != 1 || peek_cache.peek (missing_key))
    {
      printf ("LRU peek test FAIL: completed lookup changed cache state\n");
      ok = false;
    }
  peek_value.reset ();

  /* Verify true least-recently-used eviction.  The former comparison selected
     the newest free entry and therefore behaved as an MRU cache.  */
  lru_cache<test_params, int, get_new_test_fast, 2> eviction_cache (
      "test_eviction_cache");
  get_new_fast_calls = 0;
'''
replace_once(path, old, new)

old = '''  /* An explicitly selected image-layer measurement retains the historical
     behavior of supplying one transfer curve to all native channels.  */
  render_parameters image_layer_params = params;
  mtf_measurement image_layer_measurement;
  image_layer_measurement.channel = -1;
  image_layer_measurement.name = "image layer";
'''
new = '''  /* A legacy unlabelled measurement retains its historical generic behavior
     of supplying one transfer curve to all native channels.  Explicit
     image-layer measurements are domain-labelled and are rejected here.  */
  render_parameters image_layer_params = params;
  mtf_measurement image_layer_measurement;
  image_layer_measurement.channel = -1;
  image_layer_measurement.name = "legacy unlabelled";
'''
replace_once(path, old, new)

# Insert one-hot specialization checks after native channel metadata checks.
needle = '''  const sharpen_parameters ir_sharpen
      = params.get_sharpen_parameters_for_channel (3);
  if (ir_sharpen.scanner_mtf.measured_mtf_idx != -1
      || ir_sharpen.scanner_mtf.wavelength != 850)
    {
      fprintf (stderr,
               "Missing IR measurement did not fall back to the IR model\\n");
      return false;
    }

'''
insert = needle + '''  /* An RGB-derived image layer with exactly one nonzero mix weight is exactly
     one native scanner channel, so scalar MTF specialization must inherit that
     channel's measured curve and wavelength.  A tiny second nonzero weight is
     deliberately no longer one-hot.  */
  render_parameters onehot_specialization = params;
  onehot_specialization.mix_red = 0;
  onehot_specialization.mix_green = 2;
  onehot_specialization.mix_blue = 0;
  if (onehot_specialization.get_image_layer_native_channel (&img) != 1
      || onehot_specialization.get_image_layer_channel (&img) != 1
      || onehot_specialization.get_image_layer_sharpen_parameters (&img)
                 .scanner_mtf.measured_mtf_idx
             != 1
      || onehot_specialization.get_image_layer_sharpen_parameters (&img)
                 .scanner_mtf.wavelength
             != onehot_specialization.sharpen.scanner_mtf.wavelengths[1])
    {
      fprintf (stderr, "One-hot image layer did not inherit green MTF\\n");
      return false;
    }
  onehot_specialization.mix_blue = (luminosity_t)1e-6;
  if (onehot_specialization.get_image_layer_native_channel (&img) != -1
      || onehot_specialization.get_image_layer_channel (&img) != 1
      || onehot_specialization.get_image_layer_sharpen_parameters (&img)
                 .scanner_mtf.measured_mtf_idx
             != -1)
    {
      fprintf (stderr, "Nonzero second mix channel was treated as one-hot\\n");
      return false;
    }

'''
replace_once(path, needle, insert)

# Add render/cache behavior regression after the general mixed-RGB equivalence.
needle = '''  if (fabs (actual_mix - expected_mix) > tolerance)
    {
      fprintf (stderr,
               "Image layer was not mixed from sharpened RGB channels: "
               "expected %.12g got %.12g\\n",
               expected_mix, actual_mix);
      return false;
    }

'''
insert = needle + '''  /* When only one native channel contributes, image-layer-only rendering must
     sharpen just that channel unless the matching full RGB result is already
     cached.  Use a unique SNR so the first request cannot hit the RGB cache
     populated above.  Non-unit gain and nonzero mix-dark verify that those
     operations remain after sharpening.  */
  render_parameters onehot_params = params;
  onehot_params.sharpen.scanner_snr = 211;
  onehot_params.mix_red = 0;
  onehot_params.mix_green = (luminosity_t)1.7;
  onehot_params.mix_blue = 0;
  onehot_params.mix_dark = { (luminosity_t)0.01, (luminosity_t)0.08,
                             (luminosity_t)0.03 };

  render onehot_scalar (img, onehot_params, 65535);
  if (!onehot_scalar.precompute_all (PRECOMPUTE_IMAGE_LAYER, {1, 1, 1},
                                     nullptr))
    {
      fprintf (stderr, "One-hot scalar sharpening precomputation failed\\n");
      return false;
    }
  const image_data::pixel raw_pixel = img.get_rgb_pixel (p.x, p.y);
  const double raw_blue = (double)raw_pixel.b / img.maxval;
  const double scalar_blue = onehot_scalar.get_linearized_data_blue (p);
  if (fabs (scalar_blue - raw_blue) > 1e-7)
    {
      fprintf (stderr,
               "One-hot scalar request unexpectedly materialized full RGB\\n");
      return false;
    }

  render onehot_full (img, onehot_params, 65535);
  if (!onehot_full.precompute_all (
          PRECOMPUTE_IMAGE_LAYER | PRECOMPUTE_RGB_IMAGE, {1, 1, 1}, nullptr))
    {
      fprintf (stderr, "One-hot full RGB sharpening precomputation failed\\n");
      return false;
    }
  const double scalar_only_value = onehot_scalar.get_unadjusted_data (p);
  const double full_value = onehot_full.get_unadjusted_data (p);
  const double full_blue = onehot_full.get_linearized_data_blue (p);
  if (fabs (scalar_only_value - full_value) > 5e-5
      || fabs (full_blue - raw_blue) < 1e-5)
    {
      fprintf (stderr,
               "One-hot scalar/full RGB mismatch: scalar %.12g full %.12g, "
               "blue raw %.12g sharpened %.12g\\n",
               scalar_only_value, full_value, raw_blue, full_blue);
      return false;
    }

  /* Keep ONEHOT_FULL alive so its completed cache value is pinned.  A second
     image-layer-only renderer should now peek that RGB result and reuse it,
     which is observable through an unrelated native channel accessor.  */
  render onehot_cached (img, onehot_params, 65535);
  if (!onehot_cached.precompute_all (PRECOMPUTE_IMAGE_LAYER, {1, 1, 1},
                                     nullptr))
    {
      fprintf (stderr, "Cached one-hot RGB reuse precomputation failed\\n");
      return false;
    }
  if (fabs (onehot_cached.get_unadjusted_data (p) - full_value) > 5e-5
      || fabs (onehot_cached.get_linearized_data_blue (p) - full_blue) > 5e-5)
    {
      fprintf (stderr, "One-hot image layer did not reuse cached RGB result\\n");
      return false;
    }

'''
replace_once(path, needle, insert)

# 6. Record the incremental one-hot completion in the tracking document.
path = "doc/mtf-channel-domain-tracking.md"
old = '''### MTF-DOM-005 — Monochrome original captured through RGB channels

**Status:** open optimization/correctness refinement for forward scalar models;
full-image `render.C` is already correct.
'''
new = '''### MTF-DOM-005 — Monochrome original captured through RGB channels

**Status:** partially fixed; exact one-channel mixes are handled, while general
weighted multi-channel forward models remain open.

An exact image-layer mix with only one nonzero RGB weight now inherits that
native channel's measured/analytical MTF.  Full-image rendering sharpens only
that channel when only the scalar image layer is requested; if the identical
full RGB sharpening is already cached, a non-generating cache peek reuses it.
When RGB output is requested explicitly, the normal three-channel pass remains
the shared source for both RGB and the image layer.

The one-hot test is exact rather than epsilon-based: any second nonzero mix
weight means that two different capture transfers contribute and must use the
general model below.
'''
replace_once(path, old, new)

# Update regression bullet to mention the exact special case.
old = '''- monochrome RGB forward modelling matches a slow weighted per-channel
  reference without introducing process-primary colour mixing;
'''
new = '''- exact one-channel RGB mixes inherit the selected native MTF and scalar-only
  rendering matches the full RGB sharpen-then-mix result, including cache reuse;
- general monochrome RGB forward modelling matches a slow weighted per-channel
  reference without introducing process-primary colour mixing;
'''
replace_once(path, old, new)

print("one-hot image-layer patch applied")
