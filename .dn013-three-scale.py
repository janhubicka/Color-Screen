from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    s = p.read_text()
    assert old in s, f"missing anchor in {path}: {old[:80]!r}"
    p.write_text(s.replace(old, new, 1))

# ---------------------------------------------------------------------------
# libcolorscreen/denoise.h: three-scale result, geometry, collection and APIs.
# ---------------------------------------------------------------------------
p = Path('src/libcolorscreen/denoise.h')
s = p.read_text()
anchor = '''struct denoise_noise_scale_estimate
{
  denoise_noise_estimate spacing1;
  denoise_noise_estimate spacing2;
  luminosity_t spacing2_variance_ratio = 0;
  size_t paired_observations = 0;

  bool valid_p () const
  {
    return spacing1.valid_p () && spacing2.valid_p ()
           && paired_observations >= 256
           && my_isfinite (spacing2_variance_ratio)
           && spacing2_variance_ratio > 0;
  }
};
'''
assert anchor in s
addition = anchor + '''
/* Diagnostic using the same center/direction at physical spacings 1, 2 and
   3.  The three robust variance statistics can additionally fit an empirical
   V(h) = N + A h^p law without assuming the smooth-structure exponent p=4.
   The power-law fit is deliberately diagnostic only and is left invalid when
   the scale-dependent excess is too small or non-monotone.  */
struct denoise_noise_three_scale_estimate
{
  denoise_noise_estimate spacing1;
  denoise_noise_estimate spacing2;
  denoise_noise_estimate spacing3;
  luminosity_t spacing2_variance_ratio = 0;
  luminosity_t spacing3_variance_ratio = 0;
  luminosity_t scale_growth_exponent = 0;
  luminosity_t extrapolated_scale_invariant_fraction = 0;
  size_t paired_observations = 0;
  bool scale_fit_valid = false;

  bool valid_p () const
  {
    return spacing1.valid_p () && spacing2.valid_p () && spacing3.valid_p ()
           && paired_observations >= 256
           && my_isfinite (spacing2_variance_ratio)
           && spacing2_variance_ratio > 0
           && my_isfinite (spacing3_variance_ratio)
           && spacing3_variance_ratio > 0;
  }

  bool scale_fit_valid_p () const
  {
    return valid_p () && scale_fit_valid
           && my_isfinite (scale_growth_exponent)
           && scale_growth_exponent > 0
           && my_isfinite (extrapolated_scale_invariant_fraction)
           && extrapolated_scale_invariant_fraction >= 0
           && extrapolated_scale_invariant_fraction <= 1;
  }
};
'''
s = s.replace(anchor, addition, 1)

anchor = '''template <typename GETSUPPORT, typename ENTRY_TO_SCR, typename ADD>
inline denoise_noise_estimate
collect (int width, int height, GETSUPPORT getsupport,
'''
assert anchor in s
helper = '''/* Check a seven-sample line whose +/-1, +/-2 and +/-3 offsets are all
   physically collinear and have exact integer spacing.  This excludes packed
   lattice zig-zags independently of their array-index appearance.  */
template <typename ENTRY_TO_SCR>
inline bool
second_difference_three_scale_geometry_p (
    int_point_t a3, int_point_t a2, int_point_t a1, int_point_t b,
    int_point_t c1, int_point_t c2, int_point_t c3, ENTRY_TO_SCR entry_to_scr)
{
  if (!second_difference_geometry_p (a1, b, c1, entry_to_scr)
      || !second_difference_geometry_p (a2, b, c2, entry_to_scr)
      || !second_difference_geometry_p (a3, b, c3, entry_to_scr))
    return false;

  point_t pb = entry_to_scr (b);
  point_t d1 = entry_to_scr (c1) - pb;
  point_t d2 = entry_to_scr (c2) - pb;
  point_t d3 = entry_to_scr (c3) - pb;
  coord_t scale = std::max ((coord_t)1,
                            std::max (my_fabs (d3.x), my_fabs (d3.y)));
  return my_fabs (d2.x - 2 * d1.x) <= scale * (coord_t)1e-7
         && my_fabs (d2.y - 2 * d1.y) <= scale * (coord_t)1e-7
         && my_fabs (d3.x - 3 * d1.x) <= scale * (coord_t)1e-7
         && my_fabs (d3.y - 3 * d1.y) <= scale * (coord_t)1e-7;
}

/* Fit an empirical N + A h^p law to three robust scale statistics.  N
   cancels from the ratio of successive increments, so p can be fitted without
   assuming quadratic smooth structure.  Require a visible monotone excess;
   near scale-invariant data intentionally leave the power-law fit undefined. */
inline bool
fit_three_scale_growth (luminosity_t v1, luminosity_t v2, luminosity_t v3,
                        luminosity_t *exponent, luminosity_t *noise_fraction)
{
  if (!(v1 > 0) || !my_isfinite (v1) || !my_isfinite (v2)
      || !my_isfinite (v3))
    return false;
  const luminosity_t d12 = v2 - v1;
  const luminosity_t d23 = v3 - v2;
  /* Five percent of the spacing-1 statistic is a conservative floor against
     fitting an exponent to median sampling jitter.  */
  if (d12 <= v1 * (luminosity_t)0.05
      || d23 <= v1 * (luminosity_t)0.05)
    return false;

  const long double target = (long double)d23 / d12;
  auto increment_ratio = [] (long double p)
  {
    long double p2 = std::pow ((long double)2, p);
    long double p3 = std::pow ((long double)3, p);
    return (p3 - p2) / (p2 - 1);
  };
  /* p -> 0 has the finite logarithmic limit log(3/2)/log(2).  */
  constexpr long double min_ratio = 0.58496250072115618145L;
  if (!(target > min_ratio) || target >= increment_ratio (8))
    return false;

  long double lo = 1e-6L, hi = 8;
  for (int i = 0; i < 80; i++)
    {
      long double mid = (lo + hi) * 0.5L;
      if (increment_ratio (mid) < target)
        lo = mid;
      else
        hi = mid;
    }
  const long double power = (lo + hi) * 0.5L;
  const long double p2 = std::pow ((long double)2, power);
  const long double a = d12 / (p2 - 1);
  const long double n = (long double)v1 - a;
  if (!(n >= 0) || !std::isfinite ((double)n))
    return false;
  if (exponent)
    *exponent = (luminosity_t)power;
  if (noise_fraction)
    *noise_fraction = (luminosity_t)std::clamp (n / (long double)v1,
                                                (long double)0,
                                                (long double)1);
  return true;
}

'''
s = s.replace(anchor, helper + anchor, 1)

anchor = '''  result.spacing1 = fit (spacing1, min_support);
  result.spacing2 = fit (spacing2, min_support);
  return result;
}
} // namespace denoise_noise_estimator_detail
'''
assert anchor in s
three_collect = '''  result.spacing1 = fit (spacing1, min_support);
  result.spacing2 = fit (spacing2, min_support);
  return result;
}

template <typename GETSUPPORT, typename ENTRY_TO_SCR, typename ADD>
inline denoise_noise_three_scale_estimate
collect_three_scales (int width, int height, GETSUPPORT getsupport,
                      ENTRY_TO_SCR entry_to_scr, ADD add)
{
  denoise_noise_three_scale_estimate result;
  std::vector<observation> spacing1, spacing2, spacing3;
  if (width < 7 && height < 7)
    return result;

  luminosity_t min_support = support_threshold (width, height, getsupport);
  const size_t possible = (size_t)height * std::max (0, width - 6)
                          + (size_t)width * std::max (0, height - 6);
  const size_t stride = std::max<size_t> (1, possible / 200000);
  const size_t reserve = std::min<size_t> (possible, 200000);
  spacing1.reserve (reserve);
  spacing2.reserve (reserve);
  spacing3.reserve (reserve);
  size_t serial = 0;

  auto consider = [&] (int_point_t a3, int_point_t a2, int_point_t a1,
                       int_point_t b, int_point_t c1, int_point_t c2,
                       int_point_t c3)
  {
    if (serial++ % stride)
      return;
    if (!second_difference_three_scale_geometry_p (
            a3, a2, a1, b, c1, c2, c3, entry_to_scr))
      return;
    int_point_t points[7] = {a3, a2, a1, b, c1, c2, c3};
    for (int_point_t point : points)
      {
        luminosity_t v = getsupport ((int)point.x, (int)point.y);
        if (!my_isfinite (v) || v < min_support)
          return;
      }
    add (spacing1, spacing2, spacing3, a3, a2, a1, b, c1, c2, c3);
  };

  for (int y = 0; y < height; y++)
    for (int x = 3; x + 3 < width; x++)
      consider ({x - 3, y}, {x - 2, y}, {x - 1, y}, {x, y},
                {x + 1, y}, {x + 2, y}, {x + 3, y});
  for (int y = 3; y + 3 < height; y++)
    for (int x = 0; x < width; x++)
      consider ({x, y - 3}, {x, y - 2}, {x, y - 1}, {x, y},
                {x, y + 1}, {x, y + 2}, {x, y + 3});

  if (spacing1.size () != spacing2.size ()
      || spacing1.size () != spacing3.size ())
    return result;
  result.paired_observations = spacing1.size ();

  if (!spacing1.empty ())
    {
      std::vector<luminosity_t> v1, v2, v3;
      v1.reserve (spacing1.size ());
      v2.reserve (spacing2.size ());
      v3.reserve (spacing3.size ());
      for (size_t i = 0; i < spacing1.size (); i++)
        {
          v1.push_back (spacing1[i].variance);
          v2.push_back (spacing2[i].variance);
          v3.push_back (spacing3[i].variance);
        }
      luminosity_t m1 = median (v1);
      luminosity_t m2 = median (v2);
      luminosity_t m3 = median (v3);
      if (m1 > 0 && my_isfinite (m1) && my_isfinite (m2)
          && my_isfinite (m3))
        {
          result.spacing2_variance_ratio = m2 / m1;
          result.spacing3_variance_ratio = m3 / m1;
          result.scale_fit_valid = fit_three_scale_growth (
              m1, m2, m3, &result.scale_growth_exponent,
              &result.extrapolated_scale_invariant_fraction);
        }
    }

  result.spacing1 = fit (spacing1, min_support);
  result.spacing2 = fit (spacing2, min_support);
  result.spacing3 = fit (spacing3, min_support);
  return result;
}
} // namespace denoise_noise_estimator_detail
'''
s = s.replace(anchor, three_collect, 1)

anchor = '''template <typename ENTRY_TO_SCR>
inline bool
denoise_screen_offset_in_square'''
assert anchor in s
apis = '''/* Three-scale scalar diagnostic.  A common seven-sample mean is orthogonal
   to the spacing-1/2/3 second-difference coefficient vectors.  */
template <typename GETDATA, typename GETSUPPORT, typename ENTRY_TO_SCR>
inline denoise_noise_three_scale_estimate
estimate_screen_noise_three_scale_model (int width, int height, GETDATA getdata,
                                         GETSUPPORT getsupport,
                                         ENTRY_TO_SCR entry_to_scr)
{
  using namespace denoise_noise_estimator_detail;
  return collect_three_scales (
      width, height, getsupport, entry_to_scr,
      [&] (std::vector<observation> &spacing1,
           std::vector<observation> &spacing2,
           std::vector<observation> &spacing3, int_point_t a3,
           int_point_t a2, int_point_t a1, int_point_t b, int_point_t c1,
           int_point_t c2, int_point_t c3)
      {
        luminosity_t v[7] = {
          getdata ((int)a3.x, (int)a3.y), getdata ((int)a2.x, (int)a2.y),
          getdata ((int)a1.x, (int)a1.y), getdata ((int)b.x, (int)b.y),
          getdata ((int)c1.x, (int)c1.y), getdata ((int)c2.x, (int)c2.y),
          getdata ((int)c3.x, (int)c3.y)};
        for (luminosity_t x : v)
          if (!my_isfinite (x))
            return;
        luminosity_t signal = 0;
        for (luminosity_t x : v)
          signal += x;
        signal = std::max (signal / (luminosity_t)7, (luminosity_t)0);
        luminosity_t d1 = v[2] - 2 * v[3] + v[4];
        luminosity_t d2 = v[1] - 2 * v[3] + v[5];
        luminosity_t d3 = v[0] - 2 * v[3] + v[6];
        spacing1.push_back ({signal, d1 * d1 / (luminosity_t)6});
        spacing2.push_back ({signal, d2 * d2 / (luminosity_t)6});
        spacing3.push_back ({signal, d3 * d3 / (luminosity_t)6});
      });
}

template <typename GETDATA, typename GETSUPPORT, typename ENTRY_TO_SCR>
inline denoise_noise_three_scale_estimate
estimate_screen_rgb_noise_three_scale_model (
    int width, int height, GETDATA getdata, GETSUPPORT getsupport,
    ENTRY_TO_SCR entry_to_scr)
{
  using namespace denoise_noise_estimator_detail;
  return collect_three_scales (
      width, height, getsupport, entry_to_scr,
      [&] (std::vector<observation> &spacing1,
           std::vector<observation> &spacing2,
           std::vector<observation> &spacing3, int_point_t a3,
           int_point_t a2, int_point_t a1, int_point_t b, int_point_t c1,
           int_point_t c2, int_point_t c3)
      {
        rgbdata v[7] = {
          getdata ((int)a3.x, (int)a3.y), getdata ((int)a2.x, (int)a2.y),
          getdata ((int)a1.x, (int)a1.y), getdata ((int)b.x, (int)b.y),
          getdata ((int)c1.x, (int)c1.y), getdata ((int)c2.x, (int)c2.y),
          getdata ((int)c3.x, (int)c3.y)};
        for (int k = 0; k < 3; k++)
          {
            bool finite = true;
            for (const rgbdata &sample : v)
              finite &= my_isfinite (sample[k]);
            if (!finite)
              continue;
            luminosity_t signal = 0;
            for (const rgbdata &sample : v)
              signal += sample[k];
            signal = std::max (signal / (luminosity_t)7, (luminosity_t)0);
            luminosity_t d1 = v[2][k] - 2 * v[3][k] + v[4][k];
            luminosity_t d2 = v[1][k] - 2 * v[3][k] + v[5][k];
            luminosity_t d3 = v[0][k] - 2 * v[3][k] + v[6][k];
            spacing1.push_back ({signal, d1 * d1 / (luminosity_t)6});
            spacing2.push_back ({signal, d2 * d2 / (luminosity_t)6});
            spacing3.push_back ({signal, d3 * d3 / (luminosity_t)6});
          }
      });
}

'''
s = s.replace(anchor, apis + anchor, 1)
p.write_text(s)

# ---------------------------------------------------------------------------
# analyze_base API.
# ---------------------------------------------------------------------------
replace_once('src/libcolorscreen/analyze-base.h',
             'struct denoise_noise_scale_estimate;\n',
             'struct denoise_noise_scale_estimate;\nstruct denoise_noise_three_scale_estimate;\n')
p = Path('src/libcolorscreen/analyze-base.h')
s = p.read_text()
anchor = '''  bool estimate_noise_scale_models (denoise_noise_scale_estimate *red,
                                    denoise_noise_scale_estimate *green,
                                    denoise_noise_scale_estimate *blue) const;
'''
assert anchor in s
s = s.replace(anchor, anchor + '''
  /* Compare identical centers/directions at physical spacings 1, 2 and 3 and
     optionally fit an empirical scale-growth exponent.  Diagnostic only.  */
  bool estimate_noise_three_scale_models (
      denoise_noise_three_scale_estimate *red,
      denoise_noise_three_scale_estimate *green,
      denoise_noise_three_scale_estimate *blue) const;
''', 1)
p.write_text(s)

p = Path('src/libcolorscreen/analyze-base.C')
s = p.read_text()
anchor = '''luminosity_t
analyze_base::red_collection_support'''
assert anchor in s
func = '''bool
analyze_base::estimate_noise_three_scale_models (
    denoise_noise_three_scale_estimate *red_est,
    denoise_noise_three_scale_estimate *green_est,
    denoise_noise_three_scale_estimate *blue_est) const
{
  auto scalar = [&] (const luminosity_t *data, const luminosity_t *support,
                     int wscl, int hscl, entry_to_scr_fn entry_to_scr)
  {
    if (!data)
      return denoise_noise_three_scale_estimate{};
    const int w = m_area.width << wscl;
    const int h = m_area.height << hscl;
    return estimate_screen_noise_three_scale_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y)
        { return support ? support[(size_t)y * w + x] : (luminosity_t)1; },
        entry_to_scr);
  };
  auto rgb = [&] (const rgbdata *data, const luminosity_t *support,
                  int wscl, int hscl, entry_to_scr_fn entry_to_scr)
  {
    if (!data)
      return denoise_noise_three_scale_estimate{};
    const int w = m_area.width << wscl;
    const int h = m_area.height << hscl;
    return estimate_screen_rgb_noise_three_scale_model (
        w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
        [&] (int x, int y)
        { return support ? support[(size_t)y * w + x] : (luminosity_t)1; },
        entry_to_scr);
  };

  denoise_noise_three_scale_estimate r, g, b;
  if (m_rgb_red || m_rgb_green || m_rgb_blue)
    {
      r = rgb (m_rgb_red.get (), m_red_support.get (), m_rwscl, m_rhscl,
               m_red_entry_to_scr);
      g = rgb (m_rgb_green.get (), m_green_support.get (), m_gwscl, m_ghscl,
               m_green_entry_to_scr);
      b = rgb (m_rgb_blue.get (), m_blue_support.get (), m_bwscl, m_bhscl,
               m_blue_entry_to_scr);
    }
  else
    {
      r = scalar (m_red.get (), m_red_support.get (), m_rwscl, m_rhscl,
                  m_red_entry_to_scr);
      g = scalar (m_green.get (), m_green_support.get (), m_gwscl, m_ghscl,
                  m_green_entry_to_scr);
      b = scalar (m_blue.get (), m_blue_support.get (), m_bwscl, m_bhscl,
                  m_blue_entry_to_scr);
    }
  if (red_est)
    *red_est = r;
  if (green_est)
    *green_est = g;
  if (blue_est)
    *blue_est = b;
  return r.valid_p () || g.valid_p () || b.valid_p ();
}

'''
s = s.replace(anchor, func + anchor, 1)
p.write_text(s)

# ---------------------------------------------------------------------------
# render_interpolate API.
# ---------------------------------------------------------------------------
replace_once('src/libcolorscreen/render-interpolate.h',
             'struct denoise_noise_scale_estimate;\n',
             'struct denoise_noise_scale_estimate;\nstruct denoise_noise_three_scale_estimate;\n')
p = Path('src/libcolorscreen/render-interpolate.h')
s = p.read_text()
anchor = '''  bool estimate_screen_noise_scale_models (denoise_noise_scale_estimate *red,
                                           denoise_noise_scale_estimate *green,
                                           denoise_noise_scale_estimate *blue) const;
'''
assert anchor in s
s = s.replace(anchor, anchor + '''  bool estimate_screen_noise_three_scale_models (
      denoise_noise_three_scale_estimate *red,
      denoise_noise_three_scale_estimate *green,
      denoise_noise_three_scale_estimate *blue) const;
''', 1)
p.write_text(s)

p = Path('src/libcolorscreen/render-interpolate.C')
s = p.read_text()
anchor = '''bool
render_interpolate::analyze_patches'''
assert anchor in s
func = '''bool
render_interpolate::estimate_screen_noise_three_scale_models (
    denoise_noise_three_scale_estimate *red,
    denoise_noise_three_scale_estimate *green,
    denoise_noise_three_scale_estimate *blue) const
{
  if (m_paget)
    return m_paget->estimate_noise_three_scale_models (red, green, blue);
  if (m_dufay)
    return m_dufay->estimate_noise_three_scale_models (red, green, blue);
  if (m_strips)
    return m_strips->estimate_noise_three_scale_models (red, green, blue);
  return false;
}

'''
s = s.replace(anchor, func + anchor, 1)
p.write_text(s)

# ---------------------------------------------------------------------------
# Unit regressions: extend existing scalar/RGB/Paget/curvature fixtures.
# ---------------------------------------------------------------------------
p = Path('src/libcolorscreen/unittests.C')
s = p.read_text()

# Scalar noise-only block.
needle = '''        return false;
      }
  }

  /* Precise-RGB estimation pools the three scanner components but must
'''
pos = s.find('"Two-scale noise estimator mismatch: ratio %g paired %zu "')
assert pos >= 0
end = s.find(needle, pos)
assert end >= 0
replacement = '''        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            identity_to_scr);
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.5
        || three.spacing2_variance_ratio > (luminosity_t)2
        || three.spacing3_variance_ratio < (luminosity_t)0.4
        || three.spacing3_variance_ratio > (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "Three-scale noise estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }
  }

  /* Precise-RGB estimation pools the three scanner components but must
'''
s = s[:end] + s[end:].replace(needle, replacement, 1)

# RGB noise-only block.
needle = '''        return false;
      }
  }

  /* Packed screen geometries must only use equally spaced physical triples.
'''
pos = s.find('"RGB two-scale estimator mismatch: ratio %g paired %zu\\n"')
assert pos >= 0
end = s.find(needle, pos)
assert end >= 0
replacement = '''        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_rgb_noise_three_scale_model (
            w, h,
            [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.5
        || three.spacing2_variance_ratio > (luminosity_t)2
        || three.spacing3_variance_ratio < (luminosity_t)0.4
        || three.spacing3_variance_ratio > (luminosity_t)2.5)
      {
        fprintf (stderr,
                 "RGB three-scale estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }
  }

  /* Packed screen geometries must only use equally spaced physical triples.
'''
s = s[:end] + s[end:].replace(needle, replacement, 1)

# Paget packed geometry block.
needle = '''        return false;
      }
  }

  /* A smooth quadratic signal is deliberately not noise.  Its second
'''
pos = s.find('"Paget two-scale estimator mismatch: ratio %g paired %zu\\n"')
assert pos >= 0
end = s.find(needle, pos)
assert end >= 0
replacement = '''        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p) { return paget_geometry::red_entry_to_scr (p); });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)0.35
        || three.spacing2_variance_ratio > (luminosity_t)2.7
        || three.spacing3_variance_ratio < (luminosity_t)0.25
        || three.spacing3_variance_ratio > (luminosity_t)3.5)
      {
        fprintf (stderr,
                 "Paget three-scale estimator mismatch: ratios %g %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 three.paired_observations);
        return false;
      }
  }

  /* A smooth quadratic signal is deliberately not noise.  Its second
'''
s = s[:end] + s[end:].replace(needle, replacement, 1)

# Quadratic field: should recover exponent near four.
needle = '''        return false;
      }
  }


  /* A bilateral filter needs a border matching its spatial kernel, not the
'''
pos = s.find('"Two-scale estimator failed to detect curvature: ratio %g "')
assert pos >= 0
end = s.find(needle, pos)
assert end >= 0
replacement = '''        return false;
      }

    denoise_noise_three_scale_estimate three
        = estimate_screen_noise_three_scale_model (
            w, h, [&] (int x, int y) { return data[(size_t)y * w + x]; },
            [&] (int x, int y) { return support[(size_t)y * w + x]; },
            [] (int_point_t p)
            { return point_t{(coord_t)p.x, (coord_t)p.y}; });
    if (!three.valid_p ()
        || three.spacing2_variance_ratio < (luminosity_t)2.5
        || three.spacing3_variance_ratio < (luminosity_t)8
        || !three.scale_fit_valid_p ()
        || three.scale_growth_exponent < (luminosity_t)2.5
        || three.scale_growth_exponent > (luminosity_t)5.5
        || three.extrapolated_scale_invariant_fraction > (luminosity_t)0.5)
      {
        fprintf (stderr,
                 "Three-scale curvature fit mismatch: ratios %g %g exponent %g "
                 "noise fraction %g paired %zu\n",
                 (double)three.spacing2_variance_ratio,
                 (double)three.spacing3_variance_ratio,
                 (double)three.scale_growth_exponent,
                 (double)three.extrapolated_scale_invariant_fraction,
                 three.paired_observations);
        return false;
      }
  }


  /* A bilateral filter needs a border matching its spatial kernel, not the
'''
s = s[:end] + s[end:].replace(needle, replacement, 1)
p.write_text(s)

print('DN-013 three-scale source transformation applied')
